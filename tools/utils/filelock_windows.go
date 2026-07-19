//go:build windows

// License: GPLv3 Copyright: 2024, Kovid Goyal, <kovid at kovidgoyal.net>

package utils

import (
	"os"

	"golang.org/x/sys/windows"
)

// Lock the whole file. LockFileEx blocks until the lock is acquired when
// LOCKFILE_FAIL_IMMEDIATELY is not passed, matching the blocking flock() used on
// Unix.
func lockWholeFile(f *os.File, flags uint32) error {
	ol := new(windows.Overlapped)
	if err := windows.LockFileEx(windows.Handle(f.Fd()), flags, 0, ^uint32(0), ^uint32(0), ol); err != nil {
		return &os.PathError{Op: "LockFileEx", Path: f.Name(), Err: err}
	}
	return nil
}

func LockFileShared(f *os.File) error {
	return lockWholeFile(f, 0)
}

func LockFileExclusive(f *os.File) error {
	return lockWholeFile(f, windows.LOCKFILE_EXCLUSIVE_LOCK)
}

func UnlockFile(f *os.File) error {
	ol := new(windows.Overlapped)
	if err := windows.UnlockFileEx(windows.Handle(f.Fd()), 0, ^uint32(0), ^uint32(0), ol); err != nil {
		return &os.PathError{Op: "UnlockFileEx", Path: f.Name(), Err: err}
	}
	return nil
}
