#!/usr/bin/env python3
# License: GPLv3 Copyright: 2025, Kovid Goyal, <kovid at kovidgoyal.net>
#
# Run a program on a real Cygwin/MSYS2 pty, bridging its I/O to stdin/stdout.
#
# On Windows kitty normally runs shells inside a ConPTY, but conhost re-renders
# the child's output on its own frame cadence: it consumes the application's
# synchronized-update markers and can split one logical update (for example a
# zsh line-editor redraw) across multiple frames, which shows up as cursor
# bouncing and flicker. Cygwin/MSYS2 shells do not need a Windows console at
# all, they need a POSIX pty. This bridge (run under MSYS2's own python, which
# is a Cygwin program and therefore can create Cygwin ptys) gives them exactly
# that, the same architecture mintty uses: the shell's escape codes flow
# through untouched, and modern Cygwin transparently attaches a hidden ConPTY
# for any native Windows program launched from the shell, so those keep
# working too.
#
# kitty resizes the pty by writing the XTWINOPS sequence ESC [ 8 ; rows ; cols t
# into the input stream; the bridge intercepts it, applies TIOCSWINSZ (which
# delivers SIGWINCH to the shell) and does not forward it. Everything else
# passes through verbatim.

import fcntl
import os
import pty
import re
import select
import signal
import struct
import sys
import termios


def set_winsize(fd: int, rows: int, cols: int) -> None:
    try:
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
    except OSError:
        pass


RESIZE_PAT = re.compile(br'\x1b\[8;(\d{1,5});(\d{1,5})t')
# Longest prefix of an incomplete resize sequence we might need to hold back.
MAX_HOLD = 16


def could_be_resize_prefix(data: bytes) -> int:
    'Length of the trailing bytes of data that are a prefix of a resize sequence, else 0.'
    full = b'\x1b[8;'
    for start in range(max(0, len(data) - MAX_HOLD), len(data)):
        tail = data[start:]
        if len(tail) <= len(full):
            if tail == full[:len(tail)]:
                return len(tail)
        elif tail.startswith(full):
            rest = tail[len(full):]
            # rest must look like digits [; digits] with no terminator yet
            if re.fullmatch(br'\d{0,5}(;\d{0,5})?', rest):
                return len(tail)
    return 0


def write_all(fd: int, data: bytes) -> None:
    while data:
        try:
            n = os.write(fd, data)
        except InterruptedError:
            continue
        data = data[n:]


def main() -> None:
    args = sys.argv[1:]
    rows, cols = 24, 80
    while args and args[0] != '--':
        if args[0] == '--rows':
            rows = int(args[1]); args = args[2:]
        elif args[0] == '--cols':
            cols = int(args[1]); args = args[2:]
        else:
            raise SystemExit(f'Unknown option: {args[0]}')
    if not args or args[0] != '--' or len(args) < 2:
        raise SystemExit('Usage: pty-bridge.py [--rows N] [--cols N] -- cmd [args...]')
    argv = args[1:]
    # Convert a Windows-style path (C:\x or C:/x) to POSIX form (/c/x), since
    # this is a Cygwin python where path handling is POSIX.
    if len(argv[0]) > 2 and argv[0][1] == ':':
        argv[0] = '/' + argv[0][0].lower() + argv[0][2:].replace('\\', '/')

    pid, master = pty.fork()
    if pid == 0:
        try:
            os.execvp(argv[0], argv)
        except OSError as e:
            print(f'Failed to exec {argv[0]}: {e}', file=sys.stderr)
            os._exit(127)
    set_winsize(master, rows, cols)

    signal.signal(signal.SIGTERM, lambda *a: os.kill(pid, signal.SIGHUP))
    stdin_fd, stdout_fd = 0, 1
    stdin_open = True
    held = b''  # possible partial resize sequence carried across reads
    status = None  # child's wait status once reaped
    hangup_deadline = 0.0

    import time as time_module
    while True:
        # Watch for the child exiting directly rather than trusting pty-master
        # EOF alone: with the Cygwin/native interop a helper process can keep the
        # pty slave open after the shell is gone, so master EOF may never come.
        if status is None:
            try:
                wpid, wstatus = os.waitpid(pid, os.WNOHANG)
            except OSError:
                wpid, wstatus = pid, 0
            if wpid == pid:
                status = wstatus
                # Drain whatever final output is already buffered, then quit.
                try:
                    os.set_blocking(master, False)
                    while True:
                        try:
                            data = os.read(master, 65536)
                        except OSError:
                            break
                        if not data:
                            break
                        write_all(stdout_fd, data)
                except OSError:
                    pass
                break
        if hangup_deadline and time_module.monotonic() > hangup_deadline:
            # The window is gone and the shell ignored SIGHUP: force it dead.
            try:
                os.kill(pid, signal.SIGKILL)
            except OSError:
                pass
            hangup_deadline = 0.0
        rfds = [master] + ([stdin_fd] if stdin_open else [])
        try:
            ready, _, _ = select.select(rfds, [], [], 0.5)
        except InterruptedError:
            continue
        if master in ready:
            try:
                data = os.read(master, 65536)
            except OSError:
                data = b''
            if not data:
                break  # pty closed
            try:
                write_all(stdout_fd, data)
            except OSError:
                # kitty is gone: nothing to display to, shut the shell down
                try:
                    os.kill(pid, signal.SIGHUP)
                except OSError:
                    pass
                hangup_deadline = hangup_deadline or (time_module.monotonic() + 5.0)
        if stdin_open and stdin_fd in ready:
            try:
                data = os.read(stdin_fd, 65536)
            except OSError:
                data = b''
            if not data:
                # kitty closed our stdin: the window is gone, hang up the shell
                stdin_open = False
                try:
                    os.kill(pid, signal.SIGHUP)
                except OSError:
                    pass
                hangup_deadline = time_module.monotonic() + 5.0
                continue
            data = held + data
            held = b''
            # Apply and strip any complete resize sequences
            out = []
            pos = 0
            for m in RESIZE_PAT.finditer(data):
                out.append(data[pos:m.start()])
                set_winsize(master, int(m.group(1)), int(m.group(2)))
                pos = m.end()
            data = b''.join(out) + data[pos:]
            # Hold back a trailing partial resize sequence for the next read
            n = could_be_resize_prefix(data)
            if n:
                held = data[len(data) - n:]
                data = data[:len(data) - n]
            if data:
                try:
                    write_all(master, data)
                except OSError:
                    pass

    if status is None:
        try:
            _, status = os.waitpid(pid, 0)
        except OSError:
            status = 0
    raise SystemExit(os.waitstatus_to_exitcode(status))


if __name__ == '__main__':
    main()
