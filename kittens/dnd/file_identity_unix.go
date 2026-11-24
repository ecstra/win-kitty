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
