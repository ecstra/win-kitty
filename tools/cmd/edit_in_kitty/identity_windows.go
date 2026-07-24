//go:build windows

package edit_in_kitty

import (
	"fmt"
	"io/fs"
)

func file_identity(fi fs.FileInfo) string {
	// kitty's edit-protocol parser (EditCmd) expects dev:inode:size -- three
	// integers. Windows fs.FileInfo exposes no POSIX dev/inode, so report 0:0 for
	// those (kitty then treats the file as non-local, edits a temp copy and writes
	// the result back to the original on save) plus the real size. The previous
	// "size:modtime" was only two fields, which made EditCmd raise StopIteration,
	// so the editor never launched and the kitten hung.
	return fmt.Sprintf("0:0:%d", fi.Size())
}
