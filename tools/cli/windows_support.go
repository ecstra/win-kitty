// License: GPLv3 Copyright: 2025, Kovid Goyal, <kovid at kovidgoyal.net>

package cli

import (
	"fmt"
	"runtime"
)

// LinuxOnly returns a clear, user-facing error when a kitten that has nothing
// to work with on Windows is run there, and nil otherwise, so the user gets an
// explicit message instead of a confusing partial failure:
//
//	if err := cli.LinuxOnly("desktop-ui", "it is a backend for the desktop portals of Linux"); err != nil { return 1, err }
func LinuxOnly(kitten, reason string) error {
	if runtime.GOOS == "windows" {
		return fmt.Errorf("The %s kitten works only on Linux, since %s", kitten, reason)
	}
	return nil
}
