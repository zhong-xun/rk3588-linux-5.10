package update

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"time"

	"rknetupdate/internal/bundlecontrol"
	"rknetupdate/internal/recoveryapply"
	"rknetupdate/internal/rkimage"
)

const bundleManifestName = recoveryapply.BundleManifestName

// bundleUploadStream lets the HTTP layer keep multipart input streaming while
// the manager owns all filesystem writes and transaction cleanup.
type bundleUploadStream func(add func(filename string, reader io.Reader) error) error

type bundleWorkflow interface {
	Prepare(context.Context, string) (bundlecontrol.Result, error)
	VerifyPrepared(context.Context, string) (bundlecontrol.Result, error)
	Reboot(context.Context) error
}

type systemBundleWorkflow struct{ testMode bool }

func (w systemBundleWorkflow) Prepare(ctx context.Context, confirmation string) (bundlecontrol.Result, error) {
	cfg := bundlecontrol.DefaultConfig()
	cfg.AllowWrite = true
	cfg.TestMode = w.testMode
	cfg.Confirmation = confirmation
	return bundlecontrol.Prepare(ctx, cfg)
}

func (w systemBundleWorkflow) VerifyPrepared(ctx context.Context, confirmation string) (bundlecontrol.Result, error) {
	cfg := bundlecontrol.DefaultConfig()
	cfg.AllowWrite = true
	cfg.TestMode = w.testMode
	cfg.Confirmation = confirmation
	return bundlecontrol.VerifyPrepared(ctx, cfg)
}

func (systemBundleWorkflow) Reboot(ctx context.Context) error {
	return systemRootfsWorkflow{}.Reboot(ctx)
}

func (m *Manager) bundleStageManifestPath() string {
	return filepath.Join(m.cfg.BundleStageDir, bundleManifestName)
}

func (m *Manager) bundleStageVacant() error {
	entries, err := os.ReadDir(m.cfg.BundleStageDir)
	if err != nil {
		return fmt.Errorf("inspect bundle staging directory: %w", err)
	}
	if len(entries) != 0 {
		return errors.New("bundle staging directory is not empty; retain it for recovery inspection or remove a known-stale bundle before a new upload")
	}
	return nil
}

// BeginBundleUpload stages rootfs.img and optional boot.img/uboot.img as one
// recovery transaction.  It is intentionally separate from BeginUpload:
// rootfs is never opened for writing by the normal system.
func (m *Manager) BeginBundleUpload(ctx context.Context, stream bundleUploadStream) (*Job, error) {
	if !m.cfg.BundleEnabled {
		return nil, errors.New("offline multi-image bundle workflow is disabled")
	}
	if stream == nil {
		return nil, errors.New("bundle upload stream is required")
	}
	if err := m.bundleStageVacant(); err != nil {
		return nil, err
	}
	targets, err := m.resolveBundleTargets()
	if err != nil {
		return nil, err
	}
	if !m.cfg.TestMode && filepath.Clean(m.cfg.ExpectedParentDevice) != bundlecontrol.ProductionParent {
		return nil, fmt.Errorf("bundle recovery requires expected parent device %s", bundlecontrol.ProductionParent)
	}
	rootfs := targets[TargetRootfs]
	now := time.Now().UTC()
	jobID, err := randomToken(12)
	if err != nil {
		return nil, err
	}
	confirmation, err := randomToken(24)
	if err != nil {
		return nil, err
	}
	job := &Job{
		ID:             jobID,
		Target:         TargetBundle,
		Filename:       bundleManifestName,
		Device:         rootfs.Configured,
		ResolvedDevice: rootfs.Path,
		ParentDevice:   rootfs.ParentPath,
		DeviceSize:     rootfs.Size,
		Status:         StatusUploading,
		DryRun:         m.dryRunForTarget(TargetBundle),
		CreatedAt:      now,
		UpdatedAt:      now,
	}

	m.mu.Lock()
	m.expireLocked(now)
	if m.busy {
		m.mu.Unlock()
		return nil, ErrBusy
	}
	m.busy = true
	m.job = job
	m.mu.Unlock()

	manifestPath, total, manifestSHA, artifacts, validation, err := m.storeBundleUpload(ctx, stream, targets)
	if err != nil {
		m.clearBundleStage()
		m.failUpload(job.ID, err)
		return nil, err
	}
	m.mu.Lock()
	job.manifestPath = manifestPath
	job.Size = total
	job.SHA256 = manifestSHA
	job.Artifacts = artifacts
	job.Validation = validation
	job.Confirmation = confirmation
	job.Status = StatusAwaiting
	job.Progress = 100
	job.confirmationEnds = time.Now().UTC().Add(m.cfg.ConfirmTTL)
	job.UpdatedAt = time.Now().UTC()
	response := cloneJob(job, false)
	m.mu.Unlock()
	m.logger.Info("offline bundle staged", "job_id", job.ID, "targets", bundleTargetsString(artifacts), "parent_device", rootfs.ParentPath, "size", total, "manifest_sha256", manifestSHA)
	return response, nil
}

