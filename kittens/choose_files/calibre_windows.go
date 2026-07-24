//go:build windows

package choose_files

import "os/exec"

func set_process_session_leader(cmd *exec.Cmd) {}
