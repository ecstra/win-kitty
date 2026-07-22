#!/bin/bash
# Assemble a relocatable, self-contained Windows install tree for kitty at
# dist/kitty, suitable for packaging with the Inno Setup script next to this
# file. Run from git-bash/MSYS2 with the repo already built (python setup.py
# build && build-launcher).
#
# The tree mirrors the source layout (the launcher resolves everything
# relative to itself), bundles the python stdlib at pylib/lib/pythonX.Y (the
# windows-dist launcher sets its python home there) and every MinGW DLL the
# binaries and python extensions link against, so nothing outside the install
# directory is needed at runtime.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MINGW="${MINGW_ROOT:-/c/msys64/mingw64}"
PYVER="${PYVER:-3.14}"
DIST="$ROOT/dist/kitty"
PY="$MINGW/bin/python.exe"

cd "$ROOT"
[ -x "$PY" ] || { echo "mingw python not found at $PY (set MINGW_ROOT)"; exit 1; }
[ -f kitty/fast_data_types.pyd ] || { echo "build kitty first: python setup.py build"; exit 1; }

echo "==> building windows-dist launcher"
rm -f kitty/launcher/kitty.exe kitty/launcher/kitty-console.exe
PYTHONUTF8=1 PATH="$MINGW/bin:$PATH" "$PY" setup.py build-windows-dist-launcher >/dev/null

echo "==> staging tree"
rm -rf "$DIST"
mkdir -p "$DIST"
cp __main__.py "$DIST/"
cp -r kitty kittens shell-integration terminfo logo fonts "$DIST/" 2>/dev/null || true
# Prune what the runtime does not need. Keep .py/.pyd/.so/.glsl/.conf/.json,
# the launcher exes and kitty.ico; drop sources, build litter and caches.
find "$DIST" -type d -name __pycache__ -prune -exec rm -rf {} +
find "$DIST" \( -name '*.c' -o -name '*.h' -o -name '*.go' -o -name '*.m' -o -name '*.o' \
    -o -name '*.old' -o -name '*.rc' -o -name '*.pyi' -o -name 'go.mod' -o -name 'go.sum' \) -delete

echo "==> restoring dev launcher"
DIST_LAUNCHER_BUILT=1
rm -f kitty/launcher/kitty.exe kitty/launcher/kitty-console.exe
PYTHONUTF8=1 PATH="$MINGW/bin:$PATH" "$PY" setup.py build-launcher >/dev/null

echo "==> bundling python stdlib"
mkdir -p "$DIST/pylib/lib"
cp -r "$MINGW/lib/python$PYVER" "$DIST/pylib/lib/"
for d in test idlelib tkinter turtledemo ensurepip venv pydoc_data site-packages; do
    rm -rf "$DIST/pylib/lib/python$PYVER/$d"
done
mkdir -p "$DIST/pylib/lib/python$PYVER/site-packages"
find "$DIST/pylib" -type d -name __pycache__ -prune -exec rm -rf {} +

echo "==> collecting DLL closure"
LAUNCHER="$DIST/kitty/launcher"
OBJDUMP="$MINGW/bin/objdump.exe"
declare -A seen
queue=("$LAUNCHER/kitty.exe" "$LAUNCHER/kitty-console.exe" "$LAUNCHER/kitten.exe")
while IFS= read -r -d '' f; do queue+=("$f"); done < <(find "$DIST/kitty" "$DIST/kittens" "$DIST/pylib" \( -name '*.pyd' -o -name '*.so' \) -print0)
i=0
while [ $i -lt ${#queue[@]} ]; do
    f="${queue[$i]}"; i=$((i+1))
    while IFS= read -r dll; do
        key="${dll,,}"
        [ -n "${seen[$key]:-}" ] && continue
        seen[$key]=1
        if [ -f "$MINGW/bin/$dll" ]; then
            cp -n "$MINGW/bin/$dll" "$LAUNCHER/" 2>/dev/null || true
            queue+=("$LAUNCHER/$dll")
        fi
    done < <("$OBJDUMP" -p "$f" 2>/dev/null | awk '/DLL Name/{print $3}')
done
echo "    $(ls "$LAUNCHER"/*.dll 2>/dev/null | wc -l) DLLs bundled"

echo "==> smoke test (clean PATH)"
SMOKE_PATH="/c/Windows/System32:/c/Windows"
if PATH="$SMOKE_PATH" "$LAUNCHER/kitten.exe" --version >/dev/null 2>&1; then
    echo "    kitten.exe OK"
else
    echo "    WARNING: kitten.exe failed with clean PATH"; exit 1
fi
if PATH="$SMOKE_PATH" PYTHONHOME= PYTHONPATH= "$LAUNCHER/kitty-console.exe" --version 2>&1 | grep -q kitty; then
    echo "    kitty-console.exe OK: $(PATH="$SMOKE_PATH" "$LAUNCHER/kitty-console.exe" --version 2>&1 | head -1)"
else
    echo "    WARNING: kitty-console.exe failed with clean PATH:"
    PATH="$SMOKE_PATH" "$LAUNCHER/kitty-console.exe" --version 2>&1 | head -5
    exit 1
fi

VERSION=$("$PY" -c "import re; print(re.search(r'version: Version = Version\((\d+), (\d+), (\d+)\)', open('kitty/constants.py').read()).expand(r'\1.\2.\3'))" 2>/dev/null || echo 0.48.0)
echo "$VERSION" > "$ROOT/dist/VERSION"
echo "==> done: $DIST (version $VERSION)"
