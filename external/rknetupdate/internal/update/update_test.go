package update

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"io"
	"log/slog"
	"mime/multipart"
	"net/http"
	"net/http/httptest"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"rknetupdate/internal/bundlecontrol"
	"rknetupdate/internal/recoveryapply"
	"rknetupdate/internal/rootfscontrol"
)

func TestAPIAuthentication(t *testing.T) {
	cfg, manager := testManager(t, true, 1<<20)
	cfg.Token = DefaultToken
	api, err := NewAPI(cfg, manager, slog.New(slog.NewTextHandler(io.Discard, nil)))
	if err != nil {
		t.Fatal(err)
	}

	request := httptest.NewRequest(http.MethodGet, "/api/v1/status", nil)
	response := httptest.NewRecorder()
	api.Handler().ServeHTTP(response, request)
	if response.Code != http.StatusUnauthorized {
		t.Fatalf("unauthenticated status = %d, want 401", response.Code)
	}

	request = httptest.NewRequest(http.MethodGet, "/api/v1/status", nil)
	request.Header.Set("Authorization", "Bearer "+DefaultToken)
	response = httptest.NewRecorder()
	api.Handler().ServeHTTP(response, request)
	if response.Code != http.StatusOK {
		t.Fatalf("authenticated status = %d, want 200: %s", response.Code, response.Body.String())
	}

	request = httptest.NewRequest(http.MethodPost, "/api/v1/reboot", strings.NewReader(`{"job_id":"job"}`))
	response = httptest.NewRecorder()
	api.Handler().ServeHTTP(response, request)
	if response.Code != http.StatusUnauthorized {
		t.Fatalf("unauthenticated reboot = %d, want 401", response.Code)
	}
}

func TestDirectRebootAPIRequiresVerifiedCompletedJob(t *testing.T) {
	t.Run("method", func(t *testing.T) {
		cfg, manager := testManager(t, true, 1<<20)
		api, err := NewAPI(cfg, manager, slog.New(slog.NewTextHandler(io.Discard, nil)))
		if err != nil {
			t.Fatal(err)
		}
		request := httptest.NewRequest(http.MethodGet, "/api/v1/reboot", nil)
		request.Header.Set("Authorization", "Bearer test-token")
		response := httptest.NewRecorder()
		api.Handler().ServeHTTP(response, request)
		if response.Code != http.StatusMethodNotAllowed {
			t.Fatalf("GET reboot status = %d, want 405", response.Code)
		}
	})

	t.Run("missing readback", func(t *testing.T) {
		cfg, manager := testManager(t, false, 1<<20)
		job, err := manager.BeginAndExecuteUpload(context.Background(), TargetBoot, "boot.img", bytes.NewReader(fitImage("kernel", 8192)))
		if err != nil {
			t.Fatal(err)
		}
		final := waitForFinal(t, manager)
		manager.mu.Lock()
		manager.job.ReadbackSHA256 = ""
		manager.mu.Unlock()
		api, err := NewAPI(cfg, manager, slog.New(slog.NewTextHandler(io.Discard, nil)))
		if err != nil {
			t.Fatal(err)
		}
		request := httptest.NewRequest(http.MethodPost, "/api/v1/reboot", strings.NewReader(`{"job_id":"`+job.ID+`"}`))
		request.Header.Set("Authorization", "Bearer test-token")
		response := httptest.NewRecorder()
		api.Handler().ServeHTTP(response, request)
		if response.Code != http.StatusConflict || !strings.Contains(response.Body.String(), "matching readback") {
			t.Fatalf("unverified reboot status = %d body=%s", response.Code, response.Body.String())
		}
		if final.Status != StatusSucceeded {
			t.Fatalf("flash status = %s, want succeeded", final.Status)
		}
	})

	t.Run("accepted after response", func(t *testing.T) {
		cfg, manager := testManager(t, false, 1<<20)
		responseReturned := make(chan struct{})
		ordering := make(chan bool, 1)
		manager.reboot = rebootWorkflowFunc(func(context.Context) error {
			select {
			case <-responseReturned:
				ordering <- true
			default:
				ordering <- false
			}
			return nil
		})
		job, err := manager.BeginAndExecuteUpload(context.Background(), TargetBoot, "boot.img", bytes.NewReader(fitImage("kernel", 8192)))
		if err != nil {
			t.Fatal(err)
		}
		final := waitForFinal(t, manager)
		if final.Status != StatusSucceeded || final.ReadbackSHA256 != final.SHA256 {
			t.Fatalf("direct flash is not readback verified: %+v", final)
		}
		api, err := NewAPI(cfg, manager, slog.New(slog.NewTextHandler(io.Discard, nil)))
		if err != nil {
			t.Fatal(err)
		}
		request := httptest.NewRequest(http.MethodPost, "/api/v1/reboot", strings.NewReader(`{"job_id":"`+job.ID+`"}`))
		request.Header.Set("Authorization", "Bearer test-token")
		response := httptest.NewRecorder()
		api.Handler().ServeHTTP(response, request)
		close(responseReturned)
		if response.Code != http.StatusAccepted {
			t.Fatalf("verified reboot status = %d, want 202: %s", response.Code, response.Body.String())
		}
		select {
		case afterResponse := <-ordering:
			if !afterResponse {
				t.Fatal("reboot workflow ran before the HTTP response returned")
			}
		case <-time.After(2 * time.Second):
			t.Fatal("timed out waiting for delayed reboot workflow")
		}
		status := manager.Status()
		if !status.Busy || status.Job == nil || status.Job.Status != StatusSystemRebootRequested {
			t.Fatalf("reboot status = %+v, want busy system_reboot_requested", status)
		}
	})

	t.Run("reboot failure remains retryable", func(t *testing.T) {
		_, manager := testManager(t, false, 1<<20)
		var attempts atomic.Int32
		manager.reboot = rebootWorkflowFunc(func(context.Context) error {
			if attempts.Add(1) == 1 {
				return errors.New("temporary reboot failure")
			}
			return nil
		})
		job, err := manager.BeginAndExecuteUpload(context.Background(), TargetBoot, "boot.img", bytes.NewReader(fitImage("kernel", 8192)))
		if err != nil {
			t.Fatal(err)
		}
		final := waitForFinal(t, manager)
		if _, err := manager.RequestSystemReboot(job.ID); err != nil {
			t.Fatal(err)
		}
		failed := waitForFinal(t, manager)
		if failed.Status != StatusSystemRebootFailed || !strings.Contains(failed.Error, "temporary reboot failure") {
			t.Fatalf("reboot failure status = %+v, want retryable system_reboot_failed", failed)
		}
		if failed.SHA256 != final.SHA256 || failed.ReadbackSHA256 != final.ReadbackSHA256 {
			t.Fatal("reboot failure discarded the verified flash evidence")
		}
		retrying, err := manager.RequestSystemReboot(job.ID)
		if err != nil {
			t.Fatalf("retry verified reboot: %v", err)
		}
		if retrying.Status != StatusSystemRebootRequested || retrying.Error != "" {
			t.Fatalf("retry status = %+v, want clean system_reboot_requested", retrying)
		}
		deadline := time.Now().Add(2 * time.Second)
		for attempts.Load() != 2 && time.Now().Before(deadline) {
			time.Sleep(10 * time.Millisecond)
		}
		if attempts.Load() != 2 {
			t.Fatalf("reboot attempts = %d, want 2", attempts.Load())
		}
	})
}

