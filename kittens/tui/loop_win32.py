#!/usr/bin/env python
# License: GPL v3 Copyright: 2026, Kovid Goyal <kovid at kovidgoyal.net>

"""
Windows console backend for the kitten TUI loop.

On Unix a kitten opens /dev/tty and drives it with termios raw mode. Windows has
no controlling-terminal device and no termios, so this module talks to the
console directly. A kitten launched by kitty runs inside a pseudoconsole
(ConPTY), so its CONIN$ and CONOUT$ are that pseudoconsole. Reading CONIN$ with
ENABLE_VIRTUAL_TERMINAL_INPUT yields the same escape sequences the Unix side
already parses, and writing CONOUT$ with ENABLE_VIRTUAL_TERMINAL_PROCESSING
sends escape sequences straight through. The bytes go through ReadFile and
WriteFile rather than the C runtime, so its cooked-console handling never
rewrites them.

CONIN$ matters even when the kitten has data on stdin: kitty feeds a kitten its
input (the screen text, history, and so on) through stdin while the UI keys
arrive on the console, exactly as /dev/tty separates the two on Unix.
"""

import ctypes
from ctypes import wintypes

from kitty.utils import ScreenSize

# tcsetattr optional-action selectors. The shared loop threads these through its
# signatures; on Windows they are inert, so mirror the wincompat/termios.h values.
TCSANOW, TCSADRAIN, TCSAFLUSH = 0, 1, 2

kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)

GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
FILE_SHARE_READ = 0x00000001
FILE_SHARE_WRITE = 0x00000002
OPEN_EXISTING = 3
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value

# Console input modes.
ENABLE_PROCESSED_INPUT = 0x0001
ENABLE_LINE_INPUT = 0x0002
ENABLE_ECHO_INPUT = 0x0004
ENABLE_MOUSE_INPUT = 0x0010
ENABLE_QUICK_EDIT_MODE = 0x0040
ENABLE_EXTENDED_FLAGS = 0x0080
ENABLE_VIRTUAL_TERMINAL_INPUT = 0x0200
# Console output modes.
ENABLE_PROCESSED_OUTPUT = 0x0001
ENABLE_VIRTUAL_TERMINAL_PROCESSING = 0x0004
DISABLE_NEWLINE_AUTO_RETURN = 0x0008

CP_UTF8 = 65001

# Cells have no pixel size in the console API. The kittens only need rows and
# columns; a nominal cell size keeps the pixel fields of ScreenSize sane for the
# rare mouse-pixel calculation.
DEFAULT_CELL_WIDTH = 8
DEFAULT_CELL_HEIGHT = 16

READ_BUF_SIZE = 8192


class COORD(ctypes.Structure):
    _fields_ = (('X', ctypes.c_short), ('Y', ctypes.c_short))


class SMALL_RECT(ctypes.Structure):
    _fields_ = (('Left', ctypes.c_short), ('Top', ctypes.c_short), ('Right', ctypes.c_short), ('Bottom', ctypes.c_short))


class CONSOLE_SCREEN_BUFFER_INFO(ctypes.Structure):
    _fields_ = (
        ('dwSize', COORD), ('dwCursorPosition', COORD), ('wAttributes', ctypes.c_ushort),
        ('srWindow', SMALL_RECT), ('dwMaximumWindowSize', COORD),
    )


kernel32.CreateFileW.restype = wintypes.HANDLE
kernel32.CreateFileW.argtypes = (
    wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
kernel32.GetConsoleMode.restype = wintypes.BOOL
kernel32.GetConsoleMode.argtypes = (wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD))
kernel32.SetConsoleMode.restype = wintypes.BOOL
kernel32.SetConsoleMode.argtypes = (wintypes.HANDLE, wintypes.DWORD)
kernel32.GetConsoleScreenBufferInfo.restype = wintypes.BOOL
kernel32.GetConsoleScreenBufferInfo.argtypes = (wintypes.HANDLE, ctypes.POINTER(CONSOLE_SCREEN_BUFFER_INFO))
kernel32.ReadFile.restype = wintypes.BOOL
kernel32.ReadFile.argtypes = (wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p)
kernel32.WriteFile.restype = wintypes.BOOL
kernel32.WriteFile.argtypes = (wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p)
kernel32.CloseHandle.restype = wintypes.BOOL
kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
kernel32.GetConsoleCP.restype = wintypes.UINT
kernel32.GetConsoleOutputCP.restype = wintypes.UINT
kernel32.SetConsoleCP.restype = wintypes.BOOL
kernel32.SetConsoleCP.argtypes = (wintypes.UINT,)
kernel32.SetConsoleOutputCP.restype = wintypes.BOOL
kernel32.SetConsoleOutputCP.argtypes = (wintypes.UINT,)
kernel32.OpenThread.restype = wintypes.HANDLE
kernel32.OpenThread.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
kernel32.CancelSynchronousIo.restype = wintypes.BOOL
kernel32.CancelSynchronousIo.argtypes = (wintypes.HANDLE,)

