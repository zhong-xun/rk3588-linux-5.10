package recoveryapply

import (
	"bytes"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestRootfsRecoveryArgumentCanonicalRoundTrip(t *testing.T) {
	request := testRootfsRequest()
	argument, err := request.RecoveryArgument()
	if err != nil {
		t.Fatal(err)
	}
	parsed, err := ParseRootfsRecoveryArgument(argument)
	if err != nil {
		t.Fatal(err)
	}
	if parsed != request {
		t.Fatalf("parsed request = %+v, want %+v", parsed, request)
	}
	if len(argument) >= bcbRecoveryEnd-bcbRecoveryStart {
		t.Fatalf("argument length %d exceeds recovery field", len(argument))
	}
}

func TestRootfsRecoveryArgumentRejectsNonCanonicalFields(t *testing.T) {
	argument, err := testRootfsRequest().RecoveryArgument()
	if err != nil {
		t.Fatal(err)
	}
	cases := []string{
		strings.Replace(argument, "size=33554432", "bogus=33554432", 1),
		strings.Replace(argument, "p6_name=rootfs", "p6_name=rootfs ", 1),
		strings.Replace(argument, "block_count=8192", "block_count=8192\nextra=x", 1),
	}
	for _, value := range cases {
		if _, err := ParseRootfsRecoveryArgument(value); err == nil {
			t.Fatalf("accepted malformed argument %q", value)
		}
	}
}

func TestRootfsBCBPhaseUpdateAndClearPreservesSystemFlag(t *testing.T) {
	device := testRootfsBCBDevice(t)
	request := testRootfsRequest()
	if err := PrepareRootfsBCB(device, request); err != nil {
		t.Fatal(err)
	}
	parsed, phase, err := ReadRootfsBCB(device)
	if err != nil {
		t.Fatal(err)
	}
	if parsed != request || phase != RootfsPhasePrepared {
		t.Fatalf("prepared BCB = %+v phase=%q", parsed, phase)
	}
	if err := UpdateRootfsBCBPhase(device, request, RootfsPhaseP6Verified); err != nil {
		t.Fatal(err)
	}
	beforeClear := readRootfsBCBBytes(t, device.Path)
	if err := ClearRootfsBCB(device, request); err == nil {
		t.Fatal("cleared incomplete rootfs phase")
	}
	if err := UpdateRootfsBCBPhase(device, request, RootfsPhaseExpanded); err != nil {
		t.Fatal(err)
	}
	if err := ClearRootfsBCB(device, request); err != nil {
		t.Fatal(err)
	}
	afterClear := readRootfsBCBBytes(t, device.Path)
	if !bytes.Equal(beforeClear[bcbSystemFlagStart:], afterClear[bcbSystemFlagStart:]) {
		t.Fatal("systemFlag changed while clearing rootfs BCB")
	}
	if !allZero(afterClear[:bcbNeedUpdateEnd]) {
		t.Fatal("rootfs BCB request fields were not cleared")
	}
}

func TestRootfsBCBPrepareRejectsStaleFields(t *testing.T) {
	device := testRootfsBCBDevice(t)
	request := testRootfsRequest()
	path := device.Path
	f, err := os.OpenFile(path, os.O_RDWR, 0)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := f.WriteAt([]byte{1}, bcbOffset+32); err != nil {
		_ = f.Close()
		t.Fatal(err)
	}
	_ = f.Close()
	before := readRootfsBCBBytes(t, path)
	if err := PrepareRootfsBCB(device, request); err == nil {
		t.Fatal("prepared rootfs BCB over stale status")
	}
	if after := readRootfsBCBBytes(t, path); !bytes.Equal(after, before) {
		t.Fatal("stale BCB rejection changed bytes")
	}
}

func TestRootfsBCBPhaseIsMonotonicAndIdempotent(t *testing.T) {
	device := testRootfsBCBDevice(t)
	request := testRootfsRequest()
	if err := PrepareRootfsBCB(device, request); err != nil {
		t.Fatal(err)
	}
	if err := UpdateRootfsBCBPhase(device, request, RootfsPhaseStaged); err != nil {
		t.Fatal(err)
	}
	before := readRootfsBCBBytes(t, device.Path)
	if err := UpdateRootfsBCBPhase(device, request, RootfsPhaseStaged); err != nil {
		t.Fatalf("idempotent update failed: %v", err)
	}
	if after := readRootfsBCBBytes(t, device.Path); !bytes.Equal(after, before) {
		t.Fatal("idempotent phase update rewrote BCB")
	}
	if err := UpdateRootfsBCBPhase(device, request, RootfsPhaseShrunk); err == nil {
		t.Fatal("accepted phase regression")
	}
	if after := readRootfsBCBBytes(t, device.Path); !bytes.Equal(after, before) {
		t.Fatal("phase regression rejection changed BCB")
	}
	if err := UpdateRootfsBCBPhase(device, request, RootfsPhaseExpanded); err != nil {
		t.Fatalf("forward phase jump failed: %v", err)
	}
}

func testRootfsRequest() RootfsBCBRequest {
	return RootfsBCBRequest{
		Schema:         RootfsRequestSchema,
		Size:           32 << 20,
		SHA256:         strings.Repeat("a", 64),
		FilesystemUUID: "01234567-89ab-cdef-0123-456789abcdef",
		BlockSize:      4096,
		BlockCount:     8192,
		P6Start:        491520,
		P6TypeGUID:     "0fc63daf-8483-4772-8e79-3d69d8477de4",
		P6UniqueGUID:   "01234567-89ab-cdef-0123-456789abcdef",
		P6Name:         "rootfs",
	}
}

func testRootfsBCBDevice(t *testing.T) Device {
	t.Helper()
	dir := t.TempDir()
	path := filepath.Join(dir, "misc")
	parent := filepath.Join(dir, "emmc")
	if err := os.WriteFile(parent, nil, 0600); err != nil {
		t.Fatal(err)
	}
	f, err := os.OpenFile(path, os.O_CREATE|os.O_RDWR|os.O_TRUNC, 0600)
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
	device, err := ResolveDevice(path, "misc", parent, true)
	if err != nil {
		t.Fatal(err)
	}
	buffer := make([]byte, bcbSize)
	for i := bcbSystemFlagStart; i < len(buffer); i++ {
		buffer[i] = byte(i * 17)
	}
	f, err = os.OpenFile(path, os.O_RDWR, 0)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := f.WriteAt(buffer, bcbOffset); err != nil {
		_ = f.Close()
		t.Fatal(err)
	}
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}
	return device
}

func readRootfsBCBBytes(t *testing.T, path string) []byte {
	t.Helper()
	f, err := os.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	buffer := make([]byte, bcbSize)
	if _, err := f.ReadAt(buffer, bcbOffset); err != nil {
		t.Fatal(err)
	}
	return buffer
}
