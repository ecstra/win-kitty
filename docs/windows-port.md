# kitty on Windows

This is a native Windows build of kitty. It runs on Windows directly, with no WSL, no X server, and no Cygwin layer under the terminal itself. Windows console programs run through the pseudoconsole API (ConPTY). MSYS2 and Cygwin shells run on a real Cygwin pty instead, which keeps conhost out of the way. Everything is drawn with OpenGL, the same rendering path kitty uses on every other platform.

For how the pieces work, read [windows-internals.md](windows-internals.md). For why they are built that way, read [decisions.md](decisions.md).

## Requirements

Windows 11. The acrylic and the rounded title bar are built on composition
features Windows 10 does not have. Nothing in the build or the installer checks
the version, so it will start on Windows 10, but it is neither targeted nor
tested there and the chrome will not look right.

## Status

This is a young port and it has plenty of bugs. Daily use works, and the parts listed under "What works" have been used enough to trust, but anything off that path is likely to be rough or broken. Expect to hit things nobody has hit yet.

Bug reports for the Windows build belong on the fork at https://github.com/ecstra/win-kitty and not on the upstream kitty tracker. None of this code is upstream, so upstream cannot act on it. See [CONTRIBUTING.md](../CONTRIBUTING.md).

## Installing

Run `dist/kitty-setup.exe`. It asks for administrator rights, because it installs for all users and writes to `C:\Program Files\kitty`.

The installer offers three optional tasks.

- Add kitty to your PATH: puts `<install>\bin` on the system PATH so `kitty` and `kitten` work in any terminal.
- Add "Open in kitty" to the Explorer right click menu: appears in the main Windows 11 menu, and under "Show more options" as well. Both are installed because Windows 11 reads the two from different places, see [windows-internals.md](windows-internals.md).
- Create a desktop shortcut: off by default.

A Start menu entry is always created. To install without any prompts, run it with the silent flags.

```
dist\kitty-setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
```

Close any running kitty first. The source tree keeps its own development build at `kitty\launcher\kitty.exe`, which the installer never touches.

## What works

- Window creation, OpenGL rendering, live resizing, and per monitor DPI scaling.
- Any Windows console program as the shell. cmd.exe, Windows PowerShell, and pwsh all run. Pick one with the `shell` option in kitty.conf.
- MSYS2 and Cygwin shells such as zsh and bash, on a real pty, with shell integration.
- Keyboard input, including the ctrl and alt shortcuts, the function keys, and the arrow keys.
- Tabs and OS windows. Closing one OS window leaves the others alone.
- Scrollback, mouse selection, and the real Windows clipboard for copy and paste.
- Dropping files and text onto a window, which pastes the paths or the text.
- Transparency. With `background_blur` on you get the real Windows 11 acrylic material, matched to what Windows Terminal shows, and it stays put when the window loses focus.
- A custom title bar that matches the terminal background, with rounded corners and minimize, maximize, and close buttons.
- Kittens, run from inside kitty. icat, hints, unicode_input, themes, ask, diff, choose-files, and the confirmation prompts all work.
- Desktop notifications, shown as real Windows toasts.
- Config reloading, including a theme applied from the themes kitten showing up without a restart.
- Windows specific defaults, so an empty kitty.conf already looks right. See below.

## What is not there yet

Known missing, and confirmed so.

