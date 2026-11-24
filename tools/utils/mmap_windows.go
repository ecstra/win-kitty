//go:build windows

package utils

import (
	"errors"
	"os"
)

// Callers fall back to reading the whole file when mapping fails, which is
// all the read only use here needs on Windows.
func mmap_read_only(f *os.File, size int) ([]byte, error) { return nil, errors.ErrUnsupported }

func munmap(data []byte) error { return nil }
