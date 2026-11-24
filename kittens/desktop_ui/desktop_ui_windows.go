//go:build windows

package desktop_ui

import "errors"

func kill_pid(pid int) {}
func make_fifo(path string, mode uint32) error {
	return errors.New("fifos are not supported on Windows")
}

// make_fifo always fails here, so there is never a reader to unblock.
func open_fifo_for_writing_and_close(path string) {}
