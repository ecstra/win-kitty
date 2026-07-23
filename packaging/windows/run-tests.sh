#!/bin/bash
# Run the part of the kitty test suite that works on the native Windows port.
#
# Most of the suite assumes a Unix host. It opens ptys, calls os.mkfifo, uses
# POSIX shared memory and utmp, and spawns real shells, none of which exist
# here. Running the whole suite does not fail cleanly either, it hangs, so CI
# needs an explicit list.
#
# The excluded modules are listed below with the reason each one is out, so
# this reads as a record of what is left to port rather than a quiet subset.
# When one starts working on Windows, move it up into MODULES.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

PY="${PY:-python}"
TIMEOUT="${TIMEOUT:-120}"

MODULES=(
    parser
    screen
    layout
    search_query_parser
    crypto
    multicell
    tab_bar
    open_actions
    keys
    mouse
    notifications
    command_palette
    clipboard
)

# Excluded, with the blocker:
#   atexit             spawns the atexit helper over a pty
#   check_build        checks a Unix install layout
#   completion         drives the shell completion helpers through a shell
#   datatypes          os.mkfifo
#   dnd, dnd_kitten    the dnd kitten is not implemented on Windows
#   file_transmission  needs a pty and Unix file modes
#   fonts              fontconfig matching differs, needs its own Windows tests
#   glfw               expects the X11/Cocoa backends
#   graphics, gr       need a pty for the graphics protocol round trip
#   options            execs a helper that is not a Windows binary
#   panels             the panel kitten is not implemented on Windows
#   shell_integration  needs a pty and a POSIX shell
#   shm                POSIX shared memory
#   ssh                needs a pty
#   tui                needs a pty
#   utmp               no utmp on Windows

failed=()
for m in "${MODULES[@]}"; do
    printf '==> %s\n' "$m"
    if PYTHONUTF8=1 timeout "$TIMEOUT" "$PY" test.py --module "$m"; then
        :
    else
        rc=$?
        [ "$rc" -eq 124 ] && echo "    TIMED OUT after ${TIMEOUT}s"
        failed+=("$m")
    fi
done

if [ ${#failed[@]} -gt 0 ]; then
    printf '\nFAILED: %s\n' "${failed[*]}"
    exit 1
fi
printf '\nAll %d Windows test modules passed\n' "${#MODULES[@]}"
