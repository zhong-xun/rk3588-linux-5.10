// Package updateimgcontrol owns the normal-system half of the complete
// Rockchip update.img OTA workflow.  It only validates a staged package and
// writes a hash-bound private BCB request; all partition writes happen later
// in recovery.
package updateimgcontrol

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
	"regexp"
	"strconv"
	"strings"

	"rknetupdate/internal/recoveryapply"
)

const (
	ProductionStageDir = "/var/lib/rknetupdate/updateimg-v1"
	ProductionManifest = ProductionStageDir + "/" + recoveryapply.UpdateImageManifestName
	ProductionDisk     = "/dev/mmcblk0"
	ProductionRootfs   = "/dev/disk/by-partlabel/rootfs"
	ProductionRecovery = "/dev/disk/by-partlabel/recovery"
	ProductionMisc     = "/dev/disk/by-partlabel/misc"
	ProductionParent   = "/dev/mmcblk0"
)

type Config struct {
	ManifestPath string
	DiskPath     string
	RootfsPath   string
	RecoveryPath string
	MiscPath     string
	ParentPath   string
	AllowWrite   bool
	Confirmation string
	TestMode     bool
}

type Result struct {
	Schema          int      `json:"schema"`
	Action          string   `json:"action"`
	Success         bool     `json:"success"`
	ManifestPath    string   `json:"manifest_path"`
	PackagePath     string   `json:"package_path,omitempty"`
	PackageSize     int64    `json:"package_size,omitempty"`
	PackageSHA256   string   `json:"package_sha256,omitempty"`
	ManifestSHA256  string   `json:"manifest_sha256,omitempty"`
	Items           []string `json:"items,omitempty"`
	RecoverySize    uint64   `json:"recovery_size,omitempty"`
	RecoverySHA256  string   `json:"recovery_sha256,omitempty"`
	P6Start         uint64   `json:"p6_start,omitempty"`
	P6TypeGUID      string   `json:"p6_type_guid,omitempty"`
	P6UniqueGUID    string   `json:"p6_unique_guid,omitempty"`
	BCBPhase        string   `json:"bcb_phase,omitempty"`
	RebootRequested bool     `json:"reboot_requested"`
	Error           string   `json:"error,omitempty"`
}

func DefaultConfig() Config {
	return Config{
		ManifestPath: ProductionManifest,
		DiskPath:     ProductionDisk,
		RootfsPath:   ProductionRootfs,
		RecoveryPath: ProductionRecovery,
		MiscPath:     ProductionMisc,
		ParentPath:   ProductionParent,
	}
}

func Prepare(ctx context.Context, cfg Config) (Result, error) {
	return run(ctx, "prepare-updateimg", cfg, false)
}

func VerifyPrepared(ctx context.Context, cfg Config) (Result, error) {
	return run(ctx, "reboot-updateimg", cfg, true)
}

