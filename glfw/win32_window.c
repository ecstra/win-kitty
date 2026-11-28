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
#include <imm.h>

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

// TranslateMessage posts the character a key types before the key itself is
// dispatched, so whether one follows can be read off the queue
static bool charFollows(HWND hWnd) {
    MSG m;
    return PeekMessageW(&m, hWnd, WM_CHAR, WM_DEADCHAR, PM_NOREMOVE) || PeekMessageW(&m, hWnd, WM_SYSCHAR, WM_SYSDEADCHAR, PM_NOREMOVE);
}

// The keypad keys that type a character, which WM_CHAR delivers
static bool isKeypadTextKey(uint32_t key) {
    return (key >= GLFW_FKEY_KP_0 && key <= GLFW_FKEY_KP_9) || key == GLFW_FKEY_KP_DECIMAL || key == GLFW_FKEY_KP_DIVIDE ||
        key == GLFW_FKEY_KP_MULTIPLY || key == GLFW_FKEY_KP_SUBTRACT || key == GLFW_FKEY_KP_ADD || key == GLFW_FKEY_KP_SEPARATOR;
}

// The character the active layout gives a key with no modifiers, or with only
// shift, Caps Lock left out as on X11. A dead key gives its own character.
// The flag leaves the keyboard state alone, so a dead key waiting for the next
// key still composes with it.
static uint32_t layoutChar(WPARAM vk, UINT scan, bool shifted) {
    BYTE state[256] = {0};
    if (shifted) state[VK_SHIFT] = 0x80;
    WCHAR buf[4] = {0};
    int n = ToUnicodeEx((UINT) vk, scan, state, buf, 4, 0x4, GetKeyboardLayout(0));
    if (n < 0) n = 1;  // a dead key, whose character is in buf
    if (n == 2 && IS_HIGH_SURROGATE(buf[0]) && IS_LOW_SURROGATE(buf[1]))
        return 0x10000 + (((uint32_t) (buf[0] - 0xd800) << 10) | (uint32_t) (buf[1] - 0xdc00));
    if (n == 1 && buf[0] >= 0x20 && buf[0] != 0x7f) return buf[0];
    return 0;
}

// The key a US keyboard has at that place, by scan code, which kitty sends as
// the alternate key. kitty on Linux takes it from libxkbcommon's default keymap,
// pc105 US whatever layouts the user has, so Ctrl with the С of a Russian layout
// still sends ^C, and Alt with the ü of a German one sends ESC [ there too. The
// key beside the left Shift is < there, where Windows' US layout has a backslash,
// which made Ctrl with the í of Hungarian send ^\, a quit signal in a Unix shell.
static uint32_t usKeyAtScanCode(UINT scan) {
    static const char row1[] = "1234567890-=", row2[] = "qwertyuiop[]", row3[] = "asdfghjkl;'`", row4[] = "zxcvbnm,./";
    if (scan >= 0x02 && scan <= 0x0d) return (uint32_t) row1[scan - 0x02];
    if (scan >= 0x10 && scan <= 0x1b) return (uint32_t) row2[scan - 0x10];
    if (scan >= 0x1e && scan <= 0x29) return (uint32_t) row3[scan - 0x1e];
    if (scan >= 0x2c && scan <= 0x35) return (uint32_t) row4[scan - 0x2c];
    if (scan == 0x2b) return '\\';
    if (scan == 0x56) return '<';
    if (scan == 0x39) return ' ';
    return 0;
}

