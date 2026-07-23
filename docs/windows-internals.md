# Windows internals

How the Windows specific pieces of the port work. For the reasoning behind the choices, read [decisions.md](decisions.md).

## Running Windows programs through ConPTY

Windows has no fork and no openpty, so the port uses the pseudoconsole API. `open_pty` in kitty/child.c calls CreatePseudoConsole with a size in cells and gets back an HPCON plus two pipes, one carrying the child output and one carrying its input. `spawn` then starts the child with CreateProcessW and passes the pseudoconsole through a process attribute, so the child sees it as its console.

The awkward part is the standard handles. If the parent has inheritable standard handles, which happens when kitty is launched from a console or with redirected output, the child inherits those instead of the pseudoconsole. The pseudoconsole then delivers only its startup handshake and goes quiet. The fix clears the standard handles around the spawn, calling SetStdHandle(NULL) for input, output, and error before CreateProcessW and restoring them after. child.c also marks kitty's own standard handles non inheritable once at startup.

Resize sends the new cell size to ResizePseudoConsole. The write and resize paths map a kitty file descriptor to the matching HPCON through `windows_pty_write_fd_for` and `windows_pty_resize_for`.

The pseudoconsole is created at the real window size rather than a fixed 80 by 24, so the shell lays out its first prompt correctly instead of wrapping until the next resize.

## Running MSYS2 and Cygwin shells on a real pty

conhost sits in the middle of a pseudoconsole and redraws whatever passes through it on its own frame cadence. It consumes the application's synchronized update markers and splits one logical redraw across frames. With a shell that repaints its line aggressively, which is what zsh does with syntax highlighting, autosuggestions, and a prompt like powerlevel10k, that shows up as the cursor bouncing, flicker, and a visible lag while typing.

For these shells the port takes conhost out of the data path. `cygwin_pty_bridge_cmd` in kitty/child.py recognises a Cygwin or MSYS2 program by finding `msys-2.0.dll` or `cygwin1.dll` next to the executable, then checks that the matching `python3.exe` and shell-integration/msys2/pty-bridge.py are both present. If anything is missing it returns None and the shell runs on a pseudoconsole as before. Setting `KITTY_NO_CYGWIN_PTY=1` forces that fallback.

The bridge runs under the MSYS2 Python, opens a genuine Cygwin pty, starts the shell on it, and pumps bytes between that pty and its own stdin and stdout, which are plain pipes back to kitty. The shell's escape codes reach kitty untouched, which is the same architecture mintty uses.

A few details make it work.

- Script paths are converted to POSIX form. The Python executable path is handed to CreateProcessW so the Windows form is right, but the script path is opened by the Cygwin Python itself, which reads `C:\x\y` as a relative path.
- Resize arrives in band. kitty is talking over pipes, so there is no ioctl to make. `apply_pty_resize` writes `ESC [ 8 ; rows ; cols t`, the XTWINOPS sequence, into the pipe, and the bridge intercepts it and calls TIOCSWINSZ on the pty. Because a read can split that sequence, the input pump holds back up to 16 trailing bytes that could be the start of one, and releases them once it knows.
- UTF-8 is forced. The MSYS2 runtime converts the output of native Windows programs from the console code page to the pty charset, so the bridge sets the console code page to 65001 and defaults LANG to a UTF-8 locale, which makes that conversion an identity.
- The bridge marks its own standard handles non inheritable. Otherwise every native program the shell starts inherits a copy of the output pipe write handle, and kitty never sees EOF, so the window will not close.
- Both pumps are non blocking with a short adaptive sleep. A parked Cygwin blocking read holds Cygwin internal locks in a retry cycle of 10 to 15 ms that stalls the other direction, so waiting that way costs more than polling.

kitty also installs its terminfo into the MSYS2 tree, in `ensure_msys2_terminfo`. The MSYS2 runtime only enables its console interop for native Windows programs, which is what gives them keyboard input and the right terminal size, when it can find the terminfo entry for `$TERM`. Without the entry, native programs run from the shell get neither.

