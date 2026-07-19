//go:build windows

package utils

import "os"

// Access approximates POSIX access() on Windows. Existence and read are assumed
// available for any file that stats. Write checks the read only attribute.
// Execute is decided by extension on Windows, not a mode bit, so any file that
// exists passes; callers that need PATHEXT semantics handle that separately.
const (
	AccessExists uint32 = 0
	AccessRead   uint32 = 4
	AccessWrite  uint32 = 2
	AccessExec   uint32 = 1
)

func Access(path string, mode uint32) error {
	st, err := os.Stat(path)
	if err != nil {
		return err
	}
	if mode&AccessWrite != 0 && st.Mode().Perm()&0o200 == 0 {
		return os.ErrPermission
	}
	return nil
}
