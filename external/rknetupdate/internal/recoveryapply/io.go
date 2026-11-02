package recoveryapply

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"os"
	"syscall"
)

// WriteProgress receives the number of bytes durably copied into a target.
// Callers own throttling because recovery console and status-file policies
// differ between workflows.
type WriteProgress func(phase string, written, total int64)

func openDevice(device Device, flags int) (*os.File, error) {
	fd, err := syscall.Open(device.Path, flags|syscall.O_CLOEXEC|syscall.O_NOFOLLOW, 0)
	if err != nil {
		return nil, fmt.Errorf("open device %s: %w", device.Path, err)
	}
	f := os.NewFile(uintptr(fd), device.Path)
	if f == nil {
		_ = syscall.Close(fd)
		return nil, errors.New("wrap device file descriptor")
	}
	var stat syscall.Stat_t
	if err := syscall.Fstat(fd, &stat); err != nil {
		_ = f.Close()
		return nil, fmt.Errorf("inspect opened device: %w", err)
	}
	if device.Regular {
		if uint64(stat.Dev) != device.Dev || stat.Ino != device.Ino || !os.FileMode(stat.Mode).IsRegular() {
			_ = f.Close()
			return nil, errors.New("opened regular target identity changed")
		}
	} else if uint64(stat.Rdev) != device.Rdev {
		_ = f.Close()
		return nil, fmt.Errorf("opened block target identity changed: expected rdev=%d actual=%d", device.Rdev, uint64(stat.Rdev))
	}
	return f, nil
}

func hashOpenFile(f *os.File, size int64) (string, error) {
	if _, err := f.Seek(0, io.SeekStart); err != nil {
		return "", err
	}
	hash := sha256.New()
	written, err := io.CopyN(hash, f, size)
	if err != nil {
		return "", fmt.Errorf("hash image after %d bytes: %w", written, err)
	}
	one := make([]byte, 1)
	if n, err := f.Read(one); err == nil || n != 0 {
		return "", errors.New("image contains data beyond declared size")
	} else if !errors.Is(err, io.EOF) {
		return "", fmt.Errorf("check image end: %w", err)
	}
	return hex.EncodeToString(hash.Sum(nil)), nil
}

func writeImage(ctx context.Context, source *os.File, target Device, size int64, expectedSHA, phase string, progress WriteProgress) (int64, string, error) {
	if _, err := source.Seek(0, io.SeekStart); err != nil {
		return 0, "", fmt.Errorf("rewind source image: %w", err)
	}
	targetFile, err := openDevice(target, os.O_RDWR)
	if err != nil {
		return 0, "", err
	}
	defer targetFile.Close()
	if _, err := targetFile.Seek(0, io.SeekStart); err != nil {
		return 0, "", fmt.Errorf("seek target: %w", err)
	}
	hash := sha256.New()
	buffer := make([]byte, 1<<20)
	var total int64
	for total < size {
		if err := ctx.Err(); err != nil {
			return total, "", err
		}
		remaining := size - total
		chunk := buffer
		if int64(len(chunk)) > remaining {
			chunk = chunk[:remaining]
		}
		n, readErr := io.ReadFull(source, chunk)
		if readErr != nil {
			return total, "", fmt.Errorf("read source image at %d: %w", total, readErr)
		}
		if _, err := hash.Write(chunk[:n]); err != nil {
			return total, "", err
		}
		written := 0
		for written < n {
			count, err := targetFile.Write(chunk[written:n])
			if err != nil {
				return total, "", fmt.Errorf("write target at %d: %w", total+int64(written), err)
			}
			if count == 0 {
				return total, "", io.ErrShortWrite
			}
			written += count
		}
		total += int64(n)
		if progress != nil {
			progress(phase, total, size)
		}
	}
	writtenSHA := hex.EncodeToString(hash.Sum(nil))
	if writtenSHA != expectedSHA {
		return total, writtenSHA, errors.New("source image changed while writing; target may be incomplete")
	}
	if err := targetFile.Sync(); err != nil {
		return total, writtenSHA, fmt.Errorf("fsync target: %w", err)
	}
	if _, err := targetFile.Seek(0, io.SeekStart); err != nil {
		return total, writtenSHA, fmt.Errorf("seek target for readback: %w", err)
	}
	readback := sha256.New()
	read, err := io.CopyN(readback, targetFile, size)
	if err != nil {
		return total, writtenSHA, fmt.Errorf("read back target after %d bytes: %w", read, err)
	}
	readbackSHA := hex.EncodeToString(readback.Sum(nil))
	if readbackSHA != expectedSHA {
		return total, readbackSHA, fmt.Errorf("readback sha256 %s does not match manifest %s", readbackSHA, expectedSHA)
	}
	return total, readbackSHA, nil
}
