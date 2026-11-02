package recoveryapply

import (
	"encoding/binary"
	"encoding/hex"
	"hash/crc32"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"unicode/utf16"
)

func TestReadGPTValidatesPrimaryBackupAndRootfsIdentity(t *testing.T) {
	disk := createGPTFixture(t)
	table, err := ReadGPT(disk, true)
	if err != nil {
		t.Fatal(err)
	}
	if table.SectorSize != 512 || table.DiskBytes != 16<<20 || table.FirstUsable != 34 || table.LastUsable != 32734 {
		t.Fatalf("unexpected GPT geometry: %+v", table)
	}
	p6, err := table.Partition(6)
	if err != nil {
		t.Fatal(err)
	}
	want := GPTPartition{
		Number:     6,
		TypeGUID:   linuxFilesystemTypeGUID,
		UniqueGUID: "66666666-7777-8888-9999-aaaaaaaaaaaa",
		FirstLBA:   12288,
		LastLBA:    32734,
		Name:       "rootfs",
	}
	if p6 != want {
		t.Fatalf("p6 = %+v, want %+v", p6, want)
	}
}

func TestReadGPTRejectsCorruptPrimaryHeader(t *testing.T) {
	disk := createGPTFixture(t)
	f, err := os.OpenFile(disk, os.O_RDWR, 0)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := f.WriteAt([]byte{0xff}, 512+24); err != nil {
		_ = f.Close()
		t.Fatal(err)
	}
	_ = f.Close()
	if _, err := ReadGPT(disk, true); err == nil {
		t.Fatal("accepted a corrupt primary GPT header")
	}
}

func TestReadGPTRejectsDifferentBackupEntries(t *testing.T) {
	disk := createGPTFixture(t)
	f, err := os.OpenFile(disk, os.O_RDWR, 0)
	if err != nil {
		t.Fatal(err)
	}
	const backupEntriesOffset = int64(32735 * 512)
	entries := make([]byte, 128*128)
	if _, err := f.ReadAt(entries, backupEntriesOffset); err != nil {
		_ = f.Close()
		t.Fatal(err)
	}
	entries[128+56] = 'X'
	if _, err := f.WriteAt(entries, backupEntriesOffset); err != nil {
		_ = f.Close()
		t.Fatal(err)
	}
	header := make([]byte, 512)
	if _, err := f.ReadAt(header, 32767*512); err != nil {
		_ = f.Close()
		t.Fatal(err)
	}
	binary.LittleEndian.PutUint32(header[88:92], crc32.ChecksumIEEE(entries))
	rewriteHeaderCRC(header)
	if _, err := f.WriteAt(header, 32767*512); err != nil {
		_ = f.Close()
		t.Fatal(err)
	}
	_ = f.Close()
	if _, err := ReadGPT(disk, true); err == nil {
		t.Fatal("accepted different primary and backup partition arrays")
	}
}

func createGPTFixture(t *testing.T) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "disk.img")
	f, err := os.OpenFile(path, os.O_CREATE|os.O_RDWR|os.O_TRUNC, 0600)
	if err != nil {
		t.Fatal(err)
	}
	if err := f.Truncate(16 << 20); err != nil {
		_ = f.Close()
		t.Fatal(err)
	}
	entries := make([]byte, 128*128)
	partitions := []GPTPartition{
		{1, linuxFilesystemTypeGUID, "11111111-2222-3333-4444-555555555551", 2048, 4095, "uboot"},
		{2, linuxFilesystemTypeGUID, "22222222-3333-4444-5555-666666666662", 4096, 6143, "misc"},
		{3, linuxFilesystemTypeGUID, "33333333-4444-5555-6666-777777777773", 6144, 8191, "boot"},
		{4, linuxFilesystemTypeGUID, "44444444-5555-6666-7777-888888888884", 8192, 10239, "recovery"},
		{5, linuxFilesystemTypeGUID, "55555555-6666-7777-8888-999999999995", 10240, 12287, "backup"},
		{6, linuxFilesystemTypeGUID, "66666666-7777-8888-9999-aaaaaaaaaaaa", 12288, 32734, "rootfs"},
	}
	for _, partition := range partitions {
		entry := entries[(partition.Number-1)*128 : partition.Number*128]
		putGPTGUID(entry[0:16], partition.TypeGUID)
		putGPTGUID(entry[16:32], partition.UniqueGUID)
		binary.LittleEndian.PutUint64(entry[32:40], partition.FirstLBA)
		binary.LittleEndian.PutUint64(entry[40:48], partition.LastLBA)
		name := utf16.Encode([]rune(partition.Name))
		for i, word := range name {
			binary.LittleEndian.PutUint16(entry[56+i*2:58+i*2], word)
		}
	}
	entriesCRC := crc32.ChecksumIEEE(entries)
	primary := makeGPTHeader(1, 32767, 2, entriesCRC)
	backup := makeGPTHeader(32767, 1, 32735, entriesCRC)
	protective := make([]byte, 512)
	protective[510], protective[511] = 0x55, 0xaa
	for _, write := range []struct {
		offset int64
		data   []byte
	}{
		{0, protective},
		{512, primary},
		{2 * 512, entries},
		{32735 * 512, entries},
		{32767 * 512, backup},
	} {
		if _, err := f.WriteAt(write.data, write.offset); err != nil {
			_ = f.Close()
			t.Fatal(err)
		}
	}
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}
	return path
}

func makeGPTHeader(current, backup, entriesLBA uint64, entriesCRC uint32) []byte {
	header := make([]byte, 512)
	copy(header[:8], "EFI PART")
	binary.LittleEndian.PutUint32(header[8:12], 0x00010000)
	binary.LittleEndian.PutUint32(header[12:16], 92)
	binary.LittleEndian.PutUint64(header[24:32], current)
	binary.LittleEndian.PutUint64(header[32:40], backup)
	binary.LittleEndian.PutUint64(header[40:48], 34)
	binary.LittleEndian.PutUint64(header[48:56], 32734)
	putGPTGUID(header[56:72], "01234567-89ab-cdef-0123-456789abcdef")
	binary.LittleEndian.PutUint64(header[72:80], entriesLBA)
	binary.LittleEndian.PutUint32(header[80:84], 128)
	binary.LittleEndian.PutUint32(header[84:88], 128)
	binary.LittleEndian.PutUint32(header[88:92], entriesCRC)
	rewriteHeaderCRC(header)
	return header
}

func rewriteHeaderCRC(header []byte) {
	clear(header[16:20])
	binary.LittleEndian.PutUint32(header[16:20], crc32.ChecksumIEEE(header[:92]))
}

func putGPTGUID(destination []byte, value string) {
	compact := strings.ReplaceAll(value, "-", "")
	raw, err := hex.DecodeString(compact)
	if err != nil || len(raw) != 16 {
		panic("invalid test GUID " + strconv.Quote(value))
	}
	binary.LittleEndian.PutUint32(destination[0:4], binary.BigEndian.Uint32(raw[0:4]))
	binary.LittleEndian.PutUint16(destination[4:6], binary.BigEndian.Uint16(raw[4:6]))
	binary.LittleEndian.PutUint16(destination[6:8], binary.BigEndian.Uint16(raw[6:8]))
	copy(destination[8:16], raw[8:16])
}
