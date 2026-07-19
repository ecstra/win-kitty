//go:build !windows

package tui

import (
	"golang.org/x/sys/unix"

	"github.com/kovidgoyal/kitty/tools/tty"
)

// save_term_state reads the current termios and returns a function that restores
// it, or nil if the state could not be read.
func save_term_state(term *tty.Term) func() {
	var state_before unix.Termios
	if term.Tcgetattr(&state_before) != nil {
		return nil
	}
	return func() { _ = term.Tcsetattr(tty.TCSANOW, &state_before) }
}
