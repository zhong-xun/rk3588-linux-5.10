package recoveryapply

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"syscall"
)

type RootfsManifestV2 struct {
	Schema          int    `json:"schema"`
	Image           string `json:"image"`
	Size            int64  `json:"size"`
	SHA256          string `json:"sha256"`
	Filesystem      string `json:"filesystem"`
	FilesystemUUID  string `json:"filesystem_uuid"`
	BlockSize       uint64 `json:"block_size"`
	BlockCount      uint64 `json:"block_count"`
	TargetPartlabel string `json:"target_partlabel"`
	ExpectedParent  string `json:"expected_parent"`
}

func (m RootfsManifestV2) Validate() error {
	switch {
	case m.Schema != RootfsRequestSchema:
		return fmt.Errorf("unsupported rootfs v2 manifest schema %d", m.Schema)
	case m.Image != "rootfs.img":
		return errors.New("rootfs v2 manifest image must be rootfs.img")
	case m.Size <= 0:
		return errors.New("rootfs v2 manifest size must be positive")
	case !sha256Pattern.MatchString(m.SHA256):
		return errors.New("rootfs v2 manifest sha256 must be lowercase hexadecimal")
	case m.Filesystem != "ext4":
		return errors.New("rootfs v2 manifest filesystem must be ext4")
	case !guidPattern.MatchString(m.FilesystemUUID):
		return errors.New("rootfs v2 manifest filesystem_uuid is invalid")
	case m.BlockSize < 1024 || m.BlockSize > 65536 || m.BlockSize&(m.BlockSize-1) != 0:
		return errors.New("rootfs v2 manifest block_size is invalid")
	case m.BlockCount == 0 || m.BlockCount > ^uint64(0)/m.BlockSize:
		return errors.New("rootfs v2 manifest block_count is invalid")
	case m.BlockCount*m.BlockSize > uint64(m.Size):
		return errors.New("rootfs v2 manifest filesystem exceeds image size")
	case m.TargetPartlabel != "rootfs":
		return errors.New("rootfs v2 manifest target_partlabel must be rootfs")
	case filepath.Clean(m.ExpectedParent) != ProductionExpectedParentPath:
		return fmt.Errorf("rootfs v2 manifest expected_parent must be %s", ProductionExpectedParentPath)
	}
	return nil
}

func LoadRootfsManifestV2(path string, testMode bool) (RootfsManifestV2, string, error) {
	info, err := secureRegularFile(path, testMode)
	if err != nil {
		return RootfsManifestV2{}, "", fmt.Errorf("rootfs v2 manifest: %w", err)
	}
	if info.Size() <= 0 || info.Size() > maxManifestBytes {
		return RootfsManifestV2{}, "", fmt.Errorf("rootfs v2 manifest size must be between 1 and %d bytes", maxManifestBytes)
	}
	f, err := os.Open(path)
	if err != nil {
		return RootfsManifestV2{}, "", fmt.Errorf("open rootfs v2 manifest: %w", err)
	}
	defer f.Close()
	var manifest RootfsManifestV2
	decoder := json.NewDecoder(io.LimitReader(f, maxManifestBytes+1))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&manifest); err != nil {
		return RootfsManifestV2{}, "", fmt.Errorf("decode rootfs v2 manifest: %w", err)
	}
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		if err == nil {
			return RootfsManifestV2{}, "", errors.New("rootfs v2 manifest must contain one JSON object")
		}
		return RootfsManifestV2{}, "", fmt.Errorf("decode trailing rootfs v2 manifest data: %w", err)
	}
	if err := manifest.Validate(); err != nil {
		return RootfsManifestV2{}, "", err
	}
	return manifest, filepath.Join(filepath.Dir(filepath.Clean(path)), "rootfs.img"), nil
}