One shell integration detail belongs to this path. The MSYS2 profile exports PS1, zsh keeps the export flag when it takes the variable over, and every child then inherits a prompt written in zsh syntax. shell-integration/zsh/kitty-integration clears that flag on Cygwin and MSYS2 with `typeset -g +x PS1 PROMPT`.

## Kittens

Kittens are VT programs, so they run over plain pipes rather than a pseudoconsole. Their escape codes then reach kitty untouched, and kitty sees EOF when the kitten exits, which is what closes its window.

Wrapped kittens spawn from `kitten.exe`, built as part of the normal build. If it is missing, boss.py falls back to running them through the launcher with `kitty.exe +runpy`, and that path needs `kitty_exe()` to return the name with the `.exe` suffix or the spawn fails with file not found.

Output takes a side channel. A kitten running inside kitty opens `\\.\pipe\kitty-gfx-<pid>`, writes the target window id as an 8 byte header, and then streams its output down that pipe, which kitty injects into the right window. kitty serves the pipe with one thread per connection and applies backpressure when a window's queue is full. This is how the TUI kittens and the graphics protocol both avoid conhost. tools/tty/tty_windows.go routes writes to the bypass whenever `KITTY_WINDOW_ID` and `KITTY_PID` are set and the process is on a console.

Kittens refuse to run outside kitty. `New()` in tools/tui/loop/api.go checks for `KITTY_WINDOW_ID` on Windows and exits with a message if it is not there. It exits rather than returning an error because several kittens use the loop before they check the error, and a returned error surfaced as a nil dereference.

Four kittens are not ported. dnd, panel, quick-access-terminal, and desktop-ui call `cli.NotImplementedOnWindows` at the top of their main and get `(not supported on Windows)` appended to their description in the `kitten` command list.

## The window frame

kitty draws its own title bar so the caption shares the terminal background and the acrylic surface. The window keeps the normal WS_CAPTION and resize styles, so Windows still handles snapping, the taskbar menu, and the maximize animation. Two message handlers remove the standard frame from view.

- WM_NCCALCSIZE returns the whole window as client area, so DWM draws no frame. When maximized it insets by the frame size so nothing spills off screen.
- WM_NCHITTEST reports the resize borders and a draggable caption strip along the top, so the borderless window still resizes and moves.

kitty/state.c reserves a strip at the top of the OS window for the title bar. The grid and any tab bar sit below it, and a border rectangle fills the strip with the background colour at the same opacity as the cells, so the title bar matches the body exactly.

## Caption buttons

The minimize, maximize, and close controls live in a child window layered over the top right of the frame. It is a WS_CHILD window with WS_EX_LAYERED, so the compositor animates it together with the main window through minimize, maximize, and restore instead of letting it snap ahead. The content is drawn with the GDI+ flat API into a surface of 32 bits per pixel and pushed with UpdateLayeredWindow, which gives per pixel alpha for the rounded corners and the hover fills. The glyphs are Material Symbols icons rasterized to pixels and embedded in glfw/caption_icons.h.

## Keyboard

kitty wants one key event per physical press, carrying either a functional key value (0xe000 and up) or the typed text. The Windows backend splits this across two messages.

- WM_KEYDOWN forwards the functional keys and any key pressed with ctrl, alt, or the Windows key, so shortcuts and control combinations reach kitty.
- WM_CHAR forwards ordinary typed text and drops the control characters that the key path already handled.

The keycode table maps virtual keys to kitty key values, and it has to be an `int` array rather than the stock GLFW `short`. kitty's functional key values sit above 0x7fff, so a signed short held them as negative numbers that sign extended to 0xffffXXXX on read. Those matched nothing and silently dropped Enter, Backspace, Tab, and the arrows. Widening the array to int fixed every functional key together.

## Transparency and acrylic

