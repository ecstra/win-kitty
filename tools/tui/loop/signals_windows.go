//go:build windows

// License: GPLv3 Copyright: 2024, Kovid Goyal, <kovid at kovidgoyal.net>

package loop

import (
	"os"
	"os/signal"
	"syscall"
	"time"
)

// Windows has no Unix signals. Ctrl+C arrives as a byte through the pipe and is
// handled in handle_key_event, and resize arrives as an in-band CSI escape, so
// the only OS signals worth watching are interrupt and terminate.

// Other terminals on Windows send no in-band resize and Windows sends no
// SIGWINCH, so outside kitty the console's size is checked a few times a second
// and a change handled as SIGWINCH is on Unix
func (self *Loop) watch_console_size() {
	if os.Getenv("KITTY_WINDOW_ID") != "" || self.controlling_term == nil {
		return
	}
	last, _ := self.controlling_term.GetSize()
	_, _ = self.AddTimer(250*time.Millisecond, true, func(IdType) error {
		if self.seen_inband_resize || self.controlling_term == nil {
			return nil
		}
		now, err := self.controlling_term.GetSize()
		if err != nil || (last != nil && now.Row == last.Row && now.Col == last.Col && now.Xpixel == last.Xpixel && now.Ypixel == last.Ypixel) {
			return nil
		}
		last = now
		return self.on_SIGWINCH()
	})
}

func kill_self(sig syscall.Signal) {
	// Windows cannot re-raise an arbitrary signal, so there is nothing to do.
}

func suspend_process() error {
	// There is no Ctrl+Z process suspension on Windows.
	return nil
}

func (self *Loop) notify_signals(ch chan os.Signal) (reset func()) {
	handled := []os.Signal{os.Interrupt, syscall.SIGTERM}
	signal.Notify(ch, handled...)
	return func() { signal.Reset(handled...) }
}

func (self *Loop) dispatch_signal(s os.Signal) error {
	switch s {
	case os.Interrupt, syscall.SIGINT:
		if self.OnSIGINT != nil {
			if handled, err := self.OnSIGINT(); handled {
				return err
			}
		}
		return self.on_SIGINT()
	case syscall.SIGTERM:
		if self.OnSIGTERM != nil {
			if handled, err := self.OnSIGTERM(); handled {
				return err
			}
		}
		return self.on_SIGTERM()
	}
	return nil
}
