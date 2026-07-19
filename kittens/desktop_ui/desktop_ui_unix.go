//go:build !windows

package desktop_ui

import "golang.org/x/sys/unix"

func kill_pid(pid int)                         { _ = unix.Kill(pid, unix.SIGTERM) }
func make_fifo(path string, mode uint32) error { return unix.Mkfifo(path, mode) }
