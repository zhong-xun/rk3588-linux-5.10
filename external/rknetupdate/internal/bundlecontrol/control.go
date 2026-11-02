// Package bundlecontrol owns the normal-system half of RKNetUpdate's offline
// multi-image workflow.  It never writes rootfs, boot, or uboot; it only
// validates the staged set and prepares a hash-bound BCB request for recovery.
package bundlecontrol

import (
	"context"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"

	"rknetupdate/internal/recoveryapply"
)

const (
	ProductionStageDir = "/var/lib/rknetupdate/bundle-v1"
	ProductionManifest = ProductionStageDir + "/" + recoveryapply.BundleManifestName
	ProductionDisk     = "/dev/mmcblk0"
	ProductionRootfs   = "/dev/disk/by-partlabel/rootfs"
	ProductionBoot     = "/dev/disk/by-partlabel/boot"
	ProductionUboot    = "/dev/disk/by-partlabel/uboot"
	ProductionMisc     = "/dev/disk/by-partlabel/misc"
	ProductionParent   = "/dev/mmcblk0"
)

type Config struct {
	ManifestPath string
	DiskPath     string
	RootfsPath   string
	BootPath     string
	UbootPath    string
	MiscPath     string
	ParentPath   string
	AllowWrite   bool
	Confirmation string
	TestMode     bool
}

type Result struct {
	Schema          int    `json:"schema"`
	Action          string `json:"action"`
	Success         bool   `json:"success"`
	ManifestPath    string `json:"manifest_path"`
	ManifestSHA256  string `json:"manifest_sha256,omitempty"`
	ArtifactCount   int    `json:"artifact_count,omitempty"`
	ArtifactBytes   int64  `json:"artifact_bytes,omitempty"`
	RootfsUUID      string `json:"rootfs_uuid,omitempty"`
	P6Start         uint64 `json:"p6_start,omitempty"`
	P6TypeGUID      string `json:"p6_type_guid,omitempty"`
	P6UniqueGUID    string `json:"p6_unique_guid,omitempty"`
	BCBPhase        string `json:"bcb_phase,omitempty"`
	RebootRequested bool   `json:"reboot_requested"`
	Error           string `json:"error,omitempty"`
}

func DefaultConfig() Config {
	return Config{
		ManifestPath: ProductionManifest,
		DiskPath:     ProductionDisk,
		RootfsPath:   ProductionRootfs,
		BootPath:     ProductionBoot,
		UbootPath:    ProductionUboot,
		MiscPath:     ProductionMisc,
		ParentPath:   ProductionParent,
	}
}

func Prepare(ctx context.Context, cfg Config) (Result, error) {
	return run(ctx, "prepare-bundle", cfg, false)
}

func VerifyPrepared(ctx context.Context, cfg Config) (Result, error) {
	return run(ctx, "reboot-bundle", cfg, true)
}

