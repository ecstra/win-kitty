//go:build !windows

package choose_files

import "golang.org/x/sys/unix"

// Opening a FIFO blocks until a writer appears, unless it is opened nonblocking.
const open_nonblocking = unix.O_NONBLOCK
