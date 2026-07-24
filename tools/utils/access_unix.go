//go:build !windows

package utils

import "golang.org/x/sys/unix"

const (
	AccessExists uint32 = unix.F_OK
	AccessRead   uint32 = unix.R_OK
	AccessWrite  uint32 = unix.W_OK
	AccessExec   uint32 = unix.X_OK
)

func Access(path string, mode uint32) error { return unix.Access(path, mode) }