func run(_ context.Context, action string, cfg Config, reboot bool) (Result, error) {
	result := Result{Schema: recoveryapply.BundleSchema, Action: action, ManifestPath: cfg.ManifestPath}
	if err := cfg.Validate(); err != nil {
		return fail(result, err)
	}
	manifest, manifestSHA, err := validateStage(cfg)
	if err != nil {
		return fail(result, err)
	}
	result.ManifestSHA256 = manifestSHA
	result.ArtifactCount = len(manifest.Artifacts)
	if result.ArtifactBytes, err = manifest.TotalSize(); err != nil {
		return fail(result, err)
	}
	rootfsArtifact, err := manifest.Artifact("rootfs")
	if err != nil {
		return fail(result, err)
	}
	result.RootfsUUID = rootfsArtifact.FilesystemUUID
	if err := validateArtifactTargets(cfg, manifest); err != nil {
		return fail(result, err)
	}
	table, err := recoveryapply.ReadGPT(cfg.DiskPath, cfg.TestMode)
	if err != nil {
		return fail(result, err)
	}
	p6, err := table.Partition(6)
	if err != nil {
		return fail(result, err)
	}
	if p6.Name != "rootfs" || p6.LastLBA != table.LastUsable {
		return fail(result, errors.New("normal-system bundle prepare requires p6 rootfs to reach GPT last usable LBA"))
	}
	misc, err := recoveryapply.ResolveDevice(cfg.MiscPath, "misc", cfg.ParentPath, cfg.TestMode)
	if err != nil {
		return fail(result, fmt.Errorf("validate misc partition: %w", err))
	}
	request := recoveryapply.BundleBCBRequest{
		Schema:         recoveryapply.BundleSchema,
		ManifestSHA256: manifestSHA,
		FilesystemUUID: rootfsArtifact.FilesystemUUID,
		BlockSize:      rootfsArtifact.BlockSize,
		BlockCount:     rootfsArtifact.BlockCount,
		P6Start:        p6.FirstLBA,
		P6TypeGUID:     p6.TypeGUID,
		P6UniqueGUID:   p6.UniqueGUID,
		P6Name:         p6.Name,
	}
	result.P6Start = p6.FirstLBA
	result.P6TypeGUID = p6.TypeGUID
	result.P6UniqueGUID = p6.UniqueGUID
	if err := validateConfirmation(confirmation(action, manifestSHA), cfg.Confirmation); err != nil {
		return fail(result, err)
	}
	if reboot {
		actual, phase, err := recoveryapply.ReadBundleBCB(misc)
		if err != nil {
			return fail(result, err)
		}
		if actual != request || phase != recoveryapply.BundlePhasePrepared {
			return fail(result, errors.New("prepared bundle BCB does not exactly match staged manifest and GPT"))
		}
		result.BCBPhase = phase
		result.RebootRequested = true
	} else {
		if err := recoveryapply.PrepareBundleBCB(misc, request); err != nil {
			return fail(result, err)
		}
		actual, phase, err := recoveryapply.ReadBundleBCB(misc)
		if err != nil || actual != request || phase != recoveryapply.BundlePhasePrepared {
			return fail(result, errors.New("bundle BCB exact readback verification failed"))
		}
		result.BCBPhase = phase
	}
	result.Success = true
	return result, nil
}

func validateStage(cfg Config) (recoveryapply.BundleManifest, string, error) {
	manifest, err := recoveryapply.LoadBundleManifest(cfg.ManifestPath, cfg.TestMode)
	if err != nil {
		return recoveryapply.BundleManifest{}, "", err
	}
	if filepath.Clean(manifest.ExpectedParent) != filepath.Clean(cfg.ParentPath) {
		return recoveryapply.BundleManifest{}, "", fmt.Errorf("bundle manifest expected_parent %s does not match configured parent %s", manifest.ExpectedParent, cfg.ParentPath)
	}
	manifestSHA, _, err := recoveryapply.HashBundleManifest(cfg.ManifestPath, cfg.TestMode)
	if err != nil {
		return recoveryapply.BundleManifest{}, "", err
	}
	for _, artifact := range manifest.Artifacts {
		path, err := recoveryapply.BundleArtifactPath(filepath.Dir(cfg.ManifestPath), artifact)
		if err != nil {
			return recoveryapply.BundleManifest{}, "", err
		}
		if err := validateArtifactFile(path, artifact, cfg.TestMode); err != nil {
			return recoveryapply.BundleManifest{}, "", err
		}
	}
	return manifest, manifestSHA, nil
}

