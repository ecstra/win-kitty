//go:build !windows

package desktop_ui

import "golang.org/x/sys/unix"

func kill_pid(pid int)                         { _ = unix.Kill(pid, unix.SIGTERM) }
func make_fifo(path string, mode uint32) error { return unix.Mkfifo(path, mode) }

// open_fifo_for_writing_and_close unblocks a reader waiting on the fifo.
func open_fifo_for_writing_and_close(path string) {
	if fd, err := unix.Open(path, unix.O_WRONLY|unix.O_NONBLOCK|unix.O_CLOEXEC, 0); err == nil {
		unix.Close(fd)
	}
}
