//go:build windows

package utils

import (
	"os"
	"path/filepath"
	"testing"
)

// Windows runs a bare program name through PATHEXT, so a search for zsh has
// to find zsh.exe.
func TestWhichWithPathextWindows(t *testing.T) {
	dir := t.TempDir()
	for _, name := range []string{"prog.exe", "script.cmd", "noext"} {
		if err := os.WriteFile(filepath.Join(dir, name), nil, 0o755); err != nil {
			t.Fatal(err)
		}
	}
	t.Setenv("PATHEXT", ".COM;.EXE;.BAT;.CMD")
	for _, tc := range []struct{ cmd, expected string }{
		{"prog", filepath.Join(dir, "prog.exe")},
		{"prog.exe", filepath.Join(dir, "prog.exe")},
		{"script", filepath.Join(dir, "script.cmd")},
		{"noext", ""},
		{"missing", ""},
	} {
		if actual := Which(tc.cmd, dir); actual != tc.expected {
			t.Fatalf("Which(%q) = %q, expected %q", tc.cmd, actual, tc.expected)
		}
	}
	// An empty entry, from a trailing or doubled semicolon, is no extension
	t.Setenv("PATHEXT", ".COM;.EXE;;.BAT;.CMD;")
	if actual, expected := Which("prog", dir), filepath.Join(dir, "prog.exe"); actual != expected {
		t.Fatalf("Which(prog) with empty PATHEXT entries = %q, expected %q", actual, expected)
	}
}
