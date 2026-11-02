package smokecontrol

import (
	"context"
	"errors"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
)

func TestRequestCleanRebootUsesSystemdCommandAfterSync(t *testing.T) {
	dirInfo, executableInfo := rebootTestFileInfo(t)
	events := []string{}
	ops := RebootOperations{
		Stat: func(path string) (os.FileInfo, error) {
			switch path {
			case systemdRuntimePath:
				return dirInfo, nil
			case systemdRebootPaths[0]:
				return executableInfo, nil
			default:
				return nil, os.ErrNotExist
			}
		},
		Sync: func() { events = append(events, "sync") },
		Run: func(_ context.Context, path string, args ...string) error {
			events = append(events, "run:"+strings.Join(append([]string{path}, args...), " "))
			return nil
		},
	}
	path, err := RequestCleanReboot(context.Background(), ops)
	if err != nil {
		t.Fatal(err)
	}
	if path != systemdRebootPaths[0] {
		t.Fatalf("reboot path = %q, want %q", path, systemdRebootPaths[0])
	}
	want := []string{"sync", "run:" + systemdRebootPaths[0] + " --no-block reboot"}
	if !reflect.DeepEqual(events, want) {
		t.Fatalf("events = %#v, want %#v", events, want)
	}
}

func TestRequestCleanRebootUsesCompatSystemctlPath(t *testing.T) {
	dirInfo, executableInfo := rebootTestFileInfo(t)
	var gotPath string
	ops := RebootOperations{
		Stat: func(path string) (os.FileInfo, error) {
			switch path {
			case systemdRuntimePath:
				return dirInfo, nil
			case systemdRebootPaths[0]:
				return nil, os.ErrNotExist
			case systemdRebootPaths[1]:
				return executableInfo, nil
			default:
				return nil, os.ErrNotExist
			}
		},
		Sync: func() {},
		Run: func(_ context.Context, path string, args ...string) error {
			gotPath = strings.Join(append([]string{path}, args...), " ")
			return nil
		},
	}
	if _, err := RequestCleanReboot(context.Background(), ops); err != nil {
		t.Fatal(err)
	}
	want := systemdRebootPaths[1] + " --no-block reboot"
	if gotPath != want {
		t.Fatalf("reboot command = %q, want %q", gotPath, want)
	}
}

func TestRequestCleanRebootUsesBuildrootCommandWithoutSystemd(t *testing.T) {
	_, executableInfo := rebootTestFileInfo(t)
	events := []string{}
	ops := RebootOperations{
		Stat: func(path string) (os.FileInfo, error) {
			switch path {
			case systemdRuntimePath:
				return nil, os.ErrNotExist
			case buildrootRebootPath:
				return executableInfo, nil
			default:
				return nil, os.ErrNotExist
			}
		},
		Sync: func() { events = append(events, "sync") },
		Run: func(_ context.Context, path string, args ...string) error {
			events = append(events, "run:"+strings.Join(append([]string{path}, args...), " "))
			return nil
		},
	}
	path, err := RequestCleanReboot(context.Background(), ops)
	if err != nil {
		t.Fatal(err)
	}
	if path != buildrootRebootPath {
		t.Fatalf("reboot path = %q, want %q", path, buildrootRebootPath)
	}
	want := []string{"sync", "run:" + buildrootRebootPath}
	if !reflect.DeepEqual(events, want) {
		t.Fatalf("events = %#v, want %#v", events, want)
	}
}

func TestRequestCleanRebootPropagatesCommandFailure(t *testing.T) {
	_, executableInfo := rebootTestFileInfo(t)
	wantErr := errors.New("reboot rejected")
	ops := RebootOperations{
		Stat: func(path string) (os.FileInfo, error) {
			if path == systemdRuntimePath {
				return nil, os.ErrNotExist
			}
			return executableInfo, nil
		},
		Sync: func() {},
		Run:  func(context.Context, string, ...string) error { return wantErr },
	}
	if _, err := RequestCleanReboot(context.Background(), ops); !errors.Is(err, wantErr) {
		t.Fatalf("expected command failure, got %v", err)
	}
}

func rebootTestFileInfo(t *testing.T) (os.FileInfo, os.FileInfo) {
	t.Helper()
	dir := t.TempDir()
	dirInfo, err := os.Stat(dir)
	if err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(dir, "reboot")
	if err := os.WriteFile(path, []byte("test"), 0755); err != nil {
		t.Fatal(err)
	}
	executableInfo, err := os.Stat(path)
	if err != nil {
		t.Fatal(err)
	}
	return dirInfo, executableInfo
}
