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
    short int* k = _glfw.win32.keycodes;
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
    for (int i = 0; i < 24; i++) k[VK_F1 + i] = GLFW_FKEY_F1 + i;
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
// Window procedure
// ---------------------------------------------------------------------------

static void fitToMonitor(_GLFWwindow* window); // fwd

static LRESULT CALLBACK windowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    _GLFWwindow* window = GetPropW(hWnd, L"GLFW");
    if (!window) return DefWindowProcW(hWnd, uMsg, wParam, lParam);

    switch (uMsg) {
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
            return 0;
        }

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
            // Printable keys deliver their text via WM_CHAR; only forward the
            // functional keys here, plus releases (WM_CHAR has no up event).
            if (key || up) {
                GLFWkeyevent ev = {0};
                ev.key = key;
                ev.native_key = (int) wParam;
                ev.action = up ? GLFW_RELEASE : (HIWORD(lParam) & KF_REPEAT ? GLFW_REPEAT : GLFW_PRESS);
                ev.mods = getKeyMods();
                if (key) _glfwInputKeyboard(window, &ev);
            }
            if (uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP) break; // allow system keys
            return 0;
        }

        case WM_CHAR: case WM_SYSCHAR: {
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

// kitty publishes its configured background colour in KITTY_TITLEBAR_RGB
// (0xRRGGBB hex) so the caption can match it without a cross-module glfw call.
static COLORREF titlebarColorref(void) {
    const char* e = getenv("KITTY_TITLEBAR_RGB");
    unsigned long v = 0x1e1e1e;  // dark default
    if (e && *e) { char* end = NULL; unsigned long p = strtoul(e, &end, 16); if (end && end != e) v = p; }
    return RGB((v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
}

// Theme the title bar: colour the caption to the terminal background, hide the
// icon and the title text (drawn in the caption colour so it is invisible), and
// keep the min/max/close buttons.
static void styleTitlebar(_GLFWwindow* window) {
    HWND hwnd = window->win32.handle;
    if (!hwnd) return;
    if (_glfw.win32.dwmapi.SetWindowAttribute) {
        BOOL dark = TRUE;
        _glfw.win32.dwmapi.SetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
        COLORREF cap = titlebarColorref();
        _glfw.win32.dwmapi.SetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &cap, sizeof(cap));
        _glfw.win32.dwmapi.SetWindowAttribute(hwnd, 36 /* DWMWA_TEXT_COLOR */, &cap, sizeof(cap));
        _glfw.win32.dwmapi.SetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &cap, sizeof(cap));
    }
    // Remove the title-bar icon (WS_EX_DLGMODALFRAME drops the icon slot).
    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    SetWindowLongW(hwnd, GWL_EXSTYLE, ex | WS_EX_DLGMODALFRAME);
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
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
    if (window->win32.blur > 0) policy.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
    else if (window->win32.transparent) policy.AccentState = ACCENT_ENABLE_BLURBEHIND;
    else policy.AccentState = ACCENT_DISABLED;
    policy.GradientColor = 0x00000000;  // no tint; the terminal's bg alpha does the rest
    WIN_COMP_ATTR_DATA data = { WCA_ACCENT_POLICY, &policy, sizeof(policy) };
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
    if (window->win32.transparent) updateWindowComposition(window);
    styleTitlebar(window);
    return true;
}

void _glfwPlatformDestroyWindow(_GLFWwindow* window) {
    if (window->context.destroy) window->context.destroy(window);
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