func TestConfigurationDefaultsTokenAndRequiresFlashOptIn(t *testing.T) {
	t.Setenv("RKNETUPDATE_TOKEN", "")
	cfg, err := ConfigFromArgs([]string{"--config="})
	if err != nil {
		t.Fatalf("default token configuration should start: %v", err)
	}
	if cfg.Token != DefaultToken {
		t.Fatalf("default token = %q, want %q", cfg.Token, DefaultToken)
	}
	if _, err := ConfigFromArgs([]string{"--config=", "--dry-run=false"}); err == nil || !strings.Contains(err.Error(), "requires --allow-flash") {
		t.Fatalf("real mode without allow-flash should fail, got %v", err)
	}
	if _, err := ConfigFromArgs([]string{"--config=", "--dry-run=false", "--allow-flash"}); err == nil || !strings.Contains(err.Error(), "expected-parent-device") {
		t.Fatalf("real mode without expected parent should fail, got %v", err)
	}
	cfg, err = ConfigFromArgs([]string{"--config=", "--rootfs-enabled", "--rootfs-allow-write", "--expected-parent-device=/dev/mmcblk0"})
	if err != nil {
		t.Fatalf("rootfs-only live mode should be accepted: %v", err)
	}
	if !cfg.DryRun || !cfg.RootfsEnabled || !cfg.RootfsAllowWrite {
		t.Fatalf("unexpected rootfs-only mode: %+v", cfg)
	}
	if _, err := ConfigFromArgs([]string{"--config=", "--rootfs-allow-write", "--expected-parent-device=/dev/mmcblk0"}); err == nil || !strings.Contains(err.Error(), "requires rootfs-enabled") {
		t.Fatalf("rootfs write without rootfs enabled should fail, got %v", err)
	}
	bundleCfg, err := ConfigFromArgs([]string{"--config=", "--bundle-enabled", "--bundle-allow-write", "--expected-parent-device=/dev/mmcblk0"})
	if err != nil || !bundleCfg.BundleEnabled || !bundleCfg.BundleAllowWrite {
		t.Fatalf("bundle-only live mode should be accepted: cfg=%+v err=%v", bundleCfg, err)
	}
	if _, err := ConfigFromArgs([]string{"--config=", "--bundle-allow-write", "--expected-parent-device=/dev/mmcblk0"}); err == nil || !strings.Contains(err.Error(), "requires bundle-enabled") {
		t.Fatalf("bundle write without bundle enabled should fail, got %v", err)
	}
	updateImageCfg, err := ConfigFromArgs([]string{"--config=", "--updateimg-enabled", "--updateimg-allow-write", "--expected-parent-device=/dev/mmcblk0"})
	if err != nil || !updateImageCfg.UpdateImageEnabled || !updateImageCfg.UpdateImageAllowWrite {
		t.Fatalf("update.img-only live mode should be accepted: cfg=%+v err=%v", updateImageCfg, err)
	}
	if _, err := ConfigFromArgs([]string{"--config=", "--updateimg-allow-write", "--expected-parent-device=/dev/mmcblk0"}); err == nil || !strings.Contains(err.Error(), "requires updateimg-enabled") {
		t.Fatalf("update.img write without update.img enabled should fail, got %v", err)
	}
}

func TestConfigurationHelpDoesNotExposeConfiguredToken(t *testing.T) {
	oldToken, tokenWasSet := os.LookupEnv("RKNETUPDATE_TOKEN")
	if err := os.Unsetenv("RKNETUPDATE_TOKEN"); err != nil {
		t.Fatalf("unset RKNETUPDATE_TOKEN: %v", err)
	}
	t.Cleanup(func() {
		if tokenWasSet {
			_ = os.Setenv("RKNETUPDATE_TOKEN", oldToken)
			return
		}
		_ = os.Unsetenv("RKNETUPDATE_TOKEN")
	})

	const configuredToken = "configured-secret-must-not-appear-in-usage"
	configPath := filepath.Join(t.TempDir(), "rknetupdate.conf")
	if err := os.WriteFile(configPath, []byte("token="+configuredToken+"\n"), 0o600); err != nil {
		t.Fatalf("write config: %v", err)
	}

	var usage bytes.Buffer
	if _, err := configFromArgs([]string{"--config=" + configPath, "--help"}, &usage); !errors.Is(err, flag.ErrHelp) {
		t.Fatalf("help error = %v, want flag.ErrHelp", err)
	}
	if strings.Contains(usage.String(), configuredToken) {
		t.Fatalf("usage exposed configured token: %q", usage.String())
	}

	cfg, err := configFromArgs([]string{"--config=" + configPath, "--token=explicit-token"}, io.Discard)
	if err != nil {
		t.Fatalf("parse explicit token: %v", err)
	}
	if cfg.Token != "explicit-token" {
		t.Fatalf("explicit token = %q, want explicit-token", cfg.Token)
	}
}

