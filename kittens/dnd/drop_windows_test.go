//go:build windows

package dnd

import (
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

// An editor saving over a file swaps a new one in by rename, which a move must
// not take for the source it was given, and delete
func TestAMoveSourceReplacedByRenameIsNotTakenForIt(t *testing.T) {
	dir := t.TempDir()
	src, saved := filepath.Join(dir, "report.txt"), filepath.Join(dir, "save.tmp")
	for _, p := range []string{src, saved} {
		if err := os.WriteFile(p, []byte(p), 0o600); err != nil {
			t.Fatal(err)
		}
	}
	info, err := os.Lstat(src)
	if err != nil {
		t.Fatal(err)
	}
	ms := move_source{info: with_identity(info)}
	if err := os.Rename(saved, src); err != nil {
		t.Fatal(err)
	}
	if cur, err := ms.current(src); err != nil || cur != nil {
		t.Fatalf("the file saved over the source passes for it: %v %v", cur, err)
	}
}

// A folder reached through a junction, or a subst drive, sits where the folder
// it leads to does, so a move of a folder above that is a drop into itself
func TestADropThroughAJunctionIsInsideTheRealSource(t *testing.T) {
	root := t.TempDir()
	src := filepath.Join(root, "proj")
	target := filepath.Join(src, "a", "b")
	if err := os.MkdirAll(filepath.Join(target, "c"), 0o700); err != nil {
		t.Fatal(err)
	}
	link := filepath.Join(root, "j")
	if out, err := exec.Command("cmd", "/c", "mklink", "/J", link, target).CombinedOutput(); err != nil {
		t.Fatalf("could not make a junction: %s %s", err, out)
	}
	src_info, err := os.Lstat(src)
	if err != nil {
		t.Fatal(err)
	}
	d, err := os.Open(filepath.Join(link, "c"))
	if err != nil {
		t.Fatal(err)
	}
	defer d.Close()
	inside, err := destination_is_within_source(d, []os.FileInfo{src_info})
	if err != nil || !inside {
		t.Fatalf("a drop into %s through a junction is not inside %s: %v %v", d.Name(), src, inside, err)
	}
}

// A local folder drop checks the folder is not dropped into itself, and a move
// checks each source is still the file it was, both by comparing files
func TestDropComparesFilesOnWindows(t *testing.T) {
	root := t.TempDir()
	src, other := filepath.Join(root, "src"), filepath.Join(root, "other")
	inner := filepath.Join(src, "inner")
	for _, d := range []string{inner, other} {
		if err := os.MkdirAll(d, 0o700); err != nil {
			t.Fatal(err)
		}
	}
	src_info, err := os.Lstat(src)
	if err != nil {
		t.Fatal(err)
	}
	for _, c := range []struct {
		dest   string
		inside bool
	}{{inner, true}, {src, true}, {other, false}, {root, false}} {
		d, err := os.Open(c.dest)
		if err != nil {
			t.Fatal(err)
		}
		inside, err := destination_is_within_source(d, []os.FileInfo{src_info})
		d.Close()
		if err != nil {
			t.Fatalf("dropping into %s: %s", c.dest, err)
		}
		if inside != c.inside {
			t.Fatalf("dropping into %s counted as inside the source: %v", c.dest, inside)
		}
	}
	if cur, err := (move_source{info: src_info}).current(src); err != nil || cur == nil {
		t.Fatalf("an unchanged move source is not recognised: %v %v", cur, err)
	}
	if cur, err := (move_source{info: src_info}).current(other); err != nil || cur != nil {
		t.Fatalf("another folder passes for the move source: %v %v", cur, err)
	}
}

func TestParseUriListGivesWindowsPaths(t *testing.T) {
	ans, err := parse_uri_list("file:///C:/Users/me/a%20b.txt\r\nfile://///server/share/x.txt\r\n")
	if err != nil {
		t.Fatal(err)
	}
	expected := []string{`C:\Users\me\a b.txt`, `\\server\share\x.txt`}
	if len(ans) != len(expected) {
		t.Fatalf("got %d paths: %#v", len(ans), ans)
	}
	for i, e := range expected {
		if ans[i].path != e {
			t.Fatalf("path %d is %q, not %q", i, ans[i].path, e)
		}
	}
}
