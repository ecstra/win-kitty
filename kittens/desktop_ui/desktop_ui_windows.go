//go:build windows

package desktop_ui

import "errors"

func kill_pid(pid int) {}
func make_fifo(path string, mode uint32) error {
	return errors.New("fifos are not supported on Windows")
}