func run(_ context.Context, action string, cfg Config, reboot bool) (Result, error) {
	result := Result{Schema: recoveryapply.UpdateImageSchema, Action: action, ManifestPath: cfg.ManifestPath}
	if err := cfg.Validate(); err != nil {
		return fail(result, err)
	}
	manifest, imagePath, manifestSHA, err := validateStage(cfg)
	if err != nil {
		return fail(result, err)
	}
	result.PackagePath = imagePath
	result.PackageSize = manifest.Size
	result.PackageSHA256 = manifest.SHA256
	result.ManifestSHA256 = manifestSHA
	for _, item := range manifest.Items {
		result.Items = append(result.Items, item.Name)
	}
	recoveryItem, _ := manifest.Item("recovery")
	result.RecoverySize = recoveryItem.Size
	result.RecoverySHA256 = recoveryItem.SHA256

	table, err := recoveryapply.ReadGPT(cfg.DiskPath, cfg.TestMode)
	if err != nil {
		return fail(result, err)
	}
	p6, err := table.Partition(6)
	if err != nil {
		return fail(result, err)
	}
	if p6.Name != "rootfs" || p6.LastLBA != table.LastUsable {
		return fail(result, errors.New("normal-system update.img prepare requires p6 rootfs to reach GPT last usable LBA"))
	}
	if err := validateParameterLayout(imagePath, manifest, table); err != nil {
		return fail(result, err)
	}
	rootfs, err := recoveryapply.ResolveDevice(cfg.RootfsPath, "rootfs", cfg.ParentPath, cfg.TestMode)
	if err != nil {
		return fail(result, fmt.Errorf("validate update.img rootfs: %w", err))
	}
	if rootfs.Size <= manifest.Size {
		return fail(result, errors.New("rootfs partition is too small to create a temporary update.img userdata stage"))
	}
	recovery, err := recoveryapply.ResolveDevice(cfg.RecoveryPath, "recovery", cfg.ParentPath, cfg.TestMode)
	if err != nil {
		return fail(result, fmt.Errorf("validate update.img recovery: %w", err))
	}
	misc, err := recoveryapply.ResolveDevice(cfg.MiscPath, "misc", cfg.ParentPath, cfg.TestMode)
	if err != nil {
		return fail(result, fmt.Errorf("validate update.img misc: %w", err))
	}
	if err := validatePackageTargetCapacity(manifest, cfg, rootfs, recovery, misc); err != nil {
		return fail(result, err)
	}
	if miscItem, err := manifest.Item("misc"); err == nil {
		if err := recoveryapply.ValidateUpdateImageMiscItem(imagePath, miscItem); err != nil {
			return fail(result, err)
		}
	}
	request := recoveryapply.UpdateImageBCBRequest{
		Schema: recoveryapply.UpdateImageSchema, ManifestSHA256: manifestSHA,
		PackageSize: manifest.Size, PackageSHA256: manifest.SHA256,
		P6Start: p6.FirstLBA, P6TypeGUID: p6.TypeGUID, P6UniqueGUID: p6.UniqueGUID, P6Name: p6.Name,
	}
	result.P6Start = p6.FirstLBA
	result.P6TypeGUID = p6.TypeGUID
	result.P6UniqueGUID = p6.UniqueGUID
	if err := validateConfirmation(confirmation(action, manifest.SHA256), cfg.Confirmation); err != nil {
		return fail(result, err)
	}
	if reboot {
		actual, phase, err := recoveryapply.ReadUpdateImageMigrationBCB(misc)
		if err != nil {
			return fail(result, err)
		}
		if actual != request || phase != recoveryapply.UpdateImagePhasePrepared {
			return fail(result, errors.New("prepared update.img BCB does not exactly match staged package and GPT"))
		}
		result.BCBPhase = phase
		result.RebootRequested = true
	} else {
		if err := recoveryapply.PrepareUpdateImageMigrationBCB(misc, request); err != nil {
			return fail(result, err)
		}
		actual, phase, err := recoveryapply.ReadUpdateImageMigrationBCB(misc)
		if err != nil || actual != request || phase != recoveryapply.UpdateImagePhasePrepared {
			return fail(result, errors.New("update.img BCB exact readback verification failed"))
		}
		result.BCBPhase = phase
	}
	result.Success = true
	return result, nil
}

func validateStage(cfg Config) (recoveryapply.UpdateImageManifest, string, string, error) {
	manifest, imagePath, err := recoveryapply.ValidateUpdateImageStage(cfg.ManifestPath, cfg.TestMode)
	if err != nil {
		return recoveryapply.UpdateImageManifest{}, "", "", err
	}
	if filepath.Clean(manifest.ExpectedParent) != filepath.Clean(cfg.ParentPath) {
		return recoveryapply.UpdateImageManifest{}, "", "", fmt.Errorf("update.img manifest expected_parent %s does not match configured parent %s", manifest.ExpectedParent, cfg.ParentPath)
	}
	if err := validateSupportedItems(manifest); err != nil {
		return recoveryapply.UpdateImageManifest{}, "", "", err
	}
	manifestSHA, err := hashFile(cfg.ManifestPath, 1<<20)
	if err != nil {
		return recoveryapply.UpdateImageManifest{}, "", "", fmt.Errorf("hash update.img manifest: %w", err)
	}
	return manifest, imagePath, manifestSHA, nil
}

