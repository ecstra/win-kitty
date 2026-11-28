//go:build windows

package loop

import (
	"bytes"
)

// Windows' console drops OSC from what a program reads, and passes APC on, so
// while a kitten runs it asks kitty to send its OSC replies wrapped in APC,
// such as those of drag and drop and the clipboard

var wrapped_osc_prefix = []byte("kitty-osc;")

func osc_wrap_escape_code(on bool) string {
	if on {
		return "\x1b_kitty-osc-wrap;1\x1b\\"
	}
	return "\x1b_kitty-osc-wrap;0\x1b\\"
}

// The OSC an APC from kitty carries, if it carries one
func unwrap_osc(raw []byte) ([]byte, bool) {
	if bytes.HasPrefix(raw, wrapped_osc_prefix) {
		return raw[len(wrapped_osc_prefix):], true
	}
	return nil, false
}
