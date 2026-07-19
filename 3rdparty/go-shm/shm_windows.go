// License: GPLv3 Copyright: 2024, Kovid Goyal, <kovid at kovidgoyal.net>

//go:build windows

package shm

import "errors"

// POSIX shared memory does not exist on Windows, and kitty's C side reads POSIX
// shm anyway, so report it as unsupported. Callers (icat, ssh) check for
// ErrNotSupported and fall back to transferring data directly.

func create_temp(pattern string, size uint64) (MMap, error) {
	return nil, &ErrNotSupported{err: errors.ErrUnsupported}
}

func Open(name string, size uint64) (MMap, error) {
	return nil, &ErrNotSupported{err: errors.ErrUnsupported}
}

// SHM_DIR is empty on Windows (like macOS), so callers that use it as a fast
// temp location fall back to the normal temp dir.
const SHM_DIR = ""

func ShmUnlink(name string) error { return nil }
