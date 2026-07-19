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

func write_to_tty(
	pipe_r *os.File, term *tty.Term,
	job_channel <-chan write_msg, err_channel chan<- error, write_done_channel chan<- IdType,
) {
	defer func() {
		if r := recover(); r != nil {
			err_channel <- parallel.Format_stacktrace_on_panic(r, 1)
		}
	}()
	keep_going := true
	defer func() {
		pipe_r.Close()
		close(write_done_channel)
	}()
	selector := utils.CreateSelect(2)
	pipe_fd := int(pipe_r.Fd())
	tty_fd := term.Fd()
	selector.RegisterRead(pipe_fd)
	selector.RegisterWrite(tty_fd)

	wait_for_write_available := func() {
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

	write_data := func(msg write_msg) {
		for !msg.is_empty() {
			wait_for_write_available()
			if !keep_going {
				return
			}
			if err := msg.write(term); err != nil {
				err_channel <- err
				keep_going = false
				return
			}
		}
	}

	for {
		data, more := <-job_channel
		if !more {
			keep_going = false
			break
		}
		write_data(data)
		if keep_going {
			write_done_channel <- data.id
		} else {
			break
		}
	}
}
