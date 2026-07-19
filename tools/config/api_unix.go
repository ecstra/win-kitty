//go:build !windows

package config

import (
	"github.com/shirou/gopsutil/v4/process"
	"golang.org/x/sys/unix"
)

func send_reload_signal(p *process.Process) error {
	return p.SendSignal(unix.SIGUSR1)
}
