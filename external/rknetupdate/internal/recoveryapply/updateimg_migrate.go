package recoveryapply

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strconv"
	"time"
)

const (
	ProductionUpdateImageDiskPath      = "/dev/mmcblk0"
	ProductionUpdateImageRootfsPath    = "/dev/disk/by-partlabel/rootfs"
	ProductionUpdateImageMiscPath      = "/dev/disk/by-partlabel/misc"
	ProductionUpdateImageUserdataPath  = "/dev/disk/by-partlabel/userdata"
	ProductionUpdateImageMountInfoPath = "/proc/self/mountinfo"
	ProductionUpdateImageSourceMount   = "/mnt/rknetupdate-updateimg"
	ProductionUpdateImageUserdataMount = "/userdata"
	ProductionUpdateImageStatusPath    = "/run/rknetupdate-updateimg-v1.status.json"
	ProductionUpdateImageSGDiskPath    = "/usr/sbin/sgdisk"
	ProductionUpdateImagePartprobePath = "/usr/sbin/partprobe"
	ProductionUpdateImageE2fsckPath    = "/usr/sbin/e2fsck"
	ProductionUpdateImageResize2fsPath = "/usr/sbin/resize2fs"
	ProductionUpdateImageMkfsExt4Path  = "/usr/sbin/mkfs.ext4"

	updateImageSourceRelative = "var/lib/rknetupdate/updateimg-v1"
	updateImageStageRelative  = "rknetupdate/updateimg-v1"
)

type UpdateImageMigrationConfig struct {
	DiskPath           string
	RootfsPath         string
	MiscPath           string
	UserdataPath       string
	ExpectedParentPath string
	MountInfoPath      string
	RecoveryMarkerPath string
	SourceMountPath    string
	UserdataMountPath  string
	StatusPath         string
	SGDiskPath         string
	PartprobePath      string
	E2fsckPath         string
	Resize2fsPath      string
	MkfsExt4Path       string
	StageMarginBytes   int64
	StopAfterPhase     string
	ToolExtraEnv       []string
	TestMode           bool
	Progress           func(phase, detail string)
	WriteProgress      WriteProgress
}

type UpdateImageMigrationStatus struct {
	Schema       int                   `json:"schema"`
	Phase        string                `json:"phase"`
	Success      bool                  `json:"success"`
	HandoffReady bool                  `json:"handoff_ready"`
	BCBCleared   bool                  `json:"bcb_cleared"`
	Request      UpdateImageBCBRequest `json:"request"`
	Error        string                `json:"error,omitempty"`
	UpdatedAt    time.Time             `json:"updated_at"`
}

type updateImageStageState struct {
	Schema  int                   `json:"schema"`
	Phase   string                `json:"phase"`
	Request UpdateImageBCBRequest `json:"request"`
}

func DefaultUpdateImageMigrationConfig() UpdateImageMigrationConfig {
	return UpdateImageMigrationConfig{
		DiskPath:           ProductionUpdateImageDiskPath,
		RootfsPath:         ProductionUpdateImageRootfsPath,
		MiscPath:           ProductionUpdateImageMiscPath,
		UserdataPath:       ProductionUpdateImageUserdataPath,
		ExpectedParentPath: ProductionExpectedParentPath,
		MountInfoPath:      ProductionUpdateImageMountInfoPath,
		RecoveryMarkerPath: ProductionRecoveryMarkerPath,
		SourceMountPath:    ProductionUpdateImageSourceMount,
		UserdataMountPath:  ProductionUpdateImageUserdataMount,
		StatusPath:         ProductionUpdateImageStatusPath,
		SGDiskPath:         ProductionUpdateImageSGDiskPath,
		PartprobePath:      ProductionUpdateImagePartprobePath,
		E2fsckPath:         ProductionUpdateImageE2fsckPath,
		Resize2fsPath:      ProductionUpdateImageResize2fsPath,
		MkfsExt4Path:       ProductionUpdateImageMkfsExt4Path,
	}
}

