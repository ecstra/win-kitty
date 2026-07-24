//go:build !windows

package ssh

import (
	"os"
	"syscall"
)

func get_file_unique_id(fi os.FileInfo) file_unique_id {
	if st, ok := fi.Sys().(*syscall.Stat_t); ok {
		return file_unique_id{dev: uint64(st.Dev), inode: uint64(st.Ino)}
	}
	return file_unique_id{}
}
