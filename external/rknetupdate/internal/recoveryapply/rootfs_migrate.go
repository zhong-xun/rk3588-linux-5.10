package recoveryapply

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"syscall"
	"time"
)

const (
	ProductionMigrationDiskPath            = "/dev/mmcblk0"
	ProductionMigrationRootfsPath          = "/dev/disk/by-partlabel/rootfs"
	ProductionMigrationMiscPath            = "/dev/disk/by-partlabel/misc"
	ProductionMigrationUserdataPath        = "/dev/disk/by-partlabel/userdata"
	ProductionMigrationMountInfoPath       = "/proc/self/mountinfo"
	ProductionMigrationSourceMount         = "/mnt/rknetupdate-rootfs"
	ProductionMigrationUserdataMount       = "/userdata"
	ProductionMigrationStatusPath          = "/run/rknetupdate-rootfs-v2.status.json"
	ProductionMigrationSGDiskPath          = "/usr/sbin/sgdisk"
	ProductionMigrationPartprobePath       = "/usr/sbin/partprobe"
	ProductionMigrationE2fsckPath          = "/usr/sbin/e2fsck"
	ProductionMigrationResize2fsPath       = "/usr/sbin/resize2fs"
	ProductionMigrationMkfsExt4Path        = "/usr/sbin/mkfs.ext4"
	migrationSourceRelative                = "var/lib/rknetupdate/rootfs-v2"
	migrationStageRelative                 = "rknetupdate-rootfs-v2"
	migrationManifestName                  = "rootfs.manifest.json"
	migrationImageName                     = "rootfs.img"
	migrationStateName                     = "state.json"
	defaultStageMinimumMargin        int64 = 128 << 20
)

var minimumBlocksPattern = regexp.MustCompile(`(?m)minimum size of the filesystem:\s*([0-9]+)\s*$`)

var errMigrationTestStop = errors.New("migration stopped at test phase")

type MigrationConfig struct {
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
	ToolExtraEnv       []string
	TestMode           bool
	StopAfterPhase     string
	// Progress receives human-readable recovery milestones. It is optional so
	// library callers and host tests remain silent unless they opt in.
	Progress      func(phase, detail string)
	WriteProgress WriteProgress
}

type MigrationStatus struct {
	Schema           int              `json:"schema"`
	Phase            string           `json:"phase"`
	Success          bool             `json:"success"`
	BCBCleared       bool             `json:"bcb_cleared"`
	Request          RootfsBCBRequest `json:"request"`
	P6LastLBA        uint64           `json:"p6_last_lba,omitempty"`
	P7Present        bool             `json:"p7_present"`
	ReadbackSHA256   string           `json:"readback_sha256,omitempty"`
	ImageSize        int64            `json:"image_size,omitempty"`
	BytesWritten     int64            `json:"bytes_written,omitempty"`
	Progress         int              `json:"progress,omitempty"`
	FilesystemBlocks uint64           `json:"filesystem_blocks,omitempty"`
	Error            string           `json:"error,omitempty"`
	UpdatedAt        time.Time        `json:"updated_at"`
}

type migrationState struct {
	Schema  int              `json:"schema"`
	Phase   string           `json:"phase"`
	Request RootfsBCBRequest `json:"request"`
}

func DefaultMigrationConfig() MigrationConfig {
	return MigrationConfig{
		DiskPath:           ProductionMigrationDiskPath,
		RootfsPath:         ProductionMigrationRootfsPath,
		MiscPath:           ProductionMigrationMiscPath,
		UserdataPath:       ProductionMigrationUserdataPath,
		ExpectedParentPath: ProductionExpectedParentPath,
		MountInfoPath:      ProductionMigrationMountInfoPath,
		RecoveryMarkerPath: ProductionRecoveryMarkerPath,
		SourceMountPath:    ProductionMigrationSourceMount,
		UserdataMountPath:  ProductionMigrationUserdataMount,
		StatusPath:         ProductionMigrationStatusPath,
		SGDiskPath:         ProductionMigrationSGDiskPath,
		PartprobePath:      ProductionMigrationPartprobePath,
		E2fsckPath:         ProductionMigrationE2fsckPath,
		Resize2fsPath:      ProductionMigrationResize2fsPath,
		MkfsExt4Path:       ProductionMigrationMkfsExt4Path,
	}
}

func reportMigrationProgress(cfg MigrationConfig, phase, detail string) {
	if cfg.Progress != nil {
		cfg.Progress(phase, detail)
	}
}

