package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"syscall"

	"rknetupdate/internal/bundlecontrol"
	"rknetupdate/internal/rootfscontrol"
	"rknetupdate/internal/smokecontrol"
	"rknetupdate/internal/updateimgcontrol"
)

func main() {
	if len(os.Args) < 2 {
		usage()
		os.Exit(2)
	}
	action := os.Args[1]
	flags := flag.NewFlagSet("rknetupdate-control "+action, flag.ContinueOnError)
	flags.SetOutput(os.Stderr)
	confirmation := flags.String("confirm", "", "required exact confirmation phrase")
	allowWrite := flags.Bool("allow-write", false, "explicitly authorize BCB and recovery workflow changes")
	testMode := flags.Bool("test-mode", false, "allow loop devices and test paths")
	manifestPath := flags.String("manifest", rootfscontrol.ProductionManifest, "test-only rootfs v2 manifest path")
	diskPath := flags.String("disk", rootfscontrol.ProductionDisk, "test-only rootfs v2 disk path")
	rootfsPath := flags.String("rootfs", rootfscontrol.ProductionRootfs, "test-only rootfs partition path")
	miscPath := flags.String("misc", rootfscontrol.ProductionMisc, "test-only misc partition path")
	parentPath := flags.String("parent", rootfscontrol.ProductionParent, "test-only parent disk path")
	e2fsckPath := flags.String("e2fsck", rootfscontrol.ProductionE2fsck, "test-only e2fsck path")
	bundleManifestPath := flags.String("bundle-manifest", bundlecontrol.ProductionManifest, "test-only bundle manifest path")
	bundleDiskPath := flags.String("bundle-disk", bundlecontrol.ProductionDisk, "test-only bundle disk path")
	bundleRootfsPath := flags.String("bundle-rootfs", bundlecontrol.ProductionRootfs, "test-only bundle rootfs path")
	bundleBootPath := flags.String("bundle-boot", bundlecontrol.ProductionBoot, "test-only bundle boot path")
	bundleUbootPath := flags.String("bundle-uboot", bundlecontrol.ProductionUboot, "test-only bundle uboot path")
	bundleMiscPath := flags.String("bundle-misc", bundlecontrol.ProductionMisc, "test-only bundle misc path")
	bundleParentPath := flags.String("bundle-parent", bundlecontrol.ProductionParent, "test-only bundle parent disk")
	updateImageManifestPath := flags.String("updateimg-manifest", updateimgcontrol.ProductionManifest, "test-only update.img manifest path")
	updateImageDiskPath := flags.String("updateimg-disk", updateimgcontrol.ProductionDisk, "test-only update.img disk path")
	updateImageRootfsPath := flags.String("updateimg-rootfs", updateimgcontrol.ProductionRootfs, "test-only update.img rootfs partition path")
	updateImageRecoveryPath := flags.String("updateimg-recovery", updateimgcontrol.ProductionRecovery, "test-only update.img recovery partition path")
	updateImageMiscPath := flags.String("updateimg-misc", updateimgcontrol.ProductionMisc, "test-only update.img misc partition path")
	updateImageParentPath := flags.String("updateimg-parent", updateimgcontrol.ProductionParent, "test-only update.img parent disk")
	if err := flags.Parse(os.Args[2:]); err != nil {
		if errors.Is(err, flag.ErrHelp) {
			return
		}
		os.Exit(2)
	}
	if len(flags.Args()) != 0 {
		fmt.Fprintln(os.Stderr, "rknetupdate-control does not accept positional arguments")
		os.Exit(2)
	}

	var (
		result any
		err    error
	)
	switch action {
	case "prepare-smoke":
		if *allowWrite || *testMode {
			fmt.Fprintln(os.Stderr, "--allow-write and --test-mode are not valid for recovery smoke")
			os.Exit(2)
		}
		cfg := smokecontrol.DefaultConfig()
		cfg.Confirmation = *confirmation
		result, err = smokecontrol.Prepare(cfg)
	case "reboot-smoke":
		if *allowWrite || *testMode {
			fmt.Fprintln(os.Stderr, "--allow-write and --test-mode are not valid for recovery smoke")
			os.Exit(2)
		}
		cfg := smokecontrol.DefaultConfig()
		cfg.Confirmation = *confirmation
		result, err = smokecontrol.VerifyPrepared(cfg)
	case "prepare-rootfs":
		cfg := rootfscontrol.DefaultConfig()
		cfg.Confirmation = *confirmation
		cfg.AllowWrite = *allowWrite
		cfg.TestMode = *testMode
		cfg.ManifestPath = *manifestPath
		cfg.DiskPath = *diskPath
		cfg.RootfsPath = *rootfsPath
		cfg.MiscPath = *miscPath
		cfg.ParentPath = *parentPath
		cfg.E2fsckPath = *e2fsckPath
		result, err = rootfscontrol.Prepare(context.Background(), cfg)
	case "reboot-rootfs":
		cfg := rootfscontrol.DefaultConfig()
		cfg.Confirmation = *confirmation
		cfg.AllowWrite = *allowWrite
		cfg.TestMode = *testMode
		cfg.ManifestPath = *manifestPath
		cfg.DiskPath = *diskPath
		cfg.RootfsPath = *rootfsPath
		cfg.MiscPath = *miscPath
		cfg.ParentPath = *parentPath
		cfg.E2fsckPath = *e2fsckPath
		result, err = rootfscontrol.VerifyPrepared(context.Background(), cfg)
	case "prepare-bundle":
		cfg := bundlecontrol.DefaultConfig()
		cfg.Confirmation = *confirmation
		cfg.AllowWrite = *allowWrite
		cfg.TestMode = *testMode
		cfg.ManifestPath = *bundleManifestPath
		cfg.DiskPath = *bundleDiskPath
		cfg.RootfsPath = *bundleRootfsPath
		cfg.BootPath = *bundleBootPath
		cfg.UbootPath = *bundleUbootPath
		cfg.MiscPath = *bundleMiscPath
		cfg.ParentPath = *bundleParentPath
		result, err = bundlecontrol.Prepare(context.Background(), cfg)
	case "reboot-bundle":
		cfg := bundlecontrol.DefaultConfig()
		cfg.Confirmation = *confirmation
		cfg.AllowWrite = *allowWrite
		cfg.TestMode = *testMode
		cfg.ManifestPath = *bundleManifestPath
		cfg.DiskPath = *bundleDiskPath
		cfg.RootfsPath = *bundleRootfsPath
		cfg.BootPath = *bundleBootPath
		cfg.UbootPath = *bundleUbootPath
		cfg.MiscPath = *bundleMiscPath
		cfg.ParentPath = *bundleParentPath
		result, err = bundlecontrol.VerifyPrepared(context.Background(), cfg)
	case "prepare-updateimg":
		cfg := updateimgcontrol.DefaultConfig()
		cfg.Confirmation = *confirmation
		cfg.AllowWrite = *allowWrite
		cfg.TestMode = *testMode
		cfg.ManifestPath = *updateImageManifestPath
		cfg.DiskPath = *updateImageDiskPath
		cfg.RootfsPath = *updateImageRootfsPath
		cfg.RecoveryPath = *updateImageRecoveryPath
		cfg.MiscPath = *updateImageMiscPath
		cfg.ParentPath = *updateImageParentPath
		result, err = updateimgcontrol.Prepare(context.Background(), cfg)
	case "reboot-updateimg":
		cfg := updateimgcontrol.DefaultConfig()
		cfg.Confirmation = *confirmation
		cfg.AllowWrite = *allowWrite
		cfg.TestMode = *testMode
		cfg.ManifestPath = *updateImageManifestPath
		cfg.DiskPath = *updateImageDiskPath
		cfg.RootfsPath = *updateImageRootfsPath
		cfg.RecoveryPath = *updateImageRecoveryPath
		cfg.MiscPath = *updateImageMiscPath
		cfg.ParentPath = *updateImageParentPath
		result, err = updateimgcontrol.VerifyPrepared(context.Background(), cfg)
	default:
		usage()
		os.Exit(2)
	}
	if encodeErr := json.NewEncoder(os.Stdout).Encode(result); encodeErr != nil {
		fmt.Fprintf(os.Stderr, "encode result: %v\n", encodeErr)
		os.Exit(1)
	}
	if err != nil {
		fmt.Fprintf(os.Stderr, "rknetupdate-control: %v\n", err)
		os.Exit(1)
	}
	if action == "prepare-smoke" || action == "prepare-rootfs" || action == "prepare-bundle" || action == "prepare-updateimg" {
		return
	}

	_, err = smokecontrol.RequestCleanReboot(context.Background(), smokecontrol.RebootOperations{
		Stat: os.Stat,
		Sync: syscall.Sync,
		Run: func(ctx context.Context, path string, args ...string) error {
			command := exec.CommandContext(ctx, path, args...)
			command.Stdout = os.Stdout
			command.Stderr = os.Stderr
			return command.Run()
		},
	})
	if err != nil {
		fmt.Fprintf(os.Stderr, "rknetupdate-control: reboot: %v\n", err)
		os.Exit(1)
	}
}

