//go:build windows

package utils

import (
	"os"
	"os/exec"
)

// Windows has no execve, so run the program to completion and exit with its code.
func Exec(argv0 string, argv []string, envv []string) error {
	c := exec.Command(argv0)
	c.Args = argv
	c.Env = envv
	c.Stdin, c.Stdout, c.Stderr = os.Stdin, os.Stdout, os.Stderr
	if err := c.Run(); err != nil {
		if ee, ok := err.(*exec.ExitError); ok {
			os.Exit(ee.ExitCode())
		}
		return err
	}
	os.Exit(0)
	return nil
}
