//========================================================================
// GLFW 3.4 Win32 port for kitty
//========================================================================
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

#define _glfw_dlopen(name) LoadLibraryA(name)
#define _glfw_dlclose(handle) FreeLibrary((HMODULE) handle)
#define _glfw_dlsym(handle, name) (void*) GetProcAddress((HMODULE) handle, name)

#define _GLFW_WNDCLASSNAME L"kitty-glfw"

// Per-window Win32 state.
typedef struct _GLFWwindowWin32 {
    HWND    handle;
    HICON   bigIcon;
    HICON   smallIcon;

    bool    cursorTracked;
    bool    frameAction;
    bool    iconified;
    bool    maximized;
    bool    transparent;      // whether framebuffer has an alpha channel
    bool    scaleToMonitor;

    // The last received cursor position, regardless of source, used to
    // synthesize enter/leave and to restore position in disabled mode.
    int     lastCursorPosX;
    int     lastCursorPosY;
    // The last WM_CHAR high surrogate awaiting its low surrogate.
    WCHAR   highSurrogate;

    int     minwidth, minheight, maxwidth, maxheight;
    int     numer, denom;     // aspect ratio
    float   opacity;
    int     blur;             // background_blur radius (0 = off)
    bool    customFrame;      // reclaim the caption for an acrylic custom title bar
    HWND    captionButtons;   // layered overlay hosting min/max/close
    void*   dropTarget;       // IDropTarget* registered for OLE drag-and-drop (drop-in)
} _GLFWwindowWin32;

// Per-monitor Win32 state.
typedef struct _GLFWmonitorWin32 {
    HMONITOR handle;
    WCHAR    adapterName[32];
    WCHAR    displayName[32];
    char     publicAdapterName[32];
    char     publicDisplayName[32];
    bool     modesPruned;
    bool     modeChanged;
} _GLFWmonitorWin32;

// Per-cursor Win32 state.
typedef struct _GLFWcursorWin32 {
    HCURSOR handle;
} _GLFWcursorWin32;

// Process-wide Win32 state.
typedef struct _GLFWlibraryWin32 {
    HINSTANCE    instance;
    HWND         helperWindowHandle;
    ATOM         mainWindowClass;
    ATOM         helperWindowClass;
    HDEVNOTIFY   deviceNotificationHandle;
    int          acquiredMonitorCount;
    char*        clipboardString;
    int          keycodes[512];
    short int    scancodes[512];
    char         keynames[512][5];

    DWORD        foregroundLockTimeout;
    _GLFWwindow* disabledCursorWindow;
    RAWINPUT*    rawInput;
    int          rawInputSize;
    UINT         mouseTrailSize;

    // kitty main loop integration.
    bool             mainLoopRunning;
    GLFWtickcallback tickCallback;
    void*            tickCallbackData;

    struct {
        HINSTANCE instance;
        BOOL (WINAPI *SetProcessDPIAware)(void);
        BOOL (WINAPI *SetProcessDpiAwarenessContext)(HANDLE);
        UINT (WINAPI *GetDpiForWindow)(HWND);
        BOOL (WINAPI *AdjustWindowRectExForDpi)(LPRECT, DWORD, BOOL, DWORD, UINT);
        BOOL (WINAPI *EnableNonClientDpiScaling)(HWND);
    } user32;

    struct {
        HINSTANCE instance;
        UINT (WINAPI *BeginPeriod)(UINT);
        UINT (WINAPI *EndPeriod)(UINT);
    } winmm;

    struct {
        HINSTANCE instance;
        HRESULT (WINAPI *IsCompositionEnabled)(BOOL*);
        HRESULT (WINAPI *Flush)(void);
        HRESULT (WINAPI *EnableBlurBehindWindow)(HWND, const void*);
        HRESULT (WINAPI *ExtendFrameIntoClientArea)(HWND, const MARGINS*);
        HRESULT (WINAPI *SetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
    } dwmapi;
} _GLFWlibraryWin32;

#define _GLFW_PLATFORM_WINDOW_STATE         _GLFWwindowWin32  win32
#define _GLFW_PLATFORM_LIBRARY_WINDOW_STATE _GLFWlibraryWin32 win32
#define _GLFW_PLATFORM_MONITOR_STATE        _GLFWmonitorWin32 win32
#define _GLFW_PLATFORM_CURSOR_STATE         _GLFWcursorWin32  win32

#include "win32_thread.h"
#include "win32_joystick.h"
#include "wgl_context.h"

void _glfwInitTimerWin32(void);
void _glfwPollMonitorsWin32(void);
bool _glfwRegisterWindowClassWin32(void);
void _glfwUnregisterWindowClassWin32(void);
void _glfwWin32RegisterDropTarget(_GLFWwindow *window);
void _glfwWin32RevokeDropTarget(_GLFWwindow *window);
