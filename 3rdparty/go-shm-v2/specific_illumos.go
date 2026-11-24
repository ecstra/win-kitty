// License: GPLv3 Copyright: 2022, Kovid Goyal, <kovid at kovidgoyal.net>

package shm

// On most illumos distributions, /tmp is always mounted as tmpfs (in-memory),
// providing equivalent semantics to POSIX shared memory.
const SHM_DIR = "/tmp"
