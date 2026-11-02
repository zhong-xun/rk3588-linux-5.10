package recoveryapply

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

func TestRootfsManifestV2RejectsUnknownField(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, migrationManifestName)
	data := `{"schema":2,"image":"rootfs.img","size":4096,"sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","filesystem":"ext4","filesystem_uuid":"01234567-89ab-cdef-0123-456789abcdef","block_size":4096,"block_count":1,"target_partlabel":"rootfs","expected_parent":"/dev/mmcblk0","unknown":true}`
	if err := os.WriteFile(path, []byte(data), 0600); err != nil {
		t.Fatal(err)
	}
	if _, _, err := LoadRootfsManifestV2(path, true); err == nil {
		t.Fatal("accepted unknown manifest field")
	}
}

func TestRootfsManifestV2RejectsFilesystemGeometryMismatch(t *testing.T) {
	mkfs, err := exec.LookPath("mkfs.ext4")
	if err != nil {
		t.Skip("mkfs.ext4 unavailable")
	}
	e2fsck, err := exec.LookPath("e2fsck")
	if err != nil {
		t.Skip("e2fsck unavailable")
	}
	dir := t.TempDir()
	imagePath := filepath.Join(dir, migrationImageName)
	f, err := os.Create(imagePath)
	if err != nil {
		t.Fatal(err)
	}
	if err := f.Truncate(16 << 20); err != nil {
		t.Fatal(err)
	}
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}
	if output, err := exec.Command(mkfs, "-q", "-F", imagePath).CombinedOutput(); err != nil {
		t.Fatalf("mkfs.ext4: %v: %s", err, output)
	}
	f, err = os.Open(imagePath)
	if err != nil {
		t.Fatal(err)
	}
	info, err := InspectExt4(f, 16<<20)
	_ = f.Close()
	if err != nil {
		t.Fatal(err)
	}
	image, err := os.ReadFile(imagePath)
	if err != nil {
		t.Fatal(err)
	}
	sum := sha256.Sum256(image)
	manifest := fmt.Sprintf(`{"schema":2,"image":"rootfs.img","size":%d,"sha256":"%s","filesystem":"ext4","filesystem_uuid":"%s","block_size":%d,"block_count":%d,"target_partlabel":"rootfs","expected_parent":"/dev/mmcblk0"}`,
		len(image), hex.EncodeToString(sum[:]), info.UUID, info.BlockSize, info.BlockCount-1)
	manifestPath := filepath.Join(dir, migrationManifestName)
	if err := os.WriteFile(manifestPath, []byte(manifest), 0600); err != nil {
		t.Fatal(err)
	}
	if _, _, _, err := ValidateRootfsImageV2(context.Background(), manifestPath, e2fsck, true); err == nil {
		t.Fatal("accepted mismatched ext4 geometry")
	}
}
