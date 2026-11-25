# Changelog

What changed in each release of kitty for Windows, newest first. What upstream kitty changed is in [its own changelog](https://sw.kovidgoyal.net/kitty/changelog/).

## Next (unreleased)

Based on kitty 0.51.1.

### Added

- Dragging tabs. Drag one along the tab bar to move it, onto another kitty window to move it there, or out of kitty to open it in a new window where you drop it. Escape cancels.

### Fixed

- With the acrylic background on, the window could show only the background, with no text and no cursor.
- Completing a path that starts with a backslash, such as `\Windows`, now looks in the root of the current drive.
- Files the ssh kitten copies from inside your home folder no longer arrive on the remote with backslashes in their names.
- The ssh kitten asks for `--dest` when you copy a file from outside your home folder, instead of putting it in a folder named after the drive on the remote.
- `kitten run-shell` goes back to the shell that ran it, zsh, pwsh or cmd, instead of opening cmd. Kittens now find programs on your PATH by their name alone, without the `.exe`.
- Pressing Ctrl+C no longer leaves the shell or program a kitten started running in the background, where it read your typing alongside the next shell.
- With `cursor_trail` on, resizing or maximizing the window no longer sends the cursor flying in from the side. It stays in its cell.
- Kittens such as choose-files no longer go blank when you resize the window while one is open.
- The cursor no longer flashes off and on while you type in PowerShell.
- Many kitty.conf settings that did nothing on Windows now work. The pointer hides while you type (`mouse_hide_wait`), `focus_follows_mouse` moves between kitty windows, `confirm_os_window_close` asks when you set it, saving kitty.conf reloads it, `resize_in_steps` snaps to whole cells, and `clear_selection_on_clipboard_loss` notices other programs copying.
- `command_on_bell` and background launches, such as `launch --type=background`, run instead of failing with an error.
- The window keeps its size and place across restarts instead of growing and drifting a little each time.
- Changing the background opacity while kitty runs works with blur on, and an opacity of 1 is fully opaque.
- Keypad keys, the modifier keys on their own, and the mouse back and forward buttons can be bound. More pointer shapes show their own cursor.
- A tab bar on the left or right starts below the title bar, and a precision touchpad scrolls smoothly instead of a line at a time.
- `KITTY_OS` is `windows`, and kitty opens files to edit in Notepad when no other editor is found.

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