func usage() {
	fmt.Fprintln(os.Stderr, "usage:")
	fmt.Fprintf(os.Stderr, "  rknetupdate-control prepare-smoke --confirm %q\n", smokecontrol.PrepareConfirmation)
	fmt.Fprintf(os.Stderr, "  rknetupdate-control reboot-smoke --confirm %q\n", smokecontrol.RebootConfirmation)
	fmt.Fprintln(os.Stderr, "  rknetupdate-control prepare-rootfs --allow-write --confirm PREPARE_ROOTFS_V2:<sha256>")
	fmt.Fprintln(os.Stderr, "  rknetupdate-control reboot-rootfs --allow-write --confirm REBOOT_ROOTFS_V2:<sha256>")
	fmt.Fprintln(os.Stderr, "  rknetupdate-control prepare-bundle --allow-write --confirm PREPARE_BUNDLE_V1:<manifest-sha256>")
	fmt.Fprintln(os.Stderr, "  rknetupdate-control reboot-bundle --allow-write --confirm REBOOT_BUNDLE_V1:<manifest-sha256>")
	fmt.Fprintln(os.Stderr, "  rknetupdate-control prepare-updateimg --allow-write --confirm PREPARE_UPDATEIMG_V1:<package-sha256>")
	fmt.Fprintln(os.Stderr, "  rknetupdate-control reboot-updateimg --allow-write --confirm REBOOT_UPDATEIMG_V1:<package-sha256>")
}
