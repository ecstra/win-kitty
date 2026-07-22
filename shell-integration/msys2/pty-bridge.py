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
import signal
import struct
import sys
import termios
import threading
import time

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
        except BlockingIOError:
            time.sleep(0.001)
            continue
        data = data[n:]

def ensure_utf8_environment() -> None:
    # The msys2 runtime's pty pump converts the output of native (non-msys)
    # programs from GetConsoleCP() to the pty charset (term_code_page). kitty
    # spawns this bridge with CREATE_NO_WINDOW, so it owns a hidden console
    # whose codepage defaults to the OEM one (437): set it to UTF-8 so that,
    # with a UTF-8 locale, the conversion is an identity and native programs'
    # UTF-8 output reaches kitty unmangled.
    try:
        import ctypes
        k32 = ctypes.CDLL('kernel32.dll')
        k32.SetConsoleCP(65001)
        k32.SetConsoleOutputCP(65001)
        # Mark our stdio pipe handles non-inheritable: otherwise every native
        # program spawned from the shell (and its hidden conhost) inherits a
        # copy of the output pipe's write handle, and kitty does not see EOF
        # (the window would not close) until all of them exit.
        k32.GetStdHandle.restype = ctypes.c_void_p
        for std in (-10, -11, -12):
            h = k32.GetStdHandle(std)
            if h:
                k32.SetHandleInformation(ctypes.c_void_p(h), 1, 0)  # HANDLE_FLAG_INHERIT off
    except Exception:
        pass
    # Ensure a UTF-8 locale so the pty charset (latched from the shell process's
    # locale) is UTF-8. Respect any explicit UTF-8 locale the user configured.
    vals = [os.environ.get(v, '') for v in ('LC_ALL', 'LC_CTYPE', 'LANG')]
    if not any('UTF-8' in v.upper() or 'UTF8' in v.upper() for v in vals):
        os.environ['LANG'] = 'en_US.UTF-8'

def main() -> None:
    ensure_utf8_environment()
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

    # One thread per direction, each a non-blocking poll loop with a short,
    # idle-adaptive sleep. Blocking waits are avoided entirely: a parked
    # blocking read (or cygwin select on the non-cygwin stdin pipe) holds
    # cygwin-internal locks in a ~10-15ms timed-retry cycle that stalls the
    # other direction and adds a visible per-keystroke delay. The directions
    # run in separate threads so a master read stalled on the pty lock (held by
    # the shell's own input wait) cannot delay keystroke delivery.
    for fd in (0, master):
        try:
            os.set_blocking(fd, False)
        except OSError:
            pass

    def hangup_then_kill() -> None:
        try:
            os.kill(pid, signal.SIGHUP)
        except OSError:
            return
        def force_kill() -> None:
            time.sleep(5.0)
            try:
                os.kill(pid, signal.SIGKILL)
            except OSError:
                pass
        threading.Thread(target=force_kill, daemon=True).start()

    def adaptive_sleep(last_busy: float) -> None:
        # Keep the poll tight while interaction is ongoing (a shell redraw takes
        # a few ms to come back, and backing off during that window would delay
        # the next keystroke), relax it only when genuinely idle.
        idle = time.monotonic() - last_busy
        time.sleep(0.0008 if idle < 0.25 else (0.005 if idle < 2.0 else 0.012))

    def pump_input() -> None:
        held = b''
        last_busy = time.monotonic()
        while True:
            try:
                data = os.read(0, 65536)
            except BlockingIOError:
                adaptive_sleep(last_busy)
                continue
            except OSError:
                data = b''
            if not data:
                # kitty closed our stdin: the window is gone, hang up the shell
                hangup_then_kill()
                return
            last_busy = time.monotonic()
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
                    return

    def pump_output() -> None:
        last_busy = time.monotonic()
        while True:
            try:
                data = os.read(master, 65536)
            except BlockingIOError:
                adaptive_sleep(last_busy)
                continue
            except OSError:
                return
            if not data:
                return
            last_busy = time.monotonic()
            try:
                write_all(1, data)
            except OSError:
                # kitty is gone: nothing to display to, shut the shell down
                hangup_then_kill()
                return

    tin = threading.Thread(target=pump_input, daemon=True)
    tout = threading.Thread(target=pump_output, daemon=True)
    tin.start(); tout.start()

    # Reap the child directly rather than trusting pty-master EOF: with the
    # Cygwin/native interop a helper process can keep the pty slave open after
    # the shell is gone, so master EOF may never come.
    try:
        _, status = os.waitpid(pid, 0)
    except OSError:
        status = 0
    tout.join(timeout=0.5)  # let the final output drain
    raise SystemExit(os.waitstatus_to_exitcode(status))

if __name__ == '__main__':
    main()
