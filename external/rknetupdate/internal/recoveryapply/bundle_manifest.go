package recoveryapply

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
)

// BundleSchema is the on-disk format used by the offline multi-image
// workflow.  It deliberately is not a Rockchip update.img format: the
// individual images stay independently inspectable and hash-bound.
const BundleSchema = 1

const (
	BundleManifestName = "bundle.manifest.json"
	BundleRootfsImage  = "rootfs.img"
	BundleBootImage    = "boot.img"
	BundleUbootImage   = "uboot.img"
)

// BundleArtifact describes one independently uploaded image.  Rootfs carries
// the ext4 geometry that recovery verifies again before it changes GPT.
type BundleArtifact struct {
	Target         string `json:"target"`
	Image          string `json:"image"`
	Size           int64  `json:"size"`
	SHA256         string `json:"sha256"`
	Filesystem     string `json:"filesystem,omitempty"`
	FilesystemUUID string `json:"filesystem_uuid,omitempty"`
	BlockSize      uint64 `json:"block_size,omitempty"`
	BlockCount     uint64 `json:"block_count,omitempty"`
}

// BundleManifest is persisted on the running rootfs first, then copied with
// every artifact into temporary p7 before rootfs is touched.  Its sha256 is
// embedded in the BCB request, so recovery accepts only the exact staged
// manifest that normal Linux prepared.
type BundleManifest struct {
	Schema         int              `json:"schema"`
	ExpectedParent string           `json:"expected_parent"`
	Artifacts      []BundleArtifact `json:"artifacts"`
}

func (m BundleManifest) Validate() error {
	if m.Schema != BundleSchema {
		return fmt.Errorf("unsupported bundle manifest schema %d", m.Schema)
	}
	if m.ExpectedParent == "" || !filepath.IsAbs(m.ExpectedParent) || strings.TrimSpace(m.ExpectedParent) != m.ExpectedParent {
		return errors.New("bundle manifest expected_parent must be an absolute device path without surrounding whitespace")
	}
	if len(m.Artifacts) == 0 || len(m.Artifacts) > 3 {
		return errors.New("bundle manifest must contain between one and three artifacts")
	}
	seen := make(map[string]bool, len(m.Artifacts))
	rootfsSeen := false
	lastOrder := -1
	for _, artifact := range m.Artifacts {
		if seen[artifact.Target] {
			return fmt.Errorf("bundle manifest contains duplicate target %q", artifact.Target)
		}
		seen[artifact.Target] = true
		order, err := validateBundleArtifact(artifact)
		if err != nil {
			return err
		}
		if order <= lastOrder {
			return errors.New("bundle manifest artifacts are not in canonical write order")
		}
		lastOrder = order
		rootfsSeen = rootfsSeen || artifact.Target == "rootfs"
	}
	if !rootfsSeen {
		return errors.New("bundle manifest must contain rootfs.img; raw boot/uboot-only updates use the direct workflow")
	}
	return nil
}

// Bundle artifact ordering is the recovery write ordering.  Rootfs is first
// while a verified p7 source still exists; boot follows; uboot is deliberately
// last because it has the largest recovery risk.
func bundleArtifactOrder(target string) int {
	switch target {
	case "rootfs":
		return 0
	case "boot":
		return 1
	case "uboot":
		return 2
	default:
		return -1
	}
}

func bundleImageName(target string) string {
	switch target {
	case "rootfs":
		return BundleRootfsImage
	case "boot":
		return BundleBootImage
	case "uboot":
		return BundleUbootImage
	default:
		return ""
	}
}

func validateBundleArtifact(artifact BundleArtifact) (int, error) {
	order := bundleArtifactOrder(artifact.Target)
	if order < 0 {
		return -1, fmt.Errorf("bundle manifest target %q is unsupported", artifact.Target)
	}
	if artifact.Image != bundleImageName(artifact.Target) {
		return -1, fmt.Errorf("bundle manifest target %s must use image %s", artifact.Target, bundleImageName(artifact.Target))
	}
	if artifact.Size <= 0 {
		return -1, fmt.Errorf("bundle artifact %s size must be positive", artifact.Target)
	}
	if !sha256Pattern.MatchString(artifact.SHA256) {
		return -1, fmt.Errorf("bundle artifact %s sha256 must be lowercase hexadecimal", artifact.Target)
	}
	if artifact.Target != "rootfs" {
		if artifact.Filesystem != "" || artifact.FilesystemUUID != "" || artifact.BlockSize != 0 || artifact.BlockCount != 0 {
			return -1, fmt.Errorf("bundle artifact %s must not contain filesystem metadata", artifact.Target)
		}
		return order, nil
	}
	if artifact.Filesystem != "ext4" {
		return -1, errors.New("bundle rootfs artifact filesystem must be ext4")
	}
	if !guidPattern.MatchString(artifact.FilesystemUUID) {
		return -1, errors.New("bundle rootfs artifact filesystem_uuid is invalid")
	}
	if artifact.BlockSize < 1024 || artifact.BlockSize > 65536 || artifact.BlockSize&(artifact.BlockSize-1) != 0 {
		return -1, errors.New("bundle rootfs artifact block_size is invalid")
	}
	if artifact.BlockCount == 0 || artifact.BlockCount > ^uint64(0)/artifact.BlockSize {
		return -1, errors.New("bundle rootfs artifact block_count is invalid")
	}
	if artifact.BlockCount*artifact.BlockSize > uint64(artifact.Size) {
		return -1, errors.New("bundle rootfs filesystem exceeds image size")
	}
	return order, nil
}