func validateArtifactFile(path string, artifact recoveryapply.BundleArtifact, testMode bool) error {
	info, err := os.Lstat(path)
	if err != nil {
		return fmt.Errorf("bundle artifact %s: %w", artifact.Target, err)
	}
	if info.Mode()&os.ModeSymlink != 0 || !info.Mode().IsRegular() {
		return fmt.Errorf("bundle artifact %s must be a regular non-symlink file", artifact.Target)
	}
	if !testMode && info.Mode().Perm()&0022 != 0 {
		return fmt.Errorf("bundle artifact %s must not be writable by group or other users", artifact.Target)
	}
	if info.Size() != artifact.Size {
		return fmt.Errorf("bundle artifact %s size %d does not match manifest %d", artifact.Target, info.Size(), artifact.Size)
	}
	f, err := os.Open(path)
	if err != nil {
		return fmt.Errorf("open bundle artifact %s: %w", artifact.Target, err)
	}
	defer f.Close()
	hash := sha256.New()
	n, err := io.Copy(hash, io.LimitReader(f, artifact.Size+1))
	if err != nil {
		return fmt.Errorf("hash bundle artifact %s: %w", artifact.Target, err)
	}
	if n != artifact.Size || subtle.ConstantTimeCompare([]byte(hex.EncodeToString(hash.Sum(nil))), []byte(artifact.SHA256)) != 1 {
		return fmt.Errorf("bundle artifact %s sha256 does not match manifest", artifact.Target)
	}
	if artifact.Target != "rootfs" {
		return nil
	}
	ext4, err := recoveryapply.InspectExt4(f, artifact.Size)
	if err != nil {
		return fmt.Errorf("inspect bundle rootfs ext4: %w", err)
	}
	if ext4.UUID != artifact.FilesystemUUID || ext4.BlockSize != artifact.BlockSize || ext4.BlockCount != artifact.BlockCount {
		return errors.New("bundle rootfs ext4 geometry does not match manifest")
	}
	return nil
}

func validateArtifactTargets(cfg Config, manifest recoveryapply.BundleManifest) error {
	paths := map[string]string{"rootfs": cfg.RootfsPath, "boot": cfg.BootPath, "uboot": cfg.UbootPath}
	for _, artifact := range manifest.Artifacts {
		target, err := recoveryapply.ResolveDevice(paths[artifact.Target], artifact.Target, cfg.ParentPath, cfg.TestMode)
		if err != nil {
			return fmt.Errorf("validate bundle target %s: %w", artifact.Target, err)
		}
		if artifact.Size > target.Size {
			return fmt.Errorf("bundle artifact %s size %d exceeds target capacity %d", artifact.Target, artifact.Size, target.Size)
		}
	}
	return nil
}

func (c Config) Validate() error {
	if !c.AllowWrite {
		return errors.New("bundle prepare/reboot requires --allow-write")
	}
	paths := map[string]string{
		"manifest": c.ManifestPath, "disk": c.DiskPath, "rootfs": c.RootfsPath,
		"boot": c.BootPath, "uboot": c.UbootPath, "misc": c.MiscPath, "parent": c.ParentPath,
	}
	for name, path := range paths {
		if path == "" || !filepath.IsAbs(path) {
			return fmt.Errorf("%s path must be absolute", name)
		}
	}
	if c.TestMode {
		return nil
	}
	expected := map[string]string{
		"manifest": ProductionManifest, "disk": ProductionDisk, "rootfs": ProductionRootfs,
		"boot": ProductionBoot, "uboot": ProductionUboot, "misc": ProductionMisc, "parent": ProductionParent,
	}
	for name, want := range expected {
		if filepath.Clean(paths[name]) != want {
			return fmt.Errorf("production %s path is fixed at %s", name, want)
		}
	}
	return nil
}

func PrepareConfirmation(manifestSHA256 string) string { return "PREPARE_BUNDLE_V1:" + manifestSHA256 }

func RebootConfirmation(manifestSHA256 string) string { return "REBOOT_BUNDLE_V1:" + manifestSHA256 }

func confirmation(action, manifestSHA256 string) string {
	if action == "prepare-bundle" {
		return PrepareConfirmation(manifestSHA256)
	}
	return RebootConfirmation(manifestSHA256)
}

func validateConfirmation(expected, actual string) error {
	if subtle.ConstantTimeCompare([]byte(expected), []byte(actual)) != 1 {
		return fmt.Errorf("confirmation phrase must exactly match %q", expected)
	}
	return nil
}

func fail(result Result, err error) (Result, error) {
	result.Error = err.Error()
	return result, err
}
