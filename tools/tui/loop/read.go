// License: GPLv3 Copyright: 2022, Kovid Goyal, <kovid at kovidgoyal.net>

package loop

import (
	"fmt"
	"io"
	"os"
	"regexp"
	"strings"
	"time"

	"github.com/kovidgoyal/go-parallel"
	"github.com/kovidgoyal/kitty/tools/tty"
)

var _ = fmt.Print

func (self *Loop) dispatch_input_data(data []byte) error {
	// Opt-in diagnostic: set KITTEN_INPUT_LOG to a file path to record every chunk
	// of input the kitten receives from the terminal. Used to debug key handling.
	if p := os.Getenv("KITTEN_INPUT_LOG"); p != "" {
		if f, err := os.OpenFile(p, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o644); err == nil {
			fmt.Fprintf(f, "%q\n", string(data))
			f.Close()
		}
	}
	if self.OnReceivedData != nil {
		err := self.OnReceivedData(data)
		if err != nil {
			return err
		}
	}
	err := self.escape_code_parser.Parse(data)
	if err != nil {
		return err
	}
	return nil
}

func read_ignoring_temporary_errors(f *tty.Term, buf []byte) (int, error) {
	n, err := f.Read(buf)
	if is_temporary_error(err) {
		return 0, nil
	}
	if n == 0 {
		return 0, io.EOF
	}
	return n, err
}

func has_da1_response(s string) bool {
	pat := regexp.MustCompile("\x1b\\[\\?[0-9:;]+c")
	return pat.FindString(s) != ""
}

func do_roundtrip_to_terminal(term *tty.Term, timeout time.Duration) {
	// ask for primary device attributes
	for {
		if err := term.WriteAllString("\x1b[c"); err != nil && !is_temporary_error(err) {
			return
		} else {
			break
		}
	}
	s := strings.Builder{}
	s.Grow(256)
	received := make(chan error)
	go func() {
		defer func() {
			if r := recover(); r != nil {
				text := parallel.Format_stacktrace_on_panic(r, 1).Error()
				received <- fmt.Errorf("%s", text)
			}
		}()
		for {
			buf := make([]byte, 1024)
			n, err := read_ignoring_temporary_errors(term, buf)
			if n > 0 {
				s.Write(buf[:n])
				if has_da1_response(s.String()) {
					received <- nil
					return
				}
			}
			if err != nil {
				received <- err
			}
		}
	}()
	select {
	case <-received:
	case <-time.After(timeout):
	}
}
