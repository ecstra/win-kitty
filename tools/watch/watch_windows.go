//go:build windows

package watch

import (
	"fmt"

	"golang.org/x/sys/windows"
)

// Windows has no SIGUSR1, so kitty waits for a reload on a named event of its
// own, the one start_windows_reload_listener in child-monitor.c creates.
func signal_kitty_to_reload_config(kitty_pid int) error {
	name, err := windows.UTF16PtrFromString(fmt.Sprintf(`Local\kitty-reload-%d`, kitty_pid))
	if err != nil {
		return err
	}
	h, err := windows.OpenEvent(windows.EVENT_MODIFY_STATE, false, name)
	if err != nil {
		return err
	}
	defer windows.CloseHandle(h)
	return windows.SetEvent(h)
}
