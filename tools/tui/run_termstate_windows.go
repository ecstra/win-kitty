//go:build windows

package tui

import "github.com/kovidgoyal/kitty/tools/tty"

// There is no termios on Windows; the terminal is a pipe managed by kitty.
func save_term_state(term *tty.Term) func() { return func() {} }
