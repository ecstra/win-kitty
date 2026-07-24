//go:build !windows

// License: GPLv3 Copyright: 2023, Kovid Goyal, <kovid at kovidgoyal.net>

package ssh

import (
	"golang.org/x/sys/unix"
)

// check_is_executable asserts the execute bit survived staging for the remote host.
func check_is_executable(path string) error {
	return unix.Access(path, unix.X_OK)
}
