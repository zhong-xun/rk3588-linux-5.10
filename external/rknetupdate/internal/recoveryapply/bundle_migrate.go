package recoveryapply

import (
	"context"
	"crypto/subtle"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"syscall"
	"time"
)

const (
	ProductionBundleMigrationDiskPath      = "/dev/mmcblk0"
	ProductionBundleMigrationRootfsPath    = "/dev/disk/by-partlabel/rootfs"
	ProductionBundleMigrationBootPath      = "/dev/disk/by-partlabel/boot"
	ProductionBundleMigrationUbootPath     = "/dev/disk/by-partlabel/uboot"
	ProductionBundleMigrationMiscPath      = "/dev/disk/by-partlabel/misc"
	ProductionBundleMigrationUserdataPath  = "/dev/disk/by-partlabel/userdata"
	ProductionBundleMigrationMountInfoPath = "/proc/self/mountinfo"
	ProductionBundleMigrationSourceMount   = "/mnt/rknetupdate-bundle"
	ProductionBundleMigrationUserdataMount = "/userdata"
	ProductionBundleMigrationStatusPath    = "/run/rknetupdate-bundle-v1.status.json"
	ProductionBundleMigrationSGDiskPath    = "/usr/sbin/sgdisk"
	ProductionBundleMigrationPartprobePath = "/usr/sbin/partprobe"
	ProductionBundleMigrationE2fsckPath    = "/usr/sbin/e2fsck"
	ProductionBundleMigrationResize2fsPath = "/usr/sbin/resize2fs"
	ProductionBundleMigrationMkfsExt4Path  = "/usr/sbin/mkfs.ext4"
	bundleMigrationSourceRelative          = "var/lib/rknetupdate/bundle-v1"
	bundleMigrationStageRelative           = "rknetupdate-bundle-v1"
	bundleMigrationStateName               = "state.json"
)

type BundleMigrationConfig struct {
	DiskPath           string
	RootfsPath         string
	BootPath           string
	UbootPath          string
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
	ToolExtraEnv       []string
	TestMode           bool
	StopAfterPhase     string
	WriteProgress      WriteProgress
}

type BundleMigrationStatus struct {
	Schema            int               `json:"schema"`
	Phase             string            `json:"phase"`
	Success           bool              `json:"success"`
	BCBCleared        bool              `json:"bcb_cleared"`
	Request           BundleBCBRequest  `json:"request"`
	ManifestSHA256    string            `json:"manifest_sha256,omitempty"`
	P6LastLBA         uint64            `json:"p6_last_lba,omitempty"`
	P7Present         bool              `json:"p7_present"`
	ArtifactReadbacks map[string]string `json:"artifact_readbacks,omitempty"`
	FilesystemBlocks  uint64            `json:"filesystem_blocks,omitempty"`
	Error             string            `json:"error,omitempty"`
	UpdatedAt         time.Time         `json:"updated_at"`
}

type bundleMigrationState struct {
	Schema         int              `json:"schema"`
	Phase          string           `json:"phase"`
	Request        BundleBCBRequest `json:"request"`
	ManifestSHA256 string           `json:"manifest_sha256"`
	Completed      []string         `json:"completed"`
}

func DefaultBundleMigrationConfig() BundleMigrationConfig {
	return BundleMigrationConfig{
		DiskPath:           ProductionBundleMigrationDiskPath,
		RootfsPath:         ProductionBundleMigrationRootfsPath,
		BootPath:           ProductionBundleMigrationBootPath,
		UbootPath:          ProductionBundleMigrationUbootPath,
		MiscPath:           ProductionBundleMigrationMiscPath,
		UserdataPath:       ProductionBundleMigrationUserdataPath,
		ExpectedParentPath: ProductionExpectedParentPath,
		MountInfoPath:      ProductionBundleMigrationMountInfoPath,
		RecoveryMarkerPath: ProductionRecoveryMarkerPath,
		SourceMountPath:    ProductionBundleMigrationSourceMount,
		UserdataMountPath:  ProductionBundleMigrationUserdataMount,
		StatusPath:         ProductionBundleMigrationStatusPath,
		SGDiskPath:         ProductionBundleMigrationSGDiskPath,
		PartprobePath:      ProductionBundleMigrationPartprobePath,
		E2fsckPath:         ProductionBundleMigrationE2fsckPath,
		Resize2fsPath:      ProductionBundleMigrationResize2fsPath,
		MkfsExt4Path:       ProductionBundleMigrationMkfsExt4Path,
	}
}

