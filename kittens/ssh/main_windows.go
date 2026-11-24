//go:build windows

package ssh

import (
	"io/fs"

	"github.com/kovidgoyal/kitty/tools/tty"
)

// The Windows console keeps no termios, so there is nothing to save before a
// launch or to put back after a stuck shared connection is killed.
type terminal_attributes struct{}

func get_terminal_attributes(term *tty.Term, t *terminal_attributes) error { return nil }

func set_terminal_attributes(term *tty.Term, t *terminal_attributes) error { return nil }

// Shared memory is not used on Windows, so there is no SHM owner to verify.
func check_shm_owner(s fs.FileInfo) error { return nil }

// Windows cannot re-raise SIGINT to itself.
func reraise_sigint_to_self() {}
