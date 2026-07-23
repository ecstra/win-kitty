# Decisions

Why some of the Windows choices are the way they are. Read this for the reasoning behind a choice. The code and the other docs say what the system does.

## Real acrylic via the DWM system backdrop

The port uses DWMWA_SYSTEMBACKDROP_TYPE set to DWMSBT_TRANSIENTWINDOW, the genuine Windows 11 acrylic material, the same one Windows Terminal shows. It is requested untinted: kitty's own background_opacity stays in charge of the colour, as on every other platform. Handing the tint to DWM instead blends it with DWM's own maths and a saturation boost, which reads darker and warmer than the configured colour.

This reverses an earlier decision. The port previously used the accent policy, ACCENT_ENABLE_ACRYLICBLURBEHIND, which on Windows 11 24H2 renders a plain Gaussian blur no matter what tint alpha it is given, and which tapered off over the outermost 64 pixels because the blur kernel cannot sample past the window boundary.

The system backdrop had been rejected for one reason: it goes opaque the moment the window is deactivated. That turns out to be avoidable in four lines. The swap keys off the frame's active state, and that state is nothing more than the wParam DefWindowProc receives in WM_NCACTIVATE. Reporting active unconditionally keeps the material, and keeps the per pixel alpha that DWM otherwise also drops on deactivation. There is no cosmetic cost here because kitty draws its own caption, so nothing keys off the DWM active look.

Two claims in the earlier reasoning were wrong and are worth recording. Neither a composition render path nor package identity is required: the backdrop composes with an ordinary OpenGL redirection surface, and this port has no DirectComposition surface for it to conflict with. Windows Terminal does reach acrylic a different way, through DesktopAcrylicController in a composition tree, but that is one way to solve it and not the only one. The conclusion that real acrylic had to wait for the installer and packaging phase was therefore wrong; it needed a message handler.

Note DwmExtendFrameIntoClientArea is still not used to expose the material, for the reason in the empty region section below.

## Keeping WS_CAPTION and hiding the frame

Custom frame apps on Windows keep WS_CAPTION so the window still snaps, maximizes with an animation, and shows a taskbar menu, then hide the frame by handling WM_NCCALCSIZE. Dropping WS_CAPTION was tried. It did not stop DWM from painting a caption in the glass transparency mode, so it gained nothing and put the normal window behaviour at risk.

## Caption buttons as a child window

They began as a top level popup, which DWM cannot animate in step with the parent, so they jumped to the maximized position while the window was still animating. A WS_CHILD layered window moves as one surface with the parent, which removes the desync.

## Blur behind with an empty region for no blur transparency

Two other approaches failed first. DwmExtendFrameIntoClientArea, the sheet of glass trick, makes DWM paint its own caption over the custom one. Turning off DWM non client rendering to hide that caption forces the classic Windows 98 frame in every mode. DwmEnableBlurBehindWindow with an empty blur region gives per pixel transparency without touching the frame, so neither problem shows up.

## The maximized size is not saved as the window size

Closing a maximized window used to save the full screen size, so the next launch opened maximized and then restored to full screen. The port records the window size only while the window is not maximized, so it always holds the last real size to restore to.

## No close confirmation by default

Windows terminals close without a prompt. The default also mattered while the confirmation kitten was broken, because it left an empty config unable to close at all. The kitten works now, and the default stays 0 to match how Windows terminals behave. A user who wants a prompt can set confirm_os_window_close.
