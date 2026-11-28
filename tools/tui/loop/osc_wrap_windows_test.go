//go:build windows

package loop

import (
	"strings"
	"testing"
)

func TestKittensGetOscRepliesWrappedInApc(t *testing.T) {
	opts := TerminalStateOptions{}
	if !strings.HasPrefix(opts.SetStateEscapeCodes(), "\x1b_kitty-osc-wrap;1\x1b\\") {
		t.Fatalf("a kitten does not turn the wrapping on: %q", opts.SetStateEscapeCodes())
	}
	if !strings.HasSuffix(opts.ResetStateEscapeCodes(), "\x1b_kitty-osc-wrap;0\x1b\\") {
		t.Fatalf("a kitten does not turn the wrapping off: %q", opts.ResetStateEscapeCodes())
	}
	var got []string
	lp := new_loop()
	lp.OnEscapeCode = func(etype EscapeCodeType, raw []byte) error {
		got = append(got, string(raw))
		return nil
	}
	if err := lp.handle_apc([]byte("kitty-osc;5522;t=m:x=1")); err != nil {
		t.Fatal(err)
	}
	if err := lp.handle_apc([]byte("Gi=1;OK")); err != nil {
		t.Fatal(err)
	}
	if len(got) != 2 || got[0] != "5522;t=m:x=1" || got[1] != "Gi=1;OK" {
		t.Fatalf("wrong escape codes reached the kitten: %#v", got)
	}
}
