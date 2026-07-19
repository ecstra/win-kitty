//go:build windows

package utils

import "os"

// The macOS user cache dir heuristic is never used on Windows.
func fileinfo_uid_matches_euid(s os.FileInfo) bool { return false }