- Remote control over a Unix socket, meaning `listen_on unix:...` and nothing
  else. Windows itself has carried AF_UNIX since build 17063, so this is not a
  platform limit: CPython does not build the family on Windows
  ([python/cpython#77589](https://github.com/python/cpython/issues/77589), open
  since 2018), so `socket.socket(socket.AF_UNIX)` cannot be constructed. Passing
  family 1 by number gets as far as a socket object and then fails on
  `bind(): bad family`, because the address marshalling is compiled out too.
  `listen_on unix:` now says this rather than reporting a vague parse error.
  Remote control itself works, over `tcp:`. See below.
- Four kittens. dnd, panel, quick-access-terminal, and desktop-ui are marked `(not supported on Windows)` in the `kitten` command list and print a clear message if you run one anyway.
- Dragging content out of a kitty window. Dropping onto a window works, the other direction does not.
- The `kitty +something` entry points from another terminal. They reach the window binary and print nothing. Inside kitty they work as normal. `kitty +icat` is removed on Windows, since it only forwards to `kitten icat`, which will not run outside kitty anyway.
- The full `kitty --help` option list. From another terminal `--help` prints a short pointer to the online documentation instead, because the formatter needs a terminal size that Windows reports differently. `--version` prints normally.

Deliberate, not missing.

- Kittens refuse to start outside kitty. On Windows a kitten needs kitty's own input and output path, and in another terminal it misbehaves in ways that look like bugs, so it exits with a message. Inside kitty every ported kitten works.

Not tested, so treat as unknown rather than working.

- The ssh kitten, the file transfer kitten, and anything else that expects a Unix host environment.
- Sessions, watchers, and startup scripts.

## Remote control

`kitty @` works on Windows over a TCP address. Put this in kitty.conf:

    allow_remote_control yes
    listen_on tcp:127.0.0.1:0

A port of 0 asks the OS for a free one, which is what kitty does with a `tcp:`
address from the config file anyway. The resolved address goes into
`KITTY_LISTEN_ON` for every child, so `kitten @ ls` inside a kitty window needs
no `--to`. From outside, pass the port kitty resolved.

Most of this was already in place. `parse_address_spec` and `expand_listen_on`
have always handled `tcp:` and `tcp6:`, the client in `remote_control.py` is
written against whatever family it is handed, the Go side resolves `tcp:`
through `utils.ParseSocketAddress`, and the accept loop lives in C in
`talk_loop`, where wincompat's `poll()` already routes socket descriptors to
`WSAPoll` rather than treating them as pipes. Two things were in the way.

The first stopped the listener existing at all: `boss.listen_on` tested
`socket.AF_UNIX` for every address family rather than only for `unix:`, so a
`tcp:` address raised `AttributeError` on Windows before it could bind, and the
caller reported that as a vague "Invalid listen_on, ignoring".

The second stopped any command being understood, and was visible only as a bare
`MemoryError` on stderr. `peer_message_received` is called with a `y#` format,
whose length argument is a `Py_ssize_t`, and the length was being cast to `int`.
On the SysV ABI that argument still lands in a register and writing its low half
zeroes the top, so the bug is invisible on Linux and macOS. Win64 passes only
four arguments in registers, which puts this one first on the stack, where four
bytes of stale stack were read as the top half of the length. Every remote
control command therefore arrived with a nonsense size.

One difference from Unix worth knowing. A Unix socket is a filesystem object and
carries its permissions, and kitty additionally checks the peer's uid. A loopback
TCP port has neither: any process on the machine that can reach the port can
drive kitty. Set `remote_control_password` if that matters for your setup. The
Windows equivalent of a Unix socket's ACL is a named pipe, which kitty already
uses for the graphics bypass, and that is the shape a stricter transport would
take if one is wanted later.

## Kittens run as kitten.exe, not through Python

Most kittens exist twice in this tree: an older Python implementation under
`kittens/`, and the Go one that `kitten.exe` provides. kitty chooses between them
with `wrapped_kitten_names()`, whose contents are baked into the build as the
`WRAPPED_KITTENS` define.

That define was being dropped on Windows, so the list arrived empty and every
kitten went down the Python path. The result was confusing rather than obviously
broken: kittens whose Python version still works here, such as `ask` behind tab
renaming and the paste confirmation, ran but in a reduced form, missing things
like arrow key navigation. Kittens whose Python version needs more terminal
machinery than exists here, `hints` and `unicode_input`, did nothing at all, so
`ctrl+shift+e` and `ctrl+shift+u` appeared dead.

The cause was in `setup.py`. `get_source_specific_defines` compares the source
path against literals written with forward slashes, while `find_c_files` builds
those paths with `os.path.join`, so on Windows they arrive as
`kitty\data-types.c` and match nothing. Every define it hands out was lost:
`WRAPPED_KITTENS` and `KITTY_VCS_REV` from `data-types.c`, the version numbers
from `screen.c`, `HAS_COPY_FILE_RANGE` from `fast-file-copy.c`, and the whole
`library_paths` lookup. It went unnoticed because `wincompat/win_prelude.h`
carries an `#ifndef WRAPPED_KITTENS` fallback of `""`, which turned a missing
define into an empty list rather than a compile error.

Only `vt-parser-dump.c` escaped, because it is appended as a hardcoded forward
slash literal rather than through `os.path.join`, which is why `--dump-commands`
worked while everything around it did not.

## Configuration

kitty reads `%USERPROFILE%\.config\kitty\kitty.conf`, which expands to
`C:\Users\<user>\.config\kitty\kitty.conf`. That is `.config` in the
home folder, the same place kitty uses on Linux, rather than `%APPDATA%`. The
launcher resolves it in `get_config_dir` in `kitty/launcher/utils.h`, which
checks `XDG_CONFIG_HOME` first and then `~/.config`. Setting
`KITTY_CONFIG_DIRECTORY` overrides both.

The folder is not created for you. A working config, commented for the options
where Windows differs, is in `example/.config/kitty`. Copy `kitty.conf` and the
`kitty-themes` folder next to it into your own `.config\kitty`.

Config reloading works, so edits show up without a restart.

## Shells

Windows console programs get a pseudoconsole, which is how a Windows terminal normally talks to a shell.

MSYS2 and Cygwin shells are detected from their own directory and started through the pty bridge instead, so they run on a genuine Cygwin pty. This is what stops the cursor bouncing and the flicker you otherwise get from conhost redrawing on its own cadence. kitty also copies its terminfo entry into the MSYS2 tree the first time, because the MSYS2 runtime only turns on console support for native Windows programs when it can find the terminfo for `$TERM`.

To turn the bridge off and use a pseudoconsole for these shells too, set `KITTY_NO_CYGWIN_PTY=1` in the environment.

## What this port adds that other platforms do not have

Things here that exist only because the platform needed them. The internals doc explains each one.

- ConPTY spawning: Windows console programs run on a pseudoconsole, created at the real window size.
- The Cygwin pty bridge: MSYS2 and Cygwin shells run on a genuine Cygwin pty, which is what removes the cursor bouncing and flicker conhost causes. `KITTY_NO_CYGWIN_PTY=1` turns it off.
- terminfo installed into the MSYS2 tree, so native Windows programs run from those shells get keyboard input and the right size.
- A title bar drawn by kitty, with caption buttons, matching the terminal background.
- Windows 11 acrylic built as a Windows.UI.Composition effect graph, the same recipe WinUI's AcrylicBrush uses, so it matches Windows Terminal and keeps the material and the transparency when the window loses focus.
- `background_opacity` scaled by the factor WinUI applies, so the same number means the same thing here as in Windows Terminal.
- Desktop notifications as real Windows toasts.
- An "Open in kitty" entry in both the modern and the classic Explorer menus.
- An installer that adds a Start menu entry, a PATH entry through small forwarding shims, and an optional desktop shortcut.
- A 1 ms timer resolution request in kitty and in the bridge, which is where most of the typing latency went.
- The real monitor refresh rate, rather than an assumed 60 Hz.
- A side channel pipe that carries kitten output and the graphics protocol around conhost.
- Config reloading over a named Windows event, since there is no SIGUSR1.
- A default shell of pwsh, then powershell.exe, then cmd.
- Shell integration for pwsh and for cmd, alongside the usual zsh and bash through MSYS2.

## Windows defaults

An empty config gets these built in. An explicit setting in kitty.conf still wins.

- `window_padding_width 1 10 10 10`: a small top padding, because the title bar strip already spaces the top, and normal padding on the other three sides.
- `placement_strategy top-left`: the grid sits just under the title bar instead of floating in the centre.
- `confirm_os_window_close 0`: closing a window does not prompt, which is how Windows terminals behave.

## Building from source

You need the MSYS2 mingw64 toolchain, the mingw64 build of Python 3, and Go on PATH. Run the build from git-bash or an MSYS2 shell.

```
PATH="/c/msys64/mingw64/bin:$PATH" PYTHONUTF8=1 /c/msys64/mingw64/bin/python.exe setup.py build
```

Putting the mingw64 bin directory first matters. The compiler loads its own DLLs through PATH, and without it the build fails while working out the target architecture.

The build emits `.so` names and Windows imports `.pyd`, so copy both extensions afterwards. A stale `.pyd` is imported silently in preference to the `.so` you just built, which is a good way to test a binary that has none of your changes in it.

```
cp -f kitty/fast_data_types.so kitty/fast_data_types.pyd
cp -f kitty/glfw-win32.so kitty/glfw-win32.pyd
```

Run the result with `kitty\launcher\kitty.exe`.

That build is tuned to the CPU it was compiled on. Pass `--portable` for one
that runs anywhere, which is what the packaging scripts do, since a native
build crashes on an older CPU with an illegal instruction and no message.

## Building the installer

One command does everything. It builds kitty including the `.pyd` copy, builds both launchers and the Go `kitten` binary, assembles the self contained tree at `dist/kitty` with the Python standard library and every MinGW DLL the binaries need, smoke tests that tree with a clean PATH, builds and signs the Explorer shell extension, and compiles `dist/kitty-setup.exe` with Inno Setup.

```
./packaging/windows/make-installer.sh
```

There is no need to build separately first. `dist/install.log` records what the installer did.

## Where the Windows code lives

- `kitty/child.c` and `kitty/child.py`: spawning children, the pseudoconsole, resizing, and choosing the pty bridge for MSYS2 and Cygwin shells.
- `shell-integration/msys2/pty-bridge.py`: the bridge that runs those shells on a real Cygwin pty.
- `kitty/wincompat/`: small shims for the POSIX calls the rest of the code assumes, such as `pipe2` and `poll`.
- `glfw/win32_window.c`: the window, the custom frame, the caption buttons, keyboard translation, and transparency.
- `glfw/win32_acrylic.c`: the acrylic material, its composition tree, and the swapchain that carries the OpenGL output.
- `glfw/win32_init.c`: the main loop, timers, and the timer resolution request.
- `glfw/caption_icons.h`: the caption button icons as embedded pixels.
- `kitty/state.c`: reserves the title bar strip at the top of the OS window.
- `packaging/windows/`: the installer, the Explorer shell extension, and the PATH shims.
