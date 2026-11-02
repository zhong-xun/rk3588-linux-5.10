package recoveryapply

import (
	"bytes"
	"errors"
	"fmt"
	"regexp"
	"strconv"
	"strings"
)

const (
	RootfsRequestSchema = 2
	RootfsBCBCommand    = "boot-recovery"
	RootfsRecoveryTag   = "--rknetupdate-rootfs-v2"

	RootfsPhasePrepared    = "rootfs-prepared"
	RootfsPhaseShrunk      = "rootfs-shrunk"
	RootfsPhaseP7Ready     = "rootfs-p7-ready"
	RootfsPhaseStaged      = "rootfs-staged"
	RootfsPhaseP6Verified  = "rootfs-p6-verified"
	RootfsPhaseGPTRestored = "rootfs-gpt-restored"
	RootfsPhaseExpanded    = "rootfs-expanded"
)

var (
	guidPattern  = regexp.MustCompile(`^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$`)
	rootfsPhases = map[string]bool{
		RootfsPhasePrepared:    true,
		RootfsPhaseShrunk:      true,
		RootfsPhaseP7Ready:     true,
		RootfsPhaseStaged:      true,
		RootfsPhaseP6Verified:  true,
		RootfsPhaseGPTRestored: true,
		RootfsPhaseExpanded:    true,
	}
)

var rootfsPhaseOrder = []string{
	RootfsPhasePrepared,
	RootfsPhaseShrunk,
	RootfsPhaseP7Ready,
	RootfsPhaseStaged,
	RootfsPhaseP6Verified,
	RootfsPhaseGPTRestored,
	RootfsPhaseExpanded,
}

type RootfsBCBRequest struct {
	Schema         int    `json:"schema"`
	Size           int64  `json:"size"`
	SHA256         string `json:"sha256"`
	FilesystemUUID string `json:"filesystem_uuid"`
	BlockSize      uint64 `json:"block_size"`
	BlockCount     uint64 `json:"block_count"`
	P6Start        uint64 `json:"p6_start"`
	P6TypeGUID     string `json:"p6_type_guid"`
	P6UniqueGUID   string `json:"p6_unique_guid"`
	P6Name         string `json:"p6_name"`
}

func (r RootfsBCBRequest) Validate() error {
	switch {
	case r.Schema != RootfsRequestSchema:
		return fmt.Errorf("unsupported rootfs request schema %d", r.Schema)
	case r.Size <= 0:
		return errors.New("rootfs request size must be positive")
	case !sha256Pattern.MatchString(r.SHA256):
		return errors.New("rootfs request sha256 must be lowercase hexadecimal")
	case !guidPattern.MatchString(r.FilesystemUUID):
		return errors.New("rootfs request filesystem UUID is invalid")
	case r.BlockSize < 1024 || r.BlockSize > 65536 || r.BlockSize&(r.BlockSize-1) != 0:
		return errors.New("rootfs request block size is invalid")
	case r.BlockCount == 0:
		return errors.New("rootfs request block count must be positive")
	case r.BlockCount > ^uint64(0)/r.BlockSize:
		return errors.New("rootfs request filesystem size overflows")
	case r.BlockCount*r.BlockSize > uint64(r.Size):
		return errors.New("rootfs request filesystem exceeds image size")
	case r.P6Start == 0:
		return errors.New("rootfs request p6 start must be positive")
	case !guidPattern.MatchString(r.P6TypeGUID):
		return errors.New("rootfs request p6 type GUID is invalid")
	case !guidPattern.MatchString(r.P6UniqueGUID):
		return errors.New("rootfs request p6 unique GUID is invalid")
	case r.P6Name != "rootfs":
		return errors.New("rootfs request p6 name must be rootfs")
	}
	return nil
}

func (r RootfsBCBRequest) RecoveryArgument() (string, error) {
	if err := r.Validate(); err != nil {
		return "", err
	}
	value := fmt.Sprintf(
		"recovery\n%s\nschema=%d\nsize=%d\nsha256=%s\nfs_uuid=%s\nblock_size=%d\nblock_count=%d\np6_start=%d\np6_type=%s\np6_guid=%s\np6_name=%s\n",
		RootfsRecoveryTag, r.Schema, r.Size, r.SHA256, r.FilesystemUUID,
		r.BlockSize, r.BlockCount, r.P6Start, r.P6TypeGUID, r.P6UniqueGUID, r.P6Name,
	)
	if len(value) > bcbRecoveryEnd-bcbRecoveryStart {
		return "", errors.New("rootfs request exceeds Rockchip BCB recovery field")
	}
	return value, nil
}

