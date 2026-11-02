package recoveryapply

import "testing"

func TestShrinkVerificationRangeAcceptsExt4BlockGroupRounding(t *testing.T) {
	minBlocks, maxBlocks, err := shrinkVerificationRange(5_000_000, 6_500_000)
	if err != nil {
		t.Fatal(err)
	}
	const roundedBlocks = 6_488_064
	if roundedBlocks < minBlocks || roundedBlocks > maxBlocks {
		t.Fatalf("rounded ext4 block count %d is outside %d..%d", roundedBlocks, minBlocks, maxBlocks)
	}
}

func TestShrinkVerificationRangeKeepsSafetyHeadroom(t *testing.T) {
	if _, _, err := shrinkVerificationRange(100_000, 100_999); err == nil {
		t.Fatal("accepted a shrink target without the minimum 1024-block safety margin")
	}
	minBlocks, maxBlocks, err := shrinkVerificationRange(100_000, 101_024)
	if err != nil {
		t.Fatal(err)
	}
	if minBlocks != 101_024 || maxBlocks != 101_024 {
		t.Fatalf("unexpected exact safety boundary %d..%d", minBlocks, maxBlocks)
	}
}
