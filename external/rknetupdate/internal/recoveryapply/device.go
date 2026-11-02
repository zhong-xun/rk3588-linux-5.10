package recoveryapply

import (
	"bufio"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"unsafe"
)

const blkgetsize64 = 0x80081272

type Device struct {
	Configured string
	Path       string
	ParentPath string
	PartName   string
	PartNumber int
	StartLBA   uint64
	SizeLBA    uint64
	Size       int64
	Regular    bool
	Major      uint64
	Minor      uint64
	Rdev       uint64
	Dev        uint64
	Ino        uint64
}

func ResolveDevice(path, expectedLabel, expectedParent string, testMode bool) (Device, error) {
	if path == "" || !filepath.IsAbs(path) {
		return Device{}, errors.New("device path must be absolute")
	}
	configured := filepath.Clean(path)
	lstat, err := os.Lstat(configured)
	if err != nil {
		return Device{}, fmt.Errorf("device unavailable: %w", err)
	}
	resolved := configured
	if lstat.Mode()&os.ModeSymlink != 0 {
		if testMode || !strings.HasPrefix(configured, "/dev/disk/by-partlabel/") {
			return Device{}, errors.New("device symlinks are only accepted for production PARTLABEL paths")
		}
		resolved, err = filepath.EvalSymlinks(configured)
		if err != nil {
			return Device{}, fmt.Errorf("resolve device symlink: %w", err)
		}
	}
	info, err := os.Stat(resolved)
	if err != nil {
		return Device{}, fmt.Errorf("inspect resolved device: %w", err)
	}
	stat, ok := info.Sys().(*syscall.Stat_t)
	if !ok {
		return Device{}, errors.New("cannot inspect device metadata")
	}
	device := Device{
		Configured: configured,
		Path:       resolved,
		Size:       info.Size(),
		Dev:        uint64(stat.Dev),
		Ino:        stat.Ino,
		Rdev:       uint64(stat.Rdev),
	}
	if info.Mode().IsRegular() {
		if !testMode || resolved != configured {
			return Device{}, errors.New("regular devices require test mode and a direct path")
		}
		device.Regular = true
		device.ParentPath = filepath.Clean(expectedParent)
		return device, nil
	}
	if info.Mode()&os.ModeDevice == 0 || info.Mode()&os.ModeCharDevice != 0 {
		return Device{}, errors.New("target is not a block device")
	}
	device.Major, device.Minor = linuxMajorMinor(device.Rdev)
	sysPath, err := filepath.EvalSymlinks(fmt.Sprintf("/sys/dev/block/%d:%d", device.Major, device.Minor))
	if err != nil {
		return Device{}, fmt.Errorf("resolve device sysfs path: %w", err)
	}
	if _, err := os.Stat(filepath.Join(sysPath, "partition")); err != nil {
		return Device{}, errors.New("target is a whole disk or lacks a kernel partition marker")
	}
	partNumber, err := readSysfsUint(filepath.Join(sysPath, "partition"), "partition number")
	if err != nil || partNumber == 0 || partNumber > uint64(^uint(0)>>1) {
		if err != nil {
			return Device{}, err
		}
		return Device{}, errors.New("invalid kernel partition number")
	}
	device.PartNumber = int(partNumber)
	device.StartLBA, err = readSysfsUint(filepath.Join(sysPath, "start"), "partition start")
	if err != nil {
		return Device{}, err
	}
	device.SizeLBA, err = readSysfsUint(filepath.Join(sysPath, "size"), "partition size")
	if err != nil || device.SizeLBA == 0 {
		if err != nil {
			return Device{}, err
		}
		return Device{}, errors.New("invalid kernel partition size")
	}
	uevent, err := readUevent(filepath.Join(sysPath, "uevent"))
	if err != nil {
		return Device{}, err
	}
	device.PartName = uevent["PARTNAME"]
	if device.PartName != expectedLabel {
		return Device{}, fmt.Errorf("kernel PARTNAME %q does not match expected label %q", device.PartName, expectedLabel)
	}
	if partNumberText := uevent["PARTN"]; partNumberText != "" {
		partNumberUevent, err := strconv.ParseUint(partNumberText, 10, 64)
		if err != nil || partNumberUevent != partNumber {
			return Device{}, errors.New("kernel partition number metadata is inconsistent")
		}
	}
	parentName := filepath.Base(filepath.Dir(sysPath))
	if parentName == "" || parentName == "." || parentName == "block" || parentName == filepath.Base(sysPath) {
		return Device{}, fmt.Errorf("cannot determine parent disk from %s", sysPath)
	}
	device.ParentPath = filepath.Join("/dev", parentName)
	if filepath.Clean(device.ParentPath) != filepath.Clean(expectedParent) {
		return Device{}, fmt.Errorf("partition parent %s does not match expected parent %s", device.ParentPath, filepath.Clean(expectedParent))
	}
	if testMode && !strings.HasPrefix(parentName, "loop") {
		return Device{}, errors.New("test mode block devices must be backed by /dev/loop*")
	}
	parentInfo, err := os.Lstat(device.ParentPath)
	if err != nil {
		return Device{}, fmt.Errorf("inspect parent block device: %w", err)
	}
	if parentInfo.Mode()&os.ModeSymlink != 0 || parentInfo.Mode()&os.ModeDevice == 0 || parentInfo.Mode()&os.ModeCharDevice != 0 {
		return Device{}, errors.New("expected parent is not a direct whole-disk block device")
	}
	parentStat := parentInfo.Sys().(*syscall.Stat_t)
	parentMajor, parentMinor := linuxMajorMinor(uint64(parentStat.Rdev))
	parentSys, err := filepath.EvalSymlinks(fmt.Sprintf("/sys/dev/block/%d:%d", parentMajor, parentMinor))
	if err != nil || filepath.Clean(parentSys) != filepath.Dir(filepath.Clean(sysPath)) {
		return Device{}, errors.New("partition sysfs parent does not match expected parent device")
	}
	f, err := os.Open(resolved)
	if err != nil {
		return Device{}, fmt.Errorf("open block device: %w", err)
	}
	defer f.Close()
	var size uint64
	_, _, errno := syscall.Syscall(syscall.SYS_IOCTL, f.Fd(), blkgetsize64, uintptr(unsafe.Pointer(&size)))
	if errno != 0 {
		return Device{}, fmt.Errorf("read block device size: %w", errno)
	}
	if size > uint64(^uint64(0)>>1) {
		return Device{}, errors.New("block device is too large")
	}
	device.Size = int64(size)
	if device.SizeLBA > ^uint64(0)/512 || device.SizeLBA*512 != size {
		return Device{}, errors.New("kernel partition size does not match block device size")
	}
	return device, nil
}

