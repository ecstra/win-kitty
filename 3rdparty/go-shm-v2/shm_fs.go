// License: GPLv3 Copyright: 2022, Kovid Goyal, <kovid at kovidgoyal.net>
//go:build linux || netbsd || openbsd || dragonfly || illumos

package shm

import (
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"runtime"
	"strings"

	"golang.org/x/sys/unix"
)

var _ = fmt.Print

type file_based_mmap struct {
	f            *os.File
	pos          int64
	region       []byte
	unlinked     bool
	special_name string
}

func ShmUnlink(name string) error {
	return os.Remove(file_path_from_name(PosixName(name)))
}

func file_mmap(f *os.File, size uint64, access AccessFlags, truncate bool, special_name string) (MMap, error) {
	if truncate {
		err := truncate_or_unlink(f, size, os.Remove)
		if err != nil {
			return nil, err
		}
	}
	region, err := mmap(int(size), access, int(f.Fd()), 0)
	if err != nil {
		f.Close()
		os.Remove(f.Name())
		return nil, err
	}
	return &file_based_mmap{f: f, region: region, special_name: special_name}, nil
}

func (self *file_based_mmap) Seek(offset int64, whence int) (ret int64, err error) {
	switch whence {
	case io.SeekStart:
		self.pos = offset
	case io.SeekEnd:
		self.pos = int64(len(self.region)) + offset
	case io.SeekCurrent:
		self.pos += offset
	}
	return self.pos, nil
}

func (self *file_based_mmap) Read(b []byte) (n int, err error) {
	return Read(self, b)
}

func (self *file_based_mmap) Write(b []byte) (n int, err error) {
	return Write(self, b)
}

func (self *file_based_mmap) Stat() (fs.FileInfo, error) {
	return self.f.Stat()
}

func (self *file_based_mmap) Name() string {
	if self.special_name != "" {
		return self.special_name
	}
	return PosixName(filepath.Base(self.f.Name()))
}

func (self *file_based_mmap) Flush() error {
	return unix.Msync(self.region, unix.MS_SYNC)
}

func (self *file_based_mmap) FileSystemName() string {
	return self.f.Name()
}

func (self *file_based_mmap) Slice() []byte {
	return self.region
}

func (self *file_based_mmap) Close() (err error) {
	if self.region != nil {
		self.f.Close()
		err = munmap(self.region)
		self.region = nil
	}
	return err
}

func (self *file_based_mmap) Unlink() (err error) {
	if self.unlinked {
		return nil
	}
	self.unlinked = true
	return os.Remove(self.f.Name())
}

func (self *file_based_mmap) IsFileSystemBacked() bool { return true }

// openbsd_shm_path mirrors makeshmpath() from OpenBSD libc:
//
//	SHA256Data(origpath, strlen(origpath), buf);
//	snprintf(shmpath, len, "/tmp/%s.shm", buf);
//
// The name is hashed with its leading slash included and the digest is
// rendered as a lowercase hex string.
// See https://github.com/openbsd/src/blob/master/lib/libc/gen/shm_open.c
func openbsd_shm_path(name string) string {
	hash := sha256.Sum256(UnsafeStringToBytes(PosixName(name)))
	return filepath.Join(SHM_DIR, hex.EncodeToString(hash[:])+".shm")
}

// file_path_from_name maps a POSIX shared memory name, which starts with a
// slash, onto the path of the file backing it.
func file_path_from_name(name string) string {
	if runtime.GOOS == "openbsd" {
		return openbsd_shm_path(name)
	}
	return filepath.Join(SHM_DIR, strings.TrimLeft(name, "/"))
}

func create_temp(pattern string, size uint64) (ans MMap, err error) {
	var prefix, suffix string
	prefix, suffix, err = prefix_and_suffix(pattern)
	if err != nil {
		return
	}
	var f *os.File
	var name string
	try := 0
	for {
		name = prefix + RandomFilename() + suffix
		path := file_path_from_name(name)
		f, err = os.OpenFile(path, os.O_EXCL|os.O_CREATE|os.O_RDWR, 0600)
		if err != nil {
			if errors.Is(err, fs.ErrExist) {
				try += 1
				if try > 10000 {
					return nil, &os.PathError{Op: "createtemp", Path: prefix + "*" + suffix, Err: fs.ErrExist}
				}
				continue
			}
			if errors.Is(err, fs.ErrNotExist) {
				return nil, &ErrNotSupported{err: err}
			}
			return
		}
		break
	}
	return file_mmap(f, size, WRITE, true, name)
}

func open(name string) (*os.File, error) {
	ans, err := os.OpenFile(file_path_from_name(name), os.O_RDONLY, 0)
	if err != nil {
		if errors.Is(err, fs.ErrNotExist) {
			if _, serr := os.Stat(SHM_DIR); serr != nil && errors.Is(serr, fs.ErrNotExist) {
				return nil, &ErrNotSupported{err: serr}
			}
		}
		return nil, err
	}
	return ans, nil
}

func Open(name string, size uint64) (MMap, error) {
	name = PosixName(name)
	ans, err := open(name)
	if err != nil {
		return nil, err
	}
	if size == 0 {
		s, err := ans.Stat()
		if err != nil {
			ans.Close()
			return nil, fmt.Errorf("Failed to stat SHM file with error: %w", err)
		}
		size = uint64(s.Size())
	}
	return file_mmap(ans, size, READ, false, name)
}
