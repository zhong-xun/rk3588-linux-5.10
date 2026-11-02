package recoveryapply

import (
	"bytes"
	"encoding/binary"
	"os"
	"path/filepath"
	"testing"
)

func TestUpdateImageBCBPrepareAndVerify(t *testing.T) {
	device := testRootfsBCBDevice(t)
	before := readRootfsBCBBytes(t, device.Path)
	if err := PrepareUpdateImageBCB(device); err != nil {
		t.Fatal(err)
	}
	if err := VerifyUpdateImageBCB(device); err != nil {
		t.Fatal(err)
	}
	after := readRootfsBCBBytes(t, device.Path)
	if !bytes.Equal(before[bcbSystemFlagStart:], after[bcbSystemFlagStart:]) {
		t.Fatal("update.img BCB preparation changed systemFlag")
	}
	if !IsUpdateImageBCB(after) {
		t.Fatal("prepared BCB was not recognized as update.img request")
	}
}

func TestUpdateImageBCBRejectsStaleFields(t *testing.T) {
	device := testRootfsBCBDevice(t)
	f, err := os.OpenFile(device.Path, os.O_RDWR, 0)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := f.WriteAt([]byte{1}, bcbOffset+bcbNeedUpdateStart); err != nil {
		_ = f.Close()
		t.Fatal(err)
	}
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}
	before := readRootfsBCBBytes(t, device.Path)
	if err := PrepareUpdateImageBCB(device); err == nil {
		t.Fatal("prepared over stale BCB")
	}
	if after := readRootfsBCBBytes(t, device.Path); !bytes.Equal(before, after) {
		t.Fatal("rejected preparation changed BCB")
	}
}

func TestUpdateImageMigrationBCBFullHandoff(t *testing.T) {
	device := testRootfsBCBDevice(t)
	request := UpdateImageBCBRequest{
		Schema:         UpdateImageSchema,
		ManifestSHA256: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
		PackageSize:    8 << 20,
		PackageSHA256:  "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
		P6Start:        0x78000,
		P6TypeGUID:     "0fc63daf-8483-4772-8e79-3d69d8477de4",
		P6UniqueGUID:   "614e0000-0000-4b53-8000-1d28000054a9",
		P6Name:         "rootfs",
	}
	before := readRootfsBCBBytes(t, device.Path)
	if err := PrepareUpdateImageMigrationBCB(device, request); err != nil {
		t.Fatal(err)
	}
	if got, phase, err := ReadUpdateImageMigrationBCB(device); err != nil || got != request || phase != UpdateImagePhasePrepared {
		t.Fatalf("unexpected prepared BCB: request=%+v phase=%s err=%v", got, phase, err)
	}
	if err := UpdateUpdateImageBCBPhase(device, request, UpdateImagePhaseP7Ready); err != nil {
		t.Fatal(err)
	}
	if err := UpdateUpdateImageBCBPhase(device, request, UpdateImagePhaseStaged); err != nil {
		t.Fatal(err)
	}
	if err := HandoffUpdateImageToStockBCB(device, request); err != nil {
		t.Fatal(err)
	}
	afterHandoff := readRootfsBCBBytes(t, device.Path)
	if !IsStockUpdateImageBCB(afterHandoff) {
		t.Fatal("stock update.img BCB was not recognized")
	}
	expected := make([]byte, bcbNeedUpdateEnd)
	copy(expected[bcbCommandStart:bcbCommandEnd], updateImageBCBCommand)
	copy(expected[bcbRecoveryStart:bcbRecoveryEnd], UpdateImageRecoveryArgument)
	binary.LittleEndian.PutUint32(expected[bcbNeedUpdateStart:bcbNeedUpdateEnd], UpdateImageStockMask)
	if !bytes.Equal(afterHandoff[:bcbNeedUpdateEnd], expected) {
		t.Fatal("stock update.img BCB command fields are not canonical")
	}
	if !bytes.Equal(before[bcbSystemFlagStart:], afterHandoff[bcbSystemFlagStart:]) {
		t.Fatal("migration changed BCB systemFlag")
	}
}

func TestValidateUpdateImageMiscItemAcceptsPackageBCBContent(t *testing.T) {
	dir := t.TempDir()
	imagePath := filepath.Join(dir, "update.img")
	data := make([]byte, bcbOffset+bcbNeedUpdateEnd)
	if err := os.WriteFile(imagePath, data, 0600); err != nil {
		t.Fatal(err)
	}
	item := UpdateImageItem{Name: "misc", Offset: 0, Size: uint64(len(data))}
	if err := ValidateUpdateImageMiscItem(imagePath, item); err != nil {
		t.Fatalf("zeroed misc item was rejected: %v", err)
	}
	data[bcbOffset+bcbRecoveryStart] = 'x'
	if err := os.WriteFile(imagePath, data, 0600); err != nil {
		t.Fatal(err)
	}
	if err := ValidateUpdateImageMiscItem(imagePath, item); err != nil {
		t.Fatalf("misc item with package BCB content was rejected: %v", err)
	}
}
