//go:build !windows

package utils

func exe_file_names(cmd string) []string { return []string{cmd} }
