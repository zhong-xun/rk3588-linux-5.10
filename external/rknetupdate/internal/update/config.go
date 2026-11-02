package update

import (
	"bufio"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"rknetupdate/internal/bundlecontrol"
	"rknetupdate/internal/rootfscontrol"
	"rknetupdate/internal/updateimgcontrol"
)

type Config struct {
	Listen                    string
	Token                     string
	UploadDir                 string
	MaxUploadBytes            int64
	AllowFlash                bool
	DryRun                    bool
	TestMode                  bool
	UbootDevice               string
	BootDevice                string
	RootfsEnabled             bool
	RootfsAllowWrite          bool
	RootfsDevice              string
	RootfsStageDir            string
	RootfsE2fsckPath          string
	MaxRootfsUploadBytes      int64
	BundleEnabled             bool
	BundleAllowWrite          bool
	BundleStageDir            string
	MaxBundleUploadBytes      int64
	UpdateImageEnabled        bool
	UpdateImageAllowWrite     bool
	UpdateImageStageDir       string
	MaxUpdateImageUploadBytes int64
	ExpectedParentDevice      string
	ConfirmTTL                time.Duration
	ShutdownTimeout           time.Duration
}

// DefaultToken is the factory LAN credential used when no per-device token is
// configured. The HTTP API remains authenticated and the bundled web console
// requires the operator to enter this value instead of embedding it in JS.
const DefaultToken = "topeet"

func DefaultConfig() Config {
	return Config{
		Listen:                    "0.0.0.0:17888",
		Token:                     DefaultToken,
		UploadDir:                 "/var/lib/rknetupdate/uploads",
		MaxUploadBytes:            128 << 20,
		MaxRootfsUploadBytes:      8 << 30,
		DryRun:                    true,
		UbootDevice:               "/dev/disk/by-partlabel/uboot",
		BootDevice:                "/dev/disk/by-partlabel/boot",
		RootfsDevice:              "/dev/disk/by-partlabel/rootfs",
		RootfsStageDir:            "/var/lib/rknetupdate/rootfs-v2",
		RootfsE2fsckPath:          "/usr/sbin/e2fsck",
		BundleStageDir:            bundlecontrol.ProductionStageDir,
		MaxBundleUploadBytes:      (8 << 30) + (2 * (128 << 20)),
		UpdateImageStageDir:       updateimgcontrol.ProductionStageDir,
		MaxUpdateImageUploadBytes: 8 << 30,
		ConfirmTTL:                15 * time.Minute,
		ShutdownTimeout:           10 * time.Second,
	}
}

func ConfigFromArgs(args []string) (Config, error) {
	return configFromArgs(args, os.Stderr)
}