func TestUploadFilenameTypeAndSizeLimits(t *testing.T) {
	t.Run("filename", func(t *testing.T) {
		_, manager := testManager(t, true, 1<<20)
		_, err := manager.BeginUpload(context.Background(), TargetBoot, "uboot.img", bytes.NewReader(fitImage("kernel", 512)))
		if err == nil || !strings.Contains(err.Error(), "expected filename") {
			t.Fatalf("expected filename rejection, got %v", err)
		}
	})

	t.Run("detected type mismatch", func(t *testing.T) {
		_, manager := testManager(t, true, 1<<20)
		_, err := manager.BeginUpload(context.Background(), TargetBoot, "boot.img", bytes.NewReader(fitImage("u-boot", 512)))
		if err == nil || !strings.Contains(err.Error(), "looks like uboot.img") {
			t.Fatalf("expected type rejection, got %v", err)
		}
		assertUploadDirEmpty(t, manager.cfg.UploadDir)
	})

	t.Run("size", func(t *testing.T) {
		_, manager := testManager(t, true, 128)
		_, err := manager.BeginUpload(context.Background(), TargetBoot, "boot.img", bytes.NewReader(fitImage("kernel", 512)))
		if err == nil || !strings.Contains(err.Error(), "maximum upload size") {
			t.Fatalf("expected size rejection, got %v", err)
		}
		assertUploadDirEmpty(t, manager.cfg.UploadDir)
	})
}

func TestUploadRejectsRockchipUpdateImage(t *testing.T) {
	_, manager := testManager(t, true, 1<<20)
	image := makeRKUpdateImage(t)
	_, err := manager.BeginUpload(context.Background(), TargetBoot, "boot.img", bytes.NewReader(image))
	if err == nil || !strings.Contains(err.Error(), "multi-part recovery package") {
		t.Fatalf("expected update.img rejection, got %v", err)
	}
	assertUploadDirEmpty(t, manager.cfg.UploadDir)
}

func TestCompleteUpdateImageUsesSystemOTAWorkflow(t *testing.T) {
	_, manager := testManager(t, true, 1<<20)
	manager.cfg.UpdateImageEnabled = true
	manager.cfg.ExpectedParentDevice = manager.cfg.RootfsDevice
	manager.cfg.UpdateImageStageDir = filepath.Join(filepath.Dir(manager.cfg.UploadDir), "updateimg-stage")
	if err := ensureSafeUploadDir(manager.cfg.UpdateImageStageDir); err != nil {
		t.Fatal(err)
	}
	image := makeCompleteRKUpdateImage(t)
	job, err := manager.BeginAndExecuteUpload(context.Background(), TargetUpdateImage, "update.img", bytes.NewReader(image))
	if err != nil {
		t.Fatal(err)
	}
	if job.Target != TargetUpdateImage || job.Validation.DetectedType != "rockchip-update-img" {
		t.Fatalf("unexpected update.img job: %+v", job)
	}
	if got := strings.Join(job.Validation.Items, ","); got != "parameter,recovery,rootfs" {
		t.Fatalf("update.img validation items = %q", got)
	}
	final := waitForFinal(t, manager)
	if final.Status != StatusDryRunDone {
		t.Fatalf("update.img dry-run status=%s error=%s", final.Status, final.Error)
	}
	entries, err := os.ReadDir(manager.cfg.UpdateImageStageDir)
	if err != nil {
		t.Fatal(err)
	}
	if len(entries) != 0 {
		t.Fatalf("update.img dry-run left staged files: %+v", entries)
	}
}

func TestTargetResolutionSafety(t *testing.T) {
	dir := t.TempDir()
	partition := filepath.Join(dir, "boot-partition")
	if err := os.WriteFile(partition, make([]byte, 4096), 0600); err != nil {
		t.Fatal(err)
	}
	if _, err := ResolveTarget(partition, false); err == nil || !strings.Contains(err.Error(), "test_mode") {
		t.Fatalf("regular target without test mode should fail, got %v", err)
	}
	resolved, err := ResolveTarget(partition, true)
	if err != nil {
		t.Fatalf("explicit regular target in test mode: %v", err)
	}
	if !resolved.Regular || resolved.Size != 4096 || resolved.ParentPath != partition {
		t.Fatalf("unexpected resolved target: %+v", resolved)
	}
	symlink := filepath.Join(dir, "boot-link")
	if err := os.Symlink(partition, symlink); err != nil {
		t.Fatal(err)
	}
	if _, err := ResolveTarget(symlink, true); err == nil || !strings.Contains(err.Error(), "Symbolic") && !strings.Contains(err.Error(), "symbolic") {
		t.Fatalf("explicit symbolic link should fail, got %v", err)
	}
	if _, err := ValidateExpectedParentDevice(symlink, true); err == nil || !strings.Contains(strings.ToLower(err.Error()), "symbolic") {
		t.Fatalf("symbolic expected parent should fail, got %v", err)
	}
	if _, err := ResolveTarget(filepath.Join(dir, "missing"), true); err == nil {
		t.Fatal("missing target should fail")
	}
}

func TestParentBlockNameRecognition(t *testing.T) {
	for _, test := range []struct {
		path string
		want string
	}{
		{"/sys/devices/platform/fe2e0000.mmc/mmc_host/mmc0/mmc0:0001/block/mmcblk0/mmcblk0p4", "mmcblk0"},
		{"/sys/devices/pci0000:00/0000:00:04.0/nvme/nvme0/nvme0n1/nvme0n1p3", "nvme0n1"},
	} {
		got, err := parentBlockName(test.path)
		if err != nil || got != test.want {
			t.Fatalf("parentBlockName(%q) = %q, %v; want %q", test.path, got, err, test.want)
		}
	}
}