THREAD_TERMINATE = 0x0001


def cancel_console_read(thread: object) -> None:
    # Cancel a console ReadFile the reader thread is blocked in, so the kitten can
    # exit promptly instead of hanging on shutdown. CancelSynchronousIo needs a
    # thread handle with THREAD_TERMINATE rights.
    tid = getattr(thread, 'native_id', None)
    if not tid:
        return
    h = kernel32.OpenThread(THREAD_TERMINATE, False, tid)
    if h:
        kernel32.CancelSynchronousIo(h)
        kernel32.CloseHandle(h)


def _open_handle(name: str, access: int) -> int:
    h = kernel32.CreateFileW(name, access, FILE_SHARE_READ | FILE_SHARE_WRITE, None, OPEN_EXISTING, 0, None)
    if not h or h == INVALID_HANDLE_VALUE:
        raise ctypes.WinError(ctypes.get_last_error())
    return h


def _get_mode(handle: int) -> int:
    mode = wintypes.DWORD()
    if not kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
        raise ctypes.WinError(ctypes.get_last_error())
    return mode.value


class WinConsole:
    """The console (ConPTY) a kitten draws in, in raw mode."""

    def __init__(self) -> None:
        self.hin = _open_handle('CONIN$', GENERIC_READ | GENERIC_WRITE)
        self.hout = _open_handle('CONOUT$', GENERIC_READ | GENERIC_WRITE)
        self.saved_in_mode = _get_mode(self.hin)
        self.saved_out_mode = _get_mode(self.hout)
        self.saved_in_cp = kernel32.GetConsoleCP()
        self.saved_out_cp = kernel32.GetConsoleOutputCP()
        self._read_buf = ctypes.create_string_buffer(READ_BUF_SIZE)
        self.set_raw()

    def set_raw(self) -> None:
        kernel32.SetConsoleCP(CP_UTF8)
        kernel32.SetConsoleOutputCP(CP_UTF8)
        # Extended flags without quick-edit stops a click-drag from selecting
        # text instead of reaching the kitten. No processed input means ctrl-c
        # arrives as the byte 0x03 for the loop to handle, not as a signal.
        kernel32.SetConsoleMode(self.hin, ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_EXTENDED_FLAGS)
        kernel32.SetConsoleMode(self.hout, ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN)

    def restore_modes(self) -> None:
        kernel32.SetConsoleMode(self.hin, self.saved_in_mode)
        kernel32.SetConsoleMode(self.hout, self.saved_out_mode)
        kernel32.SetConsoleCP(self.saved_in_cp)
        kernel32.SetConsoleOutputCP(self.saved_out_cp)

    def close(self) -> None:
        for h in (self.hin, self.hout):
            if h and h != INVALID_HANDLE_VALUE:
                kernel32.CloseHandle(h)
        self.hin = self.hout = INVALID_HANDLE_VALUE

    def read(self, n: int = READ_BUF_SIZE) -> bytes | None:
        # Blocking read. Returns the bytes read, or None when the read failed
        # (handle closed, or cancelled on shutdown) so the caller can stop.
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
                raise ctypes.WinError(ctypes.get_last_error())
            if not nwritten.value:
                break
            off += nwritten.value

    def read_size(self) -> ScreenSize:
        info = CONSOLE_SCREEN_BUFFER_INFO()
        if kernel32.GetConsoleScreenBufferInfo(self.hout, ctypes.byref(info)):
            cols = info.srWindow.Right - info.srWindow.Left + 1
            rows = info.srWindow.Bottom - info.srWindow.Top + 1
        else:
            cols, rows = 80, 24
        cw, ch = DEFAULT_CELL_WIDTH, DEFAULT_CELL_HEIGHT
        return ScreenSize(rows, cols, cols * cw, rows * ch, cw, ch)


class WinScreenSizeGetter:
    """Stand-in for utils.ScreenSizeGetter that reads the console instead of ioctl."""

    changed = True

    def __init__(self, console: WinConsole) -> None:
        self.console = console

    def __call__(self) -> ScreenSize:
        return self.console.read_size()
