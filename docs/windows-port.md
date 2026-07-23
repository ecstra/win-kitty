# kitty on Windows

This is a native Windows build of kitty. It runs on Windows directly, with no WSL, no Cygwin, and no X server. Windows programs run inside a pseudoconsole (ConPTY) and the whole terminal is drawn with OpenGL, the same rendering path kitty uses on every platform.

## What works

- Window creation, OpenGL rendering, resizing, and per monitor DPI scaling.
- Any Windows console program as the shell. cmd.exe, Windows PowerShell, and pwsh all run. Pick one with the `shell` option in kitty.conf. pwsh is the only one that shows inline autosuggestions.
- Keyboard input, including the ctrl and alt shortcuts, the function keys, and the arrow keys. Right arrow accepts a PSReadLine suggestion because the arrow keys now reach the shell correctly.
- Tabs and OS windows. `ctrl+shift+t` opens a tab and `ctrl+shift+n` opens a window. Closing one OS window no longer takes the others down.
- Scrollback, mouse selection, copy, and paste.
- Transparency, and `background_blur` gives the real Windows 11 acrylic material, held across focus changes.
- A custom title bar that matches the terminal background, with rounded corners and the minimize, maximize, and close buttons drawn from real icons.
- Config loading, and the Windows port ships a few of its own defaults so an empty kitty.conf already looks right. See "Windows defaults" below.

## What does not work yet

- The overlay kittens, so `set_tab_title`, `resize_window`, `hints`, `unicode_input`, and the confirmation prompts do nothing. Two things block them. Their terminal UI opens the controlling terminal through `ctermid` and drives it with `termios` raw mode, and Windows has neither, so the shared kitten UI loop needs a Windows path built on the console handles instead. On top of that a few kittens, `ask` among them, are only stubs in Python now and rely on the Go `kitten` tool, which does not cross build because it leans on Unix only syscalls. Porting the kitten UI to the Windows console is the next large piece of work, on the scale of the blur.

## Windows defaults

An empty config gets these built in, and an explicit setting in kitty.conf overrides them:

- `window_padding_width 1 10 10 10`: a small top padding because the title bar strip already spaces the top, and normal padding on the other three sides.
- `placement_strategy top-left`: the grid sits just under the title bar instead of floating in the centre.
- `confirm_os_window_close 0`: closing a window does not prompt, which is how Windows terminals behave.

## Building

You need a MinGW-w64 toolchain (the msys2 mingw64 gcc) and a Windows Python 3.

```
set PATH=C:\msys64\mingw64\bin;%PATH%
set PYTHONUTF8=1
python setup.py build
```

This builds the C extensions, both launchers and the Go `kitten` binary. Go
code generation runs on Windows like everywhere else and needs `go` on PATH.

After a build, copy both extensions so Python on Windows can import them. The
build emits `.so` names; Windows only imports `.pyd`, and a stale `.pyd` is
silently used instead of the thing you just built:

```
copy kitty\fast_data_types.so kitty\fast_data_types.pyd
copy kitty\glfw-win32.so kitty\glfw-win32.pyd
```

Run the result with `kitty\launcher\kitty.exe`.

## Installing

`packaging/windows/make-installer.sh`, run from MSYS2 or git-bash, does the
whole thing: it builds kitty (the step above, `.pyd` copy included), assembles
the self-contained tree at `dist/kitty` (bundling the python stdlib and the
MinGW DLL closure, with a clean-PATH smoke test), builds the shell extension,
and compiles `dist/kitty-setup.exe` with Inno Setup. There is no need to build
separately first.

```
./packaging/windows/make-installer.sh
dist/kitty-setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
```

The installer is `PrivilegesRequired=admin` and lands in `C:\Program Files\kitty`,
so it raises a UAC prompt; `dist/install.log` records what it did. Close any
running kitty first, and note `kitty\launcher\kitty.exe` in the source tree is a
separate dev build that the installer does not touch.

## Layout of the Windows specific code

- `kitty/child.c`: ConPTY process spawning, the pseudoconsole, and resize.
- `kitty/wincompat/`: small shims for POSIX calls the rest of the code assumes, such as `pipe2` and `poll`.
- `glfw/win32_window.c`: the window, the custom frame, the caption buttons, keyboard translation, transparency, and blur.
- `glfw/caption_icons.h`: the caption button icons as embedded pixels.
- `kitty/state.c`: reserves the title bar strip at the top of the OS window.

For how each of these works and why they are built the way they are, read `docs/windows-internals.md`.
