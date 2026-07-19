//go:build windows

// License: GPLv3 Copyright: 2024, Kovid Goyal, <kovid at kovidgoyal.net>

package tty

import (
	"fmt"
	"os"
	"strconv"
	"sync"
	"syscall"
	"time"

	"github.com/emmansun/base64"
	"golang.org/x/sys/windows"

	"github.com/kovidgoyal/kitty/tools/utils"
)

const (
	TCSANOW   = 0
	TCSADRAIN = 1
	TCSAFLUSH = 2
)

// Winsize is a platform neutral terminal size, mirroring the fields of
// unix.Winsize so callers work unchanged.
type Winsize struct {
	Row, Col, Xpixel, Ypixel uint16
}

// On Windows a kitten's stdio is either pipes (an overlay kitten, kitty on the
// other end) or a real console (a kitten run straight from the shell). Term wraps
// the read (stdin) and write (stdout) handles. For a pipe there is nothing to
// toggle: kitty already feeds pre-encoded key sequences. For a real console the
// termios-style operations translate into SetConsoleMode calls so the kitten gets
// raw, per-key VT input instead of cooked, line-buffered input.
type Term struct {
	in, out       *os.File
	owns_file     bool
	in_console    bool   // in is a real console (needs raw mode + polling reads)
	out_console   bool   // out is a real console
	in_orig_mode  uint32 // console modes saved by enable_raw_mode, restored on close
	out_orig_mode uint32
	raw_active    bool
}

type TermiosOperation func(*Term)

var (
	SetRaw          TermiosOperation = func(t *Term) { t.enable_raw_mode() }
	SetNoEcho       TermiosOperation = func(*Term) {}
	SetNoCanonical  TermiosOperation = func(*Term) {}
	SetReadPassword TermiosOperation = func(*Term) {}
	SetBlockingRead TermiosOperation = func(*Term) {}
)

// enable_raw_mode puts a real console into VT/raw mode so the kitten receives
// per-key escape sequences (arrows and the like) rather than cooked line input.
// When in/out are pipes GetConsoleMode fails and this is a no-op.
func (self *Term) enable_raw_mode() {
	if self.in != nil {
		h := windows.Handle(self.in.Fd())
		var m uint32
		if windows.GetConsoleMode(h, &m) == nil {
			self.in_console = true
			self.in_orig_mode = m
			nm := (m &^ (windows.ENABLE_ECHO_INPUT | windows.ENABLE_LINE_INPUT | windows.ENABLE_PROCESSED_INPUT)) | windows.ENABLE_VIRTUAL_TERMINAL_INPUT
			windows.SetConsoleMode(h, nm)
		}
	}
	if self.out != nil {
		h := windows.Handle(self.out.Fd())
		var m uint32
		if windows.GetConsoleMode(h, &m) == nil {
			self.out_console = true
			self.out_orig_mode = m
			nm := m | windows.ENABLE_PROCESSED_OUTPUT | windows.ENABLE_VIRTUAL_TERMINAL_PROCESSING | windows.DISABLE_NEWLINE_AUTO_RETURN
			windows.SetConsoleMode(h, nm)
		}
	}
	self.raw_active = true
}

func (self *Term) restore_console() {
	if !self.raw_active {
		return
	}
	self.raw_active = false
	if self.in_console && self.in != nil {
		// Restore the saved input mode, but force the cooked flags on. PowerShell
		// (PSReadLine) runs a child with the console still in its own raw
		// line-editing mode and does not re-initialize it when the child exits
		// normally, so restoring that raw mode verbatim would leave the prompt
		// unable to read input (only Ctrl+C, which makes PSReadLine reset, recovers
		// it). The next prompt sets whatever mode it wants over this.
		mode := self.in_orig_mode | windows.ENABLE_PROCESSED_INPUT | windows.ENABLE_LINE_INPUT | windows.ENABLE_ECHO_INPUT
		windows.SetConsoleMode(windows.Handle(self.in.Fd()), mode)
	}
	if self.out_console && self.out != nil {
		windows.SetConsoleMode(windows.Handle(self.out.Fd()), self.out_orig_mode)
	}
}

func SetReadTimeout(d time.Duration) TermiosOperation { return func(*Term) {} }

func IsTerminal(fd uintptr) bool {
	// A kitten always talks to kitty over its stdio, so treat the standard fds as
	// a terminal.
	return fd <= 2
}

func WrapTerm(fd int, name string, operations ...TermiosOperation) (self *Term, err error) {
	if name == "" {
		name = fmt.Sprintf("<fd: %d>", fd)
	}
	f := os.NewFile(uintptr(fd), name)
	if f == nil {
		return nil, os.ErrInvalid
	}
	self = &Term{in: f, out: f, owns_file: true}
	_ = self.ApplyOperations(TCSANOW, operations...)
	return
}

func OpenControllingTerm(operations ...TermiosOperation) (self *Term, err error) {
	self = &Term{in: os.Stdin, out: os.Stdout}
	_ = self.ApplyOperations(TCSANOW, operations...)
	return
}

func OpenTerm(name string, operations ...TermiosOperation) (self *Term, err error) {
	// Windows has no /dev/tty, so use the standard pipes.
	return OpenControllingTerm(operations...)
}

func (self *Term) Fd() int {
	if self.in == nil {
		return -1
	}
	return int(self.in.Fd())
}

func (self *Term) Close() error {
	self.restore_console()
	var err error
	if self.owns_file && self.in != nil {
		err = self.in.Close()
	}
	self.in, self.out = nil, nil
	return err
}

