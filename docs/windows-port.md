# kitty on Windows

This is a native Windows build of kitty. It runs on Windows directly, with no WSL, no Cygwin, and no X server. Windows programs run inside a pseudoconsole (ConPTY) and the whole terminal is drawn with OpenGL, the same rendering path kitty uses on every platform.

## What works

- Window creation, OpenGL rendering, resizing, and per monitor DPI scaling.
- Any Windows console program as the shell. cmd.exe, Windows PowerShell, and pwsh all run. Pick one with the `shell` option in kitty.conf. pwsh is the only one that shows inline autosuggestions.
- Keyboard input, including the ctrl and alt shortcuts, the function keys, and the arrow keys. Right arrow accepts a PSReadLine suggestion because the arrow keys now reach the shell correctly.
- Tabs and OS windows. `ctrl+shift+t` opens a tab and `ctrl+shift+n` opens a window. Closing one OS window no longer takes the others down.
- Scrollback, mouse selection, copy, and paste.
- Transparency and background blur through the Windows compositor.
- A custom title bar that matches the terminal background, with rounded corners and the minimize, maximize, and close buttons drawn from real icons.
- Config loading, and the Windows port ships a few of its own defaults so an empty kitty.conf already looks right. See "Windows defaults" below.

## What does not work yet

- The overlay kittens, so `set_tab_title`, `resize_window`, `hints`, `unicode_input`, and the confirmation prompts do nothing. Two things block them. Their terminal UI opens the controlling terminal through `ctermid` and drives it with `termios` raw mode, and Windows has neither, so the shared kitten UI loop needs a Windows path built on the console handles instead. On top of that a few kittens, `ask` among them, are only stubs in Python now and rely on the Go `kitten` tool, which does not cross build because it leans on Unix only syscalls. Porting the kitten UI to the Windows console is the next large piece of work, on the scale of the blur.
- Uniform acrylic blur when the window is maximized. The blur is even in a normal window and turns lighter toward the edges once maximized. The reason and the planned fix are in `docs/decisions.md`.

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

The Go step is skipped on Windows and prints a warning about `data_generated.bin`, which is expected.

After a build, copy the extension so Python on Windows can import it:

```
copy kitty\fast_data_types.so kitty\fast_data_types.pyd
```

Run the result with `kitty\launcher\kitty.exe`.

## Layout of the Windows specific code

- `kitty/child.c`: ConPTY process spawning, the pseudoconsole, and resize.
- `kitty/wincompat/`: small shims for POSIX calls the rest of the code assumes, such as `pipe2` and `poll`.
- `glfw/win32_window.c`: the window, the custom frame, the caption buttons, keyboard translation, transparency, and blur.
- `glfw/caption_icons.h`: the caption button icons as embedded pixels.
- `kitty/state.c`: reserves the title bar strip at the top of the OS window.

For how each of these works and why they are built the way they are, read `docs/windows-internals.md`.
