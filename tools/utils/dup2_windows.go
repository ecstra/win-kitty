//go:build windows

package utils

import "errors"

// There is no fd duplication onto a specific number on Windows.
func Dup2(oldfd, newfd int) error { return errors.New("Dup2 is not supported on Windows") }
