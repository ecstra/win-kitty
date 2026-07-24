//go:build !windows

package utils

import "golang.org/x/sys/unix"

func Dup2(oldfd, newfd int) error { return unix.Dup2(oldfd, newfd) }
