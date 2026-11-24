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

// A path outside home goes to the same path on the remote, and a Windows path
// there has a drive or share the remote has no counterpart for.
func TestSSHCopyOutsideHomeWindows(t *testing.T) {
	home := `C:\Users\alice`
	for _, loc := range []string{`D:\proj\tool.conf`, `C:\tools\x`, `\\server\share\x`} {
		if err := check_remote_place(loc, home); err == nil {
			t.Fatalf("%s is outside home and was not refused", loc)
		}
	}
	for _, loc := range []string{home, `C:\Users\alice\.config\app`} {
		if err := check_remote_place(loc, home); err != nil {
			t.Fatalf("%s is in home and was refused: %s", loc, err)
		}
	}
}
