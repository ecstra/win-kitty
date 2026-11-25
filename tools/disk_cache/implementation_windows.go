//go:build windows

package disk_cache

import (
	"io/fs"

	"golang.org/x/sys/windows"
)

// Windows file info carries no inode, so the NTFS file ID stands in for it,
// read as os.SameFile reads it. The entries file is replaced by a rename on
// every write, which gives it a new ID, so a rewrite of the same size inside
// one tick of the mtime clock still reads as a change. A file that cannot be
// opened gives 0, leaving size and mtime to tell.
func file_inode(path string, _ fs.FileInfo) uint64 {
	p, err := windows.UTF16PtrFromString(path)
	if err != nil {
		return 0
	}
	h, err := windows.CreateFile(p, 0, windows.FILE_SHARE_READ|windows.FILE_SHARE_WRITE|windows.FILE_SHARE_DELETE,
		nil, windows.OPEN_EXISTING, windows.FILE_FLAG_BACKUP_SEMANTICS, 0)
	if err != nil {
		return 0
	}
	defer windows.CloseHandle(h)
	var info windows.ByHandleFileInformation
	if err = windows.GetFileInformationByHandle(h, &info); err != nil {
		return 0
	}
	return uint64(info.FileIndexHigh)<<32 | uint64(info.FileIndexLow)
}
