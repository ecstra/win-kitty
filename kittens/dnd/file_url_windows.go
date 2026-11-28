//go:build windows

package dnd

import (
	"path/filepath"
	"strings"
)

// The path a file URL names. On Windows its path is /C:/x for the drive path
// C:\x and ///host/share/x for a share, as kitty writes what Explorer drops,
// and cleaned as it stands both would name a folder that does not exist.
func path_from_file_url(p string) string {
	if len(p) >= 3 && p[0] == '/' && p[2] == ':' && (p[1]|0x20) >= 'a' && (p[1]|0x20) <= 'z' {
		return filepath.Clean(p[1:])
	}
	if strings.HasPrefix(p, "///") {
		return filepath.Clean(p[1:])
	}
	return filepath.Clean(p)
}
