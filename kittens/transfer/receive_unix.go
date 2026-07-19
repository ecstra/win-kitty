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
