//========================================================================
// GLFW 3.4 Win32 port for kitty - monitors
//========================================================================

#include "internal.h"

#include <stdlib.h>
#include <string.h>

static void contentScaleForDpi(UINT dpi, float* xscale, float* yscale) {
    if (xscale) *xscale = dpi / 96.0f;
    if (yscale) *yscale = dpi / 96.0f;
}

static UINT dpiForMonitor(HMONITOR handle) {
    // GetDpiForMonitor lives in shcore.dll (Windows 8.1+); fall back to the
    // desktop DC's logical pixels per inch.
    HMODULE shcore = LoadLibraryA("shcore.dll");
    if (shcore) {
        HRESULT (WINAPI *GetDpiForMonitor)(HMONITOR, int, UINT*, UINT*) =
            (void*) GetProcAddress(shcore, "GetDpiForMonitor");
        if (GetDpiForMonitor) {
            UINT x = 96, y = 96;
            if (GetDpiForMonitor(handle, 0 /* MDT_EFFECTIVE_DPI */, &x, &y) == S_OK) {
                FreeLibrary(shcore);
                return x;
            }
        }
        FreeLibrary(shcore);
    }
    HDC dc = GetDC(NULL);
    UINT dpi = (UINT) GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    return dpi ? dpi : 96;
}

static BOOL CALLBACK monitorCallback(HMONITOR handle, HDC dc, LPRECT rect, LPARAM data) {
    (void) dc; (void) rect; (void) data;
    MONITORINFOEXW mi = { .cbSize = sizeof(mi) };
    if (!GetMonitorInfoW(handle, (MONITORINFO*) &mi)) return TRUE;

    int width = mi.rcMonitor.right - mi.rcMonitor.left;
    int height = mi.rcMonitor.bottom - mi.rcMonitor.top;

    char name[128] = {0};
    WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, name, sizeof(name) - 1, NULL, NULL);

    // Physical size is not reliably available; report zero (kitty uses content scale).
    _GLFWmonitor* monitor = _glfwAllocMonitor(name, 0, 0);
    monitor->win32.handle = handle;
    memcpy(monitor->win32.adapterName, mi.szDevice, sizeof(mi.szDevice));

    monitor->currentMode.width = width;
    monitor->currentMode.height = height;
    monitor->currentMode.redBits = 8;
    monitor->currentMode.greenBits = 8;
    monitor->currentMode.blueBits = 8;
    // Report the real refresh rate: kitty paces animations and rendering off
    // it, so hardcoding 60 makes everything feel capped on high-refresh panels.
    monitor->currentMode.refreshRate = 60;
    DEVMODEW dm = { .dmSize = sizeof(dm) };
    if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
        monitor->currentMode.refreshRate = (int)dm.dmDisplayFrequency;

    int placement = (mi.dwFlags & MONITORINFOF_PRIMARY) ? _GLFW_INSERT_FIRST : _GLFW_INSERT_LAST;
    _glfwInputMonitor(monitor, GLFW_CONNECTED, placement);
    return TRUE;
}

void _glfwPollMonitorsWin32(void) {
    EnumDisplayMonitors(NULL, NULL, monitorCallback, 0);
}

void _glfwPlatformFreeMonitor(_GLFWmonitor* monitor) {
    (void) monitor;
}

void _glfwPlatformGetMonitorPos(_GLFWmonitor* monitor, int* xpos, int* ypos) {
    MONITORINFO mi = { .cbSize = sizeof(mi) };
    if (GetMonitorInfoW(monitor->win32.handle, &mi)) {
        if (xpos) *xpos = mi.rcMonitor.left;
        if (ypos) *ypos = mi.rcMonitor.top;
    }
}

void _glfwPlatformGetMonitorContentScale(_GLFWmonitor* monitor, float* xscale, float* yscale) {
    contentScaleForDpi(dpiForMonitor(monitor->win32.handle), xscale, yscale);
}

void _glfwPlatformGetMonitorWorkarea(_GLFWmonitor* monitor, int* xpos, int* ypos, int* width, int* height) {
    MONITORINFO mi = { .cbSize = sizeof(mi) };
    if (GetMonitorInfoW(monitor->win32.handle, &mi)) {
        if (xpos) *xpos = mi.rcWork.left;
        if (ypos) *ypos = mi.rcWork.top;
        if (width) *width = mi.rcWork.right - mi.rcWork.left;
        if (height) *height = mi.rcWork.bottom - mi.rcWork.top;
    }
}

GLFWvidmode* _glfwPlatformGetVideoModes(_GLFWmonitor* monitor, int* count) {
    GLFWvidmode* mode = calloc(1, sizeof(GLFWvidmode));
    *mode = monitor->currentMode;
    *count = 1;
    return mode;
}

bool _glfwPlatformGetVideoMode(_GLFWmonitor* monitor, GLFWvidmode* mode) {
    *mode = monitor->currentMode;
    return true;
}

bool _glfwPlatformGetGammaRamp(_GLFWmonitor* monitor, GLFWgammaramp* ramp) {
    (void) monitor; (void) ramp;
    return false;
}

void _glfwPlatformSetGammaRamp(_GLFWmonitor* monitor, const GLFWgammaramp* ramp) {
    (void) monitor; (void) ramp;
}

MonitorGeometry _glfwPlatformGetMonitorGeometry(_GLFWmonitor* monitor) {
    MonitorGeometry g = {0};
    MONITORINFO mi = { .cbSize = sizeof(mi) };
    if (GetMonitorInfoW(monitor->win32.handle, &mi)) {
        g.full = (GeometryRect){ mi.rcMonitor.left, mi.rcMonitor.top,
                                 mi.rcMonitor.right - mi.rcMonitor.left,
                                 mi.rcMonitor.bottom - mi.rcMonitor.top };
        g.workarea = (GeometryRect){ mi.rcWork.left, mi.rcWork.top,
                                     mi.rcWork.right - mi.rcWork.left,
                                     mi.rcWork.bottom - mi.rcWork.top };
    }
    return g;
}
