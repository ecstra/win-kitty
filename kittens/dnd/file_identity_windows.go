//go:build windows

package dnd

import (
	"os"
)

// What os.Lstat reports on Windows carries no volume or file index, which
// os.SameFile reads from the path. Every stat here comes from os, the *At
// helpers included, which build full paths on Windows.
func same_file(a, b os.FileInfo) (bool, error) {
	return os.SameFile(a, b), nil
}

// Whether an entry already in the destination may lead to a folder elsewhere.
// Go reports a junction as irregular, neither a folder nor a symlink, though it
// leads to a folder as a symlink does, so a move must not merge into one that
// leads back into the folder being moved. Anything else irregular that leads
// to no folder still reads as no folder.
func is_link(mode os.FileMode) bool { return mode&(os.ModeSymlink|os.ModeIrregular) != 0 }

// os.SameFile reads the volume and index at the first compare, so a source
// compared only after it was copied would take those of a file saved over it
// by then. Reading them now ties the info to the file as it is.
func with_identity(info os.FileInfo) os.FileInfo {
	os.SameFile(info, info)
	return info
}
