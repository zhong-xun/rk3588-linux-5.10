package update

import (
	"bytes"
	"context"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"log/slog"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"syscall"
	"time"

	"rknetupdate/internal/bundlecontrol"
	"rknetupdate/internal/rkimage"
	"rknetupdate/internal/rootfscontrol"
	"rknetupdate/internal/updateimgcontrol"
)

var ErrBusy = errors.New("another update job is active")

type Manager struct {
	cfg    Config
	logger *slog.Logger

	mu        sync.Mutex
	busy      bool
	job       *Job
	wg        sync.WaitGroup
	rootfs    rootfsWorkflow
	bundle    bundleWorkflow
	updateimg updateImageWorkflow
	reboot    rebootWorkflow
}

func NewManager(cfg Config, logger *slog.Logger) (*Manager, error) {
	if logger == nil {
		logger = slog.Default()
	}
	if err := ensureSafeUploadDir(cfg.UploadDir); err != nil {
		return nil, err
	}
	if cfg.RootfsEnabled {
		if err := ensureSafeUploadDir(cfg.RootfsStageDir); err != nil {
			return nil, fmt.Errorf("initialize rootfs staging directory: %w", err)
		}
		info, err := os.Stat(cfg.RootfsE2fsckPath)
		if err != nil {
			return nil, fmt.Errorf("inspect rootfs e2fsck: %w", err)
		}
		if !info.Mode().IsRegular() || info.Mode().Perm()&0111 == 0 {
			return nil, errors.New("rootfs e2fsck must be an executable regular file")
		}
	}
	if cfg.BundleEnabled {
		if err := ensureSafeUploadDir(cfg.BundleStageDir); err != nil {
			return nil, fmt.Errorf("initialize bundle staging directory: %w", err)
		}
	}
	if cfg.UpdateImageEnabled {
		if err := ensureSafeUploadDir(cfg.UpdateImageStageDir); err != nil {
			return nil, fmt.Errorf("initialize update.img staging directory: %w", err)
		}
	}
	if cfg.ExpectedParentDevice != "" {
		validated, err := ValidateExpectedParentDevice(cfg.ExpectedParentDevice, cfg.TestMode)
		if err != nil {
			return nil, err
		}
		cfg.ExpectedParentDevice = validated
	} else if cfg.AllowFlash && !cfg.DryRun {
		return nil, errors.New("real flash mode requires an expected parent device")
	}
	return &Manager{
		cfg:       cfg,
		logger:    logger,
		rootfs:    systemRootfsWorkflow{testMode: cfg.TestMode, e2fsckPath: cfg.RootfsE2fsckPath},
		bundle:    systemBundleWorkflow{testMode: cfg.TestMode},
		updateimg: systemUpdateImageWorkflow{testMode: cfg.TestMode},
		reboot:    systemRebootWorkflow{},
	}, nil
}

func ensureSafeUploadDir(path string) error {
	if err := os.MkdirAll(path, 0700); err != nil {
		return fmt.Errorf("create upload directory: %w", err)
	}
	info, err := os.Lstat(path)
	if err != nil {
		return fmt.Errorf("inspect upload directory: %w", err)
	}
	if info.Mode()&os.ModeSymlink != 0 || !info.IsDir() {
		return errors.New("upload directory must be a real directory, not a symbolic link")
	}
	return os.Chmod(path, 0700)
}

func (m *Manager) targetPath(target Target) string {
	switch target {
	case TargetUboot:
		return m.cfg.UbootDevice
	case TargetBoot:
		return m.cfg.BootDevice
	case TargetUpdateImage:
		return m.cfg.RootfsDevice
	default:
		return m.cfg.RootfsDevice
	}
}

func (m *Manager) uploadLimit(target Target) int64 {
	if target == TargetRootfs {
		return m.cfg.MaxRootfsUploadBytes
	}
	if target == TargetBundle {
		return m.cfg.MaxBundleUploadBytes
	}
	if target == TargetUpdateImage {
		return m.cfg.MaxUpdateImageUploadBytes
	}
	return m.cfg.MaxUploadBytes
}

func (m *Manager) dryRunForTarget(target Target) bool {
	if target == TargetRootfs {
		return !m.cfg.RootfsAllowWrite
	}
	if target == TargetBundle {
		return !m.cfg.BundleAllowWrite
	}
	if target == TargetUpdateImage {
		return !m.cfg.UpdateImageAllowWrite
	}
	return m.cfg.DryRun
}

