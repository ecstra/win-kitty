# Minimal 'fcntl' module for Windows. The real module is Unix only. kitty uses
# it to set fd flags (cloexec, non-blocking), which on Windows are handled
# elsewhere or do not apply, so those are no-ops, and for advisory whole-file
# locks, which are real locks here, taken with LockFileEx.

import ctypes
import errno
import msvcrt
from ctypes import wintypes

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


class _Overlapped(ctypes.Structure):
    _fields_ = [
        ('Internal', ctypes.c_void_p),
        ('InternalHigh', ctypes.c_void_p),
        ('Offset', wintypes.DWORD),
        ('OffsetHigh', wintypes.DWORD),
        ('hEvent', wintypes.HANDLE),
    ]


_kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
_LockFileEx = _kernel32.LockFileEx
_LockFileEx.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.POINTER(_Overlapped)]
_LockFileEx.restype = wintypes.BOOL
_UnlockFileEx = _kernel32.UnlockFileEx
_UnlockFileEx.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.POINTER(_Overlapped)]
_UnlockFileEx.restype = wintypes.BOOL

_LOCKFILE_FAIL_IMMEDIATELY = 1
_LOCKFILE_EXCLUSIVE_LOCK = 2
_ERROR_NOT_LOCKED = 158
_ERROR_LOCK_VIOLATION = 33
_ERROR_IO_PENDING = 997


def _lock_whole_file(fd: 'int | object', operation: int) -> None:
    # Locks cover the whole file and belong to the handle, like a flock() lock
    # belongs to the open file description. Unlike flock(), asking again for a
    # different kind of lock on the same handle does not convert the one held.
    if not isinstance(fd, int):
        fd = fd.fileno()  # ty: ignore[unresolved-attribute]
    handle = msvcrt.get_osfhandle(fd)  # ty: ignore[unresolved-attribute]
    overlapped = _Overlapped()
    if operation & LOCK_UN:
        if not _UnlockFileEx(handle, 0, 0xFFFFFFFF, 0xFFFFFFFF, ctypes.byref(overlapped)):
            err = ctypes.get_last_error()
            if err != _ERROR_NOT_LOCKED:
                raise ctypes.WinError(err)  # ty: ignore[unresolved-attribute]
        return
    flags = 0
    if operation & LOCK_EX:
        flags |= _LOCKFILE_EXCLUSIVE_LOCK
    if operation & LOCK_NB:
        flags |= _LOCKFILE_FAIL_IMMEDIATELY
    if not _LockFileEx(handle, flags, 0, 0xFFFFFFFF, 0xFFFFFFFF, ctypes.byref(overlapped)):
        err = ctypes.get_last_error()
        if err in (_ERROR_LOCK_VIOLATION, _ERROR_IO_PENDING):
            raise BlockingIOError(errno.EAGAIN, 'Resource temporarily unavailable')
        raise ctypes.WinError(err)  # ty: ignore[unresolved-attribute]


def flock(fd: int, operation: int) -> None:
    _lock_whole_file(fd, operation)


def lockf(fd: int, cmd: int, length: int = 0, start: int = 0, whence: int = 0) -> None:
    # kitty only ever locks whole files, which is what a zero length means here
    _lock_whole_file(fd, cmd)