// ResolveRecoveryPartition keeps recovery migrations available while udev is
// rebuilding PARTLABEL links after a GPT reread. The fallback is accepted only
// for a missing canonical link and only after the direct kernel partition is
// matched against both sysfs identity and the CRC-validated on-disk GPT.
func ResolveRecoveryPartition(path, expectedLabel, expectedParent string, testMode bool) (Device, error) {
	device, primaryErr := ResolveDevice(path, expectedLabel, expectedParent, testMode)
	if primaryErr == nil {
		return device, nil
	}
	configured := filepath.Clean(path)
	expectedLink := filepath.Join("/dev/disk/by-partlabel", expectedLabel)
	if testMode || configured != expectedLink || !errors.Is(primaryErr, os.ErrNotExist) {
		return Device{}, primaryErr
	}
	device, fallbackErr := resolvePartitionFromGPT(expectedLabel, expectedParent)
	if fallbackErr != nil {
		return Device{}, fmt.Errorf("%v; validated GPT fallback failed: %w", primaryErr, fallbackErr)
	}
	return device, nil
}

func resolvePartitionFromGPT(expectedLabel, expectedParent string) (Device, error) {
	table, err := ReadGPT(expectedParent, false)
	if err != nil {
		return Device{}, fmt.Errorf("read parent GPT: %w", err)
	}
	var expected GPTPartition
	matches := 0
	for _, partition := range table.Partitions {
		if partition.Name == expectedLabel {
			expected = partition
			matches++
		}
	}
	if matches != 1 {
		return Device{}, fmt.Errorf("GPT contains %d partitions named %q", matches, expectedLabel)
	}

	parentInfo, err := os.Lstat(filepath.Clean(expectedParent))
	if err != nil {
		return Device{}, fmt.Errorf("inspect fallback parent block device: %w", err)
	}
	if parentInfo.Mode()&os.ModeSymlink != 0 || parentInfo.Mode()&os.ModeDevice == 0 || parentInfo.Mode()&os.ModeCharDevice != 0 {
		return Device{}, errors.New("fallback parent is not a direct whole-disk block device")
	}
	parentStat, ok := parentInfo.Sys().(*syscall.Stat_t)
	if !ok {
		return Device{}, errors.New("cannot inspect fallback parent metadata")
	}
	parentMajor, parentMinor := linuxMajorMinor(uint64(parentStat.Rdev))
	parentSys, err := filepath.EvalSymlinks(fmt.Sprintf("/sys/dev/block/%d:%d", parentMajor, parentMinor))
	if err != nil {
		return Device{}, fmt.Errorf("resolve fallback parent sysfs path: %w", err)
	}
	entries, err := os.ReadDir(parentSys)
	if err != nil {
		return Device{}, fmt.Errorf("scan fallback parent partitions: %w", err)
	}

	var candidatePath string
	for _, entry := range entries {
		childSys := filepath.Join(parentSys, entry.Name())
		partNumber, err := readSysfsUint(filepath.Join(childSys, "partition"), "partition number")
		if err != nil || int(partNumber) != expected.Number {
			continue
		}
		uevent, err := readUevent(filepath.Join(childSys, "uevent"))
		if err != nil || uevent["PARTNAME"] != expectedLabel {
			continue
		}
		devName := uevent["DEVNAME"]
		if devName == "" || filepath.Base(devName) != devName {
			return Device{}, errors.New("kernel fallback partition has an invalid DEVNAME")
		}
		if candidatePath != "" {
			return Device{}, errors.New("multiple kernel partitions match the GPT fallback identity")
		}
		candidatePath = filepath.Join("/dev", devName)
	}
	if candidatePath == "" {
		return Device{}, errors.New("kernel partition for the GPT fallback identity is unavailable")
	}
	device, err := ResolveDevice(candidatePath, expectedLabel, expectedParent, false)
	if err != nil {
		return Device{}, fmt.Errorf("validate direct fallback partition: %w", err)
	}
	if err := validateDeviceGPTIdentity(device, expected, table.SectorSize); err != nil {
		return Device{}, err
	}
	return device, nil
}

