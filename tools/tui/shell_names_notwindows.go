//go:build !windows

package tui

import "github.com/kovidgoyal/kitty/tools/tui/shell_integration"

func is_parent_shell(name string) bool { return shell_integration.IsSupportedShell(name) }
