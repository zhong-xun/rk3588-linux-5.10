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
	"sort"
	"strings"
	"syscall"

	"rknetupdate/internal/rkimage"
)

// UpdateImageSchema is the persisted metadata format for the complete
// Rockchip RKFW/RKAF OTA package flow.  It is intentionally separate from
// the rootfs and bundle schemas: the package is delegated to Rockchip's
// recovery update engine rather than raw-written by RKNetUpdate.
const UpdateImageSchema = 1

const (
	UpdateImageManifestName = "updateimg.manifest.json"
	UpdateImageFileName     = "update.img"
	UpdateImageKeepName     = "update.img.keep"
	UpdateImageStateName    = "state.json"
)

type UpdateImageItem struct {
	Name        string `json:"name"`
	Offset      uint64 `json:"offset"`
	Size        uint64 `json:"size"`
	FlashOffset uint32 `json:"flash_offset"`
	SHA256      string `json:"sha256"`
}

// UpdateImageManifest binds the exact package and its parsed recovery item.
// The package SHA-256 protects every item; per-item hashes are retained so
// the recovery item can be written last and verified after stock OTA has
// finished updating the remaining partitions.
type UpdateImageManifest struct {
	Schema         int               `json:"schema"`
	Image          string            `json:"image"`
	Size           int64             `json:"size"`
	SHA256         string            `json:"sha256"`
	ExpectedParent string            `json:"expected_parent"`
	Items          []UpdateImageItem `json:"items"`
}

func (m UpdateImageManifest) Validate() error {
	if m.Schema != UpdateImageSchema {
		return fmt.Errorf("unsupported update.img manifest schema %d", m.Schema)
	}
	if m.Image != UpdateImageFileName {
		return fmt.Errorf("update.img manifest image must be %s", UpdateImageFileName)
	}
	if m.Size <= 0 {
		return errors.New("update.img manifest size must be positive")
	}
	if !sha256Pattern.MatchString(m.SHA256) {
		return errors.New("update.img manifest sha256 must be lowercase hexadecimal")
	}
	if m.ExpectedParent == "" || !filepath.IsAbs(m.ExpectedParent) || strings.TrimSpace(m.ExpectedParent) != m.ExpectedParent {
		return errors.New("update.img manifest expected_parent must be an absolute device path without surrounding whitespace")
	}
	if len(m.Items) < 3 || len(m.Items) > 16 {
		return errors.New("update.img manifest must contain between three and sixteen items")
	}
	seen := make(map[string]bool, len(m.Items))
	lastOffset := uint64(0)
	for index, item := range m.Items {
		if item.Name == "" || strings.TrimSpace(item.Name) != item.Name {
			return fmt.Errorf("update.img item %d has an invalid name", index)
		}
		if seen[item.Name] {
			return fmt.Errorf("update.img manifest contains duplicate item %q", item.Name)
		}
		seen[item.Name] = true
		if item.Size == 0 || item.Offset > uint64(m.Size) || item.Size > uint64(m.Size)-item.Offset {
			return fmt.Errorf("update.img item %q is outside the package", item.Name)
		}
		if index > 0 && item.Offset < lastOffset {
			return errors.New("update.img manifest items are not in package order")
		}
		lastOffset = item.Offset
		if !sha256Pattern.MatchString(item.SHA256) {
			return fmt.Errorf("update.img item %q sha256 must be lowercase hexadecimal", item.Name)
		}
	}
	for _, required := range []string{"parameter", "recovery", "rootfs"} {
		if _, err := m.Item(required); err != nil {
			return fmt.Errorf("complete update.img must contain %s: %w", required, err)
		}
	}
	return nil
}

func (m UpdateImageManifest) Item(name string) (UpdateImageItem, error) {
	for _, item := range m.Items {
		if item.Name == name {
			return item, nil
		}
	}
	return UpdateImageItem{}, fmt.Errorf("item %q is missing", name)
}

