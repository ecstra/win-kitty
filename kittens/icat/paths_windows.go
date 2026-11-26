// License: GPLv3 Copyright: 2026, Kovid Goyal, <kovid at kovidgoyal.net>

package icat

import (
	"path/filepath"
	"strings"
)

// on_another_machine is whether kitty refuses to open path, as it refuses any
// path on another machine so a program cannot make Windows send the user's
// login hash there. icat sends such a file's data instead of its path.
func on_another_machine(path string) bool {
	v := filepath.VolumeName(path)
	return strings.HasPrefix(v, `\\`) || strings.HasPrefix(v, "//")
}