func TestExpectedParentMismatchRejectsUpload(t *testing.T) {
	dir := t.TempDir()
	partition := filepath.Join(dir, "partition")
	wrongParent := filepath.Join(dir, "wrong-parent")
	for _, path := range []string{partition, wrongParent} {
		if err := os.WriteFile(path, make([]byte, 4096), 0600); err != nil {
			t.Fatal(err)
		}
	}
	cfg := DefaultConfig()
	cfg.Token = "test-token"
	cfg.UploadDir = filepath.Join(dir, "uploads")
	cfg.TestMode = true
	cfg.DryRun = false
	cfg.AllowFlash = true
	cfg.BootDevice = partition
	cfg.UbootDevice = partition
	cfg.ExpectedParentDevice = wrongParent
	manager, err := NewManager(cfg, slog.New(slog.NewTextHandler(io.Discard, nil)))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(manager.Close)
	_, err = manager.BeginUpload(context.Background(), TargetBoot, "boot.img", bytes.NewReader(fitImage("kernel", 512)))
	if err == nil || !strings.Contains(err.Error(), "does not match expected") {
		t.Fatalf("expected parent mismatch rejection, got %v", err)
	}
}

func TestSingleActiveJob(t *testing.T) {
	_, manager := testManager(t, true, 1<<20)
	job, err := manager.BeginUpload(context.Background(), TargetBoot, "boot.img", bytes.NewReader(fitImage("kernel", 512)))
	if err != nil {
		t.Fatal(err)
	}
	if _, err := manager.BeginUpload(context.Background(), TargetUboot, "uboot.img", bytes.NewReader(fitImage("u-boot", 512))); err != ErrBusy {
		t.Fatalf("second job = %v, want ErrBusy", err)
	}
	if err := manager.Cancel(job.ID); err != nil {
		t.Fatal(err)
	}
}

func TestDryRunDoesNotWriteAndCleansImage(t *testing.T) {
	_, manager := testManager(t, true, 1<<20)
	before, err := os.ReadFile(manager.cfg.BootDevice)
	if err != nil {
		t.Fatal(err)
	}
	job, err := manager.BeginAndExecuteUpload(context.Background(), TargetBoot, "boot.img", bytes.NewReader(fitImage("kernel", 512)))
	if err != nil {
		t.Fatal(err)
	}
	if job.Confirmation != "" {
		t.Fatalf("one-click response exposed confirmation token: %+v", job)
	}
	final := waitForFinal(t, manager)
	if final.Status != StatusDryRunDone {
		t.Fatalf("status = %s, want %s; error=%s", final.Status, StatusDryRunDone, final.Error)
	}
	after, err := os.ReadFile(manager.cfg.BootDevice)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(before, after) {
		t.Fatal("dry-run modified fake partition")
	}
	assertUploadDirEmpty(t, manager.cfg.UploadDir)
}

func TestFakePartitionFlashAndReadback(t *testing.T) {
	_, manager := testManager(t, false, 1<<20)
	image := fitImage("kernel", 8192)
	job, err := manager.BeginAndExecuteUpload(context.Background(), TargetBoot, "boot.img", bytes.NewReader(image))
	if err != nil {
		t.Fatal(err)
	}
	final := waitForFinal(t, manager)
	if final.Status != StatusSucceeded {
		t.Fatalf("status = %s, want succeeded; error=%s", final.Status, final.Error)
	}
	if final.ReadbackSHA256 != job.SHA256 {
		t.Fatalf("readback hash = %s, want %s", final.ReadbackSHA256, job.SHA256)
	}
	written, err := os.ReadFile(manager.cfg.BootDevice)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(written[:len(image)], image) {
		t.Fatal("fake partition does not contain uploaded image")
	}
	assertUploadDirEmpty(t, manager.cfg.UploadDir)
}

func TestUploadAPIStartsOneClickWorkflow(t *testing.T) {
	cfg, manager := testManager(t, true, 1<<20)
	api, err := NewAPI(cfg, manager, slog.New(slog.NewTextHandler(io.Discard, nil)))
	if err != nil {
		t.Fatal(err)
	}
	body := &bytes.Buffer{}
	writer := multipart.NewWriter(body)
	part, err := writer.CreateFormFile("image", "boot.img")
	if err != nil {
		t.Fatal(err)
	}
	_, _ = part.Write(fitImage("kernel", 512))
	_ = writer.Close()
	request := httptest.NewRequest(http.MethodPost, "/api/v1/uploads?target=boot", body)
	request.Header.Set("Content-Type", writer.FormDataContentType())
	request.Header.Set("Authorization", "Bearer test-token")
	response := httptest.NewRecorder()
	api.Handler().ServeHTTP(response, request)
	if response.Code != http.StatusCreated {
		t.Fatalf("upload status = %d: %s", response.Code, response.Body.String())
	}
	var job Job
	if err := json.Unmarshal(response.Body.Bytes(), &job); err != nil {
		t.Fatal(err)
	}
	if job.Confirmation != "" {
		t.Fatalf("upload response exposed confirmation token: %+v", job)
	}
	if strings.Contains(response.Body.String(), "confirmation_") {
		t.Fatalf("upload response exposed confirmation metadata: %s", response.Body.String())
	}
	final := waitForFinal(t, manager)
	if final.Status != StatusDryRunDone {
		t.Fatalf("one-click upload final status = %s, want %s", final.Status, StatusDryRunDone)
	}
	request = httptest.NewRequest(http.MethodPost, "/api/v1/jobs/confirm", nil)
	request.Header.Set("Authorization", "Bearer test-token")
	response = httptest.NewRecorder()
	api.Handler().ServeHTTP(response, request)
	if response.Code != http.StatusNotFound {
		t.Fatalf("removed confirmation endpoint status = %d, want 404", response.Code)
	}
}

