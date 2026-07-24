#!/bin/bash
# Run the kitty test suite on the native Windows port.
#
# This used to run an explicit list of 13 modules. The rest either failed on
# POSIX assumptions or hung outright, so naming the ones that worked was the
# only way to get a usable signal. They now either pass or skip with a stated
# reason, so the whole suite runs, Go tests included.
#
# Skips are printed by the runner rather than hidden in a list here, so what is
# not covered stays visible in the CI log. WINDOWS_TODO records the gaps behind
# the ones that cannot pass yet.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

PY="${PY:-python}"
# A hang would otherwise sit there until the job limit. The suite takes well
# under a minute, so anything approaching this is wrong rather than slow.
TIMEOUT="${TIMEOUT:-900}"

if timeout "$TIMEOUT" "$PY" test.py; then
    echo
    echo "Test suite passed"
    exit 0
fi

rc=$?
echo
if [ "$rc" -eq 124 ]; then
    echo "TIMED OUT after ${TIMEOUT}s"
fi
exit "$rc"
