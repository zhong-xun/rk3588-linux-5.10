package main

import (
	"encoding/binary"
	"encoding/json"
	"flag"
	"fmt"
	"os"

	"rknetupdate/internal/recoveryapply"
	"rknetupdate/internal/rkimage"
)

const (
	imageHeaderSize    = 102
	firmwareHeaderSize = 140 + 16*112
)

func main() {
	imagePath := flag.String("image", "", "output update.img path")
	manifestPath := flag.String("manifest", "", "output manifest path")
	parameterPath := flag.String("parameter", "", "parameter payload path")
	parentPath := flag.String("parent", "", "expected parent device")
	flag.Parse()
	if *imagePath == "" || *manifestPath == "" || *parameterPath == "" || *parentPath == "" || flag.NArg() != 0 {
		fatalf("usage: make-updateimg --image PATH --manifest PATH --parameter PATH --parent DEVICE")
	}
	parameter, err := os.ReadFile(*parameterPath)
	if err != nil {
		fatalf("read parameter: %v", err)
	}
	payloads := [][]byte{
		parameter,
		[]byte("rknetupdate recovery fixture\n"),
		[]byte("rknetupdate rootfs fixture\n"),
	}
	names := []string{"parameter", "recovery", "rootfs"}
	total := imageHeaderSize + firmwareHeaderSize
	for _, payload := range payloads {
		total += len(payload)
	}
	image := make([]byte, total)
	copy(image[:4], rkimage.ContainerMagic[:])
	binary.LittleEndian.PutUint32(image[33:37], imageHeaderSize)
	binary.LittleEndian.PutUint32(image[37:41], uint32(total-imageHeaderSize))
	firmware := image[imageHeaderSize:]
	copy(firmware[:4], "RKAF")
	binary.LittleEndian.PutUint32(firmware[136:140], uint32(len(names)))
	offset := firmwareHeaderSize
	for index, payload := range payloads {
		entry := firmware[140+index*112 : 140+(index+1)*112]
		copy(entry[:32], names[index])
		binary.LittleEndian.PutUint32(entry[96:100], uint32(offset))
		binary.LittleEndian.PutUint32(entry[100:104], uint32(index+1))
		binary.LittleEndian.PutUint32(entry[108:112], uint32(len(payload)))
		copy(firmware[offset:offset+len(payload)], payload)
		offset += len(payload)
	}
	if err := os.WriteFile(*imagePath, image, 0600); err != nil {
		fatalf("write update.img: %v", err)
	}
	manifest, err := recoveryapply.BuildUpdateImageManifest(*imagePath, *parentPath)
	if err != nil {
		fatalf("build manifest: %v", err)
	}
	encoded, err := json.MarshalIndent(manifest, "", "  ")
	if err != nil {
		fatalf("encode manifest: %v", err)
	}
	encoded = append(encoded, '\n')
	if err := os.WriteFile(*manifestPath, encoded, 0600); err != nil {
		fatalf("write manifest: %v", err)
	}
}

func fatalf(format string, values ...any) {
	fmt.Fprintf(os.Stderr, format+"\n", values...)
	os.Exit(1)
}
