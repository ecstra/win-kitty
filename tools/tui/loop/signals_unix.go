//go:build !windows

// License: GPLv3 Copyright: 2024, Kovid Goyal, <kovid at kovidgoyal.net>

package loop

import (
	"os"
	"os/signal"
	"syscall"
	"time"

	"golang.org/x/sys/unix"
)

func kill_self(sig syscall.Signal) {
	_ = unix.Kill(os.Getpid(), sig)
	// Give the signal time to be delivered
	time.Sleep(20 * time.Millisecond)
}

func suspend_process() error {
	_ = unix.Kill(os.Getpid(), unix.SIGSTOP)
	time.Sleep(20 * time.Millisecond)
	return nil
}

func (self *Loop) notify_signals(ch chan os.Signal) (reset func()) {
	handled := []os.Signal{unix.SIGINT, unix.SIGTERM, unix.SIGTSTP, unix.SIGHUP, unix.SIGWINCH, unix.SIGPIPE}
	signal.Notify(ch, handled...)
	return func() { signal.Reset(handled...) }
}

func (self *Loop) dispatch_signal(s os.Signal) error {
	return self.on_signal(s.(syscall.Signal))
}

func (self *Loop) on_signal(s syscall.Signal) error {
	switch s {
	case unix.SIGINT:
		if self.OnSIGINT != nil {
			if handled, err := self.OnSIGINT(); handled {
				return err
			}
		}
		return self.on_SIGINT()
	case unix.SIGPIPE:
		return self.on_SIGPIPE()
	case unix.SIGWINCH:
		return self.on_SIGWINCH()
	case unix.SIGTERM:
		if self.OnSIGTERM != nil {
			if handled, err := self.OnSIGTERM(); handled {
				return err
			}
		}
		return self.on_SIGTERM()
	case unix.SIGTSTP:
		return self.on_SIGTSTP()
	case unix.SIGHUP:
		return self.on_SIGHUP()
	default:
		return nil
	}
}

func (self *Loop) on_SIGPIPE() error {
	return nil
}

func (self *Loop) on_SIGHUP() error {
	self.death_signal = unix.SIGHUP
	self.keep_going = false
	return nil
}
