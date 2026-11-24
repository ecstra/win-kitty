//go:build !windows

package utils

import (
	"os"

	"golang.org/x/sys/unix"
)

func mmap_read_only(f *os.File, size int) ([]byte, error) {
	return unix.Mmap(int(f.Fd()), 0, size, unix.PROT_READ, unix.MAP_PRIVATE)
}

func munmap(data []byte) error { return unix.Munmap(data) }
