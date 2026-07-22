#!/bin/bash
# Build the Windows 11 modern context-menu integration: compile the
# IExplorerCommand DLL, pack it into a sparse MSIX and sign it with a
# self-signed code-signing certificate (created on first run, kept in
# packaging/windows/cert which is gitignored). The installer registers the
# package and trusts the certificate.
#
# Outputs (under dist/):
#   dist/kitty/shellext/kitty_shell_ext.dll  (picked up by the dist tree)
#   dist/kitty-menu.msix
#   dist/kitty-menu.cer
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
MINGW="${MINGW_ROOT:-/c/msys64/mingw64}"
CERTDIR="$HERE/cert"
VERSION="$(cat "$ROOT/dist/VERSION" 2>/dev/null || echo 0.48.0)"

SDKBIN="$(ls -d "/c/Program Files (x86)/Windows Kits/10/bin/"*/x64 2>/dev/null | sort -V | tail -1)"
[ -n "$SDKBIN" ] || { echo "Windows SDK (makeappx/signtool) not found"; exit 1; }

echo "==> compiling shell extension dll"
mkdir -p "$ROOT/dist/kitty/shellext"
PATH="$MINGW/bin:$PATH" "$MINGW/bin/gcc" -shared -O2 -Wall -municode -o "$ROOT/dist/kitty/shellext/kitty_shell_ext.dll" \
    "$HERE/shellext/kitty_shell_ext.c" -lshlwapi -lole32 -luuid

echo "==> staging sparse package"
PKG="$ROOT/dist/msix-staging"
rm -rf "$PKG"
mkdir -p "$PKG/Assets"
sed "s/@VERSION@/$VERSION.0/" "$HERE/shellext/AppxManifest.xml" > "$PKG/AppxManifest.xml"
cp "$ROOT/logo/kitty-128.png" "$PKG/Assets/kitty150.png"
cp "$ROOT/logo/kitty-128.png" "$PKG/Assets/kitty44.png"

echo "==> packing msix"
rm -f "$ROOT/dist/kitty-menu.msix"
MSYS2_ARG_CONV_EXCL="*" "$SDKBIN/makeappx.exe" pack /d "$(cygpath -w "$PKG")" /p "$(cygpath -w "$ROOT/dist/kitty-menu.msix")" /nv >/dev/null

if [ ! -f "$CERTDIR/kitty-menu.pfx" ]; then
    echo "==> creating self-signed code-signing certificate"
    mkdir -p "$CERTDIR"
    powershell.exe -NoProfile -Command "
      \$c = New-SelfSignedCertificate -Type Custom -Subject 'CN=kitty-windows-port' \
        -KeyUsage DigitalSignature -FriendlyName 'kitty windows port' \
        -CertStoreLocation 'Cert:\CurrentUser\My' \
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}');
      \$pwd = ConvertTo-SecureString -String 'kitty' -Force -AsPlainText;
      Export-PfxCertificate -Cert \$c -FilePath '$(cygpath -w "$CERTDIR")\kitty-menu.pfx' -Password \$pwd | Out-Null;
      Export-Certificate -Cert \$c -FilePath '$(cygpath -w "$CERTDIR")\kitty-menu.cer' | Out-Null;
      Remove-Item \$c.PSPath;
      'certificate created'" | tail -1
fi
cp "$CERTDIR/kitty-menu.cer" "$ROOT/dist/kitty-menu.cer"

echo "==> signing msix"
MSYS2_ARG_CONV_EXCL="*" "$SDKBIN/signtool.exe" sign /fd SHA256 /f "$(cygpath -w "$CERTDIR/kitty-menu.pfx")" /p kitty "$(cygpath -w "$ROOT/dist/kitty-menu.msix")" >/dev/null
echo "==> shellext done"
