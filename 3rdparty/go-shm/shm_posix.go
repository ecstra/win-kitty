// License: GPLv3 Copyright: 2022, Kovid Goyal, <kovid at kovidgoyal.net>

//go:build !windows

package shm

import (
	"errors"
	"fmt"
	"os"

	"golang.org/x/sys/unix"
)

var _ = fmt.Print

func mmap(sz int, access AccessFlags, fd int, off int64) ([]byte, error) {
	flags := unix.MAP_SHARED
	prot := unix.PROT_READ
	switch access {
	case COPY:
		prot |= unix.PROT_WRITE
		flags = unix.MAP_PRIVATE
	case WRITE:
		prot |= unix.PROT_WRITE
	}

	b, err := unix.Mmap(fd, off, sz, prot, flags)
	if err != nil {
		return nil, err
	}
	return b, nil
}

func munmap(s []byte) error {
	return unix.Munmap(s)
}

func truncate_or_unlink(ans *os.File, size uint64, unlink func(string) error) (err error) {
	fd := int(ans.Fd())
	sz := int64(size)
	if err = Fallocate_simple(fd, sz); err != nil {
		if !errors.Is(err, errors.ErrUnsupported) {
			return fmt.Errorf("fallocate() failed on fd from shm_open(%s) with size: %d with error: %w", ans.Name(), size, err)
		}
		for {
			if err = unix.Ftruncate(fd, sz); !errors.Is(err, unix.EINTR) {
				break
			}
		}
	}
	if err != nil {
		_ = ans.Close()
		_ = unlink(ans.Name())
		return fmt.Errorf("Failed to ftruncate() SHM file %s to size: %d with error: %w", ans.Name(), size, err)
	}
	return
}
