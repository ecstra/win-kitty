//========================================================================
// GLFW 3.4 Win32 port for kitty - windows, input, event loop
//========================================================================

#include "internal.h"

#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>

#include "win32_acrylic.h"

#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

// ---------------------------------------------------------------------------
// Key translation: Win32 virtual key -> kitty functional key
// ---------------------------------------------------------------------------

static void createKeyTables(void) {
    memset(_glfw.win32.keycodes, 0, sizeof(_glfw.win32.keycodes));
    int* k = _glfw.win32.keycodes;
    k[VK_ESCAPE]    = GLFW_FKEY_ESCAPE;
    k[VK_RETURN]    = GLFW_FKEY_ENTER;
    k[VK_TAB]       = GLFW_FKEY_TAB;
    k[VK_BACK]      = GLFW_FKEY_BACKSPACE;
    k[VK_INSERT]    = GLFW_FKEY_INSERT;
    k[VK_DELETE]    = GLFW_FKEY_DELETE;
    k[VK_LEFT]      = GLFW_FKEY_LEFT;
    k[VK_RIGHT]     = GLFW_FKEY_RIGHT;
    k[VK_UP]        = GLFW_FKEY_UP;
    k[VK_DOWN]      = GLFW_FKEY_DOWN;
    k[VK_PRIOR]     = GLFW_FKEY_PAGE_UP;
    k[VK_NEXT]      = GLFW_FKEY_PAGE_DOWN;
    k[VK_HOME]      = GLFW_FKEY_HOME;
    k[VK_END]       = GLFW_FKEY_END;
    k[VK_CAPITAL]   = GLFW_FKEY_CAPS_LOCK;
    k[VK_SCROLL]    = GLFW_FKEY_SCROLL_LOCK;
    k[VK_NUMLOCK]   = GLFW_FKEY_NUM_LOCK;
    k[VK_SNAPSHOT]  = GLFW_FKEY_PRINT_SCREEN;
    k[VK_PAUSE]     = GLFW_FKEY_PAUSE;
    k[VK_APPS]      = GLFW_FKEY_MENU;
    for (int i = 0; i < 24; i++) k[VK_F1 + i] = GLFW_FKEY_F1 + i;
    // Printable keys carry their US-layout base (unshifted, lowercase) codepoint
    // so kitty can match shortcuts like ctrl+shift+t and encode ctrl+<key>.
    for (int i = 0; i < 26; i++) k[0x41 + i] = 'a' + i;   // VK_A..VK_Z -> a..z
    for (int i = 0; i < 10; i++) k[0x30 + i] = '0' + i;   // VK_0..VK_9
    k[VK_SPACE]     = ' ';
    k[VK_OEM_1]     = ';';   k[VK_OEM_PLUS]  = '=';  k[VK_OEM_COMMA] = ',';
    k[VK_OEM_MINUS] = '-';   k[VK_OEM_PERIOD]= '.';  k[VK_OEM_2]     = '/';
    k[VK_OEM_3]     = '`';   k[VK_OEM_4]     = '[';  k[VK_OEM_5]     = '\\';
    k[VK_OEM_6]     = ']';   k[VK_OEM_7]     = '\'';
    for (int i = 0; i < 10; i++) k[VK_NUMPAD0 + i] = GLFW_FKEY_KP_0 + i;
    k[VK_DECIMAL]   = GLFW_FKEY_KP_DECIMAL;  k[VK_DIVIDE]   = GLFW_FKEY_KP_DIVIDE;
    k[VK_MULTIPLY]  = GLFW_FKEY_KP_MULTIPLY; k[VK_SUBTRACT] = GLFW_FKEY_KP_SUBTRACT;
    k[VK_ADD]       = GLFW_FKEY_KP_ADD;      k[VK_SEPARATOR]= GLFW_FKEY_KP_SEPARATOR;
    k[VK_CLEAR]     = GLFW_FKEY_KP_BEGIN;
    k[VK_LWIN]      = GLFW_FKEY_LEFT_SUPER;  k[VK_RWIN]     = GLFW_FKEY_RIGHT_SUPER;
}

// The keys that share a virtual key with another and are told apart by the
// extended flag: the keypad's Enter and, with NumLock off, its navigation keys
// beside the dedicated ones, and the left and right modifiers.
static uint32_t refineKey(WPARAM vk, LPARAM lParam, uint32_t key) {
    const bool extended = HIWORD(lParam) & KF_EXTENDED;
    switch (vk) {
        case VK_RETURN: return extended ? GLFW_FKEY_KP_ENTER : key;
        case VK_HOME:   return extended ? key : GLFW_FKEY_KP_HOME;
        case VK_END:    return extended ? key : GLFW_FKEY_KP_END;
        case VK_PRIOR:  return extended ? key : GLFW_FKEY_KP_PAGE_UP;
        case VK_NEXT:   return extended ? key : GLFW_FKEY_KP_PAGE_DOWN;
        case VK_LEFT:   return extended ? key : GLFW_FKEY_KP_LEFT;
        case VK_RIGHT:  return extended ? key : GLFW_FKEY_KP_RIGHT;
        case VK_UP:     return extended ? key : GLFW_FKEY_KP_UP;
        case VK_DOWN:   return extended ? key : GLFW_FKEY_KP_DOWN;
        case VK_INSERT: return extended ? key : GLFW_FKEY_KP_INSERT;
        case VK_DELETE: return extended ? key : GLFW_FKEY_KP_DELETE;
        case VK_CONTROL: return extended ? GLFW_FKEY_RIGHT_CONTROL : GLFW_FKEY_LEFT_CONTROL;
        case VK_MENU:   return extended ? GLFW_FKEY_RIGHT_ALT : GLFW_FKEY_LEFT_ALT;
        case VK_SHIFT:
            return MapVirtualKeyW((lParam >> 16) & 0xff, MAPVK_VSC_TO_VK_EX) == VK_RSHIFT ? GLFW_FKEY_RIGHT_SHIFT : GLFW_FKEY_LEFT_SHIFT;
    }
    return key;
}

// The keypad keys that type a character, which WM_CHAR delivers
static bool isKeypadTextKey(uint32_t key) {
    return (key >= GLFW_FKEY_KP_0 && key <= GLFW_FKEY_KP_9) || key == GLFW_FKEY_KP_DECIMAL || key == GLFW_FKEY_KP_DIVIDE ||
        key == GLFW_FKEY_KP_MULTIPLY || key == GLFW_FKEY_KP_SUBTRACT || key == GLFW_FKEY_KP_ADD || key == GLFW_FKEY_KP_SEPARATOR;
}

static int getKeyMods(void) {
    int mods = 0;
    if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= GLFW_MOD_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= GLFW_MOD_CONTROL;
    if (GetKeyState(VK_MENU)    & 0x8000) mods |= GLFW_MOD_ALT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) mods |= GLFW_MOD_SUPER;
    if (GetKeyState(VK_CAPITAL) & 1) mods |= GLFW_MOD_CAPS_LOCK;
    if (GetKeyState(VK_NUMLOCK) & 1) mods |= GLFW_MOD_NUM_LOCK;
    return mods;
}

// ---------------------------------------------------------------------------
// Custom title bar (frame reclaimed so the caption shares the acrylic surface)
// ---------------------------------------------------------------------------

#define KITTY_TITLEBAR_LOGICAL_PX 40
#define KITTY_RESIZE_BORDER_LOGICAL_PX 6

static UINT windowDpi(HWND hWnd) {
    return _glfw.win32.user32.GetDpiForWindow ? _glfw.win32.user32.GetDpiForWindow(hWnd) : 96;
}
static int titlebarHeightPx(HWND hWnd) { return MulDiv(KITTY_TITLEBAR_LOGICAL_PX, windowDpi(hWnd), 96); }

// The repaint interval for the live-resize timer, one monitor refresh period.
// The present already paces to the compositor through DwmFlush, so the timer
// only has to request renders often enough to keep up: a fixed 12 ms held the
// resize to ~80fps on a 144Hz or faster panel. timeBeginPeriod(1) is set at
// startup, so a sub-12ms timer actually fires.
static UINT resizeTimerMs(HWND hWnd) {
    MONITORINFOEXW mi; ZeroMemory(&mi, sizeof mi); mi.cbSize = sizeof mi;
    DEVMODEW dm; ZeroMemory(&dm, sizeof dm); dm.dmSize = sizeof dm;
    HMONITOR mon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfoW(mon, (LPMONITORINFO) &mi) &&
        EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) &&
        dm.dmDisplayFrequency > 1) {
        UINT ms = 1000u / dm.dmDisplayFrequency;
        if (ms < 3) ms = 3;    // ~333Hz ceiling; DwmFlush paces the rest
        if (ms > 20) ms = 20;  // ~50Hz floor
        return ms;
    }
    return 12;
}

static void ensureCaptionButtons(_GLFWwindow* window);      // fwd
static void positionCaptionButtons(_GLFWwindow* window);    // fwd
static void updateWindowComposition(_GLFWwindow* window);   // fwd
static void runPendingDrag(void);                           // fwd
static void dragSourceWindowClosing(_GLFWwindow* window);   // fwd
static void placeAtDropPoint(int width, int height, int* x, int* y);  // fwd

// A multimedia timer drives the repaint during a live resize, because SetTimer
// clamps any interval below 10ms up to USER_TIMER_MINIMUM, which held the
// resize to ~80fps on a faster panel. timeSetEvent fires on its own thread and
// only posts this message; the render happens on the main thread where the
// modal loop pumps it. Coalesced in the handler so it never backs up.
#define WM_APP_RESIZE_TICK (WM_APP + 1)
// Starts a drag kitty asked for, see _glfwPlatformStartDrag
#define WM_APP_START_DRAG (WM_APP + 2)
#ifndef TIME_PERIODIC
#define TIME_PERIODIC 0x0001
#endif
static UINT g_resizeMMTimer = 0;
static HWND g_tickWindow = NULL;  // where the ticks go, the one window that stops them

static void CALLBACK resizeTickProc(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2) {
    (void) id; (void) msg; (void) d1; (void) d2;
    PostMessageW((HWND) user, WM_APP_RESIZE_TICK, 0, 0);
}

// Reapply the shape the application last asked for. Windows resets the pointer
// to the window class cursor on every WM_SETCURSOR, which it sends on every
// mouse move, so without this the shape set by _glfwPlatformSetCursor survives
// only until the pointer moves again.
static void applyClientCursor(_GLFWwindow* window) {
    // kitty hides the pointer while typing, see mouse_hide_wait
    if (window->cursorMode != GLFW_CURSOR_NORMAL) { SetCursor(NULL); return; }
    SetCursor((window->cursor && window->cursor->win32.handle)
              ? window->cursor->win32.handle
              : LoadCursorW(NULL, (LPCWSTR) IDC_ARROW));
}

static void startTickTimer(HWND target, UINT period) {
    g_tickWindow = target;
    if (_glfw.win32.winmm.SetEvent)
        g_resizeMMTimer = _glfw.win32.winmm.SetEvent(period, 1, resizeTickProc, (DWORD_PTR) target, TIME_PERIODIC);
    if (!g_resizeMMTimer) SetTimer(target, 1001, period, NULL);  // fallback: winmm missing
}

static void startResizeTimer(HWND hWnd) { startTickTimer(hWnd, resizeTimerMs(hWnd)); }

// Only the window the ticks go to stops them, so another window that closes
// during a live resize or a drag leaves them running.
static void stopResizeTimer(HWND hWnd) {
    if (hWnd != g_tickWindow) return;
    g_tickWindow = NULL;
    if (g_resizeMMTimer) {
        if (_glfw.win32.winmm.KillEvent) _glfw.win32.winmm.KillEvent(g_resizeMMTimer);
        g_resizeMMTimer = 0;
    }
    KillTimer(hWnd, 1001);
}

// kitty publishes the window background colour and opacity in the environment
// before it calls glfwSetWindowBlur, the same channel the caption colour
// already used. The acrylic tint is derived from both.
static unsigned int titlebarRGB(void) {
    const char* rgb = getenv("KITTY_TITLEBAR_RGB");
    if (!rgb || strlen(rgb) < 6) return 0x1e1e1e;
    return (unsigned int)(strtoul(rgb, NULL, 16) & 0xffffff);
}