func testManager(t *testing.T, dryRun bool, maxBytes int64) (Config, *Manager) {
	t.Helper()
	dir := t.TempDir()
	boot := filepath.Join(dir, "boot.partition")
	uboot := filepath.Join(dir, "uboot.partition")
	rootfs := filepath.Join(dir, "rootfs.partition")
	for _, path := range []string{boot, uboot, rootfs} {
		if err := os.WriteFile(path, make([]byte, 2<<20), 0600); err != nil {
			t.Fatal(err)
		}
	}
	cfg := DefaultConfig()
	cfg.Token = "test-token"
	cfg.UploadDir = filepath.Join(dir, "uploads")
	cfg.MaxUploadBytes = maxBytes
	cfg.TestMode = true
	cfg.DryRun = dryRun
	cfg.AllowFlash = !dryRun
	cfg.BootDevice = boot
	cfg.UbootDevice = uboot
	cfg.RootfsDevice = rootfs
	if !dryRun {
		cfg.UbootDevice = boot
		cfg.ExpectedParentDevice = boot
	}
	manager, err := NewManager(cfg, slog.New(slog.NewTextHandler(io.Discard, nil)))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(manager.Close)
	return cfg, manager
}

func fitImage(label string, size int) []byte {
	if size < 64 {
		size = 64
	}
	b := make([]byte, size)
	binary.BigEndian.PutUint32(b[:4], 0xd00dfeed)
	copy(b[32:], []byte(label))
	for i := 64; i < len(b); i++ {
		b[i] = byte(i % 251)
	}
	return b
}

func makeRKUpdateImage(t *testing.T) []byte {
	t.Helper()
	const headerSize = 102
	const firmwareHeaderSize = 1932
	image := make([]byte, headerSize+firmwareHeaderSize+4096)
	copy(image[:4], "RKFW")
	binary.LittleEndian.PutUint32(image[33:37], headerSize)
	binary.LittleEndian.PutUint32(image[37:41], firmwareHeaderSize+4096)
	firmware := image[headerSize:]
	copy(firmware[:4], "RKAF")
	binary.LittleEndian.PutUint32(firmware[136:140], 1)
	entry := firmware[140 : 140+112]
	copy(entry[:32], "recovery")
	binary.LittleEndian.PutUint32(entry[96:100], firmwareHeaderSize)
	binary.LittleEndian.PutUint32(entry[108:112], 4096)
	return image
}

func makeCompleteRKUpdateImage(t *testing.T) []byte {
	t.Helper()
	const (
		headerSize         = 102
		firmwareHeaderSize = 1932
	)
	payloads := [][]byte{
		[]byte("TYPE: GPT\nCMDLINE: mtdparts=:-@0x00078000(rootfs:grow)\n"),
		[]byte("recovery fixture"),
		[]byte("rootfs fixture"),
	}
	names := []string{"parameter", "recovery", "rootfs"}
	total := headerSize + firmwareHeaderSize
	for _, payload := range payloads {
		total += len(payload)
	}
	image := make([]byte, total)
	copy(image[:4], "RKFW")
	binary.LittleEndian.PutUint32(image[33:37], headerSize)
	binary.LittleEndian.PutUint32(image[37:41], uint32(total-headerSize))
	firmware := image[headerSize:]
	copy(firmware[:4], "RKAF")
	binary.LittleEndian.PutUint32(firmware[136:140], uint32(len(names)))
	offset := firmwareHeaderSize
	for index, payload := range payloads {
		entry := firmware[140+index*112 : 140+(index+1)*112]
		copy(entry[:32], names[index])
		binary.LittleEndian.PutUint32(entry[96:100], uint32(offset))
		binary.LittleEndian.PutUint32(entry[108:112], uint32(len(payload)))
		copy(firmware[offset:offset+len(payload)], payload)
		offset += len(payload)
	}
	return image
}

func waitForFinal(t *testing.T, manager *Manager) *Job {
	t.Helper()
	deadline := time.Now().Add(3 * time.Second)
	for time.Now().Before(deadline) {
		status := manager.Status()
		if status.Job != nil && !status.Busy {
			return status.Job
		}
		time.Sleep(10 * time.Millisecond)
	}
	t.Fatal("job did not complete")
	return nil
}

func assertUploadDirEmpty(t *testing.T, dir string) {
	t.Helper()
	entries, err := os.ReadDir(dir)
	if err != nil {
		t.Fatal(err)
	}
	if len(entries) != 0 {
		t.Fatalf("upload directory contains %d files", len(entries))
	}
}

func TestFlashAndVerifyRejectsHashMismatch(t *testing.T) {
	dir := t.TempDir()
	imagePath := filepath.Join(dir, "boot.img")
	targetPath := filepath.Join(dir, "partition")
	image := fitImage("kernel", 512)
	if err := os.WriteFile(imagePath, image, 0600); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(targetPath, make([]byte, 4096), 0600); err != nil {
		t.Fatal(err)
	}
	digest := sha256.Sum256([]byte("different"))
	_, err := flashAndVerify(context.Background(), imagePath, ResolvedTarget{Path: targetPath, Size: 4096, Regular: true}, int64(len(image)), hex.EncodeToString(digest[:]), func(JobStatus, int) {})
	if err == nil || !strings.Contains(err.Error(), "mismatch") {
		t.Fatalf("expected readback mismatch, got %v", err)
	}
}

