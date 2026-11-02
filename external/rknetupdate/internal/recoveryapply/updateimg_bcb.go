package recoveryapply

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"strconv"
	"strings"
)

const (
	// Rockchip's stock recovery explicitly recognizes this conventional path.
	// The private state and hard-link backup live under /userdata/rknetupdate,
	// but the package itself must be visible here during stock OTA.
	UpdateImagePackagePath = "/userdata/update.img"
	updateImageBCBCommand  = "boot-recovery"

	// UpdateImageRecoveryTag is consumed only by the RKNetUpdate-aware
	// recovery image.  It is replaced atomically with the standard Rockchip
	// --update_package request before the stock updater starts.
	UpdateImageRecoveryTag = "--rknetupdate-updateimg-v1"

	UpdateImagePhasePrepared = "updateimg-prepared"
	UpdateImagePhaseShrunk   = "updateimg-shrunk"
	UpdateImagePhaseP7Ready  = "updateimg-p7-ready"
	UpdateImagePhaseStaged   = "updateimg-staged"

	// Retained in the standard BCB for SDK compatibility. The managed rkupdate
	// path writes every item in the verified complete package.
	UpdateImageStockMask uint32 = 0x00bb0000
)

var updateImagePhases = map[string]bool{
	UpdateImagePhasePrepared: true,
	UpdateImagePhaseShrunk:   true,
	UpdateImagePhaseP7Ready:  true,
	UpdateImagePhaseStaged:   true,
}

var updateImagePhaseOrder = []string{
	UpdateImagePhasePrepared,
	UpdateImagePhaseShrunk,
	UpdateImagePhaseP7Ready,
	UpdateImagePhaseStaged,
}

// UpdateImageBCBRequest binds the persisted manifest, the exact package, and
// the pre-update p6 identity.  The detailed RKAF item table remains in the
// SHA-bound manifest; keeping this request compact leaves ample BCB space.
type UpdateImageBCBRequest struct {
	Schema         int    `json:"schema"`
	ManifestSHA256 string `json:"manifest_sha256"`
	PackageSize    int64  `json:"package_size"`
	PackageSHA256  string `json:"package_sha256"`
	P6Start        uint64 `json:"p6_start"`
	P6TypeGUID     string `json:"p6_type_guid"`
	P6UniqueGUID   string `json:"p6_unique_guid"`
	P6Name         string `json:"p6_name"`
}

func (r UpdateImageBCBRequest) Validate() error {
	switch {
	case r.Schema != UpdateImageSchema:
		return fmt.Errorf("unsupported update.img request schema %d", r.Schema)
	case !sha256Pattern.MatchString(r.ManifestSHA256):
		return errors.New("update.img request manifest_sha256 must be lowercase hexadecimal")
	case r.PackageSize <= 0:
		return errors.New("update.img request package_size must be positive")
	case !sha256Pattern.MatchString(r.PackageSHA256):
		return errors.New("update.img request package_sha256 must be lowercase hexadecimal")
	case r.P6Start == 0:
		return errors.New("update.img request p6_start must be positive")
	case !guidPattern.MatchString(r.P6TypeGUID):
		return errors.New("update.img request p6_type_guid is invalid")
	case !guidPattern.MatchString(r.P6UniqueGUID):
		return errors.New("update.img request p6_unique_guid is invalid")
	case r.P6Name != "rootfs":
		return errors.New("update.img request p6_name must be rootfs")
	}
	return nil
}

func (r UpdateImageBCBRequest) RecoveryArgument() (string, error) {
	if err := r.Validate(); err != nil {
		return "", err
	}
	value := fmt.Sprintf(
		"recovery\n%s\nschema=%d\nmanifest_sha256=%s\npackage_size=%d\npackage_sha256=%s\np6_start=%d\np6_type=%s\np6_guid=%s\np6_name=%s\n",
		UpdateImageRecoveryTag, r.Schema, r.ManifestSHA256, r.PackageSize, r.PackageSHA256,
		r.P6Start, r.P6TypeGUID, r.P6UniqueGUID, r.P6Name,
	)
	if len(value) > bcbRecoveryEnd-bcbRecoveryStart {
		return "", errors.New("update.img request exceeds Rockchip BCB recovery field")
	}
	return value, nil
}

