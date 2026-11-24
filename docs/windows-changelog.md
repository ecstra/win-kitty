# Changelog

What changed in each release of kitty for Windows, newest first. What upstream kitty changed is in [its own changelog](https://sw.kovidgoyal.net/kitty/changelog/).

## Next (unreleased)

Based on kitty 0.51.1.

### Fixed

- With the acrylic background on, the window could show only the background, with no text and no cursor.
- Completing a path that starts with a backslash, such as `\Windows`, now looks in the root of the current drive.
- Files the ssh kitten copies from inside your home folder no longer arrive on the remote with backslashes in their names.
- The ssh kitten asks for `--dest` when you copy a file from outside your home folder, instead of putting it in a folder named after the drive on the remote.

### Changed

- Updated from kitty 0.48.0 to 0.51.1.
- Building from source now uses the MSYS2 UCRT64 toolchain instead of MINGW64, which MSYS2 is retiring, and needs the Slang shader compiler.

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
