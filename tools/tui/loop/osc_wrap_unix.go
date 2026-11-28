//go:build !windows

package loop

// Terminals pass OSC through to programs here, so nothing is wrapped
func osc_wrap_escape_code(on bool) string { return "" }

func unwrap_osc(raw []byte) ([]byte, bool) { return nil, false }