func (m *Manager) BeginUpload(ctx context.Context, target Target, filename string, reader io.Reader) (*Job, error) {
	if !target.ImageTarget() && target != TargetUpdateImage {
		return nil, errors.New("target must be uboot, boot, rootfs, or updateimg")
	}
	if target == TargetRootfs && !m.cfg.RootfsEnabled {
		return nil, errors.New("rootfs recovery workflow is disabled")
	}
	if target == TargetUpdateImage && !m.cfg.UpdateImageEnabled {
		return nil, errors.New("complete update.img OTA workflow is disabled")
	}
	if filename != filepath.Base(filename) || filename != target.Filename() {
		return nil, fmt.Errorf("expected filename %s for target %s", target.Filename(), target)
	}
	resolved, err := ResolveTarget(m.targetPath(target), m.cfg.TestMode)
	if err != nil {
		return nil, err
	}
	if err := m.validateParent(resolved); err != nil {
		return nil, err
	}
	if target == TargetRootfs && !m.cfg.TestMode && filepath.Clean(m.cfg.ExpectedParentDevice) != rootfscontrol.ProductionParent {
		return nil, fmt.Errorf("rootfs recovery requires expected parent device %s", rootfscontrol.ProductionParent)
	}
	if target == TargetUpdateImage && !m.cfg.TestMode && filepath.Clean(m.cfg.ExpectedParentDevice) != updateimgcontrol.ProductionParent {
		return nil, fmt.Errorf("update.img recovery requires expected parent device %s", updateimgcontrol.ProductionParent)
	}

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
		ID: jobID, Target: target, Filename: filename, Device: resolved.Configured,
		ResolvedDevice: resolved.Path, ParentDevice: resolved.ParentPath,
		DeviceSize: resolved.Size, Status: StatusUploading, DryRun: m.dryRunForTarget(target),
		CreatedAt: now, UpdatedAt: now,
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

	var path, manifestPath string
	var size int64
	var digest string
	var validation Validation
	if target == TargetRootfs {
		path, manifestPath, size, digest, validation, err = m.storeRootfsUpload(ctx, reader)
	} else if target == TargetUpdateImage {
		path, manifestPath, size, digest, validation, err = m.storeUpdateImageUpload(ctx, reader)
	} else {
		path, size, digest, validation, err = m.storeUpload(ctx, job, reader)
	}
	if err != nil {
		m.failUpload(job.ID, err)
		return nil, err
	}
	if target != TargetUpdateImage && size > resolved.Size {
		removeStagedFiles(path, manifestPath)
		err = fmt.Errorf("image size %d exceeds partition size %d", size, resolved.Size)
		m.failUpload(job.ID, err)
		return nil, err
	}

	m.mu.Lock()
	job.imagePath = path
	job.manifestPath = manifestPath
	job.Size = size
	job.SHA256 = digest
	job.Validation = validation
	job.Confirmation = confirmation
	job.Status = StatusAwaiting
	job.Progress = 100
	job.confirmationEnds = time.Now().UTC().Add(m.cfg.ConfirmTTL)
	job.UpdatedAt = time.Now().UTC()
	response := cloneJob(job, false)
	m.mu.Unlock()
	m.logger.Info("image staged", "job_id", job.ID, "target", target, "resolved_device", resolved.Path, "parent_device", resolved.ParentPath, "size", size, "sha256", digest, "validation", validation.Summary)
	return response, nil
}

// BeginAndExecuteUpload is the one-click image workflow.  It keeps staging
// and all validation separate from execution, but performs the formerly
// operator-entered confirmation steps inside the authenticated service.
// Callers which must finish validating a multipart request first may use
// BeginUpload followed by ExecuteStaged instead.
func (m *Manager) BeginAndExecuteUpload(ctx context.Context, target Target, filename string, reader io.Reader) (*Job, error) {
	job, err := m.BeginUpload(ctx, target, filename, reader)
	if err != nil {
		return nil, err
	}
	return m.ExecuteStaged(ctx, job.ID)
}

// ExecuteStaged starts an already validated image without requiring an
// operator to disclose or retype a transient confirmation token or phrase.
// The existing Confirm and RebootRootfs paths remain the single enforcement
// points for the image hash, BCB preparation, BCB readback and clean reboot.
func (m *Manager) ExecuteStaged(ctx context.Context, jobID string) (*Job, error) {
	m.mu.Lock()
	if m.job == nil || m.job.ID != jobID {
		m.mu.Unlock()
		return nil, errors.New("job not found")
	}
	if m.job.Status != StatusAwaiting {
		status := m.job.Status
		m.mu.Unlock()
		return nil, fmt.Errorf("job is not ready to start (status %s)", status)
	}
	job := cloneJob(m.job, false)
	m.mu.Unlock()

	prepared, err := m.Confirm(ctx, job.ID, job.SHA256, job.Confirmation, automaticPreparePhrase(job))
	if err != nil {
		return nil, err
	}
	if (prepared.Target != TargetRootfs && prepared.Target != TargetBundle && prepared.Target != TargetUpdateImage) || prepared.Status != StatusRecoveryPrepared {
		return prepared, nil
	}
	return m.RebootRootfs(ctx, prepared.ID, prepared.SHA256, prepared.Confirmation, automaticRebootPhrase(prepared))
}

func automaticPreparePhrase(job *Job) string {
	if job.Target == TargetRootfs && !job.DryRun {
		return rootfscontrol.PrepareConfirmation(job.SHA256)
	}
	if job.Target == TargetBundle && !job.DryRun {
		return bundlecontrol.PrepareConfirmation(job.SHA256)
	}
	if job.Target == TargetUpdateImage && !job.DryRun {
		return updateimgcontrol.PrepareConfirmation(job.SHA256)
	}
	return ConfirmationPhrase(job.Target, job.DryRun)
}

