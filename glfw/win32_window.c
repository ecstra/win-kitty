//========================================================================
// GLFW 3.4 Win32 port for kitty - windows, input, event loop
//========================================================================

#include "internal.h"

#include <stdlib.h>
#include <string.h>

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

static void ensureCaptionButtons(_GLFWwindow* window);      // fwd
static void positionCaptionButtons(_GLFWwindow* window);    // fwd
static void updateWindowComposition(_GLFWwindow* window);   // fwd

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

static void fitToMonitor(_GLFWwindow* window); // fwd

static LRESULT CALLBACK windowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
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
            _glfwInputWindowFocus(window, true);
            return 0;
        case WM_KILLFOCUS:
            _glfwInputWindowFocus(window, false);
            return 0;

        case WM_PAINT:
            _glfwInputWindowDamage(window);
            break; // let DefWindowProc validate the region

        case WM_SIZE: {
            int width = LOWORD(lParam), height = HIWORD(lParam);
            window->win32.iconified = wParam == SIZE_MINIMIZED;
            window->win32.maximized = wParam == SIZE_MAXIMIZED;
            _glfwInputFramebufferSize(window, width, height);
            _glfwInputWindowSize(window, width, height);
            positionCaptionButtons(window);
            // DWM drops the acrylic accent when the window is maximized/restored;
            // re-apply it on those transitions.
            if (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED) updateWindowComposition(window);
            // The Win32 modal resize loop blocks our main loop, so render here to
            // keep the window live (otherwise DWM stretches the stale frame).
            if (_glfw.win32.tickCallback && !window->win32.iconified)
                _glfw.win32.tickCallback(_glfw.win32.tickCallbackData);
            return 0;
        }

        case WM_ENTERSIZEMOVE:
            _glfwInputLiveResize(window, true);
            SetTimer(hWnd, 1001, 12, NULL);   // ~80fps repaint during the drag
            break;
        case WM_EXITSIZEMOVE:
            KillTimer(hWnd, 1001);
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

        case WM_MOUSEWHEEL: {
            GLFWScrollEvent ev = {0};
            ev.y_offset = ev.unscaled.y = (double) GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            ev.keyboard_modifiers = getKeyMods();
            _glfwInputScroll(window, &ev);
            return 0;
        }
        case WM_MOUSEHWHEEL: {
            GLFWScrollEvent ev = {0};
            ev.x_offset = ev.unscaled.x = -(double) GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            ev.keyboard_modifiers = getKeyMods();
            _glfwInputScroll(window, &ev);
            return 0;
        }

        case WM_KEYDOWN: case WM_SYSKEYDOWN:
        case WM_KEYUP: case WM_SYSKEYUP: {
            const bool up = uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP;
            uint32_t key = (wParam < 512) ? (uint32_t) _glfw.win32.keycodes[wParam] : 0;
            int mods = getKeyMods();
            const bool functional = key >= GLFW_FKEY_FIRST;
            const bool shortcutMod = mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER);
            // A plain printable key (no ctrl/alt) is delivered as text by WM_CHAR,
            // to keep dead keys and layouts working. Forward everything else here:
            // functional keys (arrows, enter, tab...) and any key combined with a
            // shortcut modifier, so kitty can match bindings and encode ctrl+<key>.
            if (key && (functional || shortcutMod)) {
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
                    ev.key = cp;
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
    return true;
}

