//go:build windows

package config

import (
	"fmt"

	"github.com/shirou/gopsutil/v4/process"
	"golang.org/x/sys/windows"
)

// Windows has no SIGUSR1. kitty listens for config-reload requests on a
// per-process named event (see child-monitor.c start_windows_reload_listener).
// Open it by kitty's pid and signal it.
func send_reload_signal(p *process.Process) error {
	name, err := windows.UTF16PtrFromString(fmt.Sprintf(`Local\kitty-reload-%d`, p.Pid))
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