func ParseRootfsRecoveryArgument(value string) (RootfsBCBRequest, error) {
	lines := strings.Split(value, "\n")
	if len(lines) != 13 || lines[0] != "recovery" || lines[1] != RootfsRecoveryTag || lines[12] != "" {
		return RootfsBCBRequest{}, errors.New("rootfs recovery request has invalid canonical structure")
	}
	expectedKeys := []string{"schema", "size", "sha256", "fs_uuid", "block_size", "block_count", "p6_start", "p6_type", "p6_guid", "p6_name"}
	values := make(map[string]string, len(expectedKeys))
	for i, key := range expectedKeys {
		actualKey, actualValue, ok := strings.Cut(lines[i+2], "=")
		if !ok || actualKey != key || actualValue == "" {
			return RootfsBCBRequest{}, fmt.Errorf("rootfs recovery request field %d must be %s", i+1, key)
		}
		values[key] = actualValue
	}
	schema, err := strconv.Atoi(values["schema"])
	if err != nil {
		return RootfsBCBRequest{}, errors.New("rootfs request schema is invalid")
	}
	size, err := strconv.ParseInt(values["size"], 10, 64)
	if err != nil {
		return RootfsBCBRequest{}, errors.New("rootfs request size is invalid")
	}
	blockSize, err := strconv.ParseUint(values["block_size"], 10, 64)
	if err != nil {
		return RootfsBCBRequest{}, errors.New("rootfs request block size is invalid")
	}
	blockCount, err := strconv.ParseUint(values["block_count"], 10, 64)
	if err != nil {
		return RootfsBCBRequest{}, errors.New("rootfs request block count is invalid")
	}
	p6Start, err := strconv.ParseUint(values["p6_start"], 10, 64)
	if err != nil {
		return RootfsBCBRequest{}, errors.New("rootfs request p6 start is invalid")
	}
	request := RootfsBCBRequest{
		Schema:         schema,
		Size:           size,
		SHA256:         values["sha256"],
		FilesystemUUID: values["fs_uuid"],
		BlockSize:      blockSize,
		BlockCount:     blockCount,
		P6Start:        p6Start,
		P6TypeGUID:     values["p6_type"],
		P6UniqueGUID:   values["p6_guid"],
		P6Name:         values["p6_name"],
	}
	if err := request.Validate(); err != nil {
		return RootfsBCBRequest{}, err
	}
	canonical, err := request.RecoveryArgument()
	if err != nil || canonical != value {
		return RootfsBCBRequest{}, errors.New("rootfs recovery request is not canonical")
	}
	return request, nil
}

func PrepareRootfsBCB(device Device, request RootfsBCBRequest) error {
	recovery, err := request.RecoveryArgument()
	if err != nil {
		return err
	}
	before, err := readBCB(device)
	if err != nil {
		return err
	}
	if !allZero(before[bcbCommandStart:bcbNeedUpdateEnd]) {
		return errors.New("refusing to prepare rootfs BCB: command/status/recovery/needupdate region is not empty")
	}
	after := append([]byte(nil), before...)
	copy(after[bcbCommandStart:bcbCommandEnd], RootfsBCBCommand)
	copy(after[bcbCommandEnd:bcbStatusEnd], RootfsPhasePrepared)
	copy(after[bcbRecoveryStart:bcbRecoveryEnd], recovery)
	return replaceBCB(device, before, after)
}

func ReadRootfsBCB(device Device) (RootfsBCBRequest, string, error) {
	buffer, err := readBCB(device)
	if err != nil {
		return RootfsBCBRequest{}, "", err
	}
	return parseRootfsBCB(buffer)
}

func UpdateRootfsBCBPhase(device Device, expected RootfsBCBRequest, phase string) error {
	if !rootfsPhases[phase] || len(phase) > bcbStatusEnd-bcbCommandEnd {
		return fmt.Errorf("invalid rootfs BCB phase %q", phase)
	}
	before, err := readBCB(device)
	if err != nil {
		return err
	}
	actual, currentPhase, err := parseRootfsBCB(before)
	if err != nil {
		return err
	}
	if actual != expected {
		return errors.New("rootfs BCB request changed")
	}
	if rootfsPhaseIndex(phase) < rootfsPhaseIndex(currentPhase) {
		return fmt.Errorf("refusing rootfs BCB phase regression from %s to %s", currentPhase, phase)
	}
	if phase == currentPhase {
		return nil
	}
	after := append([]byte(nil), before...)
	clear(after[bcbCommandEnd:bcbStatusEnd])
	copy(after[bcbCommandEnd:bcbStatusEnd], phase)
	return replaceBCB(device, before, after)
}

func rootfsPhaseIndex(phase string) int {
	for index, candidate := range rootfsPhaseOrder {
		if candidate == phase {
			return index
		}
	}
	return -1
}

func ClearRootfsBCB(device Device, expected RootfsBCBRequest) error {
	before, err := readBCB(device)
	if err != nil {
		return err
	}
	actual, phase, err := parseRootfsBCB(before)
	if err != nil {
		return err
	}
	if actual != expected || phase != RootfsPhaseExpanded {
		return errors.New("refusing to clear incomplete rootfs BCB request")
	}
	after := append([]byte(nil), before...)
	clear(after[bcbCommandStart:bcbNeedUpdateEnd])
	return replaceBCB(device, before, after)
}

func parseRootfsBCB(buffer []byte) (RootfsBCBRequest, string, error) {
	if len(buffer) != bcbSize {
		return RootfsBCBRequest{}, "", errors.New("invalid Rockchip BCB size")
	}
	if !paddedFieldEquals(buffer[bcbCommandStart:bcbCommandEnd], RootfsBCBCommand) {
		return RootfsBCBRequest{}, "", errors.New("BCB command is not the rootfs recovery command")
	}
	phase := strings.TrimRight(string(buffer[bcbCommandEnd:bcbStatusEnd]), "\x00")
	if !rootfsPhases[phase] || !allZero(buffer[bcbCommandEnd+len(phase):bcbStatusEnd]) {
		return RootfsBCBRequest{}, "", fmt.Errorf("invalid rootfs BCB phase %q", phase)
	}
	if !allZero(buffer[bcbNeedUpdateStart:bcbNeedUpdateEnd]) {
		return RootfsBCBRequest{}, "", errors.New("rootfs BCB needupdate field is not empty")
	}
	recoveryField := buffer[bcbRecoveryStart:bcbRecoveryEnd]
	nul := bytes.IndexByte(recoveryField, 0)
	if nul < 0 || !allZero(recoveryField[nul:]) {
		return RootfsBCBRequest{}, "", errors.New("rootfs BCB recovery field is not zero padded")
	}
	request, err := ParseRootfsRecoveryArgument(string(recoveryField[:nul]))
	if err != nil {
		return RootfsBCBRequest{}, "", err
	}
	return request, phase, nil
}
