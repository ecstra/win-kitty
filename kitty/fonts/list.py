#!/usr/bin/env python
# License: GPL v3 Copyright: 2017, Kovid Goyal <kovid at kovidgoyal.net>

from collections.abc import Sequence

from kitty.constants import is_macos

from . import ListedFont
from .common import get_variable_data_for_descriptor

if is_macos:
    from .core_text import list_fonts, prune_family_group
else:
    from .fontconfig import list_fonts, prune_family_group


def create_family_groups(monospaced: bool = True) -> dict[str, list[ListedFont]]:
    g: dict[str, list[ListedFont]] = {}
    for f in list_fonts():
        if not monospaced or f['is_monospace']:
            g.setdefault(f['family'], []).append(f)
    return {k: prune_family_group(v) for k, v in g.items()}


def as_json(indent: int | None = None) -> str:
    import json
    groups = create_family_groups()
    for v in groups.values():
        for f in v:
            f['variable_data'] = get_variable_data_for_descriptor(f['descriptor'])  # type: ignore
    return json.dumps(groups, indent=indent)


def main(argv: Sequence[str]) -> None:
    import os

    from kitty.constants import is_windows, kitten_exe, kitty_exe
    argv = list(argv)
    psnames = '--psnames' in argv
    if psnames:
        argv.remove('--psnames')
    if is_windows:
        # kitty.exe is a GUI-subsystem binary: when run from another terminal it
        # has no console, so the choose-fonts TUI it would exec cannot set up
        # raw input and misbehaves. Print the actual font list instead; the
        # interactive UI remains available as `kitten choose-fonts`.
        for family, faces in sorted(create_family_groups().items(), key=lambda x: x[0].lower()):
            print(family)
            for f in faces:
                if psnames:
                    print('   ', f['full_name'], f'({f["postscript_name"]})')
                else:
                    print('   ', f['full_name'])
        return
    os.environ['KITTY_PATH_TO_KITTY_EXE'] = kitty_exe()
    os.execlp(kitten_exe(), 'kitten', 'choose-fonts')