void _glfwUnregisterWindowClassWin32(void) {
    if (_glfw.win32.helperWindowHandle) DestroyWindow(_glfw.win32.helperWindowHandle);
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

// --- Transparency + acrylic blur via the DWM composition attribute -----------
// A normal Win32 window is opaque regardless of the GL framebuffer alpha. The
// accent policy tells DWM to honor per-pixel alpha and (optionally) draw an
// acrylic-blurred backdrop behind the window's translucent pixels.
typedef enum {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
} ACCENT_STATE;
typedef struct { ACCENT_STATE AccentState; DWORD AccentFlags; DWORD GradientColor; DWORD AnimationId; } ACCENT_POLICY;
typedef struct { DWORD Attrib; PVOID pvData; SIZE_T cbData; } WIN_COMP_ATTR_DATA;
#define WCA_ACCENT_POLICY 19

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
// Text (for the Segoe caption-icon glyphs)
extern int WINAPI GdipCreateFontFamilyFromName(const WCHAR*, void*, void**);
extern int WINAPI GdipDeleteFontFamily(void*);
extern int WINAPI GdipCreateFont(void*, float, int, int, void**);
extern int WINAPI GdipDeleteFont(void*);
extern int WINAPI GdipCreateStringFormat(int, unsigned short, void**);
extern int WINAPI GdipDeleteStringFormat(void*);
extern int WINAPI GdipSetStringFormatAlign(void*, int);
extern int WINAPI GdipSetStringFormatLineAlign(void*, int);
extern int WINAPI GdipDrawString(void*, const WCHAR*, int, void*, const void*, void*, void*);
extern int WINAPI GdipSetTextRenderingHint(void*, int);
typedef struct { float X, Y, W, H; } GpRectF;

#define CB_COUNT 3   // minimize, maximize/restore, close
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
        GdipSetTextRenderingHint(g, 4 /* antialias, alpha-correct on a layered window */);
        // Load the native caption-icon font once (Win11 ships Segoe Fluent Icons,
        // Win10 ships Segoe MDL2 Assets; the caption glyphs share codepoints).
        void* fam = NULL;
        if (GdipCreateFontFamilyFromName(L"Segoe Fluent Icons", NULL, &fam) != 0) {
            fam = NULL; GdipCreateFontFamilyFromName(L"Segoe MDL2 Assets", NULL, &fam);
        }
        void* font = NULL;
        if (fam) GdipCreateFont(fam, (float) MulDiv(10, dpi, 96), 0 /* regular */, 2 /* pixel */, &font);
        void* fmt = NULL;
        if (GdipCreateStringFormat(0, 0, &fmt) == 0) {
            GdipSetStringFormatAlign(fmt, 1 /* center */);
            GdipSetStringFormatLineAlign(fmt, 1 /* center */);
        }
        int top = (H - bh) / 2;
        float rad = (float) MulDiv(7, dpi, 96);
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
            // glyph: draw the native Segoe caption icon, centered in the button
            //  E921 minimize, E922 maximize, E923 restore, E8BB close
            WCHAR glyph[2] = { 0, 0 };
            glyph[0] = (i == 0) ? 0xE921u
                     : (i == 1) ? (IsZoomed(st->owner->win32.handle) ? 0xE923u : 0xE922u)
                                : 0xE8BBu;
            ULONG gcol = hot ? 0xFFFFFFFFu : 0xFFE6E6E6u;
            void* gbrush = NULL;
            if (font && fmt && GdipCreateSolidFill(gcol, &gbrush) == 0) {
                GpRectF layout = { (float) bx, (float) top, (float) bw, (float) bh };
                GdipDrawString(g, glyph, 1, font, &layout, fmt, gbrush);
                GdipDeleteBrush(gbrush);
            }
        }
        if (font) GdipDeleteFont(font);
        if (fam) GdipDeleteFontFamily(fam);
        if (fmt) GdipDeleteStringFormat(fmt);
        GdipDeleteGraphics(g);
    }
    // premultiply for UpdateLayeredWindow
    uint8_t* px = (uint8_t*) bits;
    for (int i = 0; i < W * H; i++) {
        uint8_t a = px[3];
        px[0] = (uint8_t)(px[0] * a / 255); px[1] = (uint8_t)(px[1] * a / 255); px[2] = (uint8_t)(px[2] * a / 255);
        px += 4;
    }
    RECT wr; GetWindowRect(overlay, &wr);
    POINT dst = { wr.left, wr.top }, srcp = { 0, 0 }; SIZE sz = { W, H };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(overlay, screen, &dst, &sz, dc, &srcp, 0, &bf, ULW_ALPHA);
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
    HWND overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        CAPTION_CLASS, L"", WS_POPUP, 0, 0, 10, 10,
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
    // top-right of the client area, in screen coords
    RECT cr; GetClientRect(owner, &cr);
    POINT tr = { cr.right, cr.top }; ClientToScreen(owner, &tr);
    SetWindowPos(overlay, HWND_TOP, tr.x - W, tr.y, W, strip,
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
}

