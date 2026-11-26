// License: GPLv3 Copyright: 2026, Kovid Goyal, <kovid at kovidgoyal.net>

//go:build !windows

package utils

// UnixSocketPath is where a unix: address lies on this machine
func UnixSocketPath(addr string) string { return addr }
