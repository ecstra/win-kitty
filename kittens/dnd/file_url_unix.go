//go:build !windows

package dnd

import (
	"path/filepath"
)

// The path a file URL names
func path_from_file_url(p string) string {
	return filepath.Clean(p)
}
