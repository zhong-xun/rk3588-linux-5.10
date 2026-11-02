package recoveryapply

import (
	"strings"
	"testing"
)

func TestValidateDeviceGPTIdentity(t *testing.T) {
	partition := GPTPartition{
		Number:   2,
		FirstLBA: 4096,
		LastLBA:  6143,
		Name:     "misc",
	}
	device := Device{
		PartName:   "misc",
		PartNumber: 2,
		StartLBA:   4096,
		SizeLBA:    2048,
		Size:       2048 * 512,
	}
	if err := validateDeviceGPTIdentity(device, partition, 512); err != nil {
		t.Fatalf("valid GPT identity rejected: %v", err)
	}

	tests := []struct {
		name      string
		device    Device
		partition GPTPartition
		sector    uint64
	}{
		{name: "partname", device: withDevicePartName(device, "rootfs"), partition: partition, sector: 512},
		{name: "number", device: withDevicePartNumber(device, 3), partition: partition, sector: 512},
		{name: "start", device: withDeviceStart(device, 4097), partition: partition, sector: 512},
		{name: "sysfs-size", device: withDeviceSizeLBA(device, 2047), partition: partition, sector: 512},
		{name: "block-size", device: withDeviceSize(device, 2047*512), partition: partition, sector: 512},
		{name: "sector-size", device: device, partition: partition, sector: 1024},
		{name: "regular-file", device: withDeviceRegular(device), partition: partition, sector: 512},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if err := validateDeviceGPTIdentity(test.device, test.partition, test.sector); err == nil {
				t.Fatal("mismatched direct fallback identity was accepted")
			}
		})
	}
}

func TestResolveRecoveryPartitionDoesNotFallbackInTestMode(t *testing.T) {
	_, err := ResolveRecoveryPartition(t.TempDir()+"/missing", "misc", t.TempDir()+"/disk", true)
	if err == nil || !strings.Contains(err.Error(), "device unavailable") || strings.Contains(err.Error(), "GPT fallback") {
		t.Fatalf("unexpected test-mode resolution error: %v", err)
	}
}

func withDevicePartName(device Device, value string) Device {
	device.PartName = value
	return device
}

func withDevicePartNumber(device Device, value int) Device {
	device.PartNumber = value
	return device
}

func withDeviceStart(device Device, value uint64) Device {
	device.StartLBA = value
	return device
}

func withDeviceSizeLBA(device Device, value uint64) Device {
	device.SizeLBA = value
	return device
}

func withDeviceSize(device Device, value int64) Device {
	device.Size = value
	return device
}

func withDeviceRegular(device Device) Device {
	device.Regular = true
	return device
}
