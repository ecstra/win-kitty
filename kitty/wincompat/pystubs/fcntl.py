# Minimal 'fcntl' module for Windows. The real module is Unix only. kitty uses
# it to set fd flags (cloexec, non-blocking) and advisory locks; on Windows those
# are handled elsewhere or do not apply, so these are no-ops.

F_DUPFD = 0
F_GETFD = 1
F_SETFD = 2
F_GETFL = 3
F_SETFL = 4
F_GETLK = 5
F_SETLK = 6
F_SETLKW = 7
FD_CLOEXEC = 1

LOCK_SH = 1
LOCK_EX = 2
LOCK_NB = 4
LOCK_UN = 8


def fcntl(fd: int, cmd: int, arg: int = 0) -> int:
    return 0


def ioctl(fd: int, request: int, arg: object = 0, mutate_flag: bool = True) -> int:
    return 0


def flock(fd: int, operation: int) -> None:
    return None


def lockf(fd: int, cmd: int, length: int = 0, start: int = 0, whence: int = 0) -> None:
    return None