func (c BundleMigrationConfig) Validate() error {
	paths := map[string]string{
		"disk": c.DiskPath, "rootfs": c.RootfsPath, "boot": c.BootPath, "uboot": c.UbootPath,
		"misc": c.MiscPath, "userdata": c.UserdataPath, "expected-parent": c.ExpectedParentPath,
		"mountinfo": c.MountInfoPath, "marker": c.RecoveryMarkerPath, "source-mount": c.SourceMountPath,
		"userdata-mount": c.UserdataMountPath, "status": c.StatusPath, "sgdisk": c.SGDiskPath,
		"partprobe": c.PartprobePath, "e2fsck": c.E2fsckPath, "resize2fs": c.Resize2fsPath, "mkfs.ext4": c.MkfsExt4Path,
	}
	for name, path := range paths {
		if path == "" || !filepath.IsAbs(path) {
			return fmt.Errorf("bundle migration %s path must be absolute", name)
		}
	}
	if c.StageMarginBytes < 0 {
		return errors.New("bundle migration stage margin must not be negative")
	}
	if c.StopAfterPhase != "" && !c.TestMode {
		return errors.New("bundle migration stop-after-phase is test-only")
	}
	if len(c.ToolExtraEnv) != 0 && !c.TestMode {
		return errors.New("bundle migration tool environment overrides are test-only")
	}
	if c.TestMode {
		return nil
	}
	expected := map[string]string{
		"disk": ProductionBundleMigrationDiskPath, "rootfs": ProductionBundleMigrationRootfsPath,
		"boot": ProductionBundleMigrationBootPath, "uboot": ProductionBundleMigrationUbootPath,
		"misc": ProductionBundleMigrationMiscPath, "userdata": ProductionBundleMigrationUserdataPath,
		"expected-parent": ProductionExpectedParentPath, "mountinfo": ProductionBundleMigrationMountInfoPath,
		"marker": ProductionRecoveryMarkerPath, "source-mount": ProductionBundleMigrationSourceMount,
		"userdata-mount": ProductionBundleMigrationUserdataMount, "status": ProductionBundleMigrationStatusPath,
		"sgdisk": ProductionBundleMigrationSGDiskPath, "partprobe": ProductionBundleMigrationPartprobePath,
		"e2fsck": ProductionBundleMigrationE2fsckPath, "resize2fs": ProductionBundleMigrationResize2fsPath,
		"mkfs.ext4": ProductionBundleMigrationMkfsExt4Path,
	}
	for name, want := range expected {
		if filepath.Clean(paths[name]) != want {
			return fmt.Errorf("production bundle migration %s path is fixed at %s", name, want)
		}
	}
	return nil
}