func validateSupportedItems(manifest recoveryapply.UpdateImageManifest) error {
	// package-file and bootloader are present in SDK-generated full update.img
	// files. The recovery flow handles bootloader via the SDK engine and writes
	// misc only after its private BCB is no longer needed.
	supported := map[string]bool{
		"bootloader": true, "parameter": true, "uboot": true, "trust": true,
		"boot": true, "recovery": true, "rootfs": true, "oem": true,
		"misc": true, "package-file": true,
	}
	for _, item := range manifest.Items {
		switch item.Name {
		case "userdata":
			return errors.New("update.img item userdata is not supported: RKNetUpdate owns temporary p7 userdata staging")
		case "uboot_a", "uboot_b", "boot_a", "boot_b", "system_a", "system_b":
			return fmt.Errorf("update.img item %q is an A/B target; this RK3588 workflow only supports the single-slot p1..p6 layout", item.Name)
		case "backup":
			return errors.New("update.img item backup is not supported by the single-slot OTA workflow")
		}
		if !supported[item.Name] {
			return fmt.Errorf("update.img item %q is not supported by the bundled RK3588 recovery update engine", item.Name)
		}
	}
	return nil
}

// validatePackageTargetCapacity catches a package that the SDK update engine
// would otherwise only reject after it has already entered recovery.  Unlike
// the parameter/bootloader/package-file metadata, every item selected by the
// OTA mask must resolve to a real partition on this exact eMMC and fit inside
// it.  The target names deliberately match update_engine/update.c.
func validatePackageTargetCapacity(manifest recoveryapply.UpdateImageManifest, cfg Config, rootfs, recovery, misc recoveryapply.Device) error {
	targets := map[string]recoveryapply.Device{
		"rootfs":   rootfs,
		"recovery": recovery,
		"misc":     misc,
	}
	for _, name := range []string{"uboot", "trust", "boot", "oem"} {
		if _, err := manifest.Item(name); err != nil {
			continue
		}
		target, err := recoveryapply.ResolveDevice(filepath.Join("/dev/disk/by-partlabel", name), name, cfg.ParentPath, cfg.TestMode)
		if err != nil {
			return fmt.Errorf("update.img item %s has no compatible target partition: %w", name, err)
		}
		targets[name] = target
	}
	for _, name := range []string{"uboot", "trust", "boot", "rootfs", "oem", "recovery", "misc"} {
		item, err := manifest.Item(name)
		if err != nil {
			continue
		}
		target := targets[name]
		if item.Size > uint64(target.Size) {
			return fmt.Errorf("update.img item %s size %d exceeds %s capacity %d", name, item.Size, target.Path, target.Size)
		}
	}
	return nil
}

var (
	parameterPartitionPattern = regexp.MustCompile(`(?i)(-|0x[0-9a-f]+)@0x([0-9a-f]+)\(([^)]+)\)`)
	parameterUUIDPattern      = regexp.MustCompile(`(?im)^uuid:([a-z0-9_]+)=([0-9a-f-]+)\s*$`)
)

type parameterPartition struct {
	Size  string
	Start uint64
	Name  string
}

// The package parameter is applied through the final GPT restore after p7 is
// removed. That is only safe when it describes exactly the board's existing
// p1..p6 layout, including the growable rootfs end-of-disk convention.
func validateParameterLayout(imagePath string, manifest recoveryapply.UpdateImageManifest, table recoveryapply.GPTTable) error {
	item, err := manifest.Item("parameter")
	if err != nil {
		return err
	}
	data, err := recoveryapply.ReadUpdateImageItem(imagePath, item, 1<<20)
	if err != nil {
		return fmt.Errorf("read update.img parameter item: %w", err)
	}
	text := strings.ReplaceAll(string(data), "\x00", "\n")
	return validateParameterText(text, table)
}

