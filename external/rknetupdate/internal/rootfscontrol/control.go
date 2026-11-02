package rootfscontrol

import (
	"context"
	"crypto/subtle"
	"errors"
	"fmt"
	"path/filepath"

	"rknetupdate/internal/recoveryapply"
)

const (
	ProductionStageDir    = "/var/lib/rknetupdate/rootfs-v2"
	ProductionManifest    = ProductionStageDir + "/rootfs.manifest.json"
	ProductionDisk        = "/dev/mmcblk0"
	ProductionRootfs      = "/dev/disk/by-partlabel/rootfs"
	ProductionMisc        = "/dev/disk/by-partlabel/misc"
	ProductionE2fsck      = "/usr/sbin/e2fsck"
	ProductionParent      = "/dev/mmcblk0"
	ProductionRootfsBlock = "/dev/mmcblk0p6"
)

type Config struct {
	ManifestPath string
	DiskPath     string
	RootfsPath   string
	MiscPath     string
	E2fsckPath   string
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
	ImagePath       string `json:"image_path,omitempty"`
	ImageSize       int64  `json:"image_size,omitempty"`
	SHA256          string `json:"sha256,omitempty"`
	FilesystemUUID  string `json:"filesystem_uuid,omitempty"`
	BlockSize       uint64 `json:"block_size,omitempty"`
	BlockCount      uint64 `json:"block_count,omitempty"`
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
		MiscPath:     ProductionMisc,
		E2fsckPath:   ProductionE2fsck,
		ParentPath:   ProductionParent,
	}
}

func Prepare(ctx context.Context, cfg Config) (Result, error) {
	return run(ctx, "prepare-rootfs", cfg, false)
}

func VerifyPrepared(ctx context.Context, cfg Config) (Result, error) {
	return run(ctx, "reboot-rootfs", cfg, true)
}

func run(ctx context.Context, action string, cfg Config, reboot bool) (Result, error) {
	result := Result{Schema: 2, Action: action, ManifestPath: cfg.ManifestPath}
	if err := cfg.Validate(); err != nil {
		return fail(result, err)
	}
	manifest, imagePath, ext4, err := recoveryapply.ValidateRootfsImageV2Metadata(cfg.ManifestPath, cfg.TestMode)
	if err != nil {
		return fail(result, err)
	}
	result.ImagePath = imagePath
	result.ImageSize = manifest.Size
	result.SHA256 = manifest.SHA256
	result.FilesystemUUID = ext4.UUID
	result.BlockSize = ext4.BlockSize
	result.BlockCount = ext4.BlockCount
	expectedConfirmation := confirmation(action, manifest.SHA256)
	if err := validateConfirmation(expectedConfirmation, cfg.Confirmation); err != nil {
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
		return fail(result, errors.New("normal-system prepare requires p6 rootfs to reach GPT last usable LBA"))
	}
	rootfs, err := recoveryapply.ResolveDevice(cfg.RootfsPath, "rootfs", cfg.ParentPath, cfg.TestMode)
	if err != nil {
		return fail(result, fmt.Errorf("validate rootfs partition: %w", err))
	}
	if !cfg.TestMode && filepath.Clean(rootfs.Path) != ProductionRootfsBlock {
		return fail(result, fmt.Errorf("rootfs PARTLABEL must resolve to %s", ProductionRootfsBlock))
	}
	misc, err := recoveryapply.ResolveDevice(cfg.MiscPath, "misc", cfg.ParentPath, cfg.TestMode)
	if err != nil {
		return fail(result, fmt.Errorf("validate misc partition: %w", err))
	}
	request := recoveryapply.RootfsBCBRequest{
		Schema:         recoveryapply.RootfsRequestSchema,
		Size:           manifest.Size,
		SHA256:         manifest.SHA256,
		FilesystemUUID: ext4.UUID,
		BlockSize:      ext4.BlockSize,
		BlockCount:     ext4.BlockCount,
		P6Start:        p6.FirstLBA,
		P6TypeGUID:     p6.TypeGUID,
		P6UniqueGUID:   p6.UniqueGUID,
		P6Name:         p6.Name,
	}
	result.P6Start = p6.FirstLBA
	result.P6TypeGUID = p6.TypeGUID
	result.P6UniqueGUID = p6.UniqueGUID
	if reboot {
		actual, phase, err := recoveryapply.ReadRootfsBCB(misc)
		if err != nil {
			return fail(result, err)
		}
		if actual != request || phase != recoveryapply.RootfsPhasePrepared {
			return fail(result, errors.New("prepared rootfs BCB does not exactly match staged image and GPT"))
		}
		result.BCBPhase = phase
		result.RebootRequested = true
	} else {
		if err := recoveryapply.PrepareRootfsBCB(misc, request); err != nil {
			return fail(result, err)
		}
		actual, phase, err := recoveryapply.ReadRootfsBCB(misc)
		if err != nil || actual != request || phase != recoveryapply.RootfsPhasePrepared {
			return fail(result, errors.New("rootfs BCB exact readback verification failed"))
		}
		result.BCBPhase = phase
	}
	result.Success = true
	return result, nil
}

func validateConfirmation(expected, actual string) error {
	if subtle.ConstantTimeCompare([]byte(actual), []byte(expected)) != 1 {
		return fmt.Errorf("confirmation phrase must exactly match %q", expected)
	}
	return nil
}

func (c Config) Validate() error {
	if !c.AllowWrite {
		return errors.New("rootfs prepare/reboot requires --allow-write")
	}
	paths := map[string]string{
		"manifest": c.ManifestPath,
		"disk":     c.DiskPath,
		"rootfs":   c.RootfsPath,
		"misc":     c.MiscPath,
		"e2fsck":   c.E2fsckPath,
		"parent":   c.ParentPath,
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
		"manifest": ProductionManifest,
		"disk":     ProductionDisk,
		"rootfs":   ProductionRootfs,
		"misc":     ProductionMisc,
		"parent":   ProductionParent,
	}
	for name, want := range expected {
		if filepath.Clean(paths[name]) != want {
			return fmt.Errorf("production %s path is fixed at %s", name, want)
		}
	}
	if !IsSupportedProductionE2fsck(c.E2fsckPath) {
		return fmt.Errorf("production e2fsck path must be %s or /sbin/e2fsck", ProductionE2fsck)
	}
	return nil
}

// IsSupportedProductionE2fsck accepts the standard Ubuntu/Debian and
// BusyBox/Buildroot locations without allowing an arbitrary executable path.
func IsSupportedProductionE2fsck(path string) bool {
	cleaned := filepath.Clean(path)
	return cleaned == ProductionE2fsck || cleaned == "/sbin/e2fsck"
}

func PrepareConfirmation(sha256 string) string {
	return "PREPARE_ROOTFS_V2:" + sha256
}

func RebootConfirmation(sha256 string) string {
	return "REBOOT_ROOTFS_V2:" + sha256
}

func confirmation(action, sha256 string) string {
	if action == "prepare-rootfs" {
		return PrepareConfirmation(sha256)
	}
	return RebootConfirmation(sha256)
}

func fail(result Result, err error) (Result, error) {
	result.Error = err.Error()
	return result, err
}