// RunBundleMigration resumes an authenticated offline bundle request.  The
// source is first copied from p6 to a temporary p7, so no operation after the
// rootfs write depends on the running root filesystem still existing.
func RunBundleMigration(ctx context.Context, cfg BundleMigrationConfig) (BundleMigrationStatus, error) {
	status := BundleMigrationStatus{Schema: BundleSchema, Phase: "starting", ArtifactReadbacks: map[string]string{}}
	fail := func(err error) (BundleMigrationStatus, error) {
		status.Success = false
		status.Error = err.Error()
		status.UpdatedAt = time.Now().UTC()
		_ = writeJSONAtomic(cfg.StatusPath, status)
		return status, err
	}
	if err := cfg.Validate(); err != nil {
		return fail(err)
	}
	if err := verifyRecoveryMarker(cfg.RecoveryMarkerPath, cfg.TestMode); err != nil {
		return fail(err)
	}
	misc, err := ResolveDevice(cfg.MiscPath, "misc", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return fail(fmt.Errorf("validate bundle misc: %w", err))
	}
	request, phase, err := ReadBundleBCB(misc)
	if err != nil {
		return fail(err)
	}
	status.Request = request
	status.ManifestSHA256 = request.ManifestSHA256
	status.Phase = phase
	mutator := GPTMutator{SGDiskPath: cfg.SGDiskPath, PartprobePath: cfg.PartprobePath, TestMode: cfg.TestMode, ExtraEnv: cfg.ToolExtraEnv}

	table, p6, p7, err := inspectBundleMigrationGPT(cfg, request)
	if err != nil {
		return fail(err)
	}
	status.P6LastLBA = p6.LastLBA
	status.P7Present = p7 != nil
	if p7 == nil && p6.LastLBA < table.LastUsable && phase != BundlePhaseShrunk && !bundlePhaseAtLeast(phase, BundlePhaseRootfsVerified) {
		return fail(errors.New("shortened p6 without p7 is only valid after bundle rootfs verification"))
	}

	if p7 == nil && phase == BundlePhaseShrunk {
		if err := createBundleP7AfterShrink(ctx, cfg, mutator, misc, request, table, p6); err != nil {
			return fail(err)
		}
		misc, err = ResolveDevice(cfg.MiscPath, "misc", cfg.ExpectedParentPath, cfg.TestMode)
		if err != nil {
			return fail(fmt.Errorf("refresh bundle misc after resumed GPT change: %w", err))
		}
		phase = BundlePhaseP7Ready
		table, p6, p7, err = inspectBundleMigrationGPT(cfg, request)
		if err != nil {
			return fail(err)
		}
		if bundleStopAt(cfg, BundlePhaseP7Ready) {
			status.Phase = BundlePhaseP7Ready
			status.UpdatedAt = time.Now().UTC()
			return status, nil
		}
	} else if p7 == nil && !bundlePhaseAtLeast(phase, BundlePhaseRootfsVerified) {
		if err := prepareTemporaryBundleStage(ctx, cfg, mutator, misc, request, table, p6); err != nil {
			if errors.Is(err, errMigrationTestStop) {
				status.Phase = BundlePhaseShrunk
				status.UpdatedAt = time.Now().UTC()
				return status, nil
			}
			return fail(err)
		}
		misc, err = ResolveDevice(cfg.MiscPath, "misc", cfg.ExpectedParentPath, cfg.TestMode)
		if err != nil {
			return fail(fmt.Errorf("refresh bundle misc after GPT change: %w", err))
		}
		if bundleStopAt(cfg, BundlePhaseP7Ready) {
			status.Phase = BundlePhaseP7Ready
			status.UpdatedAt = time.Now().UTC()
			return status, nil
		}
		table, p6, p7, err = inspectBundleMigrationGPT(cfg, request)
		if err != nil {
			return fail(err)
		}
	}

	if p7 != nil && !bundlePhaseAtLeast(phase, BundlePhaseRootfsVerified) {
		if err := ensureBundleStagedSource(ctx, cfg, misc, request, phase); err != nil {
			return fail(err)
		}
		_, phase, err = ReadBundleBCB(misc)
		if err != nil {
			return fail(err)
		}
		if bundleStopAt(cfg, BundlePhaseStaged) {
			status.Phase = BundlePhaseStaged
			status.UpdatedAt = time.Now().UTC()
			return status, nil
		}
	}

	rootfsArtifact := bundleRootfsArtifactFromRequest(request)
	resumeAfterStageRemoved := p7 == nil && bundlePhaseAtLeast(phase, BundlePhasePayloadVerified) && p6.LastLBA == table.LastUsable
	if !resumeAfterStageRemoved {
		if p7 == nil {
			return fail(errors.New("bundle temporary p7 staging partition is missing"))
		}
		// On a reboot p7 is not mounted automatically.  Later phases still need
		// its hash-bound source and persistent state, so remount it before any
		// validation or resume decision.
		if bundlePhaseAtLeast(phase, BundlePhaseRootfsVerified) {
			userdata, err := ResolveDevice(cfg.UserdataPath, "userdata", cfg.ExpectedParentPath, cfg.TestMode)
			if err != nil {
				return fail(err)
			}
			if err := ensureMounted(userdata, cfg.UserdataMountPath, false, cfg.MountInfoPath); err != nil {
				return fail(fmt.Errorf("mount persisted bundle p7: %w", err))
			}
		}
		manifest, state, err := validateBundleStagedSource(ctx, cfg, request)
		if err != nil {
			return fail(err)
		}
		artifact, err := manifest.Artifact("rootfs")
		if err != nil {
			return fail(err)
		}
		if !bundleRootfsArtifactMatchesRequest(artifact, request) {
			return fail(errors.New("bundle rootfs manifest metadata does not match BCB request"))
		}
		rootfsArtifact = artifact
		if bundlePhaseAtLeast(phase, BundlePhaseRootfsVerified) && !bundleStateContains(state, "rootfs") {
			return fail(errors.New("bundle BCB says rootfs verified but persistent stage state does not"))
		}

		if !bundlePhaseAtLeast(phase, BundlePhaseRootfsVerified) {
			readback, err := writeBundleRootfsFromStage(ctx, cfg, misc, request, manifest)
			if err != nil {
				return fail(err)
			}
			status.ArtifactReadbacks["rootfs"] = readback
			phase = BundlePhaseRootfsVerified
			if bundleStopAt(cfg, BundlePhaseRootfsVerified) {
				status.Phase = phase
				status.UpdatedAt = time.Now().UTC()
				return status, nil
			}
		}

		if !bundlePhaseAtLeast(phase, BundlePhasePayloadVerified) {
			readbacks, err := writeBundlePayloadsFromStage(ctx, cfg, misc, request, manifest)
			if err != nil {
				return fail(err)
			}
			for target, readback := range readbacks {
				status.ArtifactReadbacks[target] = readback
			}
			phase = BundlePhasePayloadVerified
			if bundleStopAt(cfg, BundlePhasePayloadVerified) {
				status.Phase = phase
				status.UpdatedAt = time.Now().UTC()
				return status, nil
			}
		} else if !bundleStateComplete(manifest, state) {
			return fail(errors.New("bundle BCB says payload verified but persistent stage state is incomplete"))
		}
	}

	table, p6, p7, err = inspectBundleMigrationGPT(cfg, request)
	if err != nil {
		return fail(err)
	}
	if p7 != nil {
		userdata, err := ResolveDevice(cfg.UserdataPath, "userdata", cfg.ExpectedParentPath, cfg.TestMode)
		if err != nil {
			return fail(err)
		}
		if err := ensureUnmounted(userdata, cfg.UserdataMountPath, cfg.MountInfoPath); err != nil {
			return fail(err)
		}
		table, p6, p7, err = inspectBundleMigrationGPT(cfg, request)
		if err != nil {
			return fail(err)
		}
	}
	if p7 != nil || p6.LastLBA != table.LastUsable {
		if err := verifyBundleTargetFilesystem(cfg, rootfsArtifact, false); err != nil {
			return fail(fmt.Errorf("verify rootfs before deleting p7: %w", err))
		}
		if err := mutator.RestoreRootfs(ctx, cfg.DiskPath, bundleP6Original(request, p6), table.LastUsable, p7 != nil); err != nil {
			return fail(err)
		}
		if err := waitForPath(cfg.RootfsPath, true, 10*time.Second); err != nil {
			return fail(err)
		}
		table, p6, p7, err = inspectBundleMigrationGPT(cfg, request)
		if err != nil {
			return fail(err)
		}
		if p7 != nil || p6.LastLBA != table.LastUsable {
			return fail(errors.New("bundle GPT restore did not remove p7 and extend p6"))
		}
	}
	if !bundlePhaseAtLeast(phase, BundlePhaseGPTRestored) {
		misc, err = ResolveDevice(cfg.MiscPath, "misc", cfg.ExpectedParentPath, cfg.TestMode)
		if err != nil {
			return fail(fmt.Errorf("refresh bundle misc after GPT restore: %w", err))
		}
		if err := UpdateBundleBCBPhase(misc, request, BundlePhaseGPTRestored); err != nil {
			return fail(err)
		}
		phase = BundlePhaseGPTRestored
	}
	if bundleStopAt(cfg, BundlePhaseGPTRestored) {
		status.Phase = phase
		status.UpdatedAt = time.Now().UTC()
		return status, nil
	}
	if err := expandBundleRootfs(ctx, cfg, rootfsArtifact, table, p6); err != nil {
		return fail(err)
	}
	if err := UpdateBundleBCBPhase(misc, request, BundlePhaseExpanded); err != nil {
		return fail(err)
	}
	status.Phase = BundlePhaseExpanded
	if bundleStopAt(cfg, BundlePhaseExpanded) {
		status.UpdatedAt = time.Now().UTC()
		return status, nil
	}
	if err := ClearBundleBCB(misc, request); err != nil {
		return fail(err)
	}
	status.Success = true
	status.BCBCleared = true
	status.Error = ""
	status.Phase = "succeeded"
	status.UpdatedAt = time.Now().UTC()
	_ = writeJSONAtomic(cfg.StatusPath, status)
	return status, nil
}

