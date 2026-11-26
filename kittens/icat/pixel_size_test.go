// License: GPLv3 Copyright: 2026, Kovid Goyal, <kovid at kovidgoyal.net>

package icat

import (
	"testing"
)

func TestPixelSizeReplyStaysInRange(t *testing.T) {
	for reply, expected := range map[string][2]uint16{
		"\x1b[4;900;1600t":            {1600, 900},
		"junk\x1b[4;720;1280t":        {1280, 720},
		"\x1b[4;100;70000t":           {0, 0}, // too wide to fit, not wrapped to 4464
		"\x1b[4;65535;65535t":         {65535, 65535},
		"\x1b[4;0;1280t":              {0, 0},
		"\x1b[4;-5;1280t":             {0, 0},
		"\x1b[4;720t":                 {0, 0},
		"no reply":                    {0, 0},
		"\x1b[4;99999999999999;1280t": {0, 0},
	} {
		if w, h := parse_pixel_size_reply(reply); w != expected[0] || h != expected[1] {
			t.Fatalf("parse_pixel_size_reply(%q) = %d, %d, expected %d, %d", reply, w, h, expected[0], expected[1])
		}
	}
}
