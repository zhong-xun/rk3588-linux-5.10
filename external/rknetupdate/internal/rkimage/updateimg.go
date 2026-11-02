// Package rkimage validates the container layout used by Rockchip update.img.
package rkimage

import (
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"os"
	"strings"
)

const (
	imageHeaderSize    = 102
	firmwareHeaderSize = 140 + maxItems*itemSize
	itemSize           = 112
	maxItems           = 16
)

var (
	ContainerMagic = [4]byte{'R', 'K', 'F', 'W'}
	firmwareMagic  = [4]byte{'R', 'K', 'A', 'F'}
)

type Item struct {
	Name        string
	Offset      uint64
	Size        uint64
	FlashOffset uint32
}

type Image struct {
	FirmwareOffset uint64
	FirmwareSize   uint64
	Items          []Item
}

func InspectFile(path string) (Image, error) {
	f, err := os.Open(path)
	if err != nil {
		return Image{}, err
	}
	defer f.Close()
	info, err := f.Stat()
	if err != nil {
		return Image{}, err
	}
	if !info.Mode().IsRegular() {
		return Image{}, errors.New("update image must be a regular file")
	}
	return Inspect(f, info.Size())
}

func Inspect(r io.ReaderAt, fileSize int64) (Image, error) {
	if fileSize < imageHeaderSize {
		return Image{}, errors.New("update image is smaller than the Rockchip image header")
	}
	header := make([]byte, imageHeaderSize)
	if _, err := r.ReadAt(header, 0); err != nil {
		return Image{}, fmt.Errorf("read update image header: %w", err)
	}
	if string(header[:4]) != string(ContainerMagic[:]) {
		return Image{}, errors.New("update image is missing RKFW container magic")
	}
	fwOffset := uint64(binary.LittleEndian.Uint32(header[33:37]))
	fwSize := uint64(binary.LittleEndian.Uint32(header[37:41]))
	if fwOffset < imageHeaderSize || fwOffset > uint64(fileSize)-firmwareHeaderSize {
		return Image{}, errors.New("update image firmware header offset is outside the file")
	}
	if fwSize != 0 && (fwSize < firmwareHeaderSize || fwSize > uint64(fileSize)-fwOffset) {
		return Image{}, errors.New("update image firmware size is outside the file")
	}

	firmware := make([]byte, firmwareHeaderSize)
	if _, err := r.ReadAt(firmware, int64(fwOffset)); err != nil {
		return Image{}, fmt.Errorf("read update image firmware header: %w", err)
	}
	if string(firmware[:4]) != string(firmwareMagic[:]) {
		return Image{}, errors.New("update image firmware header is missing RKAF magic")
	}
	count := int32(binary.LittleEndian.Uint32(firmware[136:140]))
	if count < 1 || count > maxItems {
		return Image{}, fmt.Errorf("update image item count %d is outside 1..%d", count, maxItems)
	}

	image := Image{FirmwareOffset: fwOffset, FirmwareSize: fwSize, Items: make([]Item, 0, count)}
	seen := make(map[string]struct{}, count)
	for index := 0; index < int(count); index++ {
		entry := firmware[140+index*itemSize : 140+(index+1)*itemSize]
		name := cString(entry[:32])
		if name == "" {
			return Image{}, fmt.Errorf("update image item %d has an empty name", index)
		}
		if _, ok := seen[name]; ok {
			return Image{}, fmt.Errorf("update image contains duplicate item %q", name)
		}
		seen[name] = struct{}{}
		offset := uint64(binary.LittleEndian.Uint32(entry[96:100]))
		// Newer rkImageMaker packages encode a high 32-bit extension in the
		// otherwise-path string.  This is the same extension consumed by the
		// bundled rktoolkit update receiver (file[50]/file[51:55] and
		// file[55]/file[56:60]).  Without it, a >4 GiB rootfs item would be
		// mistaken for a small range and could be staged incorrectly.
		if entry[32+50] == 'H' {
			offset |= uint64(binary.LittleEndian.Uint32(entry[32+51:32+55])) << 32
		}
		size := uint64(binary.LittleEndian.Uint32(entry[108:112]))
		if entry[32+55] == 'H' {
			size |= uint64(binary.LittleEndian.Uint32(entry[32+56:32+60])) << 32
		}
		if size == 0 {
			return Image{}, fmt.Errorf("update image item %q has zero size", name)
		}
		start := fwOffset + offset
		if start < fwOffset || start > uint64(fileSize) || size > uint64(fileSize)-start {
			return Image{}, fmt.Errorf("update image item %q extends beyond the file", name)
		}
		image.Items = append(image.Items, Item{
			Name: name, Offset: start, Size: size,
			FlashOffset: binary.LittleEndian.Uint32(entry[100:104]),
		})
	}
	return image, nil
}

func (i Image) ItemNames() []string {
	names := make([]string, len(i.Items))
	for index, item := range i.Items {
		names[index] = item.Name
	}
	return names
}

func cString(value []byte) string {
	if end := strings.IndexByte(string(value), 0); end >= 0 {
		value = value[:end]
	}
	return strings.TrimSpace(string(value))
}