// configFromArgs applies file and environment configuration before explicit
// command-line overrides.  Keeping the flag output injectable lets tests
// assert that configuration secrets never become flag defaults in usage text.
func configFromArgs(args []string, flagOutput io.Writer) (Config, error) {
	cfg := DefaultConfig()
	configPath := "/etc/rknetupdate.conf"
	for i := 0; i < len(args); i++ {
		if args[i] == "--config" && i+1 < len(args) {
			configPath = args[i+1]
		} else if strings.HasPrefix(args[i], "--config=") {
			configPath = strings.TrimPrefix(args[i], "--config=")
		}
	}
	if configPath != "" {
		if err := applyConfigFile(&cfg, configPath); err != nil && !errors.Is(err, os.ErrNotExist) {
			return Config{}, err
		}
	}
	if err := applyEnv(&cfg); err != nil {
		return Config{}, err
	}

	fs := flag.NewFlagSet("rknetupdate", flag.ContinueOnError)
	fs.SetOutput(flagOutput)
	fs.String("config", configPath, "configuration file")
	fs.StringVar(&cfg.Listen, "listen", cfg.Listen, "HTTP listen address")
	// Do not use cfg.Token as a flag default: flag's usage formatter prints
	// non-empty defaults, which would disclose a token loaded from config.
	var tokenOverride string
	fs.StringVar(&tokenOverride, "token", "", "API bearer token (prefer RKNETUPDATE_TOKEN)")
	fs.StringVar(&cfg.UploadDir, "upload-dir", cfg.UploadDir, "temporary upload directory")
	maxMiB := cfg.MaxUploadBytes >> 20
	fs.Int64Var(&maxMiB, "max-upload-mib", maxMiB, "maximum image size in MiB")
	fs.BoolVar(&cfg.AllowFlash, "allow-flash", cfg.AllowFlash, "permit writes to configured partitions")
	fs.BoolVar(&cfg.DryRun, "dry-run", cfg.DryRun, "validate the workflow without writing")
	fs.BoolVar(&cfg.TestMode, "test-mode", cfg.TestMode, "allow regular files as explicit partition targets")
	fs.StringVar(&cfg.UbootDevice, "uboot-device", cfg.UbootDevice, "uboot partition path")
	fs.StringVar(&cfg.BootDevice, "boot-device", cfg.BootDevice, "boot partition path")
	fs.BoolVar(&cfg.RootfsEnabled, "rootfs-enabled", cfg.RootfsEnabled, "enable the recovery rootfs upload workflow")
	fs.BoolVar(&cfg.RootfsAllowWrite, "rootfs-allow-write", cfg.RootfsAllowWrite, "allow rootfs recovery BCB preparation and reboot")
	fs.StringVar(&cfg.RootfsDevice, "rootfs-device", cfg.RootfsDevice, "rootfs partition path")
	fs.StringVar(&cfg.RootfsStageDir, "rootfs-stage-dir", cfg.RootfsStageDir, "rootfs v2 staging directory")
	fs.StringVar(&cfg.RootfsE2fsckPath, "rootfs-e2fsck", cfg.RootfsE2fsckPath, "e2fsck binary used for rootfs validation")
	rootfsMaxMiB := cfg.MaxRootfsUploadBytes >> 20
	fs.Int64Var(&rootfsMaxMiB, "rootfs-max-upload-mib", rootfsMaxMiB, "maximum rootfs image size in MiB")
	fs.BoolVar(&cfg.BundleEnabled, "bundle-enabled", cfg.BundleEnabled, "enable offline rootfs+boot+uboot bundle uploads")
	fs.BoolVar(&cfg.BundleAllowWrite, "bundle-allow-write", cfg.BundleAllowWrite, "allow bundle recovery BCB preparation and reboot")
	fs.StringVar(&cfg.BundleStageDir, "bundle-stage-dir", cfg.BundleStageDir, "offline multi-image bundle staging directory")
	bundleMaxMiB := cfg.MaxBundleUploadBytes >> 20
	fs.Int64Var(&bundleMaxMiB, "bundle-max-upload-mib", bundleMaxMiB, "maximum total bundle size in MiB")
	fs.BoolVar(&cfg.UpdateImageEnabled, "updateimg-enabled", cfg.UpdateImageEnabled, "enable complete Rockchip update.img OTA uploads")
	fs.BoolVar(&cfg.UpdateImageAllowWrite, "updateimg-allow-write", cfg.UpdateImageAllowWrite, "allow update.img recovery BCB preparation and reboot")
	fs.StringVar(&cfg.UpdateImageStageDir, "updateimg-stage-dir", cfg.UpdateImageStageDir, "complete update.img staging directory")
	updateImageMaxMiB := cfg.MaxUpdateImageUploadBytes >> 20
	fs.Int64Var(&updateImageMaxMiB, "updateimg-max-upload-mib", updateImageMaxMiB, "maximum complete update.img size in MiB")
	fs.StringVar(&cfg.ExpectedParentDevice, "expected-parent-device", cfg.ExpectedParentDevice, "expected parent disk for real flash mode")
	if err := fs.Parse(args); err != nil {
		return Config{}, err
	}
	fs.Visit(func(f *flag.Flag) {
		if f.Name == "token" {
			cfg.Token = tokenOverride
		}
	})
	if maxMiB < 1 || maxMiB > 4096 {
		return Config{}, fmt.Errorf("max-upload-mib must be between 1 and 4096")
	}
	cfg.MaxUploadBytes = maxMiB << 20
	if rootfsMaxMiB < 16 || rootfsMaxMiB > 65536 {
		return Config{}, fmt.Errorf("rootfs-max-upload-mib must be between 16 and 65536")
	}
	cfg.MaxRootfsUploadBytes = rootfsMaxMiB << 20
	if bundleMaxMiB < 16 || bundleMaxMiB > 65536 {
		return Config{}, fmt.Errorf("bundle-max-upload-mib must be between 16 and 65536")
	}
	cfg.MaxBundleUploadBytes = bundleMaxMiB << 20
	if updateImageMaxMiB < 16 || updateImageMaxMiB > 65536 {
		return Config{}, fmt.Errorf("updateimg-max-upload-mib must be between 16 and 65536")
	}
	cfg.MaxUpdateImageUploadBytes = updateImageMaxMiB << 20
	// An unset (or deliberately blank) configuration keeps the factory LAN
	// default.  Installations that need a different credential can still use a
	// config file, RKNETUPDATE_TOKEN, or --token.
	if strings.TrimSpace(cfg.Token) == "" {
		cfg.Token = DefaultToken
	}
	if cfg.Listen == "" {
		return Config{}, errors.New("listen address is required")
	}
	if !filepath.IsAbs(cfg.UploadDir) {
		return Config{}, errors.New("upload directory must be absolute")
	}
	for name, path := range map[string]string{
		"rootfs-device":       cfg.RootfsDevice,
		"rootfs-stage-dir":    cfg.RootfsStageDir,
		"rootfs-e2fsck":       cfg.RootfsE2fsckPath,
		"bundle-stage-dir":    cfg.BundleStageDir,
		"updateimg-stage-dir": cfg.UpdateImageStageDir,
	} {
		if (cfg.RootfsEnabled || cfg.BundleEnabled || cfg.UpdateImageEnabled) && !filepath.IsAbs(path) {
			return Config{}, fmt.Errorf("%s must be an absolute path", name)
		}
	}
	if cfg.RootfsEnabled && !cfg.TestMode {
		if filepath.Clean(cfg.RootfsDevice) != "/dev/disk/by-partlabel/rootfs" {
			return Config{}, errors.New("production rootfs-device is fixed at /dev/disk/by-partlabel/rootfs")
		}
		if filepath.Clean(cfg.RootfsStageDir) != "/var/lib/rknetupdate/rootfs-v2" {
			return Config{}, errors.New("production rootfs-stage-dir is fixed at /var/lib/rknetupdate/rootfs-v2")
		}
		if !rootfscontrol.IsSupportedProductionE2fsck(cfg.RootfsE2fsckPath) {
			return Config{}, errors.New("production rootfs-e2fsck must be /usr/sbin/e2fsck or /sbin/e2fsck")
		}
	}
	if cfg.BundleEnabled && !cfg.TestMode {
		if filepath.Clean(cfg.BundleStageDir) != bundlecontrol.ProductionStageDir {
			return Config{}, fmt.Errorf("production bundle-stage-dir is fixed at %s", bundlecontrol.ProductionStageDir)
		}
	}
	if cfg.UpdateImageEnabled && !cfg.TestMode {
		if filepath.Clean(cfg.UpdateImageStageDir) != updateimgcontrol.ProductionStageDir {
			return Config{}, fmt.Errorf("production updateimg-stage-dir is fixed at %s", updateimgcontrol.ProductionStageDir)
		}
	}
	if !cfg.DryRun && !cfg.AllowFlash {
		return Config{}, errors.New("real flash mode requires --allow-flash")
	}
	if cfg.ExpectedParentDevice != "" && !filepath.IsAbs(cfg.ExpectedParentDevice) {
		return Config{}, errors.New("expected-parent-device must be an absolute path")
	}
	if cfg.AllowFlash && !cfg.DryRun && cfg.ExpectedParentDevice == "" {
		return Config{}, errors.New("real flash mode requires expected-parent-device")
	}
	if cfg.RootfsEnabled && !cfg.TestMode && filepath.Clean(cfg.ExpectedParentDevice) != "/dev/mmcblk0" {
		return Config{}, errors.New("production rootfs recovery requires expected_parent_device=/dev/mmcblk0")
	}
	if cfg.BundleEnabled && !cfg.TestMode && filepath.Clean(cfg.ExpectedParentDevice) != bundlecontrol.ProductionParent {
		return Config{}, errors.New("production bundle recovery requires expected_parent_device=/dev/mmcblk0")
	}
	if cfg.UpdateImageEnabled && !cfg.TestMode && filepath.Clean(cfg.ExpectedParentDevice) != updateimgcontrol.ProductionParent {
		return Config{}, errors.New("production update.img recovery requires expected_parent_device=/dev/mmcblk0")
	}
	if cfg.RootfsAllowWrite && !cfg.RootfsEnabled {
		return Config{}, errors.New("rootfs-allow-write requires rootfs-enabled=true")
	}
	if cfg.BundleAllowWrite && !cfg.BundleEnabled {
		return Config{}, errors.New("bundle-allow-write requires bundle-enabled=true")
	}
	if cfg.UpdateImageAllowWrite && !cfg.UpdateImageEnabled {
		return Config{}, errors.New("updateimg-allow-write requires updateimg-enabled=true")
	}
	return cfg, nil
}

