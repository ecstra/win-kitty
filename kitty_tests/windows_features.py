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
        # A shell's runtime knows where its own paths lie, /tmp on the temp
        # folder for Git Bash among them, so the mapping is the runtime's
        mounts = {'/home/me/notes.txt': 'C:\\msys64\\home\\me\\notes.txt', '/tmp/a b': 'C:\\Users\\me\\AppData\\Local\\Temp\\a b'}

        def windows_path_of(path: str) -> str:
            return mounts.get(path, '')

        for url, expected in (
            (f'file://{host}/home/me/notes.txt', 'file:///C:/msys64/home/me/notes.txt'),
            ('file:///tmp/a%20b', 'file:///C:/Users/me/AppData/Local/Temp/a%20b'),
            ('file:///etc/unknown', 'file:///etc/unknown'),
            ('file:///mnt/c/Users/me/x.txt', 'file:///C:/Users/me/x.txt'),
            ('file:///c/Users/me', 'file:///C:/Users/me'),
        ):
            with self.subTest(url=url):
                self.assertEqual(windows_file_url(url, windows_path_of), expected)

    def test_posix_paths_come_from_the_shells_own_runtime(self):
        import shutil
        import tempfile

        from kitty.utils import posix_path_converter
        python = shutil.which('python')
        bash = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(python))), 'usr', 'bin', 'bash.exe') if python else ''
        convert = posix_path_converter(bash) if bash and os.path.exists(bash) else None
        if convert is None:
            self.skipTest('no MSYS2 bash beside the Python running the tests')
        self.assertIsNone(posix_path_converter(os.path.join(os.environ['SystemRoot'], 'System32', 'cmd.exe')))
        root = os.path.dirname(os.path.dirname(os.path.dirname(bash)))
        self.assertEqual(os.path.normcase(convert('/usr/bin')), os.path.normcase(os.path.join(root, 'usr', 'bin')))
        self.assertIn(os.path.normcase(convert('/tmp')), (os.path.normcase(os.path.join(root, 'tmp')), os.path.normcase(tempfile.gettempdir())))
        # A shell started through msys2_shell.cmd, in the MSYS2 folder itself
        through_script = posix_path_converter(os.path.join(root, 'msys2_shell.cmd'))
        self.assertIsNotNone(through_script)
        assert through_script is not None
        self.assertEqual(os.path.normcase(through_script('/usr/bin')), os.path.normcase(os.path.join(root, 'usr', 'bin')))

    def test_listen_on_tmp_lands_in_the_temp_folder(self):
        from kitty.main import expand_listen_on
        # The user's own temp folder, whatever TMP an MSYS2 shell set, as kitten takes it
        with tempfile.TemporaryDirectory() as tdir, patch.dict(os.environ, {'LOCALAPPDATA': tdir, 'TMP': os.path.join(tdir, 'msys64', 'tmp')}):
            self.assertEqual(expand_listen_on('unix:/tmp/mykitty', False, {}), f'unix:{os.path.join(tdir, "Temp", "mykitty")}')

    def test_listen_on_a_missing_folder_says_so(self):
        from kitty.boss import listen_on
        with tempfile.TemporaryDirectory() as tdir:
            path = os.path.join(tdir, 'missing', 'kitty.sock')
            with self.assertRaises(ValueError) as cm:
                listen_on(f'unix:{path}', FakeAtexit())  # type: ignore[arg-type]
            self.assertIn('does not exist', str(cm.exception))

    def test_plus_commands_handing_off_to_kitten_keep_its_exit_code(self):
        exe = os.path.join(os.path.dirname(kitty_exe()), 'kitty-console.exe')

        def run(*args: str) -> subprocess.CompletedProcess[str]:
            return subprocess.run([exe] + list(args), capture_output=True, text=True, timeout=60, stdin=subprocess.DEVNULL)

        p = run('+shebang', 'C:/nope/missing.py')
        self.assertEqual(p.returncode, 1, p.stderr)
        p = run('+kitten', 'clipboard', '--no such option')
        self.assertEqual(p.returncode, 1, p.stderr)
        self.assertIn('--no such option', p.stderr)

    def test_plus_commands_handing_off_to_kitten_keep_accented_letters(self):
        try:
            'é'.encode('mbcs')
        except UnicodeEncodeError:
            self.skipTest('the ANSI code page here has no é')
        exe = os.path.join(os.path.dirname(kitty_exe()), 'kitty-console.exe')
        p = subprocess.run([exe, '+kitten', 'clipboard', '--Café'], capture_output=True, encoding='utf-8', errors='replace',
                           timeout=60, stdin=subprocess.DEVNULL)
        self.assertIn('--Café', p.stderr)

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

    def test_program_names_drop_the_exe(self):
        # Tab titles show nvim, and the ssh checks know ssh.exe as ssh
        from kittens.ssh.utils import is_kitten_cmdline
        from kitty.utils import program_name

        for path, name in (
            (r'C:\Program Files\Neovim\bin\nvim.exe', 'nvim'),
            ('nvim', 'nvim'),
            (r'C:\Windows\System32\OpenSSH\ssh.EXE', 'ssh'),
            ('archive.exe.old', 'archive.exe.old'),
        ):
            self.ae(program_name(path), name)
        self.assertTrue(is_kitten_cmdline([r'C:\kitty\bin\kitten.exe', 'ssh', 'host']))
