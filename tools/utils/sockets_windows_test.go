// License: GPLv3 Copyright: 2026, Kovid Goyal, <kovid at kovidgoyal.net>

package utils

import (
	"os"
	"path/filepath"
	"testing"
)

func TestUnixSocketPathPutsTmpInTheTempFolder(t *testing.T) {
	for addr, expected := range map[string]string{
		"/tmp/mykitty":         filepath.Join(os.TempDir(), "mykitty"),
		"/tmp":                 os.TempDir(),
		"/tmpfoo":              "/tmpfoo",
		`C:\Users\me\kitty.sk`: `C:\Users\me\kitty.sk`,
	} {
		if actual := UnixSocketPath(addr); actual != expected {
			t.Fatalf("UnixSocketPath(%#v) = %#v, expected %#v", addr, actual, expected)
		}
	}
}
