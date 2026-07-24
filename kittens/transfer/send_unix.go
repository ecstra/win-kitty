//go:build !windows

package transfer

import (
	"io/fs"
	"syscall"
)

func file_hash_for(fi fs.FileInfo) (FileHash, bool) {
	if st, ok := fi.Sys().(*syscall.Stat_t); ok {
		return FileHash{uint64(st.Dev), uint64(st.Ino)}, true
	}
	return FileHash{}, false
}
