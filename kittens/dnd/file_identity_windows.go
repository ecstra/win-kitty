//go:build windows

package dnd

import (
	"fmt"
	"os"
)

// What os.Lstat reports on Windows carries no device or file index, so files
// cannot be identified this way. Only the Linux birth time lookup uses it.
func file_identity(info os.FileInfo) (dev, ino uint64, err error) {
	return 0, 0, fmt.Errorf("cannot identify %s by device and inode on Windows", info.Name())
}
