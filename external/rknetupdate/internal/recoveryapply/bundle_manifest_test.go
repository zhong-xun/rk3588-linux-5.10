package recoveryapply

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestBundleManifestValidationAndHash(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, BundleManifestName)
	manifest := testBundleManifest()
	data, err := json.Marshal(manifest)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, append(data, '\n'), 0600); err != nil {
		t.Fatal(err)
	}
	got, err := LoadBundleManifest(path, true)
	if err != nil {
		t.Fatal(err)
	}
	if len(got.Artifacts) != 3 || got.Artifacts[0].Target != "rootfs" || got.Artifacts[2].Target != "uboot" {
		t.Fatalf("unexpected manifest: %+v", got)
	}
	hash, size, err := HashBundleManifest(path, true)
	if err != nil || len(hash) != 64 || size != int64(len(data)+1) {
		t.Fatalf("hash=%q size=%d err=%v", hash, size, err)
	}
	if total, err := got.TotalSize(); err != nil || total != 30<<20 {
		t.Fatalf("total=%d err=%v", total, err)
	}
}

func TestBundleManifestRejectsUnsafeOrUnorderedArtifacts(t *testing.T) {
	manifest := testBundleManifest()
	manifest.Artifacts[0], manifest.Artifacts[1] = manifest.Artifacts[1], manifest.Artifacts[0]
	if err := manifest.Validate(); err == nil || !strings.Contains(err.Error(), "canonical") {
		t.Fatalf("unordered manifest error = %v", err)
	}
	manifest = testBundleManifest()
	manifest.Artifacts[1].Image = "../boot.img"
	if err := manifest.Validate(); err == nil || !strings.Contains(err.Error(), "must use image") {
		t.Fatalf("unsafe filename error = %v", err)
	}
	manifest = testBundleManifest()
	manifest.Artifacts = manifest.Artifacts[1:]
	if err := manifest.Validate(); err == nil || !strings.Contains(err.Error(), "rootfs") {
		t.Fatalf("no-rootfs error = %v", err)
	}
}

func testBundleManifest() BundleManifest {
	return BundleManifest{
		Schema:         BundleSchema,
		ExpectedParent: ProductionExpectedParentPath,
		Artifacts: []BundleArtifact{
			{Target: "rootfs", Image: BundleRootfsImage, Size: 16 << 20, SHA256: strings.Repeat("a", 64), Filesystem: "ext4", FilesystemUUID: "d5bed891-f45e-42e6-9f84-60152589ffec", BlockSize: 4096, BlockCount: 4096},
			{Target: "boot", Image: BundleBootImage, Size: 8 << 20, SHA256: strings.Repeat("b", 64)},
			{Target: "uboot", Image: BundleUbootImage, Size: 6 << 20, SHA256: strings.Repeat("c", 64)},
		},
	}
}
