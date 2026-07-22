#!/bin/bash
# Build the kitty Windows installer: assembles the dist tree (make-dist.sh)
# and compiles it into dist/kitty-setup.exe with Inno Setup.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

"$HERE/make-dist.sh"
"$HERE/make-shellext.sh"

ISCC=""
for c in "$LOCALAPPDATA/Programs/Inno Setup 6/ISCC.exe" \
         "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" \
         "/c/Program Files/Inno Setup 6/ISCC.exe"; do
    [ -f "$c" ] && ISCC="$c" && break
done
[ -n "$ISCC" ] || { echo "Inno Setup not found; install it (winget install JRSoftware.InnoSetup)"; exit 1; }

VERSION="$(cat "$ROOT/dist/VERSION" 2>/dev/null || echo 0.48.0)"
echo "==> compiling installer (version $VERSION)"
MSYS2_ARG_CONV_EXCL="/D" "$ISCC" "/DAppVersion=$VERSION" "$HERE/kitty.iss" | tail -3
ls -la "$ROOT"/dist/kitty-setup.exe
