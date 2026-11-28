# Decisions

Why some of the Windows choices are the way they are. Read this for the reasoning behind a choice. The code and the other docs say what the system does.

## Real acrylic via a composition effect graph

The material is built as a Windows.UI.Composition effect graph, the same recipe WinUI's AcrylicBrush uses and therefore the same one Windows Terminal shows. A host backdrop brush feeds a luminosity blend that flattens contrast, a colour blend then lays the tint over it, and the noise texture WinUI ships is composited on top at two percent. Measured against Terminal over the same wallpaper the two match to a fraction of a percent in brightness, and their blur spread agrees to within a few percent.

This replaces the DWM system backdrop the port used before, DWMWA_SYSTEMBACKDROP_TYPE set to DWMSBT_TRANSIENTWINDOW. That is also real acrylic, but DWM renders it with maths of its own that cannot be matched to Terminal, and it needed two workarounds the effect graph does not. It went opaque on focus loss unless WM_NCACTIVATE reported the frame active unconditionally, and it showed a stale cropped piece of wallpaper after every maximize unless the attribute was toggled off and on to force a resample. Neither is needed now.

The recipe is reproduced through IGraphicsEffectD2D1Interop, a small COM object the file implements by hand rather than pulling in Win2D. Two details are load bearing. The flood colours go across straight and the composition engine premultiplies them, so premultiplying them again reads far too dark. And the two blend modes are passed by their raw D2D values, 22 and 23, because the WinUI enum names for Colour and Luminosity are swapped upstream, so the value that reads as Colour means luminosity here. Correcting them turns the window greyscale.

The effect graph runs against mingw, which is the port's toolchain. Its headers carry only a fraction of the composition interfaces and none of the D2D effect class ids, so the file declares the five interfaces and five class ids it needs. Direct2D has no C bindings at all, but its enums and property indices compile as C and that is all the graph uses from it. Nothing calls into D2D and nothing links against d2d1.

## The window has no redirection surface

A composition tree always draws above the window's redirection surface, so an acrylic sprite on an ordinary window would cover the terminal. The window takes WS_EX_NOREDIRECTIONBITMAP so it has no such surface, and every pixel comes from the composition tree instead: the acrylic underneath, the OpenGL output above it in a composition swapchain. This is the shape Windows Terminal uses, and for the same reason.

OpenGL reaches the swapchain through WGL_NV_DX_interop2, an extension AMD and NVIDIA ship and Intel does not reliably. kitty renders through bind_framebuffer_for_output, which rebinds framebuffer 0 every frame, so the renderer is untouched. Each present blits framebuffer 0 into a D3D texture shared with GL, flipping Y because OpenGL puts the origin at the bottom left and a DXGI texture puts it at the top left, and D3D then copies that texture into the swapchain's current back buffer. When the interop extension is missing the window falls back to glass, transparent with no material.

GL used to be handed the swapchain's back buffer itself, registered once. A flip model swapchain has two buffers that take turns, and buffer 0 names whichever is current. D3D follows that as the buffers turn, but the interop registration pinned the one buffer that was current when it was made. Read back on an NVIDIA laptop, GL drew every frame into that one buffer, so every other present showed the other, empty buffer, and the window showed the acrylic with no text and no cursor. Registering the back buffer again after each present would also work, but registration is the slow part of the interop and the shared texture costs one copy on the GPU per frame.

Windows.UI.Composition also refuses to activate on a thread with no DispatcherQueue, and reports that as a bare access denied from RoActivateInstance. One queue is made for the thread and never released, because releasing it when a window closes leaves the next window unable to make another.

## The swapchain content is never scaled

The content surface brush is CompositionStretch.None, top left aligned. The swapchain buffer and the composition visual sit on different clocks, so a live resize leaves them disagreeing by a frame, and the default Fill stretch turned that into the terminal content pulsing larger and smaller for the length of the drag. With no stretch a stale frame simply does not reach the new edge, and the acrylic shows through there for the one frame before kitty repaints. DXGI_SCALING_NONE on the swapchain would have been the other lever, but a composition swapchain rejects it with DXGI_ERROR_INVALID_CALL.

The swapchain also queues at most one frame, through IDXGIDevice1 SetMaximumFrameLatency. The default is three, and during a resize both the size events and the repaint timer present rapidly, so the compositor showed frames a few behind. While the prompt reflowed that read as the cursor jittering between recent positions.

## The live resize repaint uses a multimedia timer

