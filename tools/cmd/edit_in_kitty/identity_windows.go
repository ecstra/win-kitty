//go:build windows

package edit_in_kitty

import (
	"fmt"
	"io/fs"
)

func file_identity(fi fs.FileInfo) string {
	return fmt.Sprintf("%d:%d", fi.Size(), fi.ModTime().UnixNano())
}
