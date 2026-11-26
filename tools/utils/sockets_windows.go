// License: GPLv3 Copyright: 2026, Kovid Goyal, <kovid at kovidgoyal.net>

package utils

import (
	"os"
	"path/filepath"
	"strings"
)

// UnixSocketPath is where a unix: address lies on this machine. kitty's docs
// and kitty.conf files shared with Linux use /tmp, which Windows would read as
// a tmp folder at the root of the drive, so it is the temp folder, as kitty
// itself takes it when it listens.
func UnixSocketPath(addr string) string {
	if addr == "/tmp" || strings.HasPrefix(addr, "/tmp/") {
		return filepath.Join(os.TempDir(), strings.TrimPrefix(strings.TrimPrefix(addr, "/tmp"), "/"))
	}
	return addr
}
