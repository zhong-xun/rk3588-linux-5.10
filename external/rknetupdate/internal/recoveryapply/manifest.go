package recoveryapply

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strings"
)

const maxManifestBytes = 64 << 10

var sha256Pattern = regexp.MustCompile(`^[0-9a-f]{64}$`)

type Manifest struct {
	Schema          int    `json:"schema"`
	Image           string `json:"image"`
	Size            int64  `json:"size"`
	SHA256          string `json:"sha256"`
	Filesystem      string `json:"filesystem"`
	TargetPartlabel string `json:"target_partlabel"`
	ExpectedParent  string `json:"expected_parent"`
}

func LoadManifest(path string, testMode bool) (Manifest, string, error) {
	info, err := secureRegularFile(path, testMode)
	if err != nil {
		return Manifest{}, "", fmt.Errorf("manifest: %w", err)
	}
	if info.Size() <= 0 || info.Size() > maxManifestBytes {
		return Manifest{}, "", fmt.Errorf("manifest size must be between 1 and %d bytes", maxManifestBytes)
	}
	f, err := os.Open(path)
	if err != nil {
		return Manifest{}, "", fmt.Errorf("open manifest: %w", err)
	}
	defer f.Close()

	var manifest Manifest
	decoder := json.NewDecoder(io.LimitReader(f, maxManifestBytes+1))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&manifest); err != nil {
		return Manifest{}, "", fmt.Errorf("decode manifest: %w", err)
	}
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		if err == nil {
			return Manifest{}, "", errors.New("manifest must contain one JSON object")
		}
		return Manifest{}, "", fmt.Errorf("decode trailing manifest data: %w", err)
	}
	if err := manifest.Validate(); err != nil {
		return Manifest{}, "", err
	}
	imagePath := filepath.Join(filepath.Dir(filepath.Clean(path)), manifest.Image)
	if filepath.Base(imagePath) != "rootfs.img" {
		return Manifest{}, "", errors.New("manifest image must resolve to rootfs.img")
	}
	return manifest, imagePath, nil
}

func (m Manifest) Validate() error {
	switch {
	case m.Schema != 1:
		return fmt.Errorf("unsupported manifest schema %d", m.Schema)
	case m.Image != "rootfs.img":
		return errors.New("manifest image must be rootfs.img")
	case m.Size <= 0:
		return errors.New("manifest size must be positive")
	case !sha256Pattern.MatchString(m.SHA256):
		return errors.New("manifest sha256 must be 64 lowercase hexadecimal characters")
	case m.Filesystem != "ext4":
		return errors.New("manifest filesystem must be ext4")
	case m.TargetPartlabel != "rootfs":
		return errors.New("manifest target_partlabel must be rootfs")
	case m.ExpectedParent == "" || !filepath.IsAbs(m.ExpectedParent):
		return errors.New("manifest expected_parent must be an absolute device path")
	case strings.TrimSpace(m.ExpectedParent) != m.ExpectedParent:
		return errors.New("manifest expected_parent must not contain surrounding whitespace")
	}
	return nil
}

func ExpectedConfirmation(sha256 string) string {
	return "WRITE_ROOTFS:" + sha256
}

func secureRegularFile(path string, testMode bool) (os.FileInfo, error) {
	info, err := os.Lstat(path)
	if err != nil {
		return nil, err
	}
	if info.Mode()&os.ModeSymlink != 0 || !info.Mode().IsRegular() {
		return nil, errors.New("must be a regular non-symlink file")
	}
	if !testMode && info.Mode().Perm()&0022 != 0 {
		return nil, errors.New("must not be writable by group or other users")
	}
	return info, nil
}