static float backgroundOpacity(void) {
    const char* s = getenv("KITTY_BACKGROUND_OPACITY");
    if (!s || !*s) return 1.f;
    double v = strtod(s, NULL);
    if (v < 0.) v = 0.;
    if (v > 1.) v = 1.;
    return (float) v;
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

static void fitToMonitor(_GLFWwindow* window); // fwd

static LRESULT CALLBACK windowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // A drag runs from the helper window, which no kitty window closing during
    // it can take along: the drag starts there and its repaint ticks go there.
    if (hWnd == _glfw.win32.helperWindowHandle) {
        if (uMsg == WM_APP_START_DRAG) { runPendingDrag(); return 0; }
        // kitty writes the clipboard through this window, so another owner
        // means another program copied
        if (uMsg == WM_CLIPBOARDUPDATE) {
            if (GetClipboardOwner() != hWnd) _glfwInputClipboardLost(GLFW_CLIPBOARD);
            return 0;
        }
        if (uMsg == WM_APP_RESIZE_TICK || (uMsg == WM_TIMER && wParam == 1001)) {
            MSG extra;
            while (PeekMessageW(&extra, hWnd, WM_APP_RESIZE_TICK, WM_APP_RESIZE_TICK, PM_REMOVE)) { }
            if (_glfw.win32.tickCallback) _glfw.win32.tickCallback(_glfw.win32.tickCallbackData);
            _glfwWin32ProcessTimers();
            return 0;
        }
    }
    _GLFWwindow* window = GetPropW(hWnd, L"GLFW");
    if (!window) return DefWindowProcW(hWnd, uMsg, wParam, lParam);

    switch (uMsg) {
        case WM_NCCALCSIZE: {
            // Reclaim the whole window as client area so the caption shares the
            // (acrylic) GL surface. Resize is provided by WM_NCHITTEST below.
            if (!wParam || !window->win32.customFrame) break;
            NCCALCSIZE_PARAMS* p = (NCCALCSIZE_PARAMS*) lParam;
            if (IsZoomed(hWnd)) {
                // Maximized: inset by the frame so nothing is clipped off-screen.
                int fx = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                int fy = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                p->rgrc[0].left += fx; p->rgrc[0].right -= fx;
                p->rgrc[0].top += fy; p->rgrc[0].bottom -= fy;
            }
            return 0;  // client == window rect (no standard frame drawn)
        }

        case WM_NCACTIVATE:
            // DWM swaps the acrylic backdrop for a flat neutral fill as soon as
            // it considers the frame inactive -- deliberate API behaviour, meant
            // to reinforce focus. That state is just the wParam DefWindowProc is
            // told here, so report active unconditionally and the material (and
            // the per-pixel alpha, which DWM also drops on deactivation) stays.
            // lParam -1 suppresses the non-client repaint. No cosmetic cost: the
            // caption is drawn by kitty, so nothing keys off the DWM active look.
            if (!window->win32.transparent) break;
            return DefWindowProcW(hWnd, uMsg, TRUE, -1);

        case WM_NCHITTEST: {
            if (!window->win32.customFrame) break;
            RECT rc; GetWindowRect(hWnd, &rc);
            int x = GET_X_LPARAM(lParam) - rc.left, y = GET_Y_LPARAM(lParam) - rc.top;
            int w = rc.right - rc.left, h = rc.bottom - rc.top;
            int b = MulDiv(KITTY_RESIZE_BORDER_LOGICAL_PX, windowDpi(hWnd), 96);
            if (!IsZoomed(hWnd)) {
                bool t = y < b, bo = y >= h - b, l = x < b, r = x >= w - b;
                if (t && l) return HTTOPLEFT;    if (t && r) return HTTOPRIGHT;
                if (bo && l) return HTBOTTOMLEFT; if (bo && r) return HTBOTTOMRIGHT;
                if (t) return HTTOP; if (bo) return HTBOTTOM; if (l) return HTLEFT; if (r) return HTRIGHT;
            }
            if (y < titlebarHeightPx(hWnd)) return HTCAPTION;  // draggable title strip
            return HTCLIENT;
        }

        case WM_WINDOWPOSCHANGED:
            positionCaptionButtons(window);
            break;  // let DefWindowProc run too

        case WM_CLOSE:
            _glfwInputWindowCloseRequest(window);
            return 0;

        case WM_SETFOCUS:
            // The accent/glass set before the window was first shown does not
            // always take (the window opens with an opaque grey backdrop until
            // interacted with); re-apply once the window is actually active.
            if (window->win32.transparent) updateWindowComposition(window);
            _glfwInputWindowFocus(window, true);
            return 0;
        case WM_KILLFOCUS:
            _glfwInputWindowFocus(window, false);
            return 0;

        case WM_SETCURSOR:
            // DefWindowProc answers this by setting the window class cursor,
            // and it arrives on every mouse move, so a shape the application
            // asked for was being overwritten between moves: the pointer
            // alternated between that shape and the class arrow for as long as
            // the mouse kept moving, and only settled once it stopped. Claim
            // the message for the terminal area and put our own shape back.
            // Only HTCLIENT is ours -- the resize borders and the caption strip
            // that WM_NCHITTEST reports still want DefWindowProc's cursors.
            if (LOWORD(lParam) == HTCLIENT) {
                applyClientCursor(window);
                return TRUE;
            }
            break;

        case WM_PAINT:
            _glfwInputWindowDamage(window);
            break; // let DefWindowProc validate the region

        case WM_APP_RESIZE_TICK: {
            // Drain any ticks that piled up while a render was in flight, then
            // render once. The present paces to the compositor, so this settles
            // at the refresh rate without backing up the queue.
            MSG extra;
            while (PeekMessageW(&extra, hWnd, WM_APP_RESIZE_TICK, WM_APP_RESIZE_TICK, PM_REMOVE)) { }
            if (_glfw.win32.tickCallback && !window->win32.iconified)
                _glfw.win32.tickCallback(_glfw.win32.tickCallbackData);
            // The main loop that runs kitty's timers is blocked too, and its
            // animations need them.
            _glfwWin32ProcessTimers();
            return 0;
        }

        case WM_SIZE: {
            int width = LOWORD(lParam), height = HIWORD(lParam);
            window->win32.iconified = wParam == SIZE_MINIMIZED;
            window->win32.maximized = wParam == SIZE_MAXIMIZED;
            _glfwInputFramebufferSize(window, width, height);
            _glfwInputWindowSize(window, width, height);
            positionCaptionButtons(window);
            // The composition swapchain holding the GL output has to follow the
            // window, or the terminal is stretched from the old size.
            if (window->win32.acrylic && !window->win32.iconified)
                _glfwWin32AcrylicResize(window, width, height);
            // The Win32 modal resize loop blocks our main loop, so render here to
            // keep the window live (otherwise DWM stretches the stale frame).
            if (_glfw.win32.tickCallback && !window->win32.iconified)
                _glfw.win32.tickCallback(_glfw.win32.tickCallbackData);
            return 0;
        }

        case WM_ENTERSIZEMOVE: {
            RECT r; GetWindowRect(hWnd, &r);
            window->win32.sizeAtResizeStart = (SIZE){r.right - r.left, r.bottom - r.top};
            _glfwInputLiveResize(window, true);
            startResizeTimer(hWnd);
            break;
        }
        // resize_in_steps: the dragged edges move in whole cells from where the
        // resize started, as X11 window managers do with size increments
        case WM_SIZING: {
            if (window->widthincr <= 0 || window->heightincr <= 0 || window->win32.maximized) break;
            RECT* r = (RECT*) lParam;
            const SIZE start = window->win32.sizeAtResizeStart;
            const int w = start.cx + (int) lround((double) (r->right - r->left - start.cx) / window->widthincr) * window->widthincr;
            const int h = start.cy + (int) lround((double) (r->bottom - r->top - start.cy) / window->heightincr) * window->heightincr;
            if (wParam == WMSZ_LEFT || wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT) r->left = r->right - w;
            else r->right = r->left + w;
            if (wParam == WMSZ_TOP || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT) r->top = r->bottom - h;
            else r->bottom = r->top + h;
            return TRUE;
        }
        case WM_EXITSIZEMOVE:
            stopResizeTimer(hWnd);
            _glfwInputLiveResize(window, false);
            if (_glfw.win32.tickCallback) _glfw.win32.tickCallback(_glfw.win32.tickCallbackData);
            break;
        case WM_TIMER:
            if (wParam == 1001) {
                if (_glfw.win32.tickCallback) _glfw.win32.tickCallback(_glfw.win32.tickCallbackData);
                return 0;
            }
            break;

        case WM_MOUSEMOVE: {
            const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
            if (!window->win32.cursorTracked) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
                TrackMouseEvent(&tme);
                window->win32.cursorTracked = true;
                _glfwInputCursorEnter(window, true);
            }
            _glfwInputCursorPos(window, x, y);
            window->win32.lastCursorPosX = x;
            window->win32.lastCursorPosY = y;
            return 0;
        }
        case WM_MOUSELEAVE:
            window->win32.cursorTracked = false;
            _glfwInputCursorEnter(window, false);
            return 0;

        case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: {
            int button = GLFW_MOUSE_BUTTON_LEFT;
            if (uMsg == WM_RBUTTONDOWN || uMsg == WM_RBUTTONUP) button = GLFW_MOUSE_BUTTON_RIGHT;
            else if (uMsg == WM_MBUTTONDOWN || uMsg == WM_MBUTTONUP) button = GLFW_MOUSE_BUTTON_MIDDLE;
            int action = (uMsg == WM_LBUTTONDOWN || uMsg == WM_RBUTTONDOWN || uMsg == WM_MBUTTONDOWN) ? GLFW_PRESS : GLFW_RELEASE;
            if (action == GLFW_PRESS) SetCapture(hWnd); else ReleaseCapture();
            _glfwInputMouseClick(window, button, action, getKeyMods());
            return 0;
        }
        // The back and forward buttons, the fourth and fifth as on X11
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: {
            const int button = GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? GLFW_MOUSE_BUTTON_4 : GLFW_MOUSE_BUTTON_5;
            _glfwInputMouseClick(window, button, uMsg == WM_XBUTTONDOWN ? GLFW_PRESS : GLFW_RELEASE, getKeyMods());
            return TRUE;
        }

        // The delta is in 120ths of a notch, which a precision touchpad sends a
        // few at a time, so it goes to kitty as is and kitty adds the pieces up
        // rather than rounding each one up to a whole line
        case WM_MOUSEWHEEL: {
            GLFWScrollEvent ev = {.offset_type = GLFW_SCROLL_OFFEST_V120};
            ev.y_offset = ev.unscaled.y = (double) GET_WHEEL_DELTA_WPARAM(wParam);
            ev.keyboard_modifiers = getKeyMods();
            _glfwInputScroll(window, &ev);
            return 0;
        }
        case WM_MOUSEHWHEEL: {
            GLFWScrollEvent ev = {.offset_type = GLFW_SCROLL_OFFEST_V120};
            ev.x_offset = ev.unscaled.x = -(double) GET_WHEEL_DELTA_WPARAM(wParam);
            ev.keyboard_modifiers = getKeyMods();
            _glfwInputScroll(window, &ev);
            return 0;
        }

        case WM_KEYDOWN: case WM_SYSKEYDOWN:
        case WM_KEYUP: case WM_SYSKEYUP: {
            const bool up = uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP;
            uint32_t key = refineKey(wParam, lParam, (wParam < 512) ? (uint32_t) _glfw.win32.keycodes[wParam] : 0);
            int mods = getKeyMods();
            const bool functional = key >= GLFW_FKEY_FIRST;
            const int shortcutMods = mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER);
            if (!up) window->win32.pendingCharKey = 0;
            if (isKeypadTextKey(key) && (!shortcutMods || (shortcutMods == GLFW_MOD_ALT && key <= GLFW_FKEY_KP_9))) {
                // A keypad key that types goes out with its text from WM_CHAR,
                // which follows, as one key event the way X11 sends it. Alt and
                // keypad digits are left to Windows, which types the character
                // their code names once Alt comes up.
                if (!up && !shortcutMods) window->win32.pendingCharKey = key;
                if (uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP) break;
                return 0;
            }
            if (key && !functional && shortcutMods == GLFW_MOD_ALT) {
                // Alt with a key that types: WM_SYSCHAR follows with the
                // character, shift and the layout applied, and the two go out as
                // one event. Sent from both, the shell got the key's escape code
                // and then the character as well.
                if (!up) window->win32.pendingCharKey = key;
                break;  // a system key, which DefWindowProc still sees
            }
            // A plain printable key (no ctrl/alt) is delivered as text by WM_CHAR,
            // to keep dead keys and layouts working. Forward everything else here:
            // functional keys (arrows, enter, tab...) and any key combined with a
            // shortcut modifier, so kitty can match bindings and encode ctrl+<key>.
            if (key && (functional || shortcutMods)) {
                GLFWkeyevent ev = {0};
                ev.key = key;
                ev.native_key = (int) wParam;
                ev.action = up ? GLFW_RELEASE : (HIWORD(lParam) & KF_REPEAT ? GLFW_REPEAT : GLFW_PRESS);
                ev.mods = mods;
                _glfwInputKeyboard(window, &ev);
            }
            if (uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP) break; // allow system keys
            return 0;
        }

        case WM_CHAR: case WM_SYSCHAR: {
            const uint32_t pairedKey = window->win32.pendingCharKey;
            window->win32.pendingCharKey = 0;
            // Control chars (< 0x20) and pure-ctrl combos are encoded from the key
            // event above; only genuine text reaches the child here.
            if ((WCHAR) wParam < 0x20) return 0;
            {
                int m = getKeyMods();
                if ((m & GLFW_MOD_CONTROL) && !(m & GLFW_MOD_ALT)) return 0;
            }
            // wParam is a UTF-16 code unit; accumulate surrogate pairs.
            WCHAR utf16[2]; int n = 0;
            WCHAR c = (WCHAR) wParam;
            if (c >= 0xd800 && c <= 0xdbff) { window->win32.highSurrogate = c; return 0; }
            if (c >= 0xdc00 && c <= 0xdfff) {
                if (window->win32.highSurrogate) { utf16[0] = window->win32.highSurrogate; utf16[1] = c; n = 2; }
                window->win32.highSurrogate = 0;
            } else { utf16[0] = c; n = 1; }
            if (n) {
                char text[8] = {0};
                int len = WideCharToMultiByte(CP_UTF8, 0, utf16, n, text, sizeof(text) - 1, NULL, NULL);
                if (len > 0) {
                    uint32_t cp = 0;
                    if (n == 2) cp = 0x10000 + (((utf16[0] - 0xd800) << 10) | (utf16[1] - 0xdc00));
                    else cp = utf16[0];
                    GLFWkeyevent ev = {0};
                    ev.key = pairedKey ? pairedKey : cp;
                    ev.text = text;
                    ev.action = GLFW_PRESS;
                    ev.mods = getKeyMods();
                    _glfwInputKeyboard(window, &ev);
                }
            }
            return 0;
        }

        case WM_DPICHANGED: {
            const float xscale = HIWORD(wParam) / 96.0f;
            const float yscale = LOWORD(wParam) / 96.0f;
            _glfwInputWindowContentScale(window, xscale, yscale);
            RECT* r = (RECT*) lParam;
            SetWindowPos(hWnd, HWND_TOP, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            return 0;
        }
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Window class + helper window
// ---------------------------------------------------------------------------

bool _glfwRegisterWindowClassWin32(void) {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = _glfw.win32.instance;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR) IDC_ARROW);
    wc.lpszClassName = _GLFW_WNDCLASSNAME;
    wc.hIcon = NULL;  // no title-bar icon (kitty theming hides it entirely)
    _glfw.win32.mainWindowClass = RegisterClassExW(&wc);
    if (!_glfw.win32.mainWindowClass) {
        _glfwInputError(GLFW_PLATFORM_ERROR, "Win32: Failed to register window class");
        return false;
    }
    createKeyTables();

    _glfw.win32.helperWindowHandle = CreateWindowExW(0, _GLFW_WNDCLASSNAME, L"kitty-helper",
        WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 1, 1, HWND_MESSAGE, NULL, _glfw.win32.instance, NULL);
    // Told of every clipboard change, so kitty learns when another program copies
    if (_glfw.win32.helperWindowHandle) AddClipboardFormatListener(_glfw.win32.helperWindowHandle);
    return true;
}

