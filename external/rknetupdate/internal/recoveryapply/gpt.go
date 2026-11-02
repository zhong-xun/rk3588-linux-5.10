package recoveryapply

import (
	"bytes"
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"hash/crc32"
	"os"
	"os/exec"
	"sort"
	"strings"
	"syscall"
	"unicode/utf16"
	"unsafe"
)

const (
	gptHeaderMinSize        = 92
	gptPartitionEntryMin    = 128
	linuxFilesystemTypeGUID = "0fc63daf-8483-4772-8e79-3d69d8477de4"
	blksszget               = 0x1268
)

type GPTPartition struct {
	Number     int
	TypeGUID   string
	UniqueGUID string
	FirstLBA   uint64
	LastLBA    uint64
	Name       string
}

func (p GPTPartition) SizeSectors() uint64 {
	if p.LastLBA < p.FirstLBA {
		return 0
	}
	return p.LastLBA - p.FirstLBA + 1
}

type GPTTable struct {
	SectorSize  uint64
	DiskBytes   uint64
	DiskGUID    string
	FirstUsable uint64
	LastUsable  uint64
	Partitions  map[int]GPTPartition
}

func (t GPTTable) Partition(number int) (GPTPartition, error) {
	partition, ok := t.Partitions[number]
	if !ok {
		return GPTPartition{}, fmt.Errorf("GPT partition %d is missing", number)
	}
	return partition, nil
}

type gptHeader struct {
	CurrentLBA      uint64
	BackupLBA       uint64
	FirstUsable     uint64
	LastUsable      uint64
	DiskGUID        string
	EntriesLBA      uint64
	EntryCount      uint32
	EntrySize       uint32
	EntriesChecksum uint32
}

func ReadGPT(path string, testMode bool) (GPTTable, error) {
	f, err := openDiskReadOnly(path, testMode)
	if err != nil {
		return GPTTable{}, err
	}
	defer f.Close()
	diskBytes, sectorSize, err := diskGeometry(f)
	if err != nil {
		return GPTTable{}, err
	}
	if diskBytes < sectorSize*3 || diskBytes%sectorSize != 0 {
		return GPTTable{}, errors.New("disk size is incompatible with its logical sector size")
	}
	primary, primaryEntries, err := readGPTCopy(f, sectorSize, 1, diskBytes/sectorSize)
	if err != nil {
		return GPTTable{}, fmt.Errorf("primary GPT: %w", err)
	}
	backup, backupEntries, err := readGPTCopy(f, sectorSize, primary.BackupLBA, diskBytes/sectorSize)
	if err != nil {
		return GPTTable{}, fmt.Errorf("backup GPT: %w", err)
	}
	if primary.CurrentLBA != 1 || backup.CurrentLBA != primary.BackupLBA || backup.BackupLBA != primary.CurrentLBA {
		return GPTTable{}, errors.New("primary and backup GPT header locations are inconsistent")
	}
	if primary.DiskGUID != backup.DiskGUID || primary.FirstUsable != backup.FirstUsable || primary.LastUsable != backup.LastUsable ||
		primary.EntryCount != backup.EntryCount || primary.EntrySize != backup.EntrySize {
		return GPTTable{}, errors.New("primary and backup GPT geometry differs")
	}
	if !bytes.Equal(primaryEntries, backupEntries) {
		return GPTTable{}, errors.New("primary and backup GPT partition arrays differ")
	}
	partitions, err := decodeGPTPartitions(primaryEntries, primary.EntryCount, primary.EntrySize, primary.FirstUsable, primary.LastUsable)
	if err != nil {
		return GPTTable{}, err
	}
	return GPTTable{
		SectorSize:  sectorSize,
		DiskBytes:   diskBytes,
		DiskGUID:    primary.DiskGUID,
		FirstUsable: primary.FirstUsable,
		LastUsable:  primary.LastUsable,
		Partitions:  partitions,
	}, nil
}

