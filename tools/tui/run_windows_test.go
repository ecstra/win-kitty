//go:build windows

package tui

import (
	"os"
	"path/filepath"
	"slices"
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

// A pwsh or cmd that run-shell goes back to gets the integration kitty gives
// those shells when it starts them, see kitty/shell_integration.py.
func TestRunShellSetsUpWindowsShells(t *testing.T) {
	kid := t.TempDir()
	script := filepath.Join(kid, "shell-integration", "powershell", "kitty.ps1")
	if err := os.MkdirAll(filepath.Dir(script), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(script, nil, 0o644); err != nil {
		t.Fatal(err)
	}
	t.Setenv("KITTY_INSTALLATION_DIR", kid)

	argv, env := setup_native_shell_integration("pwsh", "enabled", []string{"pwsh"}, nil)
	if expected := []string{"pwsh", "-NoExit", "-Command", ". '" + script + "'"}; !slices.Equal(argv, expected) {
		t.Fatalf("pwsh argv = %#v, expected %#v", argv, expected)
	}
	if env["KITTY_SHELL_INTEGRATION"] != "enabled" {
		t.Fatalf("pwsh env has no KITTY_SHELL_INTEGRATION: %#v", env["KITTY_SHELL_INTEGRATION"])
	}
	// a command to run makes pwsh non-interactive, and -Command would eat the rest
	if argv, _ = setup_native_shell_integration("powershell", "enabled", []string{"powershell", "-Command", "dir"}, nil); len(argv) != 3 {
		t.Fatalf("non-interactive powershell argv changed: %#v", argv)
	}
	_, env = setup_native_shell_integration("cmd", "enabled", []string{"cmd"}, map[string]string{"PROMPT": "$P$G"})
	if expected := "$E]133;A$E\\$E]2;$P$E\\$P$G"; env["PROMPT"] != expected {
		t.Fatalf("cmd PROMPT = %q, expected %q", env["PROMPT"], expected)
	}
	if argv, env = setup_native_shell_integration("pwsh", "disabled", []string{"pwsh"}, nil); len(argv) != 1 || env != nil {
		t.Fatalf("pwsh with integration disabled was changed: %#v %#v", argv, env)
	}
}
