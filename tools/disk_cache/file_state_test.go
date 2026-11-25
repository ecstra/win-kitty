package disk_cache

import (
	"os"
	"path/filepath"
	"testing"
)

// The entries file is rewritten through a rename, and a rewrite of the same
// size inside one tick of the mtime clock must still read as a change.
func TestFileStateSeesSameSizeReplacement(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "entries.json")
	if err := os.WriteFile(path, []byte("first"), 0o600); err != nil {
		t.Fatal(err)
	}
	before, err := get_file_state_from_path(path)
	if err != nil {
		t.Fatal(err)
	}
	temp := path + ".temp"
	if err := os.WriteFile(temp, []byte("other"), 0o600); err != nil {
		t.Fatal(err)
	}
	if err := os.Rename(temp, path); err != nil {
		t.Fatal(err)
	}
	if err := os.Chtimes(path, before.ModTime, before.ModTime); err != nil {
		t.Fatal(err)
	}
	after, err := get_file_state_from_path(path)
	if err != nil {
		t.Fatal(err)
	}
	if after.equal(before) {
		t.Fatalf("a same size replacement with the old mtime reads as unchanged: %s", after)
	}
}
