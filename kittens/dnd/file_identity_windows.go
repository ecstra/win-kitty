//go:build windows

package dnd

import (
	"os"
)

// What os.Lstat reports on Windows carries no volume or file index, which
// os.SameFile reads from the path. Every stat here comes from os, the *At
// helpers included, which build full paths on Windows.
func same_file(a, b os.FileInfo) (bool, error) {
	return os.SameFile(a, b), nil
}