func TestRootfsUploadUsesRecoveryWorkflow(t *testing.T) {
	cfg, manager, target := testRootfsManager(t)
	image := testExt4Image(t, 16<<20)
	before, err := os.ReadFile(target)
	if err != nil {
		t.Fatal(err)
	}

	job, err := manager.BeginUpload(context.Background(), TargetRootfs, "rootfs.img", bytes.NewReader(image))
	if err != nil {
		t.Fatal(err)
	}
	if job.Validation.DetectedType != "ext4" || job.Status != StatusAwaiting {
		t.Fatalf("unexpected rootfs upload result: %+v", job)
	}
	manifestPath := filepath.Join(cfg.RootfsStageDir, rootfsManifestName)
	manifest, stagedPath, err := recoveryapply.LoadRootfsManifestV2(manifestPath, true)
	if err != nil {
		t.Fatal(err)
	}
	if stagedPath != filepath.Join(cfg.RootfsStageDir, rootfsImageName) || manifest.SHA256 != job.SHA256 || manifest.Size != int64(len(image)) {
		t.Fatalf("unexpected staged rootfs manifest: %+v, image=%s", manifest, stagedPath)
	}
	if _, err := manager.Confirm(context.Background(), job.ID, job.SHA256, job.Confirmation, "FLASH ROOTFS"); err == nil {
		t.Fatal("accepted a non-recovery rootfs confirmation phrase")
	}
	prepared, err := manager.Confirm(context.Background(), job.ID, job.SHA256, job.Confirmation, rootfscontrol.PrepareConfirmation(job.SHA256))
	if err != nil {
		t.Fatal(err)
	}
	if prepared.Status != StatusRecoveryPrepared || prepared.RecoveryPhase != "rootfs-prepared" || prepared.Confirmation == "" {
		t.Fatalf("unexpected prepared rootfs job: %+v", prepared)
	}
	afterPrepare, err := os.ReadFile(target)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(before, afterPrepare) {
		t.Fatal("rootfs upload or preparation wrote the running target")
	}
	if _, err := manager.RebootRootfs(context.Background(), prepared.ID, prepared.SHA256, prepared.Confirmation, "REBOOT ROOTFS"); err == nil {
		t.Fatal("accepted a non-v2 rootfs reboot confirmation phrase")
	}
	rebooted, err := manager.RebootRootfs(context.Background(), prepared.ID, prepared.SHA256, prepared.Confirmation, rootfscontrol.RebootConfirmation(prepared.SHA256))
	if err != nil {
		t.Fatal(err)
	}
	if rebooted.Status != StatusRebootRequested {
		t.Fatalf("reboot status = %s, want %s", rebooted.Status, StatusRebootRequested)
	}
	fake := manager.rootfs.(*fakeRootfsWorkflow)
	select {
	case <-fake.rebooted:
	case <-time.After(2 * time.Second):
		t.Fatal("rootfs clean reboot was not requested")
	}
	afterRebootRequest, err := os.ReadFile(target)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(before, afterRebootRequest) {
		t.Fatal("rootfs reboot request wrote the running target")
	}
	manager.Close()
	if _, err := os.Stat(filepath.Join(cfg.RootfsStageDir, rootfsImageName)); err != nil {
		t.Fatalf("prepared rootfs image was removed during manager shutdown: %v", err)
	}
}

func TestRootfsUploadRejectsNonExt4AndCleansStage(t *testing.T) {
	cfg, manager, _ := testRootfsManager(t)
	_, err := manager.BeginUpload(context.Background(), TargetRootfs, "rootfs.img", bytes.NewReader(make([]byte, 64<<10)))
	if err == nil || !strings.Contains(err.Error(), "ext4") {
		t.Fatalf("expected ext4 rejection, got %v", err)
	}
	assertUploadDirEmpty(t, cfg.RootfsStageDir)
}

func TestRootfsUploadDefersE2fsckToRecovery(t *testing.T) {
	cfg, manager, _ := testRootfsManager(t)
	failingE2fsck := filepath.Join(t.TempDir(), "e2fsck-fail")
	if err := os.WriteFile(failingE2fsck, []byte("#!/bin/sh\nexit 12\n"), 0700); err != nil {
		t.Fatal(err)
	}
	manager.cfg.RootfsE2fsckPath = failingE2fsck

	job, err := manager.BeginUpload(context.Background(), TargetRootfs, "rootfs.img", bytes.NewReader(testExt4Image(t, 16<<20)))
	if err != nil {
		t.Fatalf("normal-system staging must defer e2fsck to recovery, got %v", err)
	}
	if job.Validation.DetectedType != "ext4" || !strings.Contains(job.Validation.Summary, "recovery") {
		t.Fatalf("unexpected deferred rootfs validation: %+v", job.Validation)
	}
	if err := manager.Cancel(job.ID); err != nil {
		t.Fatal(err)
	}
	assertUploadDirEmpty(t, cfg.RootfsStageDir)
}

func TestRootfsAPIStartsRecoveryAutomatically(t *testing.T) {
	_, manager, _ := testRootfsManager(t)
	api, err := NewAPI(manager.cfg, manager, slog.New(slog.NewTextHandler(io.Discard, nil)))
	if err != nil {
		t.Fatal(err)
	}
	image := testExt4Image(t, 16<<20)
	body := &bytes.Buffer{}
	writer := multipart.NewWriter(body)
	part, err := writer.CreateFormFile("image", "rootfs.img")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := part.Write(image); err != nil {
		t.Fatal(err)
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}
	request := httptest.NewRequest(http.MethodPost, "/api/v1/uploads?target=rootfs", body)
	request.Header.Set("Content-Type", writer.FormDataContentType())
	request.Header.Set("Authorization", "Bearer test-token")
	response := httptest.NewRecorder()
	api.Handler().ServeHTTP(response, request)
	if response.Code != http.StatusCreated {
		t.Fatalf("rootfs upload status = %d: %s", response.Code, response.Body.String())
	}
	var started Job
	if err := json.Unmarshal(response.Body.Bytes(), &started); err != nil {
		t.Fatal(err)
	}
	if started.Status != StatusRebootRequested || started.Confirmation != "" {
		t.Fatalf("unexpected automatic rootfs response: %+v", started)
	}
	fake := manager.rootfs.(*fakeRootfsWorkflow)
	if !fake.prepared || !fake.verified {
		t.Fatalf("rootfs workflow did not prepare and verify BCB: %+v", fake)
	}
	select {
	case <-fake.rebooted:
	case <-time.After(2 * time.Second):
		t.Fatal("rootfs API did not request reboot")
	}
}