static void updateWindowComposition(_GLFWwindow* window) {
    HWND hwnd = window->win32.handle;
    if (!hwnd) return;
    // The accent policy is the only mechanism that makes an OpenGL window's
    // per-pixel alpha translucent (the Win11 system backdrop does not compose
    // with a GL surface). It blurs behind the client area.
    if (_glfw.win32.dwmapi.SetWindowAttribute) {
        DWORD backdrop = window->win32.blur > 0 ? 3 : 1;
        _glfw.win32.dwmapi.SetWindowAttribute(hwnd, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &backdrop, sizeof(backdrop));
    }
    if (!_glfw.win32.user32.SetWindowCompositionAttribute) return;
    ACCENT_POLICY policy = {0};
    if (window->win32.blur > 0) {
        // DWM disables acrylic blur for maximized windows, so fall back to the
        // classic blur-behind (which does render when zoomed) in that state.
        policy.AccentState = IsZoomed(hwnd) ? ACCENT_ENABLE_BLURBEHIND : ACCENT_ENABLE_ACRYLICBLURBEHIND;
    }
    else if (window->win32.transparent) policy.AccentState = ACCENT_ENABLE_BLURBEHIND;
    else policy.AccentState = ACCENT_DISABLED;
    policy.GradientColor = 0x00000000;  // no tint; the terminal's bg alpha does the rest
    WIN_COMP_ATTR_DATA data = { WCA_ACCENT_POLICY, &policy, sizeof(policy) };
    // Toggle off first so DWM re-composites cleanly across maximize/restore
    // transitions (otherwise the accent can get stuck disabled after unzoom).
    ACCENT_POLICY off = {0};
    WIN_COMP_ATTR_DATA offdata = { WCA_ACCENT_POLICY, &off, sizeof(off) };
    _glfw.win32.user32.SetWindowCompositionAttribute(hwnd, &offdata);
    _glfw.win32.user32.SetWindowCompositionAttribute(hwnd, &data);
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
    AdjustWindowRectEx(&rect, style, FALSE, WS_EX_APPWINDOW);

    window->win32.handle = CreateWindowExW(WS_EX_APPWINDOW, _GLFW_WNDCLASSNAME, wtitle ? wtitle : L"kitty",
        style, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, _glfw.win32.instance, NULL);
    free(wtitle);
    if (!window->win32.handle) {
        _glfwInputError(GLFW_PLATFORM_ERROR, "Win32: Failed to create window");
        return false;
    }
    SetPropW(window->win32.handle, L"GLFW", window);

    if (ctxconfig->client != GLFW_NO_API) {
        if (!_glfwCreateContextWGL(window, ctxconfig, fbconfig)) return false;
        if (!_glfwRefreshContextAttribs(window, ctxconfig)) return false;
    }
    window->win32.customFrame = window->decorated && !window->monitor;
    if (window->win32.transparent) updateWindowComposition(window);
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
    if (window->context.destroy) window->context.destroy(window);
    if (window->win32.captionButtons) {
        DestroyWindow(window->win32.captionButtons);
        window->win32.captionButtons = NULL;
    }
    if (window->win32.handle) {
        RemovePropW(window->win32.handle, L"GLFW");
        DestroyWindow(window->win32.handle);
        window->win32.handle = NULL;
    }
    free(window->win32.bigIcon);
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

void _glfwPlatformSetWindowPos(_GLFWwindow* window, int xpos, int ypos) {
    RECT rect = { xpos, ypos, xpos, ypos };
    AdjustWindowRectEx(&rect, windowStyle(window), FALSE, WS_EX_APPWINDOW);
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
    AdjustWindowRectEx(&rect, windowStyle(window), FALSE, WS_EX_APPWINDOW);
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
    AdjustWindowRectEx(&rect, windowStyle(window), FALSE, WS_EX_APPWINDOW);
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
void _glfwPlatformRequestWindowAttention(_GLFWwindow* window) { FlashWindow(window->win32.handle, TRUE); }
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
    return _glfw.win32.user32.SetWindowCompositionAttribute != NULL;
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
void _glfwPlatformSetCursorMode(_GLFWwindow* window, int mode) {
    if (mode == GLFW_CURSOR_HIDDEN || mode == GLFW_CURSOR_DISABLED) { (void) window; }
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
        case GLFW_NWSE_RESIZE_CURSOR: id = IDC_SIZENWSE; break;
        case GLFW_NESW_RESIZE_CURSOR: id = IDC_SIZENESW; break;
        default: break;
    }
    cursor->win32.handle = LoadCursorW(NULL, (LPCWSTR) id);
    return true;
}
void _glfwPlatformDestroyCursor(_GLFWcursor* cursor) { (void) cursor; }
void _glfwPlatformSetCursor(_GLFWwindow* window, _GLFWcursor* cursor) {
    if (window->win32.cursorTracked && cursor && cursor->win32.handle) SetCursor(cursor->win32.handle);
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
    MsgWaitForMultipleObjects(0, NULL, FALSE, (DWORD) monotonic_t_to_ms(timeout), QS_ALLINPUT);
    _glfwPlatformPollEvents();
}

// ---------------------------------------------------------------------------
// Drag and drop (not supported on this port yet)
// ---------------------------------------------------------------------------

void _glfwPlatformRequestDropUpdate(_GLFWwindow* window) { (void) window; }
ssize_t _glfwPlatformReadAvailableDropData(GLFWwindow* w, GLFWDropEvent* ev, char* buffer, size_t sz) { (void) w; (void) ev; (void) buffer; (void) sz; return -1; }
void _glfwPlatformEndDrop(GLFWwindow* w, GLFWDragOperationType op) { (void) w; (void) op; }
int _glfwPlatformRequestDropData(_GLFWwindow* window, const char* mime) { (void) window; (void) mime; return -1; }
int _glfwPlatformStartDrag(_GLFWwindow* window, const GLFWimage* thumbnail) { (void) window; (void) thumbnail; return false; }
void _glfwPlatformCancelDrag(_GLFWwindow* window) { (void) window; }
void _glfwPlatformFreeDragSourceData(void) {}
int _glfwPlatformDragDataReady(const char* mime_type, const char* data, size_t sz, int type) { (void) mime_type; (void) data; (void) sz; (void) type; return false; }
int _glfwPlatformChangeDragImage(const GLFWimage* thumbnail) { (void) thumbnail; return false; }

// Public API defined per platform. Layer shell is a Wayland concept.
GLFWAPI bool glfwIsLayerShellSupported(void) { return false; }