func automaticRebootPhrase(job *Job) string {
	if job.Target == TargetBundle {
		return bundlecontrol.RebootConfirmation(job.SHA256)
	}
	if job.Target == TargetUpdateImage {
		return updateimgcontrol.RebootConfirmation(job.SHA256)
	}
	return rootfscontrol.RebootConfirmation(job.SHA256)
}

func (m *Manager) storeUpload(ctx context.Context, job *Job, reader io.Reader) (path string, size int64, digest string, validation Validation, err error) {
	tmp, err := os.CreateTemp(m.cfg.UploadDir, ".rknetupdate-*.img")
	if err != nil {
		return "", 0, "", Validation{}, fmt.Errorf("create temporary image: %w", err)
	}
	tempPath := tmp.Name()
	path = tempPath
	cleanup := true
	defer func() {
		_ = tmp.Close()
		if cleanup {
			_ = os.Remove(tempPath)
		}
	}()
	if err := tmp.Chmod(0600); err != nil {
		return "", 0, "", Validation{}, err
	}
	info, err := tmp.Stat()
	if err != nil || !info.Mode().IsRegular() {
		return "", 0, "", Validation{}, errors.New("temporary upload is not a regular file")
	}
	hash := sha256.New()
	limited := &contextReader{ctx: ctx, r: io.LimitReader(reader, m.cfg.MaxUploadBytes+1)}
	written, err := io.Copy(io.MultiWriter(tmp, hash), limited)
	if err != nil {
		return "", 0, "", Validation{}, fmt.Errorf("store image: %w", err)
	}
	if written == 0 {
		return "", 0, "", Validation{}, errors.New("image is empty")
	}
	if written > m.cfg.MaxUploadBytes {
		return "", 0, "", Validation{}, fmt.Errorf("image exceeds maximum upload size of %d bytes", m.cfg.MaxUploadBytes)
	}
	if err := tmp.Sync(); err != nil {
		return "", 0, "", Validation{}, fmt.Errorf("sync uploaded image: %w", err)
	}
	headerSize := int64(64 << 10)
	if written < headerSize {
		headerSize = written
	}
	header := make([]byte, headerSize)
	if _, err := tmp.ReadAt(header, 0); err != nil && !errors.Is(err, io.EOF) {
		return "", 0, "", Validation{}, fmt.Errorf("inspect image: %w", err)
	}
	if bytes.HasPrefix(header, rkimage.ContainerMagic[:]) {
		container, inspectErr := rkimage.InspectFile(tempPath)
		validation = Validation{DetectedType: "rockchip-update-img", Confidence: "high", Summary: "检测到 Rockchip update.img 多分区容器：" + strings.Join(container.ItemNames(), ", ")}
		if inspectErr != nil {
			return "", 0, "", validation, fmt.Errorf("validate Rockchip update.img container: %w", inspectErr)
		}
		return "", 0, "", validation, errors.New("Rockchip update.img is a multi-part recovery package and cannot be flashed to a single partition; upload it with the 系统 OTA (update.img) target")
	}
	validation, err = InspectImage(job.Target, header)
	if err != nil {
		return "", 0, "", validation, err
	}
	cleanup = false
	return path, written, hex.EncodeToString(hash.Sum(nil)), validation, nil
}

func (m *Manager) failUpload(jobID string, err error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.job == nil || m.job.ID != jobID {
		return
	}
	m.job.Status = StatusFailed
	m.job.Error = err.Error()
	m.job.UpdatedAt = time.Now().UTC()
	m.busy = false
	m.logger.Error("upload failed", "job_id", jobID, "error", err)
}

func (m *Manager) Confirm(ctx context.Context, jobID, sha, confirmation, phrase string) (*Job, error) {
	m.mu.Lock()
	if m.job == nil || m.job.ID != jobID {
		m.mu.Unlock()
		return nil, errors.New("job not found")
	}
	job := m.job
	if job.Status != StatusAwaiting {
		m.mu.Unlock()
		return nil, fmt.Errorf("job is not awaiting confirmation (status %s)", job.Status)
	}
	if time.Now().UTC().After(job.confirmationEnds) {
		imagePath, manifestPath, target := job.imagePath, job.manifestPath, job.Target
		job.imagePath = ""
		job.manifestPath = ""
		job.Status = StatusCancelled
		job.Error = "confirmation expired"
		job.UpdatedAt = time.Now().UTC()
		m.busy = false
		m.mu.Unlock()
		m.removeJobStage(target, imagePath, manifestPath)
		return nil, errors.New("confirmation expired")
	}
	if subtle.ConstantTimeCompare([]byte(job.SHA256), []byte(sha)) != 1 || subtle.ConstantTimeCompare([]byte(job.Confirmation), []byte(confirmation)) != 1 {
		m.mu.Unlock()
		return nil, errors.New("confirmation token or image hash does not match")
	}
	expectedPhrase := ConfirmationPhrase(job.Target, job.DryRun)
	if job.Target == TargetRootfs && !job.DryRun {
		expectedPhrase = rootfscontrol.PrepareConfirmation(job.SHA256)
	} else if job.Target == TargetBundle && !job.DryRun {
		expectedPhrase = bundlecontrol.PrepareConfirmation(job.SHA256)
	} else if job.Target == TargetUpdateImage && !job.DryRun {
		expectedPhrase = updateimgcontrol.PrepareConfirmation(job.SHA256)
	}
	if subtle.ConstantTimeCompare([]byte(expectedPhrase), []byte(phrase)) != 1 {
		m.mu.Unlock()
		return nil, fmt.Errorf("confirmation phrase must exactly match %q", expectedPhrase)
	}
	if job.Target == TargetRootfs {
		return m.confirmRootfs(ctx, job, phrase)
	}
	if job.Target == TargetBundle {
		return m.confirmBundle(ctx, job, phrase)
	}
	if job.Target == TargetUpdateImage {
		return m.confirmUpdateImage(ctx, job, phrase)
	}
	job.Confirmation = ""
	job.confirmationEnds = time.Time{}
	job.Status = StatusFlashing
	job.Progress = 0
	job.UpdatedAt = time.Now().UTC()
	response := cloneJob(job, true)
	m.mu.Unlock()

	m.wg.Add(1)
	go func() {
		defer m.wg.Done()
		m.runFlash(context.WithoutCancel(ctx), jobID)
	}()
	return response, nil
}

