package recoveryapply

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

func TestProductionConfigIsFixed(t *testing.T) {
	cfg := DefaultConfig()
	if err := cfg.Validate(); err != nil {
		t.Fatalf("default config: %v", err)
	}
	cfg.TargetPath = "/dev/mmcblk0p6"
	if err := cfg.Validate(); err == nil || !strings.Contains(err.Error(), "production target path is fixed") {
		t.Fatalf("expected fixed target rejection, got %v", err)
	}
}

func TestApplyRequiresAllOptIns(t *testing.T) {
	cfg := DefaultConfig()
	cfg.Apply = true
	if err := cfg.Validate(); err == nil || !strings.Contains(err.Error(), "--allow-write") {
		t.Fatalf("expected allow-write error, got %v", err)
	}
	cfg.AllowWrite = true
	if err := cfg.Validate(); err == nil || !strings.Contains(err.Error(), "--confirm") {
		t.Fatalf("expected confirm error, got %v", err)
	}
}

func TestManifestRejectsUnknownFieldsAndUppercaseHash(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "rootfs.manifest.json")
	base := `{"schema":1,"image":"rootfs.img","size":4096,"sha256":"%s","filesystem":"ext4","target_partlabel":"rootfs","expected_parent":"/tmp/disk"%s}`
	upperHash := strings.Repeat("A", 64)
	if err := os.WriteFile(path, []byte(fmt.Sprintf(base, upperHash, "")), 0600); err != nil {
		t.Fatal(err)
	}
	if _, _, err := LoadManifest(path, true); err == nil || !strings.Contains(err.Error(), "lowercase") {
		t.Fatalf("expected lowercase hash rejection, got %v", err)
	}
	lowerHash := strings.Repeat("a", 64)
	if err := os.WriteFile(path, []byte(fmt.Sprintf(base, lowerHash, `,"extra":true`)), 0600); err != nil {
		t.Fatal(err)
	}
	if _, _, err := LoadManifest(path, true); err == nil || !strings.Contains(err.Error(), "unknown field") {
		t.Fatalf("expected unknown field rejection, got %v", err)
	}
}

func TestInspectExt4(t *testing.T) {
	path := filepath.Join(t.TempDir(), "rootfs.img")
	image := make([]byte, 8<<20)
	super := image[ext4SuperblockOffset : ext4SuperblockOffset+ext4SuperblockSize]
	binary.LittleEndian.PutUint32(super[0x04:0x08], 2048)
	binary.LittleEndian.PutUint32(super[0x18:0x1c], 2)
	binary.LittleEndian.PutUint16(super[0x38:0x3a], ext4Magic)
	if err := os.WriteFile(path, image, 0600); err != nil {
		t.Fatal(err)
	}
	f, err := os.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	info, err := InspectExt4(f, int64(len(image)))
	if err != nil {
		t.Fatalf("inspect ext4: %v", err)
	}
	if info.BlockSize != 4096 || info.BlockCount != 2048 || info.FilesystemSize != uint64(len(image)) {
		t.Fatalf("unexpected ext4 info: %+v", info)
	}
}

func TestMountInfoUsesDeviceNumberAndEscapedSource(t *testing.T) {
	path := filepath.Join(t.TempDir(), "mountinfo")
	line := `36 25 179:6 / /old\040root rw,relatime - ext4 /dev/mmcblk0p6 rw` + "\n"
	if err := os.WriteFile(path, []byte(line), 0600); err != nil {
		t.Fatal(err)
	}
	records, err := ReadMountInfo(path)
	if err != nil {
		t.Fatal(err)
	}
	device := Device{Path: "/dev/other-name", Major: 179, Minor: 6}
	mounted, mountPoint := IsDeviceMounted(device, records)
	if !mounted || mountPoint != "/old root" {
		t.Fatalf("expected mounted device, got mounted=%v point=%q", mounted, mountPoint)
	}
}