SetTimer raises any interval below USER_TIMER_MINIMUM, ten milliseconds, up to that floor, and WM_TIMER coalescing pulls it lower, so a repaint driven by it was pinned near eighty frames a second on a faster panel. DwmFlush and the composition swapchain present were both measured at the full refresh rate, so the timer was the only thing in the way. The repaint runs off timeSetEvent at one refresh period instead, which fires below the ten millisecond floor on its own thread and posts a message the modal resize loop pumps, coalesced so a slow frame never backs the queue up. SetTimer stays as the fallback when winmm is unavailable.

## Matching the Windows Terminal tint strength

Terminal renders acrylic through the AcrylicBrush, whose GetTintOpacityModifier never applies the configured opacity literally. It scales the opacity by a factor derived from the tint's brightness, about 0.86 for a background as dark as the default, so its eighty percent is really nearer sixty nine. The same modifier lives in the effect graph here, so with blur on the material carries the same discounted tint and kitty paints no background of its own. platform_bg_alpha returns zero on that path so the default cells stay transparent and the material shows through. With blur off there is no material, kitty paints its own background, and platform_bg_alpha applies the same modifier so the number still means what Terminal means by it. Without the discount kitty is visibly the darker of the two at an identical setting.

## Running MSYS2 and Cygwin shells on a Cygwin pty

These shells run through a bridge on a real Cygwin pty rather than on a pseudoconsole. conhost redraws whatever passes through it on its own frame cadence, eats the synchronized update markers an application sends, and splits one logical redraw across frames. A shell that repaints its line aggressively, which is what zsh with syntax highlighting, autosuggestions, and powerlevel10k does, then shows cursor bouncing and flicker that no amount of work on the kitty side can remove, because the damage happens before kitty sees the bytes.

Running the shell on a Cygwin pty and passing the bytes over plain pipes is the architecture mintty uses, and it makes those artifacts disappear. The cost is a second process and a dependency on the MSYS2 Python, so the bridge is used only when every piece it needs is present and `KITTY_NO_CYGWIN_PTY=1` turns it off.

## The bridge drops stale mouse motion

Writing to the cygwin pty master blocks until the child drains it, and it does that even with O_NONBLOCK set, which cygwin ignores for this write. So a child that consumes input slowly stalls the input pump, and every stall lets more input pile up, which makes the next write block longer again. Measured with the mouse-demo kitten, a single write grew from 118ms to 2.6 seconds as the backlog went from 15 mouse reports to 241, and the window appeared to hang every few mouse movements. Only msys and cygwin shells show it, because only they go through the bridge.

Buffering the overflow was tried first and changed nothing, since the block is in the write itself rather than in a retry loop around it. What works is not sending the child input it would only throw away. When several mouse motion reports are queued, all but the newest are dropped, because only the current pointer position means anything. Presses, releases and scroll are never dropped.

