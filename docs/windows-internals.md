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
- Superseded mouse motion is dropped. Writing to the pty master blocks until the child drains it whatever O_NONBLOCK says, so a slow child stalls the input pump and the backlog makes the next write block longer still. `coalesce_mouse_motion` forwards only the newest motion report and drops the ones it replaces. Presses, releases and scroll always go through. See `docs/decisions.md` for why this is done in the bridge.

kitty also installs its terminfo into the MSYS2 tree, in `ensure_msys2_terminfo`. The MSYS2 runtime only enables its console interop for native Windows programs, which is what gives them keyboard input and the right terminal size, when it can find the terminfo entry for `$TERM`. Without the entry, native programs run from the shell get neither.

One shell integration detail belongs to this path. The MSYS2 profile exports PS1, zsh keeps the export flag when it takes the variable over, and every child then inherits a prompt written in zsh syntax. shell-integration/zsh/kitty-integration clears that flag on Cygwin and MSYS2 with `typeset -g +x PS1 PROMPT`.

## Kittens

Kittens are VT programs, so they run over plain pipes rather than a pseudoconsole. Their escape codes then reach kitty untouched, and kitty sees EOF when the kitten exits, which is what closes its window.

Wrapped kittens spawn from `kitten.exe`, built as part of the normal build. If it is missing, boss.py falls back to running them through the launcher with `kitty.exe +runpy`, and that path needs `kitty_exe()` to return the name with the `.exe` suffix or the spawn fails with file not found.

Output takes a side channel. A kitten running inside kitty opens `\\.\pipe\kitty-gfx-<pid>`, writes the target window id as an 8 byte header, and then streams its output down that pipe, which kitty injects into the right window. kitty serves the pipe with one thread per connection and applies backpressure when a window's queue is full. This is how the TUI kittens and the graphics protocol both avoid conhost. tools/tty/tty_windows.go routes writes to the bypass whenever `KITTY_WINDOW_ID` and `KITTY_PID` are set and the process is on a console.

A TUI kitten owns the screen while it runs. The console it was started from never sees the kitten's UI, so resizing that console would have conhost repaint its own copy over it. kitty counts the side channel streams open to each window, and while one is open and the window has the in-band resize notification on, which only a full screen kitten asks for, `resize_pty` in kitty/child-monitor.c holds the console resize back and keeps the last size. The last stream's close applies it. The kitten still gets each size from the in-band notification, which conhost passes through to its input. A kitten that keeps its stream open while another program draws through the console, such as the ssh kitten, never turns that notification on, so its window resizes as usual.

Kittens refuse to run outside kitty. `New()` in tools/tui/loop/api.go checks for `KITTY_WINDOW_ID` on Windows and exits with a message if it is not there. It exits rather than returning an error because several kittens use the loop before they check the error, and a returned error surfaced as a nil dereference.

Four kittens are not ported. dnd, panel, quick-access-terminal, and desktop-ui call `cli.NotImplementedOnWindows` at the top of their main and get `(not supported on Windows)` appended to their description in the `kitten` command list.

## The window frame

kitty draws its own title bar so the caption shares the terminal background and the acrylic surface. The window keeps the normal WS_CAPTION and resize styles, so Windows still handles snapping, the taskbar menu, and the maximize animation. Two message handlers remove the standard frame from view.

- WM_NCCALCSIZE returns the whole window as client area, so DWM draws no frame. When maximized it insets by the frame size so nothing spills off screen.
- WM_NCHITTEST reports the resize borders and a draggable caption strip along the top, so the borderless window still resizes and moves.

Since the whole window is client area, a size or a position kitty asks for is the window's own, with no standard frame added around it. Adding one made a window grow by the frame on every restart with `remember_window_size` on. `resize_in_steps` is a WM_SIZING handler that moves the dragged edges in whole cells from the size the resize started at, as X11 window managers do with size increments.

kitty/state.c reserves a strip at the top of the OS window for the title bar. The grid and any tab bar sit below it, and a border rectangle fills the strip with the background colour at the same opacity as the cells, so the title bar matches the body exactly.