// BeginAndExecuteBundleUpload is the one-click counterpart to
// BeginBundleUpload.  It first streams and validates every bundle member,
// then automatically prepares and verifies recovery before requesting a
// clean reboot when live writes are enabled.
func (m *Manager) BeginAndExecuteBundleUpload(ctx context.Context, stream bundleUploadStream) (*Job, error) {
	job, err := m.BeginBundleUpload(ctx, stream)
	if err != nil {
		return nil, err
	}
	return m.ExecuteStaged(ctx, job.ID)
}

func (m *Manager) resolveBundleTargets() (map[Target]ResolvedTarget, error) {
	targets := make(map[Target]ResolvedTarget, 3)
	for _, target := range []Target{TargetRootfs, TargetBoot, TargetUboot} {
		resolved, err := ResolveTarget(m.targetPath(target), m.cfg.TestMode)
		if err != nil {
			return nil, fmt.Errorf("resolve bundle %s target: %w", target, err)
		}
		if err := m.validateParent(resolved); err != nil {
			return nil, fmt.Errorf("validate bundle %s target: %w", target, err)
		}
		targets[target] = resolved
	}
	return targets, nil
}

func (m *Manager) storeBundleUpload(ctx context.Context, stream bundleUploadStream, targets map[Target]ResolvedTarget) (string, int64, string, []Artifact, Validation, error) {
	if err := m.bundleStageVacant(); err != nil {
		return "", 0, "", nil, Validation{}, err
	}
	artifacts := make(map[Target]Artifact, 3)
	var total int64
	err := stream(func(filename string, reader io.Reader) error {
		target, ok := targetFromBundleFilename(filename)
		if !ok {
			return fmt.Errorf("bundle accepts only rootfs.img, boot.img, or uboot.img; got %q", filename)
		}
		if _, exists := artifacts[target]; exists {
			return fmt.Errorf("bundle contains duplicate %s", filename)
		}
		artifact, err := m.storeBundleArtifact(ctx, target, reader, targets[target])
		if err != nil {
			return err
		}
		if total > m.cfg.MaxBundleUploadBytes-artifact.Size {
			return fmt.Errorf("bundle exceeds maximum upload size of %d bytes", m.cfg.MaxBundleUploadBytes)
		}
		total += artifact.Size
		artifacts[target] = artifact
		return nil
	})
	if err != nil {
		return "", 0, "", nil, Validation{}, err
	}
	if _, ok := artifacts[TargetRootfs]; !ok {
		return "", 0, "", nil, Validation{}, errors.New("offline bundle must contain rootfs.img; use the direct target workflow for boot.img or uboot.img alone")
	}
	ordered := make([]Artifact, 0, len(artifacts))
	manifest := recoveryapply.BundleManifest{Schema: recoveryapply.BundleSchema, ExpectedParent: m.cfg.ExpectedParentDevice}
	for _, target := range []Target{TargetRootfs, TargetBoot, TargetUboot} {
		artifact, ok := artifacts[target]
		if !ok {
			continue
		}
		ordered = append(ordered, artifact)
		entry := recoveryapply.BundleArtifact{Target: string(target), Image: artifact.Filename, Size: artifact.Size, SHA256: artifact.SHA256}
		if target == TargetRootfs {
			if artifact.Filesystem != "ext4" || artifact.FilesystemUUID == "" || artifact.BlockSize == 0 || artifact.BlockCount == 0 {
				return "", 0, "", nil, Validation{}, errors.New("rootfs bundle artifact is missing ext4 geometry")
			}
			entry.Filesystem = "ext4"
			entry.FilesystemUUID = artifact.FilesystemUUID
			entry.BlockSize = artifact.BlockSize
			entry.BlockCount = artifact.BlockCount
		}
		manifest.Artifacts = append(manifest.Artifacts, entry)
	}
	if err := manifest.Validate(); err != nil {
		return "", 0, "", nil, Validation{}, fmt.Errorf("validate generated bundle manifest: %w", err)
	}
	manifestPath := m.bundleStageManifestPath()
	if err := publishBundleManifest(m.cfg.BundleStageDir, manifestPath, manifest); err != nil {
		return "", 0, "", nil, Validation{}, err
	}
	if err := syncDirectory(m.cfg.BundleStageDir); err != nil {
		return "", 0, "", nil, Validation{}, fmt.Errorf("sync bundle staging directory: %w", err)
	}
	manifestSHA, _, err := recoveryapply.HashBundleManifest(manifestPath, m.cfg.TestMode)
	if err != nil {
		return "", 0, "", nil, Validation{}, fmt.Errorf("hash staged bundle manifest: %w", err)
	}
	return manifestPath, total, manifestSHA, ordered, Validation{
		DetectedType: "rknetupdate-bundle-v1",
		Confidence:   "high",
		Summary:      "已分别校验并绑定离线 recovery bundle：" + bundleTargetsString(ordered),
	}, nil
}