func applyConfigFile(cfg *Config, path string) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	values := map[string]string{}
	s := bufio.NewScanner(f)
	for s.Scan() {
		line := strings.TrimSpace(s.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		key, value, ok := strings.Cut(line, "=")
		if !ok {
			return fmt.Errorf("%s: invalid configuration line %q", path, line)
		}
		values[strings.TrimSpace(key)] = strings.TrimSpace(value)
	}
	if err := s.Err(); err != nil {
		return err
	}
	return applyValues(cfg, values)
}

func applyEnv(cfg *Config) error {
	values := map[string]string{}
	for key, env := range map[string]string{
		"listen": "RKNETUPDATE_LISTEN", "token": "RKNETUPDATE_TOKEN",
		"upload_dir": "RKNETUPDATE_UPLOAD_DIR", "max_upload_mib": "RKNETUPDATE_MAX_UPLOAD_MIB",
		"allow_flash": "RKNETUPDATE_ALLOW_FLASH", "dry_run": "RKNETUPDATE_DRY_RUN",
		"test_mode": "RKNETUPDATE_TEST_MODE", "uboot_device": "RKNETUPDATE_UBOOT_DEVICE",
		"boot_device": "RKNETUPDATE_BOOT_DEVICE", "expected_parent_device": "RKNETUPDATE_EXPECTED_PARENT_DEVICE",
		"rootfs_enabled": "RKNETUPDATE_ROOTFS_ENABLED", "rootfs_allow_write": "RKNETUPDATE_ROOTFS_ALLOW_WRITE", "rootfs_device": "RKNETUPDATE_ROOTFS_DEVICE",
		"rootfs_stage_dir": "RKNETUPDATE_ROOTFS_STAGE_DIR", "rootfs_e2fsck": "RKNETUPDATE_ROOTFS_E2FSCK",
		"rootfs_max_upload_mib": "RKNETUPDATE_ROOTFS_MAX_UPLOAD_MIB",
		"bundle_enabled":        "RKNETUPDATE_BUNDLE_ENABLED", "bundle_allow_write": "RKNETUPDATE_BUNDLE_ALLOW_WRITE",
		"bundle_stage_dir": "RKNETUPDATE_BUNDLE_STAGE_DIR", "bundle_max_upload_mib": "RKNETUPDATE_BUNDLE_MAX_UPLOAD_MIB",
		"updateimg_enabled": "RKNETUPDATE_UPDATEIMG_ENABLED", "updateimg_allow_write": "RKNETUPDATE_UPDATEIMG_ALLOW_WRITE",
		"updateimg_stage_dir": "RKNETUPDATE_UPDATEIMG_STAGE_DIR", "updateimg_max_upload_mib": "RKNETUPDATE_UPDATEIMG_MAX_UPLOAD_MIB",
	} {
		if value, ok := os.LookupEnv(env); ok {
			values[key] = value
		}
	}
	return applyValues(cfg, values)
}