func openDiskReadOnly(path string, testMode bool) (*os.File, error) {
	info, err := os.Lstat(path)
	if err != nil {
		return nil, fmt.Errorf("inspect GPT disk: %w", err)
	}
	if info.Mode()&os.ModeSymlink != 0 {
		return nil, errors.New("GPT disk path must not be a symlink")
	}
	if info.Mode().IsRegular() {
		if !testMode {
			return nil, errors.New("production GPT disk must be a block device")
		}
	} else if info.Mode()&os.ModeDevice == 0 || info.Mode()&os.ModeCharDevice != 0 {
		return nil, errors.New("GPT disk is not a block device")
	}
	fd, err := syscall.Open(path, syscall.O_RDONLY|syscall.O_CLOEXEC|syscall.O_NOFOLLOW, 0)
	if err != nil {
		return nil, fmt.Errorf("open GPT disk: %w", err)
	}
	return os.NewFile(uintptr(fd), path), nil
}

func diskGeometry(f *os.File) (uint64, uint64, error) {
	info, err := f.Stat()
	if err != nil {
		return 0, 0, fmt.Errorf("stat GPT disk: %w", err)
	}
	if info.Mode().IsRegular() {
		return uint64(info.Size()), 512, nil
	}
	var bytesSize uint64
	if _, _, errno := syscall.Syscall(syscall.SYS_IOCTL, f.Fd(), blkgetsize64, uintptr(unsafe.Pointer(&bytesSize))); errno != 0 {
		return 0, 0, fmt.Errorf("read GPT disk size: %w", errno)
	}
	var sectorSize uint32
	if _, _, errno := syscall.Syscall(syscall.SYS_IOCTL, f.Fd(), blksszget, uintptr(unsafe.Pointer(&sectorSize))); errno != 0 {
		return 0, 0, fmt.Errorf("read GPT logical sector size: %w", errno)
	}
	if sectorSize < 512 || sectorSize > 4096 || sectorSize&(sectorSize-1) != 0 {
		return 0, 0, fmt.Errorf("unsupported GPT logical sector size %d", sectorSize)
	}
	return bytesSize, uint64(sectorSize), nil
}

func readGPTCopy(f *os.File, sectorSize, headerLBA, diskSectors uint64) (gptHeader, []byte, error) {
	if headerLBA >= diskSectors {
		return gptHeader{}, nil, errors.New("header LBA is outside disk")
	}
	sector := make([]byte, sectorSize)
	if _, err := f.ReadAt(sector, int64(headerLBA*sectorSize)); err != nil {
		return gptHeader{}, nil, fmt.Errorf("read header: %w", err)
	}
	if string(sector[:8]) != "EFI PART" {
		return gptHeader{}, nil, errors.New("missing EFI PART signature")
	}
	headerSize := binary.LittleEndian.Uint32(sector[12:16])
	if headerSize < gptHeaderMinSize || uint64(headerSize) > sectorSize {
		return gptHeader{}, nil, fmt.Errorf("invalid header size %d", headerSize)
	}
	headerBytes := append([]byte(nil), sector[:headerSize]...)
	wantHeaderCRC := binary.LittleEndian.Uint32(headerBytes[16:20])
	clear(headerBytes[16:20])
	if got := crc32.ChecksumIEEE(headerBytes); got != wantHeaderCRC {
		return gptHeader{}, nil, fmt.Errorf("header CRC32 %08x does not match %08x", got, wantHeaderCRC)
	}
	header := gptHeader{
		CurrentLBA:      binary.LittleEndian.Uint64(sector[24:32]),
		BackupLBA:       binary.LittleEndian.Uint64(sector[32:40]),
		FirstUsable:     binary.LittleEndian.Uint64(sector[40:48]),
		LastUsable:      binary.LittleEndian.Uint64(sector[48:56]),
		DiskGUID:        formatGPTGUID(sector[56:72]),
		EntriesLBA:      binary.LittleEndian.Uint64(sector[72:80]),
		EntryCount:      binary.LittleEndian.Uint32(sector[80:84]),
		EntrySize:       binary.LittleEndian.Uint32(sector[84:88]),
		EntriesChecksum: binary.LittleEndian.Uint32(sector[88:92]),
	}
	if header.CurrentLBA != headerLBA || header.FirstUsable > header.LastUsable || header.LastUsable >= diskSectors {
		return gptHeader{}, nil, errors.New("invalid GPT header LBA geometry")
	}
	if header.EntryCount == 0 || header.EntryCount > 4096 || header.EntrySize < gptPartitionEntryMin || header.EntrySize > 4096 || header.EntrySize%8 != 0 {
		return gptHeader{}, nil, errors.New("invalid GPT partition array geometry")
	}
	entriesBytes := uint64(header.EntryCount) * uint64(header.EntrySize)
	if entriesBytes > 16<<20 || header.EntriesLBA >= diskSectors || entriesBytes > (diskSectors-header.EntriesLBA)*sectorSize {
		return gptHeader{}, nil, errors.New("GPT partition array is outside disk")
	}
	entries := make([]byte, entriesBytes)
	if _, err := f.ReadAt(entries, int64(header.EntriesLBA*sectorSize)); err != nil {
		return gptHeader{}, nil, fmt.Errorf("read partition array: %w", err)
	}
	if got := crc32.ChecksumIEEE(entries); got != header.EntriesChecksum {
		return gptHeader{}, nil, fmt.Errorf("partition array CRC32 %08x does not match %08x", got, header.EntriesChecksum)
	}
	return header, entries, nil
}

