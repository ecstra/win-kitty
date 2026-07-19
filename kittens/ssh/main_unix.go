//go:build !windows

package ssh

import (
	"fmt"
	"io/fs"
	"os"
	"syscall"
	"time"

	"golang.org/x/sys/unix"
)

func check_shm_owner(s fs.FileInfo) error {
	if stat, ok := s.Sys().(syscall.Stat_t); ok {
		if os.Getuid() != int(stat.Uid) || os.Getgid() != int(stat.Gid) {
			return fmt.Errorf("Incorrect owner on SHM file")
		}
	}
	return nil
}

func reraise_sigint_to_self() {
	_ = unix.Kill(os.Getpid(), unix.SIGINT)
	// Give the signal time to be delivered
	time.Sleep(20 * time.Millisecond)
}
