package recoveryapply

import (
	"errors"
	"fmt"
	"path/filepath"
	"strings"
)

const (
	ProductionManifestPath       = "/userdata/rootfs.manifest.json"
	ProductionStatusPath         = "/userdata/rootfs.status.json"
	ProductionRequestPath        = "/userdata/rootfs.apply"
	ProductionTargetPath         = "/dev/disk/by-partlabel/rootfs"
	ProductionMiscPath           = "/dev/disk/by-partlabel/misc"
	ProductionUserdataPath       = "/dev/disk/by-partlabel/userdata"
	ProductionExpectedParentPath = "/dev/mmcblk0"
	ProductionMountInfoPath      = "/proc/self/mountinfo"
	ProductionRecoveryMarkerPath = "/etc/rknetupdate-recovery"
	ProductionE2fsckPath         = "/sbin/e2fsck"
	ProductionSmokeStatusPath    = "/run/rknetupdate-smoke.status.json"

	RecoveryMarker = "rknetupdate-recovery-v1\n"
)

type Config struct {
	ManifestPath       string
	StatusPath         string
	RequestPath        string
	TargetPath         string
	MiscPath           string
	UserdataPath       string
	ExpectedParentPath string
	MountInfoPath      string
	RecoveryMarkerPath string
	E2fsckPath         string
	SmokeStatusPath    string
	Apply              bool
	AllowWrite         bool
	Confirmation       string
	Smoke              bool
	TestMode           bool
	Progress           func(phase, detail string)
	WriteProgress      WriteProgress
}

func DefaultConfig() Config {
	return Config{
		ManifestPath:       ProductionManifestPath,
		StatusPath:         ProductionStatusPath,
		RequestPath:        ProductionRequestPath,
		TargetPath:         ProductionTargetPath,
		MiscPath:           ProductionMiscPath,
		UserdataPath:       ProductionUserdataPath,
		ExpectedParentPath: ProductionExpectedParentPath,
		MountInfoPath:      ProductionMountInfoPath,
		RecoveryMarkerPath: ProductionRecoveryMarkerPath,
		E2fsckPath:         ProductionE2fsckPath,
		SmokeStatusPath:    ProductionSmokeStatusPath,
	}
}

func (c Config) Validate() error {
	paths := map[string]string{
		"manifest":        c.ManifestPath,
		"status":          c.StatusPath,
		"request":         c.RequestPath,
		"target":          c.TargetPath,
		"misc":            c.MiscPath,
		"expected-parent": c.ExpectedParentPath,
		"mountinfo":       c.MountInfoPath,
		"recovery-marker": c.RecoveryMarkerPath,
		"e2fsck":          c.E2fsckPath,
	}
	if c.Smoke {
		paths["smoke-status"] = c.SmokeStatusPath
	}
	if !c.TestMode {
		paths["userdata"] = c.UserdataPath
	}
	for name, path := range paths {
		if path == "" || !filepath.IsAbs(path) {
			return fmt.Errorf("%s path must be absolute", name)
		}
	}
	if c.Smoke {
		if c.Apply || c.AllowWrite || c.Confirmation != "" {
			return errors.New("--smoke cannot be combined with --apply, --allow-write, or --confirm")
		}
	} else if c.Apply {
		if !c.AllowWrite {
			return errors.New("--apply requires --allow-write")
		}
		if strings.TrimSpace(c.Confirmation) == "" {
			return errors.New("--apply requires --confirm")
		}
	} else {
		if c.AllowWrite {
			return errors.New("--allow-write is only valid with --apply")
		}
		if c.Confirmation != "" {
			return errors.New("--confirm is only valid with --apply")
		}
	}
	if c.TestMode {
		return validateTestPaths(c)
	}

	expected := map[string][2]string{
		"manifest":        {c.ManifestPath, ProductionManifestPath},
		"status":          {c.StatusPath, ProductionStatusPath},
		"request":         {c.RequestPath, ProductionRequestPath},
		"target":          {c.TargetPath, ProductionTargetPath},
		"misc":            {c.MiscPath, ProductionMiscPath},
		"userdata":        {c.UserdataPath, ProductionUserdataPath},
		"expected-parent": {c.ExpectedParentPath, ProductionExpectedParentPath},
		"mountinfo":       {c.MountInfoPath, ProductionMountInfoPath},
		"recovery-marker": {c.RecoveryMarkerPath, ProductionRecoveryMarkerPath},
		"e2fsck":          {c.E2fsckPath, ProductionE2fsckPath},
	}
	if c.Smoke {
		expected["smoke-status"] = [2]string{c.SmokeStatusPath, ProductionSmokeStatusPath}
	}
	for name, pair := range expected {
		if filepath.Clean(pair[0]) != pair[1] {
			return fmt.Errorf("production %s path is fixed at %s", name, pair[1])
		}
	}
	return nil
}

func validateTestPaths(c Config) error {
	parent := filepath.Clean(c.ExpectedParentPath)
	if strings.HasPrefix(parent, "/dev/") && !strings.HasPrefix(filepath.Base(parent), "loop") {
		return errors.New("test mode only accepts regular files or loop devices")
	}
	return nil
}