func (m BundleManifest) Artifact(target string) (BundleArtifact, error) {
	for _, artifact := range m.Artifacts {
		if artifact.Target == target {
			return artifact, nil
		}
	}
	return BundleArtifact{}, fmt.Errorf("bundle does not contain %s", target)
}

func (m BundleManifest) TotalSize() (int64, error) {
	var total int64
	for _, artifact := range m.Artifacts {
		if artifact.Size > int64(^uint64(0)>>1)-total {
			return 0, errors.New("bundle artifact total size overflows")
		}
		total += artifact.Size
	}
	return total, nil
}

// LoadBundleManifest only accepts a fixed filename in its containing
// directory.  That keeps artifact path traversal out of recovery entirely.
func LoadBundleManifest(path string, testMode bool) (BundleManifest, error) {
	if filepath.Base(filepath.Clean(path)) != BundleManifestName {
		return BundleManifest{}, fmt.Errorf("bundle manifest filename must be %s", BundleManifestName)
	}
	info, err := secureRegularFile(path, testMode)
	if err != nil {
		return BundleManifest{}, fmt.Errorf("bundle manifest: %w", err)
	}
	if info.Size() <= 0 || info.Size() > maxManifestBytes {
		return BundleManifest{}, fmt.Errorf("bundle manifest size must be between 1 and %d bytes", maxManifestBytes)
	}
	f, err := os.Open(path)
	if err != nil {
		return BundleManifest{}, fmt.Errorf("open bundle manifest: %w", err)
	}
	defer f.Close()
	var manifest BundleManifest
	decoder := json.NewDecoder(io.LimitReader(f, maxManifestBytes+1))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&manifest); err != nil {
		return BundleManifest{}, fmt.Errorf("decode bundle manifest: %w", err)
	}
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		if err == nil {
			return BundleManifest{}, errors.New("bundle manifest must contain one JSON object")
		}
		return BundleManifest{}, fmt.Errorf("decode trailing bundle manifest data: %w", err)
	}
	if err := manifest.Validate(); err != nil {
		return BundleManifest{}, err
	}
	return manifest, nil
}

// HashBundleManifest returns the full SHA-256 of the persisted bytes.  Do not
// hash a re-marshaled Go value: BCB must bind the exact file copied to p7.
func HashBundleManifest(path string, testMode bool) (string, int64, error) {
	info, err := secureRegularFile(path, testMode)
	if err != nil {
		return "", 0, fmt.Errorf("bundle manifest: %w", err)
	}
	if info.Size() <= 0 || info.Size() > maxManifestBytes {
		return "", 0, fmt.Errorf("bundle manifest size must be between 1 and %d bytes", maxManifestBytes)
	}
	f, err := os.Open(path)
	if err != nil {
		return "", 0, fmt.Errorf("open bundle manifest: %w", err)
	}
	defer f.Close()
	hash := sha256.New()
	if _, err := io.Copy(hash, io.LimitReader(f, maxManifestBytes+1)); err != nil {
		return "", 0, fmt.Errorf("hash bundle manifest: %w", err)
	}
	return hex.EncodeToString(hash.Sum(nil)), info.Size(), nil
}

func BundleArtifactPath(stageDir string, artifact BundleArtifact) (string, error) {
	if _, err := validateBundleArtifact(artifact); err != nil {
		return "", err
	}
	path := filepath.Join(filepath.Clean(stageDir), artifact.Image)
	if filepath.Dir(path) != filepath.Clean(stageDir) || filepath.Base(path) != artifact.Image {
		return "", errors.New("bundle artifact escaped its stage directory")
	}
	return path, nil
}