func rootfsValidation(ext4 recoveryapply.Ext4Info) Validation {
	return Validation{
		DetectedType: "ext4",
		Confidence:   "high",
		Summary: fmt.Sprintf("已验证 ext4：UUID %s，%d x %d-byte blocks；完整 e2fsck 将由 recovery 执行",
			ext4.UUID, ext4.BlockCount, ext4.BlockSize),
	}
}

func (m *Manager) storeBundleArtifact(ctx context.Context, target Target, reader io.Reader, device ResolvedTarget) (Artifact, error) {
	limit := m.uploadLimit(target)
	tmp, err := os.CreateTemp(m.cfg.BundleStageDir, ".bundle-"+string(target)+"-*.img")
	if err != nil {
		return Artifact{}, fmt.Errorf("create bundle %s image: %w", target, err)
	}
	tmpPath := tmp.Name()
	cleanup := true
	defer func() {
		_ = tmp.Close()
		if cleanup {
			_ = os.Remove(tmpPath)
		}
	}()
	if err := tmp.Chmod(0600); err != nil {
		return Artifact{}, fmt.Errorf("set bundle %s image mode: %w", target, err)
	}
	hash := sha256.New()
	written, err := io.Copy(io.MultiWriter(tmp, hash), &contextReader{ctx: ctx, r: io.LimitReader(reader, limit+1)})
	if err != nil {
		return Artifact{}, fmt.Errorf("store bundle %s image: %w", target, err)
	}
	if written == 0 {
		return Artifact{}, fmt.Errorf("bundle %s image is empty", target)
	}
	if written > limit {
		return Artifact{}, fmt.Errorf("bundle %s image exceeds maximum upload size of %d bytes", target, limit)
	}
	if written > device.Size {
		return Artifact{}, fmt.Errorf("bundle %s image size %d exceeds partition size %d", target, written, device.Size)
	}
	if err := tmp.Sync(); err != nil {
		return Artifact{}, fmt.Errorf("sync bundle %s image: %w", target, err)
	}
	headerSize := int64(64 << 10)
	if written < headerSize {
		headerSize = written
	}
	header := make([]byte, headerSize)
	if _, err := tmp.ReadAt(header, 0); err != nil && !errors.Is(err, io.EOF) {
		return Artifact{}, fmt.Errorf("inspect bundle %s image: %w", target, err)
	}
	if bytes.HasPrefix(header, rkimage.ContainerMagic[:]) {
		if _, err := rkimage.InspectFile(tmpPath); err != nil {
			return Artifact{}, fmt.Errorf("validate Rockchip update.img container: %w", err)
		}
		return Artifact{}, errors.New("Rockchip update.img is a multi-part recovery package; upload its rootfs.img, boot.img, and uboot.img members as separate bundle files")
	}
	var validation Validation
	var filesystem string
	var filesystemUUID string
	var blockSize, blockCount uint64
	if target == TargetRootfs {
		ext4, err := recoveryapply.InspectExt4(tmp, written)
		if err != nil {
			return Artifact{}, fmt.Errorf("validate bundle rootfs ext4: %w", err)
		}
		validation = rootfsValidation(ext4)
		filesystem = "ext4"
		filesystemUUID = ext4.UUID
		blockSize = ext4.BlockSize
		blockCount = ext4.BlockCount
	} else {
		var err error
		validation, err = InspectImage(target, header)
		if err != nil {
			return Artifact{}, err
		}
	}
	if err := tmp.Close(); err != nil {
		return Artifact{}, fmt.Errorf("close bundle %s image: %w", target, err)
	}
	finalPath := filepath.Join(m.cfg.BundleStageDir, target.Filename())
	if err := os.Link(tmpPath, finalPath); err != nil {
		return Artifact{}, fmt.Errorf("publish bundle %s image: %w", target, err)
	}
	if err := os.Remove(tmpPath); err != nil {
		return Artifact{}, fmt.Errorf("remove temporary bundle %s image: %w", target, err)
	}
	cleanup = false
	return Artifact{
		Target:         target,
		Filename:       target.Filename(),
		Size:           written,
		SHA256:         hex.EncodeToString(hash.Sum(nil)),
		Device:         device.Configured,
		ResolvedPath:   device.Path,
		DeviceSize:     device.Size,
		Filesystem:     filesystem,
		FilesystemUUID: filesystemUUID,
		BlockSize:      blockSize,
		BlockCount:     blockCount,
		Validation:     validation,
	}, nil
}