func validateDeviceGPTIdentity(device Device, partition GPTPartition, logicalSectorSize uint64) error {
	if device.Regular || partition.Number != device.PartNumber || partition.Name != device.PartName {
		return errors.New("direct fallback partition identity does not match GPT")
	}
	if logicalSectorSize < 512 || logicalSectorSize > 4096 || logicalSectorSize&(logicalSectorSize-1) != 0 {
		return errors.New("invalid GPT logical sector size for fallback validation")
	}
	if partition.FirstLBA > ^uint64(0)/logicalSectorSize || partition.SizeSectors() > ^uint64(0)/logicalSectorSize ||
		device.StartLBA > ^uint64(0)/512 || device.SizeLBA > ^uint64(0)/512 {
		return errors.New("partition geometry overflows fallback validation")
	}
	gptStartBytes := partition.FirstLBA * logicalSectorSize
	gptSizeBytes := partition.SizeSectors() * logicalSectorSize
	if gptStartBytes != device.StartLBA*512 || gptSizeBytes != device.SizeLBA*512 ||
		device.Size < 0 || gptSizeBytes != uint64(device.Size) {
		return errors.New("direct fallback partition geometry does not match GPT")
	}
	return nil
}

func readSysfsUint(path, field string) (uint64, error) {
	value, err := os.ReadFile(path)
	if err != nil {
		return 0, fmt.Errorf("read kernel %s: %w", field, err)
	}
	parsed, err := strconv.ParseUint(strings.TrimSpace(string(value)), 10, 64)
	if err != nil {
		return 0, fmt.Errorf("parse kernel %s: %w", field, err)
	}
	return parsed, nil
}

