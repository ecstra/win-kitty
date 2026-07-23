#!/usr/bin/env python
# License: GPL v3 Copyright: 2026, Kovid Goyal <kovid at kovidgoyal.net>

"""
Windows I/O backend for the kitten TUI loop.

On Unix a kitten opens /dev/tty and drives it with termios raw mode. Windows has
no controlling-terminal device and no termios. kitty spawns a kitten with plain
pipes as its standard handles (see child.c open_pty in pipe mode), so the kitten
just reads its stdin handle and writes its stdout handle: raw bytes in and out,
with no console and no line discipline in the way. kitty is the terminal on the
other end of those pipes, so the kitten's escape codes reach it untouched (the
synchronized-update sequence survives, so redraws do not flicker) and kitty sees
EOF the moment the kitten exits.
"""

import ctypes
import os
from ctypes import wintypes

from kitty.utils import ScreenSize

# tcsetattr optional-action selectors. The shared loop threads these through its
# signatures; on Windows they are inert, so mirror the wincompat/termios.h values.
TCSANOW, TCSADRAIN, TCSAFLUSH = 0, 1, 2

kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)  # ty: ignore[unresolved-attribute]

STD_INPUT_HANDLE = 0xFFFFFFF6   # (DWORD)-10
STD_OUTPUT_HANDLE = 0xFFFFFFF5  # (DWORD)-11
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
THREAD_TERMINATE = 0x0001

# Cells have no pixel size over a pipe. The kittens only need rows and columns; a
# nominal cell size keeps the pixel fields of ScreenSize sane for the rare
# mouse-pixel calculation.
DEFAULT_CELL_WIDTH = 8
DEFAULT_CELL_HEIGHT = 16
READ_BUF_SIZE = 8192

kernel32.GetStdHandle.restype = wintypes.HANDLE
kernel32.GetStdHandle.argtypes = (wintypes.DWORD,)
kernel32.ReadFile.restype = wintypes.BOOL
kernel32.ReadFile.argtypes = (wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p)
kernel32.WriteFile.restype = wintypes.BOOL
kernel32.WriteFile.argtypes = (wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p)
kernel32.OpenThread.restype = wintypes.HANDLE
kernel32.OpenThread.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
kernel32.CancelSynchronousIo.restype = wintypes.BOOL
kernel32.CancelSynchronousIo.argtypes = (wintypes.HANDLE,)
kernel32.CloseHandle.restype = wintypes.BOOL
kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)


def cancel_console_read(thread: object) -> None:
    # Cancel the read the reader thread is blocked in, so the kitten can exit
    # promptly instead of hanging on shutdown. CancelSynchronousIo needs a thread
    # handle with THREAD_TERMINATE rights.
    tid = getattr(thread, 'native_id', None)
    if not tid:
        return
    h = kernel32.OpenThread(THREAD_TERMINATE, False, tid)
    if h:
        kernel32.CancelSynchronousIo(h)
        kernel32.CloseHandle(h)


class WinConsole:
    """The stdin/stdout pipes a kitten reads keys from and draws to."""

    def __init__(self) -> None:
        self.hin = kernel32.GetStdHandle(STD_INPUT_HANDLE)
        self.hout = kernel32.GetStdHandle(STD_OUTPUT_HANDLE)
        self._read_buf = ctypes.create_string_buffer(READ_BUF_SIZE)

    def set_raw(self) -> None:
        pass  # pipes are already raw; nothing to configure

    def restore_modes(self) -> None:
        pass

    def close(self) -> None:
        pass  # the std handles belong to the process; do not close them

    def read(self, n: int = READ_BUF_SIZE) -> bytes | None:
        # Blocking read. Returns the bytes read, or None when the read failed
        # (pipe closed, or cancelled on shutdown) so the caller can stop.
        if n > READ_BUF_SIZE:
            n = READ_BUF_SIZE
        nread = wintypes.DWORD(0)
        ok = kernel32.ReadFile(self.hin, self._read_buf, n, ctypes.byref(nread), None)
        if not ok:
            return None
        return self._read_buf.raw[:nread.value]

    def write(self, data: bytes | str) -> None:
        if isinstance(data, str):
            data = data.encode('utf-8')
        off, total = 0, len(data)
        while off < total:
            chunk = data[off:]
            buf = (ctypes.c_char * len(chunk)).from_buffer_copy(chunk)
            nwritten = wintypes.DWORD(0)
            if not kernel32.WriteFile(self.hout, buf, len(chunk), ctypes.byref(nwritten), None):
                raise ctypes.WinError(ctypes.get_last_error())  # ty: ignore[unresolved-attribute]
            if not nwritten.value:
                break
            off += nwritten.value

    def read_size(self) -> ScreenSize:
        # There is no console to query over a pipe; kitty passes the overlay size
        # in the environment when it launches the kitten.
        def env_int(name: str, default: int) -> int:
            try:
                return max(1, int(os.environ.get(name, default)))
            except Exception:
                return default
        rows = env_int('OVERLAID_WINDOW_LINES', 24)
        cols = env_int('OVERLAID_WINDOW_COLS', 80)
        cw, ch = DEFAULT_CELL_WIDTH, DEFAULT_CELL_HEIGHT
        return ScreenSize(rows, cols, cols * cw, rows * ch, cw, ch)


class WinScreenSizeGetter:
    """Stand-in for utils.ScreenSizeGetter that reports the size kitty passed in."""

    changed = True

    def __init__(self, console: WinConsole) -> None:
        self.console = console

    def __call__(self) -> ScreenSize:
        return self.console.read_size()
