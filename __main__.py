#!/usr/bin/env python
# License: GPL v3 Copyright: 2015, Kovid Goyal <kovid at kovidgoyal.net>


if __name__ == '__main__':
    import sys
    if sys.platform == 'win32':
        import os
        # The C extension depends on mingw runtime DLLs (cairo, harfbuzz, ...) that
        # live in the Python prefix's bin dir. Python 3.8+ does not search PATH for
        # extension dependencies, so register that directory with the loader.
        dll_dir = os.path.join(sys.base_prefix, 'bin')
        if os.path.isdir(dll_dir):
            os.add_dll_directory(dll_dir)
        # Python stubs for the Unix only stdlib modules kitty imports (pwd, ...).
        stubs = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'kitty', 'wincompat', 'pystubs')
        if os.path.isdir(stubs):
            sys.path.insert(0, stubs)
        # Unix-only os credential functions kitty calls; single interactive user.
        for _fn in ('geteuid', 'getuid', 'getegid', 'getgid'):
            if not hasattr(os, _fn):
                setattr(os, _fn, lambda: 0)
    from kitty.entry_points import main
    main()