func (c UpdateImageMigrationConfig) Validate() error {
	paths := map[string]string{
		"disk": c.DiskPath, "rootfs": c.RootfsPath,
		"misc": c.MiscPath, "userdata": c.UserdataPath, "expected-parent": c.ExpectedParentPath,
		"mountinfo": c.MountInfoPath, "marker": c.RecoveryMarkerPath, "source-mount": c.SourceMountPath,
		"userdata-mount": c.UserdataMountPath,
		"status":         c.StatusPath, "sgdisk": c.SGDiskPath,
		"partprobe": c.PartprobePath, "e2fsck": c.E2fsckPath, "resize2fs": c.Resize2fsPath,
		"mkfs.ext4": c.MkfsExt4Path,
	}
	for name, path := range paths {
		if path == "" || !filepath.IsAbs(path) {
			return fmt.Errorf("update.img migration %s path must be absolute", name)
		}
	}
	if c.StageMarginBytes < 0 {
		return errors.New("update.img migration stage margin must not be negative")
	}
	if c.StopAfterPhase != "" && !c.TestMode {
		return errors.New("update.img migration stop-after-phase is test-only")
	}
	if c.StopAfterPhase != "" && c.StopAfterPhase != UpdateImagePhaseShrunk && c.StopAfterPhase != UpdateImagePhaseP7Ready {
		return errors.New("invalid update.img migration stop-after-phase")
	}
	if len(c.ToolExtraEnv) != 0 && !c.TestMode {
		return errors.New("update.img migration tool environment overrides are test-only")
	}
	if c.TestMode {
		return nil
	}
	expected := map[string]string{
		"disk": ProductionUpdateImageDiskPath, "rootfs": ProductionUpdateImageRootfsPath,
		"misc":     ProductionUpdateImageMiscPath,
		"userdata": ProductionUpdateImageUserdataPath, "expected-parent": ProductionExpectedParentPath,
		"mountinfo": ProductionUpdateImageMountInfoPath, "marker": ProductionRecoveryMarkerPath,
		"source-mount": ProductionUpdateImageSourceMount, "userdata-mount": ProductionUpdateImageUserdataMount,
		"status": ProductionUpdateImageStatusPath, "sgdisk": ProductionUpdateImageSGDiskPath,
		"partprobe": ProductionUpdateImagePartprobePath, "e2fsck": ProductionUpdateImageE2fsckPath,
		"resize2fs": ProductionUpdateImageResize2fsPath, "mkfs.ext4": ProductionUpdateImageMkfsExt4Path,
	}
	for name, want := range expected {
		if filepath.Clean(paths[name]) != want {
			return fmt.Errorf("production update.img migration %s path is fixed at %s", name, want)
		}
	}
	return nil
}

// RunUpdateImageMigration prepares the disposable p7 stage and turns the
// private BCB into the canonical Rockchip update-package request. The managed
// rkupdate process then consumes every package item in one OTA pass; the
// package parameter restores the original GPT and removes p7 itself.
func RunUpdateImageMigration(ctx context.Context, cfg UpdateImageMigrationConfig) (UpdateImageMigrationStatus, error) {
	status := UpdateImageMigrationStatus{Schema: UpdateImageSchema, Phase: "starting", UpdatedAt: time.Now().UTC()}
	fail := func(err error) (UpdateImageMigrationStatus, error) {
		status.Success = false
		status.Error = err.Error()
		status.UpdatedAt = time.Now().UTC()
		reportUpdateImageProgress(cfg, "failed", status.Error)
		_ = writeJSONAtomic(cfg.StatusPath, status)
		return status, err
	}
	if err := cfg.Validate(); err != nil {
		return fail(err)
	}
	if err := verifyRecoveryMarker(cfg.RecoveryMarkerPath, cfg.TestMode); err != nil {
		return fail(err)
	}
	misc, err := ResolveRecoveryPartition(cfg.MiscPath, "misc", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return fail(fmt.Errorf("validate update.img misc: %w", err))
	}
	request, phase, err := ReadUpdateImageMigrationBCB(misc)
	if err != nil {
		return fail(err)
	}
	status.Request, status.Phase = request, phase
	if phase != UpdateImagePhasePrepared && phase != UpdateImagePhaseShrunk && phase != UpdateImagePhaseP7Ready && phase != UpdateImagePhaseStaged {
		return fail(fmt.Errorf("unsupported update.img migration phase %s", phase))
	}
	return runUpdateImageStage(ctx, cfg, misc, request, phase, status, fail)
}

