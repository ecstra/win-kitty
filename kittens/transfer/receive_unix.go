//go:build !windows

package transfer

import (
	"errors"
	"os"
	"syscall"

	"golang.org/x/sys/unix"
)

func (self *remote_file) apply_metadata() {
	t := unix.NsecToTimespec(int64(self.mtime))
	for {
		if err := unix.UtimesNanoAt(unix.AT_FDCWD, self.expanded_local_path, []unix.Timespec{t, t}, unix.AT_SYMLINK_NOFOLLOW); err == nil || !(errors.Is(err, syscall.EINTR) || errors.Is(err, syscall.EAGAIN)) {
			break
		}
	}
	if self.ftype == FileType_symlink {
		for {
			if err := unix.Fchmodat(unix.AT_FDCWD, self.expanded_local_path, syscall_mode(self.permissions), unix.AT_SYMLINK_NOFOLLOW); err == nil || !(errors.Is(err, syscall.EINTR) || errors.Is(err, syscall.EAGAIN)) {
				break
			}
		}
	} else {
		_ = os.Chmod(self.expanded_local_path, self.permissions)
	}
}

// Open path for writing in place, only when that cannot affect any other file,
// that is, a regular file with a single link, never through a symlink.
func open_for_writing_in_place(path string) *os.File {
	var st unix.Stat_t
	if unix.Lstat(path, &st) == nil && st.Mode&unix.S_IFMT == unix.S_IFREG && st.Nlink == 1 {
		if f, err := os.OpenFile(path, os.O_WRONLY|os.O_TRUNC|unix.O_NOFOLLOW, 0); err == nil {
			return f
		}
	}
	return nil
}
