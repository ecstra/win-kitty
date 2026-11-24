//go:build windows

package choose_files

// Windows has no FIFOs in the file system to block on, and named pipes live
// under \\.\pipe\, where opening one with no server fails at once.
const open_nonblocking = 0
