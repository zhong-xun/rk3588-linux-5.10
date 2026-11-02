package smokecontrol

import (
	"context"
	"errors"
	"fmt"
	"os"
	"strings"
)

const (
	systemdRuntimePath  = "/run/systemd/system"
	buildrootRebootPath = "/sbin/reboot"
)

var (
	systemdRebootPaths = []string{"/usr/bin/systemctl", "/bin/systemctl"}
	systemdRebootArgs  = []string{"--no-block", "reboot"}
)

// RebootOperations makes the shutdown boundary explicit. Production wires
// these operations in main; tests use fakes and cannot invoke a real reboot.
type RebootOperations struct {
	Stat func(string) (os.FileInfo, error)
	Sync func()
	Run  func(context.Context, string, ...string) error
}

// RequestCleanReboot selects the init-system-appropriate fixed reboot command,
// flushes filesystem state, and invokes the command without force flags.
func RequestCleanReboot(ctx context.Context, ops RebootOperations) (string, error) {
	if ops.Stat == nil || ops.Sync == nil || ops.Run == nil {
		return "", errors.New("incomplete reboot operations")
	}
	path, args, err := selectRebootCommand(ops.Stat)
	if err != nil {
		return "", err
	}
	ops.Sync()
	if err := ops.Run(ctx, path, args...); err != nil {
		return path, fmt.Errorf("run clean reboot command %s: %w", path, err)
	}
	return path, nil
}

func selectRebootCommand(stat func(string) (os.FileInfo, error)) (string, []string, error) {
	systemd, err := stat(systemdRuntimePath)
	switch {
	case err == nil:
		if !systemd.IsDir() {
			return "", nil, fmt.Errorf("systemd runtime marker %s is not a directory", systemdRuntimePath)
		}
		for _, path := range systemdRebootPaths {
			if err := requireExecutable(stat, path); err == nil {
				return path, append([]string(nil), systemdRebootArgs...), nil
			} else if !errors.Is(err, os.ErrNotExist) {
				return "", nil, err
			}
		}
		return "", nil, fmt.Errorf("systemd reboot command not found at %s", strings.Join(systemdRebootPaths, " or "))
	case !errors.Is(err, os.ErrNotExist):
		return "", nil, fmt.Errorf("inspect systemd runtime marker: %w", err)
	}
	if err := requireExecutable(stat, buildrootRebootPath); err != nil {
		return "", nil, err
	}
	return buildrootRebootPath, nil, nil
}

func requireExecutable(stat func(string) (os.FileInfo, error), path string) error {
	info, err := stat(path)
	if err != nil {
		return fmt.Errorf("inspect reboot command %s: %w", path, err)
	}
	if !info.Mode().IsRegular() || info.Mode().Perm()&0111 == 0 {
		return fmt.Errorf("reboot command %s is not an executable regular file", path)
	}
	return nil
}
