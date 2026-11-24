//go:build windows

package ssh

import "testing"

// Local paths on Windows have drive letters and backslashes, while the
// arcname is a path on the remote, POSIX, host.
func TestSSHCopyArcnamesWindows(t *testing.T) {
	for _, tc := range []struct {
		name, loc, dest, home, expected string
	}{
		{"home", `C:\Users\alice`, "", `C:\Users\alice`, "home/."},
		{"inside home", `C:\Users\alice\.config\app`, "", `C:\Users\alice`, "home/.config/app"},
		{"trailing separator", `C:\Users\alice\.config\app`, "", `C:\Users\alice\`, "home/.config/app"},
		{"relative destination", `C:\Users\alice-backup\config`, "config", `C:\Users\alice`, "home/config"},
		{"absolute destination", `C:\Users\alice\config`, "/etc/config", `C:\Users\alice`, "root/etc/config"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			if actual := get_arcname(tc.loc, tc.dest, tc.home); actual != tc.expected {
				t.Fatalf("Incorrect arcname: %q != %q", actual, tc.expected)
			}
		})
	}
}
