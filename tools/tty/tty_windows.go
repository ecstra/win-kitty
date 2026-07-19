//go:build windows

// License: GPLv3 Copyright: 2024, Kovid Goyal, <kovid at kovidgoyal.net>

package tty

import (
	"fmt"
	"os"
	"strconv"
	"sync"
	"time"

	"github.com/emmansun/base64"

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

// On Windows a kitten runs over pipes with kitty on the other end, so there is
// no termios and no console raw mode to toggle. Term wraps the read (stdin) and
// write (stdout) pipes, and the termios operations are all no-ops.
type Term struct {
	in, out   *os.File
	owns_file bool
}

type TermiosOperation func(*Term)

var (
	SetRaw          TermiosOperation = func(*Term) {}
	SetNoEcho       TermiosOperation = func(*Term) {}
	SetNoCanonical  TermiosOperation = func(*Term) {}
	SetReadPassword TermiosOperation = func(*Term) {}
	SetBlockingRead TermiosOperation = func(*Term) {}
)

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
	var err error
	if self.owns_file && self.in != nil {
		err = self.in.Close()
	}
	self.in, self.out = nil, nil
	return err
}

func (self *Term) Read(b []byte) (int, error)        { return self.in.Read(b) }
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
	// the loop's reader goroutine.
	return self.in.Read(b)
}

func (self *Term) ApplyOperations(when uintptr, operations ...TermiosOperation) error {
	for _, op := range operations {
		op(self)
	}
	return nil
}

func (self *Term) PopState() error                 { return nil }
func (self *Term) PopStateWhen(when uintptr) error { return nil }
func (self *Term) Restore() error                  { return nil }
func (self *Term) RestoreWhen(when uintptr) error  { return nil }
func (self *Term) RestoreAndClose() error          { return self.Close() }
func (self *Term) WasEchoOnOriginally() bool       { return false }

func (self *Term) Suspend() (resume func() error, err error) {
	return func() error { return nil }, nil
}

func (self *Term) SuspendAndRun(callback func() error) error {
	return callback()
}

// The kitten runs as an overlay window, so kitty passes its size in the
// environment (there is no ioctl on Windows pipes).
func GetSize(fd int) (*Winsize, error) {
	ans := &Winsize{Row: 24, Col: 80}
	if v, err := strconv.Atoi(os.Getenv("OVERLAID_WINDOW_LINES")); err == nil && v > 0 {
		ans.Row = uint16(v)
	}
	if v, err := strconv.Atoi(os.Getenv("OVERLAID_WINDOW_COLS")); err == nil && v > 0 {
		ans.Col = uint16(v)
	}
	return ans, nil
}

func (self *Term) GetSize() (*Winsize, error) {
	return GetSize(self.Fd())
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