func TestBundleUploadStagesIndependentArtifactsAndDryRuns(t *testing.T) {
	cfg, manager := testBundleManager(t, false)
	rootfs := testExt4Image(t, 16<<20)
	boot := fitImage("kernel", 4096)
	uboot := fitImage("u-boot", 4096)
	job, err := manager.BeginBundleUpload(context.Background(), func(add func(string, io.Reader) error) error {
		for _, item := range []struct {
			name string
			data []byte
		}{
			{"uboot.img", uboot},
			{"rootfs.img", rootfs},
			{"boot.img", boot},
		} {
			if err := add(item.name, bytes.NewReader(item.data)); err != nil {
				return err
			}
		}
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	if job.Target != TargetBundle || job.Status != StatusAwaiting || len(job.Artifacts) != 3 || job.Artifacts[0].Target != TargetRootfs || job.Artifacts[2].Target != TargetUboot {
		t.Fatalf("unexpected staged bundle: %+v", job)
	}
	if job.Artifacts[0].FilesystemUUID == "" || job.Artifacts[0].BlockSize == 0 {
		t.Fatalf("rootfs geometry was not preserved in artifact: %+v", job.Artifacts[0])
	}
	manifest, err := recoveryapply.LoadBundleManifest(filepath.Join(cfg.BundleStageDir, bundleManifestName), true)
	if err != nil || len(manifest.Artifacts) != 3 {
		t.Fatalf("manifest=%+v err=%v", manifest, err)
	}
	if _, err := manager.Confirm(context.Background(), job.ID, job.SHA256, job.Confirmation, "CHECK ROOTFS"); err == nil {
		t.Fatal("accepted non-bundle confirmation")
	}
	if _, err := manager.Confirm(context.Background(), job.ID, job.SHA256, job.Confirmation, "CHECK BUNDLE"); err != nil {
		t.Fatal(err)
	}
	final := manager.Status().Job
	if final == nil || final.Status != StatusDryRunDone {
		t.Fatalf("final bundle job = %+v", final)
	}
	assertUploadDirEmpty(t, cfg.BundleStageDir)
}

func TestBundleUploadLiveStartsRecoveryAutomatically(t *testing.T) {
	_, manager := testBundleManager(t, true)
	job, err := manager.BeginAndExecuteBundleUpload(context.Background(), func(add func(string, io.Reader) error) error {
		if err := add("rootfs.img", bytes.NewReader(testExt4Image(t, 16<<20))); err != nil {
			return err
		}
		return add("boot.img", bytes.NewReader(fitImage("kernel", 4096)))
	})
	if err != nil {
		t.Fatal(err)
	}
	if err != nil || job.Status != StatusRebootRequested || job.Confirmation != "" {
		t.Fatalf("automatic bundle start=%+v err=%v", job, err)
	}
	fake := manager.bundle.(*fakeBundleWorkflow)
	if !fake.prepared || !fake.verified {
		t.Fatalf("bundle workflow did not prepare and verify BCB: %+v", fake)
	}
	select {
	case <-fake.rebooted:
	case <-time.After(2 * time.Second):
		t.Fatal("bundle clean reboot was not requested")
	}
}

func TestBundleAPIStartsRecoveryAutomatically(t *testing.T) {
	cfg, manager := testBundleManager(t, true)
	api, err := NewAPI(cfg, manager, slog.New(slog.NewTextHandler(io.Discard, nil)))
	if err != nil {
		t.Fatal(err)
	}
	body := &bytes.Buffer{}
	writer := multipart.NewWriter(body)
	for _, image := range []struct {
		name string
		data []byte
	}{{"rootfs.img", testExt4Image(t, 16<<20)}, {"boot.img", fitImage("kernel", 4096)}} {
		part, err := writer.CreateFormFile("image", image.name)
		if err != nil {
			t.Fatal(err)
		}
		if _, err := part.Write(image.data); err != nil {
			t.Fatal(err)
		}
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}
	request := httptest.NewRequest(http.MethodPost, "/api/v1/bundles", body)
	request.Header.Set("Content-Type", writer.FormDataContentType())
	request.Header.Set("Authorization", "Bearer test-token")
	response := httptest.NewRecorder()
	api.Handler().ServeHTTP(response, request)
	if response.Code != http.StatusCreated {
		t.Fatalf("bundle API status = %d: %s", response.Code, response.Body.String())
	}
	var started Job
	if err := json.Unmarshal(response.Body.Bytes(), &started); err != nil {
		t.Fatal(err)
	}
	if started.Status != StatusRebootRequested || started.Confirmation != "" {
		t.Fatalf("unexpected automatic bundle response: %+v", started)
	}
	fake := manager.bundle.(*fakeBundleWorkflow)
	select {
	case <-fake.rebooted:
	case <-time.After(2 * time.Second):
		t.Fatal("bundle API did not request reboot")
	}
}

func TestBundleAPIRejectsDuplicateArtifactAndCleansStage(t *testing.T) {
	cfg, manager := testBundleManager(t, false)
	api, err := NewAPI(cfg, manager, slog.New(slog.NewTextHandler(io.Discard, nil)))
	if err != nil {
		t.Fatal(err)
	}
	body := &bytes.Buffer{}
	writer := multipart.NewWriter(body)
	for _, image := range []struct {
		name string
		data []byte
	}{{"rootfs.img", testExt4Image(t, 16<<20)}, {"rootfs.img", testExt4Image(t, 16<<20)}} {
		part, err := writer.CreateFormFile("image", image.name)
		if err != nil {
			t.Fatal(err)
		}
		if _, err := part.Write(image.data); err != nil {
			t.Fatal(err)
		}
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}
	request := httptest.NewRequest(http.MethodPost, "/api/v1/bundles", body)
	request.Header.Set("Content-Type", writer.FormDataContentType())
	request.Header.Set("Authorization", "Bearer test-token")
	response := httptest.NewRecorder()
	api.Handler().ServeHTTP(response, request)
	if response.Code != http.StatusBadRequest || !strings.Contains(response.Body.String(), "duplicate") {
		t.Fatalf("bundle response = %d: %s", response.Code, response.Body.String())
	}
	assertUploadDirEmpty(t, cfg.BundleStageDir)
}

type fakeRootfsWorkflow struct {
	prepared bool
	verified bool
	rebooted chan struct{}
}

type rebootWorkflowFunc func(context.Context) error

func (f rebootWorkflowFunc) Reboot(ctx context.Context) error { return f(ctx) }

type fakeBundleWorkflow struct {
	prepared bool
	verified bool
	rebooted chan struct{}
}

func (f *fakeBundleWorkflow) Prepare(_ context.Context, _ string) (bundlecontrol.Result, error) {
	f.prepared = true
	return bundlecontrol.Result{Success: true, BCBPhase: "bundle-prepared"}, nil
}

func (f *fakeBundleWorkflow) VerifyPrepared(_ context.Context, _ string) (bundlecontrol.Result, error) {
	if !f.prepared {
		return bundlecontrol.Result{}, os.ErrInvalid
	}
	f.verified = true
	return bundlecontrol.Result{Success: true, BCBPhase: "bundle-prepared"}, nil
}

func (f *fakeBundleWorkflow) Reboot(context.Context) error {
	close(f.rebooted)
	return nil
}

func (f *fakeRootfsWorkflow) Prepare(_ context.Context, _ string) (rootfscontrol.Result, error) {
	f.prepared = true
	return rootfscontrol.Result{Success: true, BCBPhase: "rootfs-prepared"}, nil
}

func (f *fakeRootfsWorkflow) VerifyPrepared(_ context.Context, _ string) (rootfscontrol.Result, error) {
	if !f.prepared {
		return rootfscontrol.Result{}, os.ErrInvalid
	}
	f.verified = true
	return rootfscontrol.Result{Success: true, BCBPhase: "rootfs-prepared"}, nil
}

func (f *fakeRootfsWorkflow) Reboot(context.Context) error {
	close(f.rebooted)
	return nil
}

func testRootfsManager(t *testing.T) (Config, *Manager, string) {
	t.Helper()
	e2fsck, err := exec.LookPath("e2fsck")
	if err != nil {
		t.Skip("e2fsck unavailable")
	}
	dir := t.TempDir()
	target := filepath.Join(dir, "rootfs.partition")
	if err := os.WriteFile(target, make([]byte, 32<<20), 0600); err != nil {
		t.Fatal(err)
	}
	cfg := DefaultConfig()
	cfg.Token = "test-token"
	cfg.UploadDir = filepath.Join(dir, "uploads")
	cfg.RootfsStageDir = filepath.Join(dir, "rootfs-v2")
	cfg.RootfsE2fsckPath = e2fsck
	cfg.MaxRootfsUploadBytes = 24 << 20
	cfg.RootfsEnabled = true
	cfg.RootfsAllowWrite = true
	cfg.RootfsDevice = target
	cfg.UbootDevice = target
	cfg.BootDevice = target
	cfg.ExpectedParentDevice = target
	cfg.TestMode = true
	cfg.DryRun = false
	cfg.AllowFlash = true
	manager, err := NewManager(cfg, slog.New(slog.NewTextHandler(io.Discard, nil)))
	if err != nil {
		t.Fatal(err)
	}
	fake := &fakeRootfsWorkflow{rebooted: make(chan struct{})}
	manager.rootfs = fake
	t.Cleanup(manager.Close)
	return cfg, manager, target
}

func testBundleManager(t *testing.T, live bool) (Config, *Manager) {
	t.Helper()
	dir := t.TempDir()
	partition := filepath.Join(dir, "bundle.partition")
	if err := os.WriteFile(partition, make([]byte, 40<<20), 0600); err != nil {
		t.Fatal(err)
	}
	cfg := DefaultConfig()
	cfg.Token = "test-token"
	cfg.UploadDir = filepath.Join(dir, "uploads")
	cfg.BundleStageDir = filepath.Join(dir, "bundle-v1")
	cfg.MaxBundleUploadBytes = 32 << 20
	cfg.MaxRootfsUploadBytes = 24 << 20
	cfg.MaxUploadBytes = 8 << 20
	cfg.BundleEnabled = true
	cfg.BundleAllowWrite = live
	cfg.RootfsDevice = partition
	cfg.BootDevice = partition
	cfg.UbootDevice = partition
	cfg.ExpectedParentDevice = partition
	cfg.TestMode = true
	cfg.DryRun = !live
	cfg.AllowFlash = live
	manager, err := NewManager(cfg, slog.New(slog.NewTextHandler(io.Discard, nil)))
	if err != nil {
		t.Fatal(err)
	}
	if live {
		manager.bundle = &fakeBundleWorkflow{rebooted: make(chan struct{})}
	}
	t.Cleanup(manager.Close)
	return cfg, manager
}

func testExt4Image(t *testing.T, size int64) []byte {
	t.Helper()
	mkfs, err := exec.LookPath("mkfs.ext4")
	if err != nil {
		t.Skip("mkfs.ext4 unavailable")
	}
	path := filepath.Join(t.TempDir(), "rootfs.img")
	f, err := os.Create(path)
	if err != nil {
		t.Fatal(err)
	}
	if err := f.Truncate(size); err != nil {
		t.Fatal(err)
	}
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}
	if output, err := exec.Command(mkfs, "-q", "-F", path).CombinedOutput(); err != nil {
		t.Fatalf("mkfs.ext4: %v: %s", err, output)
	}
	image, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	return image
}
