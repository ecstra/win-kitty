// License: GPLv3 Copyright: 2026, Kovid Goyal, <kovid at kovidgoyal.net>

//go:build !windows

package icat

// on_another_machine is whether kitty refuses to open path, which happens on Windows only
func on_another_machine(string) bool { return false }
