# Decisions

Why some of the Windows choices are the way they are. Read this for the reasoning behind a choice. The code and the other docs say what the system does.

## Always-on Gaussian blur instead of acrylic

The port uses ACCENT_ENABLE_BLURBEHIND, a plain Gaussian blur that stays applied whether or not the window is focused. It is not the frosted acrylic that Windows Terminal shows, and it has one tell: it gets lighter toward the edges of a maximized window, because the blur kernel runs out of samples past the screen edge. Acrylic was pursued at length and every window level path fails on an unpackaged OpenGL window:

- The DWM system backdrop, DWMWA_SYSTEMBACKDROP_TYPE set to DWMSBT_TRANSIENTWINDOW, is real acrylic and looks correct while focused, but it goes opaque when the window is inactive (this is the acrylic material dropping, separate from the Mica inactive fallback), and it does not compose behind a DirectComposition surface.
- Both accent blur states, BLURBEHIND and ACRYLICBLURBEHIND, render the same Gaussian blur on Windows 11 24H2. The acrylic material never appears, with any tint alpha. Microsoft neutered the private acrylic accent for non UWP windows years ago.
- A separate borderless acrylic backdrop window pinned behind the main window shows real acrylic, but two independent top level windows cannot move in lockstep, so it trails the main window while dragging.

The transparency itself is not the problem. Per pixel alpha holds across focus changes for the glass mode. What cannot be kept always on is the acrylic effect.

Windows Terminal gets always on desktop acrylic because it is a packaged WinUI app whose acrylic and alpha both live in a composition tree via DesktopAcrylicController, where focus state cannot touch them, and it samples the desktop because it has package identity. wezterm uses the same DWM system backdrop this port tried and has the identical inactive opacity bug filed (wezterm issue 6979), so it is not a counterexample.

Matching Windows Terminal therefore needs two things kitty lacks: a composition based render path (kitty renders with OpenGL, so its frames must go through a DirectComposition swapchain, proven to hold alpha when inactive only while it keeps presenting) and package identity for the host backdrop brush. Both belong with the installer and packaging work, so real acrylic is deferred to that phase rather than bolted on now.

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