func migrationWriteProgress(cfg MigrationConfig, status *MigrationStatus) WriteProgress {
	lastPublished := -1
	return func(phase string, written, total int64) {
		if total <= 0 {
			return
		}
		percent := int((written * 100) / total)
		if percent > 100 {
			percent = 100
		}
		status.ImageSize = total
		status.BytesWritten = written
		status.Progress = percent
		if cfg.WriteProgress != nil {
			cfg.WriteProgress(phase, written, total)
		}
		if percent == 100 || percent >= lastPublished+5 {
			status.UpdatedAt = time.Now().UTC()
			_ = writeJSONAtomic(cfg.StatusPath, *status)
			lastPublished = percent
		}
	}
}

func (c MigrationConfig) Validate() error {
	paths := map[string]string{
		"disk": c.DiskPath, "rootfs": c.RootfsPath, "misc": c.MiscPath, "userdata": c.UserdataPath,
		"expected-parent": c.ExpectedParentPath, "mountinfo": c.MountInfoPath, "marker": c.RecoveryMarkerPath,
		"source-mount": c.SourceMountPath, "userdata-mount": c.UserdataMountPath, "status": c.StatusPath,
		"sgdisk": c.SGDiskPath, "partprobe": c.PartprobePath, "e2fsck": c.E2fsckPath,
		"resize2fs": c.Resize2fsPath, "mkfs.ext4": c.MkfsExt4Path,
	}
	for name, path := range paths {
		if path == "" || !filepath.IsAbs(path) {
			return fmt.Errorf("migration %s path must be absolute", name)
		}
	}
	if c.StageMarginBytes < 0 {
		return errors.New("migration stage margin must not be negative")
	}
	if c.StopAfterPhase != "" && !c.TestMode {
		return errors.New("migration stop-after-phase is test-only")
	}
	if len(c.ToolExtraEnv) != 0 && !c.TestMode {
		return errors.New("migration tool environment overrides are test-only")
	}
	if c.TestMode {
		return nil
	}
	expected := map[string]string{
		"disk": ProductionMigrationDiskPath, "rootfs": ProductionMigrationRootfsPath,
		"misc": ProductionMigrationMiscPath, "userdata": ProductionMigrationUserdataPath,
		"expected-parent": ProductionExpectedParentPath, "mountinfo": ProductionMigrationMountInfoPath,
		"marker": ProductionRecoveryMarkerPath, "source-mount": ProductionMigrationSourceMount,
		"userdata-mount": ProductionMigrationUserdataMount, "status": ProductionMigrationStatusPath,
		"sgdisk": ProductionMigrationSGDiskPath, "partprobe": ProductionMigrationPartprobePath,
		"e2fsck": ProductionMigrationE2fsckPath, "resize2fs": ProductionMigrationResize2fsPath,
		"mkfs.ext4": ProductionMigrationMkfsExt4Path,
	}
	for name, want := range expected {
		if filepath.Clean(paths[name]) != want {
			return fmt.Errorf("production migration %s path is fixed at %s", name, want)
		}
	}
	return nil
}

