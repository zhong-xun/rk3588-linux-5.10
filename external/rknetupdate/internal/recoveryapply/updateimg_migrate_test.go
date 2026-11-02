package recoveryapply

import (
	"context"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestEnsureUpdateImageStagedFormatsRawP7BeforeMount(t *testing.T) {
	dir := t.TempDir()
	userdataPath := filepath.Join(dir, "userdata.img")
	if err := os.WriteFile(userdataPath, nil, 0600); err != nil {
		t.Fatal(err)
	}
	mountInfoPath := filepath.Join(dir, "mountinfo")
	if err := os.WriteFile(mountInfoPath, nil, 0600); err != nil {
		t.Fatal(err)
	}
	mkfsPath := filepath.Join(dir, "mkfs.ext4")
	if err := os.WriteFile(mkfsPath, []byte("#!/bin/sh\n: > \"$0.ran\"\n"), 0700); err != nil {
		t.Fatal(err)
	}

	cfg := UpdateImageMigrationConfig{
		TestMode:           true,
		UserdataPath:       userdataPath,
		UserdataMountPath:  filepath.Join(dir, "userdata"),
		MountInfoPath:      mountInfoPath,
		ExpectedParentPath: filepath.Join(dir, "disk"),
		MkfsExt4Path:       mkfsPath,
	}
	err := ensureUpdateImageStaged(context.Background(), cfg, Device{}, UpdateImageBCBRequest{}, UpdateImagePhaseP7Ready)
	if err == nil || !strings.Contains(err.Error(), "mount ") {
		t.Fatalf("expected raw temporary p7 mount to fail after formatting, got %v", err)
	}
	if _, err := os.Stat(mkfsPath + ".ran"); err != nil {
		t.Fatalf("mkfs.ext4 was not run before the first mount attempt: %v", err)
	}
}

func TestUpdateImageMigrationStopAfterIsTestOnly(t *testing.T) {
	cfg := DefaultUpdateImageMigrationConfig()
	cfg.StopAfterPhase = UpdateImagePhaseShrunk
	if err := cfg.Validate(); err == nil || !strings.Contains(err.Error(), "test-only") {
		t.Fatalf("production stop-after phase was not rejected: %v", err)
	}
	cfg.TestMode = true
	if err := cfg.Validate(); err != nil {
		t.Fatalf("test stop-after phase was rejected: %v", err)
	}
	cfg.StopAfterPhase = UpdateImagePhaseStaged
	if err := cfg.Validate(); err == nil || !strings.Contains(err.Error(), "invalid") {
		t.Fatalf("unsupported stop-after phase was not rejected: %v", err)
	}
}
