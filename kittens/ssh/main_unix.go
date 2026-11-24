//go:build !windows

package ssh

import (
	"fmt"
	"io/fs"
	"os"
	"syscall"
	"time"

	"github.com/kovidgoyal/kitty/tools/tty"
	"golang.org/x/sys/unix"
)

type terminal_attributes = unix.Termios

func get_terminal_attributes(term *tty.Term, t *terminal_attributes) error {
	return term.Tcgetattr(t)
}

func set_terminal_attributes(term *tty.Term, t *terminal_attributes) error {
	return term.Tcsetattr(tty.TCSANOW, t)
}

func check_shm_owner(s fs.FileInfo) error {
	if stat, ok := s.Sys().(syscall.Stat_t); ok {
		if os.Getuid() != int(stat.Uid) || os.Getgid() != int(stat.Gid) {
			return fmt.Errorf("Incorrect owner on SHM file")
		}
	}
	return nil
}

func reraise_sigint_to_self() {
	_ = unix.Kill(os.Getpid(), unix.SIGINT)
	// Give the signal time to be delivered
	time.Sleep(20 * time.Millisecond)
}