func applyValues(cfg *Config, values map[string]string) error {
	for key, value := range values {
		switch key {
		case "listen":
			cfg.Listen = value
		case "token":
			cfg.Token = value
		case "upload_dir":
			cfg.UploadDir = value
		case "max_upload_mib":
			v, err := strconv.ParseInt(value, 10, 64)
			if err != nil {
				return fmt.Errorf("invalid %s: %w", key, err)
			}
			cfg.MaxUploadBytes = v << 20
		case "allow_flash", "dry_run", "test_mode", "rootfs_enabled", "rootfs_allow_write", "bundle_enabled", "bundle_allow_write", "updateimg_enabled", "updateimg_allow_write":
			v, err := strconv.ParseBool(value)
			if err != nil {
				return fmt.Errorf("invalid %s: %w", key, err)
			}
			switch key {
			case "allow_flash":
				cfg.AllowFlash = v
			case "dry_run":
				cfg.DryRun = v
			case "test_mode":
				cfg.TestMode = v
			case "rootfs_enabled":
				cfg.RootfsEnabled = v
			case "rootfs_allow_write":
				cfg.RootfsAllowWrite = v
			case "bundle_enabled":
				cfg.BundleEnabled = v
			case "bundle_allow_write":
				cfg.BundleAllowWrite = v
			case "updateimg_enabled":
				cfg.UpdateImageEnabled = v
			case "updateimg_allow_write":
				cfg.UpdateImageAllowWrite = v
			}
		case "uboot_device":
			cfg.UbootDevice = value
		case "boot_device":
			cfg.BootDevice = value
		case "rootfs_device":
			cfg.RootfsDevice = value
		case "rootfs_stage_dir":
			cfg.RootfsStageDir = value
		case "rootfs_e2fsck":
			cfg.RootfsE2fsckPath = value
		case "rootfs_max_upload_mib":
			v, err := strconv.ParseInt(value, 10, 64)
			if err != nil {
				return fmt.Errorf("invalid %s: %w", key, err)
			}
			cfg.MaxRootfsUploadBytes = v << 20
		case "bundle_stage_dir":
			cfg.BundleStageDir = value
		case "bundle_max_upload_mib":
			v, err := strconv.ParseInt(value, 10, 64)
			if err != nil {
				return fmt.Errorf("invalid %s: %w", key, err)
			}
			cfg.MaxBundleUploadBytes = v << 20
		case "updateimg_stage_dir":
			cfg.UpdateImageStageDir = value
		case "updateimg_max_upload_mib":
			v, err := strconv.ParseInt(value, 10, 64)
			if err != nil {
				return fmt.Errorf("invalid %s: %w", key, err)
			}
			cfg.MaxUpdateImageUploadBytes = v << 20
		case "expected_parent_device":
			cfg.ExpectedParentDevice = value
		default:
			return fmt.Errorf("unknown configuration key %q", key)
		}
	}
	return nil
}