## Caption buttons

The minimize, maximize, and close controls live in a child window layered over the top right of the frame. It is a WS_CHILD window with WS_EX_LAYERED, so the compositor animates it together with the main window through minimize, maximize, and restore instead of letting it snap ahead. The content is drawn with the GDI+ flat API into a surface of 32 bits per pixel and pushed with UpdateLayeredWindow, which gives per pixel alpha for the rounded corners and the hover fills. The glyphs are Material Symbols icons rasterized to pixels and embedded in glfw/caption_icons.h.

## Keyboard

kitty wants one key event per physical press, carrying either a functional key value (0xe000 and up) or the typed text. The Windows backend splits this across two messages.

- WM_KEYDOWN forwards the functional keys and any key pressed with ctrl, alt, or the Windows key, so shortcuts and control combinations reach kitty.
- WM_CHAR forwards ordinary typed text and drops the control characters that the key path already handled.

A keypad key that types, a digit or an operator, would need both messages, so WM_KEYDOWN only notes which keypad key it was and the WM_CHAR that follows sends one event with that key and its text, the way X11 does. TranslateMessage posts the character before the key is dispatched, so the key handler looks in the queue for it and pairs only when one is there, sending the key alone as a shortcut when none follows. A key pressed with Alt alone is paired with WM_SYSCHAR the same way, but goes out as the key with no text, since kitty sends any event with text as the bare character, and with the character shift made as its shifted key, so Alt+Shift+S encodes as ESC S as on X11. With a dead key WM_SYSDEADCHAR comes instead and the key goes out alone. AltGr with a key that types a character is paired with WM_CHAR and goes out as that character, without the ctrl and alt Windows reports for AltGr. Windows also types with the left Ctrl and Alt as if they were AltGr, and scripts that type text press the plain Ctrl and Alt keys for an AltGr character. Those are paired the same way but keep their ctrl and alt, so kitty matches its shortcuts first, kitty_mod+alt+t among them, and types the text when none is bound, since kitty sends an event with text as the bare character. AltGr is told apart by the right Alt key being down. Forwarded from both messages, these reached the shell as an escape code and then the character again. Alt with keypad digits is left to Windows, which types the character the digits name once Alt comes up. The keypad's Enter and, with NumLock off, its navigation keys share virtual keys with the main ones and are told apart by the extended key flag, as are the left and right modifiers, which now arrive as keys of their own.

The keycode table maps virtual keys to kitty key values, and it has to be an `int` array rather than the stock GLFW `short`. kitty's functional key values sit above 0x7fff, so a signed short held them as negative numbers that sign extended to 0xffffXXXX on read. Those matched nothing and silently dropped Enter, Backspace, Tab, and the arrows. Widening the array to int fixed every functional key together.

## Transparency and acrylic

A plain OpenGL window on Windows is opaque whatever the framebuffer alpha is. `background_blur` picks between two very different paths.

With blur off the window stays an ordinary OpenGL window and uses DwmEnableBlurBehindWindow with an empty blur region. That turns on per pixel alpha with no blur of its own, and it does not extend the window frame, so DWM never paints a caption over the custom one.

With blur on the window builds the Windows 11 acrylic material itself, in glfw/win32_acrylic.c, as a Windows.UI.Composition effect graph. It is the recipe WinUI's AcrylicBrush uses and therefore the same one Windows Terminal shows: a host backdrop brush, a luminosity blend to flatten contrast, a colour blend for the tint, and the 256 by 256 noise texture WinUI ships at two percent. The graph is described to the compositor through IGraphicsEffectD2D1Interop, a COM object the file implements by hand. Measured against Terminal over the same wallpaper it matches to a fraction of a percent in brightness.

The material sits in a composition tree, which always draws above the window's redirection surface. So on the blur path the window takes WS_EX_NOREDIRECTIONBITMAP and has no redirection surface at all, and every pixel comes from the tree: the acrylic underneath, the OpenGL output above it in a composition swapchain. OpenGL reaches the swapchain through WGL_NV_DX_interop2. kitty renders through bind_framebuffer_for_output which rebinds framebuffer 0 every frame, so the renderer needs no changes. swapBuffersWGL blits framebuffer 0 into a D3D texture shared with GL, flipping Y, then D3D copies that texture into the swapchain's current back buffer and presents. WGL_NV_DX_interop2 is an AMD and NVIDIA extension that Intel does not reliably ship, so when it is missing the window drops to the glass path above.