func decodeGPTPartitions(entries []byte, count, entrySize uint32, firstUsable, lastUsable uint64) (map[int]GPTPartition, error) {
	partitions := make(map[int]GPTPartition)
	ordered := make([]GPTPartition, 0)
	for i := uint32(0); i < count; i++ {
		entry := entries[uint64(i)*uint64(entrySize) : uint64(i+1)*uint64(entrySize)]
		if allZero(entry[:16]) {
			continue
		}
		partition := GPTPartition{
			Number:     int(i + 1),
			TypeGUID:   formatGPTGUID(entry[:16]),
			UniqueGUID: formatGPTGUID(entry[16:32]),
			FirstLBA:   binary.LittleEndian.Uint64(entry[32:40]),
			LastLBA:    binary.LittleEndian.Uint64(entry[40:48]),
			Name:       decodeGPTName(entry[56:minInt(len(entry), 128)]),
		}
		if partition.FirstLBA < firstUsable || partition.LastLBA > lastUsable || partition.FirstLBA > partition.LastLBA {
			return nil, fmt.Errorf("GPT partition %d is outside usable LBAs", partition.Number)
		}
		if partition.UniqueGUID == "00000000-0000-0000-0000-000000000000" {
			return nil, fmt.Errorf("GPT partition %d has an empty unique GUID", partition.Number)
		}
		partitions[partition.Number] = partition
		ordered = append(ordered, partition)
	}
	sort.Slice(ordered, func(i, j int) bool { return ordered[i].FirstLBA < ordered[j].FirstLBA })
	for i := 1; i < len(ordered); i++ {
		if ordered[i].FirstLBA <= ordered[i-1].LastLBA {
			return nil, fmt.Errorf("GPT partitions %d and %d overlap", ordered[i-1].Number, ordered[i].Number)
		}
	}
	return partitions, nil
}

func formatGPTGUID(value []byte) string {
	if len(value) < 16 {
		return ""
	}
	return fmt.Sprintf("%08x-%04x-%04x-%02x%02x-%012x",
		binary.LittleEndian.Uint32(value[0:4]),
		binary.LittleEndian.Uint16(value[4:6]),
		binary.LittleEndian.Uint16(value[6:8]),
		value[8], value[9], value[10:16])
}

func decodeGPTName(value []byte) string {
	words := make([]uint16, 0, len(value)/2)
	for i := 0; i+1 < len(value); i += 2 {
		word := binary.LittleEndian.Uint16(value[i : i+2])
		if word == 0 {
			break
		}
		words = append(words, word)
	}
	return string(utf16.Decode(words))
}

func minInt(a, b int) int {
	if a < b {
		return a
	}
	return b
}

type GPTMutator struct {
	SGDiskPath    string
	PartprobePath string
	TestMode      bool
	ExtraEnv      []string
}

