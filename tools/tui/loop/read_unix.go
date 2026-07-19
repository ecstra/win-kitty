//go:build !windows

// License: GPLv3 Copyright: 2022, Kovid Goyal, <kovid at kovidgoyal.net>

package loop

import (
	"os"

	"golang.org/x/sys/unix"

	"github.com/kovidgoyal/go-parallel"
	"github.com/kovidgoyal/kitty/tools/tty"
	"github.com/kovidgoyal/kitty/tools/utils"
)

func read_from_tty(pipe_r *os.File, term *tty.Term, results_channel chan<- []byte, err_channel chan<- error, quit_channel <-chan byte, leftover_channel chan<- []byte) {
	defer func() {
		if r := recover(); r != nil {
			err := parallel.Format_stacktrace_on_panic(r, 1)
			err_channel <- err
		}
	}()
	keep_going := true
	pipe_fd := int(pipe_r.Fd())
	tty_fd := term.Fd()
	selector := utils.CreateSelect(2)
	selector.RegisterRead(pipe_fd)
	selector.RegisterRead(tty_fd)

	defer func() {
		close(results_channel)
		pipe_r.Close()
	}()

	const bufsize = 2 * utils.DEFAULT_IO_BUFFER_SIZE

	wait_for_read_available := func() {
		for {
			n, err := selector.WaitForever()
			if err != nil && err != unix.EINTR {
				err_channel <- err
				keep_going = false
				return
			}
			if n > 0 {
				break
			}
		}
		if selector.IsReadyToRead(pipe_fd) {
			keep_going = false
		}
	}

	buf := make([]byte, bufsize)
	for keep_going {
		if len(buf) < 64 {
			buf = make([]byte, bufsize)
		}
		if wait_for_read_available(); !keep_going {
			break
		}
		n, err := read_ignoring_temporary_errors(term, buf)
		if err != nil {
			err_channel <- err
			keep_going = false
			break
		}
		if n == 0 { // temporary error
			continue
		}
		send := buf[:n]
		buf = buf[n:]
		select {
		case results_channel <- send:
		case <-quit_channel:
			leftover_channel <- send
			keep_going = false
		}
	}
}
