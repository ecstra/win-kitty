# Decisions

Why some of the Windows choices are the way they are. Read this for the reasoning behind a choice. The code and the other docs say what the system does.

## Blur behind instead of acrylic

The undocumented acrylic accent, ACCENT_ENABLE_ACRYLICBLURBEHIND, is broken on Windows 11 for a plain Win32 or OpenGL window. It shows a flat tint, and DWM drops it when the window is maximized, which also caused a flash during the maximize animation. ACCENT_ENABLE_BLURBEHIND works in every state and is what the port uses. It has one tell. It gets lighter toward the edges of a maximized window, because the blur kernel runs out of samples past the screen edge. The proper fix is the DirectComposition host backdrop brush, the same acrylic material Windows Terminal uses, and that is left as a larger piece of work.

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
