# Windows internals

How the Windows specific pieces of the port work.

## Running programs through ConPTY

Windows has no fork and no openpty, so the port uses the pseudoconsole API. `open_pty` in kitty/child.c calls CreatePseudoConsole with a size in cells and gets back an HPCON plus two pipes, one carrying the child output and one carrying its input. `spawn` then starts the child with CreateProcessW and passes the pseudoconsole through a process attribute, so the child sees it as its console.

The awkward part is the standard handles. If the parent has inheritable standard handles, which happens when kitty is launched from a console or with redirected output, the child inherits those instead of the pseudoconsole. The pseudoconsole then delivers only its startup handshake and goes quiet. The fix clears the standard handles around the spawn: call SetStdHandle(NULL) for input, output, and error before CreateProcessW and restore them after. child.c also marks kitty's own standard handles non inheritable once at startup.

Resize sends the new cell size to ResizePseudoConsole. The write and resize paths map a kitty file descriptor to the matching HPCON through `windows_pty_write_fd_for` and `windows_pty_resize_for`.

The pseudoconsole is created at the real window size rather than a fixed 80 by 24, so the shell lays out its first prompt correctly instead of wrapping until the next resize.

## The window frame

kitty draws its own title bar so the caption shares the terminal background and the acrylic surface. The window keeps the normal WS_CAPTION and resize styles, so Windows still handles snapping, the taskbar menu, and the maximize animation. Two message handlers remove the standard frame from view:

- WM_NCCALCSIZE returns the whole window as client area, so DWM draws no frame. When maximized it insets by the frame size so nothing spills off screen.
- WM_NCHITTEST reports the resize borders and a draggable caption strip along the top, so the borderless window still resizes and moves.

kitty/state.c reserves a strip at the top of the OS window for the title bar. The grid and any tab bar sit below it, and a border rectangle fills the strip with the background colour at the same opacity as the cells, so the title bar matches the body exactly.

## Caption buttons

The minimize, maximize, and close controls live in a child window layered over the top right of the frame. It is a WS_CHILD window with WS_EX_LAYERED, so the compositor animates it together with the main window through minimize, maximize, and restore instead of letting it snap ahead. The content is drawn with the GDI+ flat API into a 32 bit per pixel surface and pushed with UpdateLayeredWindow, which gives per pixel alpha for the rounded corners and the hover fills. The glyphs are Material Symbols icons rasterized to pixels and embedded in glfw/caption_icons.h.

## Keyboard

kitty wants one key event per physical press, carrying either a functional key value (0xe000 and up) or the typed text. The Windows backend splits this across two messages:

- WM_KEYDOWN forwards the functional keys and any key pressed with ctrl, alt, or the Windows key, so shortcuts and control combinations reach kitty.
- WM_CHAR forwards ordinary typed text and drops the control characters that the key path already handled.

The keycode table maps virtual keys to kitty key values, and it has to be an `int` array rather than the stock GLFW `short`. kitty's functional key values sit above 0x7fff, so a signed short held them as negative numbers that sign extended to 0xffffXXXX on read. Those matched nothing and silently dropped Enter, Backspace, Tab, and the arrows. Widening the array to int fixed every functional key together.

## Transparency and blur

A plain OpenGL window on Windows is opaque whatever the framebuffer alpha is. Two DWM mechanisms make it translucent, and the port picks one from background_blur:

- Blur on: the accent policy with ACCENT_ENABLE_BLURBEHIND. It composites the per pixel alpha and blurs whatever sits behind the window, in every window state.
- Blur off, translucent background: DwmEnableBlurBehindWindow with an empty blur region. It turns on per pixel alpha with no blur, and it does not extend the window frame, so DWM never paints a caption over the custom one.

The first attempt at the no blur case extended the DWM frame across the client area, which made DWM draw a second standard caption, the phantom title bar. Turning off DWM non client rendering to hide it forced the classic Windows 98 style frame instead. Blur behind with an empty region sidesteps both.

## Overlay kittens

Kittens such as ask, resize_window, hints, and unicode_input run as a child program inside an overlay terminal window. There are no wrapped Go kittens on this build, so kitty runs each one through the launcher with `kitty.exe +runpy`. That path needs `kitty_exe()` to return the name with the .exe suffix, otherwise the spawn fails with file not found.

The spawn works now, but the kittens still do not render, for two reasons. Their UI loop in kittens/tui/loop.py opens the controlling terminal through `open_tty` in kitty/data-types.c, which calls `ctermid` and puts the terminal in raw mode with `termios`. Windows has neither, and the single read and write descriptor that Unix gives for /dev/tty does not map onto the separate CONIN$ and CONOUT$ console handles, so the loop needs a Windows path built on those handles and SetConsoleMode. Separately, a handful of kittens, ask included, are stubs in Python that defer to the Go `kitten` tool, and that tool does not cross build for Windows because it uses Unix only syscalls such as mmap and Mkdirat. Both of these are open work.

## Icons and closing windows

The taskbar and window icon is set from a real PNG at startup. When a window closes, the port destroys those icon handles with DestroyIcon. An earlier version freed them with `free`, which corrupts the heap because an HICON is a handle and not heap memory, and that crash took down every OS window when you closed one of several.
