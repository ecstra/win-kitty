//========================================================================
// GLFW 3.4 Win32 port for kitty - initialization, threads, timers, main loop
//========================================================================

#include "internal.h"

#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Version
// ---------------------------------------------------------------------------

const char* _glfwPlatformGetVersionString(void) {
    return _GLFW_VERSION_NUMBER " Win32 WGL kitty";
}

// ---------------------------------------------------------------------------
// Thread local storage and mutexes on native Win32 primitives
// ---------------------------------------------------------------------------

bool _glfwPlatformCreateTls(_GLFWtls* tls) {
    tls->win32.index = TlsAlloc();
    if (tls->win32.index == TLS_OUT_OF_INDEXES) {
        _glfwInputError(GLFW_PLATFORM_ERROR, "Win32: Failed to allocate TLS index");
        return false;
    }
    tls->win32.allocated = true;
    return true;
}

void _glfwPlatformDestroyTls(_GLFWtls* tls) {
    if (tls->win32.allocated) TlsFree(tls->win32.index);
    memset(&tls->win32, 0, sizeof(tls->win32));
}

void* _glfwPlatformGetTls(_GLFWtls* tls) {
    return TlsGetValue(tls->win32.index);
}

void _glfwPlatformSetTls(_GLFWtls* tls, void* value) {
    TlsSetValue(tls->win32.index, value);
}

bool _glfwPlatformCreateMutex(_GLFWmutex* mutex) {
    InitializeCriticalSection(&mutex->win32.section);
    mutex->win32.allocated = true;
    return true;
}

void _glfwPlatformDestroyMutex(_GLFWmutex* mutex) {
    if (mutex->win32.allocated) DeleteCriticalSection(&mutex->win32.section);
    memset(&mutex->win32, 0, sizeof(mutex->win32));
}

void _glfwPlatformLockMutex(_GLFWmutex* mutex) {
    EnterCriticalSection(&mutex->win32.section);
}

void _glfwPlatformUnlockMutex(_GLFWmutex* mutex) {
    LeaveCriticalSection(&mutex->win32.section);
}

// ---------------------------------------------------------------------------
// Load the optional user32/dwmapi entry points that vary by Windows version
// ---------------------------------------------------------------------------

static bool loadLibraries(void) {
    _glfw.win32.user32.instance = LoadLibraryA("user32.dll");
    if (!_glfw.win32.user32.instance) {
        _glfwInputError(GLFW_PLATFORM_ERROR, "Win32: Failed to load user32.dll");
        return false;
    }
    _glfw.win32.user32.SetProcessDPIAware = (void*)GetProcAddress(_glfw.win32.user32.instance, "SetProcessDPIAware");
    _glfw.win32.user32.SetProcessDpiAwarenessContext = (void*)GetProcAddress(_glfw.win32.user32.instance, "SetProcessDpiAwarenessContext");
    _glfw.win32.user32.GetDpiForWindow = (void*)GetProcAddress(_glfw.win32.user32.instance, "GetDpiForWindow");
    _glfw.win32.user32.AdjustWindowRectExForDpi = (void*)GetProcAddress(_glfw.win32.user32.instance, "AdjustWindowRectExForDpi");
    _glfw.win32.user32.EnableNonClientDpiScaling = (void*)GetProcAddress(_glfw.win32.user32.instance, "EnableNonClientDpiScaling");
    _glfw.win32.user32.SetWindowCompositionAttribute = (void*)GetProcAddress(_glfw.win32.user32.instance, "SetWindowCompositionAttribute");

    _glfw.win32.dwmapi.instance = LoadLibraryA("dwmapi.dll");
    if (_glfw.win32.dwmapi.instance) {
        _glfw.win32.dwmapi.IsCompositionEnabled = (void*)GetProcAddress(_glfw.win32.dwmapi.instance, "DwmIsCompositionEnabled");
        _glfw.win32.dwmapi.Flush = (void*)GetProcAddress(_glfw.win32.dwmapi.instance, "DwmFlush");
        _glfw.win32.dwmapi.EnableBlurBehindWindow = (void*)GetProcAddress(_glfw.win32.dwmapi.instance, "DwmEnableBlurBehindWindow");
        _glfw.win32.dwmapi.ExtendFrameIntoClientArea = (void*)GetProcAddress(_glfw.win32.dwmapi.instance, "DwmExtendFrameIntoClientArea");
        _glfw.win32.dwmapi.SetWindowAttribute = (void*)GetProcAddress(_glfw.win32.dwmapi.instance, "DwmSetWindowAttribute");
    }
    return true;
}

