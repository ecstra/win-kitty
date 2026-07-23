//========================================================================
// GLFW 3.4 Win32 port for kitty - windows, input, event loop
//========================================================================

#include "internal.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ole2.h>
#include <shellapi.h>

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
static void refreshBackdrop(_GLFWwindow* window);           // fwd

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
            // Maximize/restore/fullscreen: DWM carries the backdrop it sampled
            // for the old bounds through the transition, so the acrylic lands
            // showing a stale, cropped piece of wallpaper. Force a resample.
            if (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED) refreshBackdrop(window);
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
            refreshBackdrop(window);
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
    // A child (not popup) window so DWM animates it together with the main
    // window during minimize/maximize/restore instead of snapping it ahead.
    // WS_EX_LAYERED child windows are supported on Windows 8+.
    HWND overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE,
        CAPTION_CLASS, L"", WS_CHILD, 0, 0, 10, 10,
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
    // top-right of the client area; as a child window these are parent-client
    // coordinates, so the buttons ride along with the parent through animations.
    RECT cr; GetClientRect(owner, &cr);
    SetWindowPos(overlay, HWND_TOP, cr.right - W, cr.top, W, strip,
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
// This is the genuine Windows 11 acrylic material, the same one Windows
// Terminal shows -- not the plain Gaussian blur this port used previously.
// The accent policy (ACCENT_ENABLE_ACRYLICBLURBEHIND) is gone: on Win11 24H2
// it renders the same flat Gaussian blur as the legacy aero state no matter
// what tint alpha it is given, and it tapered off over the outer ~64px.
//
// The DWM system backdrop was originally rejected for one reason -- it goes
// opaque the moment the window is deactivated. That turns out to be trivially
// avoidable: the swap keys off the frame's active state, which is nothing more
// than the wParam DefWindowProc receives in WM_NCACTIVATE. windowProc reports
// active unconditionally for transparent windows, so the material survives
// losing focus. Nothing about packaging or a composition render path was
// actually required.
//
// Note the backdrop is requested untinted; kitty's own background_opacity
// stays in charge of the colour, exactly as it does on the other platforms.
// Handing the tint to DWM instead blends it with DWM's own maths and a
// saturation boost, which reads darker and warmer than the configured colour.
//
// Two modes, both of which need the empty-region blur-behind for per-pixel
// alpha (a plain Win32 window is opaque regardless of framebuffer alpha):
//   * glass (transparent, no blur): alpha only, no material.
//   * blur  (transparent + blur):   alpha plus the acrylic backdrop.
// DwmExtendFrameIntoClientArea is deliberately not used to expose the
// material -- it makes DWM paint its own caption over the custom one (see
// docs/decisions.md).
static void updateWindowComposition(_GLFWwindow* window) {
    HWND hwnd = window->win32.handle;
    if (!hwnd) return;
    const bool wantBlur = window->win32.blur > 0;
    const bool wantTransparent = window->win32.transparent;
    const bool acrylic = wantTransparent && wantBlur;
    // Per-pixel alpha for every transparent mode. Dark caption/border is set
    // separately in styleTitlebar().
    if (_glfw.win32.dwmapi.EnableBlurBehindWindow) {
        DWM_BLURBEHIND bb; memset(&bb, 0, sizeof bb);
        bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
        bb.fEnable = wantTransparent ? TRUE : FALSE;
        bb.hRgnBlur = wantTransparent ? CreateRectRgn(0, 0, -1, -1) : NULL;  // empty -> no blur of its own
        _glfw.win32.dwmapi.EnableBlurBehindWindow(hwnd, &bb);
        if (bb.hRgnBlur) DeleteObject(bb.hRgnBlur);
    }
    if (_glfw.win32.dwmapi.SetWindowAttribute) {
        DWORD backdrop = acrylic ? 3 /* DWMSBT_TRANSIENTWINDOW */ : 1 /* DWMSBT_NONE */;
        _glfw.win32.dwmapi.SetWindowAttribute(hwnd, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &backdrop, sizeof(backdrop));
    }
}

// Force DWM to resample the acrylic for the window's current bounds.
//
// Through a maximize/restore/fullscreen transition DWM keeps the backdrop it
// sampled for the old bounds, so once the window lands the acrylic is a stale
// piece of wallpaper, cropped to where the window used to be. Re-setting the
// attribute to the value it already holds is a no-op as far as DWM is
// concerned, so clear it first: the off/on pair is what makes it resample.
static void refreshBackdrop(_GLFWwindow* window) {
    HWND hwnd = window->win32.handle;
    if (!hwnd || !_glfw.win32.dwmapi.SetWindowAttribute) return;
    if (!window->win32.transparent || window->win32.blur <= 0) return;
    DWORD none = 1 /* DWMSBT_NONE */, acrylic = 3 /* DWMSBT_TRANSIENTWINDOW */;
    _glfw.win32.dwmapi.SetWindowAttribute(hwnd, 38, &none, sizeof(none));
    _glfw.win32.dwmapi.SetWindowAttribute(hwnd, 38, &acrylic, sizeof(acrylic));
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
    _glfwWin32RegisterDropTarget(window);

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
void _glfwPlatformRequestWindowAttention(_GLFWwindow* window) {
    // Deliberately a no-op on Windows: FlashWindowEx flashes the taskbar button.
    // Under a Windows pseudoconsole, conhost re-renders the child's output and
    // mangles the shell's BEL-terminated OSC sequences (window title, OSC 7 cwd,
    // OSC 133 prompt marks -- emitted on every command and prompt), delivering the
    // BEL terminators to kitty as standalone bells. That makes bell-on-attention
    // fire constantly on noise (a taskbar flash on every kitten run/exit and
    // prompt), most of it from sources kitty does not control (e.g. the shell/p10k
    // title). So the taskbar alert is net-negative here and is disabled; the
    // (focus-gated) tab bell indicator still covers genuine cases.
    (void)window;
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
// Dragging OUT of kitty (the dnd kitten) is not implemented -- see
// _glfwPlatformStartDrag below.

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
static HRESULT STDMETHODCALLTYPE dt_DragEnter(IDropTarget *This, IDataObject *pdo, DWORD keys, POINTL pt, DWORD *effect) {
    (void)keys;
    Win32DropTarget *self = (Win32DropTarget*)This;
    win32_drop.window = self->window;
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
    _glfwInputDropEvent(self->window, GLFW_DROP_LEAVE, 0, 0, NULL, 0, false);
    win32_drop.offered_count = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dt_Drop(IDropTarget *This, IDataObject *pdo, DWORD keys, POINTL pt, DWORD *effect) {
    (void)keys;
    Win32DropTarget *self = (Win32DropTarget*)This;
    free_win32_drop_items();
    win32_drop.window = self->window;
    win32_drop.offered_count = 0;
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
// Drag and drop is not implemented on Windows. Report it as unsupported (not 0,
// which glfwStartDrag reads as success) so kitty cleans up its drag state instead
// of waiting forever for a drop. Without this, dragging a tab wedges the tab bar
// and blocks all later mouse dragging until restart.
int _glfwPlatformStartDrag(_GLFWwindow* window, const GLFWimage* thumbnail) { (void) window; (void) thumbnail; return ENOTSUP; }
void _glfwPlatformCancelDrag(_GLFWwindow* window) { (void) window; }
void _glfwPlatformFreeDragSourceData(void) {}
int _glfwPlatformDragDataReady(const char* mime_type, const char* data, size_t sz, int type) { (void) mime_type; (void) data; (void) sz; (void) type; return false; }
int _glfwPlatformChangeDragImage(const GLFWimage* thumbnail) { (void) thumbnail; return false; }

// Public API defined per platform. Layer shell is a Wayland concept.
GLFWAPI bool glfwIsLayerShellSupported(void) { return false; }