The content surface brush is CompositionStretch.None, so the swapchain is drawn one to one and never scaled. The swapchain buffer and the visual are on different clocks and disagree by a frame during a live resize, and a stretch would turn that into the content pulsing. The swapchain queues at most one frame, through SetMaximumFrameLatency, so the cursor does not jitter between recent frames while the prompt reflows.

kitty's `background_opacity` never reaches the window literally. Windows Terminal renders through the WinUI AcrylicBrush, whose GetTintOpacityModifier scales the opacity by a factor taken from the tint's HSV value, roughly 0.862 for a background as dark as `#1e1e1e`, so what Terminal calls 80 percent is really about 69 percent. The same modifier lives in the effect graph, so with blur on the material carries the discounted tint and `platform_bg_alpha` in kitty/state.c returns zero, leaving the default cells transparent so the material shows through. With blur off there is no material and `platform_bg_alpha` applies the modifier to kitty's own background instead, so the number means the same thing either way.

## Input latency

Windows gives a process that has not asked otherwise a timer granularity of 15.625 ms, and every wait shorter than that is rounded up to it. Measured in this port before it was fixed, the `poll()` sample interval, both of the pty bridge pump sleeps, and the main loop waits for `input_delay` and `repaint_delay` all took about 15.8 ms however little they asked for. A keystroke echo sitting readable in a pipe waited through three of those intervals before anything drew, which is tens of milliseconds that no frame rate counter can see.

Both processes now ask for 1 ms. glfw does it in `loadLibraries` and `_glfwPlatformInit` through winmm, loaded the way it loads its other optional entry points, and releases it in `_glfwPlatformTerminate`. The bridge does it in `raise_timer_resolution` with ctypes, which is ABI correct because 64 bit Cygwin uses the Microsoft x64 calling convention. The request has been per process since Windows 10 2004, so kitty raising its own does nothing for the bridge and each has to ask separately.

`poll()` in kitty/wincompat/wincompat.c has no single Windows primitive that covers both anonymous pipes and sockets, so it samples PeekNamedPipe and WSAPoll in a loop. Whatever it sleeps between samples is latency, so the backoff is keyed off recent activity. It spins with SwitchToThread for a short window right after data, sleeps 1 ms while the terminal is still warm, and falls back to the old 5 ms interval once it is genuinely idle. Timeout accounting uses QueryPerformanceCounter, because GetTickCount64 advances in whole timer ticks and cannot express a 3 ms timeout. Removing the sampling altogether needs overlapped reads on the pipes, which means restructuring the child monitor.

Two smaller things sit alongside. The monitor refresh rate comes from EnumDisplaySettingsW rather than the hardcoded 60 the backend started with, which matters on a high refresh panel. Presentation sets the swap interval to 0 and paces on DwmFlush instead, because a vsynced SwapBuffers under DWM queues frames and adds latency. The main loop rounds its wait up rather than truncating, since at 1 ms resolution a wait shorter than a millisecond is common and truncating one to zero turns the sleep into a spin.

## Notifications

Desktop notifications are real Windows toasts. kitty/notifications.py registers a per user AppUserModelID so toasts are attributed to kitty, then shows the toast through PowerShell using the WinRT ToastNotificationManager. Live toasts cannot be enumerated from there, so the backend reports none outstanding.

## Clipboard and dropped files

Windows has an eager clipboard rather than the lazy selection ownership of X11 and Wayland, so kitty pulls its own data at the time of the copy and hands it over as CF_UNICODETEXT. There is no primary selection, so kitty treats Windows as it treats macOS, with `supports_primary_selection` off. Pasting the selection pastes the clipboard, and a program asking for the primary selection over OSC 52 gets nothing. The helper window listens for clipboard changes, and when the owner of the clipboard is no longer that window another program copied, which kitty hears as the clipboard being lost, for `clear_selection_on_clipboard_loss`.

