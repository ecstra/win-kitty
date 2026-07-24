//go:build !windows

package choose_files

import (
	"os/exec"

	"golang.org/x/sys/unix"
)

func set_process_session_leader(cmd *exec.Cmd) {
	cmd.SysProcAttr = &unix.SysProcAttr{Setsid: true}
}
