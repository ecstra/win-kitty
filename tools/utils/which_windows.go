//go:build windows

package utils

import (
	"os"
	"path/filepath"
	"slices"
	"strings"
)

// The file names a bare program name stands for. Windows runs a name with no
// extension it knows by trying each one in PATHEXT, so zsh is zsh.exe, and a
// file with no such extension is never run at all.
func exe_file_names(cmd string) []string {
	pathext := os.Getenv("PATHEXT")
	if pathext == "" {
		pathext = ".COM;.EXE;.BAT;.CMD"
	}
	exts := strings.Split(strings.ToLower(pathext), ";")
	if slices.Contains(exts, strings.ToLower(filepath.Ext(cmd))) {
		return []string{cmd}
	}
	ans := make([]string, 0, len(exts))
	for _, ext := range exts {
		if ext != "" {
			ans = append(ans, cmd+ext)
		}
	}
	return ans
}
