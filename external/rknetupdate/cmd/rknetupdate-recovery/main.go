package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"strings"
	"syscall"

	"rknetupdate/internal/recoveryapply"
)

func main() {
	cfg := recoveryapply.DefaultConfig()
	migration := recoveryapply.DefaultMigrationConfig()
	bundleMigration := recoveryapply.DefaultBundleMigrationConfig()
	updateImageMigration := recoveryapply.DefaultUpdateImageMigrationConfig()
	flags := flag.NewFlagSet("rknetupdate-recovery", flag.ContinueOnError)
	flags.SetOutput(os.Stderr)
	flags.StringVar(&cfg.ManifestPath, "manifest", cfg.ManifestPath, "rootfs manifest path")
	flags.StringVar(&cfg.StatusPath, "status", cfg.StatusPath, "atomic status record path")
	flags.StringVar(&cfg.RequestPath, "request", cfg.RequestPath, "apply request path")
	flags.StringVar(&cfg.TargetPath, "target", cfg.TargetPath, "rootfs partition path")
	flags.StringVar(&cfg.MiscPath, "misc", cfg.MiscPath, "misc partition path")
	flags.StringVar(&cfg.UserdataPath, "userdata", cfg.UserdataPath, "userdata partition path")
	flags.StringVar(&cfg.ExpectedParentPath, "expected-parent", cfg.ExpectedParentPath, "expected parent disk")
	flags.StringVar(&cfg.MountInfoPath, "mountinfo", cfg.MountInfoPath, "mountinfo path")
	flags.StringVar(&cfg.RecoveryMarkerPath, "recovery-marker", cfg.RecoveryMarkerPath, "recovery environment marker")
	flags.StringVar(&cfg.E2fsckPath, "e2fsck", cfg.E2fsckPath, "e2fsck binary path")
	flags.StringVar(&cfg.SmokeStatusPath, "smoke-status", cfg.SmokeStatusPath, "RAM-backed smoke status path")
	flags.BoolVar(&cfg.Apply, "apply", false, "write rootfs after all checks pass")
	flags.BoolVar(&cfg.AllowWrite, "allow-write", false, "second opt-in required for writes")
	flags.StringVar(&cfg.Confirmation, "confirm", "", "must equal WRITE_ROOTFS:<manifest sha256>")
	flags.BoolVar(&cfg.Smoke, "smoke", false, "verify the one-shot smoke request and clear only its BCB command fields")
	flags.BoolVar(&cfg.TestMode, "test-mode", false, "allow regular files and loop devices for host tests")
	resumeRootfsV2 := flags.Bool("resume-rootfs-v2", false, "resume the fixed temporary-p7 rootfs v2 workflow")
	resumeBundleV1 := flags.Bool("resume-bundle-v1", false, "resume the fixed temporary-p7 multi-image bundle v1 workflow")
	resumeUpdateImageV1 := flags.Bool("resume-updateimg-v1", false, "resume the fixed temporary-p7 complete update.img OTA workflow")
	stayInRecovery := flags.Bool("stay-in-recovery", false, "do not reboot automatically after a successful recovery operation")
	flags.StringVar(&migration.DiskPath, "migration-disk", migration.DiskPath, "test-only migration disk path")
	flags.StringVar(&migration.RootfsPath, "migration-rootfs", migration.RootfsPath, "test-only migration rootfs path")
	flags.StringVar(&migration.MiscPath, "migration-misc", migration.MiscPath, "test-only migration misc path")
	flags.StringVar(&migration.UserdataPath, "migration-userdata", migration.UserdataPath, "test-only migration userdata path")
	flags.StringVar(&migration.ExpectedParentPath, "migration-parent", migration.ExpectedParentPath, "test-only migration parent disk")
	flags.StringVar(&migration.MountInfoPath, "migration-mountinfo", migration.MountInfoPath, "test-only migration mountinfo path")
	flags.StringVar(&migration.RecoveryMarkerPath, "migration-marker", migration.RecoveryMarkerPath, "test-only migration marker path")
	flags.StringVar(&migration.SourceMountPath, "migration-source-mount", migration.SourceMountPath, "test-only source mount")
	flags.StringVar(&migration.UserdataMountPath, "migration-userdata-mount", migration.UserdataMountPath, "test-only userdata mount")
	flags.StringVar(&migration.StatusPath, "migration-status", migration.StatusPath, "migration RAM status path")
	flags.StringVar(&migration.SGDiskPath, "migration-sgdisk", migration.SGDiskPath, "test-only sgdisk path")
	flags.StringVar(&migration.PartprobePath, "migration-partprobe", migration.PartprobePath, "test-only partprobe path")
	flags.StringVar(&migration.E2fsckPath, "migration-e2fsck", migration.E2fsckPath, "test-only e2fsck path")
	flags.StringVar(&migration.Resize2fsPath, "migration-resize2fs", migration.Resize2fsPath, "test-only resize2fs path")
	flags.StringVar(&migration.MkfsExt4Path, "migration-mkfs-ext4", migration.MkfsExt4Path, "test-only mkfs.ext4 path")
	flags.Int64Var(&migration.StageMarginBytes, "migration-stage-margin", migration.StageMarginBytes, "test-only p7 stage margin bytes")
	flags.StringVar(&migration.StopAfterPhase, "migration-stop-after", "", "test-only persistent phase stop")
	migrationToolPreload := flags.String("migration-tool-ld-preload", "", "test-only LD_PRELOAD for sgdisk")
	flags.StringVar(&bundleMigration.DiskPath, "bundle-migration-disk", bundleMigration.DiskPath, "test-only bundle migration disk path")
	flags.StringVar(&bundleMigration.RootfsPath, "bundle-migration-rootfs", bundleMigration.RootfsPath, "test-only bundle migration rootfs path")
	flags.StringVar(&bundleMigration.BootPath, "bundle-migration-boot", bundleMigration.BootPath, "test-only bundle migration boot path")
	flags.StringVar(&bundleMigration.UbootPath, "bundle-migration-uboot", bundleMigration.UbootPath, "test-only bundle migration uboot path")
	flags.StringVar(&bundleMigration.MiscPath, "bundle-migration-misc", bundleMigration.MiscPath, "test-only bundle migration misc path")
	flags.StringVar(&bundleMigration.UserdataPath, "bundle-migration-userdata", bundleMigration.UserdataPath, "test-only bundle migration userdata path")
	flags.StringVar(&bundleMigration.ExpectedParentPath, "bundle-migration-parent", bundleMigration.ExpectedParentPath, "test-only bundle migration parent disk")
	flags.StringVar(&bundleMigration.MountInfoPath, "bundle-migration-mountinfo", bundleMigration.MountInfoPath, "test-only bundle migration mountinfo path")
	flags.StringVar(&bundleMigration.RecoveryMarkerPath, "bundle-migration-marker", bundleMigration.RecoveryMarkerPath, "test-only bundle migration marker path")
	flags.StringVar(&bundleMigration.SourceMountPath, "bundle-migration-source-mount", bundleMigration.SourceMountPath, "test-only bundle source mount")
	flags.StringVar(&bundleMigration.UserdataMountPath, "bundle-migration-userdata-mount", bundleMigration.UserdataMountPath, "test-only bundle userdata mount")
	flags.StringVar(&bundleMigration.StatusPath, "bundle-migration-status", bundleMigration.StatusPath, "bundle migration RAM status path")
	flags.StringVar(&bundleMigration.SGDiskPath, "bundle-migration-sgdisk", bundleMigration.SGDiskPath, "test-only bundle sgdisk path")
	flags.StringVar(&bundleMigration.PartprobePath, "bundle-migration-partprobe", bundleMigration.PartprobePath, "test-only bundle partprobe path")
	flags.StringVar(&bundleMigration.E2fsckPath, "bundle-migration-e2fsck", bundleMigration.E2fsckPath, "test-only bundle e2fsck path")
	flags.StringVar(&bundleMigration.Resize2fsPath, "bundle-migration-resize2fs", bundleMigration.Resize2fsPath, "test-only bundle resize2fs path")
	flags.StringVar(&bundleMigration.MkfsExt4Path, "bundle-migration-mkfs-ext4", bundleMigration.MkfsExt4Path, "test-only bundle mkfs.ext4 path")
	flags.Int64Var(&bundleMigration.StageMarginBytes, "bundle-migration-stage-margin", bundleMigration.StageMarginBytes, "test-only bundle p7 stage margin bytes")
	flags.StringVar(&bundleMigration.StopAfterPhase, "bundle-migration-stop-after", "", "test-only bundle persistent phase stop")
	bundleMigrationToolPreload := flags.String("bundle-migration-tool-ld-preload", "", "test-only LD_PRELOAD for bundle sgdisk")
	flags.StringVar(&updateImageMigration.DiskPath, "updateimg-migration-disk", updateImageMigration.DiskPath, "test-only update.img migration disk path")
	flags.StringVar(&updateImageMigration.RootfsPath, "updateimg-migration-rootfs", updateImageMigration.RootfsPath, "test-only update.img migration rootfs path")
	flags.StringVar(&updateImageMigration.MiscPath, "updateimg-migration-misc", updateImageMigration.MiscPath, "test-only update.img migration misc path")
	flags.StringVar(&updateImageMigration.UserdataPath, "updateimg-migration-userdata", updateImageMigration.UserdataPath, "test-only update.img migration userdata path")
	flags.StringVar(&updateImageMigration.ExpectedParentPath, "updateimg-migration-parent", updateImageMigration.ExpectedParentPath, "test-only update.img migration parent disk")
	flags.StringVar(&updateImageMigration.MountInfoPath, "updateimg-migration-mountinfo", updateImageMigration.MountInfoPath, "test-only update.img migration mountinfo path")
	flags.StringVar(&updateImageMigration.RecoveryMarkerPath, "updateimg-migration-marker", updateImageMigration.RecoveryMarkerPath, "test-only update.img migration marker path")
	flags.StringVar(&updateImageMigration.SourceMountPath, "updateimg-migration-source-mount", updateImageMigration.SourceMountPath, "test-only update.img source mount")
	flags.StringVar(&updateImageMigration.UserdataMountPath, "updateimg-migration-userdata-mount", updateImageMigration.UserdataMountPath, "test-only update.img userdata mount")
	flags.StringVar(&updateImageMigration.StatusPath, "updateimg-migration-status", updateImageMigration.StatusPath, "update.img migration RAM status path")
	flags.StringVar(&updateImageMigration.SGDiskPath, "updateimg-migration-sgdisk", updateImageMigration.SGDiskPath, "test-only update.img sgdisk path")
	flags.StringVar(&updateImageMigration.PartprobePath, "updateimg-migration-partprobe", updateImageMigration.PartprobePath, "test-only update.img partprobe path")
	flags.StringVar(&updateImageMigration.E2fsckPath, "updateimg-migration-e2fsck", updateImageMigration.E2fsckPath, "test-only update.img e2fsck path")
	flags.StringVar(&updateImageMigration.Resize2fsPath, "updateimg-migration-resize2fs", updateImageMigration.Resize2fsPath, "test-only update.img resize2fs path")
	flags.StringVar(&updateImageMigration.MkfsExt4Path, "updateimg-migration-mkfs-ext4", updateImageMigration.MkfsExt4Path, "test-only update.img mkfs.ext4 path")
	flags.Int64Var(&updateImageMigration.StageMarginBytes, "updateimg-migration-stage-margin", updateImageMigration.StageMarginBytes, "test-only update.img p7 stage margin bytes")
	flags.StringVar(&updateImageMigration.StopAfterPhase, "updateimg-migration-stop-after", "", "test-only update.img persistent phase stop")
	updateImageMigrationToolPreload := flags.String("updateimg-migration-tool-ld-preload", "", "test-only LD_PRELOAD for update.img sgdisk")
	if err := flags.Parse(os.Args[1:]); err != nil {
		if errors.Is(err, flag.ErrHelp) {
			return
		}
		os.Exit(2)
	}
	if len(flags.Args()) != 0 {
		fmt.Fprintln(os.Stderr, "rknetupdate-recovery does not accept positional arguments")
		os.Exit(2)
	}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	var status any
	var err error
	autoReboot := false
	modeCount := recoveryModeCount(cfg, *resumeRootfsV2, *resumeBundleV1, *resumeUpdateImageV1)
	if modeCount > 1 {
		status = map[string]any{"error": "recovery operation modes are mutually exclusive"}
		err = errors.New("choose one recovery operation mode")
	} else if *resumeRootfsV2 {
		migration.TestMode = cfg.TestMode
		printer := newRecoveryProgressPrinter("rootfs-v2")
		migration.Progress = printer.milestone
		migration.WriteProgress = printer.write
		if *migrationToolPreload != "" {
			migration.ToolExtraEnv = []string{"LD_PRELOAD=" + *migrationToolPreload}
		}
		status, err = recoveryapply.RunRootfsMigration(ctx, migration)
		autoReboot = true
	} else if *resumeBundleV1 {
		bundleMigration.TestMode = cfg.TestMode
		bundleMigration.WriteProgress = newRecoveryProgressPrinter("bundle-v1").write
		if *bundleMigrationToolPreload != "" {
			bundleMigration.ToolExtraEnv = []string{"LD_PRELOAD=" + *bundleMigrationToolPreload}
		}
		status, err = recoveryapply.RunBundleMigration(ctx, bundleMigration)
		autoReboot = true
	} else if *resumeUpdateImageV1 {
		updateImageMigration.TestMode = cfg.TestMode
		printer := newRecoveryProgressPrinter("updateimg-v1")
		updateImageMigration.Progress = printer.milestone
		updateImageMigration.WriteProgress = printer.write
		if *updateImageMigrationToolPreload != "" {
			updateImageMigration.ToolExtraEnv = []string{"LD_PRELOAD=" + *updateImageMigrationToolPreload}
		}
		status, err = recoveryapply.RunUpdateImageMigration(ctx, updateImageMigration)
		autoReboot = true
	} else {
		printer := newRecoveryProgressPrinter("rootfs-v1")
		cfg.Progress = printer.milestone
		cfg.WriteProgress = printer.write
		status, err = recoveryapply.Run(ctx, cfg)
		autoReboot = cfg.Apply || cfg.Smoke
	}
	encoder := json.NewEncoder(os.Stdout)
	encoder.SetIndent("", "  ")
	if encodeErr := encoder.Encode(status); encodeErr != nil {
		fmt.Fprintf(os.Stderr, "encode result: %v\n", encodeErr)
		os.Exit(1)
	}
	if err != nil {
		fmt.Fprintf(os.Stderr, "rknetupdate-recovery: %v\n", err)
		os.Exit(1)
	}
	if shouldAutoReboot(cfg.TestMode, *stayInRecovery, autoReboot, status) {
		fmt.Fprintln(os.Stderr, "RKNETUPDATE-RECOVERY recovery operation succeeded; syncing and rebooting normal system")
		if err := rebootNormalSystem(); err != nil {
			fmt.Fprintf(os.Stderr, "RKNETUPDATE-RECOVERY normal-system reboot failed: %v\n", err)
			os.Exit(1)
		}
	}
}