func ValidateRootfsImageV2(ctx context.Context, manifestPath, e2fsckPath string, testMode bool) (RootfsManifestV2, string, Ext4Info, error) {
	// Validate the structure before invoking e2fsck, then validate and hash the
	// same staged artifact again after it returns.  The normal system uses the
	// metadata-only variant below because its e2fsprogs can be older than the
	// image produced by Buildroot; recovery contains the required e2fsprogs.
	_, imagePath, _, err := inspectRootfsImageV2(manifestPath, testMode, false)
	if err != nil {
		return RootfsManifestV2{}, "", Ext4Info{}, err
	}
	if _, err := runE2fsck(ctx, e2fsckPath, imagePath); err != nil {
		return RootfsManifestV2{}, "", Ext4Info{}, err
	}
	return ValidateRootfsImageV2Metadata(manifestPath, testMode)
}

// ValidateRootfsImageV2Metadata validates the immutable properties available
// on the normal system: manifest schema, file identity, ext4 geometry, and
// SHA-256.  It deliberately does not execute e2fsck.  Full filesystem checks
// happen in the controlled recovery environment, whose e2fsprogs version is
// part of the recovery image rather than the OS being updated.
func ValidateRootfsImageV2Metadata(manifestPath string, testMode bool) (RootfsManifestV2, string, Ext4Info, error) {
	return inspectRootfsImageV2(manifestPath, testMode, true)
}

func inspectRootfsImageV2(manifestPath string, testMode, verifyHash bool) (RootfsManifestV2, string, Ext4Info, error) {
	manifest, imagePath, err := LoadRootfsManifestV2(manifestPath, testMode)
	if err != nil {
		return RootfsManifestV2{}, "", Ext4Info{}, err
	}
	info, err := secureRegularFile(imagePath, testMode)
	if err != nil {
		return RootfsManifestV2{}, "", Ext4Info{}, fmt.Errorf("rootfs v2 image: %w", err)
	}
	if info.Size() != manifest.Size {
		return RootfsManifestV2{}, "", Ext4Info{}, fmt.Errorf("rootfs v2 image size %d does not match manifest %d", info.Size(), manifest.Size)
	}
	f, err := os.Open(imagePath)
	if err != nil {
		return RootfsManifestV2{}, "", Ext4Info{}, fmt.Errorf("open rootfs v2 image: %w", err)
	}
	defer f.Close()
	if err := syscall.Flock(int(f.Fd()), syscall.LOCK_SH|syscall.LOCK_NB); err != nil {
		return RootfsManifestV2{}, "", Ext4Info{}, fmt.Errorf("lock rootfs v2 image: %w", err)
	}
	defer syscall.Flock(int(f.Fd()), syscall.LOCK_UN) //nolint:errcheck
	ext4, err := InspectExt4(f, manifest.Size)
	if err != nil {
		return RootfsManifestV2{}, "", Ext4Info{}, err
	}
	if ext4.UUID != manifest.FilesystemUUID || ext4.BlockSize != manifest.BlockSize || ext4.BlockCount != manifest.BlockCount {
		return RootfsManifestV2{}, "", Ext4Info{}, fmt.Errorf("rootfs v2 ext4 geometry does not match manifest: uuid=%s block_size=%d block_count=%d", ext4.UUID, ext4.BlockSize, ext4.BlockCount)
	}
	if !verifyHash {
		return manifest, imagePath, ext4, nil
	}
	if current, err := f.Stat(); err != nil || !os.SameFile(info, current) || current.Size() != manifest.Size {
		return RootfsManifestV2{}, "", Ext4Info{}, errors.New("rootfs v2 image identity changed during validation")
	}
	hash, err := hashOpenFile(f, manifest.Size)
	if err != nil {
		return RootfsManifestV2{}, "", Ext4Info{}, err
	}
	if hash != manifest.SHA256 {
		return RootfsManifestV2{}, "", Ext4Info{}, fmt.Errorf("rootfs v2 image sha256 %s does not match manifest %s", hash, manifest.SHA256)
	}
	return manifest, imagePath, ext4, nil
}