void _glfwUnregisterWindowClassWin32(void) {
    if (_glfw.win32.helperWindowHandle) {
        RemoveClipboardFormatListener(_glfw.win32.helperWindowHandle);
        DestroyWindow(_glfw.win32.helperWindowHandle);
    }
    if (_glfw.win32.mainWindowClass) UnregisterClassW(_GLFW_WNDCLASSNAME, _glfw.win32.instance);
}

// ---------------------------------------------------------------------------
// Window creation / destruction
// ---------------------------------------------------------------------------

static DWORD windowStyle(const _GLFWwindow* window) {
    DWORD style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    if (window->monitor) style |= WS_POPUP;
    else if (window->decorated) {
        style |= WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        if (window->resizable) style |= WS_MAXIMIZEBOX | WS_THICKFRAME;
    } else style |= WS_POPUP;
    return style;
}

// ---------------------------------------------------------------------------
// Caption buttons: a per-pixel-alpha layered overlay pinned to the title strip
// so the min/max/close controls float over the acrylic surface. Drawn with the
// GDI+ flat API (declared here to avoid the C++ gdiplus headers).
// ---------------------------------------------------------------------------
typedef struct { UINT32 version; void* cb; BOOL noThread; BOOL noCodecs; } GpStartupInput;
extern int WINAPI GdiplusStartup(ULONG_PTR*, const GpStartupInput*, void*);
extern int WINAPI GdipCreateFromHDC(HDC, void**);
extern int WINAPI GdipDeleteGraphics(void*);
extern int WINAPI GdipSetSmoothingMode(void*, int);
extern int WINAPI GdipCreateSolidFill(ULONG, void**);
extern int WINAPI GdipDeleteBrush(void*);
extern int WINAPI GdipCreatePen1(ULONG, float, int, void**);
extern int WINAPI GdipDeletePen(void*);
extern int WINAPI GdipSetPenStartCap(void*, int);
extern int WINAPI GdipSetPenEndCap(void*, int);
extern int WINAPI GdipDrawLineI(void*, void*, int, int, int, int);
extern int WINAPI GdipDrawRectangleI(void*, void*, int, int, int, int);
extern int WINAPI GdipCreatePath(int, void**);
extern int WINAPI GdipDeletePath(void*);
extern int WINAPI GdipAddPathArcI(void*, int, int, int, int, float, float);
extern int WINAPI GdipClosePathFigure(void*);
extern int WINAPI GdipFillPath(void*, void*, void*);
extern int WINAPI GdipDrawPath(void*, void*, void*);
extern int WINAPI GdipAddPathRectangleI(void*, int, int, int, int);
// Bitmaps (the caption-icon glyphs are rasterized Material Symbols SVGs)
extern int WINAPI GdipCreateBitmapFromScan0(int, int, int, int, void*, void**);
extern int WINAPI GdipDrawImageRectI(void*, void*, int, int, int, int);
extern int WINAPI GdipDisposeImage(void*);
extern int WINAPI GdipSetInterpolationMode(void*, int);
#include "caption_icons.h"

#define CB_COUNT 3   // minimize, maximize/restore, close

// Lazily create (and cache) the GDI+ bitmap for a caption icon from its embedded
// BGRA pixels. idx: 0 minimize, 1 maximize, 2 restore, 3 close.
static void* cbIconBitmap(int idx, bool dark) {
    static void* cacheLight[4] = { NULL, NULL, NULL, NULL };
    static void* cacheDark[4] = { NULL, NULL, NULL, NULL };
    if (idx < 0 || idx > 3) return NULL;
    void** cache = dark ? cacheDark : cacheLight;
    if (!cache[idx]) {
        const unsigned char* data[4] = { CB_ICON_MIN, CB_ICON_MAX, CB_ICON_RESTORE, CB_ICON_CLOSE };
        void* bmp = NULL;
        const void* pixels = data[idx];
        if (dark) {
            // The embedded glyphs are white; zero the colour channels (keep alpha)
            // to make them dark for light captions. GdipCreateBitmapFromScan0
            // references (does not copy) the pixels, so this copy is kept alive for
            // the process by the cache. Layout is BGRA, alpha at byte 3.
            size_t n = (size_t) CB_ICON_W * CB_ICON_H * 4;
            unsigned char* copy = (unsigned char*) malloc(n);
            if (!copy) return NULL;
            memcpy(copy, data[idx], n);
            for (size_t p = 0; p < n; p += 4) { copy[p] = 0; copy[p + 1] = 0; copy[p + 2] = 0; }
            pixels = copy;
        }
        // PixelFormat32bppARGB = 0x0026200A
        if (GdipCreateBitmapFromScan0(CB_ICON_W, CB_ICON_H, CB_ICON_W * 4, 0x0026200A, (void*) pixels, &bmp) == 0)
            cache[idx] = bmp;
        else if (dark) free((void*) pixels);
    }
    return cache[idx];
}

// A light caption (near-white theme background) needs dark button glyphs. The
// caption colour is published by kitty as KITTY_TITLEBAR_RGB (the window bg).
static bool cbCaptionIsLight(void) {
    const char* rgb = getenv("KITTY_TITLEBAR_RGB");
    if (!rgb || strlen(rgb) < 6) return false;
    unsigned long v = strtoul(rgb, NULL, 16);
    int r = (int)((v >> 16) & 255), g = (int)((v >> 8) & 255), b = (int)(v & 255);
    return (r * 299 + g * 587 + b * 114) / 1000 >= 128;  // Rec. 601 luma midpoint
}

typedef struct { _GLFWwindow* owner; int hovered; int pressed; } CaptionState;
static const wchar_t* CAPTION_CLASS = L"kittyCaptionButtons";

static void cbLayout(HWND overlay, int* bw, int* bh, int* gap, int* pad) {
    UINT dpi = windowDpi(overlay);
    // 30px square buttons in a 40px strip -> 5px vertical margin; pad matches it
    // so the top and right gaps are equal. Glyphs stay small (see cbPaint).
    *bw = *bh = MulDiv(30, dpi, 96);
    *gap = MulDiv(4, dpi, 96); *pad = MulDiv(5, dpi, 96);
}
static int cbHitTest(HWND overlay, int x, int y) {
    int bw, bh, gap, pad; cbLayout(overlay, &bw, &bh, &gap, &pad);
    RECT rc; GetClientRect(overlay, &rc);
    int top = (rc.bottom - bh) / 2;
    if (y < top || y >= top + bh) return -1;
    for (int i = 0; i < CB_COUNT; i++) {
        int bx = pad + i * (bw + gap);
        if (x >= bx && x < bx + bw) return i;
    }
    return -1;
}

static void cbPaint(HWND overlay) {
    CaptionState* st = (CaptionState*) GetWindowLongPtrW(overlay, GWLP_USERDATA);
    if (!st) return;
    RECT rc; GetClientRect(overlay, &rc);
    int W = rc.right, H = rc.bottom;
    if (W <= 0 || H <= 0) return;
    int bw, bh, gap, pad; cbLayout(overlay, &bw, &bh, &gap, &pad);
    UINT dpi = windowDpi(overlay);

    HDC screen = GetDC(NULL);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO bi; ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HBITMAP oldbm = (HBITMAP) SelectObject(dc, dib);
    memset(bits, 0, (size_t) W * H * 4);

    void* g = NULL;
    if (GdipCreateFromHDC(dc, &g) == 0) {
        GdipSetSmoothingMode(g, 4 /* antialias */);
        GdipSetInterpolationMode(g, 7 /* HighQualityBicubic: crisp icon downscale */);
        int top = (H - bh) / 2;
        float rad = (float) MulDiv(7, dpi, 96);
        bool light_caption = cbCaptionIsLight();
        for (int i = 0; i < CB_COUNT; i++) {
            int bx = pad + i * (bw + gap);
            bool hot = st->hovered == i;
            // Always fill the button rect: a base alpha of 1 keeps the WHOLE
            // button clickable/hoverable (a layered window is click-through only
            // where alpha == 0), while hover shows a clear background.
            ULONG col = hot ? ((i == 2) ? 0xF0E81123u /* red */ : 0x55FFFFFFu /* ~33% white */)
                            : 0x01FFFFFFu /* invisible but hit-testable */;
            void* brush = NULL;
            if (GdipCreateSolidFill(col, &brush) == 0) {
                void* path = NULL;
                if (GdipCreatePath(0, &path) == 0) {
                    int d = (int)(rad * 2);
                    GdipAddPathArcI(path, bx, top, d, d, 180, 90);
                    GdipAddPathArcI(path, bx + bw - d, top, d, d, 270, 90);
                    GdipAddPathArcI(path, bx + bw - d, top + bh - d, d, d, 0, 90);
                    GdipAddPathArcI(path, bx, top + bh - d, d, d, 90, 90);
                    GdipClosePathFigure(path);
                    GdipFillPath(g, brush, path);
                    GdipDeletePath(path);
                }
                GdipDeleteBrush(brush);
            }
            // glyph: the rasterized Material Symbols icon, scaled and centered
            //  0 minimize, 1 maximize (or 2 restore when zoomed), 3 close
            int iconIdx = (i == 0) ? 0
                        : (i == 1) ? (IsZoomed(st->owner->win32.handle) ? 2 : 1)
                                   : 3;
            // Dark glyphs on a light caption; but keep the close glyph white while
            // its hover fill is red so the X stays visible.
            void* bmp = cbIconBitmap(iconIdx, light_caption && !(i == 2 && hot));
            if (bmp) {
                int gsz = MulDiv(16, dpi, 96);   // glyph box within the 30px button
                int gx = bx + (bw - gsz) / 2;
                int gy = top + (bh - gsz) / 2 + MulDiv(1, dpi, 96);  // +1px: sits 1px high otherwise
                GdipDrawImageRectI(g, bmp, gx, gy, gsz, gsz);
            }
        }
        GdipDeleteGraphics(g);
    }
    // premultiply for UpdateLayeredWindow
    uint8_t* px = (uint8_t*) bits;
    for (int i = 0; i < W * H; i++) {
        uint8_t a = px[3];
        px[0] = (uint8_t)(px[0] * a / 255); px[1] = (uint8_t)(px[1] * a / 255); px[2] = (uint8_t)(px[2] * a / 255);
        px += 4;
    }
    POINT srcp = { 0, 0 }; SIZE sz = { W, H };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    // pptDst = NULL: position/size is owned by SetWindowPos (child coords); here we
    // only refresh the per-pixel-alpha content.
    UpdateLayeredWindow(overlay, screen, NULL, &sz, dc, &srcp, 0, &bf, ULW_ALPHA);
    SelectObject(dc, oldbm); DeleteObject(dib); DeleteDC(dc); ReleaseDC(NULL, screen);
}

