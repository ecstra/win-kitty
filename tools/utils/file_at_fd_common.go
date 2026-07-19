// License: GPLv3 Copyright: 2024, Kovid Goyal, <kovid at kovidgoyal.net>

package utils

import (
	"context"
	"io"
	"os"
	"sync/atomic"

	"github.com/kovidgoyal/go-parallel"
)

// Portable helpers shared by the Unix and Windows file_at_fd implementations.
// Nothing here depends on platform specific syscalls.

// Not thread safe reference counted wrapper for os.File
type RefCountedFile struct {
	f      *os.File
	refcnt atomic.Int32
}

func NewRefCountedFile(f *os.File) *RefCountedFile {
	ans := RefCountedFile{f: f}
	ans.refcnt.Add(1)
	return &ans
}

func (f *RefCountedFile) NewRef() *RefCountedFile {
	f.refcnt.Add(1)
	return f
}

func (f *RefCountedFile) Unref() *RefCountedFile {
	if f.refcnt.Add(-1) == 0 {
		f.f.Close()
		f.f = nil
	}
	return nil
}

func (f *RefCountedFile) File() *os.File { return f.f }

type CopyFolderOptions struct {
	Disallow_hardlinks bool
	Follow_symlinks    bool
	Filter_files       func(parent *os.File, child os.FileInfo) bool
}

// Copy the file objects as efficiently as possible with cancellation. The
// files are always closed before this function returns.
func CopyFileAndClose(ctx context.Context, src *os.File, dest *os.File) (err error) {
	err_chan := make(chan error)
	go func() {
		defer func() {
			if r := recover(); r != nil {
				err_chan <- parallel.Format_stacktrace_on_panic(r, 1)
			}
		}()
		// this go routine will automatically exit when src/dest are closed
		// even if copying is not complete. io.Copy() automatically use
		// sendfile() or similar mechanisms for efficiency.
		_, err := io.Copy(dest, src)
		err_chan <- err
	}()

	select {
	case <-ctx.Done():
		src.Close()
		dest.Close()
		// wait for go routine to exit
		<-err_chan
		return ctx.Err()
	case err := <-err_chan:
		src.Close()
		dest.Close()
		return err
	}
}