func RunRootfsMigration(ctx context.Context, cfg MigrationConfig) (MigrationStatus, error) {
	status := MigrationStatus{Schema: 2, Phase: "starting"}
	fail := func(err error) (MigrationStatus, error) {
		status.Success = false
		status.Error = err.Error()
		reportMigrationProgress(cfg, "failed", status.Error)
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
		return fail(fmt.Errorf("validate migration misc: %w", err))
	}
	request, phase, err := ReadRootfsBCB(misc)
	if err != nil {
		return fail(err)
	}
	status.Request = request
	status.Phase = phase
	status.ImageSize = request.Size
	reportMigrationProgress(cfg, phase, "recovery entered; BCB and GPT request validated")
	mutator := GPTMutator{SGDiskPath: cfg.SGDiskPath, PartprobePath: cfg.PartprobePath, TestMode: cfg.TestMode, ExtraEnv: cfg.ToolExtraEnv}

	table, p6, p7, err := inspectMigrationGPT(cfg, request)
	if err != nil {
		return fail(err)
	}
	status.P6LastLBA = p6.LastLBA
	status.P7Present = p7 != nil
	if p7 == nil && p6.LastLBA < table.LastUsable && !phaseAtLeast(phase, RootfsPhaseP6Verified) {
		return fail(errors.New("shortened p6 without p7 is only valid after p6-verified"))
	}

	if p7 == nil && !phaseAtLeast(phase, RootfsPhaseP6Verified) {
		reportMigrationProgress(cfg, phase, "validating source rootfs and preparing temporary p7")
		if err := prepareTemporaryStage(ctx, cfg, mutator, misc, request, table, p6); err != nil {
			if errors.Is(err, errMigrationTestStop) {
				status.Phase = RootfsPhaseShrunk
				return status, nil
			}
			return fail(err)
		}
		misc, err = ResolveDevice(cfg.MiscPath, "misc", cfg.ExpectedParentPath, cfg.TestMode)
		if err != nil {
			return fail(fmt.Errorf("refresh migration misc after GPT change: %w", err))
		}
		if stopAt(cfg, RootfsPhaseP7Ready) {
			status.Phase = RootfsPhaseP7Ready
			return status, nil
		}
		table, p6, p7, err = inspectMigrationGPT(cfg, request)
		if err != nil {
			return fail(err)
		}
	}

	if p7 != nil && !phaseAtLeast(phase, RootfsPhaseP6Verified) {
		if err := ensureStagedSource(ctx, cfg, misc, request, phase); err != nil {
			return fail(err)
		}
		_, phase, err = ReadRootfsBCB(misc)
		if err != nil {
			return fail(err)
		}
		if stopAt(cfg, RootfsPhaseStaged) {
			status.Phase = RootfsPhaseStaged
			return status, nil
		}
	}

	if !phaseAtLeast(phase, RootfsPhaseP6Verified) {
		reportMigrationProgress(cfg, RootfsPhaseStaged, "writing rootfs image to p6 and verifying SHA-256 readback")
		readback, err := writeRootfsFromStage(ctx, cfg, misc, request, migrationWriteProgress(cfg, &status))
		if err != nil {
			return fail(err)
		}
		status.ReadbackSHA256 = readback
		phase = RootfsPhaseP6Verified
		reportMigrationProgress(cfg, phase, "p6 image write and readback verification completed")
		if stopAt(cfg, RootfsPhaseP6Verified) {
			status.Phase = phase
			return status, nil
		}
	}

	table, p6, p7, err = inspectMigrationGPT(cfg, request)
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
		table, p6, p7, err = inspectMigrationGPT(cfg, request)
		if err != nil {
			return fail(err)
		}
	}
	if p7 != nil || p6.LastLBA != table.LastUsable {
		reportMigrationProgress(cfg, RootfsPhaseP6Verified, "removing temporary p7 and restoring the full rootfs GPT partition")
		if err := verifyTargetFilesystem(cfg, request, false); err != nil {
			return fail(fmt.Errorf("verify target before deleting p7: %w", err))
		}
		if err := mutator.RestoreRootfs(ctx, cfg.DiskPath, p6Original(request, p6), table.LastUsable, p7 != nil); err != nil {
			return fail(err)
		}
		if err := waitForPath(cfg.RootfsPath, true, 10*time.Second); err != nil {
			return fail(err)
		}
		table, p6, p7, err = inspectMigrationGPT(cfg, request)
		if err != nil {
			return fail(err)
		}
		if p7 != nil || p6.LastLBA != table.LastUsable {
			return fail(errors.New("GPT restore did not remove p7 and extend p6"))
		}
	}
	if !phaseAtLeast(phase, RootfsPhaseGPTRestored) {
		misc, err = ResolveDevice(cfg.MiscPath, "misc", cfg.ExpectedParentPath, cfg.TestMode)
		if err != nil {
			return fail(fmt.Errorf("refresh migration misc after GPT restore: %w", err))
		}
		if err := UpdateRootfsBCBPhase(misc, request, RootfsPhaseGPTRestored); err != nil {
			return fail(err)
		}
		phase = RootfsPhaseGPTRestored
		reportMigrationProgress(cfg, phase, "GPT restored; p7 removed and p6 reaches the disk end")
	}
	if stopAt(cfg, RootfsPhaseGPTRestored) {
		status.Phase = phase
		return status, nil
	}

	reportMigrationProgress(cfg, RootfsPhaseGPTRestored, "expanding and checking ext4 on the restored p6")
	if err := expandRootfs(ctx, cfg, request, table, p6); err != nil {
		return fail(err)
	}
	if err := UpdateRootfsBCBPhase(misc, request, RootfsPhaseExpanded); err != nil {
		return fail(err)
	}
	status.Phase = RootfsPhaseExpanded
	reportMigrationProgress(cfg, RootfsPhaseExpanded, "filesystem expanded; clearing the completed recovery request")
	if stopAt(cfg, RootfsPhaseExpanded) {
		return status, nil
	}
	if err := ClearRootfsBCB(misc, request); err != nil {
		return fail(err)
	}
	status.Success = true
	status.BCBCleared = true
	status.Error = ""
	status.Phase = "succeeded"
	status.BytesWritten = request.Size
	status.Progress = 100
	reportMigrationProgress(cfg, status.Phase, "update complete; normal-system reboot will follow")
	_ = writeJSONAtomic(cfg.StatusPath, status)
	return status, nil
}