func TestRegularFileApplyAndBCBClear(t *testing.T) {
	requireCommand(t, "/usr/sbin/mkfs.ext4")
	requireCommand(t, "/usr/sbin/e2fsck")
	dir := t.TempDir()
	imagePath := filepath.Join(dir, "rootfs.img")
	targetPath := filepath.Join(dir, "rootfs.partition")
	miscPath := filepath.Join(dir, "misc.partition")
	parentPath := filepath.Join(dir, "emmc.disk")
	statusPath := filepath.Join(dir, "rootfs.status.json")
	manifestPath := filepath.Join(dir, "rootfs.manifest.json")
	requestPath := filepath.Join(dir, "rootfs.apply")
	markerPath := filepath.Join(dir, "recovery-marker")
	mountInfoPath := filepath.Join(dir, "mountinfo")

	truncateFile(t, imagePath, 16<<20)
	runCommand(t, "/usr/sbin/mkfs.ext4", "-q", "-F", imagePath)
	truncateFile(t, targetPath, 24<<20)
	truncateFile(t, miscPath, 64<<10)
	truncateFile(t, parentPath, 1)
	if err := os.WriteFile(markerPath, []byte(RecoveryMarker), 0600); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(mountInfoPath, nil, 0600); err != nil {
		t.Fatal(err)
	}
	imageSHA := fileSHA256(t, imagePath)
	writeManifest(t, manifestPath, 16<<20, imageSHA, parentPath)
	confirmation := ExpectedConfirmation(imageSHA)
	if err := os.WriteFile(requestPath, []byte(confirmation+"\n"), 0600); err != nil {
		t.Fatal(err)
	}
	writeTestBCB(t, miscPath)

	cfg := Config{
		ManifestPath:       manifestPath,
		StatusPath:         statusPath,
		RequestPath:        requestPath,
		TargetPath:         targetPath,
		MiscPath:           miscPath,
		ExpectedParentPath: parentPath,
		MountInfoPath:      mountInfoPath,
		RecoveryMarkerPath: markerPath,
		E2fsckPath:         "/usr/sbin/e2fsck",
		Apply:              true,
		AllowWrite:         true,
		Confirmation:       confirmation,
		TestMode:           true,
	}
	status, err := Run(context.Background(), cfg)
	if err != nil {
		t.Fatalf("apply: %v", err)
	}
	if !status.Success || !status.Applied || !status.BCBCleared || status.ReadbackSHA256 != imageSHA {
		t.Fatalf("unexpected status: %+v", status)
	}
	if got := prefixSHA256(t, targetPath, 16<<20); got != imageSHA {
		t.Fatalf("target sha256 %s, want %s", got, imageSHA)
	}
	if _, err := os.Stat(requestPath + ".consumed"); err != nil {
		t.Fatalf("consumed request: %v", err)
	}
	bcb := readAt(t, miscPath, bcbOffset, bcbSize)
	if bcbCommand(bcb) != "" {
		t.Fatalf("BCB command was not cleared: %q", bcbCommand(bcb))
	}
	if string(bcb[bcbSystemFlagStart:bcbSystemFlagStart+4]) != "KEEP" {
		t.Fatalf("BCB systemFlag was not preserved")
	}
	var persisted Status
	data, err := os.ReadFile(statusPath)
	if err != nil {
		t.Fatal(err)
	}
	if err := json.Unmarshal(data, &persisted); err != nil {
		t.Fatal(err)
	}
	if persisted.Phase != "succeeded" || !persisted.BCBCleared {
		t.Fatalf("unexpected persisted status: %+v", persisted)
	}
}

func TestMountedTargetIsRejectedWithoutWriting(t *testing.T) {
	requireCommand(t, "/usr/sbin/mkfs.ext4")
	requireCommand(t, "/usr/sbin/e2fsck")
	dir := t.TempDir()
	imagePath := filepath.Join(dir, "rootfs.img")
	targetPath := filepath.Join(dir, "rootfs.partition")
	miscPath := filepath.Join(dir, "misc.partition")
	parentPath := filepath.Join(dir, "emmc.disk")
	truncateFile(t, imagePath, 16<<20)
	runCommand(t, "/usr/sbin/mkfs.ext4", "-q", "-F", imagePath)
	truncateFile(t, targetPath, 24<<20)
	truncateFile(t, miscPath, 64<<10)
	truncateFile(t, parentPath, 1)
	markerPath := filepath.Join(dir, "marker")
	if err := os.WriteFile(markerPath, []byte(RecoveryMarker), 0600); err != nil {
		t.Fatal(err)
	}
	mountInfoPath := filepath.Join(dir, "mountinfo")
	line := "1 0 0:0 / /mnt/rootfs rw - ext4 " + targetPath + " rw\n"
	if err := os.WriteFile(mountInfoPath, []byte(line), 0600); err != nil {
		t.Fatal(err)
	}
	manifestPath := filepath.Join(dir, "rootfs.manifest.json")
	writeManifest(t, manifestPath, 16<<20, fileSHA256(t, imagePath), parentPath)
	cfg := Config{
		ManifestPath:       manifestPath,
		StatusPath:         filepath.Join(dir, "status.json"),
		RequestPath:        filepath.Join(dir, "request"),
		TargetPath:         targetPath,
		MiscPath:           miscPath,
		ExpectedParentPath: parentPath,
		MountInfoPath:      mountInfoPath,
		RecoveryMarkerPath: markerPath,
		E2fsckPath:         "/usr/sbin/e2fsck",
		TestMode:           true,
	}
	if _, err := Run(context.Background(), cfg); err == nil || !strings.Contains(err.Error(), "mounted at /mnt/rootfs") {
		t.Fatalf("expected mounted target rejection, got %v", err)
	}
}