Drops arrive through OLE. Each window registers an IDropTarget, and on a drop the data is pulled out of the IDataObject synchronously and surfaced to kitty's platform independent drop API, with CF_HDROP file lists converted to `file://` URIs and CF_UNICODETEXT passed through as text.

kitty's own drags, of a tab, a window, a selection or a link, go out through OLE as well, in `_glfwPlatformStartDrag` in glfw/win32_window.c. Each mime type travels as a clipboard format registered under its own name, served by an IDataObject of the port's own, and an IDropSource tells Escape, a cancel, from a release, a drop, so a release over something that accepts nothing reaches kitty as a drop outside every kitty window, which opens the tab in a new one. DoDragDrop runs a modal loop, so the drag starts from a message posted to glfw's hidden helper window rather than inside the Python call that asked for it. A repaint timer like the live resize's runs kitty's tick and its glfw timers during the loop, the one that switches tabs on a hover among them. Its ticks go to the helper window too, since kitty's tick can close any kitty window while the drag runs, and closing the one the drag started from cancels the drag, whose end then reaches kitty through any window left. The loop shares the main thread with rendering, so buffer swaps skip their DwmFlush while a drag runs and the loop is never held for a refresh. A tab or a window released outside every kitty window opens in a new one at the release point, kept inside that monitor's work area, since kitty opens it while it handles the drop. When the drag is kitty's own, the drop target hands kitty only the mime types and flags the drop as its own, since kitty already holds the data. The drag picture is a layered window of the port's own, since kitty swaps between two pictures during a drag. It lives on a thread of its own that moves it once per refresh, its lower left corner just above the pointer, so a slow drop target in another process, which the loop waits on, never holds it back. The data object also keeps what the shell's drag helpers store in it, and carries a transparent one pixel shell picture, so the taskbar and Explorer draw nothing of their own over kitty's. Only kitty reads the formats named after mime types, so a drag also goes out under the formats other programs read. `text/plain` becomes CF_UNICODETEXT. Each link of a `text/uri-list` goes out as text, the first web link also as UniformResourceLocatorW, which browsers open, and the links to files as CF_HDROP, which Explorer copies and editors open. kitty's Python writes a link to a file on this machine as `file:///C:/path` before the drag starts, an MSYS2 `/c/path` among them, in `windows_file_url` in kitty/utils.py, so the C side only reads that form. A drag that carries files allows copy and link but never move, so a link dropped on Explorer can never take the file away from where its owner keeps it. A drop target such as Explorer would otherwise copy the files inside its Drop call, which DoDragDrop on kitty's main thread waits for, so a question such as whether to replace a file held kitty with the drag picture stuck to the pointer. The data object offers IDataObjectAsyncCapability and keeps its own copy of the data, so Explorer copies in the background, and the drag picture goes the moment the button comes up. Drags that programs in a window start, the dnd kitten's side, are not implemented.

## POSIX shims

kitty/wincompat/ fills in the POSIX surface the rest of the code assumes. Beyond `poll()` it covers `pipe2`, which puts both ends in PIPE_NOWAIT so a read on an empty pipe returns instead of blocking, `socketpair` built over a loopback connection, positioned reads and writes, `strndup`, `posix_memalign`, and `arc4random_buf` over RtlGenRandom. The headers under that directory stand in for the Unix ones the sources include, such as termios.h and sys/, so the same sources compile unchanged.

Config reloading needs one of these too. Windows has no SIGUSR1, so a reload request, such as the themes kitten applying a theme, is delivered by signalling a named event that a waiter thread in kitty/child-monitor.c turns into the same reload flag the signal would have set.

## Launchers and PATH shims

There are two launchers. `kitty.exe` is a GUI subsystem binary, so starting it from Explorer or a shortcut opens no console window. `kitty-console.exe` is the console subsystem twin, used when you want subcommand output.

