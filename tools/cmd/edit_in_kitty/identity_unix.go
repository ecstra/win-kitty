//go:build !windows

package edit_in_kitty

import (
	"fmt"
	"io/fs"
	"syscall"
)

func file_identity(fi fs.FileInfo) string {
	if st, ok := fi.Sys().(*syscall.Stat_t); ok {
		return fmt.Sprintf("%d:%d:%d", st.Dev, st.Ino, fi.ModTime().UnixNano())
	}
	return fmt.Sprintf("%d:%d", fi.Size(), fi.ModTime().UnixNano())
}
