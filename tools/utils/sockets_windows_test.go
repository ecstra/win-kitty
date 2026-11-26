// License: GPLv3 Copyright: 2026, Kovid Goyal, <kovid at kovidgoyal.net>

package utils

import (
	"path/filepath"
	"testing"
)

func TestUnixSocketPathPutsTmpInTheTempFolder(t *testing.T) {
	// The user's own temp folder, whatever TMP an MSYS2 shell set, as kitty takes it
	local := t.TempDir()
	t.Setenv("LOCALAPPDATA", local)
	t.Setenv("TMP", filepath.Join(local, "msys64", "tmp"))
	for addr, expected := range map[string]string{
		"/tmp/mykitty":         filepath.Join(local, "Temp", "mykitty"),
		"/tmp":                 filepath.Join(local, "Temp"),
		"/tmpfoo":              "/tmpfoo",
		`C:\Users\me\kitty.sk`: `C:\Users\me\kitty.sk`,
	} {
		if actual := UnixSocketPath(addr); actual != expected {
			t.Fatalf("UnixSocketPath(%#v) = %#v, expected %#v", addr, actual, expected)
		}
	}
}