A plain OpenGL window on Windows is opaque whatever the framebuffer alpha is. Two DWM mechanisms are used together, and `background_blur` decides whether the second one runs.

- Always, for any translucent window, DwmEnableBlurBehindWindow with an empty blur region. It turns on per pixel alpha with no blur of its own, and it does not extend the window frame, so DWM never paints a caption over the custom one.
- With blur on, DWMWA_SYSTEMBACKDROP_TYPE set to DWMSBT_TRANSIENTWINDOW, the real Windows 11 acrylic material, requested untinted so `background_opacity` still decides the colour.

DWM drops both when the window is deactivated, turning the material into a flat neutral fill and the alpha opaque. windowProc answers WM_NCACTIVATE with wParam forced to TRUE, which is the whole of the frame active state as far as DWM is concerned, so both survive losing focus. lParam is passed as -1 to suppress the non client repaint.

Through a maximize, restore, or fullscreen transition DWM keeps the backdrop it sampled for the old bounds, so the acrylic lands showing a stale piece of wallpaper cropped to where the window used to be. `refreshBackdrop` forces a resample. Setting the attribute again to the value it already holds is a no op as far as DWM is concerned, so it clears the attribute first and the off and on pair is what makes DWM look again.

kitty's `background_opacity` is scaled before it reaches the window, in `platform_bg_alpha` in kitty/state.c. Windows Terminal renders through the WinUI AcrylicBrush, which never applies the configured opacity literally. Its GetTintOpacityModifier scales it by a factor taken from the tint's HSV value, roughly 0.862 for a background as dark as `#1e1e1e`, so what Terminal calls 80 percent is really about 69 percent. The same factor is applied here, reading the real background colour so a light theme gets its own factor rather than being thinned too far.

## Input latency

Windows gives a process that has not asked otherwise a timer granularity of 15.625 ms, and every wait shorter than that is rounded up to it. Measured in this port before it was fixed, the `poll()` sample interval, both of the pty bridge pump sleeps, and the main loop waits for `input_delay` and `repaint_delay` all took about 15.8 ms however little they asked for. A keystroke echo sitting readable in a pipe waited through three of those intervals before anything drew, which is tens of milliseconds that no frame rate counter can see.

Both processes now ask for 1 ms. glfw does it in `loadLibraries` and `_glfwPlatformInit` through winmm, loaded the way it loads its other optional entry points, and releases it in `_glfwPlatformTerminate`. The bridge does it in `raise_timer_resolution` with ctypes, which is ABI correct because 64 bit Cygwin uses the Microsoft x64 calling convention. The request has been per process since Windows 10 2004, so kitty raising its own does nothing for the bridge and each has to ask separately.

`poll()` in kitty/wincompat/wincompat.c has no single Windows primitive that covers both anonymous pipes and sockets, so it samples PeekNamedPipe and WSAPoll in a loop. Whatever it sleeps between samples is latency, so the backoff is keyed off recent activity. It spins with SwitchToThread for a short window right after data, sleeps 1 ms while the terminal is still warm, and falls back to the old 5 ms interval once it is genuinely idle. Timeout accounting uses QueryPerformanceCounter, because GetTickCount64 advances in whole timer ticks and cannot express a 3 ms timeout. Removing the sampling altogether needs overlapped reads on the pipes, which means restructuring the child monitor.

Two smaller things sit alongside. The monitor refresh rate comes from EnumDisplaySettingsW rather than the hardcoded 60 the backend started with, which matters on a high refresh panel. Presentation sets the swap interval to 0 and paces on DwmFlush instead, because a vsynced SwapBuffers under DWM queues frames and adds latency. The main loop rounds its wait up rather than truncating, since at 1 ms resolution a wait shorter than a millisecond is common and truncating one to zero turns the sleep into a spin.

## Notifications

Desktop notifications are real Windows toasts. kitty/notifications.py registers a per user AppUserModelID so toasts are attributed to kitty, then shows the toast through PowerShell using the WinRT ToastNotificationManager. Live toasts cannot be enumerated from there, so the backend reports none outstanding.