static LRESULT CALLBACK captionProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    CaptionState* st = (CaptionState*) GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    switch (msg) {
        case WM_MOUSEMOVE: {
            if (!st) break;
            TRACKMOUSEEVENT tme = { sizeof tme, TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            int h = cbHitTest(hWnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (h != st->hovered) { st->hovered = h; cbPaint(hWnd); }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (st && st->hovered != -1) { st->hovered = -1; st->pressed = -1; cbPaint(hWnd); }
            return 0;
        case WM_LBUTTONDOWN:
            if (st) { st->pressed = cbHitTest(hWnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); SetCapture(hWnd); }
            return 0;
        case WM_LBUTTONUP: {
            if (!st) break;
            ReleaseCapture();
            int h = cbHitTest(hWnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            int pressed = st->pressed; st->pressed = -1;
            HWND owner = st->owner->win32.handle;
            if (h == pressed && h != -1) {
                if (h == 0) ShowWindow(owner, SW_MINIMIZE);
                else if (h == 1) ShowWindow(owner, IsZoomed(owner) ? SW_RESTORE : SW_MAXIMIZE);
                else PostMessageW(owner, WM_CLOSE, 0, 0);
            }
            return 0;
        }
        case WM_NCDESTROY:
            if (st) { free(st); SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0); }
            break;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

static void ensureCaptionButtons(_GLFWwindow* window) {
    if (window->win32.captionButtons) return;
    static ULONG_PTR gdipToken = 0;
    static bool classReady = false;
    if (!gdipToken) { GpStartupInput in = { 1, NULL, FALSE, FALSE }; GdiplusStartup(&gdipToken, &in, NULL); }
    if (!classReady) {
        WNDCLASSEXW wc; ZeroMemory(&wc, sizeof wc); wc.cbSize = sizeof wc;
        wc.lpfnWndProc = captionProc; wc.hInstance = _glfw.win32.instance;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR) IDC_ARROW); wc.lpszClassName = CAPTION_CLASS;
        RegisterClassExW(&wc); classReady = true;
    }
    // A child window composites into its parent's redirection surface. The
    // acrylic path removes that surface (WS_EX_NOREDIRECTIONBITMAP), so a child
    // has nowhere to draw and the buttons vanish. An owned popup carries its
    // own surface and stays visible.
    //
    // The child form is kept for every other mode because DWM animates a child
    // together with the parent through minimize/maximize/restore, where a popup
    // snaps ahead. That does not cost anything here: the acrylic path disables
    // those transitions anyway, since the animation is DWM scaling a snapshot
    // and the material cannot follow it.
    const bool ownedPopup = window->win32.acrylic != NULL;
    HWND overlay = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_NOACTIVATE | (ownedPopup ? WS_EX_TOOLWINDOW : 0),
        CAPTION_CLASS, L"", ownedPopup ? WS_POPUP : WS_CHILD, 0, 0, 10, 10,
        window->win32.handle, NULL, _glfw.win32.instance, NULL);
    if (!overlay) return;
    CaptionState* st = calloc(1, sizeof(CaptionState));
    st->owner = window; st->hovered = -1; st->pressed = -1;
    SetWindowLongPtrW(overlay, GWLP_USERDATA, (LONG_PTR) st);
    window->win32.captionButtons = overlay;
    positionCaptionButtons(window);
    ShowWindow(overlay, SW_SHOWNOACTIVATE);
}

static void positionCaptionButtons(_GLFWwindow* window) {
    HWND overlay = window->win32.captionButtons;
    if (!overlay) return;
    HWND owner = window->win32.handle;
    if (IsIconic(owner)) { ShowWindow(overlay, SW_HIDE); return; }
    int strip = titlebarHeightPx(owner);
    int bw, bh, gap, pad; cbLayout(overlay, &bw, &bh, &gap, &pad);
    int W = pad * 2 + CB_COUNT * bw + (CB_COUNT - 1) * gap;
    // Top-right of the client area. A child window wants parent-client
    // coordinates, so the buttons ride along with the parent through
    // animations. The acrylic path uses an owned popup instead (see
    // ensureCaptionButtons), and a popup is positioned in screen coordinates.
    RECT cr; GetClientRect(owner, &cr);
    int x = cr.right - W, y = cr.top;
    if (!(GetWindowLongW(overlay, GWL_STYLE) & WS_CHILD)) {
        POINT origin = { x, y };
        ClientToScreen(owner, &origin);
        x = origin.x; y = origin.y;
    }
    SetWindowPos(overlay, HWND_TOP, x, y, W, strip,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    cbPaint(overlay);
}

// With the custom frame kitty renders the whole surface (caption included), so
// the title bar colour/opacity/blur is the terminal background automatically.
// Here we only ask DWM for rounded corners and a thin dark border so the window
// edge reads clearly, plus dark-mode window controls.
static void styleTitlebar(_GLFWwindow* window) {
    HWND hwnd = window->win32.handle;
    if (!hwnd || !_glfw.win32.dwmapi.SetWindowAttribute) return;
    BOOL dark = TRUE;
    _glfw.win32.dwmapi.SetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    DWORD round = 2 /* DWMWCP_ROUND */;
    _glfw.win32.dwmapi.SetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &round, sizeof(round));
    COLORREF border = 0x00000000 /* thin black edge, like a normal window */;
    _glfw.win32.dwmapi.SetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof(border));
    // Repaint the custom caption buttons so their glyph colour tracks a caption
    // colour change (e.g. a live theme preview restyles via set_os_window_chrome),
    // not only window events like resize/focus/hover.
    if (window->win32.captionButtons) cbPaint(window->win32.captionButtons);
}

// Transparency + real frosted acrylic on the window itself (no second window,
// so no drag ghost).
//
// The material is built by win32_acrylic.c as a Windows.UI.Composition effect
// graph, the same recipe WinUI's AcrylicBrush uses and therefore the same one
// Windows Terminal shows. It replaces the DWM system backdrop this port used
// before (DWMWA_SYSTEMBACKDROP_TYPE with DWMSBT_TRANSIENTWINDOW), which DWM
// renders with maths of its own that cannot be matched to Terminal, and which
// needed two workarounds this path does not: a forced-active WM_NCACTIVATE to
// stop DWM dropping the material on focus loss, and an off/on toggle after
// every maximize to make DWM resample a stale backdrop.
//
// Glass mode, transparent with blur off, keeps the old mechanism: an
// empty-region blur-behind for per-pixel alpha with no material. A plain Win32
// window is opaque regardless of framebuffer alpha, so it is still needed.
//
// DwmExtendFrameIntoClientArea is deliberately not used to expose the
// material -- it makes DWM paint its own caption over the custom one (see
// docs/decisions.md).
static void updateWindowComposition(_GLFWwindow* window) {
    HWND hwnd = window->win32.handle;
    if (!hwnd) return;
    const bool wantTransparent = window->win32.transparent;
    // A window made for acrylic has no redirection surface, so the material is
    // the only way its frames reach the screen. Dropping it, as an opacity of 1
    // asks with dynamic_background_opacity, would freeze the window, so the
    // material stays and its tint and the cells turn opaque instead.
    const bool noRedirection = (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP) != 0;
    const bool wantAcrylic = wantTransparent && (window->win32.blur > 0 || (noRedirection && window->win32.acrylic));

    if (wantAcrylic) {
        // The host backdrop brush samples what is behind the window, and needs
        // this attribute on the top level HWND to produce anything at all.
        if (_glfw.win32.dwmapi.SetWindowAttribute) {
            BOOL on = TRUE;
            _glfw.win32.dwmapi.SetWindowAttribute(hwnd, 17 /* DWMWA_USE_HOSTBACKDROPBRUSH */, &on, sizeof(on));
            // The minimize/maximize animation is DWM scaling a snapshot of the
            // window. The backdrop reaches us through the redirection surface,
            // so that snapshot carries a stale, stretched copy of the desktop
            // and the material visibly lags the animation. A snapshot cannot
            // be made live, so drop the transition instead.
            BOOL noTransitions = TRUE;
            _glfw.win32.dwmapi.SetWindowAttribute(hwnd, 3 /* DWMWA_TRANSITIONS_FORCEDISABLED */,
                                                  &noTransitions, sizeof(noTransitions));
        }
        if (!window->win32.acrylic) _glfwWin32AcrylicCreate(window);
        if (window->win32.acrylic) {
            _glfwWin32AcrylicSetTint(window, titlebarRGB(), backgroundOpacity());
            return;
        }
        // Creating it failed, most likely no WGL_NV_DX_interop2. Fall through
        // to glass so the window is at least translucent.
    }

    if (window->win32.acrylic) _glfwWin32AcrylicDestroy(window);

    if (_glfw.win32.dwmapi.EnableBlurBehindWindow) {
        DWM_BLURBEHIND bb; memset(&bb, 0, sizeof bb);
        bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
        bb.fEnable = wantTransparent ? TRUE : FALSE;
        bb.hRgnBlur = wantTransparent ? CreateRectRgn(0, 0, -1, -1) : NULL;  // empty -> no blur of its own
        _glfw.win32.dwmapi.EnableBlurBehindWindow(hwnd, &bb);
        if (bb.hRgnBlur) DeleteObject(bb.hRgnBlur);
    }
    if (_glfw.win32.dwmapi.SetWindowAttribute) {
        DWORD backdrop = 1 /* DWMSBT_NONE */;
        _glfw.win32.dwmapi.SetWindowAttribute(hwnd, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &backdrop, sizeof(backdrop));
    }
}

int _glfwPlatformCreateWindow(_GLFWwindow* window, const _GLFWwndconfig* wndconfig,
                              const _GLFWctxconfig* ctxconfig, const _GLFWfbconfig* fbconfig,
                              const GLFWLayerShellConfig* lsc) {
    (void) lsc;
    window->win32.transparent = fbconfig->transparent;
    window->win32.scaleToMonitor = wndconfig->scaleToMonitor;
    window->win32.opacity = 1.f;

    WCHAR* wtitle = NULL;
    int tn = MultiByteToWideChar(CP_UTF8, 0, wndconfig->title, -1, NULL, 0);
    if (tn > 0) { wtitle = calloc((size_t) tn, sizeof(WCHAR)); MultiByteToWideChar(CP_UTF8, 0, wndconfig->title, -1, wtitle, tn); }

    DWORD style = windowStyle(window);
    RECT rect = { 0, 0, wndconfig->width, wndconfig->height };
    // The custom frame, turned on below, makes the whole window client area,
    // so the size asked for is the window's own
    if (!(window->decorated && !window->monitor)) AdjustWindowRectEx(&rect, style, FALSE, WS_EX_APPWINDOW);

    // WS_EX_NOREDIRECTIONBITMAP has to be decided here, since it cannot be
    // added after the window exists. Acrylic needs it: the material lives in a
    // composition tree, and a composition tree always draws above the
    // redirection surface, so leaving the surface in place would hide the
    // terminal behind the material. See win32_acrylic.c.
    window->win32.blur = _glfw.hints.window.blur_radius;
    const DWORD exStyle = WS_EX_APPWINDOW |
        (window->win32.transparent && window->win32.blur > 0 ? WS_EX_NOREDIRECTIONBITMAP : 0);

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    placeAtDropPoint(rect.right - rect.left, rect.bottom - rect.top, &x, &y);
    window->win32.handle = CreateWindowExW(exStyle, _GLFW_WNDCLASSNAME, wtitle ? wtitle : L"kitty",
        style, x, y, rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, _glfw.win32.instance, NULL);
    free(wtitle);
    if (!window->win32.handle) {
        _glfwInputError(GLFW_PLATFORM_ERROR, "Win32: Failed to create window");
        return false;
    }
    SetPropW(window->win32.handle, L"GLFW", window);
    _glfwWin32RegisterDropTarget(window);

    if (ctxconfig->client != GLFW_NO_API) {
        if (!_glfwCreateContextWGL(window, ctxconfig, fbconfig)) return false;
        if (!_glfwRefreshContextAttribs(window, ctxconfig)) return false;
    }
    window->win32.customFrame = window->decorated && !window->monitor;
    if (window->win32.transparent) updateWindowComposition(window);
    if (window->win32.acrylic) {
        // _glfwRefreshContextAttribs restores whatever context was current
        // before it ran, which during window creation is none. Binding the
        // swapchain to GL needs one, so make it current for the call.
        _GLFWwindow* previous = _glfwPlatformGetTls(&_glfw.contextSlot);
        glfwMakeContextCurrent((GLFWwindow*) window);
        if (!_glfwWin32AcrylicBindFramebuffer(window)) {
            // No WGL_NV_DX_interop2, so nothing can reach the swapchain. Drop
            // the material rather than show an empty window: the redirection
            // surface is already gone, but glass still composites.
            _glfwWin32AcrylicDestroy(window);
            window->win32.blur = 0;
            updateWindowComposition(window);
        }
        glfwMakeContextCurrent((GLFWwindow*) previous);
    }
    styleTitlebar(window);
    if (window->win32.customFrame) {
        ensureCaptionButtons(window);
        // Trigger a frame recalculation now that customFrame is set.
        SetWindowPos(window->win32.handle, NULL, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return true;
}

void _glfwPlatformDestroyWindow(_GLFWwindow* window) {
    dragSourceWindowClosing(window);
    if (window->win32.handle) stopResizeTimer(window->win32.handle);
    if (window->win32.acrylic) {
        // Unregistering the interop objects needs the context that registered
        // them, and it has to happen before that context is destroyed.
        _GLFWwindow* previous = _glfwPlatformGetTls(&_glfw.contextSlot);
        glfwMakeContextCurrent((GLFWwindow*) window);
        _glfwWin32AcrylicDestroy(window);
        glfwMakeContextCurrent((GLFWwindow*) (previous == window ? NULL : previous));
    }
    if (window->context.destroy) window->context.destroy(window);
    if (window->win32.captionButtons) {
        DestroyWindow(window->win32.captionButtons);
        window->win32.captionButtons = NULL;
    }
    if (window->win32.handle) {
        _glfwWin32RevokeDropTarget(window);
        RemovePropW(window->win32.handle, L"GLFW");
        DestroyWindow(window->win32.handle);
        window->win32.handle = NULL;
    }
    // These are HICON handles (CreateIconIndirect), not heap memory: destroy them
    // with DestroyIcon. free() here corrupts the heap and crashes the process on
    // window close (fatal when other OS windows are still open).
    if (window->win32.bigIcon) DestroyIcon(window->win32.bigIcon);
    if (window->win32.smallIcon) DestroyIcon(window->win32.smallIcon);
    window->win32.bigIcon = window->win32.smallIcon = NULL;
}

// ---------------------------------------------------------------------------
// Window operations
// ---------------------------------------------------------------------------

void _glfwPlatformSetWindowTitle(_GLFWwindow* window, const char* title) {
    // The caption text is drawn in the caption colour (invisible), so this only
    // affects the taskbar/alt-tab label.
    int n = MultiByteToWideChar(CP_UTF8, 0, title, -1, NULL, 0);
    if (n <= 0) return;
    WCHAR* w = calloc((size_t) n, sizeof(WCHAR));
    if (!w) return;
    MultiByteToWideChar(CP_UTF8, 0, title, -1, w, n);
    SetWindowTextW(window->win32.handle, w);
    free(w);
}

static HICON createIcon(const GLFWimage* image) {
    BITMAPV5HEADER bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = image->width;
    bi.bV5Height = -image->height;   // top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask   = 0x00ff0000;
    bi.bV5GreenMask = 0x0000ff00;
    bi.bV5BlueMask  = 0x000000ff;
    bi.bV5AlphaMask = 0xff000000;

    HDC dc = GetDC(NULL);
    unsigned char* target = NULL;
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*) &bi, DIB_RGB_COLORS, (void**) &target, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!color) return NULL;
    HBITMAP mask = CreateBitmap(image->width, image->height, 1, 1, NULL);
    if (!mask) { DeleteObject(color); return NULL; }
    const unsigned char* source = image->pixels;   // RGBA
    for (int i = 0; i < image->width * image->height; i++) {
        target[0] = source[2]; target[1] = source[1]; target[2] = source[0]; target[3] = source[3];
        target += 4; source += 4;
    }
    ICONINFO ii;
    ZeroMemory(&ii, sizeof(ii));
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HICON handle = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return handle;
}

