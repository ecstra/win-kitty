// License: GPLv3 Copyright: 2025, Kovid Goyal, <kovid at kovidgoyal.net>

package cli

import (
	"fmt"
	"runtime"
)

// NotImplementedOnWindows returns a clear, user-facing error when a kitten that
// does not yet work on the native Windows port is run there, and nil otherwise.
// Unsupported kittens call it at the top of their main so the user gets an
// explicit message instead of a confusing partial failure or crash:
//
//	if err := cli.NotImplementedOnWindows("dnd"); err != nil { return 1, err }
func NotImplementedOnWindows(kitten string) error {
	if runtime.GOOS == "windows" {
		return fmt.Errorf("The %s kitten is not yet implemented on Windows", kitten)
	}
	return nil
}