func (m GPTMutator) CreateStage(ctx context.Context, disk string, original GPTPartition, p6End, p7Start, lastUsable uint64) error {
	if err := validateRootfsPartition(original); err != nil {
		return err
	}
	if p6End < original.FirstLBA || p7Start != p6End+1 || p7Start > lastUsable {
		return errors.New("invalid staged GPT geometry")
	}
	args := []string{
		"--delete=6",
		fmt.Sprintf("--new=6:%d:%d", original.FirstLBA, p6End),
		"--typecode=6:" + original.TypeGUID,
		"--partition-guid=6:" + original.UniqueGUID,
		"--change-name=6:" + original.Name,
		fmt.Sprintf("--new=7:%d:%d", p7Start, lastUsable),
		"--typecode=7:" + linuxFilesystemTypeGUID,
		"--change-name=7:userdata",
		disk,
	}
	return m.mutate(ctx, disk, args...)
}

func (m GPTMutator) RestoreRootfs(ctx context.Context, disk string, original GPTPartition, lastUsable uint64, p7Present bool) error {
	if err := validateRootfsPartition(original); err != nil {
		return err
	}
	if lastUsable < original.FirstLBA {
		return errors.New("invalid restored GPT geometry")
	}
	args := []string{}
	if p7Present {
		args = append(args, "--delete=7")
	}
	args = append(args,
		"--delete=6",
		fmt.Sprintf("--new=6:%d:%d", original.FirstLBA, lastUsable),
		"--typecode=6:"+original.TypeGUID,
		"--partition-guid=6:"+original.UniqueGUID,
		"--change-name=6:"+original.Name,
		disk,
	)
	return m.mutate(ctx, disk, args...)
}

func (m GPTMutator) mutate(ctx context.Context, disk string, args ...string) error {
	if m.SGDiskPath == "" {
		return errors.New("sgdisk path is required")
	}
	command := exec.CommandContext(ctx, m.SGDiskPath, args...)
	command.Env = append([]string{"LC_ALL=C", "PATH=/usr/sbin:/usr/bin:/sbin:/bin"}, m.ExtraEnv...)
	output, err := command.CombinedOutput()
	if err != nil {
		return fmt.Errorf("sgdisk failed: %w: %s", err, strings.TrimSpace(string(output)))
	}
	f, err := os.OpenFile(disk, os.O_RDWR, 0)
	if err != nil {
		return fmt.Errorf("open GPT disk for sync: %w", err)
	}
	if err := f.Sync(); err != nil {
		_ = f.Close()
		return fmt.Errorf("sync GPT disk: %w", err)
	}
	if err := f.Close(); err != nil {
		return fmt.Errorf("close GPT disk: %w", err)
	}
	if m.TestMode {
		if info, err := os.Stat(disk); err == nil && info.Mode().IsRegular() {
			return nil
		}
	}
	if m.PartprobePath == "" {
		return errors.New("partprobe path is required for block devices")
	}
	probe := exec.CommandContext(ctx, m.PartprobePath, disk)
	probe.Env = command.Env
	if output, err := probe.CombinedOutput(); err != nil {
		return fmt.Errorf("partprobe failed: %w: %s", err, strings.TrimSpace(string(output)))
	}
	settlePartitionEvents(ctx, command.Env)
	return nil
}

func settlePartitionEvents(ctx context.Context, env []string) {
	for _, path := range []string{"/usr/bin/udevadm", "/usr/sbin/udevadm", "/sbin/udevadm"} {
		info, err := os.Stat(path)
		if err != nil || info.Mode()&0111 == 0 {
			continue
		}
		settle := exec.CommandContext(ctx, path, "settle", "--timeout=10")
		settle.Env = env
		_ = settle.Run()
		return
	}
}

func validateRootfsPartition(partition GPTPartition) error {
	if partition.Number != 6 || partition.Name != "rootfs" || partition.FirstLBA == 0 || partition.LastLBA < partition.FirstLBA {
		return errors.New("invalid rootfs GPT partition identity")
	}
	if !guidPattern.MatchString(partition.TypeGUID) || !guidPattern.MatchString(partition.UniqueGUID) {
		return errors.New("invalid rootfs GPT partition GUID")
	}
	return nil
}
