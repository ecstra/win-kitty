# Changelog

What changed in each release of kitty for Windows, newest first. What upstream kitty changed is in [its own changelog](https://sw.kovidgoyal.net/kitty/changelog/).

## Next (unreleased)

### Changed

- Building from source now uses the MSYS2 UCRT64 toolchain instead of MINGW64, which MSYS2 is retiring.

## 0.48.0-win.1 (2026-07-24)

The first release, kitty 0.48.0 running natively on Windows 11.

### Added

- Acrylic background, the same one Windows Terminal has.
- A title bar in the terminal's colours, so the window looks like one piece.
- pwsh, cmd and MSYS2 shells, each on the right kind of terminal.
- Kittens, the real Go ones.
- Images through icat and the graphics protocol.
- Remote control over TCP.
- Windows notifications.
- Open in kitty in the Explorer menu.
- Low input lag.
- Config lives in `%USERPROFILE%\.config\kitty\`, the same place as on Linux.