func validateParameterText(text string, table recoveryapply.GPTTable) error {
	if !strings.Contains(text, "TYPE: GPT") {
		return errors.New("update.img parameter item is not a GPT parameter file")
	}
	if strings.Contains(strings.ToLower(text), "(userdata") {
		return errors.New("update.img parameter must not contain a persistent userdata partition; RKNetUpdate owns temporary p7 staging")
	}
	matches := parameterPartitionPattern.FindAllStringSubmatch(text, -1)
	if len(matches) != 6 {
		return fmt.Errorf("update.img parameter must define exactly p1..p6 for this single-slot layout; found %d partition entries", len(matches))
	}
	entries := make([]parameterPartition, 0, len(matches))
	for _, match := range matches {
		start, err := strconv.ParseUint(match[2], 16, 64)
		if err != nil {
			return fmt.Errorf("parse update.img parameter start 0x%s: %w", match[2], err)
		}
		entries = append(entries, parameterPartition{Size: strings.ToLower(match[1]), Start: start, Name: strings.ToLower(match[3])})
	}
	for index, entry := range entries {
		partition, err := table.Partition(index + 1)
		if err != nil {
			return err
		}
		expectedName := strings.ToLower(partition.Name)
		if index == len(entries)-1 {
			if entry.Name != "rootfs:grow" || expectedName != "rootfs" || entry.Size != "-" {
				return errors.New("update.img parameter must end with rootfs:grow")
			}
			if partition.LastLBA != table.LastUsable {
				return errors.New("current rootfs does not reach GPT last usable LBA")
			}
		} else if entry.Name != expectedName || entry.Size == "-" {
			return fmt.Errorf("update.img parameter partition %d is %s, expected fixed %s", index+1, entry.Name, expectedName)
		}
		if entry.Start != partition.FirstLBA {
			return fmt.Errorf("update.img parameter %s start 0x%x does not match current p%d start 0x%x", expectedName, entry.Start, index+1, partition.FirstLBA)
		}
		if index == len(entries)-1 {
			continue
		}
		size, err := strconv.ParseUint(strings.TrimPrefix(entry.Size, "0x"), 16, 64)
		if err != nil {
			return fmt.Errorf("parse update.img parameter %s size %s: %w", expectedName, entry.Size, err)
		}
		if size != partition.SizeSectors() {
			return fmt.Errorf("update.img parameter %s size 0x%x does not match current p%d size 0x%x", expectedName, size, index+1, partition.SizeSectors())
		}
	}
	for _, match := range parameterUUIDPattern.FindAllStringSubmatch(text, -1) {
		var number int
		switch strings.ToLower(match[1]) {
		case "boot":
			number = 3
		case "rootfs":
			number = 6
		default:
			continue
		}
		partition, err := table.Partition(number)
		if err != nil {
			return err
		}
		if !strings.EqualFold(match[2], partition.UniqueGUID) {
			return fmt.Errorf("update.img parameter %s UUID %s does not match current p%d UUID %s", match[1], match[2], number, partition.UniqueGUID)
		}
	}
	return nil
}

func (c Config) Validate() error {
	if !c.AllowWrite {
		return errors.New("update.img prepare/reboot requires --allow-write")
	}
	paths := map[string]string{
		"manifest": c.ManifestPath, "disk": c.DiskPath, "rootfs": c.RootfsPath,
		"recovery": c.RecoveryPath, "misc": c.MiscPath, "parent": c.ParentPath,
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
		"recovery": ProductionRecovery, "misc": ProductionMisc, "parent": ProductionParent,
	}
	for name, want := range expected {
		if filepath.Clean(paths[name]) != want {
			return fmt.Errorf("production %s path is fixed at %s", name, want)
		}
	}
	return nil
}

func PrepareConfirmation(sha256 string) string { return "PREPARE_UPDATEIMG_V1:" + sha256 }
func RebootConfirmation(sha256 string) string  { return "REBOOT_UPDATEIMG_V1:" + sha256 }

func confirmation(action, sha256 string) string {
	if action == "prepare-updateimg" {
		return PrepareConfirmation(sha256)
	}
	return RebootConfirmation(sha256)
}

func validateConfirmation(expected, actual string) error {
	if subtle.ConstantTimeCompare([]byte(expected), []byte(actual)) != 1 {
		return fmt.Errorf("confirmation phrase must exactly match %q", expected)
	}
	return nil
}

func hashFile(path string, limit int64) (string, error) {
	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()
	hash := sha256.New()
	n, err := io.Copy(hash, io.LimitReader(f, limit+1))
	if err != nil {
		return "", err
	}
	if n > limit {
		return "", errors.New("file exceeds hash limit")
	}
	return hex.EncodeToString(hash.Sum(nil)), nil
}

func fail(result Result, err error) (Result, error) {
	result.Error = err.Error()
	return result, err
}
