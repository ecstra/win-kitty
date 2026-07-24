//========================================================================
// GLFW 3.4 Win32 port for kitty - WGL context
//========================================================================

#include "internal.h"

#include <stdlib.h>

#include "win32_acrylic.h"

// WGL_ARB_pixel_format / WGL_ARB_create_context tokens used below.
#define WGL_DRAW_TO_WINDOW_ARB 0x2001
#define WGL_SUPPORT_OPENGL_ARB 0x2010
#define WGL_DOUBLE_BUFFER_ARB  0x2011
#define WGL_PIXEL_TYPE_ARB     0x2013
#define WGL_TYPE_RGBA_ARB      0x202b
#define WGL_ACCELERATION_ARB   0x2003
#define WGL_FULL_ACCELERATION_ARB 0x2027
#define WGL_RED_BITS_ARB       0x2015
#define WGL_GREEN_BITS_ARB     0x2017
#define WGL_BLUE_BITS_ARB      0x2019
#define WGL_ALPHA_BITS_ARB     0x201b
#define WGL_DEPTH_BITS_ARB     0x2022
#define WGL_STENCIL_BITS_ARB   0x2023
#define WGL_SAMPLES_ARB        0x2042
#define WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB 0x20a9

#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB  0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#define WGL_CONTEXT_FLAGS_ARB         0x2094
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x00000002

static void makeContextCurrentWGL(_GLFWwindow* window) {
    if (window)
        _glfw.wgl.MakeCurrent(window->context.wgl.dc, window->context.wgl.handle);
    else
        _glfw.wgl.MakeCurrent(NULL, NULL);
    _glfwPlatformSetTls(&_glfw.contextSlot, window);
}

static void swapBuffersWGL(_GLFWwindow* window) {
    // Under DWM (always active on modern Windows) a vsynced GL SwapBuffers
    // adds a driver frame-queue's worth of latency, which shows up as sluggish
    // typing compared to flip-model apps like Windows Terminal. Composition
    // makes tearing impossible anyway, so swap immediately and pace with
    // DwmFlush() instead (the strategy upstream GLFW uses): the present goes
    // out right away and we wait at most one composition pass.
    if (window->context.wgl.interval > 0) DwmFlush();
    // With acrylic the window has no redirection surface, so SwapBuffers has
    // nowhere to present. The frame was drawn straight into the composition
    // swapchain instead, and that is what gets presented.
    if (_glfwWin32AcrylicActive(window)) _glfwWin32AcrylicPresent(window);
    else SwapBuffers(window->context.wgl.dc);
}

static void swapIntervalWGL(int interval) {
    _GLFWwindow* window = _glfwPlatformGetTls(&_glfw.contextSlot);
    if (window) window->context.wgl.interval = interval;
    // Real GL vsync stays off; pacing happens via DwmFlush in swapBuffersWGL.
    if (_glfw.wgl.EXT_swap_control) _glfw.wgl.SwapIntervalEXT(0);
}

static int extensionSupportedWGL(const char* extension) {
    const char* extensions = NULL;
    if (_glfw.wgl.GetExtensionsStringARB)
        extensions = _glfw.wgl.GetExtensionsStringARB(_glfw.wgl.GetCurrentDC());
    else if (_glfw.wgl.GetExtensionsStringEXT)
        extensions = _glfw.wgl.GetExtensionsStringEXT();
    if (!extensions) return false;
    return _glfwStringInExtensionString(extension, extensions);
}

static GLFWglproc getProcAddressWGL(const char* procname) {
    const GLFWglproc proc = (GLFWglproc) _glfw.wgl.GetProcAddress(procname);
    if (proc) return proc;
    return (GLFWglproc) GetProcAddress(_glfw.wgl.instance, procname);
}

static void destroyContextWGL(_GLFWwindow* window) {
    if (window->context.wgl.handle) {
        _glfw.wgl.DeleteContext(window->context.wgl.handle);
        window->context.wgl.handle = NULL;
    }
}

