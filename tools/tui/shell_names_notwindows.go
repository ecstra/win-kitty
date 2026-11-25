//go:build !windows

package tui

import "github.com/kovidgoyal/kitty/tools/tui/shell_integration"

func is_parent_shell(name string) bool { return shell_integration.IsSupportedShell(name) }

func setup_native_shell_integration(name, ksi string, argv []string, env map[string]string) ([]string, map[string]string) {
	return argv, env
}
