//go:build windows

package ssh

import "io/fs"

// Shared memory is not used on Windows, so there is no SHM owner to verify.
func check_shm_owner(s fs.FileInfo) error { return nil }

// Windows cannot re-raise SIGINT to itself.
func reraise_sigint_to_self() {}
