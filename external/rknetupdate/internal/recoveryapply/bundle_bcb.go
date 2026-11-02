package recoveryapply

import (
	"bytes"
	"errors"
	"fmt"
	"strconv"
	"strings"
)

const (
	BundleBCBCommand  = "boot-recovery"
	BundleRecoveryTag = "--rknetupdate-bundle-v1"

	BundlePhasePrepared        = "bundle-prepared"
	BundlePhaseShrunk          = "bundle-shrunk"
	BundlePhaseP7Ready         = "bundle-p7-ready"
	BundlePhaseStaged          = "bundle-staged"
	BundlePhaseRootfsVerified  = "bundle-rootfs-ok"
	BundlePhasePayloadVerified = "bundle-payload-ok"
	BundlePhaseGPTRestored     = "bundle-gpt-restored"
	BundlePhaseExpanded        = "bundle-expanded"
)

var bundlePhases = map[string]bool{
	BundlePhasePrepared:        true,
	BundlePhaseShrunk:          true,
	BundlePhaseP7Ready:         true,
	BundlePhaseStaged:          true,
	BundlePhaseRootfsVerified:  true,
	BundlePhasePayloadVerified: true,
	BundlePhaseGPTRestored:     true,
	BundlePhaseExpanded:        true,
}

var bundlePhaseOrder = []string{
	BundlePhasePrepared,
	BundlePhaseShrunk,
	BundlePhaseP7Ready,
	BundlePhaseStaged,
	BundlePhaseRootfsVerified,
	BundlePhasePayloadVerified,
	BundlePhaseGPTRestored,
	BundlePhaseExpanded,
}

// BundleBCBRequest is deliberately compact.  The BCB binds a fixed local
// manifest by hash and binds the rootfs GPT identity; the detailed artifact
// data remains in the hash-bound manifest copied to p7.
type BundleBCBRequest struct {
	Schema         int    `json:"schema"`
	ManifestSHA256 string `json:"manifest_sha256"`
	FilesystemUUID string `json:"filesystem_uuid"`
	BlockSize      uint64 `json:"block_size"`
	BlockCount     uint64 `json:"block_count"`
	P6Start        uint64 `json:"p6_start"`
	P6TypeGUID     string `json:"p6_type_guid"`
	P6UniqueGUID   string `json:"p6_unique_guid"`
	P6Name         string `json:"p6_name"`
}

func (r BundleBCBRequest) Validate() error {
	switch {
	case r.Schema != BundleSchema:
		return fmt.Errorf("unsupported bundle request schema %d", r.Schema)
	case !sha256Pattern.MatchString(r.ManifestSHA256):
		return errors.New("bundle request manifest_sha256 must be lowercase hexadecimal")
	case !guidPattern.MatchString(r.FilesystemUUID):
		return errors.New("bundle request filesystem UUID is invalid")
	case r.BlockSize < 1024 || r.BlockSize > 65536 || r.BlockSize&(r.BlockSize-1) != 0:
		return errors.New("bundle request block size is invalid")
	case r.BlockCount == 0 || r.BlockCount > ^uint64(0)/r.BlockSize:
		return errors.New("bundle request block count is invalid")
	case r.P6Start == 0:
		return errors.New("bundle request p6 start must be positive")
	case !guidPattern.MatchString(r.P6TypeGUID):
		return errors.New("bundle request p6 type GUID is invalid")
	case !guidPattern.MatchString(r.P6UniqueGUID):
		return errors.New("bundle request p6 unique GUID is invalid")
	case r.P6Name != "rootfs":
		return errors.New("bundle request p6 name must be rootfs")
	}
	return nil
}

func (r BundleBCBRequest) RecoveryArgument() (string, error) {
	if err := r.Validate(); err != nil {
		return "", err
	}
	value := fmt.Sprintf(
		"recovery\n%s\nschema=%d\nmanifest_sha256=%s\nfs_uuid=%s\nblock_size=%d\nblock_count=%d\np6_start=%d\np6_type=%s\np6_guid=%s\np6_name=%s\n",
		BundleRecoveryTag, r.Schema, r.ManifestSHA256, r.FilesystemUUID, r.BlockSize, r.BlockCount, r.P6Start, r.P6TypeGUID, r.P6UniqueGUID, r.P6Name,
	)
	if len(value) > bcbRecoveryEnd-bcbRecoveryStart {
		return "", errors.New("bundle request exceeds Rockchip BCB recovery field")
	}
	return value, nil
}

func ParseBundleRecoveryArgument(value string) (BundleBCBRequest, error) {
	lines := strings.Split(value, "\n")
	if len(lines) != 12 || lines[0] != "recovery" || lines[1] != BundleRecoveryTag || lines[11] != "" {
		return BundleBCBRequest{}, errors.New("bundle recovery request has invalid canonical structure")
	}
	expectedKeys := []string{"schema", "manifest_sha256", "fs_uuid", "block_size", "block_count", "p6_start", "p6_type", "p6_guid", "p6_name"}
	values := make(map[string]string, len(expectedKeys))
	for i, key := range expectedKeys {
		actualKey, actualValue, ok := strings.Cut(lines[i+2], "=")
		if !ok || actualKey != key || actualValue == "" {
			return BundleBCBRequest{}, fmt.Errorf("bundle recovery request field %d must be %s", i+1, key)
		}
		values[key] = actualValue
	}
	schema, err := strconv.Atoi(values["schema"])
	if err != nil {
		return BundleBCBRequest{}, errors.New("bundle request schema is invalid")
	}
	p6Start, err := strconv.ParseUint(values["p6_start"], 10, 64)
	if err != nil {
		return BundleBCBRequest{}, errors.New("bundle request p6 start is invalid")
	}
	blockSize, err := strconv.ParseUint(values["block_size"], 10, 64)
	if err != nil {
		return BundleBCBRequest{}, errors.New("bundle request block size is invalid")
	}
	blockCount, err := strconv.ParseUint(values["block_count"], 10, 64)
	if err != nil {
		return BundleBCBRequest{}, errors.New("bundle request block count is invalid")
	}
	request := BundleBCBRequest{
		Schema:         schema,
		ManifestSHA256: values["manifest_sha256"],
		FilesystemUUID: values["fs_uuid"],
		BlockSize:      blockSize,
		BlockCount:     blockCount,
		P6Start:        p6Start,
		P6TypeGUID:     values["p6_type"],
		P6UniqueGUID:   values["p6_guid"],
		P6Name:         values["p6_name"],
	}
	if err := request.Validate(); err != nil {
		return BundleBCBRequest{}, err
	}
	canonical, err := request.RecoveryArgument()
	if err != nil || canonical != value {
		return BundleBCBRequest{}, errors.New("bundle recovery request is not canonical")
	}
	return request, nil
}

