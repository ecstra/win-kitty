# Decisions

Why some of the Windows choices are the way they are. Read this for the reasoning behind a choice. The code and the other docs say what the system does.

## Real acrylic via the DWM system backdrop

The port uses DWMWA_SYSTEMBACKDROP_TYPE set to DWMSBT_TRANSIENTWINDOW, the genuine Windows 11 acrylic material, the same one Windows Terminal shows. It is requested untinted, so kitty's own `background_opacity` stays in charge of the colour as it does on every other platform. Handing the tint to DWM instead blends it with DWM's own maths and a saturation boost, which reads darker and warmer than the configured colour.

This reverses an earlier decision. The port previously used the accent policy, ACCENT_ENABLE_ACRYLICBLURBEHIND, which on Windows 11 24H2 renders a plain Gaussian blur no matter what tint alpha it is given, and which tapered off over the outermost 64 pixels because the blur kernel cannot sample past the window boundary.

The system backdrop had been rejected because it goes opaque the moment the window is deactivated. That turns out to be avoidable in four lines. The swap keys off the frame active state, and that state is nothing more than the wParam DefWindowProc receives in WM_NCACTIVATE. Reporting active unconditionally keeps the material, and keeps the per pixel alpha that DWM otherwise also drops on deactivation. There is no cosmetic cost, because kitty draws its own caption and nothing keys off the DWM active look.

Two claims in the earlier reasoning were wrong and are worth recording. Neither a composition render path nor package identity is required. The backdrop composes with an ordinary OpenGL redirection surface, and this port has no DirectComposition surface for it to conflict with. Windows Terminal does reach acrylic another way, through DesktopAcrylicController in a composition tree, but that is one way to solve it rather than the only one. The conclusion that real acrylic had to wait for the packaging phase was therefore wrong. It needed a message handler.

DwmExtendFrameIntoClientArea is still not used to expose the material, for the reason in the empty region section below.

## Matching the Windows Terminal tint strength

`background_opacity` is scaled by the same factor WinUI applies before it reaches the window. Terminal renders acrylic through the AcrylicBrush, whose GetTintOpacityModifier never applies the configured opacity literally, so the same number means something different in the two terminals. Without the discount kitty is visibly the darker of the two at an identical setting. The alternative was to leave the number literal and be honest about it, which is defensible, but a user comparing the two side by side reads the difference as a bug in the port rather than as a difference in what the number means.

## Running MSYS2 and Cygwin shells on a Cygwin pty

These shells run through a bridge on a real Cygwin pty rather than on a pseudoconsole. conhost redraws whatever passes through it on its own frame cadence, eats the synchronized update markers an application sends, and splits one logical redraw across frames. A shell that repaints its line aggressively, which is what zsh with syntax highlighting, autosuggestions, and powerlevel10k does, then shows cursor bouncing and flicker that no amount of work on the kitty side can remove, because the damage happens before kitty sees the bytes.

Running the shell on a Cygwin pty and passing the bytes over plain pipes is the architecture mintty uses, and it makes those artifacts disappear. The cost is a second process and a dependency on the MSYS2 Python, so the bridge is used only when every piece it needs is present and `KITTY_NO_CYGWIN_PTY=1` turns it off.

## Kittens over pipes rather than a pseudoconsole

A kitten is a VT program talking to kitty, so there is nothing for a pseudoconsole to do except get in the way. Running kittens on one made conhost rewrite their escape codes, which showed as flicker, and left kitty without an EOF when the kitten exited, so its window stayed open. Plain pipes fix both.

## Kittens refuse to run outside kitty on Windows

A kitten started from another terminal exits with a message instead of trying. On Windows it depends on kitty's own input and output path, and without it the failures are strange rather than obvious, such as a mouse that emits junk on movement or a full screen kitten that draws nothing. Making each of those work in a foreign console is a large amount of work for something nobody asked for, and a clear refusal is more useful than a partial success.

It exits rather than returning an error because several kittens use the loop before checking the error they were handed, so returning one surfaced as a nil dereference inside the kitten.

## Asking for a 1 ms timer resolution in both processes

Windows rounds every wait shorter than 15.625 ms up to that, for a process that has not asked otherwise, and the port waits on the keystroke path in three places. Raising the resolution is the whole fix for most of the port's typing latency.

kitty and the pty bridge each ask separately, which looks redundant and is not. The request has been per process since Windows 10 2004, and a test with a helper process holding 1 ms confirmed the Cygwin side still measured 15.5 ms until it asked for itself.

The alternative was to leave the resolution alone and shorten the sleeps, which does nothing, since the rounding is applied to whatever is asked for.

## Static shims are what goes on PATH

`<install>\bin` holds two small statically linked forwarders rather than the real executables. An earlier version put `<install>\kitty\launcher` on PATH directly, which also exposed the MinGW runtime DLLs sitting next to the binaries to every other process on the system. It broke unrelated MinGW toolchains, whose compilers resolve their own runtime DLLs through PATH and loaded mismatched copies, with a failure that looks nothing like a PATH problem.

## The Windows 11 Explorer entry needs a package

Classic registry verbs under HKLM are enough on Windows 10, and Windows 11 shows them only under "Show more options". Its main menu reads packaged commands, so the entry there comes from a sparse MSIX carrying an IExplorerCommand implementation, signed with a certificate the build creates once and the installer trusts. This is heavier than a registry key and there is no lighter option that puts an entry in the menu users actually see.

## Keeping WS_CAPTION and hiding the frame

Custom frame apps on Windows keep WS_CAPTION so the window still snaps, maximizes with an animation, and shows a taskbar menu, then hide the frame by handling WM_NCCALCSIZE. Dropping WS_CAPTION was tried. It did not stop DWM from painting a caption in the glass transparency mode, so it gained nothing and put the normal window behaviour at risk.

## Caption buttons as a child window

They began as a top level popup, which DWM cannot animate in step with the parent, so they jumped to the maximized position while the window was still animating. A WS_CHILD layered window moves as one surface with the parent, which removes the desync.

## Blur behind with an empty region for no blur transparency

Two other approaches failed first. DwmExtendFrameIntoClientArea, the sheet of glass trick, makes DWM paint its own caption over the custom one. Turning off DWM non client rendering to hide that caption forces the classic Windows 98 frame in every mode. DwmEnableBlurBehindWindow with an empty blur region gives per pixel transparency without touching the frame, so neither problem shows up.

## The maximized size is not saved as the window size

Closing a maximized window used to save the full screen size, so the next launch opened maximized and then restored to full screen. The port records the window size only while the window is not maximized, so it always holds the last real size to restore to.

## No close confirmation by default

Windows terminals close without a prompt. The default also mattered while the confirmation kitten was broken, because it left an empty config unable to close at all. The kitten works now, and the default stays 0 to match how Windows terminals behave. A user who wants a prompt can set `confirm_os_window_close`.