func ParseUpdateImageRecoveryArgument(value string) (UpdateImageBCBRequest, error) {
	lines := strings.Split(value, "\n")
	if len(lines) != 11 || lines[0] != "recovery" || lines[1] != UpdateImageRecoveryTag || lines[10] != "" {
		return UpdateImageBCBRequest{}, errors.New("update.img recovery request has invalid canonical structure")
	}
	expectedKeys := []string{"schema", "manifest_sha256", "package_size", "package_sha256", "p6_start", "p6_type", "p6_guid", "p6_name"}
	values := make(map[string]string, len(expectedKeys))
	for i, key := range expectedKeys {
		actualKey, actualValue, ok := strings.Cut(lines[i+2], "=")
		if !ok || actualKey != key || actualValue == "" {
			return UpdateImageBCBRequest{}, fmt.Errorf("update.img recovery request field %d must be %s", i+1, key)
		}
		values[key] = actualValue
	}
	schema, err := strconv.Atoi(values["schema"])
	if err != nil {
		return UpdateImageBCBRequest{}, errors.New("update.img request schema is invalid")
	}
	packageSize, err := strconv.ParseInt(values["package_size"], 10, 64)
	if err != nil {
		return UpdateImageBCBRequest{}, errors.New("update.img request package_size is invalid")
	}
	p6Start, err := strconv.ParseUint(values["p6_start"], 10, 64)
	if err != nil {
		return UpdateImageBCBRequest{}, errors.New("update.img request p6_start is invalid")
	}
	request := UpdateImageBCBRequest{
		Schema: schema, ManifestSHA256: values["manifest_sha256"], PackageSize: packageSize,
		PackageSHA256: values["package_sha256"], P6Start: p6Start,
		P6TypeGUID: values["p6_type"], P6UniqueGUID: values["p6_guid"], P6Name: values["p6_name"],
	}
	if err := request.Validate(); err != nil {
		return UpdateImageBCBRequest{}, err
	}
	canonical, err := request.RecoveryArgument()
	if err != nil || canonical != value {
		return UpdateImageBCBRequest{}, errors.New("update.img recovery request is not canonical")
	}
	return request, nil
}

func PrepareUpdateImageMigrationBCB(device Device, request UpdateImageBCBRequest) error {
	recovery, err := request.RecoveryArgument()
	if err != nil {
		return err
	}
	before, err := readBCB(device)
	if err != nil {
		return err
	}
	if !allZero(before[bcbCommandStart:bcbNeedUpdateEnd]) {
		return errors.New("refusing to prepare update.img BCB: command/status/recovery/needupdate region is not empty")
	}
	after := append([]byte(nil), before...)
	copy(after[bcbCommandStart:bcbCommandEnd], updateImageBCBCommand)
	copy(after[bcbCommandEnd:bcbStatusEnd], UpdateImagePhasePrepared)
	copy(after[bcbRecoveryStart:bcbRecoveryEnd], recovery)
	return replaceBCB(device, before, after)
}

func ReadUpdateImageMigrationBCB(device Device) (UpdateImageBCBRequest, string, error) {
	buffer, err := readBCB(device)
	if err != nil {
		return UpdateImageBCBRequest{}, "", err
	}
	return parseUpdateImageMigrationBCB(buffer)
}

func UpdateUpdateImageBCBPhase(device Device, expected UpdateImageBCBRequest, phase string) error {
	if !updateImagePhases[phase] || len(phase) > bcbStatusEnd-bcbCommandEnd {
		return fmt.Errorf("invalid update.img BCB phase %q", phase)
	}
	before, err := readBCB(device)
	if err != nil {
		return err
	}
	actual, currentPhase, err := parseUpdateImageMigrationBCB(before)
	if err != nil {
		return err
	}
	if actual != expected {
		return errors.New("update.img BCB request changed")
	}
	if updateImagePhaseIndex(phase) < updateImagePhaseIndex(currentPhase) {
		return fmt.Errorf("refusing update.img BCB phase regression from %s to %s", currentPhase, phase)
	}
	if phase == currentPhase {
		return nil
	}
	after := append([]byte(nil), before...)
	clear(after[bcbCommandEnd:bcbStatusEnd])
	copy(after[bcbCommandEnd:bcbStatusEnd], phase)
	return replaceBCB(device, before, after)
}

// HandoffUpdateImageToStockBCB atomically changes the private staging request
// into the SDK's canonical update-package request. The patched recovery
// detects the verified p7 stage and invokes rkupdate in its full-image mode.
func HandoffUpdateImageToStockBCB(device Device, expected UpdateImageBCBRequest) error {
	before, err := readBCB(device)
	if err != nil {
		return err
	}
	actual, phase, err := parseUpdateImageMigrationBCB(before)
	if err != nil {
		return err
	}
	if actual != expected || phase != UpdateImagePhaseStaged {
		return errors.New("update.img BCB is not at the staged handoff phase")
	}
	after := append([]byte(nil), before...)
	clear(after[bcbCommandStart:bcbNeedUpdateEnd])
	copy(after[bcbCommandStart:bcbCommandEnd], updateImageBCBCommand)
	copy(after[bcbRecoveryStart:bcbRecoveryEnd], UpdateImageRecoveryArgument)
	binary.LittleEndian.PutUint32(after[bcbNeedUpdateStart:bcbNeedUpdateEnd], UpdateImageStockMask)
	return replaceBCB(device, before, after)
}