func TestFailedApplyDoesNotClearBCB(t *testing.T) {
	dir := t.TempDir()
	imagePath := filepath.Join(dir, "rootfs.img")
	targetPath := filepath.Join(dir, "rootfs.partition")
	miscPath := filepath.Join(dir, "misc.partition")
	parentPath := filepath.Join(dir, "emmc.disk")
	truncateFile(t, imagePath, 1<<20)
	truncateFile(t, targetPath, 2<<20)
	truncateFile(t, miscPath, 64<<10)
	truncateFile(t, parentPath, 1)
	markerPath := filepath.Join(dir, "marker")
	mountInfoPath := filepath.Join(dir, "mountinfo")
	if err := os.WriteFile(markerPath, []byte(RecoveryMarker), 0600); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(mountInfoPath, nil, 0600); err != nil {
		t.Fatal(err)
	}
	manifestPath := filepath.Join(dir, "rootfs.manifest.json")
	badHash := strings.Repeat("1", 64)
	writeManifest(t, manifestPath, 1<<20, badHash, parentPath)
	requestPath := filepath.Join(dir, "rootfs.apply")
	confirmation := ExpectedConfirmation(badHash)
	if err := os.WriteFile(requestPath, []byte(confirmation+"\n"), 0600); err != nil {
		t.Fatal(err)
	}
	writeTestBCB(t, miscPath)
	cfg := Config{
		ManifestPath:       manifestPath,
		StatusPath:         filepath.Join(dir, "status.json"),
		RequestPath:        requestPath,
		TargetPath:         targetPath,
		MiscPath:           miscPath,
		ExpectedParentPath: parentPath,
		MountInfoPath:      mountInfoPath,
		RecoveryMarkerPath: markerPath,
		E2fsckPath:         "/usr/sbin/e2fsck",
		Apply:              true,
		AllowWrite:         true,
		Confirmation:       confirmation,
		TestMode:           true,
	}
	if _, err := Run(context.Background(), cfg); err == nil {
		t.Fatal("expected invalid ext4 apply failure")
	}
	if command := bcbCommand(readAt(t, miscPath, bcbOffset, bcbSize)); command != "boot-recovery" {
		t.Fatalf("BCB command changed on failure: %q", command)
	}
	if _, err := os.Stat(requestPath + ".failed"); err != nil {
		t.Fatalf("failed request was not quarantined: %v", err)
	}
}

func TestSmokeClearsOnlyBCBAndDoesNotWriteRootfs(t *testing.T) {
	fixture := newSmokeFixture(t, SmokeRecoveryArgument, "")
	beforeTarget := fileSHA256(t, fixture.targetPath)
	beforeBCB := readAt(t, fixture.miscPath, bcbOffset, bcbSize)

	status, err := Run(context.Background(), fixture.cfg)
	if err != nil {
		t.Fatalf("smoke: %v", err)
	}
	if !status.Success || status.Applied || !status.BCBCleared || !status.SmokeRequestVerified {
		t.Fatalf("unexpected smoke status: %+v", status)
	}
	if status.Mode != "smoke" || status.BytesWritten != 0 {
		t.Fatalf("smoke reported a rootfs write: %+v", status)
	}
	if got := fileSHA256(t, fixture.targetPath); got != beforeTarget {
		t.Fatalf("rootfs target changed: %s != %s", got, beforeTarget)
	}
	afterBCB := readAt(t, fixture.miscPath, bcbOffset, bcbSize)
	if !allZero(afterBCB[:bcbNeedUpdateEnd]) {
		t.Fatal("BCB command fields were not cleared")
	}
	if !bytes.Equal(afterBCB[bcbSystemFlagStart:], beforeBCB[bcbSystemFlagStart:]) {
		t.Fatal("BCB systemFlag was not preserved")
	}
	var persisted Status
	data, err := os.ReadFile(fixture.cfg.SmokeStatusPath)
	if err != nil {
		t.Fatal(err)
	}
	if err := json.Unmarshal(data, &persisted); err != nil {
		t.Fatal(err)
	}
	if persisted.Mode != "smoke" || persisted.Phase != "succeeded" || !persisted.BCBCleared {
		t.Fatalf("unexpected persisted smoke status: %+v", persisted)
	}
}