func (self *Term) Read(b []byte) (int, error) {
	if self.in == nil {
		return 0, os.ErrClosed
	}
	if !self.in_console {
		return self.in.Read(b)
	}
	// A console read blocks with no way to select on it, so poll with a short
	// timeout: the reader goroutine can then notice a shutdown request between
	// polls. "No data yet" becomes a temporary error the loop already ignores.
	h := windows.Handle(self.in.Fd())
	ev, err := windows.WaitForSingleObject(h, 75)
	if err != nil {
		return 0, err
	}
	if ev != windows.WAIT_OBJECT_0 {
		return 0, syscall.EAGAIN
	}
	n, rerr := self.in.Read(b)
	if n == 0 && rerr == nil {
		return 0, syscall.EAGAIN
	}
	return n, rerr
}

// CloseRead makes a blocking Read in another goroutine return. For a pipe (an
// overlay kitten) close the read side. For a real console do NOT close the handle
// (it is the shell's shared stdin); cancel any pending read instead, and rely on
// the polling Read noticing the loop's quit channel.
func (self *Term) CloseRead() error {
	if self.in == nil {
		return nil
	}
	if self.in_console {
		windows.CancelIoEx(windows.Handle(self.in.Fd()), nil)
		return nil
	}
	return self.in.Close()
}
func (self *Term) Write(b []byte) (int, error)       { return self.out.Write(b) }
func (self *Term) WriteString(s string) (int, error) { return self.out.WriteString(s) }

func (self *Term) WriteAll(b []byte) error {
	for len(b) > 0 {
		n, err := self.out.Write(b)
		if err != nil {
			return err
		}
		b = b[n:]
	}
	return nil
}

func (self *Term) WriteAllString(s string) error {
	return self.WriteAll(utils.UnsafeStringToBytes(s))
}

func (self *Term) ReadWithTimeout(b []byte, d time.Duration) (int, error) {
	// There is no fd-select on Windows pipes; timeouts are handled a level up by
	// the loop's reader goroutine. Read is console-aware (polls a real console).
	return self.Read(b)
}

func (self *Term) ApplyOperations(when uintptr, operations ...TermiosOperation) error {
	for _, op := range operations {
		op(self)
	}
	return nil
}

func (self *Term) PopState() error                 { return nil }
func (self *Term) PopStateWhen(when uintptr) error { return nil }
func (self *Term) Restore() error                  { self.restore_console(); return nil }
func (self *Term) RestoreWhen(when uintptr) error  { self.restore_console(); return nil }
func (self *Term) RestoreAndClose() error          { return self.Close() }
func (self *Term) WasEchoOnOriginally() bool       { return false }

func (self *Term) Suspend() (resume func() error, err error) {
	return func() error { return nil }, nil
}

func (self *Term) SuspendAndRun(callback func() error) error {
	return callback()
}

// An overlay kitten's stdio is a pipe with no ioctl, so kitty passes its size in
// the environment. A standalone kitten in a real console queries it directly. fd
// should be a console OUTPUT handle. Pixel sizes are left zero (consoles do not
// report them; kittens that need pixels ask kitty over the wire).
func GetSize(fd int) (*Winsize, error) {
	ans := &Winsize{Row: 24, Col: 80}
	haveEnv := false
	if v, err := strconv.Atoi(os.Getenv("OVERLAID_WINDOW_LINES")); err == nil && v > 0 {
		ans.Row = uint16(v)
		haveEnv = true
	}
	if v, err := strconv.Atoi(os.Getenv("OVERLAID_WINDOW_COLS")); err == nil && v > 0 {
		ans.Col = uint16(v)
		haveEnv = true
	}
	if !haveEnv && fd >= 0 {
		var info windows.ConsoleScreenBufferInfo
		if windows.GetConsoleScreenBufferInfo(windows.Handle(fd), &info) == nil {
			if r := int(info.Window.Bottom) - int(info.Window.Top) + 1; r > 0 {
				ans.Row = uint16(r)
			}
			if c := int(info.Window.Right) - int(info.Window.Left) + 1; c > 0 {
				ans.Col = uint16(c)
			}
		}
	}
	return ans, nil
}

func (self *Term) GetSize() (*Winsize, error) {
	fd := -1
	if self.out != nil {
		fd = int(self.out.Fd())
	}
	return GetSize(fd)
}

func Ctermid() string { return "CON" }

func (self *Term) DebugPrintln(a ...any) {
	msg := fmt.Appendln(nil, a...)
	const limit = 2048
	encoded := make([]byte, limit*2)
	for i := 0; i < len(msg); i += limit {
		end := min(i+limit, len(msg))
		chunk := msg[i:end]
		encoded = encoded[:cap(encoded)]
		base64.StdEncoding.Encode(encoded, chunk)
		_, _ = self.WriteString("\x1bP@kitty-print|")
		_, _ = self.Write(encoded)
		_, _ = self.WriteString("\x1b\\")
	}
}

var KittyStdout = sync.OnceValue(func() *os.File {
	if fds := os.Getenv(`KITTY_STDIO_FORWARDED`); fds != "" {
		if fd, err := strconv.Atoi(fds); err == nil && fd > -1 {
			if f := os.NewFile(uintptr(fd), "<kitty_stdout>"); f != nil {
				return f
			}
		}
	}
	return nil
})

func DebugPrintln(a ...any) {
	if f := KittyStdout(); f != nil {
		fmt.Fprintln(f, a...)
		return
	}
	term, err := OpenControllingTerm()
	if err == nil {
		defer term.Close()
		term.DebugPrintln(a...)
	}
}

func ReadSingleByteFromTerminal() (b byte, err error) {
	term, err := OpenControllingTerm()
	if err != nil {
		return 0, err
	}
	defer term.Close()
	ans := []byte{0}
	for {
		n, err := term.Read(ans)
		if err != nil {
			return 0, err
		}
		if n > 0 {
			return ans[0], nil
		}
	}
}
