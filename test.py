#!./kitty/launcher/kitty +launch
# License: GPL v3 Copyright: 2016, Kovid Goyal <kovid at kovidgoyal.net>

import importlib


def bootstrap_windows() -> None:
    # The shebang above runs this through the kitty launcher, which does this
    # setup itself. Windows has no shebang, so the suite is run as
    # `python test.py` and needs the same bootstrap __main__.py performs, or it
    # fails on the first import of a Unix only stdlib module.
    import os
    import sys
    dll_dir = os.path.join(sys.base_prefix, 'bin')
    if os.path.isdir(dll_dir):
        os.add_dll_directory(dll_dir)  # ty: ignore[unresolved-attribute]
    stubs = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'kitty', 'wincompat', 'pystubs')
    if os.path.isdir(stubs):
        sys.path.insert(0, stubs)
    for fn in ('geteuid', 'getuid', 'getegid', 'getgid'):
        if not hasattr(os, fn):
            setattr(os, fn, lambda: 0)


def ensure_utf8_mode() -> None:
    # Same reason as setup.py: Windows defaults to a legacy code page for text
    # files, and the suite reads UTF-8 sources, so it dies while collecting the
    # Go packages before it runs anything. UTF-8 mode cannot be switched on once
    # the interpreter is up, so start again with it set.
    import os
    import subprocess
    import sys
    if sys.platform != 'win32' or sys.flags.utf8_mode:
        return
    env = dict(os.environ)
    env['PYTHONUTF8'] = '1'
    cmd = [sys.executable, '-X', 'utf8', os.path.abspath(__file__)] + sys.argv[1:]
    raise SystemExit(subprocess.run(cmd, env=env).returncode)


def main() -> None:
    import sys
    ensure_utf8_mode()
    if sys.platform == 'win32':
        bootstrap_windows()
    m = importlib.import_module('kitty_tests.main')
    getattr(m, 'main')()


if __name__ == '__main__':
    main()
