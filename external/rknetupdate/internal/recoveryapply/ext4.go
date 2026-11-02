package recoveryapply

import (
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"math"
	"os"
)

const (
	ext4SuperblockOffset = 1024
	ext4SuperblockSize   = 1024
	ext4Magic            = 0xef53
	ext4Incompat64Bit    = 0x80
)

type Ext4Info struct {
	BlockSize      uint64
	BlockCount     uint64
	FilesystemSize uint64
	UUID           string
}

func InspectExt4(f *os.File, declaredSize int64) (Ext4Info, error) {
	return inspectExt4(f, declaredSize, true)
}

func InspectExt4Device(f *os.File, capacity int64) (Ext4Info, error) {
	return inspectExt4(f, capacity, false)
}

func inspectExt4(f *os.File, declaredSize int64, requireTightSize bool) (Ext4Info, error) {
	if declaredSize < ext4SuperblockOffset+ext4SuperblockSize {
		return Ext4Info{}, errors.New("image is too small for an ext4 superblock")
	}
	super := make([]byte, ext4SuperblockSize)
	if _, err := f.ReadAt(super, ext4SuperblockOffset); err != nil && !errors.Is(err, io.EOF) {
		return Ext4Info{}, fmt.Errorf("read ext4 superblock: %w", err)
	}
	if binary.LittleEndian.Uint16(super[0x38:0x3a]) != ext4Magic {
		return Ext4Info{}, errors.New("image does not contain an ext4 superblock")
	}
	logBlockSize := binary.LittleEndian.Uint32(super[0x18:0x1c])
	if logBlockSize > 6 {
		return Ext4Info{}, fmt.Errorf("invalid ext4 block size shift %d", logBlockSize)
	}
	blockSize := uint64(1024) << logBlockSize
	blocks := uint64(binary.LittleEndian.Uint32(super[0x04:0x08]))
	incompat := binary.LittleEndian.Uint32(super[0x60:0x64])
	if incompat&ext4Incompat64Bit != 0 {
		blocks |= uint64(binary.LittleEndian.Uint32(super[0x150:0x154])) << 32
	}
	if blocks == 0 || blocks > math.MaxUint64/blockSize {
		return Ext4Info{}, errors.New("invalid ext4 block count")
	}
	filesystemSize := blocks * blockSize
	if filesystemSize > uint64(declaredSize) {
		return Ext4Info{}, fmt.Errorf("ext4 filesystem size %d exceeds declared image size %d", filesystemSize, declaredSize)
	}
	if requireTightSize && uint64(declaredSize)-filesystemSize >= blockSize {
		return Ext4Info{}, fmt.Errorf("declared image size %d has unexpected trailing data after ext4 filesystem size %d", declaredSize, filesystemSize)
	}
	uuid := super[0x68:0x78]
	uuidText := hex.EncodeToString(uuid)
	uuidText = uuidText[0:8] + "-" + uuidText[8:12] + "-" + uuidText[12:16] + "-" + uuidText[16:20] + "-" + uuidText[20:32]
	return Ext4Info{BlockSize: blockSize, BlockCount: blocks, FilesystemSize: filesystemSize, UUID: uuidText}, nil
}