// Pick the image whose size is closest to the requested target.
static const GLFWimage* chooseIcon(int count, const GLFWimage* images, int target) {
    const GLFWimage* best = images;
    int bestDiff = 1 << 30;
    for (int i = 0; i < count; i++) {
        int diff = abs(images[i].width - target);
        if (diff < bestDiff) { bestDiff = diff; best = images + i; }
    }
    return best;
}

void _glfwPlatformSetWindowIcon(_GLFWwindow* window, int count, const GLFWimage* images) {
    HICON bigIcon = NULL, smallIcon = NULL;
    if (count > 0) {
        bigIcon = createIcon(chooseIcon(count, images, GetSystemMetrics(SM_CXICON)));
        smallIcon = createIcon(chooseIcon(count, images, GetSystemMetrics(SM_CXSMICON)));
    }
    // ICON_BIG drives the taskbar and alt-tab; the caption icon stays hidden via
    // WS_EX_DLGMODALFRAME set in styleTitlebar().
    SendMessageW(window->win32.handle, WM_SETICON, ICON_BIG, (LPARAM) bigIcon);
    SendMessageW(window->win32.handle, WM_SETICON, ICON_SMALL, (LPARAM) smallIcon);
    if (window->win32.bigIcon) DestroyIcon(window->win32.bigIcon);
    if (window->win32.smallIcon) DestroyIcon(window->win32.smallIcon);
    window->win32.bigIcon = bigIcon;
    window->win32.smallIcon = smallIcon;
}

void _glfwPlatformGetWindowPos(_GLFWwindow* window, int* xpos, int* ypos) {
    POINT pos = {0};
    ClientToScreen(window->win32.handle, &pos);
    if (xpos) *xpos = pos.x;
    if (ypos) *ypos = pos.y;
}

// With the custom frame the whole window is client area, so a size or a place
// kitty asks for is the window's own, with no standard frame around it
static void adjustForFrame(_GLFWwindow* window, RECT* rect) {
    if (!window->win32.customFrame) AdjustWindowRectEx(rect, windowStyle(window), FALSE, WS_EX_APPWINDOW);
}

