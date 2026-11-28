//go:build !windows

package dnd

import (
	"fmt"
	"os"
	"syscall"

	"golang.org/x/sys/unix"
)

// Identify a file by device and inode. Unlike os.SameFile this also works
// with the os.FileInfo objects returned by utils.StatAt.
func file_identity(info os.FileInfo) (dev, ino uint64, err error) {
	switch s := info.Sys().(type) {
	case *syscall.Stat_t:
		return uint64(s.Dev), uint64(s.Ino), nil
	case *unix.Stat_t:
		return uint64(s.Dev), uint64(s.Ino), nil
	}
	return 0, 0, fmt.Errorf("unknown stat result type for %s: %T", info.Name(), info.Sys())
}

// The device and inode are in the info from the start
func with_identity(info os.FileInfo) os.FileInfo { return info }

func same_file(a, b os.FileInfo) (bool, error) {
	adev, aino, err := file_identity(a)
	if err != nil {
		return false, err
	}
	bdev, bino, err := file_identity(b)
	if err != nil {
		return false, err
	}
	return adev == bdev && aino == bino, nil
}