## Clipboard and dropped files

Windows has an eager clipboard rather than the lazy selection ownership of X11 and Wayland, so kitty pulls its own data at the time of the copy and hands it over as CF_UNICODETEXT. There is no primary selection, so only GLFW_CLIPBOARD is honoured.

Drops arrive through OLE. Each window registers an IDropTarget, and on a drop the data is pulled out of the IDataObject synchronously and surfaced to kitty's platform independent drop API, with CF_HDROP file lists converted to `file://` URIs and CF_UNICODETEXT passed through as text. Dragging out of a window is not implemented.

## POSIX shims

kitty/wincompat/ fills in the POSIX surface the rest of the code assumes. Beyond `poll()` it covers `pipe2`, which puts both ends in PIPE_NOWAIT so a read on an empty pipe returns instead of blocking, `socketpair` built over a loopback connection, positioned reads and writes, `strndup`, `posix_memalign`, and `arc4random_buf` over RtlGenRandom. The headers under that directory stand in for the Unix ones the sources include, such as termios.h and sys/, so the same sources compile unchanged.

Config reloading needs one of these too. Windows has no SIGUSR1, so a reload request, such as the themes kitten applying a theme, is delivered by signalling a named event that a waiter thread in kitty/child-monitor.c turns into the same reload flag the signal would have set.

## Launchers and PATH shims

There are two launchers. `kitty.exe` is a GUI subsystem binary, so starting it from Explorer or a shortcut opens no console window. `kitty-console.exe` is the console subsystem twin, used when you want subcommand output.

The GUI launcher still tries to write to a console when it has one. `ensure_working_stdio` records which of the three standard streams are unusable, checking that each handle is neither null nor invalid and that GetFileType recognises it, then calls AttachConsole(ATTACH_PARENT_PROCESS) and reopens the unusable ones on CONIN$ and CONOUT$. The check has to happen before the attach, because attaching changes what the handles look like.

What goes on PATH is `<install>\bin`, which holds two tiny statically linked shims that forward their command line to the real binary and mirror its exit code. The real executables live in `<install>\kitty\launcher` next to their MinGW runtime DLLs, and putting that directory on PATH exposes those DLLs to every other program on the system. It broke other MinGW toolchains, whose compilers resolve their runtime DLLs through PATH and picked up mismatched copies.

## Packaging

packaging/windows/make-dist.sh assembles a relocatable tree at `dist/kitty`. It builds kitty and both launchers, copies the `.so` files to `.pyd`, mirrors the source layout because the launcher resolves everything relative to itself, bundles the Python standard library under `pylib`, walks the import table of every binary with objdump to copy in the MinGW DLL closure, and then smoke tests the result with a cleaned PATH so a missing DLL fails the build rather than the user.

make-installer.sh runs that, builds the shell extension, and compiles the Inno Setup script. The installer requires administrator rights and installs to `C:\Program Files\kitty`. It writes the PATH entry to the system environment, and it migrates away the older entry that pointed at the launcher directory.

The Explorer entry is built twice, because Windows 11 and Windows 10 read different things. Windows 10 gets classic registry verbs under HKLM for Directory, Directory\Background, and Drive. Windows 11 ignores those in its main menu and wants a package, so the entry there comes from a sparse MSIX that carries an IExplorerCommand implementation written in plain C. make-shellext.sh compiles that DLL, stages the package, packs it with makeappx, and signs it with a self signed certificate it creates once. The installer adds the certificate to the machine Root and TrustedPeople stores and registers the package with Add-AppxPackage and an external location. Registering over an identical version is rejected, so it removes the package first and adds it again.

## Icons and closing windows

The taskbar and window icon is set from a real PNG at startup. When a window closes, the port destroys those icon handles with DestroyIcon. An earlier version freed them with `free`, which corrupts the heap because an HICON is a handle and not heap memory, and that crash took down every OS window when you closed one of several.
