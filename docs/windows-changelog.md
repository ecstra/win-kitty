# Changelog

What changed in each release of kitty for Windows, newest first. What upstream kitty changed is in [its own changelog](https://sw.kovidgoyal.net/kitty/changelog/).

## Next (unreleased)

Based on kitty 0.51.1.

### Added

- Dragging tabs. Drag one along the tab bar to move it, onto another kitty window to move it there, or out of kitty to open it in a new window where you drop it. Escape cancels.
- Dragging selected text or a link out of kitty into other programs. A link to a file drops as the file, which Explorer copies.
- Remote control over a Unix socket, `listen_on unix:PATH`.
- `kitty +list-fonts` and the other `kitty +` commands, and the full `kitty --help`, print in any terminal, PowerShell included.
- The installer can install kitty for you only, with no administrator prompt, which is now the default, or for everyone as before. It follows Windows' light or dark mode, with kitty's cat on its pages.
- Custom shaders, `custom_shaders`. The installer now includes the shader compiler they need.
- `restore_session_after_logout`. kitty saves its windows, tabs and scrollback when you sign out, restart or shut down, and brings them back the next time it starts.
- Tab titles can show the program running in a window, `{tab.active_exe}`, and how much memory a tab's programs use, `{tab.children_mem_usage}`. `confirm_os_window_close` with `count-background` counts programs left running in the background.
- Launch wrappers, `launch_wrapper.py`.
- Typing with an input method, such as Japanese, Chinese or Korean. The text you are composing shows at the cursor, and the list of words opens beside it.
- `remap_modifiers`.
- Automatic color themes switch as soon as you change Windows between light and dark.
- Panels, `kitten panel`. kitty can sit as a bar on a screen edge that maximised windows leave free, above other windows, or behind your desktop icons like a live wallpaper.
- The quick access terminal, `kitten quick-access-terminal`, a kitty that drops down from the top of the screen and hides again when you run it again, or on a hotkey you set with `hotkey` in quick-access-terminal.conf. The installer can start it hidden when you sign in.
- The dnd kitten, and drag and drop for programs inside kitty, which can take every format a drop offers and drag their own data out, files from another machine included.

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
- A program can no longer make kitty send your Windows login hash to another machine by naming an image file on a network share. Such paths are refused before kitty touches them.
- The pipe kittens use to draw no longer accepts connections from other machines.
- The installed kitty no longer prints "Fontconfig error: Cannot load default config file", and keeps a font cache, so it finds fonts faster.
- Windows opened after the first one, a dragged out tab among them, no longer draw their window borders and the edges of the tab bar in random colours, often black.
- With a see-through background, the space beside the tabs no longer shows as a solid dark strip.
- Characters typed with AltGr, such as @ on a German layout, no longer arrive with an escape code in front of them.
- Alt with a letter, such as Alt+B in a shell, no longer types the letter as well, which also left stray text like `5;4uS` behind after Shift+Alt+S.
- Shortcuts with the left Ctrl and Alt work on layouts that have AltGr, instead of typing the AltGr character. Where no shortcut is set, they still type it.
- A background process reports its real exit code, including codes above 255 and crash codes.
- `launch --hold` keeps the window open after its program exits.
- New windows and tabs opened from an MSYS2 or Cygwin shell start in the folder the shell is in, rather than the one it started in.
- A session saved with `save_as_session` reopens an MSYS2 or Cygwin shell's window in the folder the shell was in, instead of your home folder.
- A window kitty puts back on a screen with different scaling from the main one, from a saved session or a remembered position, keeps its size instead of growing by that scaling.
- Alt or Ctrl with a key on a keyboard layout other than US goes out as that layout's character, with the US key at that place for programs to fall back on, as on Linux, so Alt+ß on German sends `ESC -` rather than `ESC [`.
- A window's program reports its real exit code, which `kitten @ launch --wait-for-child-to-exit` gives back, instead of always 0.
- `kitten @ run` runs its program and returns its output, instead of timing out, and a background process reports its exit while kitty sits idle.
- `kitty --single-instance` starts, and a second start opens its window in the kitty already running, its folder, arguments and environment whole when they hold letters outside ASCII. With `--wait-for-single-instance-window-close` it returns once that window closes.
- Images named by a path with accented letters, such as `café.png`, load.
- Uninstalling kitty installed for everyone removes the certificate its Explorer menu added, and the menu entry of every account. The shortcuts of such an install start in the home folder of whoever opens them, and Open in kitty on a drive opens its root.

### Changed

- Updated from kitty 0.48.0 to 0.51.1.
- The desktop-ui kitten is marked Linux only, since it serves Linux's desktop portals.
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