static void freeLibraries(void) {
    if (_glfw.win32.dwmapi.instance) FreeLibrary(_glfw.win32.dwmapi.instance);
    if (_glfw.win32.user32.instance) FreeLibrary(_glfw.win32.user32.instance);
}

static void enableProcessDpiAwareness(void) {
    // Per-monitor-v2 gives correct scaling; fall back to system aware.
    if (_glfw.win32.user32.SetProcessDpiAwarenessContext)
        _glfw.win32.user32.SetProcessDpiAwarenessContext((HANDLE) -4 /* PER_MONITOR_AWARE_V2 */);
    else if (_glfw.win32.user32.SetProcessDPIAware)
        _glfw.win32.user32.SetProcessDPIAware();
}

// ---------------------------------------------------------------------------
// Platform init / terminate
// ---------------------------------------------------------------------------

int _glfwPlatformInit(bool* supports_window_occlusion) {
    if (supports_window_occlusion) *supports_window_occlusion = false;

    _glfw.win32.instance = GetModuleHandleW(NULL);
    if (!loadLibraries()) return false;
    enableProcessDpiAwareness();

    if (!_glfwRegisterWindowClassWin32()) return false;
    if (!_glfwInitWGL()) return false;
    _glfwPollMonitorsWin32();
    return true;
}

void _glfwPlatformTerminate(void) {
    _glfwTerminateWGL();
    _glfwUnregisterWindowClassWin32();
    free(_glfw.win32.clipboardString);
    freeLibraries();
}

// ---------------------------------------------------------------------------
// Native key names (Win32 has no stable per-scancode names; report none)
// ---------------------------------------------------------------------------

const char* _glfwPlatformGetNativeKeyName(int native_key) {
    (void) native_key;
    return "";
}

int _glfwPlatformGetNativeKeyForKey(uint32_t key) {
    (void) key;
    return -1;
}

// ---------------------------------------------------------------------------
// Clipboard (UTF-8 text via CF_UNICODETEXT)
// ---------------------------------------------------------------------------

// Pull all of kitty's data for a mime type into a freshly malloc'd buffer, via
// the clipboard iterator API (the same protocol get_clipboard_data uses on X11).
// Returns the byte count. *out holds the buffer, which the caller frees, or NULL.
static size_t
pull_clipboard_data(const _GLFWClipboardData *cd, const char *mime, char **out) {
    *out = NULL;
    if (!cd->get_data) return 0;
    GLFWDataChunk chunk = cd->get_data(mime, NULL, cd->ctype);
    void *iter = chunk.iter;
    if (!iter) return 0;
    char *buf = NULL; size_t sz = 0, cap = 0;
    while (true) {
        chunk = cd->get_data(mime, iter, cd->ctype);
        if (!chunk.sz) break;
        if (cap < sz + chunk.sz) {
            size_t want = sz + 4 * chunk.sz;
            cap = cap * 2 > want ? cap * 2 : want;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); cd->get_data(NULL, iter, cd->ctype); return 0; }
            buf = nb;
        }
        memcpy(buf + sz, chunk.data, chunk.sz);
        sz += chunk.sz;
        if (chunk.free) chunk.free((void*)chunk.free_data);
    }
    cd->get_data(NULL, iter, cd->ctype);   // finalize / release the iterator
    *out = buf;
    return sz;
}

