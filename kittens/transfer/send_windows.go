//go:build windows

package transfer

import (
	"io/fs"
	"sync/atomic"
)

// Windows file info exposes no stable inode, so hand out a unique id per file.
// That disables hardlink de-duplication but never falsely merges distinct files.
var file_hash_counter atomic.Uint64

func file_hash_for(fi fs.FileInfo) (FileHash, bool) {
	return FileHash{0, file_hash_counter.Add(1)}, true
}
