//go:build !windows

package utils

import "golang.org/x/sys/unix"

// Exec replaces the current process with the given program.
func Exec(argv0 string, argv []string, envv []string) error {
	return unix.Exec(argv0, argv, envv)
}
