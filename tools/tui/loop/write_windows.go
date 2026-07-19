//go:build windows

// License: GPLv3 Copyright: 2024, Kovid Goyal, <kovid at kovidgoyal.net>

package loop

import (
	"os"

	"github.com/kovidgoyal/go-parallel"
	"github.com/kovidgoyal/kitty/tools/tty"
)

// On Windows the tty is a pipe, which cannot be selected on. Writes are blocking
// (they only wait if kitty is slow to drain the pipe), and shutdown happens when
// the job channel is closed, so no select is needed.
func write_to_tty(
	pipe_r *os.File, term *tty.Term,
	job_channel <-chan write_msg, err_channel chan<- error, write_done_channel chan<- IdType,
) {
	defer func() {
		if r := recover(); r != nil {
			err_channel <- parallel.Format_stacktrace_on_panic(r, 1)
		}
	}()
	defer func() {
		pipe_r.Close()
		close(write_done_channel)
	}()
	for {
		data, more := <-job_channel
		if !more {
			break
		}
		keep_going := true
		for !data.is_empty() {
			if err := data.write(term); err != nil {
				err_channel <- err
				keep_going = false
				break
			}
		}
		if keep_going {
			write_done_channel <- data.id
		} else {
			break
		}
	}
}
