package recoveryapply

import (
	"encoding/binary"
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"rknetupdate/internal/rkimage"
)

func TestUpdateImageManifestRoundTripAndTamperDetection(t *testing.T) {
	dir := t.TempDir()
	imagePath := filepath.Join(dir, UpdateImageFileName)
	if err := os.WriteFile(imagePath, updateImageFixture(t), 0600); err != nil {
		t.Fatal(err)
	}
	manifest, err := BuildUpdateImageManifest(imagePath, "/dev/mmcblk0")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := manifest.Item("recovery"); err != nil {
		t.Fatal(err)
	}
	encoded, err := json.Marshal(manifest)
	if err != nil {
		t.Fatal(err)
	}
	manifestPath := filepath.Join(dir, UpdateImageManifestName)
	if err := os.WriteFile(manifestPath, encoded, 0600); err != nil {
		t.Fatal(err)
	}
	if _, gotPath, err := ValidateUpdateImageStage(manifestPath, true); err != nil || gotPath != imagePath {
		t.Fatalf("validate staged manifest: path=%s err=%v", gotPath, err)
	}
	f, err := os.OpenFile(imagePath, os.O_RDWR, 0)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := f.WriteAt([]byte{0xff}, int64(manifest.Items[0].Offset)); err != nil {
		_ = f.Close()
		t.Fatal(err)
	}
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}
	if _, _, err := ValidateUpdateImageStage(manifestPath, true); err == nil {
		t.Fatal("expected staged package tamper rejection")
	}
}

func updateImageFixture(t *testing.T) []byte {
	t.Helper()
	const (
		imageHeaderSize = 102
		firmwareSize    = 140 + 16*112
	)
	payloads := [][]byte{
		[]byte("parameter fixture\nCMDLINE: mtdparts=:-@0x00078000(rootfs:grow)\n"),
		[]byte("recovery fixture"),
		[]byte("rootfs fixture"),
	}
	names := []string{"parameter", "recovery", "rootfs"}
	total := imageHeaderSize + firmwareSize
	for _, payload := range payloads {
		total += len(payload)
	}
	image := make([]byte, total)
	copy(image[:4], rkimage.ContainerMagic[:])
	binary.LittleEndian.PutUint32(image[33:37], imageHeaderSize)
	binary.LittleEndian.PutUint32(image[37:41], uint32(total-imageHeaderSize))
	firmware := image[imageHeaderSize:]
	copy(firmware[:4], []byte("RKAF"))
	binary.LittleEndian.PutUint32(firmware[136:140], uint32(len(names)))
	offset := firmwareSize
	for index, payload := range payloads {
		entry := firmware[140+index*112 : 140+(index+1)*112]
		copy(entry[:32], names[index])
		binary.LittleEndian.PutUint32(entry[96:100], uint32(offset))
		binary.LittleEndian.PutUint32(entry[100:104], uint32(index+1))
		binary.LittleEndian.PutUint32(entry[108:112], uint32(len(payload)))
		copy(firmware[offset:offset+len(payload)], payload)
		offset += len(payload)
	}
	return image
}
