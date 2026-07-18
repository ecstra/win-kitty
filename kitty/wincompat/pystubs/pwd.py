# Minimal 'pwd' module for Windows. The real module is Unix only. kitty uses it
# for the current user's name and home directory.
import os
from collections import namedtuple

struct_passwd = namedtuple('struct_passwd', 'pw_name pw_passwd pw_uid pw_gid pw_gecos pw_dir pw_shell')


def _current() -> 'struct_passwd':
    name = os.environ.get('USERNAME') or 'user'
    home = os.environ.get('USERPROFILE') or 'C:\\'
    shell = os.environ.get('COMSPEC') or 'cmd.exe'
    return struct_passwd(name, 'x', 0, 0, name, home, shell)


def getpwuid(uid: int) -> 'struct_passwd':
    return _current()


def getpwnam(name: str) -> 'struct_passwd':
    return _current()


def getpwall() -> 'list[struct_passwd]':
    return [_current()]