func readUevent(path string) (map[string]string, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("read partition uevent: %w", err)
	}
	defer f.Close()
	values := make(map[string]string)
	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		key, value, ok := strings.Cut(scanner.Text(), "=")
		if ok {
			values[key] = value
		}
	}
	if err := scanner.Err(); err != nil {
		return nil, fmt.Errorf("scan partition uevent: %w", err)
	}
	return values, nil
}

func linuxMajorMinor(dev uint64) (uint64, uint64) {
	major := (dev >> 8) & 0xfff
	major |= (dev >> 32) & ^uint64(0xfff)
	minor := dev & 0xff
	minor |= (dev >> 12) & ^uint64(0xff)
	return major, minor
}

type mountRecord struct {
	Major      uint64
	Minor      uint64
	MountPoint string
	Options    string
	Source     string
}

func ReadMountInfo(path string) ([]mountRecord, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("open mountinfo: %w", err)
	}
	defer f.Close()
	var records []mountRecord
	scanner := bufio.NewScanner(f)
	scanner.Buffer(make([]byte, 4096), 1<<20)
	for scanner.Scan() {
		fields := strings.Fields(scanner.Text())
		if len(fields) < 10 {
			return nil, errors.New("malformed mountinfo record")
		}
		separator := -1
		for i, field := range fields {
			if field == "-" {
				separator = i
				break
			}
		}
		if separator < 6 || separator+2 >= len(fields) {
			return nil, errors.New("malformed mountinfo separator")
		}
		majorText, minorText, ok := strings.Cut(fields[2], ":")
		if !ok {
			return nil, errors.New("malformed mountinfo device number")
		}
		major, err := strconv.ParseUint(majorText, 10, 64)
		if err != nil {
			return nil, fmt.Errorf("parse mountinfo major: %w", err)
		}
		minor, err := strconv.ParseUint(minorText, 10, 64)
		if err != nil {
			return nil, fmt.Errorf("parse mountinfo minor: %w", err)
		}
		records = append(records, mountRecord{
			Major:      major,
			Minor:      minor,
			MountPoint: unescapeMountInfo(fields[4]),
			Options:    fields[5],
			Source:     unescapeMountInfo(fields[separator+2]),
		})
	}
	if err := scanner.Err(); err != nil {
		return nil, fmt.Errorf("scan mountinfo: %w", err)
	}
	return records, nil
}

func IsDeviceMounted(device Device, records []mountRecord) (bool, string) {
	for _, record := range records {
		if !device.Regular && record.Major == device.Major && record.Minor == device.Minor {
			return true, record.MountPoint
		}
		if filepath.Clean(record.Source) == filepath.Clean(device.Path) ||
			filepath.Clean(record.Source) == filepath.Clean(device.Configured) {
			return true, record.MountPoint
		}
	}
	return false, ""
}

func IsDeviceMountedAt(device Device, mountPoint string, records []mountRecord) (bool, bool) {
	for _, record := range records {
		deviceMatch := (!device.Regular && record.Major == device.Major && record.Minor == device.Minor) ||
			filepath.Clean(record.Source) == filepath.Clean(device.Path) ||
			filepath.Clean(record.Source) == filepath.Clean(device.Configured)
		if deviceMatch && filepath.Clean(record.MountPoint) == filepath.Clean(mountPoint) {
			rw := false
			for _, option := range strings.Split(record.Options, ",") {
				if option == "rw" {
					rw = true
				}
			}
			return true, rw
		}
	}
	return false, false
}

func unescapeMountInfo(value string) string {
	replacer := strings.NewReplacer(`\040`, " ", `\011`, "\t", `\012`, "\n", `\134`, `\`)
	return replacer.Replace(value)
}