// Bootstrap a hidden window + legacy context so the ARB/EXT entry points can be
// resolved. wglGetProcAddress only returns non-NULL with a context current.
static void loadWGLExtensions(void) {
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;

    HWND dummy = CreateWindowExW(0, _GLFW_WNDCLASSNAME, L"kitty-wgl-probe",
                                 WS_OVERLAPPED, 0, 0, 1, 1, NULL, NULL, _glfw.win32.instance, NULL);
    if (!dummy) return;
    HDC dc = GetDC(dummy);
    int pf = ChoosePixelFormat(dc, &pfd);
    SetPixelFormat(dc, pf, &pfd);
    HGLRC rc = _glfw.wgl.CreateContext(dc);
    HDC pdc = _glfw.wgl.GetCurrentDC();
    HGLRC prc = _glfw.wgl.GetCurrentContext();
    if (rc && _glfw.wgl.MakeCurrent(dc, rc)) {
        _glfw.wgl.GetExtensionsStringEXT = (PFNWGLGETEXTENSIONSSTRINGEXTPROC) _glfw.wgl.GetProcAddress("wglGetExtensionsStringEXT");
        _glfw.wgl.GetExtensionsStringARB = (PFNWGLGETEXTENSIONSSTRINGARBPROC) _glfw.wgl.GetProcAddress("wglGetExtensionsStringARB");
        _glfw.wgl.CreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC) _glfw.wgl.GetProcAddress("wglCreateContextAttribsARB");
        _glfw.wgl.SwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC) _glfw.wgl.GetProcAddress("wglSwapIntervalEXT");
        _glfw.wgl.GetPixelFormatAttribivARB = (PFNWGLGETPIXELFORMATATTRIBIVARBPROC) _glfw.wgl.GetProcAddress("wglGetPixelFormatAttribivARB");

        _glfw.wgl.ARB_create_context = extensionSupportedWGL("WGL_ARB_create_context");
        _glfw.wgl.ARB_create_context_profile = extensionSupportedWGL("WGL_ARB_create_context_profile");
        _glfw.wgl.EXT_swap_control = extensionSupportedWGL("WGL_EXT_swap_control");
        _glfw.wgl.ARB_pixel_format = extensionSupportedWGL("WGL_ARB_pixel_format");
        _glfw.wgl.ARB_framebuffer_sRGB = extensionSupportedWGL("WGL_ARB_framebuffer_sRGB");
    }
    _glfw.wgl.MakeCurrent(pdc, prc);
    if (rc) _glfw.wgl.DeleteContext(rc);
    ReleaseDC(dummy, dc);
    DestroyWindow(dummy);
}

bool _glfwInitWGL(void) {
    if (_glfw.wgl.instance) return true;
    _glfw.wgl.instance = LoadLibraryA("opengl32.dll");
    if (!_glfw.wgl.instance) {
        _glfwInputError(GLFW_PLATFORM_ERROR, "WGL: Failed to load opengl32.dll");
        return false;
    }
    _glfw.wgl.CreateContext = (PFN_wglCreateContext) GetProcAddress(_glfw.wgl.instance, "wglCreateContext");
    _glfw.wgl.DeleteContext = (PFN_wglDeleteContext) GetProcAddress(_glfw.wgl.instance, "wglDeleteContext");
    _glfw.wgl.GetProcAddress = (PFN_wglGetProcAddress) GetProcAddress(_glfw.wgl.instance, "wglGetProcAddress");
    _glfw.wgl.GetCurrentDC = (PFN_wglGetCurrentDC) GetProcAddress(_glfw.wgl.instance, "wglGetCurrentDC");
    _glfw.wgl.GetCurrentContext = (PFN_wglGetCurrentContext) GetProcAddress(_glfw.wgl.instance, "wglGetCurrentContext");
    _glfw.wgl.MakeCurrent = (PFN_wglMakeCurrent) GetProcAddress(_glfw.wgl.instance, "wglMakeCurrent");
    _glfw.wgl.ShareLists = (PFN_wglShareLists) GetProcAddress(_glfw.wgl.instance, "wglShareLists");

    loadWGLExtensions();
    return true;
}

void _glfwTerminateWGL(void) {
    if (_glfw.wgl.instance) {
        FreeLibrary(_glfw.wgl.instance);
        _glfw.wgl.instance = NULL;
    }
}