func (m *Manager) confirmRootfs(ctx context.Context, job *Job, phrase string) (*Job, error) {
	if job.DryRun {
		imagePath, manifestPath := job.imagePath, job.manifestPath
		job.imagePath = ""
		job.manifestPath = ""
		job.Confirmation = ""
		job.confirmationEnds = time.Time{}
		job.Status = StatusDryRunDone
		job.Progress = 100
		job.UpdatedAt = time.Now().UTC()
		m.busy = false
		response := cloneJob(job, true)
		m.mu.Unlock()
		removeStagedFiles(imagePath, manifestPath)
		m.logger.Info("rootfs dry-run complete", "job_id", job.ID, "sha256", job.SHA256)
		return response, nil
	}
	rebootToken, err := randomToken(24)
	if err != nil {
		m.mu.Unlock()
		return nil, err
	}
	job.Confirmation = ""
	job.Status = StatusPreparingRecovery
	job.Progress = 0
	job.UpdatedAt = time.Now().UTC()
	m.mu.Unlock()

	result, err := m.rootfs.Prepare(context.WithoutCancel(ctx), phrase)
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.job == nil || m.job.ID != job.ID {
		return nil, errors.New("rootfs job disappeared while preparing recovery")
	}
	job = m.job
	job.UpdatedAt = time.Now().UTC()
	if err != nil {
		job.Status = StatusFailed
		job.Error = err.Error()
		m.busy = false
		m.logger.Error("rootfs recovery preparation failed; staged image retained", "job_id", job.ID, "error", err)
		return nil, err
	}
	job.Confirmation = rebootToken
	job.RecoveryPhase = result.BCBPhase
	job.Status = StatusRecoveryPrepared
	// Recovery has only been armed at this point. The image has not been
	// written yet, so reporting 100% here is misleading.
	job.Progress = 0
	job.confirmationEnds = time.Time{}
	m.logger.Info("rootfs recovery prepared", "job_id", job.ID, "sha256", job.SHA256, "phase", result.BCBPhase)
	return cloneJob(job, false), nil
}

func (m *Manager) confirmBundle(ctx context.Context, job *Job, phrase string) (*Job, error) {
	if job.DryRun {
		job.manifestPath = ""
		job.Confirmation = ""
		job.confirmationEnds = time.Time{}
		job.Status = StatusDryRunDone
		job.Progress = 100
		job.UpdatedAt = time.Now().UTC()
		m.busy = false
		response := cloneJob(job, true)
		m.mu.Unlock()
		m.clearBundleStage()
		m.logger.Info("offline bundle dry-run complete", "job_id", job.ID, "manifest_sha256", job.SHA256)
		return response, nil
	}
	rebootToken, err := randomToken(24)
	if err != nil {
		m.mu.Unlock()
		return nil, err
	}
	job.Confirmation = ""
	job.Status = StatusPreparingRecovery
	job.Progress = 0
	job.UpdatedAt = time.Now().UTC()
	m.mu.Unlock()

	result, err := m.bundle.Prepare(context.WithoutCancel(ctx), phrase)
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.job == nil || m.job.ID != job.ID {
		return nil, errors.New("bundle job disappeared while preparing recovery")
	}
	job = m.job
	job.UpdatedAt = time.Now().UTC()
	if err != nil {
		job.Status = StatusFailed
		job.Error = err.Error()
		m.busy = false
		m.logger.Error("bundle recovery preparation failed; staged artifacts retained", "job_id", job.ID, "error", err)
		return nil, err
	}
	job.Confirmation = rebootToken
	job.RecoveryPhase = result.BCBPhase
	job.Status = StatusRecoveryPrepared
	job.Progress = 0
	job.confirmationEnds = time.Time{}
	m.logger.Info("offline bundle recovery prepared", "job_id", job.ID, "manifest_sha256", job.SHA256, "phase", result.BCBPhase)
	return cloneJob(job, false), nil
}