func inspectMigrationGPT(cfg MigrationConfig, request RootfsBCBRequest) (GPTTable, GPTPartition, *GPTPartition, error) {
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
		return GPTTable{}, GPTPartition{}, nil, errors.New("p6 GPT identity does not match BCB request")
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

func prepareTemporaryStage(ctx context.Context, cfg MigrationConfig, mutator GPTMutator, misc Device, request RootfsBCBRequest, table GPTTable, p6 GPTPartition) error {
	if p6.LastLBA != table.LastUsable {
		return errors.New("initial p6 must reach GPT last usable LBA")
	}
	if _, _, _, err := validateSourceOnRootfs(ctx, cfg, request); err != nil {
		return err
	}
	rootfs, err := ResolveDevice(cfg.RootfsPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return err
	}
	if err := ensureUnmounted(rootfs, cfg.SourceMountPath, cfg.MountInfoPath); err != nil {
		return err
	}
	reportMigrationProgress(cfg, RootfsPhasePrepared, "checking and shrinking p6 for the temporary staging partition")
	if err := runE2fsckRepair(ctx, cfg.E2fsckPath, rootfs.Path); err != nil {
		return err
	}
	f, err := openDevice(rootfs, os.O_RDONLY)
	if err != nil {
		return fmt.Errorf("inspect rootfs before shrink: %w", err)
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
	requiredBytes := requiredStageBytes(request.Size, cfg.StageMarginBytes)
	requiredSectors := (uint64(requiredBytes) + table.SectorSize - 1) / table.SectorSize
	alignment := uint64((1 << 20) / table.SectorSize)
	if alignment == 0 {
		alignment = 1
	}
	if requiredSectors >= table.LastUsable-p6.FirstLBA {
		return errors.New("insufficient disk space for temporary p7")
	}
	p7Start := ((table.LastUsable + 1 - requiredSectors) / alignment) * alignment
	if p7Start <= p6.FirstLBA {
		return errors.New("insufficient aligned disk space for temporary p7")
	}
	p6End := p7Start - 1
	p6Bytes := (p6End - p6.FirstLBA + 1) * table.SectorSize
	targetBlocks := p6Bytes / currentFS.BlockSize
	minSafeBlocks, maxSafeBlocks, err := shrinkVerificationRange(minimumBlocks, targetBlocks)
	if err != nil {
		return fmt.Errorf("insufficient shrink headroom: %w", err)
	}
	if err := runResize2fs(ctx, cfg.Resize2fsPath, rootfs.Path, strconv.FormatUint(targetBlocks, 10)); err != nil {
		return err
	}
	if err := verifyFilesystemGeometry(rootfs, currentFS.UUID, currentFS.BlockSize, minSafeBlocks, maxSafeBlocks); err != nil {
		return fmt.Errorf("verify shrunk rootfs: %w", err)
	}
	if err := UpdateRootfsBCBPhase(misc, request, RootfsPhaseShrunk); err != nil {
		return err
	}
	reportMigrationProgress(cfg, RootfsPhaseShrunk, "p6 shrunk; creating temporary userdata p7")
	if stopAt(cfg, RootfsPhaseShrunk) {
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
	newTable, newP6, newP7, err := inspectMigrationGPT(cfg, request)
	if err != nil {
		return err
	}
	if newP7 == nil || newP6.LastLBA != p6End || newTable.LastUsable != table.LastUsable {
		return errors.New("temporary p7 GPT creation verification failed")
	}
	misc, err = ResolveDevice(cfg.MiscPath, "misc", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return fmt.Errorf("refresh migration misc after creating p7: %w", err)
	}
	if err := UpdateRootfsBCBPhase(misc, request, RootfsPhaseP7Ready); err != nil {
		return err
	}
	reportMigrationProgress(cfg, RootfsPhaseP7Ready, "temporary p7 is ready; it will now be initialized as ext4")
	return nil
}

func ensureStagedSource(ctx context.Context, cfg MigrationConfig, misc Device, request RootfsBCBRequest, phase string) error {
	if err := waitForPath(cfg.UserdataPath, true, 10*time.Second); err != nil {
		return err
	}
	userdata, err := ResolveDevice(cfg.UserdataPath, "userdata", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return err
	}
	if phaseAtLeast(phase, RootfsPhaseStaged) {
		reportMigrationProgress(cfg, phase, "validating the existing persistent p7 stage")
		if err := ensureMounted(userdata, cfg.UserdataMountPath, false, cfg.MountInfoPath); err != nil {
			return fmt.Errorf("mount existing staged p7: %w", err)
		}
		if _, err := validateStagedSource(ctx, cfg, request); err != nil {
			return fmt.Errorf("persisted staged source is invalid: %w", err)
		}
		return nil
	}

	// A newly-created p7 is raw GPT space, not an ext4 filesystem. Format it
	// directly instead of deliberately probing it with mount(2), which emits a
	// misleading kernel "Can't find ext4 filesystem" error on every update.
	reportMigrationProgress(cfg, RootfsPhaseP7Ready, "formatting the new temporary p7 userdata filesystem")
	if err := ensureUnmounted(userdata, cfg.UserdataMountPath, cfg.MountInfoPath); err != nil {
		return err
	}
	if err := runCommandChecked(ctx, cfg.MkfsExt4Path, "-F", "-L", "userdata", userdata.Path); err != nil {
		return err
	}
	if err := ensureMounted(userdata, cfg.UserdataMountPath, false, cfg.MountInfoPath); err != nil {
		return err
	}

	reportMigrationProgress(cfg, RootfsPhaseP7Ready, "copying the rootfs image and manifest to temporary p7")
	manifest, sourceImage, _, err := validateSourceOnRootfs(ctx, cfg, request)
	if err != nil {
		return err
	}
	stageDir := filepath.Join(cfg.UserdataMountPath, migrationStageRelative)
	if err := os.MkdirAll(stageDir, 0700); err != nil {
		return err
	}
	if err := copyFileSync(sourceImage, filepath.Join(stageDir, migrationImageName), 0600); err != nil {
		return err
	}
	sourceManifest := filepath.Join(cfg.SourceMountPath, migrationSourceRelative, migrationManifestName)
	if err := copyFileSync(sourceManifest, filepath.Join(stageDir, migrationManifestName), 0600); err != nil {
		return err
	}
	state := migrationState{Schema: 2, Phase: RootfsPhaseStaged, Request: request}
	if err := writeJSONAtomic(filepath.Join(stageDir, migrationStateName), state); err != nil {
		return err
	}
	if err := syncFilesystem(cfg.UserdataMountPath); err != nil {
		return err
	}
	_ = manifest
	if _, err := validateStagedSource(ctx, cfg, request); err != nil {
		return err
	}
	rootfs, err := ResolveDevice(cfg.RootfsPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return err
	}
	if err := ensureUnmounted(rootfs, cfg.SourceMountPath, cfg.MountInfoPath); err != nil {
		return err
	}
	if err := UpdateRootfsBCBPhase(misc, request, RootfsPhaseStaged); err != nil {
		return err
	}
	reportMigrationProgress(cfg, RootfsPhaseStaged, "temporary p7 stage is durable and verified")
	return nil
}

func writeRootfsFromStage(ctx context.Context, cfg MigrationConfig, misc Device, request RootfsBCBRequest, progress WriteProgress) (string, error) {
	if _, err := validateStagedSource(ctx, cfg, request); err != nil {
		return "", err
	}
	rootfs, err := ResolveDevice(cfg.RootfsPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return "", err
	}
	if err := ensureUnmounted(rootfs, cfg.SourceMountPath, cfg.MountInfoPath); err != nil {
		return "", err
	}
	if request.Size > rootfs.Size {
		return "", errors.New("staged rootfs image exceeds shortened p6")
	}
	if current, err := hashDevicePrefix(rootfs, request.Size); err == nil && current == request.SHA256 {
		if err := persistP6Verified(cfg, misc, request); err != nil {
			return "", err
		}
		return current, nil
	}
	imagePath := filepath.Join(cfg.UserdataMountPath, migrationStageRelative, migrationImageName)
	image, err := os.Open(imagePath)
	if err != nil {
		return "", err
	}
	defer image.Close()
	written, readback, err := writeImage(ctx, image, rootfs, request.Size, request.SHA256, "rootfs-writing", progress)
	if err != nil {
		return readback, fmt.Errorf("rootfs raw write stopped after %d bytes: %w", written, err)
	}
	if err := verifyTargetFilesystem(cfg, request, true); err != nil {
		return readback, err
	}
	if err := persistP6Verified(cfg, misc, request); err != nil {
		return readback, err
	}
	return readback, nil
}

func persistP6Verified(cfg MigrationConfig, misc Device, request RootfsBCBRequest) error {
	stageDir := filepath.Join(cfg.UserdataMountPath, migrationStageRelative)
	if err := writeJSONAtomic(filepath.Join(stageDir, migrationStateName), migrationState{Schema: 2, Phase: RootfsPhaseP6Verified, Request: request}); err != nil {
		return err
	}
	if err := syncFilesystem(cfg.UserdataMountPath); err != nil {
		return err
	}
	return UpdateRootfsBCBPhase(misc, request, RootfsPhaseP6Verified)
}

func validateSourceOnRootfs(ctx context.Context, cfg MigrationConfig, request RootfsBCBRequest) (RootfsManifestV2, string, Ext4Info, error) {
	rootfs, err := ResolveDevice(cfg.RootfsPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return RootfsManifestV2{}, "", Ext4Info{}, err
	}
	if err := ensureMounted(rootfs, cfg.SourceMountPath, true, cfg.MountInfoPath); err != nil {
		return RootfsManifestV2{}, "", Ext4Info{}, err
	}
	manifestPath := filepath.Join(cfg.SourceMountPath, migrationSourceRelative, migrationManifestName)
	manifest, imagePath, ext4, err := ValidateRootfsImageV2(ctx, manifestPath, cfg.E2fsckPath, cfg.TestMode)
	if err != nil {
		return RootfsManifestV2{}, "", Ext4Info{}, err
	}
	if !manifestMatchesRequest(manifest, request) {
		return RootfsManifestV2{}, "", Ext4Info{}, errors.New("source manifest does not match rootfs BCB request")
	}
	return manifest, imagePath, ext4, nil
}

func validateStagedSource(ctx context.Context, cfg MigrationConfig, request RootfsBCBRequest) (RootfsManifestV2, error) {
	manifestPath := filepath.Join(cfg.UserdataMountPath, migrationStageRelative, migrationManifestName)
	manifest, _, _, err := ValidateRootfsImageV2(ctx, manifestPath, cfg.E2fsckPath, cfg.TestMode)
	if err != nil {
		return RootfsManifestV2{}, err
	}
	if !manifestMatchesRequest(manifest, request) {
		return RootfsManifestV2{}, errors.New("staged manifest does not match rootfs BCB request")
	}
	statePath := filepath.Join(cfg.UserdataMountPath, migrationStageRelative, migrationStateName)
	var state migrationState
	if err := readJSONStrict(statePath, &state); err != nil {
		return RootfsManifestV2{}, fmt.Errorf("read staged migration state: %w", err)
	}
	if state.Schema != 2 || state.Request != request ||
		(state.Phase != RootfsPhaseStaged && state.Phase != RootfsPhaseP6Verified) {
		return RootfsManifestV2{}, errors.New("staged migration state is invalid")
	}
	return manifest, nil
}

func expandRootfs(ctx context.Context, cfg MigrationConfig, request RootfsBCBRequest, table GPTTable, p6 GPTPartition) error {
	if p6.LastLBA != table.LastUsable {
		return errors.New("cannot expand rootfs before GPT reaches last usable LBA")
	}
	if err := verifyTargetFilesystem(cfg, request, false); err != nil {
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
		return fmt.Errorf("inspect expanded rootfs: %w", err)
	}
	ext4, err := InspectExt4Device(f, rootfs.Size)
	_ = f.Close()
	if err != nil {
		return err
	}
	wantBlocks := uint64(rootfs.Size) / request.BlockSize
	if ext4.UUID != request.FilesystemUUID || ext4.BlockSize != request.BlockSize || ext4.BlockCount != wantBlocks {
		return fmt.Errorf("expanded rootfs geometry mismatch: uuid=%s block_size=%d blocks=%d want_blocks=%d", ext4.UUID, ext4.BlockSize, ext4.BlockCount, wantBlocks)
	}
	return nil
}

func verifyTargetFilesystem(cfg MigrationConfig, request RootfsBCBRequest, requireExactBlocks bool) error {
	rootfs, err := ResolveDevice(cfg.RootfsPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return err
	}
	f, err := openDevice(rootfs, os.O_RDONLY)
	if err != nil {
		return fmt.Errorf("inspect target filesystem: %w", err)
	}
	ext4, err := InspectExt4Device(f, rootfs.Size)
	_ = f.Close()
	if err != nil {
		return err
	}
	if ext4.UUID != request.FilesystemUUID || ext4.BlockSize != request.BlockSize || ext4.BlockCount < request.BlockCount {
		return errors.New("target ext4 identity does not match requested image")
	}
	if requireExactBlocks && ext4.BlockCount != request.BlockCount {
		return errors.New("target ext4 block count does not match raw image")
	}
	return nil
}

func verifyFilesystemGeometry(device Device, uuid string, blockSize, minBlocks, maxBlocks uint64) error {
	f, err := openDevice(device, os.O_RDONLY)
	if err != nil {
		return err
	}
	info, err := InspectExt4Device(f, device.Size)
	_ = f.Close()
	if err != nil {
		return err
	}
	if info.UUID != uuid || info.BlockSize != blockSize || info.BlockCount < minBlocks || info.BlockCount > maxBlocks {
		return fmt.Errorf(
			"filesystem geometry mismatch: uuid=%s block_size=%d blocks=%d expected_uuid=%s expected_block_size=%d expected_blocks=%d..%d",
			info.UUID, info.BlockSize, info.BlockCount, uuid, blockSize, minBlocks, maxBlocks,
		)
	}
	return nil
}

func shrinkVerificationRange(minimumBlocks, targetBlocks uint64) (uint64, uint64, error) {
	safetyBlocks := minimumBlocks / 100
	if safetyBlocks < 1024 {
		safetyBlocks = 1024
	}
	if targetBlocks <= minimumBlocks || targetBlocks-minimumBlocks < safetyBlocks {
		return 0, 0, fmt.Errorf("target blocks %d minimum %d safety %d", targetBlocks, minimumBlocks, safetyBlocks)
	}
	return minimumBlocks + safetyBlocks, targetBlocks, nil
}

func p6Original(request RootfsBCBRequest, current GPTPartition) GPTPartition {
	return GPTPartition{Number: 6, TypeGUID: request.P6TypeGUID, UniqueGUID: request.P6UniqueGUID, FirstLBA: request.P6Start, LastLBA: current.LastLBA, Name: request.P6Name}
}

func manifestMatchesRequest(manifest RootfsManifestV2, request RootfsBCBRequest) bool {
	return manifest.Schema == request.Schema && manifest.Size == request.Size && manifest.SHA256 == request.SHA256 &&
		manifest.FilesystemUUID == request.FilesystemUUID && manifest.BlockSize == request.BlockSize && manifest.BlockCount == request.BlockCount
}

func requiredStageBytes(imageSize, override int64) int64 {
	margin := override
	if margin == 0 {
		margin = imageSize / 10
		if margin < defaultStageMinimumMargin {
			margin = defaultStageMinimumMargin
		}
	}
	const alignment = int64(1 << 20)
	total := imageSize + margin + alignment
	return ((total + alignment - 1) / alignment) * alignment
}

func ensureMounted(device Device, mountPath string, readOnly bool, mountInfoPath string) error {
	records, err := ReadMountInfo(mountInfoPath)
	if err != nil {
		return err
	}
	if mounted, point := IsDeviceMounted(device, records); mounted {
		if filepath.Clean(point) != filepath.Clean(mountPath) {
			return fmt.Errorf("device is already mounted at %s", point)
		}
		return nil
	}
	if err := os.MkdirAll(mountPath, 0700); err != nil {
		return err
	}
	flags := uintptr(syscall.MS_NOATIME | syscall.MS_NODEV | syscall.MS_NOSUID)
	if readOnly {
		flags |= syscall.MS_RDONLY
	}
	if err := syscall.Mount(device.Path, mountPath, "ext4", flags, ""); err != nil {
		return fmt.Errorf("mount %s at %s: %w", device.Path, mountPath, err)
	}
	return nil
}

func ensureUnmounted(device Device, mountPath, mountInfoPath string) error {
	records, err := ReadMountInfo(mountInfoPath)
	if err != nil {
		return err
	}
	cleanMount := filepath.Clean(mountPath)
	if mounted, point := IsDeviceMounted(device, records); mounted {
		if filepath.Clean(point) != cleanMount {
			return fmt.Errorf("device is mounted at unexpected path %s", point)
		}
		if err := syscall.Unmount(cleanMount, 0); err != nil {
			return fmt.Errorf("unmount %s: %w", cleanMount, err)
		}
		return nil
	}
	for _, record := range records {
		if filepath.Clean(record.MountPoint) == cleanMount {
			return fmt.Errorf("mount point %s is occupied by unexpected source %s", cleanMount, record.Source)
		}
	}
	return nil
}

func runE2fsckRepair(ctx context.Context, binary, target string) error {
	command := exec.CommandContext(ctx, binary, "-f", "-p", target)
	command.Env = []string{"LC_ALL=C", "PATH=/usr/sbin:/usr/bin:/sbin:/bin"}
	output, err := command.CombinedOutput()
	if err == nil {
		return nil
	}
	var exitErr *exec.ExitError
	if errors.As(err, &exitErr) && exitErr.ExitCode() == 1 {
		return nil
	}
	return fmt.Errorf("e2fsck repair failed: %w: %s", err, strings.TrimSpace(string(output)))
}

func readMinimumBlocks(ctx context.Context, binary, target string) (uint64, error) {
	command := exec.CommandContext(ctx, binary, "-P", target)
	command.Env = []string{"LC_ALL=C", "PATH=/usr/sbin:/usr/bin:/sbin:/bin"}
	output, err := command.CombinedOutput()
	if err != nil {
		return 0, fmt.Errorf("resize2fs minimum-size query failed: %w: %s", err, strings.TrimSpace(string(output)))
	}
	match := minimumBlocksPattern.FindSubmatch(output)
	if len(match) != 2 {
		return 0, fmt.Errorf("cannot parse resize2fs minimum size: %s", strings.TrimSpace(string(output)))
	}
	blocks, err := strconv.ParseUint(string(match[1]), 10, 64)
	if err != nil || blocks == 0 {
		return 0, errors.New("invalid resize2fs minimum block count")
	}
	return blocks, nil
}

func runResize2fs(ctx context.Context, binary, target string, size ...string) error {
	args := append([]string{target}, size...)
	return runCommandChecked(ctx, binary, args...)
}

func runCommandChecked(ctx context.Context, binary string, args ...string) error {
	command := exec.CommandContext(ctx, binary, args...)
	command.Env = []string{"LC_ALL=C", "PATH=/usr/sbin:/usr/bin:/sbin:/bin"}
	output, err := command.CombinedOutput()
	if err != nil {
		return fmt.Errorf("%s failed: %w: %s", filepath.Base(binary), err, strings.TrimSpace(string(output)))
	}
	return nil
}

func copyFileSync(source, destination string, mode os.FileMode) error {
	input, err := os.Open(source)
	if err != nil {
		return err
	}
	defer input.Close()
	tmp := destination + ".tmp"
	output, err := os.OpenFile(tmp, os.O_CREATE|os.O_WRONLY|os.O_TRUNC|syscall.O_NOFOLLOW, mode)
	if err != nil {
		return err
	}
	if _, err := io.Copy(output, input); err != nil {
		_ = output.Close()
		return err
	}
	if err := output.Sync(); err != nil {
		_ = output.Close()
		return err
	}
	if err := output.Close(); err != nil {
		return err
	}
	if err := os.Rename(tmp, destination); err != nil {
		return err
	}
	dir, err := os.Open(filepath.Dir(destination))
	if err != nil {
		return err
	}
	defer dir.Close()
	return dir.Sync()
}

func hashDevicePrefix(device Device, size int64) (string, error) {
	f, err := openDevice(device, os.O_RDONLY)
	if err != nil {
		return "", err
	}
	defer f.Close()
	hash := sha256.New()
	if _, err := io.CopyN(hash, f, size); err != nil {
		return "", err
	}
	return hex.EncodeToString(hash.Sum(nil)), nil
}

func syncFilesystem(path string) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	if err := f.Sync(); err != nil {
		return fmt.Errorf("fsync %s: %w", path, err)
	}
	return nil
}

func writeJSONAtomic(path string, value any) error {
	dir := filepath.Dir(path)
	if err := os.MkdirAll(dir, 0700); err != nil {
		return err
	}
	tmp, err := os.CreateTemp(dir, ".rknetupdate-state.*")
	if err != nil {
		return err
	}
	tmpPath := tmp.Name()
	defer os.Remove(tmpPath)
	if err := tmp.Chmod(0600); err != nil {
		_ = tmp.Close()
		return err
	}
	encoder := json.NewEncoder(tmp)
	encoder.SetIndent("", "  ")
	if err := encoder.Encode(value); err != nil {
		_ = tmp.Close()
		return err
	}
	if err := tmp.Sync(); err != nil {
		_ = tmp.Close()
		return err
	}
	if err := tmp.Close(); err != nil {
		return err
	}
	if err := os.Rename(tmpPath, path); err != nil {
		return err
	}
	dirFile, err := os.Open(dir)
	if err != nil {
		return err
	}
	defer dirFile.Close()
	return dirFile.Sync()
}

func readJSONStrict(path string, destination any) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	decoder := json.NewDecoder(io.LimitReader(f, maxManifestBytes+1))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(destination); err != nil {
		return err
	}
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		return errors.New("JSON file contains trailing data")
	}
	return nil
}

func waitForPath(path string, present bool, timeout time.Duration) error {
	deadline := time.Now().Add(timeout)
	for {
		_, err := os.Stat(path)
		exists := err == nil
		if exists == present {
			return nil
		}
		if err != nil && !errors.Is(err, os.ErrNotExist) {
			return err
		}
		if time.Now().After(deadline) {
			return fmt.Errorf("timed out waiting for path %s present=%t", path, present)
		}
		time.Sleep(100 * time.Millisecond)
	}
}

func phaseAtLeast(actual, expected string) bool {
	actualIndex, expectedIndex := -1, -1
	for i, phase := range rootfsPhaseOrder {
		if phase == actual {
			actualIndex = i
		}
		if phase == expected {
			expectedIndex = i
		}
	}
	return actualIndex >= expectedIndex && expectedIndex >= 0
}

func stopAt(cfg MigrationConfig, phase string) bool {
	return cfg.TestMode && cfg.StopAfterPhase == phase
}