func TestSmokeRejectsWrongRecoveryArgumentWithoutWrites(t *testing.T) {
	fixture := newSmokeFixture(t, "recovery\n--rknetupdate-smoke-v2\n", "")
	beforeTarget := fileSHA256(t, fixture.targetPath)
	beforeBCB := readAt(t, fixture.miscPath, bcbOffset, bcbSize)

	status, err := Run(context.Background(), fixture.cfg)
	if err == nil || !strings.Contains(err.Error(), "recovery argument") {
		t.Fatalf("expected exact recovery argument rejection, got status=%+v err=%v", status, err)
	}
	if got := fileSHA256(t, fixture.targetPath); got != beforeTarget {
		t.Fatal("rootfs target changed after wrong smoke argument")
	}
	if got := readAt(t, fixture.miscPath, bcbOffset, bcbSize); !bytes.Equal(got, beforeBCB) {
		t.Fatal("BCB changed after wrong smoke argument")
	}
}

func TestSmokeRejectsMountedRootfsWithoutWrites(t *testing.T) {
	dir := t.TempDir()
	targetPath := filepath.Join(dir, "rootfs.partition")
	mountInfo := "1 0 0:0 / /mnt/rootfs rw - ext4 " + targetPath + " rw\n"
	fixture := newSmokeFixtureInDir(t, dir, targetPath, SmokeRecoveryArgument, mountInfo)
	beforeTarget := fileSHA256(t, fixture.targetPath)
	beforeBCB := readAt(t, fixture.miscPath, bcbOffset, bcbSize)

	status, err := Run(context.Background(), fixture.cfg)
	if err == nil || !strings.Contains(err.Error(), "mounted at /mnt/rootfs") {
		t.Fatalf("expected mounted rootfs rejection, got status=%+v err=%v", status, err)
	}
	if got := fileSHA256(t, fixture.targetPath); got != beforeTarget {
		t.Fatal("mounted rootfs target changed")
	}
	if got := readAt(t, fixture.miscPath, bcbOffset, bcbSize); !bytes.Equal(got, beforeBCB) {
		t.Fatal("BCB changed while rootfs was mounted")
	}
}

func TestSmokeProductionPathsAndModesAreFixed(t *testing.T) {
	cfg := DefaultConfig()
	cfg.Smoke = true
	cfg.SmokeStatusPath = "/tmp/status.json"
	if err := cfg.Validate(); err == nil || !strings.Contains(err.Error(), "production smoke-status path is fixed") {
		t.Fatalf("expected fixed smoke status rejection, got %v", err)
	}
	cfg = DefaultConfig()
	cfg.Smoke = true
	cfg.MiscPath = "/dev/mmcblk0p2"
	if err := cfg.Validate(); err == nil || !strings.Contains(err.Error(), "production misc path is fixed") {
		t.Fatalf("expected fixed misc path rejection, got %v", err)
	}
	cfg = DefaultConfig()
	cfg.Smoke = true
	cfg.Apply = true
	cfg.AllowWrite = true
	cfg.Confirmation = "anything"
	if err := cfg.Validate(); err == nil || !strings.Contains(err.Error(), "cannot be combined") {
		t.Fatalf("expected smoke/apply conflict, got %v", err)
	}
}

type smokeFixture struct {
	cfg        Config
	targetPath string
	miscPath   string
}

func newSmokeFixture(t *testing.T, recoveryArgument, mountInfo string) smokeFixture {
	t.Helper()
	dir := t.TempDir()
	return newSmokeFixtureInDir(t, dir, filepath.Join(dir, "rootfs.partition"), recoveryArgument, mountInfo)
}