func recoveryModeCount(cfg recoveryapply.Config, resumeRootfsV2, resumeBundleV1, resumeUpdateImageV1 bool) int {
	count := 0
	for _, selected := range []bool{cfg.Apply, cfg.Smoke, resumeRootfsV2, resumeBundleV1, resumeUpdateImageV1} {
		if selected {
			count++
		}
	}
	return count
}

var rebootNormalSystem = func() error {
	syscall.Sync()
	return syscall.Reboot(syscall.LINUX_REBOOT_CMD_RESTART)
}

func shouldAutoReboot(testMode, stayInRecovery, requested bool, status any) bool {
	return requested && !testMode && !stayInRecovery && recoverySucceeded(status)
}

func recoverySucceeded(status any) bool {
	switch result := status.(type) {
	case recoveryapply.Status:
		return result.Success
	case recoveryapply.MigrationStatus:
		return result.Success
	case recoveryapply.BundleMigrationStatus:
		return result.Success
	case recoveryapply.UpdateImageMigrationStatus:
		return result.Success
	default:
		return false
	}
}

type recoveryProgressPrinter struct {
	workflow string
	last     map[string]int
}

func newRecoveryProgressPrinter(workflow string) *recoveryProgressPrinter {
	return &recoveryProgressPrinter{workflow: workflow, last: make(map[string]int)}
}

func (p *recoveryProgressPrinter) milestone(phase, detail string) {
	fmt.Fprintf(os.Stderr, "RKNETUPDATE-RECOVERY %s phase=%s: %s\n", p.workflow, phase, detail)
}

func (p *recoveryProgressPrinter) write(phase string, written, total int64) {
	if total <= 0 {
		return
	}
	percent := int((written * 100) / total)
	if percent > 100 {
		percent = 100
	}
	last, seen := p.last[phase]
	if seen && percent < 100 && percent < last+5 {
		return
	}
	p.last[phase] = percent
	filled := percent / 5
	bar := strings.Repeat("#", filled) + strings.Repeat(".", 20-filled)
	fmt.Fprintf(os.Stderr, "RKNETUPDATE-RECOVERY %s phase=%s progress=%d%% [%s] bytes=%d/%d\n", p.workflow, phase, percent, bar, written, total)
}
