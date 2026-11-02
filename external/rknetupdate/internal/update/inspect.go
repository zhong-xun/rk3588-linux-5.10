package update

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"strings"
)

var androidBootMagic = []byte("ANDROID!")

func InspectImage(target Target, header []byte) (Validation, error) {
	if len(header) < 8 {
		return Validation{}, fmt.Errorf("image is too small to contain a valid header")
	}
	lower := strings.ToLower(string(header))
	detected := "unknown"
	confidence := "low"
	summary := "未识别到强格式标记，按文件名和目标分区继续；请确认镜像来自匹配的 Rockchip SDK"

	switch {
	case bytes.HasPrefix(header, androidBootMagic):
		detected, confidence, summary = "boot", "high", "检测到 Android boot image 头"
	case binary.BigEndian.Uint32(header[:4]) == 0xd00dfeed:
		hasKernel := strings.Contains(lower, "kernel") || strings.Contains(lower, "ramdisk")
		hasUboot := strings.Contains(lower, "u-boot") || strings.Contains(lower, "uboot")
		switch {
		case hasKernel && !hasUboot:
			detected, confidence, summary = "boot", "medium", "检测到 FIT/DTB 容器及内核相关节点"
		case hasUboot && !hasKernel:
			detected, confidence, summary = "uboot", "medium", "检测到 FIT/DTB 容器及 U-Boot 相关节点"
		default:
			detected, confidence, summary = "fit", "medium", "检测到 Rockchip SDK 常用的 FIT/DTB 容器；该格式可用于 boot 或 uboot"
		}
	case bytes.HasPrefix(header, []byte("KRNL")):
		detected, confidence, summary = "boot", "medium", "检测到 KRNL 启动镜像标记"
	}

	if (detected == "boot" && target == TargetUboot) || (detected == "uboot" && target == TargetBoot) {
		return Validation{DetectedType: detected, Confidence: confidence, Summary: summary}, fmt.Errorf("image content looks like %s.img but target is %s", detected, target)
	}
	return Validation{DetectedType: detected, Confidence: confidence, Summary: summary}, nil
}
