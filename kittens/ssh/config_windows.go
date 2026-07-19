//go:build windows

package ssh

import (
	"os"
	"sync/atomic"
)

// No stable inode on Windows, so hand out a unique id per file to avoid falsely
// treating distinct files as hardlinks.
var file_uid_counter atomic.Uint64

func get_file_unique_id(fi os.FileInfo) file_unique_id {
	return file_unique_id{dev: 0, inode: file_uid_counter.Add(1)}
}
