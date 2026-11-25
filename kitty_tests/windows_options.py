#!/usr/bin/env python
# License: GPL v3 Copyright: 2026, Kovid Goyal <kovid at kovidgoyal.net>

import os
import tempfile
import time
import unittest
from unittest.mock import patch

from kitty.config import load_config

from .base import BaseTest, is_windows

only_on_windows = unittest.skipUnless(is_windows, 'checks how kitty.conf options behave on Windows')


@only_on_windows
class WindowsOptionsTest(BaseTest):

    def test_kitty_os_includes_the_windows_file(self):
        with tempfile.TemporaryDirectory() as tdir:
            with open(os.path.join(tdir, 'windows.conf'), 'w') as f:
                f.write('font_size 23\n')
            conf = os.path.join(tdir, 'kitty.conf')
            with open(conf, 'w') as f:
                f.write('include ${KITTY_OS}.conf\n')
            self.assertEqual(load_config(conf).font_size, 23)

    def test_editor_falls_back_to_notepad(self):
        from kitty import utils
        with patch.object(utils, 'which', return_value=None), patch.object(utils, 'get_editor_from_env', return_value=''), \
                patch.object(utils, 'read_shell_environment', return_value={}), patch.object(utils, 'editor_next_to_shell', return_value=''):
            self.assertEqual(utils.get_editor_from_env_vars(), ['notepad.exe'])

    def test_background_process_starts_and_reads_its_input(self):
        from kitty.boss import Boss

        class FakeBoss:
            def __init__(self) -> None:
                self.errors: list[str] = []
                self.background_process_death_notify_map: dict[int, object] = {}

            def show_error(self, title: str, msg: str) -> None:
                self.errors.append(msg)

        boss = FakeBoss()
        # sys.executable is kitty itself under the test runner, so Windows' own
        # sort, which copies a single line through, stands in as the program
        sort = os.path.join(os.environ['SystemRoot'], 'System32', 'sort.exe')
        with tempfile.TemporaryDirectory() as tdir:
            out = os.path.join(tdir, 'out.txt')
            fd = os.open(out, os.O_WRONLY | os.O_CREAT)
            try:
                Boss.run_background_process(boss, [sort], stdin=b'from kitty\n', stdout=fd)  # type: ignore[arg-type]
            finally:
                os.close(fd)
            deadline = time.monotonic() + 10
            got = ''
            while time.monotonic() < deadline and not boss.errors and not got:
                with open(out) as f:
                    got = f.read().strip()
                time.sleep(0.05)
            self.assertEqual(boss.errors, [])
            self.assertEqual(got, 'from kitty')
