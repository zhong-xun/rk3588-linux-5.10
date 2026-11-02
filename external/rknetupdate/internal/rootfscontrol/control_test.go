package rootfscontrol

import "testing"

func TestValidateConfirmationRequiresExactFullPhrase(t *testing.T) {
	expected := PrepareConfirmation("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
	for _, actual := range []string{"", expected[:len(expected)-1], expected + "x", RebootConfirmation(expected[len("PREPARE_ROOTFS_V2:"):])} {
		if err := validateConfirmation(expected, actual); err == nil {
			t.Fatalf("accepted confirmation %q", actual)
		}
	}
	if err := validateConfirmation(expected, expected); err != nil {
		t.Fatal(err)
	}
}
