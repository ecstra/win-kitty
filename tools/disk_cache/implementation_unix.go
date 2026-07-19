//go:build !windows

package disk_cache

import (
	"io/fs"
	"syscall"
)

func file_inode(fi fs.FileInfo) uint64 {
	if st, ok := fi.Sys().(*syscall.Stat_t); ok {
		return uint64(st.Ino)
	}
	return 0
}