// A power loss after the filesystem has shrunk but before GPT p7 creation is
// recoverable: GPT still gives p6 the whole tail while ext4 is already small.
// Recreate the p7 split from the same manifest without shrinking ext4 again.
func createBundleP7AfterShrink(ctx context.Context, cfg BundleMigrationConfig, mutator GPTMutator, misc Device, request BundleBCBRequest, table GPTTable, p6 GPTPartition) error {
	if p6.LastLBA != table.LastUsable {
		return errors.New("bundle shrink phase requires p6 GPT to still reach the last usable LBA")
	}
	manifest, _, err := validateBundleSourceOnRootfs(ctx, cfg, request)
	if err != nil {
		return err
	}
	total, err := manifest.TotalSize()
	if err != nil {
		return err
	}
	requiredSectors := (uint64(requiredBundleStageBytes(total, cfg.StageMarginBytes)) + table.SectorSize - 1) / table.SectorSize
	alignment := uint64((1 << 20) / table.SectorSize)
	if alignment == 0 {
		alignment = 1
	}
	if requiredSectors >= table.LastUsable-p6.FirstLBA {
		return errors.New("insufficient disk space for resumed temporary bundle p7")
	}
	p7Start := ((table.LastUsable + 1 - requiredSectors) / alignment) * alignment
	if p7Start <= p6.FirstLBA {
		return errors.New("insufficient aligned disk space for resumed temporary bundle p7")
	}
	p6End := p7Start - 1
	rootfs, err := ResolveDevice(cfg.RootfsPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return err
	}
	if err := ensureUnmounted(rootfs, cfg.SourceMountPath, cfg.MountInfoPath); err != nil {
		return err
	}
	f, err := openDevice(rootfs, os.O_RDONLY)
	if err != nil {
		return err
	}
	currentFS, inspectErr := InspectExt4Device(f, rootfs.Size)
	_ = f.Close()
	if inspectErr != nil {
		return inspectErr
	}
	p6Bytes := (p6End - p6.FirstLBA + 1) * table.SectorSize
	if currentFS.FilesystemSize > p6Bytes {
		return errors.New("shrunk bundle rootfs no longer fits the resumed p6 GPT boundary")
	}
	if err := mutator.CreateStage(ctx, cfg.DiskPath, p6, p6End, p7Start, table.LastUsable); err != nil {
		return err
	}
	if err := waitForPath(cfg.RootfsPath, true, 10*time.Second); err != nil {
		return err
	}
	if err := waitForPath(cfg.UserdataPath, true, 10*time.Second); err != nil {
		return err
	}
	_, newP6, newP7, err := inspectBundleMigrationGPT(cfg, request)
	if err != nil {
		return err
	}
	if newP7 == nil || newP6.LastLBA != p6End {
		return errors.New("resumed bundle p7 GPT creation verification failed")
	}
	misc, err = ResolveDevice(cfg.MiscPath, "misc", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return err
	}
	return UpdateBundleBCBPhase(misc, request, BundlePhaseP7Ready)
}

