//go:build windows

package utils

import (
	"io/fs"
	"os"
)

// Windows has no O_TMPFILE, so create a normal temp file. It is not unlinked
// while open (Windows forbids that by default), so it lingers until the OS temp
// cleanup runs, which is fine for the short-lived scratch uses kitty has.
func CreateAnonymousTemp(dir string, perms ...fs.FileMode) (*os.File, error) {
	f, err := os.CreateTemp(dir, "kitty-anon-*")
	if err != nil {
		return nil, err
	}
	if len(perms) > 0 {
		_ = f.Chmod(perms[0])
	}
	return f, nil
}