void _glfwPlatformSetWindowPos(_GLFWwindow* window, int xpos, int ypos) {
    RECT rect = { xpos, ypos, xpos, ypos };
    adjustForFrame(window, &rect);
    SetWindowPos(window->win32.handle, NULL, rect.left, rect.top, 0, 0, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
}

void _glfwPlatformGetWindowSize(_GLFWwindow* window, int* width, int* height) {
    RECT area;
    GetClientRect(window->win32.handle, &area);
    if (width) *width = area.right;
    if (height) *height = area.bottom;
}

void _glfwPlatformSetWindowSize(_GLFWwindow* window, int width, int height) {
    RECT rect = { 0, 0, width, height };
    adjustForFrame(window, &rect);
    SetWindowPos(window->win32.handle, HWND_TOP, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_NOZORDER);
}

void _glfwPlatformGetFramebufferSize(_GLFWwindow* window, int* width, int* height) {
    _glfwPlatformGetWindowSize(window, width, height);
}

void _glfwPlatformGetWindowContentScale(_GLFWwindow* window, float* xscale, float* yscale) {
    UINT dpi = 96;
    if (_glfw.win32.user32.GetDpiForWindow) dpi = _glfw.win32.user32.GetDpiForWindow(window->win32.handle);
    if (xscale) *xscale = dpi / 96.0f;
    if (yscale) *yscale = dpi / 96.0f;
}

void _glfwPlatformGetWindowFrameSize(_GLFWwindow* window, int* left, int* top, int* right, int* bottom) {
    RECT rect = {0};
    adjustForFrame(window, &rect);
    if (left) *left = -rect.left;
    if (top) *top = -rect.top;
    if (right) *right = rect.right;
    if (bottom) *bottom = rect.bottom;
}

void _glfwPlatformSetWindowSizeLimits(_GLFWwindow* window, int minw, int minh, int maxw, int maxh) {
    window->win32.minwidth = minw; window->win32.minheight = minh;
    window->win32.maxwidth = maxw; window->win32.maxheight = maxh;
}
void _glfwPlatformSetWindowAspectRatio(_GLFWwindow* window, int numer, int denom) {
    window->win32.numer = numer; window->win32.denom = denom;
}
void _glfwPlatformSetWindowSizeIncrements(_GLFWwindow* window, int widthincr, int heightincr) {
    (void) window; (void) widthincr; (void) heightincr;
}

void _glfwPlatformIconifyWindow(_GLFWwindow* window) { ShowWindow(window->win32.handle, SW_MINIMIZE); }
void _glfwPlatformRestoreWindow(_GLFWwindow* window) { ShowWindow(window->win32.handle, SW_RESTORE); }
void _glfwPlatformMaximizeWindow(_GLFWwindow* window) { ShowWindow(window->win32.handle, SW_MAXIMIZE); }
void _glfwPlatformShowWindow(_GLFWwindow* window, bool move_to_active_screen) { (void) move_to_active_screen; ShowWindow(window->win32.handle, SW_SHOWNA); }
void _glfwPlatformHideWindow(_GLFWwindow* window) { ShowWindow(window->win32.handle, SW_HIDE); }
void _glfwPlatformFocusWindow(_GLFWwindow* window) { SetForegroundWindow(window->win32.handle); SetFocus(window->win32.handle); }
void _glfwPlatformSetWindowAttention(_GLFWwindow* window, bool requested) {
    // Deliberately a no-op on Windows, for requests and cancels alike, since no
    // flash is ever started to cancel. FlashWindowEx flashes the taskbar button.
    // Under a Windows pseudoconsole, conhost re-renders the child's output and
    // mangles the shell's BEL-terminated OSC sequences (window title, OSC 7 cwd,
    // OSC 133 prompt marks -- emitted on every command and prompt), delivering the
    // BEL terminators to kitty as standalone bells. That makes bell-on-attention
    // fire constantly on noise (a taskbar flash on every kitten run/exit and
    // prompt), most of it from sources kitty does not control (e.g. the shell/p10k
    // title). So the taskbar alert is net-negative here and is disabled; the
    // (focus-gated) tab bell indicator still covers genuine cases.
    (void)window; (void)requested;
}
int _glfwPlatformWindowBell(_GLFWwindow* window) { (void) window; return MessageBeep(0xFFFFFFFF) ? true : false; }

void _glfwPlatformSetWindowMonitor(_GLFWwindow* window, _GLFWmonitor* monitor, int xpos, int ypos, int width, int height, int refreshRate) {
    (void) window; (void) monitor; (void) xpos; (void) ypos; (void) width; (void) height; (void) refreshRate;
}
bool _glfwPlatformToggleFullscreen(_GLFWwindow* w, unsigned int flags) { (void) w; (void) flags; return false; }
bool _glfwPlatformIsFullscreen(_GLFWwindow* w, unsigned int flags) { (void) w; (void) flags; return false; }

int _glfwPlatformWindowFocused(_GLFWwindow* window) { return window->win32.handle == GetActiveWindow(); }
int _glfwPlatformWindowOccluded(_GLFWwindow* window) { (void) window; return false; }
int _glfwPlatformWindowIconified(_GLFWwindow* window) { return IsIconic(window->win32.handle); }
int _glfwPlatformWindowVisible(_GLFWwindow* window) { return IsWindowVisible(window->win32.handle); }
int _glfwPlatformWindowMaximized(_GLFWwindow* window) { return IsZoomed(window->win32.handle); }
int _glfwPlatformWindowHovered(_GLFWwindow* window) { return window->win32.cursorTracked; }
int _glfwPlatformFramebufferTransparent(_GLFWwindow* window) { return window->win32.transparent; }
float _glfwPlatformGetWindowOpacity(_GLFWwindow* window) { return window->win32.opacity; }

void _glfwPlatformSetWindowResizable(_GLFWwindow* window, bool enabled) { (void) enabled; SetWindowLongW(window->win32.handle, GWL_STYLE, windowStyle(window)); }
void _glfwPlatformSetWindowDecorated(_GLFWwindow* window, bool enabled) { (void) enabled; SetWindowLongW(window->win32.handle, GWL_STYLE, windowStyle(window)); }
void _glfwPlatformSetWindowFloating(_GLFWwindow* window, bool enabled) {
    SetWindowPos(window->win32.handle, enabled ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
void _glfwPlatformSetWindowMousePassthrough(_GLFWwindow* window, bool enabled) { (void) window; (void) enabled; }
void _glfwPlatformSetWindowOpacity(_GLFWwindow* window, float opacity) {
    window->win32.opacity = opacity;
    LONG ex = GetWindowLongW(window->win32.handle, GWL_EXSTYLE);
    if (opacity < 1.f) {
        SetWindowLongW(window->win32.handle, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        SetLayeredWindowAttributes(window->win32.handle, 0, (BYTE)(255 * opacity), LWA_ALPHA);
    } else SetWindowLongW(window->win32.handle, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
}
void _glfwPlatformUpdateIMEState(_GLFWwindow* w, const GLFWIMEUpdateEvent* ev) { (void) w; (void) ev; }

int _glfwPlatformSetWindowBlur(_GLFWwindow* window, int value) {
    window->win32.blur = value;
    updateWindowComposition(window);
    styleTitlebar(window);  // re-read KITTY_TITLEBAR_RGB (config/theme may have changed)
    return _glfw.win32.dwmapi.SetWindowAttribute != NULL;
}
bool _glfwPlatformGrabKeyboard(bool grab) { (void) grab; return false; }

monotonic_t _glfwPlatformGetDoubleClickInterval(_GLFWwindow* window) { (void) window; return ms_to_monotonic_t(GetDoubleClickTime()); }
void _glfwPlatformGetKeyboardRepeatDelay(monotonic_t* delay, monotonic_t* interval) {
    if (delay) *delay = ms_to_monotonic_t(500);
    if (interval) *interval = ms_to_monotonic_t(33);
}

// Layer shell is Wayland-only.
bool _glfwPlatformSetLayerShellConfig(_GLFWwindow* window, const GLFWLayerShellConfig* value) { (void) window; (void) value; return false; }
const GLFWLayerShellConfig* _glfwPlatformGetLayerShellConfig(_GLFWwindow* window) { (void) window; return NULL; }

static void fitToMonitor(_GLFWwindow* window) { (void) window; }

// ---------------------------------------------------------------------------
// Cursor
// ---------------------------------------------------------------------------

void _glfwPlatformGetCursorPos(_GLFWwindow* window, double* xpos, double* ypos) {
    POINT pos;
    if (GetCursorPos(&pos)) { ScreenToClient(window->win32.handle, &pos); if (xpos) *xpos = pos.x; if (ypos) *ypos = pos.y; }
}
void _glfwPlatformSetCursorPos(_GLFWwindow* window, double xpos, double ypos) {
    POINT pos = { (int) xpos, (int) ypos };
    ClientToScreen(window->win32.handle, &pos);
    SetCursorPos(pos.x, pos.y);
}
// glfw has stored the mode already, which applyClientCursor reads
void _glfwPlatformSetCursorMode(_GLFWwindow* window, int mode) {
    (void) mode;
    if (window->win32.cursorTracked) applyClientCursor(window);
}
void _glfwPlatformSetRawMouseMotion(_GLFWwindow* window, bool enabled) { (void) window; (void) enabled; }
bool _glfwPlatformRawMouseMotionSupported(void) { return false; }

int _glfwPlatformCreateCursor(_GLFWcursor* cursor, const GLFWimage* image, int xhot, int yhot, int count) {
    (void) image; (void) xhot; (void) yhot; (void) count;
    cursor->win32.handle = LoadCursorW(NULL, (LPCWSTR) IDC_ARROW);
    return true;
}
int _glfwPlatformCreateStandardCursor(_GLFWcursor* cursor, GLFWCursorShape shape) {
    LPCSTR id = IDC_ARROW;
    switch (shape) {
        case GLFW_TEXT_CURSOR: id = IDC_IBEAM; break;
        case GLFW_POINTER_CURSOR: id = IDC_HAND; break;
        case GLFW_CROSSHAIR_CURSOR: id = IDC_CROSS; break;
        case GLFW_WAIT_CURSOR: id = IDC_WAIT; break;
        case GLFW_MOVE_CURSOR: id = IDC_SIZEALL; break;
        case GLFW_NOT_ALLOWED_CURSOR: id = IDC_NO; break;
        case GLFW_EW_RESIZE_CURSOR: case GLFW_E_RESIZE_CURSOR: case GLFW_W_RESIZE_CURSOR: id = IDC_SIZEWE; break;
        case GLFW_NS_RESIZE_CURSOR: case GLFW_N_RESIZE_CURSOR: case GLFW_S_RESIZE_CURSOR: id = IDC_SIZENS; break;
        case GLFW_NWSE_RESIZE_CURSOR: case GLFW_NW_RESIZE_CURSOR: case GLFW_SE_RESIZE_CURSOR: id = IDC_SIZENWSE; break;
        case GLFW_NESW_RESIZE_CURSOR: case GLFW_NE_RESIZE_CURSOR: case GLFW_SW_RESIZE_CURSOR: id = IDC_SIZENESW; break;
        // The nearest of the cursors Windows has, for the shapes it lacks
        case GLFW_HELP_CURSOR: id = IDC_HELP; break;
        case GLFW_PROGRESS_CURSOR: id = IDC_APPSTARTING; break;
        case GLFW_CELL_CURSOR: id = IDC_CROSS; break;
        case GLFW_VERTICAL_TEXT_CURSOR: id = IDC_IBEAM; break;
        case GLFW_NO_DROP_CURSOR: id = IDC_NO; break;
        case GLFW_GRAB_CURSOR: id = IDC_HAND; break;
        case GLFW_GRABBING_CURSOR: id = IDC_SIZEALL; break;
        default: break;
    }
    cursor->win32.handle = LoadCursorW(NULL, (LPCWSTR) id);
    return true;
}
void _glfwPlatformDestroyCursor(_GLFWcursor* cursor) { (void) cursor; }
// glfw has stored the cursor already, and a pointer hidden while typing stays hidden
void _glfwPlatformSetCursor(_GLFWwindow* window, _GLFWcursor* cursor) {
    (void) cursor;
    if (window->win32.cursorTracked) applyClientCursor(window);
}

// ---------------------------------------------------------------------------
// Event processing
// ---------------------------------------------------------------------------

void _glfwPlatformPollEvents(void) {
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            for (_GLFWwindow* window = _glfw.windowListHead; window; window = window->next)
                _glfwInputWindowCloseRequest(window);
        } else {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}
void _glfwPlatformWaitEvents(void) { WaitMessage(); _glfwPlatformPollEvents(); }
void _glfwPlatformWaitEventsTimeout(monotonic_t timeout) {
    // Round up, so a sub-millisecond timeout waits rather than returning at once.
    MsgWaitForMultipleObjects(0, NULL, FALSE, (DWORD) monotonic_t_to_ms(timeout + ms_to_monotonic_t(1) - 1), QS_ALLINPUT);
    _glfwPlatformPollEvents();
}

// ---------------------------------------------------------------------------
// Drag and drop -- drop target (files/text dropped ONTO a kitty window)
// ---------------------------------------------------------------------------
// Windows delivers drops via OLE: each window registers an IDropTarget and on
// drop we pull the data out of the IDataObject synchronously. We surface it to
// kitty's platform-agnostic drop API as text/uri-list (CF_HDROP file lists, as
// file:// URIs) and text/plain;charset=utf-8 (CF_UNICODETEXT), which kitty then
// pastes into the window (see glfw.c drop_dest_callback and window.py on_drop).
// kitty's own drags, of tabs and windows, are the drag source further below,
// and a drop of one of those onto a kitty window is handed to kitty as its own.

#define DND_MIME_URI_LIST "text/uri-list"
#define DND_MIME_TEXT "text/plain;charset=utf-8"
#define WIN32_DND_MIN(a, b) ((a) < (b) ? (a) : (b))

typedef struct { char *mime; char *data; size_t size, offset; } Win32DropItem;

// Only one drag is ever in flight, so a single global holds its state.
static struct {
    Win32DropItem items[2];   // data extracted at drop time
    size_t count;
    const char *offered[2];   // mimes the active drag offers (enter..leave)
    size_t offered_count;
    _GLFWwindow *window;
} win32_drop;

// A drag kitty started, of a tab or a window. Its mime types and data are
// copied here at the start, since glfw frees its own copy whenever another
// drag is asked for, and the drag outlives that.
#define WIN32_DRAG_MAX_FORMATS 8
static struct {
    bool pending;            // posted, DoDragDrop not running yet
    bool active;             // inside DoDragDrop
    bool escape_pressed, button_released, cancel_requested;
    GLFWid window_id;
    size_t count;
    CLIPFORMAT formats[WIN32_DRAG_MAX_FORMATS];
    char *mimes[WIN32_DRAG_MAX_FORMATS];
    char *data[WIN32_DRAG_MAX_FORMATS];
    size_t sizes[WIN32_DRAG_MAX_FORMATS];
    bool has_image;          // a picture is set, which a thread of its own shows
    POINT released_at;       // where the button came up, in screen pixels
} win32_drag;

// A tab or a window dropped outside kitty opens where it was dropped. kitty
// opens it while it handles the drop, the only time this is on.
static struct { bool on; POINT at; } drop_placement;

static void
placeAtDropPoint(int width, int height, int* x, int* y) {
    if (!drop_placement.on) return;
    MONITORINFO mi = {.cbSize = sizeof mi};
    if (!GetMonitorInfoW(MonitorFromPoint(drop_placement.at, MONITOR_DEFAULTTONEAREST), &mi)) return;
    // Kept inside the work area of the monitor it was dropped on
    const RECT work = mi.rcWork;
    *x = drop_placement.at.x + width > work.right ? work.right - width : drop_placement.at.x;
    *y = drop_placement.at.y + height > work.bottom ? work.bottom - height : drop_placement.at.y;
    if (*x < work.left) *x = work.left;
    if (*y < work.top) *y = work.top;
}

// kitty's tick runs during a drag and can close the window the drag started
// from, which ends the drag as a cancel.
static void
dragSourceWindowClosing(_GLFWwindow* window) {
    if ((win32_drag.pending || win32_drag.active) && win32_drag.window_id == window->id) win32_drag.cancel_requested = true;
}

static void
free_win32_drop_items(void) {
    for (size_t i = 0; i < win32_drop.count; i++) { free(win32_drop.items[i].mime); free(win32_drop.items[i].data); }
    win32_drop.count = 0;
}

static void
add_win32_drop_item(const char *mime, char *data, size_t size) {
    if (!data || win32_drop.count >= sizeof(win32_drop.items) / sizeof(win32_drop.items[0])) { free(data); return; }
    Win32DropItem *it = win32_drop.items + win32_drop.count++;
    it->mime = _glfw_strdup(mime); it->data = data; it->size = size; it->offset = 0;
}

static char*
win32_wide_to_utf8(const wchar_t *w, size_t *out_len) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);  // includes terminating NUL
    if (n <= 0) return NULL;
    char *s = malloc((size_t)n);
    if (!s) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    if (out_len) *out_len = (size_t)(n - 1);
    return s;
}

static bool
buf_append(char **buf, size_t *len, size_t *cap, const char *s, size_t n) {
    if (*len + n + 1 > *cap) {
        size_t ncap = *cap ? *cap : 256;
        while (ncap < *len + n + 1) ncap *= 2;
        char *nb = realloc(*buf, ncap);
        if (!nb) return false;
        *buf = nb; *cap = ncap;
    }
    memcpy(*buf + *len, s, n); *len += n; (*buf)[*len] = 0;
    return true;
}

// Append UTF-8 bytes percent-encoded, leaving RFC3986 unreserved chars plus the
// path separators '/' and ':' (the drive colon) intact.
static bool
uri_append_encoded(char **buf, size_t *len, size_t *cap, const char *s, size_t n) {
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '-' || c == '_' || c == '.' || c == '~' || c == '/' || c == ':';
        if (safe) { if (!buf_append(buf, len, cap, (const char*)&c, 1)) return false; }
        else { char pct[3] = { '%', hex[c >> 4], hex[c & 0xF] }; if (!buf_append(buf, len, cap, pct, 3)) return false; }
    }
    return true;
}

static char*
build_uri_list_from_hdrop(HDROP hdrop, size_t *out_len) {
    UINT count = DragQueryFileW(hdrop, 0xFFFFFFFF, NULL, 0);
    char *buf = NULL; size_t len = 0, cap = 0;
    for (UINT i = 0; i < count; i++) {
        UINT wn = DragQueryFileW(hdrop, i, NULL, 0);
        if (!wn) continue;
        wchar_t *wpath = malloc((wn + 1) * sizeof(wchar_t));
        if (!wpath) continue;
        DragQueryFileW(hdrop, i, wpath, wn + 1);
        for (UINT k = 0; k < wn; k++) if (wpath[k] == L'\\') wpath[k] = L'/';
        size_t u8len = 0; char *u8 = win32_wide_to_utf8(wpath, &u8len);
        free(wpath);
        if (!u8) continue;
        buf_append(&buf, &len, &cap, "file:///", 8);
        uri_append_encoded(&buf, &len, &cap, u8, u8len);
        buf_append(&buf, &len, &cap, "\r\n", 2);
        free(u8);
    }
    if (out_len) *out_len = len;
    return buf;
}

static char*
win32_get_format(IDataObject *pdo, CLIPFORMAT cf, size_t *out_len) {
    FORMATETC fmt = { cf, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stg;
    if (FAILED(pdo->lpVtbl->GetData(pdo, &fmt, &stg))) return NULL;
    char *result = NULL;
    void *p = GlobalLock(stg.hGlobal);
    if (p) {
        if (cf == CF_HDROP) result = build_uri_list_from_hdrop((HDROP)p, out_len);
        else result = win32_wide_to_utf8((const wchar_t*)p, out_len);
        GlobalUnlock(stg.hGlobal);
    }
    ReleaseStgMedium(&stg);
    return result;
}

static size_t
collect_drop_mimes(IDataObject *pdo, const char **mimes) {
    size_t n = 0;
    FORMATETC f = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    if (pdo->lpVtbl->QueryGetData(pdo, &f) == S_OK) mimes[n++] = DND_MIME_URI_LIST;
    f.cfFormat = CF_UNICODETEXT;
    if (pdo->lpVtbl->QueryGetData(pdo, &f) == S_OK) mimes[n++] = DND_MIME_TEXT;
    return n;
}

static void
drop_point_to_client(_GLFWwindow *window, POINTL pt, double *x, double *y) {
    POINT p = { pt.x, pt.y };
    ScreenToClient(window->win32.handle, &p);
    *x = p.x; *y = p.y;
}

// --- IDropTarget COM object (one per window) ---
typedef struct { IDropTarget iface; LONG ref; _GLFWwindow *window; } Win32DropTarget;

static HRESULT STDMETHODCALLTYPE dt_QueryInterface(IDropTarget *This, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropTarget)) {
        *ppv = This; This->lpVtbl->AddRef(This); return S_OK;
    }
    *ppv = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE dt_AddRef(IDropTarget *This) {
    return InterlockedIncrement(&((Win32DropTarget*)This)->ref);
}
static ULONG STDMETHODCALLTYPE dt_Release(IDropTarget *This) {
    Win32DropTarget *self = (Win32DropTarget*)This;
    LONG r = InterlockedDecrement(&self->ref);
    if (r == 0) free(self);
    return r;
}
// A drag of kitty's own reaches a kitty window here. kitty already holds what it
// dragged, so it only needs the mime types, to tell a tab from a window, and to
// know the drag is its own, which makes it use that data rather than read any.
static size_t
self_drag_event(_GLFWwindow *window, GLFWDropEventType type, POINTL pt) {
    double x, y; drop_point_to_client(window, pt, &x, &y);
    const char *m[WIN32_DRAG_MAX_FORMATS];
    for (size_t i = 0; i < win32_drag.count; i++) m[i] = win32_drag.mimes[i];
    return win32_drag.count ? _glfwInputDropEvent(window, type, x, y, m, win32_drag.count, true) : 0;
}

// A drag holds its own reference to the target under the pointer, so the
// target can outlive its window, which leaves the window NULL behind it.
static HRESULT STDMETHODCALLTYPE dt_DragEnter(IDropTarget *This, IDataObject *pdo, DWORD keys, POINTL pt, DWORD *effect) {
    (void)keys;
    Win32DropTarget *self = (Win32DropTarget*)This;
    if (!self->window) { *effect = DROPEFFECT_NONE; return S_OK; }
    win32_drop.window = self->window;
    if (win32_drag.active) {
        win32_drop.offered_count = 0;
        *effect = self_drag_event(self->window, GLFW_DROP_ENTER, pt) ? DROPEFFECT_MOVE : DROPEFFECT_NONE;
        return S_OK;
    }
    win32_drop.offered_count = collect_drop_mimes(pdo, win32_drop.offered);
    double x, y; drop_point_to_client(self->window, pt, &x, &y);
    if (win32_drop.offered_count) {
        const char *m[2]; for (size_t i = 0; i < win32_drop.offered_count; i++) m[i] = win32_drop.offered[i];
        _glfwInputDropEvent(self->window, GLFW_DROP_ENTER, x, y, m, win32_drop.offered_count, false);
    }
    *effect = win32_drop.offered_count ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dt_DragOver(IDropTarget *This, DWORD keys, POINTL pt, DWORD *effect) {
    (void)keys;
    Win32DropTarget *self = (Win32DropTarget*)This;
    if (!self->window) { *effect = DROPEFFECT_NONE; return S_OK; }
    if (win32_drag.active) {
        *effect = self_drag_event(self->window, GLFW_DROP_MOVE, pt) ? DROPEFFECT_MOVE : DROPEFFECT_NONE;
        return S_OK;
    }
    double x, y; drop_point_to_client(self->window, pt, &x, &y);
    if (win32_drop.offered_count) {
        const char *m[2]; for (size_t i = 0; i < win32_drop.offered_count; i++) m[i] = win32_drop.offered[i];
        _glfwInputDropEvent(self->window, GLFW_DROP_MOVE, x, y, m, win32_drop.offered_count, false);
    }
    *effect = win32_drop.offered_count ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dt_DragLeave(IDropTarget *This) {
    Win32DropTarget *self = (Win32DropTarget*)This;
    if (self->window) _glfwInputDropEvent(self->window, GLFW_DROP_LEAVE, 0, 0, NULL, 0, win32_drag.active);
    win32_drop.offered_count = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dt_Drop(IDropTarget *This, IDataObject *pdo, DWORD keys, POINTL pt, DWORD *effect) {
    (void)keys;
    Win32DropTarget *self = (Win32DropTarget*)This;
    if (!self->window) { *effect = DROPEFFECT_NONE; return S_OK; }
    free_win32_drop_items();
    win32_drop.window = self->window;
    win32_drop.offered_count = 0;
    if (win32_drag.active) {
        *effect = self_drag_event(self->window, GLFW_DROP_DROP, pt) ? DROPEFFECT_MOVE : DROPEFFECT_NONE;
        return S_OK;
    }
    size_t u8len; char *u8;
    if ((u8 = win32_get_format(pdo, CF_HDROP, &u8len))) add_win32_drop_item(DND_MIME_URI_LIST, u8, u8len);
    if ((u8 = win32_get_format(pdo, CF_UNICODETEXT, &u8len))) add_win32_drop_item(DND_MIME_TEXT, u8, u8len);
    if (win32_drop.count) {
        const char *m[2]; for (size_t i = 0; i < win32_drop.count; i++) m[i] = win32_drop.items[i].mime;
        double x, y; drop_point_to_client(self->window, pt, &x, &y);
        size_t accepted = _glfwInputDropEvent(self->window, GLFW_DROP_DROP, x, y, m, win32_drop.count, false);
        for (size_t i = 0; i < accepted; i++) _glfwPlatformRequestDropData(self->window, m[i]);
        *effect = DROPEFFECT_COPY;
    } else *effect = DROPEFFECT_NONE;
    return S_OK;
}

static IDropTargetVtbl win32_drop_target_vtbl = {
    dt_QueryInterface, dt_AddRef, dt_Release, dt_DragEnter, dt_DragOver, dt_DragLeave, dt_Drop
};

void
_glfwWin32RegisterDropTarget(_GLFWwindow *window) {
    static bool ole_inited = false;
    if (!ole_inited) { if (FAILED(OleInitialize(NULL))) return; ole_inited = true; }
    Win32DropTarget *dt = calloc(1, sizeof(Win32DropTarget));
    if (!dt) return;
    dt->iface.lpVtbl = &win32_drop_target_vtbl; dt->ref = 1; dt->window = window;
    if (RegisterDragDrop(window->win32.handle, &dt->iface) != S_OK) { dt->iface.lpVtbl->Release(&dt->iface); return; }
    window->win32.dropTarget = &dt->iface;
}

void
_glfwWin32RevokeDropTarget(_GLFWwindow *window) {
    IDropTarget *dt = (IDropTarget*)window->win32.dropTarget;
    if (dt) {
        RevokeDragDrop(window->win32.handle);
        ((Win32DropTarget*)dt)->window = NULL;
        dt->lpVtbl->Release(dt);
        window->win32.dropTarget = NULL;
        if (win32_drop.window == window) { free_win32_drop_items(); win32_drop.window = NULL; win32_drop.offered_count = 0; }
    }
}

// After a chunk is read, another GLFW_DROP_DATA_AVAILABLE must be posted so the
// reader is driven to completion (a 0-length read signals the mime is done).
typedef struct { char *mime; GLFWid window_id; } win32_drop_notify;
static void win32_free_drop_notify(unsigned long long tid, void *x) { (void)tid; win32_drop_notify *d = x; free(d->mime); free(d); }
static void win32_notify_drop_data(unsigned long long tid, void *x) {
    (void)tid;
    win32_drop_notify *d = x;
    _GLFWwindow *window = _glfwWindowForId(d->window_id);
    if (window) { const char *m[1] = { d->mime }; _glfwInputDropEvent(window, GLFW_DROP_DATA_AVAILABLE, 0, 0, m, 1, false); }
}

void _glfwPlatformRequestDropUpdate(_GLFWwindow* window) { (void) window; }  // OLE DragOver drives continuous updates

ssize_t _glfwPlatformReadAvailableDropData(GLFWwindow* w, GLFWDropEvent* ev, char* buffer, size_t sz) {
    _GLFWwindow *window = (_GLFWwindow*)w;
    const char *mime = ev->mimes[0];
    for (size_t i = 0; i < win32_drop.count; i++) {
        Win32DropItem *it = win32_drop.items + i;
        if (strcmp(it->mime, mime) == 0) {
            if (it->offset >= it->size) return 0;
            size_t to_read = WIN32_DND_MIN(sz, it->size - it->offset);
            memcpy(buffer, it->data + it->offset, to_read);
            it->offset += to_read;
            if (to_read) {
                win32_drop_notify *d = malloc(sizeof(win32_drop_notify));
                if (d) { d->mime = _glfw_strdup(mime); d->window_id = window->id; _glfwPlatformAddTimer(0, false, win32_notify_drop_data, d, win32_free_drop_notify); }
            }
            return (ssize_t)to_read;
        }
    }
    return -ENOENT;
}

void _glfwPlatformEndDrop(GLFWwindow* w, GLFWDragOperationType op) { (void) w; (void) op; free_win32_drop_items(); }

int _glfwPlatformRequestDropData(_GLFWwindow* window, const char* mime) {
    for (size_t i = 0; i < win32_drop.count; i++) {
        if (strcmp(win32_drop.items[i].mime, mime) == 0) {
            win32_drop.items[i].offset = 0;
            const char *m[1] = { win32_drop.items[i].mime };
            _glfwInputDropEvent(window, GLFW_DROP_DATA_AVAILABLE, 0, 0, m, 1, false);
            return 0;
        }
    }
    return EINVAL;
}
// ---------------------------------------------------------------------------
// Drag and drop -- drag source (kitty's own drags of tabs and windows)
// ---------------------------------------------------------------------------
// Each mime type travels as a clipboard format registered under its own name.
// DoDragDrop runs a modal loop, so the drag starts from a posted message rather
// than inside the call that asked for it, which comes from kitty's Python, and
// the repaint timer the live resize uses keeps kitty drawing during the loop.
// Dragging content out to other programs, the dnd kitten's side, needs the
// client drag protocol and is not done here.

static void
free_win32_drag(void) {
    for (size_t i = 0; i < win32_drag.count; i++) { free(win32_drag.mimes[i]); free(win32_drag.data[i]); }
    win32_drag.count = 0;
}

static int
drag_format_index(const FORMATETC *f) {
    if (!f || !(f->tymed & TYMED_HGLOBAL) || f->dwAspect != DVASPECT_CONTENT) return -1;
    for (size_t i = 0; i < win32_drag.count; i++) if (win32_drag.formats[i] == f->cfFormat) return (int)i;
    return -1;
}

// The data for one format in an HGLOBAL. It is zero filled, since GlobalSize may
// round the size up and a reader trusting it then sees the padding as data.
static HGLOBAL
drag_data_for(size_t idx) {
    const size_t sz = win32_drag.sizes[idx];
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sz ? sz : 1);
    if (h) {
        void *p = GlobalLock(h);
        if (p) { if (sz) memcpy(p, win32_drag.data[idx], sz); GlobalUnlock(h); }
        else { GlobalFree(h); h = NULL; }
    }
    return h;
}

// --- IDataObject COM object, offering the drag's formats ---
// The shell's drag helpers keep their own state in the data object through
// SetData, the drag picture and the drop target's hints among it, so what
// they set is held and handed back beside the drag's own formats.
#define WIN32_DRAG_MAX_STORED 16
typedef struct {
    IDataObject iface; LONG ref;
    size_t stored_count;
    FORMATETC stored_formats[WIN32_DRAG_MAX_STORED];
    STGMEDIUM stored_media[WIN32_DRAG_MAX_STORED];
} Win32DragData;

static int
stored_format_index(const Win32DragData *self, const FORMATETC *f) {
    if (!f) return -1;
    for (size_t i = 0; i < self->stored_count; i++) {
        const FORMATETC *s = self->stored_formats + i;
        if (s->cfFormat == f->cfFormat && s->dwAspect == f->dwAspect && (s->tymed & f->tymed)) return (int)i;
    }
    return -1;
}

// A copy of a stored medium for a reader, who releases it with ReleaseStgMedium
static bool
copy_stored_medium(CLIPFORMAT cf, const STGMEDIUM *src, STGMEDIUM *dst) {
    *dst = *src;
    switch (src->tymed) {
        case TYMED_HGLOBAL:
            dst->hGlobal = OleDuplicateData(src->hGlobal, cf, 0);
            dst->pUnkForRelease = NULL;
            return dst->hGlobal != NULL;
        case TYMED_ISTREAM: dst->pstm->lpVtbl->AddRef(dst->pstm); break;
        case TYMED_ISTORAGE: dst->pstg->lpVtbl->AddRef(dst->pstg); break;
        default: return false;
    }
    if (dst->pUnkForRelease) dst->pUnkForRelease->lpVtbl->AddRef(dst->pUnkForRelease);
    return true;
}

static HRESULT STDMETHODCALLTYPE dd_QueryInterface(IDataObject *This, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDataObject)) {
        *ppv = This; This->lpVtbl->AddRef(This); return S_OK;
    }
    *ppv = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE dd_AddRef(IDataObject *This) { return InterlockedIncrement(&((Win32DragData*)This)->ref); }
static ULONG STDMETHODCALLTYPE dd_Release(IDataObject *This) {
    Win32DragData *self = (Win32DragData*)This;
    LONG r = InterlockedDecrement(&self->ref);
    if (r == 0) {
        for (size_t i = 0; i < self->stored_count; i++) ReleaseStgMedium(self->stored_media + i);
        free(self);
    }
    return r;
}
static HRESULT STDMETHODCALLTYPE dd_GetData(IDataObject *This, FORMATETC *f, STGMEDIUM *m) {
    Win32DragData *self = (Win32DragData*)This;
    if (!m) return E_POINTER;
    const int stored = stored_format_index(self, f);
    if (stored >= 0) return copy_stored_medium(f->cfFormat, self->stored_media + stored, m) ? S_OK : E_OUTOFMEMORY;
    int idx = drag_format_index(f);
    if (idx < 0) return DV_E_FORMATETC;
    HGLOBAL h = drag_data_for((size_t)idx);
    if (!h) return E_OUTOFMEMORY;
    m->tymed = TYMED_HGLOBAL; m->hGlobal = h; m->pUnkForRelease = NULL;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dd_GetDataHere(IDataObject *This, FORMATETC *f, STGMEDIUM *m) { (void)This; (void)f; (void)m; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE dd_QueryGetData(IDataObject *This, FORMATETC *f) {
    return (stored_format_index((Win32DragData*)This, f) < 0 && drag_format_index(f) < 0) ? DV_E_FORMATETC : S_OK;
}
static HRESULT STDMETHODCALLTYPE dd_GetCanonicalFormatEtc(IDataObject *This, FORMATETC *in, FORMATETC *out) {
    (void)This; (void)in;
    if (out) out->ptd = NULL;
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE dd_SetData(IDataObject *This, FORMATETC *f, STGMEDIUM *m, BOOL release) {
    Win32DragData *self = (Win32DragData*)This;
    if (!f || !m || f->ptd) return E_INVALIDARG;
    STGMEDIUM held;
    if (release) held = *m;
    else if (!copy_stored_medium(f->cfFormat, m, &held)) return DV_E_TYMED;
    int idx = stored_format_index(self, f);
    if (idx >= 0) ReleaseStgMedium(self->stored_media + idx);
    else if (self->stored_count < WIN32_DRAG_MAX_STORED) idx = (int)self->stored_count++;
    else { if (!release) ReleaseStgMedium(&held); return E_OUTOFMEMORY; }
    self->stored_formats[idx] = *f;
    self->stored_formats[idx].tymed = held.tymed;
    self->stored_media[idx] = held;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dd_EnumFormatEtc(IDataObject *This, DWORD direction, IEnumFORMATETC **out) {
    const Win32DragData *self = (Win32DragData*)This;
    if (direction != DATADIR_GET) return E_NOTIMPL;
    FORMATETC f[WIN32_DRAG_MAX_FORMATS + WIN32_DRAG_MAX_STORED];
    size_t n = 0;
    for (size_t i = 0; i < win32_drag.count; i++) f[n++] = (FORMATETC){win32_drag.formats[i], NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    for (size_t i = 0; i < self->stored_count; i++) f[n++] = self->stored_formats[i];
    return SHCreateStdEnumFmtEtc((UINT)n, f, out);
}
static HRESULT STDMETHODCALLTYPE dd_DAdvise(IDataObject *This, FORMATETC *f, DWORD adv, IAdviseSink *sink, DWORD *conn) {
    (void)This; (void)f; (void)adv; (void)sink; (void)conn;
    return OLE_E_ADVISENOTSUPPORTED;
}
static HRESULT STDMETHODCALLTYPE dd_DUnadvise(IDataObject *This, DWORD conn) { (void)This; (void)conn; return OLE_E_ADVISENOTSUPPORTED; }
static HRESULT STDMETHODCALLTYPE dd_EnumDAdvise(IDataObject *This, IEnumSTATDATA **out) { (void)This; (void)out; return OLE_E_ADVISENOTSUPPORTED; }

static IDataObjectVtbl win32_drag_data_vtbl = {
    dd_QueryInterface, dd_AddRef, dd_Release, dd_GetData, dd_GetDataHere, dd_QueryGetData,
    dd_GetCanonicalFormatEtc, dd_SetData, dd_EnumFormatEtc, dd_DAdvise, dd_DUnadvise, dd_EnumDAdvise
};

bool _glfwWin32DragInProgress(void) { return win32_drag.active; }

// --- The drag picture, a layered window that follows the pointer ---
// It lives on a thread of its own. The drag's modal loop runs on the main
// thread, which also draws kitty, so a picture moved from there followed the
// pointer in steps whenever a frame was being drawn. This thread moves it once
// per display refresh, right before DWM composes, whatever the main thread is
// doing, and waits on its message queue while no picture shows.
#define DRAG_IMAGE_CLASS L"GLFW_kitty_drag_image"
#define WM_APP_DRAG_IMAGE (WM_APP + 10)  // wParam: show it, lParam: a new DragImageBits, or NULL to keep the picture

typedef struct { int width, height; uint8_t *bgra; } DragImageBits;  // premultiplied, top down

static struct {
    DWORD thread_id;  // written by the main thread, the rest only by the picture's thread
    HWND window;
    int height;
    bool visible;
} drag_image;

// Where macOS puts it, the lower left corner a little above the pointer. The
// window is click through, so OLE still finds the window under the pointer to
// deliver the drop to.
static void
place_drag_image(void) {
    POINT p; GetCursorPos(&p);
    const int gap = MulDiv(20, windowDpi(drag_image.window), 96);
    SetWindowPos(drag_image.window, NULL, p.x, p.y - drag_image.height - gap, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
}

static void
update_drag_image(bool show, DragImageBits *bits) {
    if (!drag_image.window) show = false;
    if (bits && drag_image.window) {
        BITMAPINFO bi; ZeroMemory(&bi, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = bits->width; bi.bmiHeader.biHeight = -bits->height;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        HDC screen = GetDC(NULL);
        HDC dc = CreateCompatibleDC(screen);
        void *pixels = NULL;
        HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &pixels, NULL, 0);
        if (dib && pixels) {
            memcpy(pixels, bits->bgra, (size_t)bits->width * bits->height * 4);
            HGDIOBJ old = SelectObject(dc, dib);
            POINT origin = {0, 0}; SIZE size = {bits->width, bits->height};
            BLENDFUNCTION bf = {AC_SRC_OVER, 0, 216, AC_SRC_ALPHA};  // a touch see-through, as drag pictures are
            UpdateLayeredWindow(drag_image.window, screen, NULL, &size, dc, &origin, 0, &bf, ULW_ALPHA);
            SelectObject(dc, old);
            drag_image.height = bits->height;
        }
        if (dib) DeleteObject(dib);
        DeleteDC(dc); ReleaseDC(NULL, screen);
    }
    if (bits) { free(bits->bgra); free(bits); }
    if (show) place_drag_image();
    if (show != drag_image.visible) ShowWindow(drag_image.window, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    drag_image.visible = show;
}

static void
handle_drag_image_message(MSG *msg) {
    if (msg->message == WM_APP_DRAG_IMAGE && !msg->hwnd) update_drag_image(msg->wParam != 0, (DragImageBits*)msg->lParam);
    else { TranslateMessage(msg); DispatchMessageW(msg); }
}

static DWORD WINAPI
drag_image_thread(LPVOID ready) {
    WNDCLASSEXW wc; ZeroMemory(&wc, sizeof wc); wc.cbSize = sizeof wc;
    wc.lpfnWndProc = DefWindowProcW; wc.hInstance = _glfw.win32.instance; wc.lpszClassName = DRAG_IMAGE_CLASS;
    RegisterClassExW(&wc);
    drag_image.window = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        DRAG_IMAGE_CLASS, L"", WS_POPUP, 0, 0, 1, 1, NULL, NULL, _glfw.win32.instance, NULL);
    MSG msg;
    PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE);  // the queue exists before anything is posted to it
    SetEvent((HANDLE)ready);
    for (;;) {
        if (!drag_image.visible) {
            if (GetMessageW(&msg, NULL, 0, 0) <= 0) return 0;
            handle_drag_image_message(&msg);
        }
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 0;
            handle_drag_image_message(&msg);
        }
        if (drag_image.visible) {
            place_drag_image();
            if (FAILED(DwmFlush())) Sleep(4);
        }
    }
}

static void
post_drag_image(bool show, DragImageBits *bits) {
    if (!drag_image.thread_id && (bits || show)) {
        HANDLE ready = CreateEventW(NULL, TRUE, FALSE, NULL);
        DWORD tid = 0;
        HANDLE t = ready ? CreateThread(NULL, 0, drag_image_thread, ready, 0, &tid) : NULL;
        if (t) {
            WaitForSingleObject(ready, 2000);
            CloseHandle(t);
            drag_image.thread_id = tid;
        }
        if (ready) CloseHandle(ready);
    }
    if (!drag_image.thread_id || !PostThreadMessageW(drag_image.thread_id, WM_APP_DRAG_IMAGE, show, (LPARAM)bits)) {
        if (bits) { free(bits->bgra); free(bits); }
    }
}

static void
set_drag_image(const GLFWimage *thumbnail) {
    win32_drag.has_image = false;
    if (!thumbnail || !thumbnail->pixels || thumbnail->width <= 0 || thumbnail->height <= 0) {
        post_drag_image(false, NULL);
        return;
    }
    const int w = thumbnail->width, h = thumbnail->height;
    DragImageBits *bits = malloc(sizeof *bits);
    uint8_t *bgra = malloc((size_t)w * h * 4);
    if (!bits || !bgra) { free(bits); free(bgra); return; }
    // kitty's pixels are straight RGBA, a layered window wants premultiplied BGRA
    const uint8_t *src = thumbnail->pixels;
    uint8_t *dst = bgra;
    for (int i = 0; i < w * h; i++, src += 4, dst += 4) {
        const uint8_t a = src[3];
        dst[0] = (uint8_t)(src[2] * a / 255); dst[1] = (uint8_t)(src[1] * a / 255);
        dst[2] = (uint8_t)(src[0] * a / 255); dst[3] = a;
    }
    *bits = (DragImageBits){.width = w, .height = h, .bgra = bgra};
    win32_drag.has_image = true;
    post_drag_image(win32_drag.active, bits);
}

// --- IDropSource COM object ---
typedef struct { IDropSource iface; LONG ref; } Win32DropSource;

static HRESULT STDMETHODCALLTYPE ds_QueryInterface(IDropSource *This, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropSource)) {
        *ppv = This; This->lpVtbl->AddRef(This); return S_OK;
    }
    *ppv = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE ds_AddRef(IDropSource *This) { return InterlockedIncrement(&((Win32DropSource*)This)->ref); }
static ULONG STDMETHODCALLTYPE ds_Release(IDropSource *This) {
    Win32DropSource *self = (Win32DropSource*)This;
    LONG r = InterlockedDecrement(&self->ref);
    if (r == 0) free(self);
    return r;
}
// Escape and a release both end the drag, and only this sees which it was. A
// release counts as a drop even over something that accepts nothing, which is
// how kitty learns a tab was dropped outside every kitty window.
static HRESULT STDMETHODCALLTYPE ds_QueryContinueDrag(IDropSource *This, BOOL escape, DWORD keys) {
    (void)This;
    if (escape) { win32_drag.escape_pressed = true; return DRAGDROP_S_CANCEL; }
    if (win32_drag.cancel_requested) return DRAGDROP_S_CANCEL;
    if (!(keys & MK_LBUTTON)) {
        win32_drag.button_released = true;
        GetCursorPos(&win32_drag.released_at);
        return DRAGDROP_S_DROP;
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ds_GiveFeedback(IDropSource *This, DWORD effect) {
    (void)This; (void)effect;
    return DRAGDROP_S_USEDEFAULTCURSORS;
}

static IDropSourceVtbl win32_drop_source_vtbl = {
    ds_QueryInterface, ds_AddRef, ds_Release, ds_QueryContinueDrag, ds_GiveFeedback
};

static DWORD
allowed_drop_effects(void) {
    const int ops = _glfw.drag.operations;
    DWORD e = 0;
    if (ops & GLFW_DRAG_OPERATION_COPY) e |= DROPEFFECT_COPY;
    if (ops & GLFW_DRAG_OPERATION_MOVE) e |= DROPEFFECT_MOVE;
    if ((ops & GLFW_DRAG_OPERATION_GENERIC) || !e) e |= DROPEFFECT_COPY | DROPEFFECT_MOVE;
    return e;
}

// The shell's drop targets, the taskbar and Explorer among them, draw a
// generic file picture under the pointer when the drag carries none, moved
// from their own process and so behind the pointer. A transparent one pixel
// picture keeps them from it, leaving kitty's own picture the only one.
static void
blank_shell_drag_image(IDataObject *data) {
    IDragSourceHelper *helper = NULL;
    if (FAILED(CoCreateInstance(&CLSID_DragDropHelper, NULL, CLSCTX_INPROC_SERVER, &IID_IDragSourceHelper, (void**)&helper))) return;
    BITMAPINFO bmi = {.bmiHeader = {
        .biSize = sizeof(BITMAPINFOHEADER), .biWidth = 1, .biHeight = -1, .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB}};
    void *bits = NULL;
    HBITMAP bitmap = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bitmap) {
        SHDRAGIMAGE image = {.sizeDragImage = {1, 1}, .hbmpDragImage = bitmap, .crColorKey = CLR_NONE};
        // The helper owns the bitmap once this succeeds
        if (FAILED(helper->lpVtbl->InitializeFromBitmap(helper, &image, data))) DeleteObject(bitmap);
    }
    helper->lpVtbl->Release(helper);
}

static void
runPendingDrag(void) {
    if (!win32_drag.pending) return;
    win32_drag.pending = false;
    _GLFWwindow *window = _glfwWindowForId(win32_drag.window_id);
    GLFWDragEvent ev = {.type = GLFW_DRAG_CANCELLED};
    Win32DragData *data = calloc(1, sizeof *data);
    Win32DropSource *source = calloc(1, sizeof *source);
    if (data) { data->iface.lpVtbl = &win32_drag_data_vtbl; data->ref = 1; }
    if (source) { source->iface.lpVtbl = &win32_drop_source_vtbl; source->ref = 1; }
    if (window && data && source && win32_drag.count && !win32_drag.cancel_requested) {
        blank_shell_drag_image(&data->iface);
        win32_drag.active = true;
        if (win32_drag.has_image) post_drag_image(true, NULL);
        // kitty's own loop is blocked until DoDragDrop returns, so a repaint
        // timer like the live resize's runs it meanwhile, for its drop previews,
        // its timers and the output of its windows. The window may close during
        // the drag, so its ticks go to the helper window.
        const HWND helper = _glfw.win32.helperWindowHandle;
        startTickTimer(helper, resizeTimerMs(window->win32.handle));
        DWORD effect = DROPEFFECT_NONE;
        const HRESULT hr = DoDragDrop(&data->iface, &source->iface, allowed_drop_effects(), &effect);
        stopResizeTimer(helper);
        win32_drag.active = false;
        post_drag_image(false, NULL);
        if (hr == DRAGDROP_S_DROP && effect != DROPEFFECT_NONE) {
            ev.type = GLFW_DRAG_DROPPED;
            ev.action = (effect & DROPEFFECT_MOVE) ? GLFW_DRAG_OPERATION_MOVE : GLFW_DRAG_OPERATION_COPY;
        } else if (win32_drag.button_released && !win32_drag.escape_pressed && !win32_drag.cancel_requested) {
            ev.type = GLFW_DRAG_DROPPED;
            ev.drop_maybe_a_cancel = true;
        }
    }
    if (data) data->iface.lpVtbl->Release(&data->iface);
    if (source) source->iface.lpVtbl->Release(&source->iface);
    // kitty's handler for the end of its drag does not look at the window, so
    // when the one the drag started from has closed, any window left carries it
    _GLFWwindow *from = _glfwWindowForId(win32_drag.window_id);
    if (!from) from = _glfw.windowListHead;
    if (from) {
        drop_placement.on = ev.drop_maybe_a_cancel;
        drop_placement.at = win32_drag.released_at;
        _glfwInputDragSourceRequest(from, &ev);
        drop_placement.on = false;
        if (ev.type == GLFW_DRAG_DROPPED) {
            GLFWDragEvent finished = {.type = GLFW_DRAG_FINSHED, .action = ev.action};
            _glfwInputDragSourceRequest(from, &finished);
        }
    }
    free_win32_drag();
    _glfwFreeDragSourceData();
}

int _glfwPlatformStartDrag(_GLFWwindow* window, const GLFWimage* thumbnail) {
    if (win32_drag.pending || win32_drag.active) return EBUSY;
    free_win32_drag();
    for (size_t i = 0; i < _glfw.drag.item_count && win32_drag.count < WIN32_DRAG_MAX_FORMATS; i++) {
        const GLFWDragSourceItem *item = _glfw.drag.items + i;
        // Data kitty hands over only when asked belongs to the drags programs
        // in a window start, the dnd kitten's side, which is not done here
        if (!item->optional_data) continue;
        const UINT cf = RegisterClipboardFormatA(item->mime_type);
        if (!cf) continue;
        const size_t n = win32_drag.count;
        win32_drag.formats[n] = (CLIPFORMAT)cf;
        win32_drag.mimes[n] = _glfw_strdup(item->mime_type);
        win32_drag.sizes[n] = item->data_size;
        win32_drag.data[n] = malloc(item->data_size ? item->data_size : 1);
        if (!win32_drag.mimes[n] || !win32_drag.data[n]) {
            free(win32_drag.mimes[n]); free(win32_drag.data[n]);
            free_win32_drag();
            return ENOMEM;
        }
        if (item->data_size) memcpy(win32_drag.data[n], item->optional_data, item->data_size);
        win32_drag.count++;
    }
    if (!win32_drag.count) return EINVAL;
    win32_drag.window_id = window->id;
    win32_drag.escape_pressed = win32_drag.button_released = win32_drag.cancel_requested = false;
    set_drag_image(thumbnail);
    if (!_glfw.win32.helperWindowHandle || !PostMessageW(_glfw.win32.helperWindowHandle, WM_APP_START_DRAG, 0, 0)) {
        free_win32_drag();
        return EIO;
    }
    win32_drag.pending = true;
    return 0;
}

// The drag ends, and kitty hears it was cancelled, once DoDragDrop asks
// whether to go on, or at once when it has not started yet.
void _glfwPlatformCancelDrag(_GLFWwindow* window) {
    (void) window;
    if (win32_drag.pending || win32_drag.active) win32_drag.cancel_requested = true;
}

void _glfwPlatformFreeDragSourceData(void) {
    // glfw frees its copy of a drag when another is asked for. One that has not
    // started by then is dropped, one in flight keeps its own copy.
    if (win32_drag.pending) win32_drag.cancel_requested = true;
}

// No drag here asks for its data on demand, see _glfwPlatformStartDrag
int _glfwPlatformDragDataReady(const char* mime_type, const char* data, size_t sz, int type) { (void) mime_type; (void) data; (void) sz; (void) type; return 0; }

int _glfwPlatformChangeDragImage(const GLFWimage* thumbnail) {
    if (!win32_drag.pending && !win32_drag.active) return 0;
    set_drag_image(thumbnail);
    return 0;
}

// Public API defined per platform. Layer shell is a Wayland concept.
GLFWAPI bool glfwIsLayerShellSupported(void) { return false; }