func (m *Manager) confirmUpdateImage(ctx context.Context, job *Job, phrase string) (*Job, error) {
	if job.DryRun {
		job.imagePath = ""
		job.manifestPath = ""
		job.Confirmation = ""
		job.confirmationEnds = time.Time{}
		job.Status = StatusDryRunDone
		job.Progress = 100
		job.UpdatedAt = time.Now().UTC()
		m.busy = false
		response := cloneJob(job, true)
		m.mu.Unlock()
		m.clearUpdateImageStage()
		m.logger.Info("update.img dry-run complete", "job_id", job.ID, "sha256", job.SHA256)
		return response, nil
	}
	rebootToken, err := randomToken(24)
	if err != nil {
		m.mu.Unlock()
		return nil, err
	}
	job.Confirmation = ""
	job.Status = StatusPreparingRecovery
	job.Progress = 0
	job.UpdatedAt = time.Now().UTC()
	m.mu.Unlock()

	result, err := m.updateimg.Prepare(context.WithoutCancel(ctx), phrase)
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.job == nil || m.job.ID != job.ID {
		return nil, errors.New("update.img job disappeared while preparing recovery")
	}
	job = m.job
	job.UpdatedAt = time.Now().UTC()
	if err != nil {
		job.Status = StatusFailed
		job.Error = err.Error()
		m.busy = false
		m.logger.Error("update.img recovery preparation failed; staged package retained", "job_id", job.ID, "error", err)
		return nil, err
	}
	job.Confirmation = rebootToken
	job.RecoveryPhase = result.BCBPhase
	job.Status = StatusRecoveryPrepared
	job.Progress = 0
	job.confirmationEnds = time.Time{}
	m.logger.Info("update.img recovery prepared", "job_id", job.ID, "sha256", job.SHA256, "phase", result.BCBPhase)
	return cloneJob(job, false), nil
}

func (m *Manager) RebootRootfs(ctx context.Context, jobID, sha, confirmation, phrase string) (*Job, error) {
	m.mu.Lock()
	if m.job == nil || m.job.ID != jobID {
		m.mu.Unlock()
		return nil, errors.New("job not found")
	}
	job := m.job
	if (job.Target != TargetRootfs && job.Target != TargetBundle && job.Target != TargetUpdateImage) || job.Status != StatusRecoveryPrepared {
		m.mu.Unlock()
		return nil, fmt.Errorf("job is not prepared for offline recovery (status %s)", job.Status)
	}
	if subtle.ConstantTimeCompare([]byte(job.SHA256), []byte(sha)) != 1 || subtle.ConstantTimeCompare([]byte(job.Confirmation), []byte(confirmation)) != 1 {
		m.mu.Unlock()
		return nil, errors.New("confirmation token or image hash does not match")
	}
	expectedPhrase := rootfscontrol.RebootConfirmation(job.SHA256)
	if job.Target == TargetBundle {
		expectedPhrase = bundlecontrol.RebootConfirmation(job.SHA256)
	} else if job.Target == TargetUpdateImage {
		expectedPhrase = updateimgcontrol.RebootConfirmation(job.SHA256)
	}
	if subtle.ConstantTimeCompare([]byte(expectedPhrase), []byte(phrase)) != 1 {
		m.mu.Unlock()
		return nil, fmt.Errorf("confirmation phrase must exactly match %q", expectedPhrase)
	}
	job.Confirmation = ""
	job.Status = StatusPreparingRecovery
	job.Progress = 0
	job.UpdatedAt = time.Now().UTC()
	m.mu.Unlock()

	var (
		phase string
		err   error
	)
	if job.Target == TargetBundle {
		var result bundlecontrol.Result
		result, err = m.bundle.VerifyPrepared(context.WithoutCancel(ctx), phrase)
		phase = result.BCBPhase
	} else if job.Target == TargetUpdateImage {
		var result updateimgcontrol.Result
		result, err = m.updateimg.VerifyPrepared(context.WithoutCancel(ctx), phrase)
		phase = result.BCBPhase
	} else {
		var result rootfscontrol.Result
		result, err = m.rootfs.VerifyPrepared(context.WithoutCancel(ctx), phrase)
		phase = result.BCBPhase
	}
	m.mu.Lock()
	if m.job == nil || m.job.ID != jobID {
		m.mu.Unlock()
		return nil, errors.New("rootfs job disappeared while verifying recovery")
	}
	job = m.job
	job.UpdatedAt = time.Now().UTC()
	if err != nil {
		job.Status = StatusFailed
		job.Error = err.Error()
		m.busy = false
		m.mu.Unlock()
		m.logger.Error("rootfs reboot verification failed; staged image retained", "job_id", job.ID, "error", err)
		return nil, err
	}
	job.RecoveryPhase = phase
	job.Status = StatusRebootRequested
	response := cloneJob(job, true)
	m.mu.Unlock()

	m.wg.Add(1)
	go m.requestRootfsReboot(jobID)
	m.logger.Info("offline recovery reboot requested", "job_id", jobID, "target", job.Target, "sha256", sha)
	return response, nil
}