// Windows has an eager clipboard (unlike X11/Wayland's lazy selection ownership),
// so grab kitty's text now and hand it to the OS as CF_UNICODETEXT. There is no
// primary selection on Windows, so only GLFW_CLIPBOARD is honoured.
void _glfwPlatformSetClipboard(GLFWClipboardType t) {
    if (t != GLFW_CLIPBOARD) return;
    const _GLFWClipboardData *cd = &_glfw.clipboard;
    const char *mime = NULL;
    for (size_t i = 0; i < cd->num_mime_types; i++) {
        const char *m = cd->mime_types[i];
        if (!m) continue;
        if (strcmp(m, "text/plain;charset=utf-8") == 0) { mime = m; break; }  // best
        if (!mime && strcmp(m, "text/plain") == 0) mime = m;                  // acceptable
    }
    if (!mime) return;   // nothing text-like on offer (e.g. an image copy)
    char *utf8 = NULL;
    size_t sz = pull_clipboard_data(cd, mime, &utf8);
    if (!utf8) return;   // empty selection or pull failed
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)sz, NULL, 0);
    HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, ((size_t)wlen + 1) * sizeof(WCHAR));
    if (hmem) {
        WCHAR *wide = GlobalLock(hmem);
        if (wide) {
            if (wlen) MultiByteToWideChar(CP_UTF8, 0, utf8, (int)sz, wide, wlen);
            wide[wlen] = 0;
            GlobalUnlock(hmem);
            if (OpenClipboard(_glfw.win32.helperWindowHandle)) {
                EmptyClipboard();
                if (!SetClipboardData(CF_UNICODETEXT, hmem)) GlobalFree(hmem);  // we still own it if it fails
                CloseClipboard();
            } else GlobalFree(hmem);
        } else GlobalFree(hmem);
    }
    free(utf8);
}

void _glfwPlatformGetClipboard(GLFWClipboardType clipboard_type, const char* mime_type, GLFWclipboardwritedatafun write_data, void* object) {
    (void) clipboard_type;
    if (mime_type && strcmp(mime_type, "text/plain") != 0 && strcmp(mime_type, "text/plain;charset=utf-8") != 0) return;
    if (!OpenClipboard(_glfw.win32.helperWindowHandle)) return;
    HANDLE object_handle = GetClipboardData(CF_UNICODETEXT);
    if (object_handle) {
        WCHAR* wide = GlobalLock(object_handle);
        if (wide) {
            int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
            if (n > 0) {
                char* utf8 = calloc((size_t) n, 1);
                if (utf8) {
                    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, n, NULL, NULL);
                    write_data(object, utf8, (size_t)(n - 1));
                    free(utf8);
                }
            }
            GlobalUnlock(object_handle);
        }
    }
    CloseClipboard();
}

// ---------------------------------------------------------------------------
// Timers and main loop (kitty drives rendering through the tick callback)
// ---------------------------------------------------------------------------

typedef struct {
    unsigned long long id;
    monotonic_t        interval;
    monotonic_t        next_fire;
    bool               repeats;
    bool               enabled;
    GLFWuserdatafun    callback;
    void*              callback_data;
    GLFWuserdatafun    free_callback;
} Timer;

#define MAX_TIMERS 128
static Timer timers[MAX_TIMERS];
static unsigned long long next_timer_id = 1;

unsigned long long _glfwPlatformAddTimer(monotonic_t interval, bool repeats, GLFWuserdatafun callback, void* callback_data, GLFWuserdatafun free_callback) {
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (timers[i].id) continue;
        timers[i] = (Timer){
            .id            = next_timer_id++,
            .interval      = interval,
            .next_fire     = monotonic() + interval,
            .repeats       = repeats,
            .enabled       = true,
            .callback      = callback,
            .callback_data = callback_data,
            .free_callback = free_callback,
        };
        return timers[i].id;
    }
    return 0;
}

void _glfwPlatformUpdateTimer(unsigned long long timer_id, monotonic_t interval, bool enabled) {
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (timers[i].id != timer_id) continue;
        timers[i].interval = interval;
        timers[i].enabled = enabled;
        timers[i].next_fire = monotonic() + interval;
        return;
    }
}

void _glfwPlatformRemoveTimer(unsigned long long timer_id) {
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (timers[i].id != timer_id) continue;
        if (timers[i].free_callback) timers[i].free_callback(timers[i].id, timers[i].callback_data);
        memset(&timers[i], 0, sizeof(Timer));
        return;
    }
}

