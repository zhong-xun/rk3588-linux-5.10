package recoveryapply

import (
	"context"
	"errors"
	"fmt"
)

func runSmoke(ctx context.Context, cfg Config) (Status, error) {
	status := newStatus(cfg)
	if err := cfg.Validate(); err != nil {
		status.Phase = "failed"
		status.Error = err.Error()
		return status, err
	}
	fail := func(cause error) (Status, error) {
		status.Phase = "failed"
		status.Success = false
		status.Error = cause.Error()
		if statusErr := writeStatus(cfg.SmokeStatusPath, status); statusErr != nil {
			return status, fmt.Errorf("%w; persist smoke failure status: %v", cause, statusErr)
		}
		return status, cause
	}
	setPhase := func(phase string) error {
		status.Phase = phase
		status.Error = ""
		return writeStatus(cfg.SmokeStatusPath, status)
	}

	if err := setPhase("checking"); err != nil {
		return status, err
	}
	if err := ctx.Err(); err != nil {
		return fail(err)
	}
	if err := verifyRecoveryMarker(cfg.RecoveryMarkerPath, cfg.TestMode); err != nil {
		return fail(err)
	}
	mounts, err := ReadMountInfo(cfg.MountInfoPath)
	if err != nil {
		return fail(err)
	}
	target, err := ResolveDevice(cfg.TargetPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return fail(fmt.Errorf("validate rootfs target: %w", err))
	}
	status.ResolvedTargetPath = target.Path
	if mounted, mountPoint := IsDeviceMounted(target, mounts); mounted {
		return fail(fmt.Errorf("rootfs target is mounted at %s", mountPoint))
	}
	misc, err := ResolveDevice(cfg.MiscPath, "misc", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return fail(fmt.Errorf("validate misc target: %w", err))
	}
	if misc.Size < bcbOffset+bcbSize {
		return fail(errors.New("misc target is too small for the BCB"))
	}
	if err := VerifySmokeBCB(misc); err != nil {
		return fail(fmt.Errorf("validate smoke BCB request: %w", err))
	}
	status.SmokeRequestVerified = true
	if err := setPhase("request_verified"); err != nil {
		return fail(err)
	}

	// Re-resolve both partitions and mount state immediately before the only
	// write in smoke mode: clearing the fixed Rockchip BCB request.
	if err := ctx.Err(); err != nil {
		return fail(err)
	}
	mounts, err = ReadMountInfo(cfg.MountInfoPath)
	if err != nil {
		return fail(err)
	}
	recheckedTarget, err := ResolveDevice(cfg.TargetPath, "rootfs", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return fail(fmt.Errorf("revalidate rootfs target: %w", err))
	}
	if !sameDevice(target, recheckedTarget) {
		return fail(errors.New("rootfs target identity changed during smoke validation"))
	}
	if mounted, mountPoint := IsDeviceMounted(recheckedTarget, mounts); mounted {
		return fail(fmt.Errorf("rootfs target became mounted at %s", mountPoint))
	}
	recheckedMisc, err := ResolveDevice(cfg.MiscPath, "misc", cfg.ExpectedParentPath, cfg.TestMode)
	if err != nil {
		return fail(fmt.Errorf("revalidate misc target: %w", err))
	}
	if !sameDevice(misc, recheckedMisc) {
		return fail(errors.New("misc target identity changed during smoke validation"))
	}
	if err := ClearSmokeBCB(recheckedMisc); err != nil {
		return fail(fmt.Errorf("clear smoke BCB request: %w", err))
	}
	status.BCBCleared = true
	status.Phase = "succeeded"
	status.Success = true
	status.Applied = false
	status.Error = ""
	if err := writeStatus(cfg.SmokeStatusPath, status); err != nil {
		return fail(fmt.Errorf("persist successful smoke status: %w", err))
	}
	return status, nil
}
