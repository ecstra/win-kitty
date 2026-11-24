//go:build windows

package choose_files

import (
	"os"
	"os/exec"
)

func set_process_session_leader(cmd *exec.Cmd) {}

// Windows has no process groups to signal, so only the server itself is
// killed here, and workers it started may outlive it. A job object would
// take them down together.
func kill_process_group(p *os.Process) {
	p.Kill()
}