func runUpdateImageStage(ctx context.Context, cfg UpdateImageMigrationConfig, misc Device, request UpdateImageBCBRequest, phase string, status UpdateImageMigrationStatus, fail func(error) (UpdateImageMigrationStatus, error)) (UpdateImageMigrationStatus, error) {
	mutator := GPTMutator{SGDiskPath: cfg.SGDiskPath, PartprobePath: cfg.PartprobePath, TestMode: cfg.TestMode, ExtraEnv: cfg.ToolExtraEnv}
	table, p6, p7, err := inspectUpdateImageGPT(cfg, request)
	if err != nil {
		return fail(err)
	}
	if p7 == nil && phase != UpdateImagePhasePrepared && phase != UpdateImagePhaseShrunk {
		return fail(errors.New("update.img p7 staging partition is missing after it was prepared"))
	}
	if p7 == nil {
		if err := prepareUpdateImageTemporaryStage(ctx, cfg, mutator, misc, request, table, p6); err != nil {
			if errors.Is(err, errUpdateImageMigrationTestStop) {
				status.Phase = UpdateImagePhaseShrunk
				return status, nil
			}
			return fail(err)
		}
		misc, err = ResolveRecoveryPartition(cfg.MiscPath, "misc", cfg.ExpectedParentPath, cfg.TestMode)
		if err != nil {
			return fail(err)
		}
		_, phase, err = ReadUpdateImageMigrationBCB(misc)
		if err != nil {
			return fail(err)
		}
		table, p6, p7, err = inspectUpdateImageGPT(cfg, request)
		if err != nil {
			return fail(err)
		}
		if p7 == nil {
			return fail(errors.New("temporary update.img userdata partition was not created"))
		}
		if cfg.TestMode && cfg.StopAfterPhase == UpdateImagePhaseP7Ready {
			status.Phase = UpdateImagePhaseP7Ready
			return status, nil
		}
	}
	if phase != UpdateImagePhaseStaged {
		if err := ensureUpdateImageStaged(ctx, cfg, misc, request, phase); err != nil {
			return fail(err)
		}
		_, phase, err = ReadUpdateImageMigrationBCB(misc)
		if err != nil {
			return fail(err)
		}
	}
	if phase != UpdateImagePhaseStaged {
		return fail(errors.New("update.img stage did not reach the durable handoff phase"))
	}
	if err := ensureUpdateImageFitsShortenedRootfs(cfg, request); err != nil {
		return fail(err)
	}
	if err := writeUpdateImageStageState(cfg, request, "stock-pending"); err != nil {
		return fail(err)
	}
	if err := syncFilesystem(cfg.UserdataMountPath); err != nil {
		return fail(err)
	}
	reportUpdateImageProgress(cfg, phase, "temporary p7 package stage verified; handing off to Rockchip updateEngine")
	if err := HandoffUpdateImageToStockBCB(misc, request); err != nil {
		return fail(err)
	}
	status.Phase = "stock-handoff"
	status.HandoffReady = true
	status.Success = true
	status.UpdatedAt = time.Now().UTC()
	status.Error = ""
	reportUpdateImageProgress(cfg, status.Phase, "standard --update_package BCB prepared; recovery init may start the SDK updater now")
	_ = writeJSONAtomic(cfg.StatusPath, status)
	return status, nil
}

func inspectUpdateImageGPT(cfg UpdateImageMigrationConfig, request UpdateImageBCBRequest) (GPTTable, GPTPartition, *GPTPartition, error) {
	table, err := ReadGPT(cfg.DiskPath, cfg.TestMode)
	if err != nil {
		return GPTTable{}, GPTPartition{}, nil, err
	}
	for number := 1; number <= 6; number++ {
		if _, err := table.Partition(number); err != nil {
			return GPTTable{}, GPTPartition{}, nil, err
		}
	}
	for number := range table.Partitions {
		if number > 7 {
			return GPTTable{}, GPTPartition{}, nil, fmt.Errorf("unexpected GPT partition %d", number)
		}
	}
	p6, _ := table.Partition(6)
	if p6.Name != request.P6Name || p6.TypeGUID != request.P6TypeGUID || p6.UniqueGUID != request.P6UniqueGUID || p6.FirstLBA != request.P6Start {
		return GPTTable{}, GPTPartition{}, nil, errors.New("p6 GPT identity does not match update.img BCB request")
	}
	var p7 *GPTPartition
	if partition, ok := table.Partitions[7]; ok {
		if partition.Name != "userdata" || partition.TypeGUID != linuxFilesystemTypeGUID || partition.FirstLBA != p6.LastLBA+1 || partition.LastLBA != table.LastUsable {
			return GPTTable{}, GPTPartition{}, nil, errors.New("p7 userdata GPT geometry is invalid")
		}
		p7 = &partition
	} else if p6.LastLBA > table.LastUsable {
		return GPTTable{}, GPTPartition{}, nil, errors.New("p6 exceeds GPT last usable LBA")
	}
	return table, p6, p7, nil
}