// The key table is US by virtual key, which on another layout names the wrong
// character, or none for a key a US keyboard lacks, so a key that types goes
// out as the layout's own character, with what shift makes of it as its shifted
// key and the US key at that place as its alternate key, as kitty gets them on
// X11
static void layoutKeys(WPARAM vk, LPARAM lParam, int mods, uint32_t *key, uint32_t *shifted, uint32_t *alternate) {
    *shifted = 0; *alternate = 0;
    if (*key >= GLFW_FKEY_FIRST || (HIWORD(lParam) & KF_EXTENDED)) return;
    const UINT scan = (lParam >> 16) & 0xff;
    const uint32_t base = layoutChar(vk, scan, false);
    if (!base) return;
    *key = base;
    if (mods & GLFW_MOD_SHIFT) {
        const uint32_t s = layoutChar(vk, scan, true);
        if (s && s != *key) *shifted = s;
    }
    const uint32_t us = usKeyAtScanCode(scan);
    if (us && us != *key) *alternate = us;
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

// The keys really held decide how a key press goes out, since Windows' text
// and AltGr follow them, and remap_modifiers changes only what kitty is told
static int reportedMods(int heldMods) { return _glfwApplyModifierRemap(heldMods); }

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
// What Windows tells a panel's app bar, see placeLayerWindow
#define WM_APP_APPBAR (WM_APP + 3)
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

// The environment holds the values of whichever window kitty restyled last, so
// each window keeps its own from the moment kitty sets them for it, for a later
// re-tint on focus or a repaint of its caption buttons
static void captureTint(_GLFWwindow* window) {
    window->win32.tintRGB = titlebarRGB();
    window->win32.tintOpacity = backgroundOpacity();
}

// ---------------------------------------------------------------------------
// Input methods, which type Chinese, Japanese, Korean and more
// ---------------------------------------------------------------------------

static char* win32_wide_to_utf8(const wchar_t *w, size_t *out_len); // fwd

// The text being composed, or the text the input method just finished, as
// UTF-8, with the cursor's place inside it in code points when asked for
static char*
imeText(HIMC imc, DWORD which, int *cursor) {
    const LONG bytes = ImmGetCompositionStringW(imc, which, NULL, 0);
    if (bytes < 0) return NULL;
    const LONG units = bytes / (LONG) sizeof(WCHAR);
    WCHAR *wide = calloc((size_t) units + 1, sizeof(WCHAR));
    if (!wide) return NULL;
    if (bytes) ImmGetCompositionStringW(imc, which, wide, (DWORD) bytes);
    if (cursor) {
        // Windows counts the cursor in UTF-16 units and kitty in code points
        LONG at = ImmGetCompositionStringW(imc, GCS_CURSORPOS, NULL, 0) & 0xffff;
        if (at > units) at = units;
        int points = 0;
        for (LONG i = 0; i < at; i++) if (!IS_LOW_SURROGATE(wide[i])) points++;
        *cursor = points;
    }
    char *text = win32_wide_to_utf8(wide, NULL);
    free(wide);
    return text;
}

static void
sendImeEvent(_GLFWwindow *window, GLFWIMEState state, const char *text, int cursor) {
    GLFWkeyevent ev = {.action = GLFW_PRESS, .ime_state = state, .text = text ? text : ""};
    if (cursor >= 0) { ev.has_ime_cursor = true; ev.ime_cursor = (uint32_t) cursor; }
    _glfwInputKeyboard(window, &ev);
}

// The cursor comes in the window's client pixels, which both calls take
static void
placeImeWindows(_GLFWwindow *window, const GLFWIMEUpdateEvent *ev) {
    HIMC imc = ImmGetContext(window->win32.handle);
    if (!imc) return;
    const LONG left = ev->cursor.left, top = ev->cursor.top;
    COMPOSITIONFORM composition = {.dwStyle = CFS_POINT, .ptCurrentPos = {left, top}};
    ImmSetCompositionWindow(imc, &composition);
    // The word list opens beside the cursor's cell and never covers it
    CANDIDATEFORM candidates = {.dwIndex = 0, .dwStyle = CFS_EXCLUDE, .ptCurrentPos = {left, top},
        .rcArea = {left, top, left + ev->cursor.width, top + ev->cursor.height}};
    ImmSetCandidateWindow(imc, &candidates);
    ImmReleaseContext(window->win32.handle, imc);
}

// Keys the input method takes reach kitty as VK_PROCESSKEY, which carries no
// key, so kitty is asked where its cursor is now rather than at the last key
static void
placeImeWindowsAtCursor(_GLFWwindow *window) {
    GLFWIMEUpdateEvent ev = {.type = GLFW_IME_UPDATE_CURSOR_POSITION};
    if (_glfw.callbacks.get_ime_cursor_position &&
            _glfw.callbacks.get_ime_cursor_position((GLFWwindow *) window, &ev))
        placeImeWindows(window, &ev);
}

// ---------------------------------------------------------------------------
// The end of the Windows session, for restore_session_after_logout
// ---------------------------------------------------------------------------

// The states the session monitor on Linux reports, from linux_session_monitor.h
#define SESSION_STATE_QUERY_END 2
#define SESSION_STATE_ENDING 3
typedef void (*GLFWDBussessionstatefun)(uint32_t);
static GLFWDBussessionstatefun sessionStateCallback = NULL;

// Named for the D-Bus portal Linux watches, the name kitty loads it by
GLFWAPI bool
glfwDBusMonitorSessionState(GLFWDBussessionstatefun callback) {
    sessionStateCallback = callback;
    // Windows shuts down processes from the highest level to the lowest, so
    // kitty hears of the end before its shells, whose scrollback it saves
    SetProcessShutdownParameters(0x3ff, 0);
    return true;
}

// Every kitty window hears the end of the session, and kitty saves once. It
// saves only once Windows says the session ends, since another program can
// still cancel when Windows asks, and Windows may end kitty the moment this
// returns. An installer closing kitty to replace its files is no end of the
// session, and saves nothing.
static void
sessionEnding(HWND hWnd, LPARAM lParam) {
    static bool saved = false;
    if (saved || !sessionStateCallback || (lParam & ENDSESSION_CLOSEAPP)) return;
    saved = true;
    ShutdownBlockReasonCreate(hWnd, L"Saving the kitty session");
    sessionStateCallback(SESSION_STATE_QUERY_END);
    sessionStateCallback(SESSION_STATE_ENDING);
    ShutdownBlockReasonDestroy(hWnd);
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

static void fitToMonitor(_GLFWwindow* window); // fwd
static HMONITOR layerMonitorFor(const GLFWLayerShellConfig* c);
static void applyLayerStyles(_GLFWwindow* window);
static void placeLayerWindow(_GLFWwindow* window, bool holdStrip);
static void releaseAppBar(_GLFWwindow* window);

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

        // A panel in the panel or background layer stays under other windows,
        // even once clicked, as Wayland's bottom and background layers do
        case WM_WINDOWPOSCHANGING: {
            WINDOWPOS* wp = (WINDOWPOS*) lParam;
            const GLFWLayerShellType t = window->win32.layer.config.type;
            if (window->win32.layer.active && !window->win32.layer.inDesktop && !(wp->flags & SWP_NOZORDER) &&
                    (t == GLFW_LAYER_SHELL_PANEL || t == GLFW_LAYER_SHELL_BACKGROUND))
                wp->hwndInsertAfter = HWND_BOTTOM;
            break;
        }

        case WM_MOUSEACTIVATE:
            if (window->win32.layer.active && window->win32.layer.config.focus_policy == GLFW_FOCUS_NOT_ALLOWED) return MA_NOACTIVATE;
            break;

        case WM_APP_APPBAR:
            if (!window->win32.layer.active) return 0;
            if (wParam == ABN_POSCHANGED && IsWindowVisible(hWnd)) placeLayerWindow(window, true);
            else if (wParam == ABN_FULLSCREENAPP && window->win32.layer.config.type == GLFW_LAYER_SHELL_TOP) {
                // The top layer goes under a full screen program and comes back
                // over the rest once it leaves, as on Wayland
                SetWindowPos(hWnd, lParam ? HWND_BOTTOM : HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            return 0;

        case WM_DISPLAYCHANGE:
            if (window->win32.layer.active) placeLayerWindow(window, IsWindowVisible(hWnd));
            break;

        case WM_DESTROY:
            // The desktop window a background panel sits in goes when Explorer
            // restarts and takes the panel with it, so kitty closes it
            if (window->win32.layer.inDesktop && window->win32.handle == hWnd) {
                window->win32.layer.inDesktop = false;
                _glfwInputWindowCloseRequest(window);
            }
            break;

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
            _glfwInputMouseClick(window, button, action, reportedMods(getKeyMods()));
            return 0;
        }
        // The back and forward buttons, the fourth and fifth as on X11
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: {
            const int button = GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? GLFW_MOUSE_BUTTON_4 : GLFW_MOUSE_BUTTON_5;
            _glfwInputMouseClick(window, button, uMsg == WM_XBUTTONDOWN ? GLFW_PRESS : GLFW_RELEASE, reportedMods(getKeyMods()));
            return TRUE;
        }

        // The delta is in 120ths of a notch, which a precision touchpad sends a
        // few at a time, so it goes to kitty as is and kitty adds the pieces up
        // rather than rounding each one up to a whole line
        case WM_MOUSEWHEEL: {
            GLFWScrollEvent ev = {.offset_type = GLFW_SCROLL_OFFEST_V120};
            ev.y_offset = ev.unscaled.y = (double) GET_WHEEL_DELTA_WPARAM(wParam);
            ev.keyboard_modifiers = reportedMods(getKeyMods());
            _glfwInputScroll(window, &ev);
            return 0;
        }
        case WM_MOUSEHWHEEL: {
            GLFWScrollEvent ev = {.offset_type = GLFW_SCROLL_OFFEST_V120};
            ev.x_offset = ev.unscaled.x = -(double) GET_WHEEL_DELTA_WPARAM(wParam);
            ev.keyboard_modifiers = reportedMods(getKeyMods());
            _glfwInputScroll(window, &ev);
            return 0;
        }

        case WM_KEYDOWN: case WM_SYSKEYDOWN:
        case WM_KEYUP: case WM_SYSKEYUP: {
            const bool up = uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP;
            uint32_t key = refineKey(wParam, lParam, (wParam < 512) ? (uint32_t) _glfw.win32.keycodes[wParam] : 0);
            int mods = getKeyMods();
            uint32_t shiftedKey, alternateKey;
            layoutKeys(wParam, lParam, mods, &key, &shiftedKey, &alternateKey);
            const bool functional = key >= GLFW_FKEY_FIRST;
            const int shortcutMods = mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER);
            if (!up) window->win32.pendingCharKey = window->win32.pendingShiftedKey = window->win32.pendingAlternateKey = 0;
            if (isKeypadTextKey(key) && (!shortcutMods || (shortcutMods == GLFW_MOD_ALT && key <= GLFW_FKEY_KP_9)) &&
                    (up || shortcutMods || charFollows(hWnd))) {
                // A keypad key that types goes out with its text from WM_CHAR,
                // which follows, as one key event the way X11 sends it. Alt and
                // keypad digits are left to Windows, which types the character
                // their code names once Alt comes up.
                if (!up && !shortcutMods) window->win32.pendingCharKey = key;
                if (uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP) break;
                return 0;
            }
            if (!up && key && (!functional || isKeypadTextKey(key)) &&
                    (shortcutMods == GLFW_MOD_ALT || shortcutMods == (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) && charFollows(hWnd)) {
                // Alt, or ctrl and alt, with a key that types: the WM_SYSCHAR or
                // WM_CHAR waiting in the queue carries the character with shift
                // and the layout applied, and the two go out as one event from
                // there. Sent from both, the shell got an escape code and then the
                // character too. When no character follows, the key goes out
                // below as the shortcut it is.
                window->win32.pendingCharKey = key;
                window->win32.pendingShiftedKey = shiftedKey;
                window->win32.pendingAlternateKey = alternateKey;
                if (uMsg == WM_SYSKEYDOWN) break;  // a system key, which DefWindowProc still sees
                return 0;
            }
            // A plain printable key (no ctrl/alt) is delivered as text by WM_CHAR,
            // to keep dead keys and layouts working. Forward everything else here:
            // functional keys (arrows, enter, tab...) and any key combined with a
            // shortcut modifier, so kitty can match bindings and encode ctrl+<key>.
            if (key && (functional || shortcutMods)) {
                GLFWkeyevent ev = {0};
                ev.key = key;
                ev.shifted_key = shiftedKey;
                ev.alternate_key = alternateKey;
                ev.native_key = (int) wParam;
                ev.action = up ? GLFW_RELEASE : (HIWORD(lParam) & KF_REPEAT ? GLFW_REPEAT : GLFW_PRESS);
                ev.mods = reportedMods(mods);
                _glfwInputKeyboard(window, &ev);
            }
            if (uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP) break; // allow system keys
            return 0;
        }

        case WM_SYSDEADCHAR:
            // Alt with a dead key: no character follows, so the key goes out alone
            if (window->win32.pendingCharKey) {
                const int mods = getKeyMods();
                GLFWkeyevent ev = {.key = window->win32.pendingCharKey, .action = GLFW_PRESS, .mods = reportedMods(mods),
                    .shifted_key = window->win32.pendingShiftedKey, .alternate_key = window->win32.pendingAlternateKey};
                window->win32.pendingCharKey = 0;
                // The dead key's own character, as the one shift made
                if (!ev.shifted_key && (mods & GLFW_MOD_SHIFT) && (uint32_t) wParam != ev.key) ev.shifted_key = (uint32_t) wParam;
                _glfwInputKeyboard(window, &ev);
            }
            return 0;
        case WM_DEADCHAR:
            // AltGr with a dead key composes with the next key, like any dead key
            window->win32.pendingCharKey = 0;
            break;

        // kitty draws the text being composed at its cursor, as on Linux and
        // macOS, so the input method's own composition window stays hidden
        case WM_IME_SETCONTEXT:
            lParam &= ~ISC_SHOWUICOMPOSITIONWINDOW;
            break;
        case WM_IME_STARTCOMPOSITION:
            placeImeWindowsAtCursor(window);
            return 0;
        case WM_IME_COMPOSITION: {
            HIMC imc = ImmGetContext(hWnd);
            if (!imc) break;
            // Korean finishes one syllable and starts the next in one message,
            // so the finished text goes first
            if (lParam & GCS_RESULTSTR) {
                char *text = imeText(imc, GCS_RESULTSTR, NULL);
                sendImeEvent(window, GLFW_IME_COMMIT_TEXT, text, -1);
                free(text);
            }
            if (lParam & GCS_COMPSTR) {
                int cursor = -1;
                char *text = imeText(imc, GCS_COMPSTR, (lParam & GCS_CURSORPOS) ? &cursor : NULL);
                sendImeEvent(window, GLFW_IME_PREEDIT_CHANGED, text, cursor);
                free(text);
            } else if (!(lParam & GCS_RESULTSTR)) sendImeEvent(window, GLFW_IME_PREEDIT_CHANGED, "", -1);
            ImmReleaseContext(hWnd, imc);
            // Answered here, Windows sends no WM_CHAR for the finished text,
            // which kitty has already
            return 0;
        }
        case WM_IME_ENDCOMPOSITION:
            sendImeEvent(window, GLFW_IME_PREEDIT_CHANGED, "", -1);
            return 0;

        case WM_CHAR: case WM_SYSCHAR: {
            const uint32_t pairedKey = window->win32.pendingCharKey;
            const uint32_t pairedShiftedKey = window->win32.pendingShiftedKey, pairedAlternateKey = window->win32.pendingAlternateKey;
            window->win32.pendingCharKey = 0;
            int mods = getKeyMods();
            // Control chars (< 0x20) and pure-ctrl combos are encoded from the key
            // event above; only genuine text reaches the child here. A key waiting
            // for its character still goes out, as the key alone.
            if ((WCHAR) wParam < 0x20) {
                if (pairedKey) {
                    GLFWkeyevent ev = {.key = pairedKey, .action = GLFW_PRESS, .mods = reportedMods(mods),
                        .shifted_key = pairedShiftedKey, .alternate_key = pairedAlternateKey};
                    _glfwInputKeyboard(window, &ev);
                }
                return 0;
            }
            if ((mods & GLFW_MOD_CONTROL) && !(mods & GLFW_MOD_ALT)) return 0;
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
                    if (pairedKey) ev.alternate_key = pairedAlternateKey;
                    ev.action = GLFW_PRESS;
                    if (uMsg == WM_SYSCHAR) {
                        // Alt with a key goes out as the key, which kitty encodes
                        // with the Alt, since an event with text goes out as the
                        // bare character. The character shift made lets it send
                        // ESC S for Alt+Shift+S, as on X11, taken from the layout
                        // with Caps Lock left out, since the character Windows
                        // typed has Caps Lock undo the shift.
                        if (pairedShiftedKey) ev.shifted_key = pairedShiftedKey;
                        else if ((mods & GLFW_MOD_SHIFT) && cp != ev.key) ev.shifted_key = cp;
                    } else {
                        ev.text = text;
                        // AltGr's character carries neither the ctrl nor the alt
                        // Windows reports for AltGr. Windows also types with the
                        // left Ctrl and Alt, and with the plain ones that scripts
                        // press for AltGr, and those keep both, so a kitty
                        // shortcut on them fires and kitty types the text when
                        // none is bound, as Windows Terminal does.
                        if ((mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_ALT) && (GetKeyState(VK_RMENU) & 0x8000))
                            mods &= ~(GLFW_MOD_CONTROL | GLFW_MOD_ALT);
                    }
                    ev.mods = reportedMods(mods);
                    _glfwInputKeyboard(window, &ev);
                }
            }
            return 0;
        }

        case WM_DPICHANGED: {
            const float xscale = HIWORD(wParam) / 96.0f;
            const float yscale = LOWORD(wParam) / 96.0f;
            _glfwInputWindowContentScale(window, xscale, yscale);
            // A panel keeps its size in cells, at the new scale
            if (window->win32.layer.active) {
                placeLayerWindow(window, IsWindowVisible(hWnd));
                return 0;
            }
            // A window kitty moves to its place before showing it keeps the size
            // it was made at, which kitty works out at that place's scale or
            // saved for that place. Scaled on the way from the main screen to
            // one of another scale, a restored session grew at every restore.
            if (!IsWindowVisible(hWnd)) return 0;
            RECT* r = (RECT*) lParam;
            SetWindowPos(hWnd, HWND_TOP, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            return 0;
        }

        // The helper window is message only, which broadcasts never reach, so
        // the theme change is heard here, in every kitty window at once
        case WM_SETTINGCHANGE:
            if (lParam && wcscmp((LPCWSTR) lParam, L"ImmersiveColorSet") == 0)
                _glfwWin32SystemColorThemeMayHaveChanged();
            // A panel placed in the work area follows the taskbar. One that
            // holds a strip hears of it from its app bar instead, and changes
            // the work area itself, which would loop back here.
            if (wParam == SPI_SETWORKAREA && window->win32.layer.active && !window->win32.layer.appbar && IsWindowVisible(hWnd))
                placeLayerWindow(window, false);
            break;

        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            if (wParam) sessionEnding(hWnd, lParam);
            return 0;
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
    if (window->monitor || window->win32.layer.active) style |= WS_POPUP;
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
static bool cbCaptionIsLight(const _GLFWwindow* window) {
    if (!window) return false;
    const unsigned int v = window->win32.tintRGB;
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
        bool light_caption = cbCaptionIsLight(st->owner);
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
    // A panel is square and borderless, as panels are on other platforms
    const bool panel = window->win32.layer.active;
    DWORD round = panel ? 1 /* DWMWCP_DONOTROUND */ : 2 /* DWMWCP_ROUND */;
    _glfw.win32.dwmapi.SetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &round, sizeof(round));
    COLORREF border = panel ? 0xFFFFFFFE /* DWMWA_COLOR_NONE */ : 0x00000000 /* thin black edge, like a normal window */;
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
            _glfwWin32AcrylicSetTint(window, window->win32.tintRGB, window->win32.tintOpacity);
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
    if (lsc) {
        window->win32.layer.active = true;
        window->win32.layer.config = *lsc;
        window->win32.layer.monitor = layerMonitorFor(lsc);
        window->resizable = false;
        window->decorated = false;
    }
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
    if (window->win32.layer.active) {
        applyLayerStyles(window);
        placeLayerWindow(window, false);
    }

    if (ctxconfig->client != GLFW_NO_API) {
        if (!_glfwCreateContextWGL(window, ctxconfig, fbconfig)) return false;
        if (!_glfwRefreshContextAttribs(window, ctxconfig)) return false;
    }
    window->win32.customFrame = window->decorated && !window->monitor;
    captureTint(window);
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
        releaseAppBar(window);
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
void _glfwPlatformShowWindow(_GLFWwindow* window, bool move_to_active_screen) {
    if (!window->win32.layer.active) { ShowWindow(window->win32.handle, SW_SHOWNA); return; }
    const GLFWLayerShellConfig* c = &window->win32.layer.config;
    if (move_to_active_screen) {
        POINT cursor;
        if (GetCursorPos(&cursor)) window->win32.layer.monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    }
    placeLayerWindow(window, true);
    // Only the top and overlay layers take the keyboard when they appear, as
    // with an exclusive focus policy on Wayland
    const bool takesFocus = c->focus_policy == GLFW_FOCUS_EXCLUSIVE && (c->type == GLFW_LAYER_SHELL_TOP || c->type == GLFW_LAYER_SHELL_OVERLAY);
    ShowWindow(window->win32.handle, takesFocus ? SW_SHOW : SW_SHOWNA);
    if (takesFocus) {
        SetForegroundWindow(window->win32.handle);
        SetFocus(window->win32.handle);
    }
}
void _glfwPlatformHideWindow(_GLFWwindow* window) {
    ShowWindow(window->win32.handle, SW_HIDE);
    releaseAppBar(window);
}
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
// Windows keeps an input method per window and follows focus by itself
void _glfwPlatformUpdateIMEState(_GLFWwindow* w, const GLFWIMEUpdateEvent* ev) {
    if (ev->type == GLFW_IME_UPDATE_CURSOR_POSITION) placeImeWindows(w, ev);
}

int _glfwPlatformSetWindowBlur(_GLFWwindow* window, int value) {
    window->win32.blur = value;
    captureTint(window);  // kitty set them for this window just before the call
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

// ---------------------------------------------------------------------------
// Panels, kitty's layer shell windows. A panel on a screen edge holds its strip
// of the screen as an app bar, the one supported way to keep maximised windows
// off part of a screen. The top and overlay layers are topmost, the panel layer
// stays at the bottom as Wayland's bottom layer does, and the background layer
// sits behind the desktop icons where Windows lets it.
// ---------------------------------------------------------------------------

static HMONITOR layerMonitorFor(const GLFWLayerShellConfig* c) {
    if (c->output_name[0]) {
        for (int i = 0; i < _glfw.monitorCount; i++) {
            if (strcmp(_glfw.monitors[i]->name, c->output_name) == 0) return _glfw.monitors[i]->win32.handle;
        }
    }
    return MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY);
}

static bool layerOnEdge(GLFWEdge edge) {
    return edge == GLFW_EDGE_TOP || edge == GLFW_EDGE_BOTTOM || edge == GLFW_EDGE_LEFT || edge == GLFW_EDGE_RIGHT;
}

typedef struct LayerPlace {
    RECT window, strip;  // in screen pixels, strip empty when the panel holds none
    UINT stripEdge;      // ABE_TOP and friends
} LayerPlace;

// Where a panel goes on a screen and the strip it holds, as kitty places panels
// on X11 and Wayland. Margins, a size in pixels and an exclusive zone are in
// logical pixels, as on Wayland and macOS.
static LayerPlace layerPlace(_GLFWwindow* window, const MONITORINFO* mi, float scale) {
    const GLFWLayerShellConfig* c = &window->win32.layer.config;
    const int ml = (int) lround(c->requested_left_margin * scale), mr = (int) lround(c->requested_right_margin * scale);
    const int mt = (int) lround(c->requested_top_margin * scale), mb = (int) lround(c->requested_bottom_margin * scale);
    LayerPlace p = {0};
    if (c->type == GLFW_LAYER_SHELL_BACKGROUND) {
        const RECT m = mi->rcMonitor;
        p.window = (RECT){m.left + ml, m.top + mt, m.right - mr, m.bottom - mb};
        return p;
    }
    unsigned cw = 0, ch = 0;
    double ls = 0, ts = 0, rs = 0, bs = 0;
    c->size_callback((GLFWwindow*) window, scale, scale, &cw, &ch, &ls, &ts, &rs, &bs);
    const int w = (int) (1. + ls + rs + (c->x_size_in_pixels ? c->x_size_in_pixels * scale : (double) cw * c->x_size_in_cells));
    const int h = (int) (1. + ts + bs + (c->y_size_in_pixels ? c->y_size_in_pixels * scale : (double) ch * c->y_size_in_cells));
    const bool horizontal = c->edge == GLFW_EDGE_TOP || c->edge == GLFW_EDGE_BOTTOM;
    int zone = 0;
    if (layerOnEdge(c->edge)) zone = c->override_exclusive_zone ? (int) lround(MAX(0, c->requested_exclusive_zone) * scale) : (horizontal ? h : w);
    // The work area leaves out the taskbar and other app bars. A panel that
    // holds a strip of its own is placed against them by Windows instead.
    RECT b = zone > 0 ? mi->rcMonitor : mi->rcWork;
    if (zone > 0) {
        APPBARDATA abd = {sizeof(abd)};
        abd.hWnd = window->win32.handle;
        p.strip = b;
        switch (c->edge) {
            case GLFW_EDGE_TOP: p.stripEdge = ABE_TOP; p.strip.bottom = p.strip.top + mt + zone; break;
            case GLFW_EDGE_BOTTOM: p.stripEdge = ABE_BOTTOM; p.strip.top = p.strip.bottom - mb - zone; break;
            case GLFW_EDGE_LEFT: p.stripEdge = ABE_LEFT; p.strip.right = p.strip.left + ml + zone; break;
            default: p.stripEdge = ABE_RIGHT; p.strip.left = p.strip.right - mr - zone; break;
        }
        if (window->win32.layer.appbar) {
            // Windows moves the strip clear of the taskbar and the other app
            // bars on that edge, and the strip keeps its depth
            abd.uEdge = p.stripEdge;
            abd.rc = p.strip;
            SHAppBarMessage(ABM_QUERYPOS, &abd);
            p.strip = abd.rc;
            switch (c->edge) {
                case GLFW_EDGE_TOP: p.strip.bottom = p.strip.top + mt + zone; break;
                case GLFW_EDGE_BOTTOM: p.strip.top = p.strip.bottom - mb - zone; break;
                case GLFW_EDGE_LEFT: p.strip.right = p.strip.left + ml + zone; break;
                default: p.strip.left = p.strip.right - mr - zone; break;
            }
        }
        b = p.strip;
    }
    switch (c->edge) {
        case GLFW_EDGE_TOP: p.window = (RECT){b.left + ml, b.top + mt, b.right - mr, b.top + mt + h}; break;
        case GLFW_EDGE_BOTTOM: p.window = (RECT){b.left + ml, b.bottom - mb - h, b.right - mr, b.bottom - mb}; break;
        case GLFW_EDGE_LEFT: p.window = (RECT){b.left + ml, b.top + mt, b.left + ml + w, b.bottom - mb}; break;
        case GLFW_EDGE_RIGHT: p.window = (RECT){b.right - mr - w, b.top + mt, b.right - mr, b.bottom - mb}; break;
        case GLFW_EDGE_NONE: p.window = (RECT){b.left + ml, b.top + mt, b.left + ml + w, b.top + mt + h}; break;
        case GLFW_EDGE_CENTER_SIZED: {
            const int x = b.left + (b.right - b.left - w) / 2, y = b.top + (b.bottom - b.top - h) / 2;
            p.window = (RECT){x, y, x + w, y + h};
            break;
        }
        default: p.window = (RECT){b.left + ml, b.top + mt, b.right - mr, b.bottom - mb}; break;
    }
    return p;
}

static void releaseAppBar(_GLFWwindow* window) {
    if (!window->win32.layer.appbar) return;
    APPBARDATA abd = {sizeof(abd)};
    abd.hWnd = window->win32.handle;
    SHAppBarMessage(ABM_REMOVE, &abd);
    window->win32.layer.appbar = false;
}

static BOOL CALLBACK findDesktopWorkerW(HWND top, LPARAM out) {
    if (FindWindowExW(top, NULL, L"SHELLDLL_DefView", NULL)) {
        *(HWND*) out = FindWindowExW(NULL, top, L"WorkerW", NULL);
        return FALSE;
    }
    return TRUE;
}

// The window between the desktop icons and the wallpaper, which live wallpaper
// programs draw into. Windows has no API for it, Progman makes it on an
// undocumented message, and its place moved in Windows 11 24H2, so a panel
// falls back to the bottom of the stack when it is not found.
static HWND desktopWorkerW(void) {
    HWND progman = FindWindowW(L"Progman", NULL);
    if (!progman) return NULL;
    DWORD_PTR result;
    SendMessageTimeoutW(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, &result);
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &result);
    HWND worker = FindWindowExW(progman, NULL, L"WorkerW", NULL);
    if (!worker) EnumWindows(findDesktopWorkerW, (LPARAM) &worker);
    return worker;
}

// The extended styles a panel's config asks for
static void applyLayerStyles(_GLFWwindow* window) {
    const GLFWLayerShellConfig* c = &window->win32.layer.config;
    HWND hwnd = window->win32.handle;
    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    ex = (ex & ~(WS_EX_APPWINDOW | WS_EX_NOACTIVATE)) | WS_EX_TOOLWINDOW;
    if (c->focus_policy == GLFW_FOCUS_NOT_ALLOWED) ex |= WS_EX_NOACTIVATE;
    SetWindowLongW(hwnd, GWL_EXSTYLE, ex);
    if (c->type == GLFW_LAYER_SHELL_BACKGROUND && !window->win32.layer.inDesktop) {
        HWND worker = desktopWorkerW();
        if (worker && SetParent(hwnd, worker)) window->win32.layer.inDesktop = true;
    } else if (c->type != GLFW_LAYER_SHELL_BACKGROUND && window->win32.layer.inDesktop) {
        SetParent(hwnd, NULL);
        window->win32.layer.inDesktop = false;
    }
}

// Puts a panel in its place, holding its strip of the screen while it shows
static void placeLayerWindow(_GLFWwindow* window, bool holdStrip) {
    HWND hwnd = window->win32.handle;
    const GLFWLayerShellConfig* c = &window->win32.layer.config;
    MONITORINFO mi = {sizeof(mi)};
    if (!window->win32.layer.monitor || !GetMonitorInfoW(window->win32.layer.monitor, &mi)) {
        // Its screen was unplugged
        window->win32.layer.monitor = layerMonitorFor(c);
        if (!GetMonitorInfoW(window->win32.layer.monitor, &mi)) return;
    }
    const float scale = _glfwWin32MonitorDpi(window->win32.layer.monitor) / 96.f;
    LayerPlace p = layerPlace(window, &mi, scale);
    if (holdStrip && !IsRectEmpty(&p.strip)) {
        APPBARDATA abd = {sizeof(abd)};
        abd.hWnd = hwnd;
        abd.uCallbackMessage = WM_APP_APPBAR;
        if (!window->win32.layer.appbar && SHAppBarMessage(ABM_NEW, &abd)) {
            window->win32.layer.appbar = true;
            p = layerPlace(window, &mi, scale);
        }
        if (window->win32.layer.appbar) {
            abd.uEdge = p.stripEdge;
            abd.rc = p.strip;
            SHAppBarMessage(ABM_SETPOS, &abd);
        }
    } else releaseAppBar(window);
    RECT r = p.window;
    HWND after = HWND_TOP;
    UINT flags = SWP_NOACTIVATE;
    if (window->win32.layer.inDesktop) {
        MapWindowPoints(HWND_DESKTOP, GetAncestor(hwnd, GA_PARENT), (POINT*) &r, 2);
        flags |= SWP_NOZORDER;
    } else if (c->type == GLFW_LAYER_SHELL_TOP || c->type == GLFW_LAYER_SHELL_OVERLAY) after = HWND_TOPMOST;
    else after = HWND_BOTTOM;
    if (after == HWND_BOTTOM) SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(hwnd, after, r.left, r.top, r.right - r.left, r.bottom - r.top, flags);
}

bool _glfwPlatformSetLayerShellConfig(_GLFWwindow* window, const GLFWLayerShellConfig* value) {
    if (!window->win32.layer.active) return false;
    if (value) {
        const bool monitorChanged = strcmp(value->output_name, window->win32.layer.config.output_name) != 0;
        window->win32.layer.config = *value;
        if (monitorChanged) window->win32.layer.monitor = layerMonitorFor(value);
    }
    applyLayerStyles(window);
    placeLayerWindow(window, IsWindowVisible(window->win32.handle));
    return true;
}

const GLFWLayerShellConfig* _glfwPlatformGetLayerShellConfig(_GLFWwindow* window) {
    return window->win32.layer.active ? &window->win32.layer.config : NULL;
}

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
// pastes into the window (see glfw.c drop_dest_callback and window.py on_drop),
// and under every mime type the drag names a format after, for programs in the
// window that take drops themselves, see collect_drop_offers.
// kitty's own drags, of tabs and windows, are the drag source further below,
// and a drop of one of those onto a kitty window is handed to kitty as its own.

#define DND_MIME_URI_LIST "text/uri-list"
#define DND_MIME_TEXT "text/plain;charset=utf-8"
#define WIN32_DND_MIN(a, b) ((a) < (b) ? (a) : (b))

typedef struct { char *mime; char *data; size_t size, offset; } Win32DropItem;

// A mime type a drag offers and the clipboard format it comes from: a format
// named after the mime type, as kitty and other programs that know mime types
// name theirs, or files and text, which kitty reads as a uri-list and UTF-8
enum { DROP_FROM_NAMED, DROP_FROM_FILES, DROP_FROM_TEXT };
typedef struct { char *mime; CLIPFORMAT cf; int from; } Win32DropOffer;
#define WIN32_DROP_MAX 16

// Only one drag is ever in flight, so a single global holds its state.
static struct {
    Win32DropItem items[WIN32_DROP_MAX];   // data extracted at drop time
    size_t count;
    Win32DropOffer offered[WIN32_DROP_MAX];  // what the active drag offers (enter..leave)
    size_t offered_count;
    _GLFWwindow *window;
    POINTL last_pt;            // where the last move kitty heard of was
    DWORD last_keys, last_allowed;
} win32_drop;

// A drag kitty started, of a tab or a window. Its mime types and data are
// copied here at the start, since glfw frees its own copy whenever another
// drag is asked for, and the drag outlives that.
#define WIN32_DRAG_MAX_MIMES 8
#define WIN32_DRAG_MAX_FORMATS (WIN32_DRAG_MAX_MIMES + 3)  // and text, files and a link for other programs
static struct {
    bool pending;            // posted, DoDragDrop not running yet
    bool active;             // inside DoDragDrop
    bool escape_pressed, button_released, cancel_requested;
    bool has_files;          // a file list is offered, which other programs must never move
    bool on_demand;          // a program supplies some of the data once a drop target asks
    bool data_ready;         // kitty has more of the data asked for
    GLFWid window_id;
    size_t count;            // kitty's mime types, the first entries
    size_t total;            // those and the formats other programs read, whose mime is NULL
    CLIPFORMAT formats[WIN32_DRAG_MAX_FORMATS];
    char *mimes[WIN32_DRAG_MAX_FORMATS];
    char *data[WIN32_DRAG_MAX_FORMATS];  // NULL for data a program supplies on demand
    size_t sizes[WIN32_DRAG_MAX_FORMATS];
    int derived[WIN32_DRAG_MAX_FORMATS]; // which OTHER_FORMAT a format made from data on demand is
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

// The bytes of a format as they are, for a mime type a program reads itself.
// GlobalSize may round up, so a format zero filled by its writer may carry a
// few NULs at its end.
static char*
win32_get_raw_format(IDataObject *pdo, CLIPFORMAT cf, size_t *out_len) {
    FORMATETC fmt = { cf, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stg;
    if (FAILED(pdo->lpVtbl->GetData(pdo, &fmt, &stg))) return NULL;
    char *result = NULL;
    const size_t sz = stg.tymed == TYMED_HGLOBAL ? GlobalSize(stg.hGlobal) : 0;
    void *p = sz ? GlobalLock(stg.hGlobal) : NULL;
    if (p) {
        if ((result = malloc(sz))) { memcpy(result, p, sz); *out_len = sz; }
        GlobalUnlock(stg.hGlobal);
    }
    ReleaseStgMedium(&stg);
    return result;
}

static void
free_drop_offers(void) {
    for (size_t i = 0; i < win32_drop.offered_count; i++) free(win32_drop.offered[i].mime);
    win32_drop.offered_count = 0;
}

static void
add_drop_offer(const char *mime, CLIPFORMAT cf, int from) {
    if (win32_drop.offered_count >= WIN32_DROP_MAX) return;
    for (size_t i = 0; i < win32_drop.offered_count; i++) if (strcmp(win32_drop.offered[i].mime, mime) == 0) return;
    char *m = _glfw_strdup(mime);
    if (m) win32_drop.offered[win32_drop.offered_count++] = (Win32DropOffer){m, cf, from};
}

// Every mime type the drag offers, for programs that read them, the dnd kitten
// among them: the formats named after mime types, PNG images, and the files and
// text every Windows program offers
static void
collect_drop_offers(IDataObject *pdo) {
    free_drop_offers();
    IEnumFORMATETC *e = NULL;
    if (SUCCEEDED(pdo->lpVtbl->EnumFormatEtc(pdo, DATADIR_GET, &e)) && e) {
        FORMATETC f;
        while (e->lpVtbl->Next(e, 1, &f, NULL) == S_OK) {
            if (f.ptd) CoTaskMemFree(f.ptd);
            if (f.cfFormat < 0xC000 || !(f.tymed & TYMED_HGLOBAL)) continue;  // only registered formats have names
            char name[256];
            if (!GetClipboardFormatNameA(f.cfFormat, name, sizeof(name))) continue;
            if (strcmp(name, "PNG") == 0) add_drop_offer("image/png", f.cfFormat, DROP_FROM_NAMED);
            else if (strchr(name, '/') && !strchr(name, ' ')) add_drop_offer(name, f.cfFormat, DROP_FROM_NAMED);
        }
        e->lpVtbl->Release(e);
    }
    FORMATETC f = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    if (pdo->lpVtbl->QueryGetData(pdo, &f) == S_OK) add_drop_offer(DND_MIME_URI_LIST, CF_HDROP, DROP_FROM_FILES);
    f.cfFormat = CF_UNICODETEXT;
    if (pdo->lpVtbl->QueryGetData(pdo, &f) == S_OK) add_drop_offer(DND_MIME_TEXT, CF_UNICODETEXT, DROP_FROM_TEXT);
}

static size_t
offered_mimes(const char **m) {
    for (size_t i = 0; i < win32_drop.offered_count; i++) m[i] = win32_drop.offered[i].mime;
    return win32_drop.offered_count;
}

// What the dragging program allows, which kitty hands a program in the window
// that takes drops. Without it kitty told the dnd kitten nothing was allowed,
// and the kitten took no drop at all.
static void
set_drop_source_actions(_GLFWwindow *window, DWORD effects) {
    int actions = 0;
    if (effects & DROPEFFECT_COPY) actions |= GLFW_DRAG_OPERATION_COPY;
    if (effects & DROPEFFECT_MOVE) actions |= GLFW_DRAG_OPERATION_MOVE;
    // A drag that only links, as from a browser, can still be read as a copy
    if (!actions && (effects & DROPEFFECT_LINK)) actions = GLFW_DRAG_OPERATION_COPY;
    window->drop_operation.source_actions = actions;
}

// The pointer kitty's choice shows: a move only where the program in the window
// takes the drop as a move alone, since kitty's own paste allows both and
// moves nothing
static DWORD
chosen_drop_effect(_GLFWwindow *window, size_t accepted, DWORD allowed) {
    if (!accepted) return DROPEFFECT_NONE;
    const int ops = window->drop_operation.allowed;
    if (ops == GLFW_DRAG_OPERATION_MOVE && (allowed & DROPEFFECT_MOVE)) return DROPEFFECT_MOVE;
    if (allowed & DROPEFFECT_COPY) return DROPEFFECT_COPY;
    return allowed & (DROPEFFECT_MOVE | DROPEFFECT_LINK) ? (allowed & DROPEFFECT_LINK ? DROPEFFECT_LINK : DROPEFFECT_MOVE) : DROPEFFECT_NONE;
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

// A move where the drag allows it, as for a tab, else a copy, as for a link to
// a file, which is never moved
static DWORD
self_drag_effect(size_t accepted, DWORD allowed) {
    if (!accepted) return DROPEFFECT_NONE;
    return (allowed & DROPEFFECT_MOVE) ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
}

// A drag holds its own reference to the target under the pointer, so the
// target can outlive its window, which leaves the window NULL behind it.
static HRESULT STDMETHODCALLTYPE dt_DragEnter(IDropTarget *This, IDataObject *pdo, DWORD keys, POINTL pt, DWORD *effect) {
    Win32DropTarget *self = (Win32DropTarget*)This;
    if (!self->window) { *effect = DROPEFFECT_NONE; return S_OK; }
    win32_drop.window = self->window;
    if (win32_drag.active) {
        free_drop_offers();
        *effect = self_drag_effect(self_drag_event(self->window, GLFW_DROP_ENTER, pt), *effect);
        return S_OK;
    }
    collect_drop_offers(pdo);
    set_drop_source_actions(self->window, *effect);
    win32_drop.last_pt = pt; win32_drop.last_keys = keys; win32_drop.last_allowed = *effect;
    double x, y; drop_point_to_client(self->window, pt, &x, &y);
    size_t accepted = 0;
    if (win32_drop.offered_count) {
        const char *m[WIN32_DROP_MAX];
        accepted = _glfwInputDropEvent(self->window, GLFW_DROP_ENTER, x, y, m, offered_mimes(m), false);
    }
    *effect = chosen_drop_effect(self->window, accepted, *effect);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dt_DragOver(IDropTarget *This, DWORD keys, POINTL pt, DWORD *effect) {
    Win32DropTarget *self = (Win32DropTarget*)This;
    if (!self->window) { *effect = DROPEFFECT_NONE; return S_OK; }
    if (win32_drag.active) {
        *effect = self_drag_effect(self_drag_event(self->window, GLFW_DROP_MOVE, pt), *effect);
        return S_OK;
    }
    set_drop_source_actions(self->window, *effect);
    double x, y; drop_point_to_client(self->window, pt, &x, &y);
    size_t accepted = 0;
    if (win32_drop.offered_count) {
        const char *m[WIN32_DROP_MAX];
        // Windows asks about 15 times a second even while the mouse is still.
        // A program in the window hears of a move only when there is one, and
        // otherwise kitty answers from what the program said last, which
        // took it the next ask to show, as X11 shows a status update at once.
        const bool moved = pt.x != win32_drop.last_pt.x || pt.y != win32_drop.last_pt.y || keys != win32_drop.last_keys || *effect != win32_drop.last_allowed;
        win32_drop.last_pt = pt; win32_drop.last_keys = keys; win32_drop.last_allowed = *effect;
        accepted = _glfwInputDropEvent(self->window, moved ? GLFW_DROP_MOVE : GLFW_DROP_STATUS_UPDATE, x, y, m, offered_mimes(m), false);
    }
    *effect = chosen_drop_effect(self->window, accepted, *effect);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dt_DragLeave(IDropTarget *This) {
    Win32DropTarget *self = (Win32DropTarget*)This;
    if (self->window) _glfwInputDropEvent(self->window, GLFW_DROP_LEAVE, 0, 0, NULL, 0, win32_drag.active);
    free_drop_offers();
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dt_Drop(IDropTarget *This, IDataObject *pdo, DWORD keys, POINTL pt, DWORD *effect) {
    (void)keys;
    Win32DropTarget *self = (Win32DropTarget*)This;
    if (!self->window) { *effect = DROPEFFECT_NONE; return S_OK; }
    free_win32_drop_items();
    win32_drop.window = self->window;
    if (win32_drag.active) {
        free_drop_offers();
        *effect = self_drag_effect(self_drag_event(self->window, GLFW_DROP_DROP, pt), *effect);
        return S_OK;
    }
    collect_drop_offers(pdo);
    for (size_t i = 0; i < win32_drop.offered_count; i++) {
        const Win32DropOffer *o = win32_drop.offered + i;
        size_t len = 0;
        char *data = o->from == DROP_FROM_NAMED ? win32_get_raw_format(pdo, o->cf, &len) : win32_get_format(pdo, o->cf, &len);
        add_win32_drop_item(o->mime, data, len);
    }
    free_drop_offers();
    set_drop_source_actions(self->window, *effect);
    if (win32_drop.count) {
        const char *m[WIN32_DROP_MAX]; for (size_t i = 0; i < win32_drop.count; i++) m[i] = win32_drop.items[i].mime;
        double x, y; drop_point_to_client(self->window, pt, &x, &y);
        size_t accepted = _glfwInputDropEvent(self->window, GLFW_DROP_DROP, x, y, m, win32_drop.count, false);
        for (size_t i = 0; i < accepted; i++) _glfwPlatformRequestDropData(self->window, m[i]);
        // Always a copy, so the dragging program removes nothing: the dnd
        // kitten removes the sources of a move itself, once it has them, and
        // kitty's own paste never moves anything
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
        if (win32_drop.window == window) { free_win32_drop_items(); win32_drop.window = NULL; free_drop_offers(); }
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
// Drag and drop -- drag source (kitty's own drags, and those of programs in it)
// ---------------------------------------------------------------------------
// Each mime type travels as a clipboard format registered under its own name,
// which only kitty reads, so text, links and files go out under the formats
// other programs read as well. DoDragDrop runs a modal loop, so the drag starts
// from a posted message rather than inside the call that asked for it, which
// comes from kitty's Python, and the repaint timer the live resize uses keeps
// kitty drawing during the loop. A program in a window, such as the dnd kitten,
// supplies its data once a drop target asks for it.

static void
free_win32_drag(void) {
    for (size_t i = 0; i < win32_drag.total; i++) { free(win32_drag.mimes[i]); free(win32_drag.data[i]); win32_drag.derived[i] = 0; }
    win32_drag.count = win32_drag.total = 0;
    win32_drag.has_files = win32_drag.on_demand = false;
}

typedef struct { wchar_t *p; size_t len, cap; bool failed; } WideBuf;

static void
wide_append(WideBuf *b, const wchar_t *s, size_t n) {
    if (b->failed) return;
    if (b->len + n + 1 > b->cap) {
        const size_t cap = b->cap * 2 > b->len + n + 256 ? b->cap * 2 : b->len + n + 256;
        wchar_t *p = realloc(b->p, cap * sizeof(wchar_t));
        if (!p) { b->failed = true; return; }
        b->p = p; b->cap = cap;
    }
    memcpy(b->p + b->len, s, n * sizeof(wchar_t));
    b->len += n;
    b->p[b->len] = 0;
}

static void
wide_append_utf8(WideBuf *b, const char *s, size_t n) {
    if (!n || b->failed) return;
    const int wn = MultiByteToWideChar(CP_UTF8, 0, s, (int)n, NULL, 0);
    wchar_t *w = wn > 0 ? malloc(wn * sizeof(wchar_t)) : NULL;
    if (w && MultiByteToWideChar(CP_UTF8, 0, s, (int)n, w, wn) == wn) wide_append(b, w, wn);
    else b->failed = true;
    free(w);
}

static int
hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// The Windows path a file:///C:/path URL names, in UTF-8, or NULL. kitty's
// Python writes every link to a file on this machine in that form.
static char*
path_from_file_url(const char *url, size_t n) {
    if (n < 10 || _strnicmp(url, "file:///", 8) != 0) return NULL;
    url += 8; n -= 8;
    char *path = malloc(n + 1);
    if (!path) return NULL;
    size_t len = 0;
    for (size_t i = 0; i < n; i++) {
        int hi, lo;
        if (url[i] == '%' && i + 2 < n && (hi = hex_digit(url[i+1])) >= 0 && (lo = hex_digit(url[i+2])) >= 0) {
            path[len++] = (char)(hi * 16 + lo); i += 2;
        } else path[len++] = url[i] == '/' ? '\\' : url[i];
    }
    path[len] = 0;
    const bool is_drive_path = len >= 2 && isalpha((unsigned char)path[0]) && path[1] == ':' && (len == 2 || path[2] == '\\');
    if (!is_drive_path || memchr(path, 0, len)) { free(path); return NULL; }
    return path;
}

// A format for other programs, its data NULL when it is made on demand
static void
add_drag_format(CLIPFORMAT cf, void *data, size_t sz) {
    if (!cf || (!data && !win32_drag.on_demand) || win32_drag.total >= WIN32_DRAG_MAX_FORMATS) { free(data); return; }
    const size_t n = win32_drag.total++;
    win32_drag.formats[n] = cf;
    win32_drag.mimes[n] = NULL;
    win32_drag.data[n] = data;
    win32_drag.sizes[n] = sz;
}

static void*
wide_text_data(const WideBuf *b, size_t *sz) {
    *sz = (b->len + 1) * sizeof(wchar_t);
    void *ans = malloc(*sz);
    if (ans) memcpy(ans, b->p, *sz);
    return ans;
}

enum { OTHER_FORMAT_NONE, OTHER_FORMAT_TEXT, OTHER_FORMAT_LINK, OTHER_FORMAT_FILES, OTHER_FORMAT_COUNT };
typedef struct { void *data[OTHER_FORMAT_COUNT]; size_t sizes[OTHER_FORMAT_COUNT]; } OtherFormats;

static CLIPFORMAT
other_format_clipformat(int which) {
    switch (which) {
        case OTHER_FORMAT_TEXT: return CF_UNICODETEXT;
        case OTHER_FORMAT_LINK: return (CLIPFORMAT)RegisterClipboardFormatW(L"UniformResourceLocatorW");
        case OTHER_FORMAT_FILES: return CF_HDROP;
    }
    return 0;
}

// The indices of the text and the uri-list among kitty's mime types, or -1
static void
text_and_uri_list(char *const *mimes, size_t count, int *text, int *uris) {
    *text = *uris = -1;
    for (size_t i = 0; i < count; i++) {
        const char *m = mimes[i];
        if (strcmp(m, "text/plain;charset=utf-8") == 0 || (*text < 0 && strcmp(m, "text/plain") == 0)) *text = (int)i;
        else if (strcmp(m, "text/uri-list") == 0) *uris = (int)i;
    }
}

// Text as CF_UNICODETEXT, the links of a text/uri-list as a link and as text,
// and the files among them as CF_HDROP, which Explorer copies and editors open
static OtherFormats
other_formats_from(const char *text, size_t text_sz, const char *uris, size_t uris_sz) {
    OtherFormats ans = {0};
    WideBuf as_text = {0}, files = {0}, link = {0};
    size_t num_files = 0;
    if (text) wide_append_utf8(&as_text, text, text_sz);
    for (const char *p = uris, *end = uris + uris_sz; p && p < end; ) {
        const char *eol = memchr(p, '\n', end - p), *line_end = eol ? eol : end;
        size_t n = line_end - p;
        while (n && (p[n-1] == '\r' || p[n-1] == ' ')) n--;
        if (n && p[0] != '#') {
            char *path = path_from_file_url(p, n);
            if (!text && as_text.len) wide_append(&as_text, L"\r\n", 2);
            if (path) {
                wide_append_utf8(&files, path, strlen(path));
                wide_append(&files, L"", 1);  // each path ends in a NUL
                if (!text) wide_append_utf8(&as_text, path, strlen(path));
                num_files++;
                free(path);
            } else {
                if (!text) wide_append_utf8(&as_text, p, n);
                if (!link.len) wide_append_utf8(&link, p, n);
            }
        }
        p = eol ? eol + 1 : end;
    }
    if (as_text.len && !as_text.failed) ans.data[OTHER_FORMAT_TEXT] = wide_text_data(&as_text, &ans.sizes[OTHER_FORMAT_TEXT]);
    if (link.len && !link.failed) ans.data[OTHER_FORMAT_LINK] = wide_text_data(&link, &ans.sizes[OTHER_FORMAT_LINK]);
    if (num_files && !files.failed) {
        // DROPFILES, then the paths, each ending in a NUL, and a NUL after the last
        const size_t sz = sizeof(DROPFILES) + (files.len + 1) * sizeof(wchar_t);
        DROPFILES *df = calloc(1, sz);
        if (df) {
            df->pFiles = sizeof(DROPFILES);
            df->fWide = TRUE;
            memcpy((char*)df + sizeof(DROPFILES), files.p, files.len * sizeof(wchar_t));
            ans.data[OTHER_FORMAT_FILES] = df;
            ans.sizes[OTHER_FORMAT_FILES] = sz;
        }
    }
    free(as_text.p); free(files.p); free(link.p);
    return ans;
}

static void
add_formats_for_other_programs(void) {
    int text, uris;
    text_and_uri_list(win32_drag.mimes, win32_drag.count, &text, &uris);
    if ((text >= 0 && !win32_drag.data[text]) || (uris >= 0 && !win32_drag.data[uris])) {
        // A program supplies the text or the links once a drop target asks, so
        // these are made then, and a uri-list may well hold files
        if (text >= 0 || uris >= 0) add_drag_format(CF_UNICODETEXT, NULL, 0);
        if (uris >= 0) {
            add_drag_format(other_format_clipformat(OTHER_FORMAT_LINK), NULL, 0);
            add_drag_format(CF_HDROP, NULL, 0);
            win32_drag.has_files = true;
        }
        for (size_t i = win32_drag.count; i < win32_drag.total; i++)
            win32_drag.derived[i] = win32_drag.formats[i] == CF_UNICODETEXT ? OTHER_FORMAT_TEXT : win32_drag.formats[i] == CF_HDROP ? OTHER_FORMAT_FILES : OTHER_FORMAT_LINK;
        return;
    }
    OtherFormats o = other_formats_from(
        text >= 0 ? win32_drag.data[text] : NULL, text >= 0 ? win32_drag.sizes[text] : 0,
        uris >= 0 ? win32_drag.data[uris] : NULL, uris >= 0 ? win32_drag.sizes[uris] : 0);
    for (int which = OTHER_FORMAT_TEXT; which < OTHER_FORMAT_COUNT; which++) {
        if (!o.data[which]) continue;
        const size_t before = win32_drag.total;
        add_drag_format(other_format_clipformat(which), o.data[which], o.sizes[which]);
        if (which == OTHER_FORMAT_FILES) win32_drag.has_files = win32_drag.total > before;
    }
}

// --- IDataObject COM object, offering the drag's formats ---
// The shell's drag helpers keep their own state in the data object through
// SetData, the drag picture and the drop target's hints among it, so what
// they set is held and handed back beside the drag's own formats.
//
// A drop target such as Explorer copies files inside its Drop call unless the
// data object lets it work in the background, and DoDragDrop, which holds
// kitty's main thread, waits for that call. A copy that asked whether to
// replace a file left kitty waiting on the question with the drag picture
// stuck to the pointer. So the object offers IDataObjectAsyncCapability, and
// holds its own copy of the data, which Explorer reads after the drag ends.
#define WIN32_DRAG_MAX_STORED 16
static const IID kitty_IID_IDataObjectAsyncCapability = {0x3d8b0590, 0xf691, 0x11d2, {0x8e, 0xa9, 0x00, 0x60, 0x97, 0xdf, 0x5b, 0xd4}};
typedef struct {
    IDataObject iface;
    IDataObjectAsyncCapability async_iface;
    LONG ref;
    BOOL async_mode, in_async_op;
    size_t mime_count, count;  // kitty's mime types, then the formats other programs read
    CLIPFORMAT formats[WIN32_DRAG_MAX_FORMATS];
    char *mimes[WIN32_DRAG_MAX_FORMATS];
    char *data[WIN32_DRAG_MAX_FORMATS];
    size_t sizes[WIN32_DRAG_MAX_FORMATS];
    int derived[WIN32_DRAG_MAX_FORMATS];
    bool fetched[WIN32_DRAG_MAX_FORMATS];  // asked for once, whatever came of it
    size_t stored_count;
    FORMATETC stored_formats[WIN32_DRAG_MAX_STORED];
    STGMEDIUM stored_media[WIN32_DRAG_MAX_STORED];
} Win32DragData;

static bool
take_drag_formats(Win32DragData *self) {
    for (size_t i = 0; i < win32_drag.total; i++) {
        const size_t sz = win32_drag.sizes[i];
        self->formats[i] = win32_drag.formats[i];
        self->derived[i] = win32_drag.derived[i];
        self->count = i + 1;
        if (win32_drag.mimes[i] && !(self->mimes[i] = _glfw_strdup(win32_drag.mimes[i]))) return false;
        if (!win32_drag.data[i]) continue;  // supplied on demand
        if (!(self->data[i] = malloc(sz ? sz : 1))) return false;
        if (sz) memcpy(self->data[i], win32_drag.data[i], sz);
        self->sizes[i] = sz;
    }
    self->mime_count = win32_drag.count;
    return true;
}

// Data a program in a kitty window supplies, asked for when a drop target
// reads it. kitty streams it in pieces through its loop, which the drag's
// repaint timer runs, so this waits on messages until each piece is in, as the
// X11 backend waits to answer a selection request.
static bool
fetch_drag_data(Win32DragData *self, size_t idx) {
    if (self->fetched[idx]) return self->data[idx] != NULL;
    self->fetched[idx] = true;
    _GLFWwindow *window = _glfwWindowForId(win32_drag.window_id);
    if (!window || !self->mimes[idx]) return false;
    const char *mime = self->mimes[idx];
    char *buf = NULL;
    size_t len = 0;
    int err = 0;
    // A remote program's files are copied to this machine first, so the wait is
    // for a lack of progress, not for the whole of it
    const monotonic_t patience = s_to_monotonic_t(60);
    monotonic_t deadline = monotonic() + patience;
    while (true) {
        win32_drag.data_ready = false;
        GLFWDragEvent ev = {.type = GLFW_DRAG_DATA_REQUEST, .mime_type = mime};
        _glfwInputDragSourceRequest(window, &ev);
        err = ev.err_num;
        if (ev.data && ev.data_sz) {
            char *p = realloc(buf, len + ev.data_sz);
            if (p) { memcpy(p + len, ev.data, ev.data_sz); buf = p; len += ev.data_sz; }
            // Handing the piece back lets kitty free it
            GLFWDragEvent done = {.type = GLFW_DRAG_DATA_REQUEST, .mime_type = mime, .data = ev.data, .data_sz = ev.data_sz};
            _glfwInputDragSourceRequest(window, &done);
            if (!p) { err = ENOMEM; break; }
            deadline = monotonic() + patience;
            continue;
        }
        if (err != EAGAIN) break;
        while (!win32_drag.data_ready && monotonic() < deadline) {
            MsgWaitForMultipleObjects(0, NULL, FALSE, 50, QS_ALLINPUT);
            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        }
        if (!win32_drag.data_ready) { err = ETIMEDOUT; break; }
        deadline = monotonic() + patience;
    }
    if (err) { free(buf); return false; }
    self->data[idx] = buf ? buf : malloc(1);
    self->sizes[idx] = len;
    return self->data[idx] != NULL;
}

// A format other programs read, made from data a program supplies on demand
static bool
make_other_format(Win32DragData *self, size_t idx) {
    int text, uris;
    text_and_uri_list(self->mimes, self->mime_count, &text, &uris);
    if (text >= 0) fetch_drag_data(self, (size_t)text);
    if (uris >= 0) fetch_drag_data(self, (size_t)uris);
    OtherFormats o = other_formats_from(
        text >= 0 ? self->data[text] : NULL, text >= 0 ? self->sizes[text] : 0,
        uris >= 0 ? self->data[uris] : NULL, uris >= 0 ? self->sizes[uris] : 0);
    for (size_t i = self->mime_count; i < self->count; i++) {
        const int which = self->derived[i];
        if (!which || self->data[i] || !o.data[which]) continue;
        self->data[i] = o.data[which];
        self->sizes[i] = o.sizes[which];
        o.data[which] = NULL;
    }
    for (int which = 0; which < OTHER_FORMAT_COUNT; which++) free(o.data[which]);
    self->fetched[idx] = true;
    return self->data[idx] != NULL;
}

static int
drag_format_index(const Win32DragData *self, const FORMATETC *f) {
    if (!f || !(f->tymed & TYMED_HGLOBAL) || f->dwAspect != DVASPECT_CONTENT) return -1;
    for (size_t i = 0; i < self->count; i++) if (self->formats[i] == f->cfFormat) return (int)i;
    return -1;
}

// The data for one format in an HGLOBAL. It is zero filled, since GlobalSize may
// round the size up and a reader trusting it then sees the padding as data.
static HGLOBAL
drag_data_for(const Win32DragData *self, size_t idx) {
    const size_t sz = self->sizes[idx];
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sz ? sz : 1);
    if (h) {
        void *p = GlobalLock(h);
        if (p) { if (sz) memcpy(p, self->data[idx], sz); GlobalUnlock(h); }
        else { GlobalFree(h); h = NULL; }
    }
    return h;
}

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
    if (IsEqualIID(riid, &kitty_IID_IDataObjectAsyncCapability)) {
        *ppv = &((Win32DragData*)This)->async_iface; This->lpVtbl->AddRef(This); return S_OK;
    }
    *ppv = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE dd_AddRef(IDataObject *This) { return InterlockedIncrement(&((Win32DragData*)This)->ref); }
static ULONG STDMETHODCALLTYPE dd_Release(IDataObject *This) {
    Win32DragData *self = (Win32DragData*)This;
    LONG r = InterlockedDecrement(&self->ref);
    if (r == 0) {
        for (size_t i = 0; i < self->stored_count; i++) ReleaseStgMedium(self->stored_media + i);
        for (size_t i = 0; i < self->count; i++) { free(self->data[i]); free(self->mimes[i]); }
        free(self);
    }
    return r;
}
static HRESULT STDMETHODCALLTYPE dd_GetData(IDataObject *This, FORMATETC *f, STGMEDIUM *m) {
    Win32DragData *self = (Win32DragData*)This;
    if (!m) return E_POINTER;
    const int stored = stored_format_index(self, f);
    if (stored >= 0) return copy_stored_medium(f->cfFormat, self->stored_media + stored, m) ? S_OK : E_OUTOFMEMORY;
    int idx = drag_format_index(self, f);
    if (idx < 0) return DV_E_FORMATETC;
    if (!self->data[idx]) {
        const bool ok = (size_t)idx < self->mime_count ? fetch_drag_data(self, (size_t)idx) : make_other_format(self, (size_t)idx);
        if (!ok) return DV_E_FORMATETC;
    }
    HGLOBAL h = drag_data_for(self, (size_t)idx);
    if (!h) return E_OUTOFMEMORY;
    m->tymed = TYMED_HGLOBAL; m->hGlobal = h; m->pUnkForRelease = NULL;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dd_GetDataHere(IDataObject *This, FORMATETC *f, STGMEDIUM *m) { (void)This; (void)f; (void)m; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE dd_QueryGetData(IDataObject *This, FORMATETC *f) {
    const Win32DragData *self = (Win32DragData*)This;
    return (stored_format_index(self, f) < 0 && drag_format_index(self, f) < 0) ? DV_E_FORMATETC : S_OK;
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
    // The formats other programs read first, since some take the first they know
    for (size_t i = self->mime_count; i < self->count; i++) f[n++] = (FORMATETC){self->formats[i], NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    for (size_t i = 0; i < self->mime_count; i++) f[n++] = (FORMATETC){self->formats[i], NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
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

#define async_self(This) ((Win32DragData*)((char*)(This) - offsetof(Win32DragData, async_iface)))
static HRESULT STDMETHODCALLTYPE da_QueryInterface(IDataObjectAsyncCapability *This, REFIID riid, void **ppv) {
    return dd_QueryInterface(&async_self(This)->iface, riid, ppv);
}
static ULONG STDMETHODCALLTYPE da_AddRef(IDataObjectAsyncCapability *This) { return dd_AddRef(&async_self(This)->iface); }
static ULONG STDMETHODCALLTYPE da_Release(IDataObjectAsyncCapability *This) { return dd_Release(&async_self(This)->iface); }
static HRESULT STDMETHODCALLTYPE da_SetAsyncMode(IDataObjectAsyncCapability *This, WINBOOL on) { async_self(This)->async_mode = on; return S_OK; }
static HRESULT STDMETHODCALLTYPE da_GetAsyncMode(IDataObjectAsyncCapability *This, WINBOOL *on) {
    if (!on) return E_INVALIDARG;
    *on = async_self(This)->async_mode; return S_OK;
}
static HRESULT STDMETHODCALLTYPE da_StartOperation(IDataObjectAsyncCapability *This, IBindCtx *ctx) {
    (void)ctx; async_self(This)->in_async_op = TRUE; return S_OK;
}
static HRESULT STDMETHODCALLTYPE da_InOperation(IDataObjectAsyncCapability *This, WINBOOL *in) {
    if (!in) return E_INVALIDARG;
    *in = async_self(This)->in_async_op; return S_OK;
}
static HRESULT STDMETHODCALLTYPE da_EndOperation(IDataObjectAsyncCapability *This, HRESULT result, IBindCtx *ctx, DWORD effects) {
    (void)result; (void)ctx; (void)effects;
    async_self(This)->in_async_op = FALSE; return S_OK;
}
#undef async_self

static IDataObjectAsyncCapabilityVtbl win32_drag_async_vtbl = {
    da_QueryInterface, da_AddRef, da_Release, da_SetAsyncMode, da_GetAsyncMode, da_StartOperation, da_InOperation, da_EndOperation
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
    // The picture goes as the drag ends, since a drop target can take its time
    // over the drop, and the picture would sit on the pointer meanwhile
    if (escape) { win32_drag.escape_pressed = true; post_drag_image(false, NULL); return DRAGDROP_S_CANCEL; }
    if (win32_drag.cancel_requested) { post_drag_image(false, NULL); return DRAGDROP_S_CANCEL; }
    if (!(keys & MK_LBUTTON)) {
        win32_drag.button_released = true;
        GetCursorPos(&win32_drag.released_at);
        post_drag_image(false, NULL);
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
    // A link to a file dropped on Explorer must never move the file away from
    // where its owner keeps it, so a drag of files copies or links them
    if (win32_drag.has_files) e = DROPEFFECT_COPY | DROPEFFECT_LINK;
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
    if (data) {
        data->iface.lpVtbl = &win32_drag_data_vtbl; data->async_iface.lpVtbl = &win32_drag_async_vtbl;
        // Data a program supplies has to be read during the drop, while kitty
        // still holds the drag, so such a drag lets no drop target read later
        data->ref = 1; data->async_mode = !win32_drag.on_demand;
    }
    if (source) { source->iface.lpVtbl = &win32_drop_source_vtbl; source->ref = 1; }
    if (window && data && source && win32_drag.count && !win32_drag.cancel_requested && take_drag_formats(data)) {
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
    for (size_t i = 0; i < _glfw.drag.item_count && win32_drag.count < WIN32_DRAG_MAX_MIMES; i++) {
        const GLFWDragSourceItem *item = _glfw.drag.items + i;
        const UINT cf = RegisterClipboardFormatA(item->mime_type);
        if (!cf) continue;
        const size_t n = win32_drag.count;
        win32_drag.formats[n] = (CLIPFORMAT)cf;
        win32_drag.mimes[n] = _glfw_strdup(item->mime_type);
        win32_drag.sizes[n] = item->data_size;
        // A program in a kitty window, the dnd kitten among them, supplies its
        // data once a drop target asks, see fetch_drag_data
        win32_drag.data[n] = item->optional_data ? malloc(item->data_size ? item->data_size : 1) : NULL;
        if (!win32_drag.mimes[n] || (item->optional_data && !win32_drag.data[n])) {
            free(win32_drag.mimes[n]); free(win32_drag.data[n]);
            free_win32_drag();
            return ENOMEM;
        }
        if (item->optional_data && item->data_size) memcpy(win32_drag.data[n], item->optional_data, item->data_size);
        if (!item->optional_data) win32_drag.on_demand = true;
        win32_drag.total = ++win32_drag.count;
    }
    if (!win32_drag.count) return EINVAL;
    add_formats_for_other_programs();
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

// More of the data a program supplies is in, for fetch_drag_data waiting on it
int _glfwPlatformDragDataReady(const char* mime_type, const char* data, size_t sz, int type) {
    (void) mime_type; (void) data; (void) sz; (void) type;
    win32_drag.data_ready = true;
    return 0;
}

int _glfwPlatformChangeDragImage(const GLFWimage* thumbnail) {
    if (!win32_drag.pending && !win32_drag.active) return 0;
    set_drag_image(thumbnail);
    return 0;
}

// Public API defined per platform. Layer shell is a Wayland concept.
GLFWAPI bool glfwIsLayerShellSupported(void) { return true; }
