#!/usr/bin/env python
# License: GPL v3 Copyright: 2026, Kovid Goyal <kovid at kovidgoyal.net>

import ctypes
import io
import os
import socket
import subprocess
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest.mock import patch

from kitty.constants import kitty_exe

from .base import BaseTest, is_windows

only_on_windows = unittest.skipUnless(is_windows, 'checks features that were missing on Windows')
AF_UNIX = 1  # Winsock's, which CPython does not define on Windows


class FakeAtexit:

    def __init__(self) -> None:
        self.unlinked: list[str] = []

    def unlink(self, path: str) -> None:
        self.unlinked.append(path)


class sockaddr_un(ctypes.Structure):
    _fields_ = [('sun_family', ctypes.c_ushort), ('sun_path', ctypes.c_char * 108)]


def winsock() -> ctypes.WinDLL:
    socket.socket().close()  # Python's socket module has started Winsock
    ws2 = ctypes.WinDLL('ws2_32')
    ws2.socket.restype = ctypes.c_size_t
    ws2.connect.argtypes = [ctypes.c_size_t, ctypes.c_void_p, ctypes.c_int]
    ws2.closesocket.argtypes = [ctypes.c_size_t]
    return ws2


def connects_over_unix_socket(path: str) -> bool:
    # CPython cannot make an AF_UNIX socket on Windows, so the client is
    # Winsock's own, as kitten @ dials it
    ws2 = winsock()
    s = ws2.socket(AF_UNIX, socket.SOCK_STREAM, 0)
    addr = sockaddr_un(AF_UNIX, os.fsencode(path))
    try:
        return ws2.connect(s, ctypes.byref(addr), ctypes.sizeof(addr)) == 0
    finally:
        ws2.closesocket(s)


@only_on_windows
class WindowsFeaturesTest(BaseTest):

    def test_listen_on_unix_socket(self):
        from kitty.boss import listen_on
        with tempfile.TemporaryDirectory() as tdir:
            path = os.path.join(tdir, 'kitty.sock')
            atexit = FakeAtexit()
            fd, spec = listen_on(f'unix:{path}', atexit)  # type: ignore[arg-type]
            try:
                self.assertEqual(spec, f'unix:{path}')
                self.assertEqual(atexit.unlinked, [path])
                self.assertTrue(os.path.exists(path))
                self.assertTrue(connects_over_unix_socket(path))
            finally:
                winsock().closesocket(fd)
                os.unlink(path)

    def test_abstract_unix_socket_says_why(self):
        from kitty.boss import listen_on
        with self.assertRaises(ValueError) as cm:
            listen_on('unix:@kitty', FakeAtexit())  # type: ignore[arg-type]
        self.assertIn('abstract', str(cm.exception).lower())

    def test_dragged_file_links_name_windows_paths(self):
        from kitty.utils import windows_file_url
        host = socket.gethostname()
        for url, expected in (
            ('file:///C:/Users/me/a%20b.txt', 'file:///C:/Users/me/a%20b.txt'),
            ('file://localhost/D:/x.txt', 'file:///D:/x.txt'),
            (f'file://{host}/c/Users/me/x.txt', 'file:///C:/Users/me/x.txt'),
            (f'file://{host.upper()}/c/Users/me', 'file:///C:/Users/me'),
            ('file:///cygdrive/d/work/notes.md', 'file:///D:/work/notes.md'),
            ('file:///c', 'file:///C:/'),
            ('file://another-machine/C:/x.txt', 'file://another-machine/C:/x.txt'),
            ('file:///home/me/x.txt', 'file:///home/me/x.txt'),
            ('https://example.com/a%20b', 'https://example.com/a%20b'),
        ):
            with self.subTest(url=url):
                self.assertEqual(windows_file_url(url), expected)

    def test_help_from_another_terminal_lists_every_option(self):
        exe = os.path.join(os.path.dirname(kitty_exe()), 'kitty-console.exe')
        p = subprocess.run([exe, '--help'], capture_output=True, text=True, timeout=60, stdin=subprocess.DEVNULL)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn('--listen-on', p.stdout)
        self.assertNotIn('\x1b', p.stdout)

    def test_help_in_a_console_prints_without_a_pager(self):
        from kitty.cli import parse_args

        class Console(io.StringIO):
            def isatty(self) -> bool:
                return True

            def fileno(self) -> int:
                return 1

        out = Console()
        with redirect_stdout(out), patch('subprocess.Popen', side_effect=AssertionError('Windows has no pager for the help')):
            with self.assertRaises(SystemExit):
                parse_args(['--help'])
        self.assertIn('--listen-on', out.getvalue())