func LoadUpdateImageManifest(path string, testMode bool) (UpdateImageManifest, error) {
	if filepath.Base(filepath.Clean(path)) != UpdateImageManifestName {
		return UpdateImageManifest{}, fmt.Errorf("update.img manifest filename must be %s", UpdateImageManifestName)
	}
	info, err := secureRegularFile(path, testMode)
	if err != nil {
		return UpdateImageManifest{}, fmt.Errorf("update.img manifest: %w", err)
	}
	if info.Size() <= 0 || info.Size() > maxManifestBytes {
		return UpdateImageManifest{}, fmt.Errorf("update.img manifest size must be between 1 and %d bytes", maxManifestBytes)
	}
	f, err := os.Open(path)
	if err != nil {
		return UpdateImageManifest{}, fmt.Errorf("open update.img manifest: %w", err)
	}
	defer f.Close()
	var manifest UpdateImageManifest
	decoder := json.NewDecoder(io.LimitReader(f, maxManifestBytes+1))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&manifest); err != nil {
		return UpdateImageManifest{}, fmt.Errorf("decode update.img manifest: %w", err)
	}
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		if err == nil {
			return UpdateImageManifest{}, errors.New("update.img manifest must contain one JSON object")
		}
		return UpdateImageManifest{}, fmt.Errorf("decode trailing update.img manifest data: %w", err)
	}
	if err := manifest.Validate(); err != nil {
		return UpdateImageManifest{}, err
	}
	return manifest, nil
}

// ValidateUpdateImageStage re-checks the immutable stage after a reboot.  It
// compares the parsed RKFW/RKAF layout to the persisted manifest and hashes
// the whole package as well as every item that recovery may consume.
func ValidateUpdateImageStage(manifestPath string, testMode bool) (UpdateImageManifest, string, error) {
	manifest, err := LoadUpdateImageManifest(manifestPath, testMode)
	if err != nil {
		return UpdateImageManifest{}, "", err
	}
	imagePath := filepath.Join(filepath.Dir(filepath.Clean(manifestPath)), UpdateImageFileName)
	info, err := secureRegularFile(imagePath, testMode)
	if err != nil {
		return UpdateImageManifest{}, "", fmt.Errorf("update.img: %w", err)
	}
	if info.Size() != manifest.Size {
		return UpdateImageManifest{}, "", fmt.Errorf("update.img size %d does not match manifest %d", info.Size(), manifest.Size)
	}
	f, err := os.Open(imagePath)
	if err != nil {
		return UpdateImageManifest{}, "", fmt.Errorf("open update.img: %w", err)
	}
	defer f.Close()
	if err := syscall.Flock(int(f.Fd()), syscall.LOCK_SH|syscall.LOCK_NB); err != nil {
		return UpdateImageManifest{}, "", fmt.Errorf("lock update.img: %w", err)
	}
	defer syscall.Flock(int(f.Fd()), syscall.LOCK_UN) //nolint:errcheck
	parsed, err := rkimage.Inspect(f, info.Size())
	if err != nil {
		return UpdateImageManifest{}, "", fmt.Errorf("inspect staged update.img: %w", err)
	}
	if len(parsed.Items) != len(manifest.Items) {
		return UpdateImageManifest{}, "", errors.New("staged update.img item count changed")
	}
	for index, item := range parsed.Items {
		want := manifest.Items[index]
		if item.Name != want.Name || item.Offset != want.Offset || item.Size != want.Size || item.FlashOffset != want.FlashOffset {
			return UpdateImageManifest{}, "", fmt.Errorf("staged update.img item %d metadata changed", index)
		}
		got, err := hashReaderAtRange(f, item.Offset, item.Size)
		if err != nil {
			return UpdateImageManifest{}, "", fmt.Errorf("hash staged update.img item %s: %w", item.Name, err)
		}
		if got != want.SHA256 {
			return UpdateImageManifest{}, "", fmt.Errorf("staged update.img item %s sha256 does not match manifest", item.Name)
		}
	}
	if current, err := f.Stat(); err != nil || !os.SameFile(info, current) || current.Size() != manifest.Size {
		return UpdateImageManifest{}, "", errors.New("update.img identity changed during validation")
	}
	if got, err := hashOpenFile(f, manifest.Size); err != nil {
		return UpdateImageManifest{}, "", err
	} else if got != manifest.SHA256 {
		return UpdateImageManifest{}, "", fmt.Errorf("update.img sha256 %s does not match manifest %s", got, manifest.SHA256)
	}
	return manifest, imagePath, nil
}

