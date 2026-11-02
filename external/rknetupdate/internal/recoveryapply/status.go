package recoveryapply

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"time"
)

type Status struct {
	Schema               int       `json:"schema"`
	RunID                string    `json:"run_id"`
	Mode                 string    `json:"mode"`
	Phase                string    `json:"phase"`
	Success              bool      `json:"success"`
	Applied              bool      `json:"applied"`
	ManifestPath         string    `json:"manifest_path,omitempty"`
	StatusPath           string    `json:"status_path"`
	ImagePath            string    `json:"image_path,omitempty"`
	ImageSize            int64     `json:"image_size,omitempty"`
	FilesystemSize       uint64    `json:"filesystem_size,omitempty"`
	SHA256               string    `json:"sha256,omitempty"`
	TargetPath           string    `json:"target_path,omitempty"`
	ResolvedTargetPath   string    `json:"resolved_target_path,omitempty"`
	ExpectedParent       string    `json:"expected_parent,omitempty"`
	ReadbackSHA256       string    `json:"readback_sha256,omitempty"`
	BytesWritten         int64     `json:"bytes_written,omitempty"`
	RequestDisposition   string    `json:"request_disposition,omitempty"`
	BCBCleared           bool      `json:"bcb_cleared"`
	SmokeRequestVerified bool      `json:"smoke_request_verified,omitempty"`
	E2fsckSummary        string    `json:"e2fsck_summary,omitempty"`
	Error                string    `json:"error,omitempty"`
	StartedAt            time.Time `json:"started_at"`
	UpdatedAt            time.Time `json:"updated_at"`
}

func newStatus(cfg Config) Status {
	mode := "check"
	statusPath := cfg.StatusPath
	manifestPath := cfg.ManifestPath
	if cfg.Apply {
		mode = "apply"
	} else if cfg.Smoke {
		mode = "smoke"
		statusPath = cfg.SmokeStatusPath
		manifestPath = ""
	}
	now := time.Now().UTC()
	return Status{
		Schema:         1,
		RunID:          randomID(),
		Mode:           mode,
		Phase:          "starting",
		ManifestPath:   manifestPath,
		StatusPath:     statusPath,
		TargetPath:     cfg.TargetPath,
		ExpectedParent: cfg.ExpectedParentPath,
		StartedAt:      now,
		UpdatedAt:      now,
	}
}

func randomID() string {
	buffer := make([]byte, 8)
	if _, err := rand.Read(buffer); err != nil {
		return fmt.Sprintf("time-%d", time.Now().UnixNano())
	}
	return hex.EncodeToString(buffer)
}

func writeStatus(path string, status Status) error {
	status.UpdatedAt = time.Now().UTC()
	dir := filepath.Dir(filepath.Clean(path))
	info, err := os.Stat(dir)
	if err != nil {
		return fmt.Errorf("inspect status directory: %w", err)
	}
	if !info.IsDir() {
		return fmt.Errorf("status parent %s is not a directory", dir)
	}
	tmp, err := os.CreateTemp(dir, ".rootfs.status.*")
	if err != nil {
		return fmt.Errorf("create temporary status: %w", err)
	}
	tmpPath := tmp.Name()
	removeTemp := true
	defer func() {
		if removeTemp {
			_ = os.Remove(tmpPath)
		}
	}()
	if err := tmp.Chmod(0600); err != nil {
		_ = tmp.Close()
		return fmt.Errorf("set status permissions: %w", err)
	}
	encoder := json.NewEncoder(tmp)
	encoder.SetIndent("", "  ")
	if err := encoder.Encode(status); err != nil {
		_ = tmp.Close()
		return fmt.Errorf("encode status: %w", err)
	}
	if err := tmp.Sync(); err != nil {
		_ = tmp.Close()
		return fmt.Errorf("sync status: %w", err)
	}
	if err := tmp.Close(); err != nil {
		return fmt.Errorf("close status: %w", err)
	}
	if err := os.Rename(tmpPath, path); err != nil {
		return fmt.Errorf("publish status: %w", err)
	}
	removeTemp = false
	dirFile, err := os.Open(dir)
	if err != nil {
		return fmt.Errorf("open status directory: %w", err)
	}
	defer dirFile.Close()
	if err := dirFile.Sync(); err != nil {
		return fmt.Errorf("sync status directory: %w", err)
	}
	return nil
}