func PrepareBundleBCB(device Device, request BundleBCBRequest) error {
	recovery, err := request.RecoveryArgument()
	if err != nil {
		return err
	}
	before, err := readBCB(device)
	if err != nil {
		return err
	}
	if !allZero(before[bcbCommandStart:bcbNeedUpdateEnd]) {
		return errors.New("refusing to prepare bundle BCB: command/status/recovery/needupdate region is not empty")
	}
	after := append([]byte(nil), before...)
	copy(after[bcbCommandStart:bcbCommandEnd], BundleBCBCommand)
	copy(after[bcbCommandEnd:bcbStatusEnd], BundlePhasePrepared)
	copy(after[bcbRecoveryStart:bcbRecoveryEnd], recovery)
	return replaceBCB(device, before, after)
}

func ReadBundleBCB(device Device) (BundleBCBRequest, string, error) {
	buffer, err := readBCB(device)
	if err != nil {
		return BundleBCBRequest{}, "", err
	}
	return parseBundleBCB(buffer)
}

func UpdateBundleBCBPhase(device Device, expected BundleBCBRequest, phase string) error {
	if !bundlePhases[phase] || len(phase) > bcbStatusEnd-bcbCommandEnd {
		return fmt.Errorf("invalid bundle BCB phase %q", phase)
	}
	before, err := readBCB(device)
	if err != nil {
		return err
	}
	actual, currentPhase, err := parseBundleBCB(before)
	if err != nil {
		return err
	}
	if actual != expected {
		return errors.New("bundle BCB request changed")
	}
	if bundlePhaseIndex(phase) < bundlePhaseIndex(currentPhase) {
		return fmt.Errorf("refusing bundle BCB phase regression from %s to %s", currentPhase, phase)
	}
	if phase == currentPhase {
		return nil
	}
	after := append([]byte(nil), before...)
	clear(after[bcbCommandEnd:bcbStatusEnd])
	copy(after[bcbCommandEnd:bcbStatusEnd], phase)
	return replaceBCB(device, before, after)
}

func ClearBundleBCB(device Device, expected BundleBCBRequest) error {
	before, err := readBCB(device)
	if err != nil {
		return err
	}
	actual, phase, err := parseBundleBCB(before)
	if err != nil {
		return err
	}
	if actual != expected || phase != BundlePhaseExpanded {
		return errors.New("refusing to clear incomplete bundle BCB request")
	}
	after := append([]byte(nil), before...)
	clear(after[bcbCommandStart:bcbNeedUpdateEnd])
	return replaceBCB(device, before, after)
}

func IsBundleBCB(buffer []byte) bool {
	return len(buffer) == bcbSize && bytes.Contains(buffer[bcbRecoveryStart:bcbRecoveryEnd], []byte(BundleRecoveryTag))
}

func parseBundleBCB(buffer []byte) (BundleBCBRequest, string, error) {
	if len(buffer) != bcbSize {
		return BundleBCBRequest{}, "", errors.New("invalid Rockchip BCB size")
	}
	if !paddedFieldEquals(buffer[bcbCommandStart:bcbCommandEnd], BundleBCBCommand) {
		return BundleBCBRequest{}, "", errors.New("BCB command is not the bundle recovery command")
	}
	phase := strings.TrimRight(string(buffer[bcbCommandEnd:bcbStatusEnd]), "\x00")
	if !bundlePhases[phase] || !allZero(buffer[bcbCommandEnd+len(phase):bcbStatusEnd]) {
		return BundleBCBRequest{}, "", fmt.Errorf("invalid bundle BCB phase %q", phase)
	}
	if !allZero(buffer[bcbNeedUpdateStart:bcbNeedUpdateEnd]) {
		return BundleBCBRequest{}, "", errors.New("bundle BCB needupdate field is not empty")
	}
	recoveryField := buffer[bcbRecoveryStart:bcbRecoveryEnd]
	nul := bytes.IndexByte(recoveryField, 0)
	if nul < 0 || !allZero(recoveryField[nul:]) {
		return BundleBCBRequest{}, "", errors.New("bundle BCB recovery field is not zero padded")
	}
	request, err := ParseBundleRecoveryArgument(string(recoveryField[:nul]))
	if err != nil {
		return BundleBCBRequest{}, "", err
	}
	return request, phase, nil
}

func bundlePhaseIndex(phase string) int {
	for index, candidate := range bundlePhaseOrder {
		if candidate == phase {
			return index
		}
	}
	return -1
}