static int choosePixelFormat(const _GLFWctxconfig* ctxconfig, const _GLFWfbconfig* fbconfig, HDC dc) {
    (void) ctxconfig;
    if (_glfw.wgl.ARB_pixel_format && _glfw.wgl.GetPixelFormatAttribivARB) {
        const int attribs[] = {
            WGL_DRAW_TO_WINDOW_ARB, TRUE,
            WGL_SUPPORT_OPENGL_ARB, TRUE,
            WGL_DOUBLE_BUFFER_ARB,  fbconfig->doublebuffer ? TRUE : FALSE,
            WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
            WGL_ACCELERATION_ARB,   WGL_FULL_ACCELERATION_ARB,
            WGL_RED_BITS_ARB,       fbconfig->redBits,
            WGL_GREEN_BITS_ARB,     fbconfig->greenBits,
            WGL_BLUE_BITS_ARB,      fbconfig->blueBits,
            WGL_ALPHA_BITS_ARB,     fbconfig->alphaBits,
            WGL_DEPTH_BITS_ARB,     fbconfig->depthBits,
            WGL_STENCIL_BITS_ARB,   fbconfig->stencilBits,
            0
        };
        // Fall back to the classic chooser; a full ARB enumeration is not needed
        // for the accelerated RGBA formats kitty requests.
        (void) attribs;
    }
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | (fbconfig->doublebuffer ? PFD_DOUBLEBUFFER : 0);
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = (BYTE)(fbconfig->redBits + fbconfig->greenBits + fbconfig->blueBits);
    pfd.cAlphaBits = (BYTE) fbconfig->alphaBits;
    pfd.cDepthBits = (BYTE) fbconfig->depthBits;
    pfd.cStencilBits = (BYTE) fbconfig->stencilBits;
    return ChoosePixelFormat(dc, &pfd);
}

bool _glfwCreateContextWGL(_GLFWwindow* window, const _GLFWctxconfig* ctxconfig, const _GLFWfbconfig* fbconfig) {
    HDC dc = GetDC(window->win32.handle);
    if (!dc) {
        _glfwInputError(GLFW_PLATFORM_ERROR, "WGL: Failed to retrieve DC for window");
        return false;
    }
    window->context.wgl.dc = dc;

    int pf = choosePixelFormat(ctxconfig, fbconfig, dc);
    if (!pf) {
        _glfwInputError(GLFW_FORMAT_UNAVAILABLE, "WGL: Failed to choose a pixel format");
        return false;
    }
    PIXELFORMATDESCRIPTOR pfd;
    DescribePixelFormat(dc, pf, sizeof(pfd), &pfd);
    if (!SetPixelFormat(dc, pf, &pfd)) {
        _glfwInputError(GLFW_PLATFORM_ERROR, "WGL: Failed to set the pixel format");
        return false;
    }

    HGLRC share = ctxconfig->share ? ctxconfig->share->context.wgl.handle : NULL;
    if (_glfw.wgl.ARB_create_context && ctxconfig->major >= 3) {
        int flags = 0;
        if (ctxconfig->forward) flags |= WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;
        const int attribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, ctxconfig->major,
            WGL_CONTEXT_MINOR_VERSION_ARB, ctxconfig->minor,
            WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            WGL_CONTEXT_FLAGS_ARB,         flags,
            0
        };
        window->context.wgl.handle = _glfw.wgl.CreateContextAttribsARB(dc, share, attribs);
    }
    if (!window->context.wgl.handle)
        window->context.wgl.handle = _glfw.wgl.CreateContext(dc);
    if (!window->context.wgl.handle) {
        _glfwInputError(GLFW_VERSION_UNAVAILABLE, "WGL: Failed to create OpenGL context");
        return false;
    }

    window->context.makeCurrent = makeContextCurrentWGL;
    window->context.swapBuffers = swapBuffersWGL;
    window->context.swapInterval = swapIntervalWGL;
    window->context.extensionSupported = extensionSupportedWGL;
    window->context.getProcAddress = getProcAddressWGL;
    window->context.destroy = destroyContextWGL;
    return true;
}
