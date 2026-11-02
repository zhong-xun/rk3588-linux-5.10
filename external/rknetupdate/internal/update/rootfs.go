package update

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"

	"rknetupdate/internal/recoveryapply"
	"rknetupdate/internal/rootfscontrol"
)

const (
	rootfsImageName    = "rootfs.img"
	rootfsManifestName = "rootfs.manifest.json"
)

// rootfsWorkflow is deliberately separate from the raw partition writer. A
// running rootfs must only be replaced by the recovery v2 state machine.
type rootfsWorkflow interface {
	Prepare(context.Context, string) (rootfscontrol.Result, error)
	VerifyPrepared(context.Context, string) (rootfscontrol.Result, error)
	Reboot(context.Context) error
}

type systemRootfsWorkflow struct {
	testMode   bool
	e2fsckPath string
}

func (w systemRootfsWorkflow) Prepare(ctx context.Context, confirmation string) (rootfscontrol.Result, error) {
	cfg := rootfscontrol.DefaultConfig()
	cfg.AllowWrite = true
	cfg.TestMode = w.testMode
	cfg.E2fsckPath = w.e2fsckPath
	cfg.Confirmation = confirmation
	return rootfscontrol.Prepare(ctx, cfg)
}

func (w systemRootfsWorkflow) VerifyPrepared(ctx context.Context, confirmation string) (rootfscontrol.Result, error) {
	cfg := rootfscontrol.DefaultConfig()
	cfg.AllowWrite = true
	cfg.TestMode = w.testMode
	cfg.E2fsckPath = w.e2fsckPath
	cfg.Confirmation = confirmation
	return rootfscontrol.VerifyPrepared(ctx, cfg)
}

func (systemRootfsWorkflow) Reboot(ctx context.Context) error {
	return systemRebootWorkflow{}.Reboot(ctx)
}

func (m *Manager) rootfsStagePaths() (string, string) {
	return filepath.Join(m.cfg.RootfsStageDir, rootfsImageName), filepath.Join(m.cfg.RootfsStageDir, rootfsManifestName)
}

func (m *Manager) rootfsStageVacant() error {
	entries, err := os.ReadDir(m.cfg.RootfsStageDir)
	if err != nil {
		return fmt.Errorf("inspect rootfs staging directory: %w", err)
	}
	if len(entries) != 0 {
		return errors.New("rootfs staging directory is not empty; retain it for recovery inspection or remove a known-stale stage before a new upload")
	}
	return nil
}

