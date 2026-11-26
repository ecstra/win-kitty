// License: GPLv3 Copyright: 2026, Kovid Goyal, <kovid at kovidgoyal.net>

package icat

import (
	"testing"
)

func TestImagesOnAnotherMachineAreSentAsData(t *testing.T) {
	for path, expected := range map[string]bool{
		`\\nas\photos\a.png`: true,
		`//nas/photos/a.png`: true,
		`Z:\photos\a.png`:    false,
		`C:\Users\me\a.png`:  false,
	} {
		if actual := on_another_machine(path); actual != expected {
			t.Fatalf("on_another_machine(%#v) = %v, expected %v", path, actual, expected)
		}
	}
}
