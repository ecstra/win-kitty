//go:build windows

package tui

import (
	"slices"

	"github.com/kovidgoyal/kitty/tools/tui/shell_integration"
)

// The shells run-shell goes back to when one of them started it. On Windows
// that takes in the native shells too, which kitty sets up no shell
// integration for, so that run-shell from pwsh does not land in a zsh further
// up the process tree.
func is_parent_shell(name string) bool {
	return shell_integration.IsSupportedShell(name) || slices.Contains([]string{"pwsh", "powershell", "cmd"}, name)
}