func (m *Manager) storeRootfsUpload(ctx context.Context, reader io.Reader) (string, string, int64, string, Validation, error) {
	if err := m.rootfsStageVacant(); err != nil {
		return "", "", 0, "", Validation{}, err
	}
	tmp, err := os.CreateTemp(m.cfg.RootfsStageDir, ".rootfs-upload-*.img")
	if err != nil {
		return "", "", 0, "", Validation{}, fmt.Errorf("create rootfs staging image: %w", err)
	}
	tmpPath := tmp.Name()
	imagePath, manifestPath := m.rootfsStagePaths()
	closed := false
	cleanup := true
	defer func() {
		if !closed {
			_ = tmp.Close()
		}
		_ = os.Remove(tmpPath)
		if cleanup {
			_ = os.Remove(manifestPath)
			_ = os.Remove(imagePath)
		}
	}()
	if err := tmp.Chmod(0600); err != nil {
		return "", "", 0, "", Validation{}, fmt.Errorf("set rootfs staging image mode: %w", err)
	}
	hash := sha256.New()
	limited := &contextReader{ctx: ctx, r: io.LimitReader(reader, m.cfg.MaxRootfsUploadBytes+1)}
	size, err := io.Copy(io.MultiWriter(tmp, hash), limited)
	if err != nil {
		return "", "", 0, "", Validation{}, fmt.Errorf("store rootfs image: %w", err)
	}
	if size == 0 {
		return "", "", 0, "", Validation{}, errors.New("rootfs image is empty")
	}
	if size > m.cfg.MaxRootfsUploadBytes {
		return "", "", 0, "", Validation{}, fmt.Errorf("rootfs image exceeds maximum upload size of %d bytes", m.cfg.MaxRootfsUploadBytes)
	}
	if err := tmp.Sync(); err != nil {
		return "", "", 0, "", Validation{}, fmt.Errorf("sync rootfs image: %w", err)
	}
	ext4, err := recoveryapply.InspectExt4(tmp, size)
	if err != nil {
		return "", "", 0, "", Validation{}, fmt.Errorf("validate rootfs ext4: %w", err)
	}
	if err := tmp.Close(); err != nil {
		return "", "", 0, "", Validation{}, fmt.Errorf("close rootfs staging image: %w", err)
	}
	closed = true
	if err := os.Link(tmpPath, imagePath); err != nil {
		return "", "", 0, "", Validation{}, fmt.Errorf("publish rootfs image: %w", err)
	}
	if err := os.Remove(tmpPath); err != nil {
		return "", "", 0, "", Validation{}, fmt.Errorf("remove rootfs staging temporary image: %w", err)
	}

	digest := hex.EncodeToString(hash.Sum(nil))
	manifest := recoveryapply.RootfsManifestV2{
		Schema:          recoveryapply.RootfsRequestSchema,
		Image:           rootfsImageName,
		Size:            size,
		SHA256:          digest,
		Filesystem:      "ext4",
		FilesystemUUID:  ext4.UUID,
		BlockSize:       ext4.BlockSize,
		BlockCount:      ext4.BlockCount,
		TargetPartlabel: "rootfs",
		ExpectedParent:  rootfscontrol.ProductionParent,
	}
	if err := publishRootfsManifest(m.cfg.RootfsStageDir, manifestPath, manifest); err != nil {
		return "", "", 0, "", Validation{}, err
	}
	if err := syncDirectory(m.cfg.RootfsStageDir); err != nil {
		return "", "", 0, "", Validation{}, fmt.Errorf("sync rootfs staging directory: %w", err)
	}
	if _, _, checked, err := recoveryapply.ValidateRootfsImageV2Metadata(manifestPath, m.cfg.TestMode); err != nil {
		return "", "", 0, "", Validation{}, fmt.Errorf("validate staged rootfs v2 image: %w", err)
	} else if checked != ext4 {
		return "", "", 0, "", Validation{}, errors.New("rootfs ext4 geometry changed during staging validation")
	}
	validation := Validation{
		DetectedType: "ext4",
		Confidence:   "high",
		Summary: fmt.Sprintf("已验证 ext4：UUID %s，%d x %d-byte blocks；完整 e2fsck 将由 recovery 执行",
			ext4.UUID, ext4.BlockCount, ext4.BlockSize),
	}
	cleanup = false
	return imagePath, manifestPath, size, digest, validation, nil
}

func publishRootfsManifest(stageDir, manifestPath string, manifest recoveryapply.RootfsManifestV2) error {
	tmp, err := os.CreateTemp(stageDir, ".rootfs-manifest-*.json")
	if err != nil {
		return fmt.Errorf("create rootfs manifest: %w", err)
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
		return fmt.Errorf("set rootfs manifest mode: %w", err)
	}
	encoder := json.NewEncoder(tmp)
	if err := encoder.Encode(manifest); err != nil {
		return fmt.Errorf("encode rootfs manifest: %w", err)
	}
	if err := tmp.Sync(); err != nil {
		return fmt.Errorf("sync rootfs manifest: %w", err)
	}
	if err := tmp.Close(); err != nil {
		return fmt.Errorf("close rootfs manifest: %w", err)
	}
	if err := os.Link(tmpPath, manifestPath); err != nil {
		return fmt.Errorf("publish rootfs manifest: %w", err)
	}
	if err := os.Remove(tmpPath); err != nil {
		return fmt.Errorf("remove rootfs manifest temporary file: %w", err)
	}
	cleanup = false
	return nil
}

func syncDirectory(path string) error {
	dir, err := os.Open(path)
	if err != nil {
		return err
	}
	defer dir.Close()
	return dir.Sync()
}
