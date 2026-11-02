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
	"rknetupdate/internal/updateimgcontrol"
)

const (
	updateImageName         = recoveryapply.UpdateImageFileName
	updateImageManifestName = recoveryapply.UpdateImageManifestName
)

// updateImageWorkflow keeps the normal system out of all package partition
// writes.  It only binds a fully validated stage to the private recovery BCB.
type updateImageWorkflow interface {
	Prepare(context.Context, string) (updateimgcontrol.Result, error)
	VerifyPrepared(context.Context, string) (updateimgcontrol.Result, error)
	Reboot(context.Context) error
}

type systemUpdateImageWorkflow struct{ testMode bool }

func (w systemUpdateImageWorkflow) Prepare(ctx context.Context, confirmation string) (updateimgcontrol.Result, error) {
	cfg := updateimgcontrol.DefaultConfig()
	cfg.AllowWrite = true
	cfg.TestMode = w.testMode
	cfg.Confirmation = confirmation
	return updateimgcontrol.Prepare(ctx, cfg)
}

func (w systemUpdateImageWorkflow) VerifyPrepared(ctx context.Context, confirmation string) (updateimgcontrol.Result, error) {
	cfg := updateimgcontrol.DefaultConfig()
	cfg.AllowWrite = true
	cfg.TestMode = w.testMode
	cfg.Confirmation = confirmation
	return updateimgcontrol.VerifyPrepared(ctx, cfg)
}

func (systemUpdateImageWorkflow) Reboot(ctx context.Context) error {
	return systemRootfsWorkflow{}.Reboot(ctx)
}

func (m *Manager) updateImageStagePaths() (string, string) {
	return filepath.Join(m.cfg.UpdateImageStageDir, updateImageName), filepath.Join(m.cfg.UpdateImageStageDir, updateImageManifestName)
}

func (m *Manager) updateImageStageVacant() error {
	entries, err := os.ReadDir(m.cfg.UpdateImageStageDir)
	if err != nil {
		return fmt.Errorf("inspect update.img staging directory: %w", err)
	}
	if len(entries) != 0 {
		return errors.New("update.img staging directory is not empty; retain it for recovery inspection or remove a known-stale update.img stage before a new upload")
	}
	return nil
}

func (m *Manager) clearUpdateImageStage() {
	for _, name := range []string{updateImageName, updateImageManifestName, recoveryapply.UpdateImageStateName, recoveryapply.UpdateImageKeepName} {
		_ = os.Remove(filepath.Join(m.cfg.UpdateImageStageDir, name))
	}
}

func (m *Manager) storeUpdateImageUpload(ctx context.Context, reader io.Reader) (string, string, int64, string, Validation, error) {
	if err := m.updateImageStageVacant(); err != nil {
		return "", "", 0, "", Validation{}, err
	}
	tmp, err := os.CreateTemp(m.cfg.UpdateImageStageDir, ".updateimg-upload-*.img")
	if err != nil {
		return "", "", 0, "", Validation{}, fmt.Errorf("create update.img staging file: %w", err)
	}
	tmpPath := tmp.Name()
	imagePath, manifestPath := m.updateImageStagePaths()
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
		return "", "", 0, "", Validation{}, fmt.Errorf("set update.img staging mode: %w", err)
	}
	hash := sha256.New()
	limited := &contextReader{ctx: ctx, r: io.LimitReader(reader, m.cfg.MaxUpdateImageUploadBytes+1)}
	size, err := io.Copy(io.MultiWriter(tmp, hash), limited)
	if err != nil {
		return "", "", 0, "", Validation{}, fmt.Errorf("store update.img: %w", err)
	}
	if size == 0 {
		return "", "", 0, "", Validation{}, errors.New("update.img is empty")
	}
	if size > m.cfg.MaxUpdateImageUploadBytes {
		return "", "", 0, "", Validation{}, fmt.Errorf("update.img exceeds maximum upload size of %d bytes", m.cfg.MaxUpdateImageUploadBytes)
	}
	if err := tmp.Sync(); err != nil {
		return "", "", 0, "", Validation{}, fmt.Errorf("sync update.img: %w", err)
	}
	if err := tmp.Close(); err != nil {
		return "", "", 0, "", Validation{}, fmt.Errorf("close update.img staging file: %w", err)
	}
	closed = true
	// Inspect before publishing so an interrupted/invalid package never
	// becomes a stage that recovery could later mistake for complete.
	manifest, err := recoveryapply.BuildUpdateImageManifest(tmpPath, m.cfg.ExpectedParentDevice)
	if err != nil {
		return "", "", 0, "", Validation{}, fmt.Errorf("validate Rockchip update.img container: %w", err)
	}
	if manifest.Size != size || manifest.SHA256 != hex.EncodeToString(hash.Sum(nil)) {
		return "", "", 0, "", Validation{}, errors.New("update.img changed while its manifest was generated")
	}
	if err := os.Link(tmpPath, imagePath); err != nil {
		return "", "", 0, "", Validation{}, fmt.Errorf("publish update.img: %w", err)
	}
	if err := os.Remove(tmpPath); err != nil {
		return "", "", 0, "", Validation{}, fmt.Errorf("remove temporary update.img: %w", err)
	}
	if err := publishUpdateImageManifest(m.cfg.UpdateImageStageDir, manifestPath, manifest); err != nil {
		return "", "", 0, "", Validation{}, err
	}
	if err := syncDirectory(m.cfg.UpdateImageStageDir); err != nil {
		return "", "", 0, "", Validation{}, fmt.Errorf("sync update.img staging directory: %w", err)
	}
	if _, _, err := recoveryapply.ValidateUpdateImageStage(manifestPath, m.cfg.TestMode); err != nil {
		return "", "", 0, "", Validation{}, fmt.Errorf("validate staged update.img: %w", err)
	}
	items := manifest.Items
	names := make([]string, 0, len(items))
	for _, item := range items {
		names = append(names, item.Name)
	}
	validation := Validation{
		DetectedType: "rockchip-update-img", Confidence: "high",
		Summary: "已验证 Rockchip 完整 update.img：" + joinUpdateImageItems(names) + "；将进入 Recovery 执行整包 OTA",
		Items:   append([]string(nil), names...),
	}
	cleanup = false
	return imagePath, manifestPath, size, manifest.SHA256, validation, nil
}

func publishUpdateImageManifest(stageDir, manifestPath string, manifest recoveryapply.UpdateImageManifest) error {
	tmp, err := os.CreateTemp(stageDir, ".updateimg-manifest-*.json")
	if err != nil {
		return fmt.Errorf("create update.img manifest: %w", err)
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
		return fmt.Errorf("set update.img manifest mode: %w", err)
	}
	encoder := json.NewEncoder(tmp)
	if err := encoder.Encode(manifest); err != nil {
		return fmt.Errorf("encode update.img manifest: %w", err)
	}
	if err := tmp.Sync(); err != nil {
		return fmt.Errorf("sync update.img manifest: %w", err)
	}
	if err := tmp.Close(); err != nil {
		return fmt.Errorf("close update.img manifest: %w", err)
	}
	if err := os.Link(tmpPath, manifestPath); err != nil {
		return fmt.Errorf("publish update.img manifest: %w", err)
	}
	if err := os.Remove(tmpPath); err != nil {
		return fmt.Errorf("remove temporary update.img manifest: %w", err)
	}
	cleanup = false
	return nil
}

func joinUpdateImageItems(items []string) string {
	if len(items) == 0 {
		return "无分区条目"
	}
	return fmt.Sprintf("%v", items)
}
