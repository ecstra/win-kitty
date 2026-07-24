#!/usr/bin/env python
# License: GPL v3 Copyright: 2018, Kovid Goyal <kovid at kovidgoyal.net>


from . import BaseTest, skip_on_windows


class TestTUI(BaseTest):

    def test_line_edit(self):
        from kittens.tui.line_edit import LineEdit
        le = LineEdit()
        le.on_text('abcd', False)
        self.ae(le.cursor_pos, 4)
        for i in range(5):
            self.assertTrue(le.left()) if i < 4 else self.assertFalse(le.left())
            self.ae(le.cursor_pos, max(0, 3 - i))
        self.ae(le.pending_bell, True)
        le.clear()
        le.on_text('abcd', False), le.home()
        self.ae(le.cursor_pos, 0)
        for i in range(5):
            self.assertTrue(le.right()) if i < 4 else self.assertFalse(le.right())
            self.ae(le.cursor_pos, min(4, i + 1))
        self.ae(le.pending_bell, True)
        le.clear()
        le.on_text('abcd', False)
        self.ae(le.current_input, 'abcd')
        self.ae(le.cursor_pos, 4)
        self.ae(le.split_at_cursor(), ('abcd', ''))
        le.backspace()
        self.ae(le.current_input, 'abc')
        self.ae(le.cursor_pos, 3)
        self.assertFalse(le.pending_bell)
        le.backspace(num=2)
        self.ae(le.current_input, 'a')
        self.ae(le.cursor_pos, 1)
        self.assertFalse(le.pending_bell)
        le.backspace(num=2)
        self.ae(le.current_input, '')
        self.ae(le.cursor_pos, 0)
        le.backspace()
        self.assertTrue(le.pending_bell)

    # kitty/multiprocessing.py patches multiprocessing.util.spawnv_passfds, which
    # is POSIX only. Windows spawns through popen_spawn_win32 instead, so the
    # patch never applies and the worker is started as bare kitty.exe. Making
    # this work needs a Windows equivalent of that patch, see WINDOWS_TODO.
    @skip_on_windows('multiprocessing spawn is patched through a POSIX only hook')
    def test_multiprocessing_spawn(self):
        from kitty.multiprocessing import test_spawn
        test_spawn()
