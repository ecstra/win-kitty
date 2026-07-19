//go:build windows

package disk_cache

import "io/fs"

// Windows file info does not expose an inode, so fall back to size and mod time.
func file_inode(fi fs.FileInfo) uint64 { return 0 }
