package main

import (
	"testing"

	"rknetupdate/internal/recoveryapply"
)

func TestShouldAutoReboot(t *testing.T) {
	success := recoveryapply.MigrationStatus{Success: true}
	if !shouldAutoReboot(false, false, true, success) {
		t.Fatal("successful production recovery did not request reboot")
	}
	if shouldAutoReboot(true, false, true, success) {
		t.Fatal("test-mode recovery requested reboot")
	}
	if shouldAutoReboot(false, true, true, success) {
		t.Fatal("stay-in-recovery operation requested reboot")
	}
	if shouldAutoReboot(false, false, false, success) {
		t.Fatal("non-mutating recovery check requested reboot")
	}
	if shouldAutoReboot(false, false, true, recoveryapply.MigrationStatus{}) {
		t.Fatal("failed recovery requested reboot")
	}
}

func TestRecoveryModeCount(t *testing.T) {
	if got := recoveryModeCount(recoveryapply.Config{}, false, false, false); got != 0 {
		t.Fatalf("mode count = %d, want 0", got)
	}
	cfg := recoveryapply.Config{Apply: true}
	if got := recoveryModeCount(cfg, false, true, false); got != 2 {
		t.Fatalf("mode count = %d, want 2", got)
	}
	cfg = recoveryapply.Config{Smoke: true}
	if got := recoveryModeCount(cfg, false, false, false); got != 1 {
		t.Fatalf("mode count = %d, want 1", got)
	}
}

func TestRecoveryProgressPrinterThrottlesWrites(t *testing.T) {
	printer := newRecoveryProgressPrinter("test")
	printer.write("rootfs-writing", 1, 100)
	printer.write("rootfs-writing", 2, 100)
	if printer.last["rootfs-writing"] != 1 {
		t.Fatalf("last progress = %d, want 1", printer.last["rootfs-writing"])
	}
	printer.write("rootfs-writing", 100, 100)
	if printer.last["rootfs-writing"] != 100 {
		t.Fatalf("completed progress = %d, want 100", printer.last["rootfs-writing"])
	}
}