func prepareUpdateImageTemporaryStage(ctx context.Context, cfg UpdateImageMigrationConfig, mutator GPTMutator, misc Device, request UpdateImageBCBRequest, table GPTTable, p6 GPTPartition) error {
	if p6.LastLBA != table.LastUsable {
		return errors.New("initial update.img p6 must reach GPT last usable LBA")
	}
	if _, _, _, err := validateUpdateImageSourceOnRootfs(cfg, request); err != nil {
		return err
	}
	rootfs, err := ResolveRecoveryPartition(cfg.RootfsPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return err
	}
	if err := ensureUnmounted(rootfs, cfg.SourceMountPath, cfg.MountInfoPath); err != nil {
		return err
	}
	reportUpdateImageProgress(cfg, UpdateImagePhasePrepared, "checking and shrinking p6 for temporary update.img userdata")
	if err := runE2fsckRepair(ctx, cfg.E2fsckPath, rootfs.Path); err != nil {
		return err
	}
	f, err := openDevice(rootfs, os.O_RDONLY)
	if err != nil {
		return fmt.Errorf("inspect rootfs before update.img shrink: %w", err)
	}
	currentFS, err := InspectExt4Device(f, rootfs.Size)
	_ = f.Close()
	if err != nil {
		return err
	}
	minimumBlocks, err := readMinimumBlocks(ctx, cfg.Resize2fsPath, rootfs.Path)
	if err != nil {
		return err
	}
	requiredBytes := requiredStageBytes(request.PackageSize, cfg.StageMarginBytes)
	requiredSectors := (uint64(requiredBytes) + table.SectorSize - 1) / table.SectorSize
	alignment := uint64((1 << 20) / table.SectorSize)
	if alignment == 0 {
		alignment = 1
	}
	if requiredSectors >= table.LastUsable-p6.FirstLBA {
		return errors.New("insufficient disk space for temporary update.img p7")
	}
	p7Start := ((table.LastUsable + 1 - requiredSectors) / alignment) * alignment
	if p7Start <= p6.FirstLBA {
		return errors.New("insufficient aligned disk space for temporary update.img p7")
	}
	p6End := p7Start - 1
	p6Bytes := (p6End - p6.FirstLBA + 1) * table.SectorSize
	targetBlocks := p6Bytes / currentFS.BlockSize
	minSafeBlocks, maxSafeBlocks, err := shrinkVerificationRange(minimumBlocks, targetBlocks)
	if err != nil {
		return fmt.Errorf("insufficient update.img shrink headroom: %w", err)
	}
	if err := runResize2fs(ctx, cfg.Resize2fsPath, rootfs.Path, strconv.FormatUint(targetBlocks, 10)); err != nil {
		return err
	}
	if err := verifyFilesystemGeometry(rootfs, currentFS.UUID, currentFS.BlockSize, minSafeBlocks, maxSafeBlocks); err != nil {
		return fmt.Errorf("verify shrunk update.img rootfs: %w", err)
	}
	if err := UpdateUpdateImageBCBPhase(misc, request, UpdateImagePhaseShrunk); err != nil {
		return err
	}
	if err := mutator.CreateStage(ctx, cfg.DiskPath, p6, p6End, p7Start, table.LastUsable); err != nil {
		return err
	}
	if cfg.TestMode && cfg.StopAfterPhase == UpdateImagePhaseShrunk {
		return errUpdateImageMigrationTestStop
	}
	if _, err := waitForRecoveryPartition(cfg.RootfsPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode, 10*time.Second); err != nil {
		return err
	}
	if _, err := waitForRecoveryPartition(cfg.UserdataPath, "userdata", cfg.ExpectedParentPath, cfg.TestMode, 10*time.Second); err != nil {
		return err
	}
	misc, err = waitForRecoveryPartition(cfg.MiscPath, "misc", cfg.ExpectedParentPath, cfg.TestMode, 10*time.Second)
	if err != nil {
		return err
	}
	if err := UpdateUpdateImageBCBPhase(misc, request, UpdateImagePhaseP7Ready); err != nil {
		return err
	}
	reportUpdateImageProgress(cfg, UpdateImagePhaseP7Ready, "temporary userdata p7 created")
	return nil
}

