package recoveryapply

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"syscall"
)

type runner struct {
	cfg         Config
	status      Status
	statusReady bool
	requestSeen bool
	requestDone bool
}

func Run(ctx context.Context, cfg Config) (Status, error) {
	if cfg.Smoke {
		return runSmoke(ctx, cfg)
	}
	r := &runner{cfg: cfg, status: newStatus(cfg)}
	if err := cfg.Validate(); err != nil {
		return r.status, err
	}
	if err := r.execute(ctx); err != nil {
		if cfg.Apply && r.requestSeen && !r.requestDone {
			if disposition, renameErr := r.moveRequest("failed"); renameErr == nil {
				r.status.RequestDisposition = disposition
			}
		}
		r.status.Phase = "failed"
		r.status.Success = false
		r.status.Error = err.Error()
		if r.statusReady {
			if statusErr := writeStatus(cfg.StatusPath, r.status); statusErr != nil {
				return r.status, fmt.Errorf("%w; persist failure status: %v", err, statusErr)
			}
		}
		return r.status, err
	}
	return r.status, nil
}

func (r *runner) execute(ctx context.Context) error {
	if err := verifyRecoveryMarker(r.cfg.RecoveryMarkerPath, r.cfg.TestMode); err != nil {
		return err
	}
	mounts, err := ReadMountInfo(r.cfg.MountInfoPath)
	if err != nil {
		return err
	}
	if !r.cfg.TestMode {
		userdata, err := ResolveDevice(r.cfg.UserdataPath, "userdata", r.cfg.ExpectedParentPath, false)
		if err != nil {
			return fmt.Errorf("validate userdata partition: %w", err)
		}
		mounted, readWrite := IsDeviceMountedAt(userdata, "/userdata", mounts)
		if !mounted {
			return errors.New("userdata partition is not mounted at /userdata")
		}
		if !readWrite {
			return errors.New("userdata partition is not mounted read-write")
		}
	}
	r.statusReady = true
	if err := r.setPhase("checking"); err != nil {
		return err
	}

	manifest, imagePath, err := LoadManifest(r.cfg.ManifestPath, r.cfg.TestMode)
	if err != nil {
		return err
	}
	if filepath.Clean(manifest.ExpectedParent) != filepath.Clean(r.cfg.ExpectedParentPath) {
		return fmt.Errorf("manifest expected_parent %s does not match configured parent %s", manifest.ExpectedParent, r.cfg.ExpectedParentPath)
	}
	r.status.ImagePath = imagePath
	r.status.ImageSize = manifest.Size
	r.status.SHA256 = manifest.SHA256
	if r.cfg.Apply {
		expectedConfirmation := ExpectedConfirmation(manifest.SHA256)
		if r.cfg.Confirmation != expectedConfirmation {
			return errors.New("confirmation does not match WRITE_ROOTFS:<manifest sha256>")
		}
		requestConfirmation, err := r.readRequest()
		if err != nil {
			return err
		}
		if requestConfirmation != expectedConfirmation {
			return errors.New("rootfs.apply does not match WRITE_ROOTFS:<manifest sha256>")
		}
	}

	imageInfo, err := secureRegularFile(imagePath, r.cfg.TestMode)
	if err != nil {
		return fmt.Errorf("image: %w", err)
	}
	if imageInfo.Size() != manifest.Size {
		return fmt.Errorf("image size %d does not match manifest size %d", imageInfo.Size(), manifest.Size)
	}
	image, err := os.Open(imagePath)
	if err != nil {
		return fmt.Errorf("open image: %w", err)
	}
	defer image.Close()
	if err := syscall.Flock(int(image.Fd()), syscall.LOCK_SH|syscall.LOCK_NB); err != nil {
		return fmt.Errorf("lock image against concurrent writes: %w", err)
	}
	defer syscall.Flock(int(image.Fd()), syscall.LOCK_UN) //nolint:errcheck

	target, err := ResolveDevice(r.cfg.TargetPath, "rootfs", r.cfg.ExpectedParentPath, r.cfg.TestMode)
	if err != nil {
		return fmt.Errorf("validate rootfs target: %w", err)
	}
	r.status.ResolvedTargetPath = target.Path
	if manifest.Size > target.Size {
		return fmt.Errorf("image size %d exceeds rootfs target capacity %d", manifest.Size, target.Size)
	}
	if target.Regular && imageInfo.Sys().(*syscall.Stat_t).Ino == target.Ino &&
		uint64(imageInfo.Sys().(*syscall.Stat_t).Dev) == target.Dev {
		return errors.New("source image and target are the same file")
	}
	if mounted, mountPoint := IsDeviceMounted(target, mounts); mounted {
		return fmt.Errorf("rootfs target is mounted at %s", mountPoint)
	}

	misc, err := ResolveDevice(r.cfg.MiscPath, "misc", r.cfg.ExpectedParentPath, r.cfg.TestMode)
	if err != nil {
		return fmt.Errorf("validate misc target: %w", err)
	}
	if misc.Size < bcbOffset+bcbSize {
		return errors.New("misc target is too small for the BCB")
	}

	ext4, err := InspectExt4(image, manifest.Size)
	if err != nil {
		return err
	}
	r.status.FilesystemSize = ext4.FilesystemSize
	e2fsckOutput, err := runE2fsck(ctx, r.cfg.E2fsckPath, imagePath)
	r.status.E2fsckSummary = e2fsckOutput
	if err != nil {
		return err
	}
	if current, err := image.Stat(); err != nil {
		return fmt.Errorf("restat image: %w", err)
	} else if !os.SameFile(imageInfo, current) || current.Size() != manifest.Size {
		return errors.New("image identity or size changed during validation")
	}
	imageSHA, err := hashOpenFile(image, manifest.Size)
	if err != nil {
		return err
	}
	if imageSHA != manifest.SHA256 {
		return fmt.Errorf("image sha256 %s does not match manifest %s", imageSHA, manifest.SHA256)
	}
	if err := r.setPhase("ready"); err != nil {
		return err
	}
	if !r.cfg.Apply {
		r.status.Success = true
		r.status.Applied = false
		r.status.Error = ""
		return writeStatus(r.cfg.StatusPath, r.status)
	}

	bcb, err := readBCB(misc)
	if err != nil {
		return err
	}
	if bcbCommand(bcb) != "boot-recovery" {
		return fmt.Errorf("BCB command %q does not authorize a recovery apply", bcbCommand(bcb))
	}

	// Re-resolve devices and mount state immediately before opening rootfs for write.
	mounts, err = ReadMountInfo(r.cfg.MountInfoPath)
	if err != nil {
		return err
	}
	recheckedTarget, err := ResolveDevice(r.cfg.TargetPath, "rootfs", r.cfg.ExpectedParentPath, r.cfg.TestMode)
	if err != nil {
		return fmt.Errorf("revalidate rootfs target: %w", err)
	}
	if !sameDevice(target, recheckedTarget) {
		return errors.New("rootfs target identity changed after validation")
	}
	if mounted, mountPoint := IsDeviceMounted(recheckedTarget, mounts); mounted {
		return fmt.Errorf("rootfs target became mounted at %s", mountPoint)
	}
	recheckedMisc, err := ResolveDevice(r.cfg.MiscPath, "misc", r.cfg.ExpectedParentPath, r.cfg.TestMode)
	if err != nil {
		return fmt.Errorf("revalidate misc target: %w", err)
	}
	if !sameDevice(misc, recheckedMisc) {
		return errors.New("misc target identity changed after validation")
	}
	if currentSHA, err := hashOpenFile(image, manifest.Size); err != nil {
		return err
	} else if currentSHA != manifest.SHA256 {
		return errors.New("image changed after validation")
	}
	if err := r.setPhase("writing"); err != nil {
		return err
	}
	written, readbackSHA, err := writeImage(ctx, image, recheckedTarget, manifest.Size, manifest.SHA256, "rootfs-writing", r.cfg.WriteProgress)
	r.status.BytesWritten = written
	r.status.ReadbackSHA256 = readbackSHA
	if err != nil {
		return err
	}
	r.status.Applied = true
	if err := r.setPhase("readback_verified"); err != nil {
		return err
	}
	disposition, err := r.moveRequest("consumed")
	if err != nil {
		return err
	}
	r.status.RequestDisposition = disposition
	r.requestDone = true
	if err := clearBCB(recheckedMisc); err != nil {
		return err
	}
	r.status.BCBCleared = true
	r.status.Phase = "succeeded"
	r.status.Success = true
	r.status.Error = ""
	return writeStatus(r.cfg.StatusPath, r.status)
}

