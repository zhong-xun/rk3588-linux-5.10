package updateimgcontrol

import (
	"strings"
	"testing"

	"rknetupdate/internal/recoveryapply"
)

func TestValidateSupportedItemsForSingleSlotUpdateImage(t *testing.T) {
	valid := recoveryapply.UpdateImageManifest{Items: []recoveryapply.UpdateImageItem{
		{Name: "package-file"}, {Name: "bootloader"}, {Name: "parameter"},
		{Name: "uboot"}, {Name: "trust"}, {Name: "boot"},
		{Name: "recovery"}, {Name: "rootfs"}, {Name: "oem"}, {Name: "misc"},
	}}
	if err := validateSupportedItems(valid); err != nil {
		t.Fatalf("SDK-shaped single-slot package was rejected: %v", err)
	}

	for _, name := range []string{"userdata", "boot_a", "backup", "unknown"} {
		t.Run(name, func(t *testing.T) {
			err := validateSupportedItems(recoveryapply.UpdateImageManifest{Items: []recoveryapply.UpdateImageItem{{Name: name}}})
			if err == nil || !strings.Contains(err.Error(), name) {
				t.Fatalf("item %q error = %v, want an item-specific rejection", name, err)
			}
		})
	}
}

func TestValidatePackageTargetCapacity(t *testing.T) {
	manifest := recoveryapply.UpdateImageManifest{Items: []recoveryapply.UpdateImageItem{
		{Name: "rootfs", Size: 32}, {Name: "recovery", Size: 8},
	}}
	rootfs := recoveryapply.Device{Path: "/dev/test-rootfs", Size: 31}
	recovery := recoveryapply.Device{Path: "/dev/test-recovery", Size: 8}
	misc := recoveryapply.Device{Path: "/dev/test-misc", Size: 4}
	if err := validatePackageTargetCapacity(manifest, Config{}, rootfs, recovery, misc); err == nil || !strings.Contains(err.Error(), "rootfs") {
		t.Fatalf("rootfs capacity error = %v, want a rootfs rejection", err)
	}

	rootfs.Size = 32
	if err := validatePackageTargetCapacity(manifest, Config{}, rootfs, recovery, misc); err != nil {
		t.Fatalf("exactly fitting package was rejected: %v", err)
	}
}

func TestValidateParameterTextRequiresExactSingleSlotLayout(t *testing.T) {
	table := recoveryapply.GPTTable{
		LastUsable: 0xfffff,
		Partitions: map[int]recoveryapply.GPTPartition{
			1: {Number: 1, Name: "uboot", FirstLBA: 0x4000, LastLBA: 0x5fff},
			2: {Number: 2, Name: "misc", FirstLBA: 0x6000, LastLBA: 0x7fff},
			3: {Number: 3, Name: "boot", FirstLBA: 0x8000, LastLBA: 0x27fff, UniqueGUID: "7a3f0000-0000-446a-8000-702f00006273"},
			4: {Number: 4, Name: "recovery", FirstLBA: 0x28000, LastLBA: 0x67fff},
			5: {Number: 5, Name: "backup", FirstLBA: 0x68000, LastLBA: 0x77fff},
			6: {Number: 6, Name: "rootfs", FirstLBA: 0x78000, LastLBA: 0xfffff, UniqueGUID: "614e0000-0000-4b53-8000-1d28000054a9"},
		},
	}
	text := "TYPE: GPT\nCMDLINE: mtdparts=:0x00002000@0x00004000(uboot),0x00002000@0x00006000(misc),0x00020000@0x00008000(boot),0x00040000@0x00028000(recovery),0x00010000@0x00068000(backup),-@0x00078000(rootfs:grow)\nuuid:rootfs=614e0000-0000-4b53-8000-1d28000054a9\nuuid:boot=7A3F0000-0000-446A-8000-702F00006273\n"
	if err := validateParameterText(text, table); err != nil {
		t.Fatalf("matching parameter was rejected: %v", err)
	}
	if err := validateParameterText(strings.Replace(text, "0x00020000@0x00008000(boot)", "0x0001f000@0x00008000(boot)", 1), table); err == nil || !strings.Contains(err.Error(), "boot size") {
		t.Fatalf("mismatched boot parameter error = %v, want boot size rejection", err)
	}
	if err := validateParameterText(strings.Replace(text, "rootfs:grow", "userdata", 1), table); err == nil || !strings.Contains(err.Error(), "userdata") {
		t.Fatalf("userdata parameter error = %v, want userdata rejection", err)
	}
}
