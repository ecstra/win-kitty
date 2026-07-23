# kitty on Windows

This is a native Windows build of kitty. It runs on Windows directly, with no WSL, no X server, and no Cygwin layer under the terminal itself. Windows console programs run through the pseudoconsole API (ConPTY). MSYS2 and Cygwin shells run on a real Cygwin pty instead, which keeps conhost out of the way. Everything is drawn with OpenGL, the same rendering path kitty uses on every other platform.

For how the pieces work, read [windows-internals.md](windows-internals.md). For why they are built that way, read [decisions.md](decisions.md).

## Installing

Run `dist/kitty-setup.exe`. It asks for administrator rights, because it installs for all users and writes to `C:\Program Files\kitty`.

The installer offers three optional tasks.

- Add kitty to your PATH: puts `<install>\bin` on the system PATH so `kitty` and `kitten` work in any terminal.
- Add "Open in kitty" to the Explorer right click menu: appears in the main menu on Windows 11 and in the classic menu on Windows 10.
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
- Transparency. With `background_blur` on you get the real Windows 11 acrylic material, and it stays put when the window loses focus.
- A custom title bar that matches the terminal background, with rounded corners and minimize, maximize, and close buttons.
- Kittens, run from inside kitty. icat, hints, unicode_input, themes, ask, diff, choose-files, and the confirmation prompts all work.
- Desktop notifications, shown as real Windows toasts.
- Config reloading, including a theme applied from the themes kitten showing up without a restart.
- Windows specific defaults, so an empty kitty.conf already looks right. See below.

## What is not there yet

- Four kittens do not run. dnd, panel, quick-access-terminal, and desktop-ui are marked `(not supported on Windows)` in the `kitten` command list and print a clear message if you run one anyway.
- Dragging content out of a kitty window. Dropping onto a window works, the other direction does not.
- Kittens refuse to start outside kitty. This is deliberate. On Windows a kitten needs kitty's own input and output path, and in another terminal it would misbehave in ways that look like bugs, so it exits with a message instead.
- Running `kitty --version` and other `kitty` subcommands from an interactive foreign shell can print nothing. Captured or redirected output is fine, and `kitten` is fine. If you need the output interactively, run `<install>\kitty\launcher\kitty-console.exe` instead.

## Shells

Windows console programs get a pseudoconsole, which is how a Windows terminal normally talks to a shell.

MSYS2 and Cygwin shells are detected from their own directory and started through the pty bridge instead, so they run on a genuine Cygwin pty. This is what stops the cursor bouncing and the flicker you otherwise get from conhost redrawing on its own cadence. kitty also copies its terminfo entry into the MSYS2 tree the first time, because the MSYS2 runtime only turns on console support for native Windows programs when it can find the terminfo for `$TERM`.

To turn the bridge off and use a pseudoconsole for these shells too, set `KITTY_NO_CYGWIN_PTY=1` in the environment.

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
- `glfw/win32_window.c`: the window, the custom frame, the caption buttons, keyboard translation, transparency, and acrylic.
- `glfw/win32_init.c`: the main loop, timers, and the timer resolution request.
- `glfw/caption_icons.h`: the caption button icons as embedded pixels.
- `kitty/state.c`: reserves the title bar strip at the top of the OS window.
- `packaging/windows/`: the installer, the Explorer shell extension, and the PATH shims.
