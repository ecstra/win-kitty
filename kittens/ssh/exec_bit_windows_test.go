//go:build windows

// License: GPLv3 Copyright: 2023, Kovid Goyal, <kovid at kovidgoyal.net>

package ssh

import (
	"os"
)

// Windows has no execute bit, so the most this can ask is that the file was
// staged at all. The mode is set on arrival at the remote host anyway.
func check_is_executable(path string) error {
	_, err := os.Stat(path)
	return err
}
