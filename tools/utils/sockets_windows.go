// License: GPLv3 Copyright: 2026, Kovid Goyal, <kovid at kovidgoyal.net>

package utils

import (
	"os"
	"path/filepath"
	"strings"
)

// UnixSocketPath is where a unix: address lies on this machine. kitty's docs
// and kitty.conf files shared with Linux use /tmp, which Windows would read as
// a tmp folder at the root of the drive, so it is the user's own temp folder,
// as kitty itself takes it when it listens.
func UnixSocketPath(addr string) string {
	if addr == "/tmp" || strings.HasPrefix(addr, "/tmp/") {
		return filepath.Join(socketTmpDir(), strings.TrimPrefix(strings.TrimPrefix(addr, "/tmp"), "/"))
	}
	return addr
}

// socketTmpDir is the user's own temp folder, in their local app data. TMP and
// TEMP are not read, since an MSYS2 shell points them at its own tmp folder,
// and kitten would then look somewhere kitty does not listen.
func socketTmpDir() string {
	if local := os.Getenv("LOCALAPPDATA"); local != "" {
		return filepath.Join(local, "Temp")
	}
	return os.TempDir()
}
