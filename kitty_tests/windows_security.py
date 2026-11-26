#!/usr/bin/env python
# License: GPL v3 Copyright: 2026, Kovid Goyal <kovid at kovidgoyal.net>

import os
import tempfile
import unittest
from unittest.mock import patch

from .base import BaseTest, is_windows

only_on_windows = unittest.skipUnless(is_windows, 'checks Windows specific security')

# Paths a program in a kitty window could name to make Windows contact another
# machine, sending it the user's login hash, or open a device
NETWORK_AND_DEVICE_PATHS = (
    r'\\attacker\share\x.png',
    '//attacker/share/x.png',
    r'\\?\UNC\attacker\share\x.png',
    r'\\.\pipe\x',
    r'\??\UNC\attacker\share\x.png',
    '/??/UNC/attacker/share/x.png',
    r'\\attacker\share\tty-graphics-protocol-x.png',
    'COM1',
    'com1:',
    'LPT1',
    'PRN',
    'AUX',
    'CON',
    'CONIN$',
)


def touches_the_filesystem(*args: object, **kw: object) -> None:
    raise AssertionError(f'touched the filesystem for {args}')


@only_on_windows
class WindowsSecurityTest(BaseTest):

    def test_image_paths_on_another_machine_are_refused_untouched(self):
        from kitty.utils import is_ok_to_read_image_path, is_path_in_temp_dir
        for path in NETWORK_AND_DEVICE_PATHS:
            with self.subTest(path=path), patch('os.path.realpath', touches_the_filesystem), patch('os.stat', touches_the_filesystem):
                self.assertFalse(is_ok_to_read_image_path(path))
                self.assertFalse(is_path_in_temp_dir(path))

    def test_temporary_image_cleanup_deletes_nothing_but_its_own_files(self):
        from kitty.utils import may_delete_temp_graphics_file
        temp = tempfile.gettempdir()
        drive = os.path.splitdrive(temp)[0] or 'C:'
        for path in (
            os.path.join(temp, 'tty-graphics-protocol', '..', 'report.docx'),
            os.path.join(temp, 'missing', '..', '..', 'tty-graphics-protocol-x.png'),
            drive + r'\tmp-projects\tty-graphics-protocol-x.png',
            temp + '-other' + r'\tty-graphics-protocol-x.png',
        ):
            with self.subTest(path=path):
                self.assertFalse(may_delete_temp_graphics_file(path))
        self.assertTrue(may_delete_temp_graphics_file(os.path.join(temp, 'sub', 'tty-graphics-protocol-x.png')))

    def test_local_image_paths_are_still_read(self):
        from kitty.utils import is_ok_to_read_image_path, is_path_in_temp_dir
        path = os.path.join(tempfile.gettempdir(), 'tty-graphics-protocol-x.png')
        self.assertTrue(is_ok_to_read_image_path(path))
        self.assertTrue(is_path_in_temp_dir(path))
        self.assertTrue(is_ok_to_read_image_path(os.path.join(os.environ['SystemRoot'], 'System32', 'x.png')))