func (m *Manager) requestRootfsReboot(jobID string) {
	defer m.wg.Done()
	timer := time.NewTimer(750 * time.Millisecond)
	defer timer.Stop()
	<-timer.C
	m.mu.Lock()
	if m.job == nil || m.job.ID != jobID {
		m.mu.Unlock()
		return
	}
	target := m.job.Target
	m.mu.Unlock()
	var err error
	if target == TargetBundle {
		err = m.bundle.Reboot(context.Background())
	} else if target == TargetUpdateImage {
		err = m.updateimg.Reboot(context.Background())
	} else {
		err = m.rootfs.Reboot(context.Background())
	}
	if err == nil {
		return
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.job == nil || m.job.ID != jobID {
		return
	}
	m.job.Status = StatusFailed
	m.job.Error = fmt.Sprintf("request clean reboot: %v", err)
	m.job.UpdatedAt = time.Now().UTC()
	m.busy = false
	m.logger.Error("rootfs clean reboot request failed", "job_id", jobID, "error", err)
}

func (m *Manager) runFlash(ctx context.Context, jobID string) {
	m.mu.Lock()
	if m.job == nil || m.job.ID != jobID {
		m.mu.Unlock()
		return
	}
	job := m.job
	imagePath, targetPath, expectedResolvedPath, imageSize, expectedHash := job.imagePath, m.targetPath(job.Target), job.ResolvedDevice, job.Size, job.SHA256
	dryRun := m.cfg.DryRun
	m.mu.Unlock()
	defer os.Remove(imagePath)

	resolved, err := ResolveTarget(targetPath, m.cfg.TestMode)
	if err == nil {
		err = m.validateParent(resolved)
	}
	if err == nil && resolved.Path != expectedResolvedPath {
		err = fmt.Errorf("resolved partition changed after upload: expected %s, got %s", expectedResolvedPath, resolved.Path)
	}
	if err == nil && imageSize > resolved.Size {
		err = fmt.Errorf("image size %d exceeds partition size %d", imageSize, resolved.Size)
	}
	if err == nil && !dryRun && !m.cfg.AllowFlash {
		err = errors.New("real flash is disabled; restart with --allow-flash")
	}
	var readback string
	if err == nil && dryRun {
		m.setProgress(jobID, StatusDryRunDone, 100)
	} else if err == nil {
		readback, err = flashAndVerify(ctx, imagePath, resolved, imageSize, expectedHash, func(status JobStatus, percent int) {
			m.setProgress(jobID, status, percent)
		})
	}

	m.mu.Lock()
	defer m.mu.Unlock()
	if m.job == nil || m.job.ID != jobID {
		return
	}
	m.job.UpdatedAt = time.Now().UTC()
	m.job.imagePath = ""
	m.job.Confirmation = ""
	m.job.ReadbackSHA256 = readback
	if err != nil {
		m.job.Status = StatusFailed
		m.job.Error = err.Error()
		m.logger.Error("flash job failed", "job_id", jobID, "error", err)
	} else if dryRun {
		m.job.Status = StatusDryRunDone
		m.logger.Info("dry-run job complete", "job_id", jobID, "target", job.Target, "sha256", expectedHash)
	} else {
		m.job.Status = StatusSucceeded
		m.logger.Info("flash job complete", "job_id", jobID, "target", job.Target, "sha256", expectedHash)
	}
	m.job.Progress = 100
	m.busy = false
}

func flashAndVerify(ctx context.Context, imagePath string, target ResolvedTarget, imageSize int64, expectedHash string, progress func(JobStatus, int)) (string, error) {
	image, err := os.Open(imagePath)
	if err != nil {
		return "", fmt.Errorf("open staged image: %w", err)
	}
	defer image.Close()
	fd, err := syscall.Open(target.Path, syscall.O_WRONLY|syscall.O_CLOEXEC|syscall.O_NOFOLLOW, 0)
	if err != nil {
		return "", fmt.Errorf("open target for writing: %w", err)
	}
	device := os.NewFile(uintptr(fd), target.Path)
	if device == nil {
		_ = syscall.Close(fd)
		return "", errors.New("create target file handle")
	}
	reader := &progressReader{ctx: ctx, r: image, total: imageSize, update: func(n int64) {
		progress(StatusFlashing, int(n*80/imageSize))
	}}
	written, copyErr := io.CopyBuffer(device, reader, make([]byte, 1<<20))
	if copyErr == nil && written != imageSize {
		copyErr = fmt.Errorf("short write: wrote %d of %d bytes", written, imageSize)
	}
	if copyErr == nil {
		copyErr = device.Sync()
	}
	closeErr := device.Close()
	if copyErr != nil {
		return "", fmt.Errorf("write target: %w", copyErr)
	}
	if closeErr != nil {
		return "", fmt.Errorf("close target: %w", closeErr)
	}

	progress(StatusVerifying, 82)
	fd, err = syscall.Open(target.Path, syscall.O_RDONLY|syscall.O_CLOEXEC|syscall.O_NOFOLLOW, 0)
	if err != nil {
		return "", fmt.Errorf("open target for verification: %w", err)
	}
	readbackFile := os.NewFile(uintptr(fd), target.Path)
	defer readbackFile.Close()
	hash := sha256.New()
	limited := io.LimitReader(&progressReader{ctx: ctx, r: readbackFile, total: imageSize, update: func(n int64) {
		progress(StatusVerifying, 80+int(n*20/imageSize))
	}}, imageSize)
	n, err := io.CopyBuffer(hash, limited, make([]byte, 1<<20))
	if err != nil {
		return "", fmt.Errorf("read back target: %w", err)
	}
	if n != imageSize {
		return "", fmt.Errorf("short readback: read %d of %d bytes", n, imageSize)
	}
	digest := hex.EncodeToString(hash.Sum(nil))
	if subtle.ConstantTimeCompare([]byte(digest), []byte(expectedHash)) != 1 {
		return digest, fmt.Errorf("readback SHA-256 mismatch: expected %s, got %s", expectedHash, digest)
	}
	return digest, nil
}

func (m *Manager) setProgress(jobID string, status JobStatus, percent int) {
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.job == nil || m.job.ID != jobID {
		return
	}
	if percent < m.job.Progress && status == m.job.Status {
		return
	}
	m.job.Status = status
	m.job.Progress = percent
	m.job.UpdatedAt = time.Now().UTC()
}

func (m *Manager) Cancel(jobID string) error {
	m.mu.Lock()
	if m.job == nil || m.job.ID != jobID {
		m.mu.Unlock()
		return errors.New("job not found")
	}
	if m.job.Status != StatusAwaiting {
		m.mu.Unlock()
		return errors.New("only a staged job can be cancelled")
	}
	imagePath, manifestPath, target := m.job.imagePath, m.job.manifestPath, m.job.Target
	m.job.imagePath = ""
	m.job.manifestPath = ""
	m.job.Confirmation = ""
	m.job.Status = StatusCancelled
	m.job.UpdatedAt = time.Now().UTC()
	m.busy = false
	m.mu.Unlock()
	m.removeJobStage(target, imagePath, manifestPath)
	return nil
}

func (m *Manager) Status() StatusResponse {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.expireLocked(time.Now().UTC())
	revealConfirmation := m.job != nil && m.job.Status == StatusRecoveryPrepared
	return StatusResponse{Busy: m.busy, Job: cloneJob(m.job, !revealConfirmation)}
}

func (m *Manager) Capabilities() []Capability {
	result := make([]Capability, 0, 5)
	for _, target := range []Target{TargetUboot, TargetBoot, TargetRootfs} {
		workflow := "raw-flash"
		if target == TargetRootfs {
			workflow = "recovery-v2"
		}
		path := m.targetPath(target)
		writeEnabled := m.cfg.AllowFlash && !m.cfg.DryRun
		if target == TargetRootfs {
			writeEnabled = m.cfg.RootfsAllowWrite
		}
		cap := Capability{Target: target, Filename: target.Filename(), Device: path, MaxUploadBytes: m.uploadLimit(target), Workflow: workflow, WriteEnabled: writeEnabled}
		if target == TargetRootfs && !m.cfg.RootfsEnabled {
			cap.Error = "rootfs recovery workflow is disabled"
			result = append(result, cap)
			continue
		}
		resolved, err := ResolveTarget(path, m.cfg.TestMode)
		if err != nil {
			cap.Error = err.Error()
		} else {
			cap.ResolvedDevice = resolved.Path
			cap.ParentDevice = resolved.ParentPath
			cap.DeviceSize = resolved.Size
			if err := m.validateParent(resolved); err != nil {
				cap.Error = err.Error()
			} else if target == TargetRootfs && !m.cfg.TestMode && filepath.Clean(m.cfg.ExpectedParentDevice) != rootfscontrol.ProductionParent {
				cap.Error = fmt.Sprintf("rootfs recovery requires expected parent device %s", rootfscontrol.ProductionParent)
			} else {
				cap.Available = true
			}
		}
		result = append(result, cap)
	}
	bundleCap := Capability{
		Target:         TargetBundle,
		Filename:       bundleManifestName,
		Device:         m.cfg.RootfsDevice,
		MaxUploadBytes: m.uploadLimit(TargetBundle),
		Workflow:       "offline-recovery-bundle-v1",
		WriteEnabled:   m.cfg.BundleAllowWrite,
	}
	if !m.cfg.BundleEnabled {
		bundleCap.Error = "offline multi-image bundle workflow is disabled"
	} else {
		targets, err := m.resolveBundleTargets()
		if err != nil {
			bundleCap.Error = err.Error()
		} else {
			rootfs := targets[TargetRootfs]
			bundleCap.ResolvedDevice = rootfs.Path
			bundleCap.ParentDevice = rootfs.ParentPath
			bundleCap.DeviceSize = rootfs.Size
			if !m.cfg.TestMode && filepath.Clean(m.cfg.ExpectedParentDevice) != bundlecontrol.ProductionParent {
				bundleCap.Error = fmt.Sprintf("bundle recovery requires expected parent device %s", bundlecontrol.ProductionParent)
			} else {
				bundleCap.Available = true
			}
		}
	}
	result = append(result, bundleCap)
	updateImageCap := Capability{
		Target:         TargetUpdateImage,
		Filename:       TargetUpdateImage.Filename(),
		Device:         m.cfg.RootfsDevice,
		MaxUploadBytes: m.uploadLimit(TargetUpdateImage),
		Workflow:       "rockchip-updateimg-recovery-v1",
		WriteEnabled:   m.cfg.UpdateImageAllowWrite,
	}
	if !m.cfg.UpdateImageEnabled {
		updateImageCap.Error = "complete update.img OTA workflow is disabled"
	} else {
		rootfs, err := ResolveTarget(m.cfg.RootfsDevice, m.cfg.TestMode)
		if err != nil {
			updateImageCap.Error = err.Error()
		} else {
			updateImageCap.ResolvedDevice = rootfs.Path
			updateImageCap.ParentDevice = rootfs.ParentPath
			updateImageCap.DeviceSize = rootfs.Size
			if err := m.validateParent(rootfs); err != nil {
				updateImageCap.Error = err.Error()
			} else if !m.cfg.TestMode && filepath.Clean(m.cfg.ExpectedParentDevice) != updateimgcontrol.ProductionParent {
				updateImageCap.Error = fmt.Sprintf("update.img recovery requires expected parent device %s", updateimgcontrol.ProductionParent)
			} else {
				updateImageCap.Available = true
			}
		}
	}
	result = append(result, updateImageCap)
	return result
}

func (m *Manager) validateParent(target ResolvedTarget) error {
	if m.cfg.ExpectedParentDevice == "" {
		if m.cfg.AllowFlash && !m.cfg.DryRun {
			return errors.New("real flash mode requires an expected parent device")
		}
		return nil
	}
	return ValidateTargetParent(target, m.cfg.ExpectedParentDevice)
}

func ConfirmationPhrase(target Target, dryRun bool) string {
	if target == TargetBundle {
		if dryRun {
			return "CHECK BUNDLE"
		}
		return "PREPARE_BUNDLE_V1:<manifest-sha256>"
	}
	if target == TargetUpdateImage {
		if dryRun {
			return "CHECK UPDATEIMG"
		}
		return "PREPARE_UPDATEIMG_V1:<package-sha256>"
	}
	if target == TargetRootfs {
		if dryRun {
			return "CHECK ROOTFS"
		}
		return "PREPARE_ROOTFS_V2:<sha256>"
	}
	verb := "FLASH"
	if dryRun {
		verb = "CHECK"
	}
	return verb + " " + strings.ToUpper(string(target))
}

func (m *Manager) expireLocked(now time.Time) {
	if m.job == nil || m.job.Status != StatusAwaiting || now.Before(m.job.confirmationEnds) {
		return
	}
	imagePath, manifestPath, target := m.job.imagePath, m.job.manifestPath, m.job.Target
	m.job.imagePath = ""
	m.job.manifestPath = ""
	m.job.Confirmation = ""
	m.job.Status = StatusCancelled
	m.job.Error = "confirmation expired"
	m.job.UpdatedAt = now
	m.busy = false
	m.removeJobStage(target, imagePath, manifestPath)
}

func (m *Manager) Close() {
	m.wg.Wait()
	m.mu.Lock()
	defer m.mu.Unlock()
	preserveRecoveryStage := m.job != nil && (m.job.Target == TargetRootfs || m.job.Target == TargetBundle || m.job.Target == TargetUpdateImage) && m.job.Status != StatusAwaiting && m.job.Status != StatusCancelled && m.job.Status != StatusDryRunDone
	if m.job != nil && !preserveRecoveryStage {
		m.removeJobStage(m.job.Target, m.job.imagePath, m.job.manifestPath)
		m.job.imagePath = ""
		m.job.manifestPath = ""
	}
}

func (m *Manager) removeJobStage(target Target, imagePath, manifestPath string) {
	if target == TargetBundle {
		m.clearBundleStage()
		return
	}
	if target == TargetUpdateImage {
		m.clearUpdateImageStage()
		return
	}
	removeStagedFiles(imagePath, manifestPath)
}

func removeStagedFiles(imagePath, manifestPath string) {
	if manifestPath != "" {
		_ = os.Remove(manifestPath)
	}
	if imagePath != "" {
		_ = os.Remove(imagePath)
	}
}

func cloneJob(job *Job, redactConfirmation bool) *Job {
	if job == nil {
		return nil
	}
	clone := *job
	clone.imagePath = ""
	if redactConfirmation {
		clone.Confirmation = ""
	}
	return &clone
}

func randomToken(bytesLen int) (string, error) {
	b := make([]byte, bytesLen)
	if _, err := rand.Read(b); err != nil {
		return "", fmt.Errorf("generate secure token: %w", err)
	}
	return hex.EncodeToString(b), nil
}

type contextReader struct {
	ctx context.Context
	r   io.Reader
}

func (r *contextReader) Read(p []byte) (int, error) {
	select {
	case <-r.ctx.Done():
		return 0, r.ctx.Err()
	default:
		return r.r.Read(p)
	}
}

type progressReader struct {
	ctx    context.Context
	r      io.Reader
	total  int64
	read   int64
	update func(int64)
}

func (r *progressReader) Read(p []byte) (int, error) {
	select {
	case <-r.ctx.Done():
		return 0, r.ctx.Err()
	default:
	}
	n, err := r.r.Read(p)
	r.read += int64(n)
	if n > 0 && r.update != nil {
		r.update(r.read)
	}
	return n, err
}