// BuildUpdateImageManifest is used only while the normal system initially
// stages an upload.  It records a deterministic package-order item list and
// item hashes that the recovery environment repeats before any write.
func BuildUpdateImageManifest(imagePath, expectedParent string) (UpdateImageManifest, error) {
	info, err := secureRegularFile(imagePath, true)
	if err != nil {
		return UpdateImageManifest{}, err
	}
	if info.Size() <= 0 {
		return UpdateImageManifest{}, errors.New("update.img is empty")
	}
	f, err := os.Open(imagePath)
	if err != nil {
		return UpdateImageManifest{}, err
	}
	defer f.Close()
	parsed, err := rkimage.Inspect(f, info.Size())
	if err != nil {
		return UpdateImageManifest{}, err
	}
	items := make([]UpdateImageItem, 0, len(parsed.Items))
	for _, item := range parsed.Items {
		digest, err := hashReaderAtRange(f, item.Offset, item.Size)
		if err != nil {
			return UpdateImageManifest{}, fmt.Errorf("hash update.img item %s: %w", item.Name, err)
		}
		items = append(items, UpdateImageItem{Name: item.Name, Offset: item.Offset, Size: item.Size, FlashOffset: item.FlashOffset, SHA256: digest})
	}
	// Defensive ordering: Inspect currently preserves the RKAF table order;
	// package offsets are expected to be monotonic.  Sorting makes a malformed
	// non-monotonic package fail manifest validation rather than creating a
	// potentially ambiguous recovery record.
	sort.SliceStable(items, func(i, j int) bool { return items[i].Offset < items[j].Offset })
	digest, err := hashOpenFile(f, info.Size())
	if err != nil {
		return UpdateImageManifest{}, err
	}
	manifest := UpdateImageManifest{
		Schema: UpdateImageSchema, Image: UpdateImageFileName, Size: info.Size(), SHA256: digest,
		ExpectedParent: expectedParent, Items: items,
	}
	if err := manifest.Validate(); err != nil {
		return UpdateImageManifest{}, err
	}
	return manifest, nil
}

func hashReaderAtRange(reader io.ReaderAt, offset, size uint64) (string, error) {
	if size == 0 || offset > uint64(^uint64(0)>>1) || size > uint64(^uint64(0)>>1) {
		return "", errors.New("unsafe file range")
	}
	section := io.NewSectionReader(reader, int64(offset), int64(size))
	hash := sha256.New()
	if copied, err := io.Copy(hash, section); err != nil || uint64(copied) != size {
		if err != nil {
			return "", err
		}
		return "", io.ErrUnexpectedEOF
	}
	return hex.EncodeToString(hash.Sum(nil)), nil
}

// ReadUpdateImageItem reads a bounded package item for compatibility checks
// (currently the parameter entry).  Callers cannot select arbitrary paths or
// offsets: the item must be present in a validated manifest.
func ReadUpdateImageItem(imagePath string, item UpdateImageItem, maxBytes int64) ([]byte, error) {
	if maxBytes <= 0 || item.Size > uint64(maxBytes) {
		return nil, fmt.Errorf("update.img item %s exceeds read limit", item.Name)
	}
	f, err := os.Open(imagePath)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	buffer := make([]byte, int(item.Size))
	if _, err := f.ReadAt(buffer, int64(item.Offset)); err != nil {
		return nil, err
	}
	return buffer, nil
}