func ensureUpdateImageStaged(ctx context.Context, cfg UpdateImageMigrationConfig, misc Device, request UpdateImageBCBRequest, phase string) error {
	userdata, err := ResolveRecoveryPartition(cfg.UserdataPath, "userdata", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return err
	}
	if phase == UpdateImagePhaseStaged {
		if err := ensureMounted(userdata, cfg.UserdataMountPath, false, cfg.MountInfoPath); err != nil {
			return err
		}
		if _, _, err := validateUpdateImageStageOnP7(cfg, request); err != nil {
			return fmt.Errorf("persisted update.img p7 stage is invalid: %w", err)
		}
		return nil
	}
	if phase != UpdateImagePhaseP7Ready && phase != UpdateImagePhaseShrunk && phase != UpdateImagePhasePrepared {
		return fmt.Errorf("cannot stage update.img from phase %s", phase)
	}
	if err := ensureUnmounted(userdata, cfg.UserdataMountPath, cfg.MountInfoPath); err != nil {
		return err
	}
	// A newly created p7 is raw GPT space.  Formatting it directly prevents a
	// misleading ext4 mount failure in the serial log.
	if err := runCommandChecked(ctx, cfg.MkfsExt4Path, "-F", "-L", "userdata", userdata.Path); err != nil {
		return err
	}
	if err := ensureMounted(userdata, cfg.UserdataMountPath, false, cfg.MountInfoPath); err != nil {
		return err
	}
	manifest, sourceImage, sourceManifest, err := validateUpdateImageSourceOnRootfs(cfg, request)
	if err != nil {
		return err
	}
	stageDir := filepath.Join(cfg.UserdataMountPath, updateImageStageRelative)
	if err := os.MkdirAll(stageDir, 0700); err != nil {
		return err
	}
	stageImage := filepath.Join(stageDir, UpdateImageFileName)
	if err := copyFileSync(sourceImage, stageImage, 0600); err != nil {
		return fmt.Errorf("copy update.img to p7: %w", err)
	}
	if err := copyFileSync(sourceManifest, filepath.Join(stageDir, UpdateImageManifestName), 0600); err != nil {
		return fmt.Errorf("copy update.img manifest to p7: %w", err)
	}
	packagePath := UpdateImagePackagePath
	if cfg.TestMode {
		packagePath = filepath.Join(cfg.UserdataMountPath, UpdateImageFileName)
	}
	if err := os.Link(stageImage, packagePath); err != nil {
		return fmt.Errorf("publish stock recovery update.img path: %w", err)
	}
	if err := writeUpdateImageStageState(cfg, request, "staged"); err != nil {
		return err
	}
	if err := syncFilesystem(cfg.UserdataMountPath); err != nil {
		return err
	}
	if _, _, err := validateUpdateImageStageOnP7(cfg, request); err != nil {
		return err
	}
	_ = manifest
	rootfs, err := ResolveRecoveryPartition(cfg.RootfsPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return err
	}
	if err := ensureUnmounted(rootfs, cfg.SourceMountPath, cfg.MountInfoPath); err != nil {
		return err
	}
	if err := UpdateUpdateImageBCBPhase(misc, request, UpdateImagePhaseStaged); err != nil {
		return err
	}
	reportUpdateImageProgress(cfg, UpdateImagePhaseStaged, "update.img and manifest copied to durable p7 stage")
	return nil
}

func validateUpdateImageSourceOnRootfs(cfg UpdateImageMigrationConfig, request UpdateImageBCBRequest) (UpdateImageManifest, string, string, error) {
	rootfs, err := ResolveRecoveryPartition(cfg.RootfsPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return UpdateImageManifest{}, "", "", err
	}
	if err := ensureMounted(rootfs, cfg.SourceMountPath, true, cfg.MountInfoPath); err != nil {
		return UpdateImageManifest{}, "", "", err
	}
	manifestPath := filepath.Join(cfg.SourceMountPath, updateImageSourceRelative, UpdateImageManifestName)
	manifest, imagePath, err := ValidateUpdateImageStage(manifestPath, cfg.TestMode)
	if err != nil {
		return UpdateImageManifest{}, "", "", err
	}
	if err := matchUpdateImageRequest(manifestPath, manifest, request, cfg.TestMode); err != nil {
		return UpdateImageManifest{}, "", "", err
	}
	return manifest, imagePath, manifestPath, nil
}

