//go:build windows

package watch

// Windows kitty does not reload its config on a signal.
func signal_kitty_to_reload_config(kitty_pid int) error { return nil }
