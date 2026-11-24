//go:build !windows

package choose_files

import (
	"os"
	"os/exec"

	"golang.org/x/sys/unix"
)

func set_process_session_leader(cmd *exec.Cmd) {
	cmd.SysProcAttr = &unix.SysProcAttr{Setsid: true}
}

func kill_process_group(p *os.Process) {
	unix.Kill(-p.Pid, unix.SIGKILL)
}
