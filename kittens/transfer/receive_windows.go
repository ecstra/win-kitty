//go:build windows

package transfer

import (
	"os"
	"time"
)

func (self *remote_file) apply_metadata() {
	mt := time.Unix(0, int64(self.mtime))
	_ = os.Chtimes(self.expanded_local_path, mt, mt)
	// Symlink permissions are not meaningful on Windows.
	if self.ftype != FileType_symlink {
		_ = os.Chmod(self.expanded_local_path, self.permissions)
	}
}

// Writing in place is only safe for a file no other link shares, and the link
// count is not in what os.Lstat reports here, so never fall back to it. The
// transfer then fails with the original permission error instead.
func open_for_writing_in_place(path string) *os.File { return nil }
