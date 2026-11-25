//go:build windows

package tui

import (
	"os"
	"path/filepath"
	"slices"
	"strings"

	"github.com/kovidgoyal/kitty/tools/tui/shell_integration"
)

// The shells run-shell goes back to when one of them started it. On Windows
// that takes in the native shells too, so that run-shell from pwsh does not
// land in a zsh further up the process tree.
func is_parent_shell(name string) bool {
	return shell_integration.IsSupportedShell(name) || slices.Contains([]string{"pwsh", "powershell", "cmd"}, name)
}

// kitty integrates pwsh and cmd itself when it starts them, in
// kitty/shell_integration.py, rather than through the shell integration
// package. run-shell mirrors that here, so a shell it goes back to keeps its
// prompt marks and titles.
func setup_native_shell_integration(name, ksi string, argv []string, env map[string]string) ([]string, map[string]string) {
	if !slices.Contains([]string{"pwsh", "powershell", "cmd"}, name) {
		return argv, env
	}
	rc_mod_allowed, set_ksi_env_var := rc_modification_allowed(ksi)
	if !rc_mod_allowed && !set_ksi_env_var {
		return argv, env
	}
	if env == nil {
		env = copy_os_env_as_dict()
	}
	env["KITTY_SHELL_INTEGRATION"] = ksi
	if !rc_mod_allowed {
		return argv, env
	}
	if name == "cmd" {
		// cmd expands $E to ESC in PROMPT, so the marks ride in the prompt
		existing := env["PROMPT"]
		if existing == "" {
			existing = "$P$G"
		}
		if strings.Contains(existing, "]133;A") || strings.Contains(existing, "]2;") {
			return argv, env
		}
		mark, title := "$E]133;A$E\\", "$E]2;$P$E\\"
		if strings.Contains(ksi, "no-prompt-mark") {
			mark = ""
		}
		if strings.Contains(ksi, "no-title") {
			title = ""
		}
		env["PROMPT"] = mark + title + existing
		return argv, env
	}
	// A command or script makes pwsh non-interactive, and -Command would eat it
	for _, a := range argv[1:] {
		switch strings.ToLower(strings.TrimLeft(a, "-/")) {
		case "c", "command", "f", "file", "e", "ec", "encodedcommand", "commandwithargs", "cwa":
			return argv, env
		}
	}
	dir, err := shell_integration.EnsureShellIntegrationFilesFor("powershell")
	if err != nil {
		return argv, env
	}
	script := filepath.Join(dir, "kitty.ps1")
	if _, err := os.Stat(script); err != nil {
		return argv, env
	}
	return append(slices.Clone(argv), "-NoExit", "-Command", ". '"+script+"'"), env
}
