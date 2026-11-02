package smokecontrol

import (
	"bytes"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

const (
	testBCBOffset          = 16 * 1024
	testBCBSize            = 1088
	testBCBSystemFlagStart = 836
)

func TestPrepareAndVerifyPreserveSystemFlag(t *testing.T) {
	cfg, miscPath := testConfig(t)
	before := readBCB(t, miscPath)
	result, err := Prepare(cfg)
	if err != nil {
		t.Fatalf("prepare: %v", err)
	}
	if !result.Success || result.RebootRequested {
		t.Fatalf("unexpected prepare result: %+v", result)
	}
	after := readBCB(t, miscPath)
	if !bytes.Equal(after[testBCBSystemFlagStart:], before[testBCBSystemFlagStart:]) {
		t.Fatal("systemFlag changed while preparing smoke request")
	}

	cfg.Confirmation = RebootConfirmation
	verified, err := VerifyPrepared(cfg)
	if err != nil {
		t.Fatalf("verify prepared: %v", err)
	}
	if !verified.Success || !verified.RebootRequested {
		t.Fatalf("unexpected verify result: %+v", verified)
	}
}

func TestPrepareRejectsNonblankCommandWithoutWrites(t *testing.T) {
	cfg, miscPath := testConfig(t)
	bcb := readBCB(t, miscPath)
	copy(bcb[:32], "other-command")
	writeBCB(t, miscPath, bcb)
	before := readBCB(t, miscPath)
	if _, err := Prepare(cfg); err == nil || !strings.Contains(err.Error(), "region is not empty") {
		t.Fatalf("expected nonblank command rejection, got %v", err)
	}
	if got := readBCB(t, miscPath); !bytes.Equal(got, before) {
		t.Fatal("BCB changed after nonblank command rejection")
	}
}

func TestPrepareRejectsStaleBCBFieldsWithBlankCommandWithoutWrites(t *testing.T) {
	tests := []struct {
		name   string
		offset int
	}{
		{name: "status", offset: 32},
		{name: "recovery", offset: 64},
		{name: "needupdate", offset: 832},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			cfg, miscPath := testConfig(t)
			bcb := readBCB(t, miscPath)
			bcb[test.offset] = 1
			writeBCB(t, miscPath, bcb)
			before := readBCB(t, miscPath)
			if _, err := Prepare(cfg); err == nil || !strings.Contains(err.Error(), "region is not empty") {
				t.Fatalf("expected stale %s rejection, got %v", test.name, err)
			}
			if got := readBCB(t, miscPath); !bytes.Equal(got, before) {
				t.Fatalf("BCB changed after stale %s rejection", test.name)
			}
		})
	}
}

func TestPrepareRequiresExactConfirmationWithoutWrites(t *testing.T) {
	cfg, miscPath := testConfig(t)
	cfg.Confirmation = "PREPARE RKNETUPDATE SMOKE"
	before := readBCB(t, miscPath)
	if _, err := Prepare(cfg); err == nil || !strings.Contains(err.Error(), "confirmation phrase") {
		t.Fatalf("expected confirmation rejection, got %v", err)
	}
	if got := readBCB(t, miscPath); !bytes.Equal(got, before) {
		t.Fatal("BCB changed after confirmation rejection")
	}
}

func TestProductionPathsAreFixed(t *testing.T) {
	cfg := DefaultConfig()
	cfg.MiscPath = "/dev/mmcblk0p2"
	if err := cfg.Validate(); err == nil || !strings.Contains(err.Error(), "production misc path is fixed") {
		t.Fatalf("expected fixed misc path rejection, got %v", err)
	}
	cfg = DefaultConfig()
	cfg.ExpectedParentPath = "/dev/nvme0n1"
	if err := cfg.Validate(); err == nil || !strings.Contains(err.Error(), "production expected parent path is fixed") {
		t.Fatalf("expected fixed parent rejection, got %v", err)
	}
}

func testConfig(t *testing.T) (Config, string) {
	t.Helper()
	dir := t.TempDir()
	miscPath := filepath.Join(dir, "misc.partition")
	f, err := os.OpenFile(miscPath, os.O_CREATE|os.O_RDWR|os.O_TRUNC, 0600)
	if err != nil {
		t.Fatal(err)
	}
	if err := f.Truncate(64 << 10); err != nil {
		_ = f.Close()
		t.Fatal(err)
	}
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}
	bcb := make([]byte, testBCBSize)
	for i := testBCBSystemFlagStart; i < len(bcb); i++ {
		bcb[i] = byte((i * 19) & 0xff)
	}
	writeBCB(t, miscPath, bcb)
	return Config{
		MiscPath:           miscPath,
		ExpectedParentPath: filepath.Join(dir, "emmc.disk"),
		Confirmation:       PrepareConfirmation,
		TestMode:           true,
	}, miscPath
}

func readBCB(t *testing.T, path string) []byte {
	t.Helper()
	f, err := os.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	buffer := make([]byte, testBCBSize)
	if _, err := f.ReadAt(buffer, testBCBOffset); err != nil {
		t.Fatal(err)
	}
	return buffer
}

func writeBCB(t *testing.T, path string, bcb []byte) {
	t.Helper()
	f, err := os.OpenFile(path, os.O_RDWR, 0)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	if _, err := f.WriteAt(bcb, testBCBOffset); err != nil {
		t.Fatal(err)
	}
}