func newSmokeFixtureInDir(t *testing.T, dir, targetPath, recoveryArgument, mountInfo string) smokeFixture {
	t.Helper()
	miscPath := filepath.Join(dir, "misc.partition")
	markerPath := filepath.Join(dir, "recovery-marker")
	mountInfoPath := filepath.Join(dir, "mountinfo")
	truncateFile(t, targetPath, 2<<20)
	truncateFile(t, miscPath, 64<<10)
	if err := os.WriteFile(markerPath, []byte(RecoveryMarker), 0600); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(mountInfoPath, []byte(mountInfo), 0600); err != nil {
		t.Fatal(err)
	}
	writeSmokeTestBCB(t, miscPath, recoveryArgument)
	cfg := DefaultConfig()
	cfg.TargetPath = targetPath
	cfg.MiscPath = miscPath
	cfg.ExpectedParentPath = filepath.Join(dir, "emmc.disk")
	cfg.MountInfoPath = mountInfoPath
	cfg.RecoveryMarkerPath = markerPath
	cfg.SmokeStatusPath = filepath.Join(dir, "smoke.status.json")
	cfg.Smoke = true
	cfg.TestMode = true
	return smokeFixture{cfg: cfg, targetPath: targetPath, miscPath: miscPath}
}

func writeSmokeTestBCB(t *testing.T, path, recoveryArgument string) {
	t.Helper()
	f, err := os.OpenFile(path, os.O_RDWR, 0)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	bcb := make([]byte, bcbSize)
	copy(bcb[bcbCommandStart:bcbCommandEnd], SmokeBCBCommand)
	copy(bcb[bcbRecoveryStart:bcbRecoveryEnd], recoveryArgument)
	for i := bcbSystemFlagStart; i < len(bcb); i++ {
		bcb[i] = byte((i * 37) & 0xff)
	}
	if _, err := f.WriteAt(bcb, bcbOffset); err != nil {
		t.Fatal(err)
	}
}

func requireCommand(t *testing.T, path string) {
	t.Helper()
	if _, err := os.Stat(path); err != nil {
		t.Skipf("%s is unavailable: %v", path, err)
	}
}

func truncateFile(t *testing.T, path string, size int64) {
	t.Helper()
	f, err := os.OpenFile(path, os.O_CREATE|os.O_RDWR|os.O_TRUNC, 0600)
	if err != nil {
		t.Fatal(err)
	}
	if err := f.Truncate(size); err != nil {
		_ = f.Close()
		t.Fatal(err)
	}
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}
}

func runCommand(t *testing.T, command string, args ...string) {
	t.Helper()
	output, err := exec.Command(command, args...).CombinedOutput()
	if err != nil {
		t.Fatalf("%s failed: %v: %s", command, err, output)
	}
}

func fileSHA256(t *testing.T, path string) string {
	t.Helper()
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	sum := sha256.Sum256(data)
	return hex.EncodeToString(sum[:])
}

func prefixSHA256(t *testing.T, path string, size int64) string {
	t.Helper()
	f, err := os.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	hash := sha256.New()
	if _, err := io.CopyN(hash, f, size); err != nil {
		t.Fatal(err)
	}
	return hex.EncodeToString(hash.Sum(nil))
}

func writeManifest(t *testing.T, path string, size int64, hash, parent string) {
	t.Helper()
	manifest := Manifest{
		Schema:          1,
		Image:           "rootfs.img",
		Size:            size,
		SHA256:          hash,
		Filesystem:      "ext4",
		TargetPartlabel: "rootfs",
		ExpectedParent:  parent,
	}
	data, err := json.Marshal(manifest)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, data, 0600); err != nil {
		t.Fatal(err)
	}
}

func writeTestBCB(t *testing.T, path string) {
	t.Helper()
	f, err := os.OpenFile(path, os.O_RDWR, 0)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	bcb := make([]byte, bcbSize)
	copy(bcb[bcbCommandStart:bcbCommandEnd], "boot-recovery")
	copy(bcb[64:bcbRecoveryEnd], "recovery\n--rknetupdate-rootfs\n")
	copy(bcb[bcbSystemFlagStart:], "KEEP")
	if _, err := f.WriteAt(bcb, bcbOffset); err != nil {
		t.Fatal(err)
	}
}

func readAt(t *testing.T, path string, offset int64, size int) []byte {
	t.Helper()
	f, err := os.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	buffer := make([]byte, size)
	if _, err := f.ReadAt(buffer, offset); err != nil && !errors.Is(err, io.EOF) {
		t.Fatal(err)
	}
	return buffer
}
