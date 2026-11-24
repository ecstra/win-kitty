//go:build windows

package utils

import (
	"os"
	"os/exec"
	"os/signal"
)

// Windows has no execve, so run the program to completion and exit with its code.
func Exec(argv0 string, argv []string, envv []string) error {
	c := exec.Command(argv0)
	c.Args = argv
	c.Env = envv
	c.Stdin, c.Stdout, c.Stderr = os.Stdin, os.Stdout, os.Stderr
	// After a real exec there is nothing left for Ctrl+C to kill but the
	// program. Here a Ctrl+C reaches this process too, and dying of it left the
	// program running orphaned, a shell reading the terminal beside the one
	// that started it. Handling the signal keeps this process waiting instead.
	ignore_interrupt := make(chan os.Signal, 1)
	signal.Notify(ignore_interrupt, os.Interrupt)
	defer signal.Stop(ignore_interrupt)
	if err := c.Run(); err != nil {
		if ee, ok := err.(*exec.ExitError); ok {
			os.Exit(ee.ExitCode())
		}
		return err
	}
	os.Exit(0)
	return nil
}
