//go:build windows

package dnd

import (
	"testing"
)

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