static monotonic_t process_timers(void) {
    monotonic_t now = monotonic();
    monotonic_t soonest = -1;
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (!timers[i].id || !timers[i].enabled) continue;
        if (timers[i].next_fire <= now) {
            if (timers[i].callback) timers[i].callback(timers[i].id, timers[i].callback_data);
            if (timers[i].repeats) timers[i].next_fire = now + timers[i].interval;
            else { _glfwPlatformRemoveTimer(timers[i].id); continue; }
        }
        if (soonest < 0 || timers[i].next_fire < soonest) soonest = timers[i].next_fire;
    }
    return soonest;
}

void _glfwPlatformRunMainLoop(GLFWtickcallback tick_callback, void* data) {
    _glfw.win32.mainLoopRunning = true;
    _glfw.win32.tickCallback = tick_callback;
    _glfw.win32.tickCallbackData = data;

    while (_glfw.win32.mainLoopRunning) {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { _glfw.win32.mainLoopRunning = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!_glfw.win32.mainLoopRunning) break;

        tick_callback(data);

        monotonic_t soonest = process_timers();
        DWORD wait_ms = INFINITE;
        if (soonest >= 0) {
            monotonic_t delta = soonest - monotonic();
            wait_ms = delta <= 0 ? 0 : (DWORD) monotonic_t_to_ms(delta);
        }
        // Sleep until a message arrives or the next timer is due.
        MsgWaitForMultipleObjects(0, NULL, FALSE, wait_ms, QS_ALLINPUT);
    }
}

void _glfwPlatformStopMainLoop(void) {
    _glfw.win32.mainLoopRunning = false;
    PostMessageW(_glfw.win32.helperWindowHandle, WM_NULL, 0, 0);
}

void _glfwPlatformPostEmptyEvent(void) {
    PostMessageW(_glfw.win32.helperWindowHandle, WM_NULL, 0, 0);
}

// ---------------------------------------------------------------------------
// Joysticks (not supported)
// ---------------------------------------------------------------------------

bool _glfwPlatformInitJoysticks(void) { return true; }
void _glfwPlatformTerminateJoysticks(void) {}
int _glfwPlatformPollJoystick(_GLFWjoystick* js, int mode) { (void)js; (void)mode; return false; }
void _glfwPlatformUpdateGamepadGUID(char* guid) { (void)guid; }

// ---------------------------------------------------------------------------
// Color scheme / misc
// ---------------------------------------------------------------------------

void _glfwPlatformInputColorScheme(GLFWColorScheme appearance) { (void)appearance; }

GLFWAPI GLFWColorScheme glfwGetCurrentSystemColorTheme(bool query_if_unintialized) {
    (void) query_if_unintialized;
    HKEY key;
    DWORD light = 1, sz = sizeof(light);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        RegQueryValueExW(key, L"AppsUseLightTheme", NULL, NULL, (LPBYTE) &light, &sz);
        RegCloseKey(key);
    }
    return light ? GLFW_COLOR_SCHEME_LIGHT : GLFW_COLOR_SCHEME_DARK;
}
void _glfwPlatformChangeCursorTheme(void) {}

// ---------------------------------------------------------------------------
// EGL / Vulkan not used on this port (WGL only)
// ---------------------------------------------------------------------------

EGLenum _glfwPlatformGetEGLPlatform(EGLint** attribs) { (void)attribs; return 0; }
EGLNativeDisplayType _glfwPlatformGetEGLNativeDisplay(void) { return 0; }
EGLNativeWindowType _glfwPlatformGetEGLNativeWindow(_GLFWwindow* window) { (void)window; return 0; }

void _glfwPlatformGetRequiredInstanceExtensions(char** extensions) { (void)extensions; }
int _glfwPlatformGetPhysicalDevicePresentationSupport(VkInstance instance, VkPhysicalDevice device, uint32_t queuefamily) {
    (void)instance; (void)device; (void)queuefamily; return false;
}
VkResult _glfwPlatformCreateWindowSurface(VkInstance instance, _GLFWwindow* window, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) {
    (void)instance; (void)window; (void)allocator; (void)surface;
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}
