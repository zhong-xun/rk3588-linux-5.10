package recoveryapply

import (
	"bytes"
	"strings"
	"testing"
)

func TestBundleRecoveryArgumentCanonicalRoundTrip(t *testing.T) {
	request := testBundleBCBRequest()
	argument, err := request.RecoveryArgument()
	if err != nil {
		t.Fatal(err)
	}
	parsed, err := ParseBundleRecoveryArgument(argument)
	if err != nil || parsed != request {
		t.Fatalf("parsed=%+v err=%v", parsed, err)
	}
	if _, err := ParseBundleRecoveryArgument(strings.Replace(argument, "schema=1", "schema=01", 1)); err == nil {
		t.Fatal("accepted noncanonical schema")
	}
}

func TestBundleBCBPhaseAndClearPreserveSystemFlag(t *testing.T) {
	device := testRootfsBCBDevice(t)
	before := readRootfsBCBBytes(t, device.Path)
	request := testBundleBCBRequest()
	if err := PrepareBundleBCB(device, request); err != nil {
		t.Fatal(err)
	}
	actual, phase, err := ReadBundleBCB(device)
	if err != nil || actual != request || phase != BundlePhasePrepared {
		t.Fatalf("request=%+v phase=%s err=%v", actual, phase, err)
	}
	if err := UpdateBundleBCBPhase(device, request, BundlePhaseExpanded); err != nil {
		t.Fatal(err)
	}
	if err := ClearBundleBCB(device, request); err != nil {
		t.Fatal(err)
	}
	after := readRootfsBCBBytes(t, device.Path)
	if !allZero(after[:bcbNeedUpdateEnd]) {
		t.Fatal("bundle BCB fields were not cleared")
	}
	if !bytes.Equal(before[bcbSystemFlagStart:], after[bcbSystemFlagStart:]) {
		t.Fatal("bundle BCB changed systemFlag")
	}
}

func testBundleBCBRequest() BundleBCBRequest {
	return BundleBCBRequest{
		Schema:         BundleSchema,
		ManifestSHA256: strings.Repeat("a", 64),
		FilesystemUUID: "d5bed891-f45e-42e6-9f84-60152589ffec",
		BlockSize:      4096,
		BlockCount:     4096,
		P6Start:        0x8000,
		P6TypeGUID:     "0fc63daf-8483-4772-8e79-3d69d8477de4",
		P6UniqueGUID:   "614e0000-0000-4b53-8000-1d28000054a9",
		P6Name:         "rootfs",
	}
}
