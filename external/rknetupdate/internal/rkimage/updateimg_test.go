package rkimage

import (
	"bytes"
	"encoding/binary"
	"strings"
	"testing"
)

func TestInspect(t *testing.T) {
	image := testImage(t, "recovery", 4096)
	parsed, err := Inspect(bytes.NewReader(image), int64(len(image)))
	if err != nil {
		t.Fatal(err)
	}
	if parsed.FirmwareOffset != imageHeaderSize || len(parsed.Items) != 1 {
		t.Fatalf("unexpected parsed image: %+v", parsed)
	}
	if item := parsed.Items[0]; item.Name != "recovery" || item.Offset != imageHeaderSize+firmwareHeaderSize || item.Size != 4096 {
		t.Fatalf("unexpected item: %+v", item)
	}
}

func TestInspectRejectsUnsafeLayouts(t *testing.T) {
	tests := []struct {
		name string
		edit func([]byte)
	}{
		{"wrong magic", func(image []byte) { copy(image[:4], "BAD!") }},
		{"too many items", func(image []byte) { binary.LittleEndian.PutUint32(image[imageHeaderSize+136:], 17) }},
		{"item outside file", func(image []byte) { binary.LittleEndian.PutUint32(image[imageHeaderSize+140+108:], 1<<30) }},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			image := testImage(t, "recovery", 4096)
			test.edit(image)
			if _, err := Inspect(bytes.NewReader(image), int64(len(image))); err == nil {
				t.Fatal("expected malformed image rejection")
			}
		})
	}
}

func TestInspectSupportsRockchipLargeItemExtension(t *testing.T) {
	image := testImage(t, "rootfs", 4096)
	entry := image[imageHeaderSize+140 : imageHeaderSize+140+itemSize]
	entry[32+50] = 'H'
	binary.LittleEndian.PutUint32(entry[32+51:32+55], 1)
	parsed, err := Inspect(bytes.NewReader(image), (1<<32)+int64(len(image))+4096)
	if err != nil {
		t.Fatal(err)
	}
	if got, want := parsed.Items[0].Offset, uint64(1<<32)+imageHeaderSize+firmwareHeaderSize; got != want {
		t.Fatalf("large item offset=%d want=%d", got, want)
	}
}

func testImage(t *testing.T, name string, payloadSize int) []byte {
	t.Helper()
	size := imageHeaderSize + firmwareHeaderSize + payloadSize
	image := make([]byte, size)
	copy(image[:4], ContainerMagic[:])
	binary.LittleEndian.PutUint32(image[33:37], imageHeaderSize)
	binary.LittleEndian.PutUint32(image[37:41], uint32(firmwareHeaderSize+payloadSize))
	firmware := image[imageHeaderSize:]
	copy(firmware[:4], firmwareMagic[:])
	binary.LittleEndian.PutUint32(firmware[136:140], 1)
	entry := firmware[140 : 140+itemSize]
	copy(entry[:32], name)
	binary.LittleEndian.PutUint32(entry[96:100], firmwareHeaderSize)
	binary.LittleEndian.PutUint32(entry[108:112], uint32(payloadSize))
	if strings.TrimSpace(name) == "" {
		t.Fatal("test item name must be non-empty")
	}
	return image
}