The GUI launcher still tries to write to a console when it has one. `ensure_working_stdio` records which of the three standard streams are unusable, checking that each handle is neither null nor invalid and that GetFileType recognises it, then calls AttachConsole(ATTACH_PARENT_PROCESS) and reopens the unusable ones on CONIN$ and CONOUT$. The check has to happen before the attach, because attaching changes what the handles look like.

What goes on PATH is `<install>\bin`, which holds two tiny statically linked shims that forward their command line to the real binary and mirror its exit code. The real executables live in `<install>\kitty\launcher` next to their MinGW runtime DLLs, and putting that directory on PATH exposes those DLLs to every other program on the system. It broke other MinGW toolchains, whose compilers resolve their runtime DLLs through PATH and picked up mismatched copies.

Both shims are console programs, since a shell waits for a console program and reads what it prints, while PowerShell neither waits for a GUI program nor reads its output. The kitty shim sends `--version`, `--help` and every `+` command but `+open` to `kitty-console.exe` and waits for it, so their output lands in the terminal before the prompt comes back. Anything else opens a window, so it goes to the GUI launcher and the shim returns at once. Run looks a name up in App Paths before PATH, and the installer registers the GUI launcher there, so kitty typed into Run opens the window with no console showing.

The full `kitty --help` comes from Python, which on Windows prints it straight to the console rather than through a pager, since Windows has no pager kitty can count on.

Some of these hand off to kitten, `kitty +kitten` with a kitten written in Go and `kitty @` in the launcher, `+complete`, `+hold` and `+shebang` in kitty/entry_points.py. On Unix they exec it. The CRT's exec on Windows starts the new program and ends the old one at once, with exit code 0 and its arguments unquoted, so a shell got its prompt back before kitten printed, lost its exit code, and saw an argument with a space as two. On Windows they start kitten as a child instead, with each argument quoted the way CommandLineToArgvW reads it, wait for it, and exit with its code. They and the shims ignore Ctrl+C while they wait, since it reaches the child too, which decides when it ends.

A file link from an MSYS2 or Cygwin shell names a path such as `/home/me` that only that shell's runtime can place, since its mounts decide it, and Git Bash and some MSYS2 installs mount `/tmp` on the Windows temp folder. So a drag looks for the runtime beside the window's shell, or in `usr\bin` under it as for Git Bash's `bin\bash.exe`, in `posix_path_converter`, and asks its `cygpath` for the Windows path. WSL's `/mnt/c` is the C: drive, and a WSL path outside `/mnt` stays a link, since kitty cannot tell which distribution printed it.

## Packaging

packaging/windows/make-dist.sh assembles a relocatable tree at `dist/kitty`. It builds kitty and both launchers, copies the `.so` files to `.pyd`, mirrors the source layout because the launcher resolves everything relative to itself, bundles the Python standard library under `pylib`, walks the import table of every binary with objdump to copy in the MinGW DLL closure, and then smoke tests the result with a cleaned PATH so a missing DLL fails the build rather than the user.

make-installer.sh runs that, builds the shell extension, and compiles the Inno Setup script. The installer requires administrator rights and installs to `C:\Program Files\kitty`. It writes the PATH entry to the system environment, and it migrates away the older entry that pointed at the launcher directory.

The Explorer entry is built twice, because Windows 11 and Windows 10 read different things. Windows 10 gets classic registry verbs under HKLM for Directory, Directory\Background, and Drive. Windows 11 ignores those in its main menu and wants a package, so the entry there comes from a sparse MSIX that carries an IExplorerCommand implementation written in plain C. make-shellext.sh compiles that DLL, stages the package, packs it with makeappx, and signs it with a self signed certificate it creates once. The installer adds the certificate to the machine Root and TrustedPeople stores and registers the package with Add-AppxPackage and an external location. Registering over an identical version is rejected, so it removes the package first and adds it again.

## Icons and closing windows

The taskbar and window icon is set from a real PNG at startup. When a window closes, the port destroys those icon handles with DestroyIcon. An earlier version freed them with `free`, which corrupts the heap because an HICON is a handle and not heap memory, and that crash took down every OS window when you closed one of several.
