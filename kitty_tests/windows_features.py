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

from kitty.constants import kitty_base_dir, kitty_exe

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

    def test_images_load_from_paths_with_letters_outside_ascii(self):
        # Programs send image paths as UTF-8, which the C runtime's narrow open
        # read in the ANSI code page, opening a different name
        from .graphics import load_helpers

        s, g, pl, sl = load_helpers(self)
        data = os.urandom(32 * 1024)
        with tempfile.TemporaryDirectory() as tdir:
            folder = os.path.join(tdir, 'café ünï')
            os.mkdir(folder)
            path = os.path.join(folder, 'imágé.rgba')
            with open(path, 'wb') as f:
                f.write(data)
            sl(path, s=1024, v=8, t='f', expecting_data=data)

    def test_open_in_kitty_passes_the_folder_including_a_drive_root(self):
        # The C runtime reads a backslash before a closing quote as an escaped
        # quote, and a drive's root, as Explorer hands it over, ends in one
        import re

        shell32 = ctypes.WinDLL('shell32')
        shell32.CommandLineToArgvW.restype = ctypes.POINTER(ctypes.c_wchar_p)
        shell32.CommandLineToArgvW.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
        kernel32 = ctypes.WinDLL('kernel32')
        kernel32.LocalFree.argtypes = [ctypes.c_void_p]

        def argv_of(cmdline: str) -> list[str]:
            n = ctypes.c_int()
            p = shell32.CommandLineToArgvW(cmdline, ctypes.byref(n))
            try:
                return [p[i] for i in range(n.value)]
            finally:
                kernel32.LocalFree(p)

        with open(os.path.join(kitty_base_dir, 'packaging', 'windows', 'kitty.iss'), encoding='utf-8') as f:
            iss = f.read()
        verbs = re.findall(r'Subkey: "Software\\Classes\\([^"]+?)\\shell\\kitty\\command";.*?ValueData: "(.*?)"; Tasks', iss)
        self.ae(sorted(k for k, v in verbs), ['Directory', 'Directory\\Background', 'Drive'])
        for key, value in verbs:
            for folder in ('D:\\', r'C:\Users\me', r'C:\Program Files\some app'):
                cmdline = value.replace('""', '"').replace('{app}', r'C:\Program Files\kitty').replace('%V', folder)
                argv = argv_of(cmdline)
                self.ae(argv[-2], '--directory', cmdline)
                self.ae(os.path.normcase(os.path.abspath(argv[-1])), os.path.normcase(folder), f'{key}: {cmdline}')

    def test_swapping_stderr_moves_the_handle_programs_inherit(self):
        # kitty.exe is a GUI program, whose C runtime leaves the process's
        # standard handles alone on dup2, so a program started after a test
        # swapped stderr and back inherited a closed handle
        import msvcrt

        kernel32 = ctypes.WinDLL('kernel32')
        kernel32.GetStdHandle.restype = ctypes.c_void_p
        with tempfile.TemporaryFile() as f:
            orig = os.dup(2)
            os.dup2(f.fileno(), 2)
            try:
                self.ae(kernel32.GetStdHandle(-12), msvcrt.get_osfhandle(2))
            finally:
                os.dup2(orig, 2)
                os.close(orig)
        self.ae(kernel32.GetStdHandle(-12), msvcrt.get_osfhandle(2))

    def test_saved_sessions_keep_the_windows_folder_of_an_msys2_shell(self):
        # An MSYS2 shell reports its folder at the prompt as a POSIX path, which
        # no Windows program can start in
        from types import SimpleNamespace

        from kitty.window import Window

        window = SimpleNamespace(
            get_cwd_of_child=lambda oldest=False: r'C:\Users\me\proj',
            screen=SimpleNamespace(last_reported_cwd=f'file://{socket.gethostname()}/home/me/proj'.encode()),
            at_prompt=True,
            child_is_remote=False,
            child=SimpleNamespace(cwd='C:\\'),
        )
        self.ae(Window.cwd_for_serialization.fget(window), r'C:\Users\me\proj')

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

    def test_the_quick_access_hotkey_parses(self):
        from kittens.panel.main import hotkey_from_spec
        from kitty.fast_data_types import GLFW_FKEY_F12, GLFW_MOD_ALT, GLFW_MOD_CONTROL, GLFW_MOD_SUPER

        self.ae(hotkey_from_spec('ctrl+alt+grave'), (ord('`'), GLFW_MOD_CONTROL | GLFW_MOD_ALT))
        self.ae(hotkey_from_spec('super+f12'), (GLFW_FKEY_F12, GLFW_MOD_SUPER))
        self.ae(hotkey_from_spec('ctrl+alt+q'), (ord('q'), GLFW_MOD_CONTROL | GLFW_MOD_ALT))
        for off in ('', 'none'):
            self.assertIsNone(hotkey_from_spec(off))
        for bad in ('ctrl+', 'hyperspace+x'):
            with self.assertRaises(ValueError):
                hotkey_from_spec(bad)

    def test_kittens_refuse_to_start_in_other_terminals(self):
        # A console of its own, hidden, stands in for Windows Terminal's
        from kitty.constants import kitten_exe

        env = {k: v for k, v in os.environ.items() if k not in ('KITTY_WINDOW_ID', 'KITTY_PID', 'KITTY_LISTEN_ON')}
        p = subprocess.Popen(
            [kitten_exe(), 'unicode_input'], env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            _, err = p.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            p.kill()
            _, err = p.communicate()
        self.assertEqual(p.returncode, 1)
        self.assertIn(b'needs to run inside the kitty terminal', err)

    def test_the_quick_access_terminal_shows_on_ctrl_alt_grave_by_default(self):
        from kittens.panel.main import hotkey_from_spec, parse_panel_args
        from kittens.quick_access_terminal.main import definition

        self.ae(definition.option_map['hotkey'].defval_as_string, 'ctrl+alt+grave')
        args, _ = parse_panel_args(['--hotkey=ctrl+alt+grave'])
        self.ae(args.hotkey, 'ctrl+alt+grave')
        args, _ = parse_panel_args([])
        self.assertIsNone(hotkey_from_spec(args.hotkey))
