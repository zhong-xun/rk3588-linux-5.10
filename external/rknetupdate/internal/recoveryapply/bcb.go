package recoveryapply

import (
	"bytes"
	"errors"
	"fmt"
	"os"
	"strings"
)

const (
	bcbOffset          = 16 * 1024
	bcbSize            = 1088
	bcbCommandStart    = 0
	bcbCommandEnd      = 32
	bcbStatusEnd       = 64
	bcbRecoveryStart   = bcbStatusEnd
	bcbRecoveryEnd     = 832
	bcbNeedUpdateStart = bcbRecoveryEnd
	bcbNeedUpdateEnd   = 836
	bcbSystemFlagStart = 836

	SmokeBCBCommand       = "boot-recovery"
	SmokeRecoveryArgument = "recovery\n--rknetupdate-smoke-v1\n"
)

func readBCB(device Device) ([]byte, error) {
	if device.Size < bcbOffset+bcbSize {
		return nil, fmt.Errorf("misc partition is too small for BCB at offset %d", bcbOffset)
	}
	f, err := openDevice(device, os.O_RDONLY)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	buffer := make([]byte, bcbSize)
	if _, err := f.ReadAt(buffer, bcbOffset); err != nil {
		return nil, fmt.Errorf("read BCB: %w", err)
	}
	return buffer, nil
}

// ValidateUpdateImageMiscItem accepts the misc member of a complete package.
// The managed rkupdate invocation writes it with the rest of the image, and
// recovery clears the active OTA BCB only after the updater reports success.
func ValidateUpdateImageMiscItem(imagePath string, item UpdateImageItem) error {
	if item.Name != "misc" {
		return fmt.Errorf("expected misc item, got %q", item.Name)
	}
	return nil
}

func bcbCommand(buffer []byte) string {
	if len(buffer) < bcbCommandEnd {
		return ""
	}
	return strings.TrimRight(string(buffer[bcbCommandStart:bcbCommandEnd]), "\x00\xff")
}

// PrepareSmokeBCB installs the one-shot recovery smoke request only when all
// command, status, recovery, and needupdate bytes are zero.
func PrepareSmokeBCB(device Device) error {
	before, err := readBCB(device)
	if err != nil {
		return err
	}
	if !allZero(before[bcbCommandStart:bcbNeedUpdateEnd]) {
		return errors.New("refusing to prepare smoke BCB: command/status/recovery/needupdate region is not empty")
	}
	after := append([]byte(nil), before...)
	clear(after[bcbCommandStart:bcbNeedUpdateEnd])
	copy(after[bcbCommandStart:bcbCommandEnd], SmokeBCBCommand)
	copy(after[bcbRecoveryStart:bcbRecoveryEnd], SmokeRecoveryArgument)
	return replaceBCB(device, before, after)
}

// VerifySmokeBCB requires the exact fixed request written by PrepareSmokeBCB.
func VerifySmokeBCB(device Device) error {
	buffer, err := readBCB(device)
	if err != nil {
		return err
	}
	return validateSmokeBCB(buffer)
}

// ClearSmokeBCB verifies the exact smoke request before clearing only the
// command, status, recovery, and needupdate fields.
func ClearSmokeBCB(device Device) error {
	before, err := readBCB(device)
	if err != nil {
		return err
	}
	if err := validateSmokeBCB(before); err != nil {
		return err
	}
	after := append([]byte(nil), before...)
	clear(after[bcbCommandStart:bcbNeedUpdateEnd])
	return replaceBCB(device, before, after)
}

func validateSmokeBCB(buffer []byte) error {
	if len(buffer) != bcbSize {
		return fmt.Errorf("invalid Rockchip BCB size %d", len(buffer))
	}
	if !paddedFieldEquals(buffer[bcbCommandStart:bcbCommandEnd], SmokeBCBCommand) {
		return fmt.Errorf("BCB command does not exactly match %q", SmokeBCBCommand)
	}
	if !allZero(buffer[bcbCommandEnd:bcbStatusEnd]) {
		return errors.New("BCB status field is not empty")
	}
	if !paddedFieldEquals(buffer[bcbRecoveryStart:bcbRecoveryEnd], SmokeRecoveryArgument) {
		return errors.New("BCB recovery argument does not exactly match the RKNetUpdate smoke request")
	}
	if !allZero(buffer[bcbNeedUpdateStart:bcbNeedUpdateEnd]) {
		return errors.New("BCB needupdate field is not empty")
	}
	return nil
}

func paddedFieldEquals(field []byte, value string) bool {
	if len(value) > len(field) || !bytes.Equal(field[:len(value)], []byte(value)) {
		return false
	}
	return allZero(field[len(value):])
}

func allZero(buffer []byte) bool {
	for _, b := range buffer {
		if b != 0 {
			return false
		}
	}
	return true
}

func clearBCB(device Device) error {
	before, err := readBCB(device)
	if err != nil {
		return err
	}
	if bcbCommand(before) != "boot-recovery" {
		return fmt.Errorf("refusing to clear BCB command %q; expected boot-recovery", bcbCommand(before))
	}
	after := append([]byte(nil), before...)
	clear(after[bcbCommandStart:bcbNeedUpdateEnd])
	return replaceBCB(device, before, after)
}

func replaceBCB(device Device, before, after []byte) error {
	if len(before) != bcbSize || len(after) != bcbSize {
		return errors.New("invalid Rockchip BCB replacement size")
	}
	if !bytes.Equal(after[bcbSystemFlagStart:], before[bcbSystemFlagStart:]) {
		return errors.New("refusing to change BCB systemFlag")
	}
	f, err := openDevice(device, os.O_RDWR)
	if err != nil {
		return err
	}
	current := make([]byte, bcbSize)
	if _, err := f.ReadAt(current, bcbOffset); err != nil {
		_ = f.Close()
		return fmt.Errorf("re-read BCB before write: %w", err)
	}
	if !bytes.Equal(current, before) {
		_ = f.Close()
		return errors.New("BCB changed after validation")
	}
	if n, err := f.WriteAt(after, bcbOffset); err != nil {
		_ = f.Close()
		return fmt.Errorf("write BCB: %w", err)
	} else if n != len(after) {
		_ = f.Close()
		return fmt.Errorf("short BCB write: wrote %d of %d bytes", n, len(after))
	}
	if err := f.Sync(); err != nil {
		_ = f.Close()
		return fmt.Errorf("sync misc after BCB write: %w", err)
	}
	verify := make([]byte, bcbSize)
	if _, err := f.ReadAt(verify, bcbOffset); err != nil {
		_ = f.Close()
		return fmt.Errorf("read back BCB: %w", err)
	}
	if err := f.Close(); err != nil {
		return fmt.Errorf("close misc after BCB write: %w", err)
	}
	if !bytes.Equal(verify, after) {
		return errors.New("BCB exact readback verification failed")
	}
	return nil
}