func inspectBundleMigrationGPT(cfg BundleMigrationConfig, request BundleBCBRequest) (GPTTable, GPTPartition, *GPTPartition, error) {
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
		return GPTTable{}, GPTPartition{}, nil, errors.New("p6 GPT identity does not match bundle BCB request")
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

func prepareTemporaryBundleStage(ctx context.Context, cfg BundleMigrationConfig, mutator GPTMutator, misc Device, request BundleBCBRequest, table GPTTable, p6 GPTPartition) error {
	if p6.LastLBA != table.LastUsable {
		return errors.New("initial bundle p6 must reach GPT last usable LBA")
	}
	manifest, _, err := validateBundleSourceOnRootfs(ctx, cfg, request)
	if err != nil {
		return err
	}
	rootfs, err := ResolveDevice(cfg.RootfsPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return err
	}
	if err := ensureUnmounted(rootfs, cfg.SourceMountPath, cfg.MountInfoPath); err != nil {
		return err
	}
	if err := runE2fsckRepair(ctx, cfg.E2fsckPath, rootfs.Path); err != nil {
		return err
	}
	f, err := openDevice(rootfs, os.O_RDONLY)
	if err != nil {
		return fmt.Errorf("inspect rootfs before bundle shrink: %w", err)
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
	total, err := manifest.TotalSize()
	if err != nil {
		return err
	}
	requiredBytes := requiredBundleStageBytes(total, cfg.StageMarginBytes)
	requiredSectors := (uint64(requiredBytes) + table.SectorSize - 1) / table.SectorSize
	alignment := uint64((1 << 20) / table.SectorSize)
	if alignment == 0 {
		alignment = 1
	}
	if requiredSectors >= table.LastUsable-p6.FirstLBA {
		return errors.New("insufficient disk space for temporary bundle p7")
	}
	p7Start := ((table.LastUsable + 1 - requiredSectors) / alignment) * alignment
	if p7Start <= p6.FirstLBA {
		return errors.New("insufficient aligned disk space for temporary bundle p7")
	}
	p6End := p7Start - 1
	p6Bytes := (p6End - p6.FirstLBA + 1) * table.SectorSize
	targetBlocks := p6Bytes / currentFS.BlockSize
	minSafeBlocks, maxSafeBlocks, err := shrinkVerificationRange(minimumBlocks, targetBlocks)
	if err != nil {
		return fmt.Errorf("insufficient bundle shrink headroom: %w", err)
	}
	if err := runResize2fs(ctx, cfg.Resize2fsPath, rootfs.Path, fmt.Sprintf("%d", targetBlocks)); err != nil {
		return err
	}
	if err := verifyFilesystemGeometry(rootfs, currentFS.UUID, currentFS.BlockSize, minSafeBlocks, maxSafeBlocks); err != nil {
		return fmt.Errorf("verify shrunk bundle rootfs: %w", err)
	}
	if err := UpdateBundleBCBPhase(misc, request, BundlePhaseShrunk); err != nil {
		return err
	}
	if bundleStopAt(cfg, BundlePhaseShrunk) {
		return errMigrationTestStop
	}
	if err := mutator.CreateStage(ctx, cfg.DiskPath, p6, p6End, p7Start, table.LastUsable); err != nil {
		return err
	}
	if err := waitForPath(cfg.RootfsPath, true, 10*time.Second); err != nil {
		return err
	}
	if err := waitForPath(cfg.UserdataPath, true, 10*time.Second); err != nil {
		return err
	}
	newTable, newP6, newP7, err := inspectBundleMigrationGPT(cfg, request)
	if err != nil {
		return err
	}
	if newP7 == nil || newP6.LastLBA != p6End || newTable.LastUsable != table.LastUsable {
		return errors.New("temporary bundle p7 GPT creation verification failed")
	}
	misc, err = ResolveDevice(cfg.MiscPath, "misc", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return fmt.Errorf("refresh bundle misc after creating p7: %w", err)
	}
	return UpdateBundleBCBPhase(misc, request, BundlePhaseP7Ready)
}

func ensureBundleStagedSource(ctx context.Context, cfg BundleMigrationConfig, misc Device, request BundleBCBRequest, phase string) error {
	if err := waitForPath(cfg.UserdataPath, true, 10*time.Second); err != nil {
		return err
	}
	userdata, err := ResolveDevice(cfg.UserdataPath, "userdata", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return err
	}
	if err := ensureMounted(userdata, cfg.UserdataMountPath, false, cfg.MountInfoPath); err != nil {
		if bundlePhaseAtLeast(phase, BundlePhaseStaged) {
			return fmt.Errorf("mount existing bundle p7: %w", err)
		}
		if err := ensureUnmounted(userdata, cfg.UserdataMountPath, cfg.MountInfoPath); err != nil {
			return err
		}
		if err := runCommandChecked(ctx, cfg.MkfsExt4Path, "-F", "-L", "userdata", userdata.Path); err != nil {
			return err
		}
		if err := ensureMounted(userdata, cfg.UserdataMountPath, false, cfg.MountInfoPath); err != nil {
			return err
		}
	}
	if _, _, err := validateBundleStagedSource(ctx, cfg, request); err == nil {
		if !bundlePhaseAtLeast(phase, BundlePhaseStaged) {
			return UpdateBundleBCBPhase(misc, request, BundlePhaseStaged)
		}
		return nil
	} else if bundlePhaseAtLeast(phase, BundlePhaseStaged) {
		return fmt.Errorf("persisted bundle stage is invalid: %w", err)
	}
	if err := ensureUnmounted(userdata, cfg.UserdataMountPath, cfg.MountInfoPath); err != nil {
		return err
	}
	if err := runCommandChecked(ctx, cfg.MkfsExt4Path, "-F", "-L", "userdata", userdata.Path); err != nil {
		return err
	}
	if err := ensureMounted(userdata, cfg.UserdataMountPath, false, cfg.MountInfoPath); err != nil {
		return err
	}
	manifest, sourceDir, err := validateBundleSourceOnRootfs(ctx, cfg, request)
	if err != nil {
		return err
	}
	stageDir := filepath.Join(cfg.UserdataMountPath, bundleMigrationStageRelative)
	if err := os.MkdirAll(stageDir, 0700); err != nil {
		return err
	}
	for _, artifact := range manifest.Artifacts {
		source, err := BundleArtifactPath(sourceDir, artifact)
		if err != nil {
			return err
		}
		if err := copyFileSync(source, filepath.Join(stageDir, artifact.Image), 0600); err != nil {
			return fmt.Errorf("copy bundle %s to p7: %w", artifact.Target, err)
		}
	}
	if err := copyFileSync(filepath.Join(sourceDir, BundleManifestName), filepath.Join(stageDir, BundleManifestName), 0600); err != nil {
		return fmt.Errorf("copy bundle manifest to p7: %w", err)
	}
	if err := writeBundleStageState(cfg, request, []string{}); err != nil {
		return err
	}
	if err := syncFilesystem(cfg.UserdataMountPath); err != nil {
		return err
	}
	if _, _, err := validateBundleStagedSource(ctx, cfg, request); err != nil {
		return err
	}
	rootfs, err := ResolveDevice(cfg.RootfsPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return err
	}
	if err := ensureUnmounted(rootfs, cfg.SourceMountPath, cfg.MountInfoPath); err != nil {
		return err
	}
	return UpdateBundleBCBPhase(misc, request, BundlePhaseStaged)
}

func writeBundleRootfsFromStage(ctx context.Context, cfg BundleMigrationConfig, misc Device, request BundleBCBRequest, manifest BundleManifest) (string, error) {
	rootfsArtifact, err := manifest.Artifact("rootfs")
	if err != nil {
		return "", err
	}
	rootfs, err := ResolveDevice(cfg.RootfsPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return "", err
	}
	if err := ensureUnmounted(rootfs, cfg.SourceMountPath, cfg.MountInfoPath); err != nil {
		return "", err
	}
	if rootfsArtifact.Size > rootfs.Size {
		return "", errors.New("staged bundle rootfs image exceeds shortened p6")
	}
	if current, err := hashDevicePrefix(rootfs, rootfsArtifact.Size); err == nil && subtle.ConstantTimeCompare([]byte(current), []byte(rootfsArtifact.SHA256)) == 1 {
		if err := verifyBundleTargetFilesystem(cfg, rootfsArtifact, true); err != nil {
			return current, err
		}
		if err := persistBundleArtifact(cfg, request, "rootfs"); err != nil {
			return current, err
		}
		if err := UpdateBundleBCBPhase(misc, request, BundlePhaseRootfsVerified); err != nil {
			return current, err
		}
		return current, nil
	}
	sourcePath, err := BundleArtifactPath(filepath.Join(cfg.UserdataMountPath, bundleMigrationStageRelative), rootfsArtifact)
	if err != nil {
		return "", err
	}
	source, err := os.Open(sourcePath)
	if err != nil {
		return "", err
	}
	defer source.Close()
	written, readback, err := writeImage(ctx, source, rootfs, rootfsArtifact.Size, rootfsArtifact.SHA256, "rootfs-writing", cfg.WriteProgress)
	if err != nil {
		return readback, fmt.Errorf("bundle rootfs raw write stopped after %d bytes: %w", written, err)
	}
	if err := verifyBundleTargetFilesystem(cfg, rootfsArtifact, true); err != nil {
		return readback, err
	}
	if err := persistBundleArtifact(cfg, request, "rootfs"); err != nil {
		return readback, err
	}
	if err := UpdateBundleBCBPhase(misc, request, BundlePhaseRootfsVerified); err != nil {
		return readback, err
	}
	return readback, nil
}

func writeBundlePayloadsFromStage(ctx context.Context, cfg BundleMigrationConfig, misc Device, request BundleBCBRequest, manifest BundleManifest) (map[string]string, error) {
	readbacks := make(map[string]string)
	_, state, err := validateBundleStagedSource(ctx, cfg, request)
	if err != nil {
		return nil, err
	}
	if !bundleStateContains(state, "rootfs") {
		return nil, errors.New("bundle payload write requires persisted rootfs verification")
	}
	for _, artifact := range manifest.Artifacts {
		if artifact.Target == "rootfs" {
			continue
		}
		target, err := resolveBundleArtifactTarget(cfg, artifact.Target)
		if err != nil {
			return nil, err
		}
		if artifact.Size > target.Size {
			return nil, fmt.Errorf("bundle %s image exceeds target capacity", artifact.Target)
		}
		if err := ensureDeviceUnmounted(target, cfg.MountInfoPath); err != nil {
			return nil, err
		}
		if current, err := hashDevicePrefix(target, artifact.Size); err == nil && subtle.ConstantTimeCompare([]byte(current), []byte(artifact.SHA256)) == 1 {
			readbacks[artifact.Target] = current
			if err := persistBundleArtifact(cfg, request, artifact.Target); err != nil {
				return nil, err
			}
			continue
		}
		sourcePath, err := BundleArtifactPath(filepath.Join(cfg.UserdataMountPath, bundleMigrationStageRelative), artifact)
		if err != nil {
			return nil, err
		}
		source, err := os.Open(sourcePath)
		if err != nil {
			return nil, err
		}
		written, readback, writeErr := writeImage(ctx, source, target, artifact.Size, artifact.SHA256, string(artifact.Target)+"-writing", cfg.WriteProgress)
		closeErr := source.Close()
		if writeErr != nil {
			return nil, fmt.Errorf("bundle %s raw write stopped after %d bytes: %w", artifact.Target, written, writeErr)
		}
		if closeErr != nil {
			return nil, fmt.Errorf("close bundle %s source: %w", artifact.Target, closeErr)
		}
		readbacks[artifact.Target] = readback
		if err := persistBundleArtifact(cfg, request, artifact.Target); err != nil {
			return nil, err
		}
	}
	if _, state, err = validateBundleStagedSource(ctx, cfg, request); err != nil {
		return nil, err
	}
	if !bundleStateComplete(manifest, state) {
		return nil, errors.New("bundle artifact completion record is incomplete")
	}
	if err := UpdateBundleBCBPhase(misc, request, BundlePhasePayloadVerified); err != nil {
		return nil, err
	}
	return readbacks, nil
}

func validateBundleSourceOnRootfs(ctx context.Context, cfg BundleMigrationConfig, request BundleBCBRequest) (BundleManifest, string, error) {
	rootfs, err := ResolveDevice(cfg.RootfsPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return BundleManifest{}, "", err
	}
	if err := ensureMounted(rootfs, cfg.SourceMountPath, true, cfg.MountInfoPath); err != nil {
		return BundleManifest{}, "", err
	}
	sourceDir := filepath.Join(cfg.SourceMountPath, bundleMigrationSourceRelative)
	manifest, err := validateBundleDirectory(ctx, sourceDir, cfg.ExpectedParentPath, cfg.E2fsckPath, cfg.TestMode, request.ManifestSHA256)
	if err != nil {
		return BundleManifest{}, "", err
	}
	return manifest, sourceDir, nil
}

func validateBundleStagedSource(ctx context.Context, cfg BundleMigrationConfig, request BundleBCBRequest) (BundleManifest, bundleMigrationState, error) {
	stageDir := filepath.Join(cfg.UserdataMountPath, bundleMigrationStageRelative)
	manifest, err := validateBundleDirectory(ctx, stageDir, cfg.ExpectedParentPath, cfg.E2fsckPath, cfg.TestMode, request.ManifestSHA256)
	if err != nil {
		return BundleManifest{}, bundleMigrationState{}, err
	}
	var state bundleMigrationState
	if err := readJSONStrict(filepath.Join(stageDir, bundleMigrationStateName), &state); err != nil {
		return BundleManifest{}, bundleMigrationState{}, fmt.Errorf("read staged bundle state: %w", err)
	}
	if state.Schema != BundleSchema || state.Request != request || state.ManifestSHA256 != request.ManifestSHA256 || !bundleStatePhaseValid(state.Phase) {
		return BundleManifest{}, bundleMigrationState{}, errors.New("staged bundle state is invalid")
	}
	if !bundleStateCompletionValid(manifest, state) {
		return BundleManifest{}, bundleMigrationState{}, errors.New("staged bundle completion state is invalid")
	}
	return manifest, state, nil
}

func validateBundleDirectory(ctx context.Context, stageDir, expectedParent, e2fsckPath string, testMode bool, expectedManifestSHA string) (BundleManifest, error) {
	manifestPath := filepath.Join(stageDir, BundleManifestName)
	manifest, err := LoadBundleManifest(manifestPath, testMode)
	if err != nil {
		return BundleManifest{}, err
	}
	if filepath.Clean(manifest.ExpectedParent) != filepath.Clean(expectedParent) {
		return BundleManifest{}, fmt.Errorf("bundle manifest expected_parent %s does not match configured parent %s", manifest.ExpectedParent, expectedParent)
	}
	hash, _, err := HashBundleManifest(manifestPath, testMode)
	if err != nil {
		return BundleManifest{}, err
	}
	if subtle.ConstantTimeCompare([]byte(hash), []byte(expectedManifestSHA)) != 1 {
		return BundleManifest{}, errors.New("bundle manifest sha256 does not match BCB request")
	}
	for _, artifact := range manifest.Artifacts {
		path, err := BundleArtifactPath(stageDir, artifact)
		if err != nil {
			return BundleManifest{}, err
		}
		if err := validateBundleArtifactFile(ctx, path, artifact, e2fsckPath, testMode); err != nil {
			return BundleManifest{}, err
		}
	}
	return manifest, nil
}

func validateBundleArtifactFile(ctx context.Context, path string, artifact BundleArtifact, e2fsckPath string, testMode bool) error {
	info, err := secureRegularFile(path, testMode)
	if err != nil {
		return fmt.Errorf("bundle artifact %s: %w", artifact.Target, err)
	}
	if info.Size() != artifact.Size {
		return fmt.Errorf("bundle artifact %s size %d does not match manifest %d", artifact.Target, info.Size(), artifact.Size)
	}
	f, err := os.Open(path)
	if err != nil {
		return fmt.Errorf("open bundle artifact %s: %w", artifact.Target, err)
	}
	defer f.Close()
	if err := syscall.Flock(int(f.Fd()), syscall.LOCK_SH|syscall.LOCK_NB); err != nil {
		return fmt.Errorf("lock bundle artifact %s: %w", artifact.Target, err)
	}
	defer syscall.Flock(int(f.Fd()), syscall.LOCK_UN) //nolint:errcheck
	if artifact.Target == "rootfs" {
		ext4, err := InspectExt4(f, artifact.Size)
		if err != nil {
			return fmt.Errorf("inspect bundle rootfs ext4: %w", err)
		}
		if ext4.UUID != artifact.FilesystemUUID || ext4.BlockSize != artifact.BlockSize || ext4.BlockCount != artifact.BlockCount {
			return errors.New("bundle rootfs ext4 geometry does not match manifest")
		}
		if _, err := runE2fsck(ctx, e2fsckPath, path); err != nil {
			return fmt.Errorf("validate bundle rootfs e2fsck: %w", err)
		}
	}
	if current, err := f.Stat(); err != nil || !os.SameFile(info, current) || current.Size() != artifact.Size {
		return fmt.Errorf("bundle artifact %s identity or size changed during validation", artifact.Target)
	}
	hash, err := hashOpenFile(f, artifact.Size)
	if err != nil {
		return err
	}
	if subtle.ConstantTimeCompare([]byte(hash), []byte(artifact.SHA256)) != 1 {
		return fmt.Errorf("bundle artifact %s sha256 does not match manifest", artifact.Target)
	}
	return nil
}

func persistBundleArtifact(cfg BundleMigrationConfig, request BundleBCBRequest, target string) error {
	_, state, err := validateBundleStagedSource(context.Background(), cfg, request)
	if err != nil {
		return err
	}
	if bundleStateContains(state, target) {
		return nil
	}
	state.Completed = append(state.Completed, target)
	sort.Strings(state.Completed)
	if target == "rootfs" {
		state.Phase = BundlePhaseRootfsVerified
	} else {
		state.Phase = BundlePhasePayloadVerified
	}
	return writeBundleStageStateValue(cfg, state)
}

func writeBundleStageState(cfg BundleMigrationConfig, request BundleBCBRequest, completed []string) error {
	state := bundleMigrationState{
		Schema:         BundleSchema,
		Phase:          BundlePhaseStaged,
		Request:        request,
		ManifestSHA256: request.ManifestSHA256,
		Completed:      completed,
	}
	return writeBundleStageStateValue(cfg, state)
}

func writeBundleStageStateValue(cfg BundleMigrationConfig, state bundleMigrationState) error {
	path := filepath.Join(cfg.UserdataMountPath, bundleMigrationStageRelative, bundleMigrationStateName)
	if err := writeJSONAtomic(path, state); err != nil {
		return err
	}
	return syncFilesystem(cfg.UserdataMountPath)
}

func bundleStatePhaseValid(phase string) bool {
	return phase == BundlePhaseStaged || phase == BundlePhaseRootfsVerified || phase == BundlePhasePayloadVerified
}

func bundleStateCompletionValid(manifest BundleManifest, state bundleMigrationState) bool {
	seen := make(map[string]bool, len(state.Completed))
	for _, target := range state.Completed {
		if seen[target] || bundleArtifactOrder(target) < 0 {
			return false
		}
		if _, err := manifest.Artifact(target); err != nil {
			return false
		}
		seen[target] = true
	}
	if state.Phase == BundlePhaseStaged {
		return len(state.Completed) == 0
	}
	if state.Phase == BundlePhaseRootfsVerified {
		return len(state.Completed) == 1 && seen["rootfs"]
	}
	return seen["rootfs"]
}

func bundleStateContains(state bundleMigrationState, target string) bool {
	for _, item := range state.Completed {
		if item == target {
			return true
		}
	}
	return false
}

func bundleStateComplete(manifest BundleManifest, state bundleMigrationState) bool {
	if !bundleStateCompletionValid(manifest, state) {
		return false
	}
	for _, artifact := range manifest.Artifacts {
		if !bundleStateContains(state, artifact.Target) {
			return false
		}
	}
	return true
}

func resolveBundleArtifactTarget(cfg BundleMigrationConfig, target string) (Device, error) {
	switch target {
	case "rootfs":
		return ResolveDevice(cfg.RootfsPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode)
	case "boot":
		return ResolveDevice(cfg.BootPath, "boot", cfg.ExpectedParentPath, cfg.TestMode)
	case "uboot":
		return ResolveDevice(cfg.UbootPath, "uboot", cfg.ExpectedParentPath, cfg.TestMode)
	default:
		return Device{}, fmt.Errorf("unsupported bundle target %q", target)
	}
}

func ensureDeviceUnmounted(device Device, mountInfoPath string) error {
	mounts, err := ReadMountInfo(mountInfoPath)
	if err != nil {
		return err
	}
	if mounted, mountPoint := IsDeviceMounted(device, mounts); mounted {
		return fmt.Errorf("bundle target %s is mounted at %s", device.PartName, mountPoint)
	}
	return nil
}

func verifyBundleTargetFilesystem(cfg BundleMigrationConfig, artifact BundleArtifact, requireExactBlocks bool) error {
	rootfs, err := ResolveDevice(cfg.RootfsPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return err
	}
	f, err := openDevice(rootfs, os.O_RDONLY)
	if err != nil {
		return fmt.Errorf("inspect bundle rootfs target: %w", err)
	}
	ext4, err := InspectExt4Device(f, rootfs.Size)
	_ = f.Close()
	if err != nil {
		return err
	}
	if ext4.UUID != artifact.FilesystemUUID || ext4.BlockSize != artifact.BlockSize || ext4.BlockCount < artifact.BlockCount {
		return errors.New("bundle target ext4 identity does not match requested image")
	}
	if requireExactBlocks && ext4.BlockCount != artifact.BlockCount {
		return errors.New("bundle target ext4 block count does not match raw image")
	}
	return nil
}

func expandBundleRootfs(ctx context.Context, cfg BundleMigrationConfig, artifact BundleArtifact, table GPTTable, p6 GPTPartition) error {
	if p6.LastLBA != table.LastUsable {
		return errors.New("cannot expand bundle rootfs before GPT reaches last usable LBA")
	}
	if err := verifyBundleTargetFilesystem(cfg, artifact, false); err != nil {
		return err
	}
	rootfs, err := ResolveDevice(cfg.RootfsPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return err
	}
	if err := runE2fsckRepair(ctx, cfg.E2fsckPath, rootfs.Path); err != nil {
		return err
	}
	if err := runResize2fs(ctx, cfg.Resize2fsPath, rootfs.Path); err != nil {
		return err
	}
	if err := runE2fsckRepair(ctx, cfg.E2fsckPath, rootfs.Path); err != nil {
		return err
	}
	f, err := openDevice(rootfs, os.O_RDONLY)
	if err != nil {
		return fmt.Errorf("inspect expanded bundle rootfs: %w", err)
	}
	ext4, err := InspectExt4Device(f, rootfs.Size)
	_ = f.Close()
	if err != nil {
		return err
	}
	wantBlocks := uint64(rootfs.Size) / artifact.BlockSize
	if ext4.UUID != artifact.FilesystemUUID || ext4.BlockSize != artifact.BlockSize || ext4.BlockCount != wantBlocks {
		return fmt.Errorf("expanded bundle rootfs geometry mismatch: uuid=%s block_size=%d blocks=%d want_blocks=%d", ext4.UUID, ext4.BlockSize, ext4.BlockCount, wantBlocks)
	}
	return nil
}

func bundleP6Original(request BundleBCBRequest, current GPTPartition) GPTPartition {
	return GPTPartition{Number: 6, TypeGUID: request.P6TypeGUID, UniqueGUID: request.P6UniqueGUID, FirstLBA: request.P6Start, LastLBA: current.LastLBA, Name: request.P6Name}
}

func bundleRootfsArtifactFromRequest(request BundleBCBRequest) BundleArtifact {
	return BundleArtifact{
		Target:         "rootfs",
		Image:          BundleRootfsImage,
		Filesystem:     "ext4",
		FilesystemUUID: request.FilesystemUUID,
		BlockSize:      request.BlockSize,
		BlockCount:     request.BlockCount,
	}
}

func bundleRootfsArtifactMatchesRequest(artifact BundleArtifact, request BundleBCBRequest) bool {
	return artifact.Target == "rootfs" && artifact.Filesystem == "ext4" &&
		artifact.FilesystemUUID == request.FilesystemUUID && artifact.BlockSize == request.BlockSize && artifact.BlockCount == request.BlockCount
}

func requiredBundleStageBytes(totalArtifacts, override int64) int64 {
	// Keep the same minimum margin/alignment as rootfs-v2.  The manifest and
	// state are tiny compared with the mandatory 1 MiB alignment margin.
	return requiredStageBytes(totalArtifacts+(64<<10), override)
}

func bundlePhaseAtLeast(actual, expected string) bool {
	actualIndex, expectedIndex := -1, -1
	for i, phase := range bundlePhaseOrder {
		if phase == actual {
			actualIndex = i
		}
		if phase == expected {
			expectedIndex = i
		}
	}
	return actualIndex >= expectedIndex && expectedIndex >= 0
}

func bundleStopAt(cfg BundleMigrationConfig, phase string) bool {
	return cfg.TestMode && cfg.StopAfterPhase == phase
}