func targetFromBundleFilename(filename string) (Target, bool) {
	if filename != filepath.Base(filename) {
		return "", false
	}
	for _, target := range []Target{TargetRootfs, TargetBoot, TargetUboot} {
		if filename == target.Filename() {
			return target, true
		}
	}
	return "", false
}

func publishBundleManifest(stageDir, manifestPath string, manifest recoveryapply.BundleManifest) error {
	tmp, err := os.CreateTemp(stageDir, ".bundle-manifest-*.json")
	if err != nil {
		return fmt.Errorf("create bundle manifest: %w", err)
	}
	tmpPath := tmp.Name()
	cleanup := true
	defer func() {
		_ = tmp.Close()
		if cleanup {
			_ = os.Remove(tmpPath)
		}
	}()
	if err := tmp.Chmod(0600); err != nil {
		return fmt.Errorf("set bundle manifest mode: %w", err)
	}
	encoder := json.NewEncoder(tmp)
	if err := encoder.Encode(manifest); err != nil {
		return fmt.Errorf("encode bundle manifest: %w", err)
	}
	if err := tmp.Sync(); err != nil {
		return fmt.Errorf("sync bundle manifest: %w", err)
	}
	if err := tmp.Close(); err != nil {
		return fmt.Errorf("close bundle manifest: %w", err)
	}
	if err := os.Link(tmpPath, manifestPath); err != nil {
		return fmt.Errorf("publish bundle manifest: %w", err)
	}
	if err := os.Remove(tmpPath); err != nil {
		return fmt.Errorf("remove temporary bundle manifest: %w", err)
	}
	cleanup = false
	return nil
}

func (m *Manager) clearBundleStage() {
	for _, name := range []string{recoveryapply.BundleRootfsImage, recoveryapply.BundleBootImage, recoveryapply.BundleUbootImage, bundleManifestName} {
		_ = os.Remove(filepath.Join(m.cfg.BundleStageDir, name))
	}
}

func bundleTargetsString(artifacts []Artifact) string {
	values := make([]string, 0, len(artifacts))
	for _, artifact := range artifacts {
		values = append(values, artifact.Filename)
	}
	return strings.Join(values, ", ")
}
