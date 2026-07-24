# Kitty native Windows port, handover

Goal is running kitty on native Windows without WSL. This note records the state so the next session can continue without rebuilding context. No git operations were done.

## What works and is verified

1. The whole C core compiles on native Windows with mingw gcc. 50 of 50 core files in `kitty/`, minus a few peripheral or other platform files that are intentionally excluded (dnd.c, the simde AVX variants, systemd.c, macos_*).
2. The ConPTY child layer runs. `kitty/wincompat/conpty_poc.c` spawns cmd.exe through a real pseudoconsole and pumps its output.
3. The Windows event loop compiles and has a real runtime. `kitty/wincompat/wincompat.c` implements poll() over ConPTY pipes with PeekNamedPipe and over sockets with WSAPoll, plus pipe2, socketpair, kill, waitpid, pread, mmap and the rest.
4. The GLFW Win32 plus WGL backend runs. It was written from scratch against kitty's forked GLFW API. A standalone link of all GLFW sources plus a small test opened a real window with a working OpenGL 3.3 context, cleared it, swapped buffers and exited clean.

Every high risk unknown of the port is answered. The rest is assembly.

## Toolchain

- Compiler: mingw64 gcc 15.2 at `C:\msys64\mingw64\bin`.
- Python for the build: mingw python 3.14 at `C:\msys64\mingw64\bin\python.exe`.
- Go at `C:\Program Files\Go`.
- No `make`, so run `python setup.py ...` directly.
- Installed mingw packages: lcms2, xxhash. Not available for mingw: xkbcommon (shimmed for the two constants keys.c needs).

## Compile flags that matter

For the C core and wincompat:
`-mno-ms-bitfields -include kitty/wincompat/win_prelude.h -I kitty/wincompat -I kitty -I <mingw py include> -I .../harfbuzz -I .../freetype2 -I .../cairo`

For GLFW:
`-D_GLFW_WIN32 -D_WIN32_WINNT=0x0A00 -I glfw -I kitty`

GLFW link libraries:
`-lopengl32 -lgdi32 -luser32 -ldwmapi -lws2_32 -lshell32 -limm32`

## New files

Compatibility layer in `kitty/wincompat/`:
- win_prelude.h: force included first. Winsock before windows.h, NOMINMAX, POSIX to MSVCRT typedefs, name collision renames (mouse_event, POINT, WORD, Ellipse become kitty_*), undef of hyper and small, and about twenty libc stubs.
- poll.h, pwd.h, dlfcn.h, termios.h, fcntl.h, signal.h, and sys/{stat,socket,un,mman,ioctl,wait}.h.
- xkbcommon/xkbcommon.h: two keysym constants only.
- wincompat.c: the runtime implementations.
- conpty_poc.c: the proof that ConPTY spawn works.

GLFW Win32 backend in `glfw/`:
- Headers: win32_platform.h, win32_thread.h, win32_joystick.h, wgl_context.h.
- Sources: win32_init.c, win32_window.c, win32_monitor.c, win32_joystick.c, wgl_context.c.

## Edits to existing files

- `kitty/child.c`: added a Windows branch under `#ifdef _WIN32` with ConPTY open_pty, spawn, resize_pty, close_pty. The POSIX fork path is unchanged under `#ifndef _WIN32`.
- `setup.py`: get_binary_arch near line 430 now recognizes the PE magic. compile_glfw near line 1045 selects the win32 module on Windows.
- `glfw/glfw.py`: init_env has a win32 branch that adds the Windows link libraries.
- `glfw/source-info.json`: the win32 source list drops win32_time.c and win32_thread.c because those functions live in win32_init.c.
- `glfw/input.c` near line 1122: the IME branch now includes `_GLFW_WIN32`. The old `#else` referenced variables that do not exist in that function, a pre existing bug in a branch that was never built.

## Gotchas learned

- A block comment must not contain the two characters that close it. `F_*/` inside a comment silently ended the comment and broke fcntl.h.
- The GLFW platform state macros are inconsistent. WINDOW, LIBRARY_WINDOW, MONITOR, CURSOR and JOYSTICK do not carry a trailing semicolon because internal.h adds it. CONTEXT, LIBRARY_CONTEXT and LIBRARY_JOYSTICK must carry their own semicolon. The reference is glx_context.h and linux_joystick.h.
- kitty renamed GLFW cursor shapes to CSS style names, for example GLFW_TEXT_CURSOR and GLFW_EW_RESIZE_CURSOR.
- kitty replaced the GLFW key model. Keys are Unicode codepoints and functional keys are GLFW_FKEY_* starting at 0xe000. There is no GLFW_KEY_LAST.
- kitty does not export glfwPollEvents. It drives rendering through glfwRunMainLoop and the tick callback.

## What remains for a full kitty.exe

Stage 6, the setup.py C extension env for Windows:
1. Add the wincompat include and the force included prelude and `-mno-ms-bitfields` to the extension cflags on Windows.
2. Add the harfbuzz, freetype2 and cairo include paths.
3. Add `kitty/wincompat/wincompat.c` to the extension sources.
4. Replace the `gl` pkg-config lookup on Windows with a direct link to opengl32. Windows has no gl.pc.
5. Add the Windows link libraries to the extension and to the launcher.
6. Codegen needs `PYTHONUTF8=1` for the cp1252 read issue.

Stage 7, link and first launch:
1. The launcher is Go. `kitty/launcher/main.c` also has POSIX assumptions that need the same wincompat treatment. Confirm the Go build produces kitty.exe on Windows.
2. child.py needs a Windows path that calls the new open_pty and spawn instead of os.openpty.
3. The int fd from _open_osfhandle for the ConPTY pipes must reach the event loop so poll() can watch them.
4. Then run and debug until a window opens running cmd or powershell.

The hard parts are proven. Stage 6 and 7 are wiring and iterative runtime debugging.
