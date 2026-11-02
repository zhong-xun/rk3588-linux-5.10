package update

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"unsafe"
)

const blkgetsize64 = 0x80081272

type ResolvedTarget struct {
	Configured string
	Path       string
	ParentPath string
	Size       int64
	Regular    bool
}

func ResolveTarget(path string, testMode bool) (ResolvedTarget, error) {
	if path == "" || !filepath.IsAbs(path) {
		return ResolvedTarget{}, errors.New("partition path must be absolute")
	}
	configured := filepath.Clean(path)
	info, err := os.Lstat(configured)
	if err != nil {
		return ResolvedTarget{}, fmt.Errorf("partition unavailable: %w", err)
	}
	resolved := configured
	if info.Mode()&os.ModeSymlink != 0 {
		if !strings.HasPrefix(configured, "/dev/disk/by-partlabel/") {
			return ResolvedTarget{}, errors.New("symbolic links are only accepted under /dev/disk/by-partlabel")
		}
		resolved, err = filepath.EvalSymlinks(configured)
		if err != nil {
			return ResolvedTarget{}, fmt.Errorf("cannot resolve partition label: %w", err)
		}
		info, err = os.Stat(resolved)
		if err != nil {
			return ResolvedTarget{}, fmt.Errorf("resolved partition unavailable: %w", err)
		}
	}

	if info.Mode().IsRegular() {
		if !testMode || resolved != configured {
			return ResolvedTarget{}, errors.New("regular files require test_mode=true and an explicit non-symlink path")
		}
		return ResolvedTarget{Configured: configured, Path: resolved, ParentPath: resolved, Size: info.Size(), Regular: true}, nil
	}
	if info.Mode()&os.ModeDevice == 0 || info.Mode()&os.ModeCharDevice != 0 {
		return ResolvedTarget{}, errors.New("target is not a block device")
	}
	stat, ok := info.Sys().(*syscall.Stat_t)
	if !ok {
		return ResolvedTarget{}, errors.New("cannot inspect block device metadata")
	}
	major, minor := linuxMajorMinor(uint64(stat.Rdev))
	partitionMarker := fmt.Sprintf("/sys/dev/block/%d:%d/partition", major, minor)
	if _, err := os.Stat(partitionMarker); err != nil {
		return ResolvedTarget{}, errors.New("target is a whole disk or lacks a kernel partition marker")
	}
	parentPath, err := parentDeviceFromSysfs(major, minor)
	if err != nil {
		return ResolvedTarget{}, err
	}
	f, err := os.Open(resolved)
	if err != nil {
		return ResolvedTarget{}, fmt.Errorf("cannot open partition: %w", err)
	}
	defer f.Close()
	var size uint64
	_, _, errno := syscall.Syscall(syscall.SYS_IOCTL, f.Fd(), blkgetsize64, uintptr(unsafe.Pointer(&size)))
	if errno != 0 {
		return ResolvedTarget{}, fmt.Errorf("cannot read partition size: %w", errno)
	}
	return ResolvedTarget{Configured: configured, Path: resolved, ParentPath: parentPath, Size: int64(size)}, nil
}

func ValidateExpectedParentDevice(path string, testMode bool) (string, error) {
	if path == "" || !filepath.IsAbs(path) {
		return "", errors.New("expected parent device must be an absolute path")
	}
	cleaned := filepath.Clean(path)
	info, err := os.Lstat(cleaned)
	if err != nil {
		return "", fmt.Errorf("expected parent device unavailable: %w", err)
	}
	if info.Mode()&os.ModeSymlink != 0 {
		return "", errors.New("expected parent device must not be a symbolic link")
	}
	if info.Mode().IsRegular() {
		if !testMode {
			return "", errors.New("expected parent device is not a block device")
		}
		return cleaned, nil
	}
	if info.Mode()&os.ModeDevice == 0 || info.Mode()&os.ModeCharDevice != 0 {
		return "", errors.New("expected parent device is not a block device")
	}
	stat, ok := info.Sys().(*syscall.Stat_t)
	if !ok {
		return "", errors.New("cannot inspect expected parent device metadata")
	}
	major, minor := linuxMajorMinor(uint64(stat.Rdev))
	partitionMarker := fmt.Sprintf("/sys/dev/block/%d:%d/partition", major, minor)
	if _, err := os.Stat(partitionMarker); err == nil {
		return "", errors.New("expected parent device must be a whole disk, not a partition")
	} else if !errors.Is(err, os.ErrNotExist) {
		return "", fmt.Errorf("inspect expected parent device: %w", err)
	}
	return cleaned, nil
}

func ValidateTargetParent(target ResolvedTarget, expectedParent string) error {
	if expectedParent == "" {
		return errors.New("expected parent device is required")
	}
	if filepath.Clean(expectedParent) != target.ParentPath {
		return fmt.Errorf("partition parent %s does not match expected parent device %s", target.ParentPath, filepath.Clean(expectedParent))
	}
	return nil
}

func parentDeviceFromSysfs(major, minor uint64) (string, error) {
	sysfsPath, err := filepath.EvalSymlinks(fmt.Sprintf("/sys/dev/block/%d:%d", major, minor))
	if err != nil {
		return "", fmt.Errorf("cannot resolve partition sysfs path: %w", err)
	}
	parentName, err := parentBlockName(sysfsPath)
	if err != nil {
		return "", err
	}
	parentPath := filepath.Join("/dev", parentName)
	info, err := os.Lstat(parentPath)
	if err != nil {
		return "", fmt.Errorf("parent block device %s unavailable: %w", parentPath, err)
	}
	if info.Mode()&os.ModeSymlink != 0 || info.Mode()&os.ModeDevice == 0 || info.Mode()&os.ModeCharDevice != 0 {
		return "", fmt.Errorf("parent path %s is not a direct block device", parentPath)
	}
	parentStat, ok := info.Sys().(*syscall.Stat_t)
	if !ok {
		return "", errors.New("cannot inspect parent block device metadata")
	}
	parentMajor, parentMinor := linuxMajorMinor(uint64(parentStat.Rdev))
	parentSysfs, err := filepath.EvalSymlinks(fmt.Sprintf("/sys/dev/block/%d:%d", parentMajor, parentMinor))
	if err != nil {
		return "", fmt.Errorf("cannot resolve parent sysfs path: %w", err)
	}
	if filepath.Clean(parentSysfs) != filepath.Dir(filepath.Clean(sysfsPath)) {
		return "", errors.New("partition sysfs parent does not match the resolved parent block device")
	}
	return parentPath, nil
}

func parentBlockName(partitionSysfsPath string) (string, error) {
	cleaned := filepath.Clean(partitionSysfsPath)
	partitionName := filepath.Base(cleaned)
	parentName := filepath.Base(filepath.Dir(cleaned))
	if parentName == "." || parentName == string(filepath.Separator) || parentName == "block" || parentName == partitionName {
		return "", fmt.Errorf("cannot determine parent disk from sysfs path %s", partitionSysfsPath)
	}
	return parentName, nil
}

func linuxMajorMinor(dev uint64) (uint64, uint64) {
	major := (dev >> 8) & 0xfff
	major |= (dev >> 32) & ^uint64(0xfff)
	minor := dev & 0xff
	minor |= (dev >> 12) & ^uint64(0xff)
	return major, minor
}
