// License: GPLv3 Copyright: 2024, Kovid Goyal, <kovid at kovidgoyal.net>

package utils

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

var _ = fmt.Print

func TestFileLock(t *testing.T) {
	tdir := t.TempDir()

	// Lock a regular file, not the directory. flock() takes a directory but
	// LockFileEx does not, and every caller in kitty locks a regular file
	// anyway: the disk cache lock file and the readline history file.
	lock_path := filepath.Join(tdir, "lockfile")
	if err := os.WriteFile(lock_path, []byte("x"), 0o600); err != nil {
		t.Fatalf("Could not create %s: %s", lock_path, err)
	}
	file_descriptor, err := os.Open(lock_path)
	if err != nil {
		t.Fatalf("Initial open of %s failed with error: %s", lock_path, err)
	}
	if err = LockFileExclusive(file_descriptor); err != nil {
		file_descriptor.Close()
		t.Fatalf("Initial lock of %s failed with error: %s", lock_path, err)
	}
	defer func() {
		_ = UnlockFile(file_descriptor)
		file_descriptor.Close()
	}()
	cmd := exec.Command(KittyExe(), "+runpy", `
import sys, os
fd = os.open(sys.argv[-1], os.O_RDWR)
try:
    if sys.platform == 'win32':
        import msvcrt
        msvcrt.locking(fd, msvcrt.LK_NBLCK, 1)
    else:
        import fcntl
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
except OSError:  # BlockingIOError on POSIX, PermissionError on Windows
    sys.exit(0)
else:
    print("Lock unexpectedly succeeded", flush=True)
    sys.exit(1)
`, lock_path)
	if output, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("Lock test process failed with error: %s and output:\n%s", err, string(output))
	}
}
