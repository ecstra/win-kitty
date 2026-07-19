//go:build windows

// License: GPLv3 Copyright: 2024, Kovid Goyal, <kovid at kovidgoyal.net>

package loop

import (
	"os"

	"github.com/kovidgoyal/go-parallel"
	"github.com/kovidgoyal/kitty/tools/tty"
	"github.com/kovidgoyal/kitty/tools/utils"
)

// Windows pipes cannot be selected on, so read in a goroutine with a blocking
// read. A blocking read cannot be interrupted, so when asked to quit we close the
// terminal read side, which makes the pending read return.
func read_from_tty(pipe_r *os.File, term *tty.Term, results_channel chan<- []byte, err_channel chan<- error, quit_channel <-chan byte, leftover_channel chan<- []byte) {
	defer func() {
		if r := recover(); r != nil {
			err_channel <- parallel.Format_stacktrace_on_panic(r, 1)
		}
	}()
	defer func() {
		close(results_channel)
		pipe_r.Close()
	}()

	quitting := func() bool {
		select {
		case <-quit_channel:
			return true
		default:
			return false
		}
	}

	watcher_done := make(chan struct{})
	go func() {
		select {
		case <-quit_channel:
			term.CloseRead()
		case <-watcher_done:
		}
	}()
	defer close(watcher_done)

	const bufsize = 2 * utils.DEFAULT_IO_BUFFER_SIZE
	for {
		if quitting() {
			return
		}
		buf := make([]byte, bufsize)
		n, err := read_ignoring_temporary_errors(term, buf)
		if err != nil {
			if quitting() { // the read was interrupted by CloseRead, a clean shutdown
				return
			}
			err_channel <- err
			return
		}
		if n == 0 {
			continue
		}
		send := buf[:n]
		select {
		case results_channel <- send:
		case <-quit_channel:
			leftover_channel <- send
			return
		}
	}
}
