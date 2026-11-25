//go:build windows

package config

import "testing"

// The Go config reader resolves ${KITTY_OS} in includes, and has to agree
// with kitty's own reader, which says windows
func TestKittyOSIsWindows(t *testing.T) {
	if got := kitty_os(); got != "windows" {
		t.Fatalf("kitty_os() is %q, want windows", got)
	}
}
