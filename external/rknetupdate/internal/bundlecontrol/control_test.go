package bundlecontrol

import (
	"strings"
	"testing"
)

func TestBundleConfirmationsRequireExactFullPhrase(t *testing.T) {
	sha := strings.Repeat("a", 64)
	expected := PrepareConfirmation(sha)
	for _, actual := range []string{"", expected[:len(expected)-1], expected + "x", RebootConfirmation(sha)} {
		if err := validateConfirmation(expected, actual); err == nil {
			t.Fatalf("accepted confirmation %q", actual)
		}
	}
	if err := validateConfirmation(expected, expected); err != nil {
		t.Fatal(err)
	}
}

func TestProductionBundleConfigIsFixed(t *testing.T) {
	cfg := DefaultConfig()
	cfg.AllowWrite = true
	if err := cfg.Validate(); err != nil {
		t.Fatal(err)
	}
	cfg.BootPath = "/dev/other"
	if err := cfg.Validate(); err == nil {
		t.Fatal("accepted altered production boot path")
	}
}
