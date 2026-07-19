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
