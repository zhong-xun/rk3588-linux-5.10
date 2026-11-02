package smokecontrol

import (
	"crypto/subtle"
	"errors"
	"fmt"
	"path/filepath"
	"strings"

	"rknetupdate/internal/recoveryapply"
)

const (
	ProductionMiscPath           = "/dev/disk/by-partlabel/misc"
	ProductionExpectedParentPath = "/dev/mmcblk0"

	PrepareConfirmation = "PREPARE RKNETUPDATE RECOVERY SMOKE"
	RebootConfirmation  = "REBOOT INTO RKNETUPDATE RECOVERY SMOKE"
)

type Config struct {
	MiscPath           string
	ExpectedParentPath string
	Confirmation       string
	TestMode           bool
}

type Result struct {
	Schema           int    `json:"schema"`
	Action           string `json:"action"`
	Success          bool   `json:"success"`
	MiscPath         string `json:"misc_path"`
	ResolvedMiscPath string `json:"resolved_misc_path,omitempty"`
	ExpectedParent   string `json:"expected_parent"`
	BCBCommand       string `json:"bcb_command,omitempty"`
	BCBRecovery      string `json:"bcb_recovery,omitempty"`
	RebootRequested  bool   `json:"reboot_requested"`
	Error            string `json:"error,omitempty"`
}

func DefaultConfig() Config {
	return Config{
		MiscPath:           ProductionMiscPath,
		ExpectedParentPath: ProductionExpectedParentPath,
	}
}

func (c Config) Validate() error {
	if c.MiscPath == "" || !filepath.IsAbs(c.MiscPath) {
		return errors.New("misc path must be absolute")
	}
	if c.ExpectedParentPath == "" || !filepath.IsAbs(c.ExpectedParentPath) {
		return errors.New("expected parent path must be absolute")
	}
	if c.TestMode {
		parent := filepath.Clean(c.ExpectedParentPath)
		if strings.HasPrefix(parent, "/dev/") && !strings.HasPrefix(filepath.Base(parent), "loop") {
			return errors.New("test mode only accepts regular files or loop devices")
		}
		return nil
	}
	if filepath.Clean(c.MiscPath) != ProductionMiscPath {
		return fmt.Errorf("production misc path is fixed at %s", ProductionMiscPath)
	}
	if filepath.Clean(c.ExpectedParentPath) != ProductionExpectedParentPath {
		return fmt.Errorf("production expected parent path is fixed at %s", ProductionExpectedParentPath)
	}
	return nil
}

func Prepare(cfg Config) (Result, error) {
	result := newResult("prepare-smoke", cfg)
	if err := cfg.Validate(); err != nil {
		return failed(result, err)
	}
	if err := requireConfirmation(cfg.Confirmation, PrepareConfirmation); err != nil {
		return failed(result, err)
	}
	misc, err := recoveryapply.ResolveDevice(cfg.MiscPath, "misc", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return failed(result, fmt.Errorf("validate misc partition: %w", err))
	}
	result.ResolvedMiscPath = misc.Path
	if err := recoveryapply.PrepareSmokeBCB(misc); err != nil {
		return failed(result, err)
	}
	if err := recoveryapply.VerifySmokeBCB(misc); err != nil {
		return failed(result, fmt.Errorf("verify prepared smoke BCB: %w", err))
	}
	result.Success = true
	result.BCBCommand = recoveryapply.SmokeBCBCommand
	result.BCBRecovery = recoveryapply.SmokeRecoveryArgument
	return result, nil
}

func VerifyPrepared(cfg Config) (Result, error) {
	result := newResult("reboot-smoke", cfg)
	if err := cfg.Validate(); err != nil {
		return failed(result, err)
	}
	if err := requireConfirmation(cfg.Confirmation, RebootConfirmation); err != nil {
		return failed(result, err)
	}
	misc, err := recoveryapply.ResolveDevice(cfg.MiscPath, "misc", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return failed(result, fmt.Errorf("validate misc partition: %w", err))
	}
	result.ResolvedMiscPath = misc.Path
	if err := recoveryapply.VerifySmokeBCB(misc); err != nil {
		return failed(result, fmt.Errorf("verify prepared smoke BCB: %w", err))
	}
	result.Success = true
	result.BCBCommand = recoveryapply.SmokeBCBCommand
	result.BCBRecovery = recoveryapply.SmokeRecoveryArgument
	result.RebootRequested = true
	return result, nil
}

func newResult(action string, cfg Config) Result {
	return Result{
		Schema:         1,
		Action:         action,
		MiscPath:       cfg.MiscPath,
		ExpectedParent: cfg.ExpectedParentPath,
	}
}

func requireConfirmation(actual, expected string) error {
	if subtle.ConstantTimeCompare([]byte(actual), []byte(expected)) != 1 {
		return fmt.Errorf("confirmation phrase must exactly match %q", expected)
	}
	return nil
}

func failed(result Result, err error) (Result, error) {
	result.Success = false
	result.Error = err.Error()
	return result, err
}