The cost is that the bridge now reads the stream rather than only relaying it. It already did that for the resize sequence, and this is the second exception, so the file is no longer a byte for byte pipe. The risk is that the pattern it matches, ESC [ < digits ; digits ; digits and M or m, could in principle appear in pasted binary input and be dropped. A transparent relay cannot fix this though, because deciding that a run of positions is obsolete requires knowing they are mouse motion.

## Kittens over pipes rather than a pseudoconsole

A kitten is a VT program talking to kitty, so there is nothing for a pseudoconsole to do except get in the way. Running kittens on one made conhost rewrite their escape codes, which showed as flicker, and left kitty without an EOF when the kitten exited, so its window stayed open. Plain pipes fix both.

## Kittens refuse to run outside kitty on Windows

A kitten started from another terminal exits with a message instead of trying. On Windows it depends on kitty's own input and output path, and without it the failures are strange rather than obvious, such as a mouse that emits junk on movement or a full screen kitten that draws nothing. Making each of those work in a foreign console is a large amount of work for something nobody asked for, and a clear refusal is more useful than a partial success.

It exits rather than returning an error because several kittens use the loop before checking the error they were handed, so returning one surfaced as a nil dereference inside the kitten.

## Asking for a 1 ms timer resolution in both processes

Windows rounds every wait shorter than 15.625 ms up to that, for a process that has not asked otherwise, and the port waits on the keystroke path in three places. Raising the resolution is the whole fix for most of the port's typing latency.

kitty and the pty bridge each ask separately, which looks redundant and is not. The request has been per process since Windows 10 2004, and a test with a helper process holding 1 ms confirmed the Cygwin side still measured 15.5 ms until it asked for itself.

The alternative was to leave the resolution alone and shorten the sleeps, which does nothing, since the rounding is applied to whatever is asked for.

## Anything packaged is built without -march=native

setup.py's build action compiles with `-march=native -mtune=native`, which is
right for a local build and wrong for anything anyone else runs. The binary
uses whatever instructions the build machine happened to support, so on an
older CPU it dies with 0xC000001D, illegal instruction, and no message at all.
It is a confusing failure to receive: `kitty --version` still works, because
the launcher answers that itself before loading the extension that crashes.

make-dist.sh passes `--portable` for that reason. Upstream reaches the same
conclusion in its linux-package and linux-freeze actions, which build with
native optimizations off, and the Windows packaging simply never did the same.

This was not a CI problem, though CI is where it surfaced. An installer built
on any machine would crash on a CPU older than that machine's, so every package
produced before this was safe only by luck.

## Static shims are what goes on PATH

`<install>\bin` holds two small statically linked forwarders rather than the real executables. An earlier version put `<install>\kitty\launcher` on PATH directly, which also exposed the MinGW runtime DLLs sitting next to the binaries to every other process on the system. It broke unrelated MinGW toolchains, whose compilers resolve their own runtime DLLs through PATH and loaded mismatched copies, with a failure that looks nothing like a PATH problem.

## The kitty shim on PATH is a console program

kitty typed in a terminal runs the shim in `<install>\bin`, which used to be a GUI program so that no console would flash. PowerShell does not wait for a GUI program or read its output, so `kitty +list-fonts` printed nothing there, or printed under a prompt already drawn. A console shim is waited for and read by every shell. Run would show a console for it, so the installer registers the GUI launcher in App Paths, which Run reads before PATH, and the Start menu, the desktop shortcut and Explorer already open the GUI launcher directly.

## The installer installs for the current user by default

Setup asks whether to install for me only or for everyone, and picks me only, which needs no administrator rights, as VS Code's installer does. Most people install a terminal for themselves, and an admin prompt for that was a hurdle with nothing behind it. Installing for everyone stays one click away. An upgrade keeps the earlier choice, so the installs from before, which were for everyone, upgrade in place. The one thing a current user install cannot have is the main Windows 11 menu entry, whose package needs its certificate trusted for the whole machine, so it gets the classic entry under Show more options.

## A file dragged out of kitty is copied, never moved

Dropping a link to a file on Explorer offers the file itself, and a move would take it away from where its owner keeps it, a surprise no terminal drag should cause. So a drag that carries files allows copy and link only.

## The Windows 11 Explorer entry needs a package

Classic registry verbs under HKLM are enough on Windows 10, and Windows 11 shows them only under "Show more options". Its main menu reads packaged commands, so the entry there comes from a sparse MSIX carrying an IExplorerCommand implementation, signed with a certificate the build creates once and the installer trusts. This is heavier than a registry key and there is no lighter option that puts an entry in the menu users actually see.

## Keeping WS_CAPTION and hiding the frame

Custom frame apps on Windows keep WS_CAPTION so the window still snaps, maximizes with an animation, and shows a taskbar menu, then hide the frame by handling WM_NCCALCSIZE. Dropping WS_CAPTION was tried. It did not stop DWM from painting a caption in the glass transparency mode, so it gained nothing and put the normal window behaviour at risk.

## Caption buttons follow the redirection surface

They are a WS_CHILD layered window in every mode except acrylic. A child composites into its parent's redirection surface and moves as one surface with it, so DWM animates it in step through minimize and maximize where a top level popup jumps ahead to the final position. The acrylic path removes the redirection surface, so a child there has nowhere to draw and the buttons vanish. On that path they are an owned popup, which carries its own surface. The popup cannot desync from the parent animation because the acrylic path disables those animations anyway, with DWMWA_TRANSITIONS_FORCEDISABLED, since they are DWM scaling a snapshot the material cannot follow.

## Blur behind with an empty region, for the glass mode

Transparent with blur off still uses DwmEnableBlurBehindWindow with an empty blur region, which gives per pixel alpha with no blur of its own and without extending the window frame. A plain Win32 window is opaque whatever the framebuffer alpha is, so it is still needed there. The blur mode does not use it, because that path has no redirection surface and takes its transparency from the composition tree. Two other approaches failed first. DwmExtendFrameIntoClientArea, the sheet of glass trick, makes DWM paint its own caption over the custom one. Turning off DWM non client rendering to hide that caption forces the classic Windows 98 frame in every mode.

## The maximized size is not saved as the window size

Closing a maximized window used to save the full screen size, so the next launch opened maximized and then restored to full screen. The port records the window size only while the window is not maximized, so it always holds the last real size to restore to.

## No close confirmation by default

Windows terminals close without a prompt. The default also mattered while the confirmation kitten was broken, because it left an empty config unable to close at all. The kitten works now, and the default stays 0 to match how Windows terminals behave. A user who wants a prompt can set `confirm_os_window_close`.

## No primary selection, as on macOS

Windows has no primary selection. The port used to tell kitty it had one, and then answered every read of it with the clipboard, so a program allowed to read the primary selection over OSC 52 read the clipboard instead, and writing it did nothing. Keeping a primary selection inside kitty would make a middle click paste the last selection, which no Windows program does. So Windows takes macOS's model, where pasting the selection pastes the clipboard, which is what a middle click already did here.

## The left Ctrl and Alt run a shortcut when one is set, and type otherwise

Windows types with the left Ctrl and Alt held together as if they were AltGr, on any layout with characters on its AltGr level. Treating that as AltGr swallowed shortcuts. On US International, for one, kitty's own kitty_mod+alt+t typed Þ instead of opening the tab title prompt. Treating the left keys as a shortcut only, as kitty on Linux does, broke the other way. Scripts and tools that type text press the plain Ctrl and Alt keys for an AltGr character, so `user@host` typed by one on a German layout lost its @. So the port does what Windows Terminal does. The left keys run a kitty shortcut when one is set on that key, and type the character when none is. The AltGr key always types, as on Linux.

## A key's alternate is the US key at its place, not the default layout's

kitty on Linux takes a key's alternate from libxkbcommon's default keymap, a 105 key US keyboard whatever layouts the user has, and the port gives the same key, so a program and kitty's shortcuts see one key on both. Taking it from the Windows default layout instead was tried, and on a machine whose default layout is Russian it left the С key with no ASCII alternate, so Ctrl+С lost its `^C`. The key beside the left Shift is `<` in that keymap, where Windows' own US layout has a backslash. Taking Windows' backslash made Ctrl with the í of Hungarian send `^\`, a quit signal in a Unix shell.

## Ctrl+click opens a link, alongside upstream's Ctrl+Shift+click

Windows Terminal and the GNOME terminals open a link on Ctrl+click, so that is the binding a Windows user tries first. Upstream binds Ctrl+Shift+click instead, which is left in place, as is the plain click. The two are added rather than swapped, because they use different triggers and nothing else was bound to Ctrl+click.

The press is discarded in grabbed mode, mirroring what upstream does for Ctrl+Shift. The release is what opens the URL, so a program holding the mouse must not also receive the press, or it acts on the click as well.

## A window's programs are its job, not its process tree

Windows has no foreground process group, so something has to stand in for it. Walking parent links, as macOS does, fails for MSYS2 and Cygwin shells, where every exec starts a new Windows process whose parent link points at one already gone, and it loses any program whose parent exited. Each window's child already runs in a job object, so that job is the set of the window's programs, the way upstream's cgroup per window is on Linux. Which one is in front comes from shell integration, the shell at its prompt and otherwise the newest program, ranked by start time, since Windows pids say nothing of age. WezTerm picks the newest program too, without the prompt. A program started into a window of its own, such as `start notepad`, can then show as the one in front, which upstream and WezTerm share.

## The session is saved when Windows says it ends

Windows asks before a sign out, and any program can still cancel then, so a save at the asking would have to be thrown away again, the path Linux takes. Microsoft's docs ask for cleanup at WM_ENDSESSION instead, once the end is certain, and kitty saves there. The save has to finish before kitty returns from that message, since Windows may end it the moment it does.

## Only slangc.exe and slang-compiler.dll ship

The Slang release is 121 MB, most of it an LLVM backend and graphics libraries kitty never uses. What kitty compiles at runtime, Slang modules and GLSL, needs only `slangc.exe` and `slang-compiler.dll`, which built all 19 builtin pipelines on their own, 25.6 MB on disk and about 9 MB in the installer. Upstream's own bundles also carry the GLSL modules, which change nothing kitty can reach, since it compiles Slang without `-allow-glsl`.
