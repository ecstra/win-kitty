package choose_files

import (
	"runtime"
	"testing"
)

// The preview commands these tests install are #!/bin/sh scripts, which
// Windows cannot execute.
func skip_where_sh_scripts_cannot_run(t *testing.T) {
	t.Helper()
	if runtime.GOOS == "windows" {
		t.Skip("the preview commands in this test are sh scripts, which Windows cannot run")
	}
}