func validateUpdateImageStageOnP7(cfg UpdateImageMigrationConfig, request UpdateImageBCBRequest) (UpdateImageManifest, string, error) {
	stageDir := filepath.Join(cfg.UserdataMountPath, updateImageStageRelative)
	manifestPath := filepath.Join(stageDir, UpdateImageManifestName)
	manifest, imagePath, err := ValidateUpdateImageStage(manifestPath, cfg.TestMode)
	if err != nil {
		return UpdateImageManifest{}, "", err
	}
	if err := matchUpdateImageRequest(manifestPath, manifest, request, cfg.TestMode); err != nil {
		return UpdateImageManifest{}, "", err
	}
	state, err := readUpdateImageStageState(cfg)
	if err != nil {
		return UpdateImageManifest{}, "", err
	}
	if state.Request != request {
		return UpdateImageManifest{}, "", errors.New("update.img p7 state request does not match BCB")
	}
	if state.Phase != "staged" && state.Phase != "stock-pending" {
		return UpdateImageManifest{}, "", errors.New("update.img p7 state is invalid")
	}
	return manifest, imagePath, nil
}

func matchUpdateImageRequest(manifestPath string, manifest UpdateImageManifest, request UpdateImageBCBRequest, testMode bool) error {
	if manifest.Schema != request.Schema || manifest.Size != request.PackageSize || manifest.SHA256 != request.PackageSHA256 {
		return errors.New("update.img manifest does not match BCB package identity")
	}
	manifestHash, err := hashUpdateImageManifest(manifestPath, testMode)
	if err != nil {
		return err
	}
	if manifestHash != request.ManifestSHA256 {
		return errors.New("update.img manifest sha256 does not match BCB")
	}
	return nil
}

func hashUpdateImageManifest(path string, testMode bool) (string, error) {
	info, err := secureRegularFile(path, testMode)
	if err != nil {
		return "", err
	}
	if info.Size() <= 0 || info.Size() > maxManifestBytes {
		return "", errors.New("invalid update.img manifest size")
	}
	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()
	hash := sha256.New()
	if _, err := io.Copy(hash, io.LimitReader(f, maxManifestBytes+1)); err != nil {
		return "", err
	}
	return hex.EncodeToString(hash.Sum(nil)), nil
}

func writeUpdateImageStageState(cfg UpdateImageMigrationConfig, request UpdateImageBCBRequest, phase string) error {
	if phase != "staged" && phase != "stock-pending" {
		return fmt.Errorf("invalid update.img stage state phase %s", phase)
	}
	return writeJSONAtomic(filepath.Join(cfg.UserdataMountPath, updateImageStageRelative, UpdateImageStateName), updateImageStageState{Schema: UpdateImageSchema, Phase: phase, Request: request})
}

func readUpdateImageStageState(cfg UpdateImageMigrationConfig) (updateImageStageState, error) {
	var state updateImageStageState
	if err := readJSONStrict(filepath.Join(cfg.UserdataMountPath, updateImageStageRelative, UpdateImageStateName), &state); err != nil {
		return updateImageStageState{}, fmt.Errorf("read update.img stage state: %w", err)
	}
	if state.Schema != UpdateImageSchema || state.Phase == "" || state.Request.Validate() != nil {
		return updateImageStageState{}, errors.New("update.img stage state is invalid")
	}
	return state, nil
}

func ensureUpdateImageFitsShortenedRootfs(cfg UpdateImageMigrationConfig, request UpdateImageBCBRequest) error {
	manifest, _, err := validateUpdateImageStageOnP7(cfg, request)
	if err != nil {
		return err
	}
	rootfsItem, err := manifest.Item("rootfs")
	if err != nil {
		return err
	}
	rootfs, err := ResolveRecoveryPartition(cfg.RootfsPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return err
	}
	if rootfsItem.Size > uint64(rootfs.Size) {
		return fmt.Errorf("update.img rootfs item size %d exceeds temporarily shortened p6 size %d", rootfsItem.Size, rootfs.Size)
	}
	return nil
}

func reportUpdateImageProgress(cfg UpdateImageMigrationConfig, phase, detail string) {
	if cfg.Progress != nil {
		cfg.Progress(phase, detail)
	}
}

var errUpdateImageMigrationTestStop = errors.New("update.img migration test stop")

func waitForRecoveryPartition(path, label, expectedParent string, testMode bool, timeout time.Duration) (Device, error) {
	deadline := time.Now().Add(timeout)
	var lastErr error
	for {
		device, err := ResolveRecoveryPartition(path, label, expectedParent, testMode)
		if err == nil {
			return device, nil
		}
		lastErr = err
		if time.Now().After(deadline) {
			return Device{}, fmt.Errorf("wait for validated %s partition: %w", label, lastErr)
		}
		time.Sleep(100 * time.Millisecond)
	}
}