func (r *runner) setPhase(phase string) error {
	r.status.Phase = phase
	r.status.Error = ""
	if r.cfg.Progress != nil {
		r.cfg.Progress(phase, "recovery phase entered")
	}
	return writeStatus(r.cfg.StatusPath, r.status)
}

func (r *runner) readRequest() (string, error) {
	info, err := secureRegularFile(r.cfg.RequestPath, r.cfg.TestMode)
	if err != nil {
		return "", fmt.Errorf("apply request: %w", err)
	}
	r.requestSeen = true
	if info.Size() <= 0 || info.Size() > 256 {
		return "", errors.New("rootfs.apply must be between 1 and 256 bytes")
	}
	data, err := os.ReadFile(r.cfg.RequestPath)
	if err != nil {
		return "", fmt.Errorf("read apply request: %w", err)
	}
	value := strings.TrimSpace(string(data))
	if strings.ContainsAny(value, "\r\n") {
		return "", errors.New("rootfs.apply must contain exactly one confirmation line")
	}
	return value, nil
}

func (r *runner) moveRequest(disposition string) (string, error) {
	destination := r.cfg.RequestPath + "." + disposition
	if err := os.Rename(r.cfg.RequestPath, destination); err != nil {
		return "", fmt.Errorf("mark apply request %s: %w", disposition, err)
	}
	dir, err := os.Open(filepath.Dir(r.cfg.RequestPath))
	if err != nil {
		return "", fmt.Errorf("open request directory: %w", err)
	}
	defer dir.Close()
	if err := dir.Sync(); err != nil {
		return "", fmt.Errorf("sync request directory: %w", err)
	}
	return destination, nil
}

