//go:build windows

package config

import "github.com/shirou/gopsutil/v4/process"

// Windows kitty does not reload its config on a signal, so this is a no-op.
func send_reload_signal(p *process.Process) error { return nil }
