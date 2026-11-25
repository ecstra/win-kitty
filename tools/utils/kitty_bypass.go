// License: GPLv3 Copyright: 2026, Kovid Goyal, <kovid at kovidgoyal.net>

package utils

import (
	"encoding/binary"
	"fmt"
	"io"
	"os"
	"strconv"
	"time"
)

func open_bypass_pipe() (f *os.File, window_id uint64, err error) {
	pid := os.Getenv("KITTY_PID")
	wid := os.Getenv("KITTY_WINDOW_ID")
	if pid == "" || wid == "" {
		return nil, 0, fmt.Errorf("KITTY_PID/KITTY_WINDOW_ID not set; cannot reach the kitty bypass pipe")
	}
	window_id, err = strconv.ParseUint(wid, 10, 64)
	if err != nil {
		return nil, 0, fmt.Errorf("invalid KITTY_WINDOW_ID %q: %w", wid, err)
	}
	pipe_name := `\\.\pipe\kitty-gfx-` + pid
	for attempt := 0; attempt < 100; attempt++ {
		if f, err = os.OpenFile(pipe_name, os.O_WRONLY, 0); err == nil {
			return f, window_id, nil
		}
		time.Sleep(10 * time.Millisecond) // pipe momentarily has no free instance between server accepts
	}
	return nil, 0, fmt.Errorf("failed to connect to the kitty bypass pipe: %w", err)
}

// OpenKittyBypassStream opens a persistent connection to kitty's bypass pipe,
// sends the window-id header, and returns a writer whose bytes kitty injects
// into that window's parser as they arrive. Used by kitten TUIs on Windows to
// route their output around conhost (which re-renders output on its own frame
// cadence, causing flicker and cursor bouncing). Only meaningful on Windows.
func OpenKittyBypassStream() (io.WriteCloser, error) {
	f, window_id, err := open_bypass_pipe()
	if err != nil {
		return nil, err
	}
	var header [8]byte
	binary.LittleEndian.PutUint64(header[:], window_id)
	if _, err = f.Write(header[:]); err != nil {
		f.Close()
		return nil, err
	}
	return f, nil
}

// SendToKittyBypass writes raw terminal bytes to kitty over its per-process named
// pipe (\\.\pipe\kitty-gfx-<pid>), prefixed with the target window id as a
// little-endian uint64. On Windows, conhost (ConPTY) strips DCS/APC sequences --
// the graphics APC, the kitty-edit DCS, etc. -- from a child's stdout before they
// reach kitty, so kittens that rely on those send them through this side channel
// instead; kitty injects the bytes straight into the target window's parser. Only
// meaningful on Windows (callers guard with runtime.GOOS).
func SendToKittyBypass(data []byte) error {
	f, window_id, err := open_bypass_pipe()
	if err != nil {
		return err
	}
	defer f.Close()
	var header [8]byte
	binary.LittleEndian.PutUint64(header[:], window_id)
	if _, err = f.Write(header[:]); err != nil {
		return err
	}
	_, err = f.Write(data)
	return err
}