func IsUpdateImageMigrationBCB(buffer []byte) bool {
	return len(buffer) == bcbSize && bytes.Contains(buffer[bcbRecoveryStart:bcbRecoveryEnd], []byte(UpdateImageRecoveryTag))
}

func IsStockUpdateImageBCB(buffer []byte) bool {
	return len(buffer) == bcbSize && bytes.Contains(buffer[bcbRecoveryStart:bcbRecoveryEnd], []byte("--update_package="+UpdateImagePackagePath))
}

func parseUpdateImageMigrationBCB(buffer []byte) (UpdateImageBCBRequest, string, error) {
	if len(buffer) != bcbSize {
		return UpdateImageBCBRequest{}, "", errors.New("invalid Rockchip BCB size")
	}
	if !paddedFieldEquals(buffer[bcbCommandStart:bcbCommandEnd], updateImageBCBCommand) {
		return UpdateImageBCBRequest{}, "", errors.New("BCB command is not the update.img recovery command")
	}
	phase := strings.TrimRight(string(buffer[bcbCommandEnd:bcbStatusEnd]), "\x00")
	if !updateImagePhases[phase] || !allZero(buffer[bcbCommandEnd+len(phase):bcbStatusEnd]) {
		return UpdateImageBCBRequest{}, "", fmt.Errorf("invalid update.img BCB phase %q", phase)
	}
	if !allZero(buffer[bcbNeedUpdateStart:bcbNeedUpdateEnd]) {
		return UpdateImageBCBRequest{}, "", errors.New("update.img migration BCB needupdate field is not empty")
	}
	recoveryField := buffer[bcbRecoveryStart:bcbRecoveryEnd]
	nul := bytes.IndexByte(recoveryField, 0)
	if nul < 0 || !allZero(recoveryField[nul:]) {
		return UpdateImageBCBRequest{}, "", errors.New("update.img migration BCB recovery field is not zero padded")
	}
	request, err := ParseUpdateImageRecoveryArgument(string(recoveryField[:nul]))
	if err != nil {
		return UpdateImageBCBRequest{}, "", err
	}
	return request, phase, nil
}

func updateImagePhaseIndex(phase string) int {
	for index, candidate := range updateImagePhaseOrder {
		if phase == candidate {
			return index
		}
	}
	return -1
}

// UpdateImageRecoveryArgument is deliberately compatible with the stock
// Rockchip recovery parser. Do not add private arguments here: stock recovery
// must receive exactly the standard update-package request.
const UpdateImageRecoveryArgument = "recovery\n--update_package=" + UpdateImagePackagePath + "\n"

func PrepareUpdateImageBCB(device Device) error {
	before, err := readBCB(device)
	if err != nil {
		return err
	}
	if !allZero(before[bcbCommandStart:bcbNeedUpdateEnd]) {
		return errors.New("refusing to prepare update.img BCB: command/status/recovery/needupdate region is not empty")
	}
	after := append([]byte(nil), before...)
	copy(after[bcbCommandStart:bcbCommandEnd], updateImageBCBCommand)
	copy(after[bcbRecoveryStart:bcbRecoveryEnd], UpdateImageRecoveryArgument)
	return replaceBCB(device, before, after)
}

func VerifyUpdateImageBCB(device Device) error {
	buffer, err := readBCB(device)
	if err != nil {
		return err
	}
	if !paddedFieldEquals(buffer[bcbCommandStart:bcbCommandEnd], updateImageBCBCommand) {
		return fmt.Errorf("BCB command does not exactly match %q", updateImageBCBCommand)
	}
	if !allZero(buffer[bcbCommandEnd:bcbStatusEnd]) {
		return errors.New("update.img BCB status field is not empty")
	}
	if !paddedFieldEquals(buffer[bcbRecoveryStart:bcbRecoveryEnd], UpdateImageRecoveryArgument) {
		return errors.New("update.img BCB recovery argument does not exactly match the canonical update-package request")
	}
	if !allZero(buffer[bcbNeedUpdateStart:bcbNeedUpdateEnd]) {
		return errors.New("update.img BCB needupdate field is not empty")
	}
	return nil
}

func IsUpdateImageBCB(buffer []byte) bool {
	return len(buffer) == bcbSize && bytes.Contains(buffer[bcbRecoveryStart:bcbRecoveryEnd], []byte("--update_package="+UpdateImagePackagePath))
}
