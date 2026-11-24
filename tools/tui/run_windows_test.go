//go:build windows

package tui

import (
	"os"
	"path/filepath"
	"testing"
)

// run-shell from an MSYS2 zsh window names the shell zsh, which is zsh.exe on
// PATH, and must not fall back to cmd.
func TestResolveShellFindsExeOnPathWindows(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "zsh.exe"), nil, 0o755); err != nil {
		t.Fatal(err)
	}
	t.Setenv("PATH", dir)
	t.Setenv("PATHEXT", ".COM;.EXE;.BAT;.CMD")
	if actual := ResolveShell("zsh"); len(actual) != 1 || actual[0] != "zsh" {
		t.Fatalf("ResolveShell(zsh) = %#v, expected zsh, not the fallback shell", actual)
	}
}

// run-shell started from pwsh or cmd goes back to that shell, even though
// kitty has no shell integration for it to set up.
func TestRunShellRecognizesWindowsShells(t *testing.T) {
	for _, name := range []string{"pwsh", "powershell", "cmd", "zsh"} {
		if !is_parent_shell(name) {
			t.Fatalf("%s is not recognized as a shell to go back to", name)
		}
	}
	if is_parent_shell("python") {
		t.Fatalf("python is recognized as a shell")
	}
}
