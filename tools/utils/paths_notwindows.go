//go:build !windows

package utils

import (
	"os"
	"syscall"
)

func fileinfo_uid_matches_euid(s os.FileInfo) bool {
	stat, ok := s.Sys().(syscall.Stat_t)
	return ok && int(stat.Uid) == os.Geteuid()
}