func verifyRecoveryMarker(path string, testMode bool) error {
	info, err := secureRegularFile(path, testMode)
	if err != nil {
		return fmt.Errorf("recovery marker: %w", err)
	}
	if info.Size() != int64(len(RecoveryMarker)) {
		return errors.New("invalid recovery marker size")
	}
	data, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("read recovery marker: %w", err)
	}
	if string(data) != RecoveryMarker {
		return errors.New("invalid recovery marker content")
	}
	return nil
}

func runE2fsck(ctx context.Context, binary, imagePath string) (string, error) {
	command := exec.CommandContext(ctx, binary, "-f", "-n", imagePath)
	command.Env = []string{"LC_ALL=C", "PATH=/usr/sbin:/usr/bin:/sbin:/bin"}
	var output bytes.Buffer
	command.Stdout = &output
	command.Stderr = &output
	err := command.Run()
	summary := strings.TrimSpace(output.String())
	if len(summary) > 4096 {
		summary = summary[len(summary)-4096:]
	}
	if err != nil {
		return summary, fmt.Errorf("e2fsck -f -n failed: %w: %s", err, summary)
	}
	return summary, nil
}

func sameDevice(left, right Device) bool {
	if left.Regular != right.Regular || left.Path != right.Path || left.Size != right.Size {
		return false
	}
	if left.Regular {
		return left.Dev == right.Dev && left.Ino == right.Ino
	}
	return left.Rdev == right.Rdev && left.ParentPath == right.ParentPath && left.PartName == right.PartName
}
