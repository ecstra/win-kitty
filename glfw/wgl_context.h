//========================================================================
// GLFW 3.4 Win32 port for kitty - WGL context
//========================================================================
#pragma once

/* WGL extension entry points, loaded at runtime in wgl_context.c. */
typedef HGLRC (WINAPI *PFN_wglCreateContext)(HDC);
typedef BOOL  (WINAPI *PFN_wglDeleteContext)(HGLRC);
typedef PROC  (WINAPI *PFN_wglGetProcAddress)(LPCSTR);
typedef HDC   (WINAPI *PFN_wglGetCurrentDC)(void);
typedef HGLRC (WINAPI *PFN_wglGetCurrentContext)(void);
typedef BOOL  (WINAPI *PFN_wglMakeCurrent)(HDC, HGLRC);
typedef BOOL  (WINAPI *PFN_wglShareLists)(HGLRC, HGLRC);

typedef BOOL  (WINAPI *PFNWGLSWAPINTERVALEXTPROC)(int);
typedef BOOL  (WINAPI *PFNWGLGETPIXELFORMATATTRIBIVARBPROC)(HDC, int, int, UINT, const int*, int*);
typedef const char* (WINAPI *PFNWGLGETEXTENSIONSSTRINGEXTPROC)(void);
typedef const char* (WINAPI *PFNWGLGETEXTENSIONSSTRINGARBPROC)(HDC);
typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);

/* Per-context state stored on each _GLFWwindow. */
typedef struct _GLFWcontextWGL {
    HDC   dc;
    HGLRC handle;
    int   interval;
} _GLFWcontextWGL;

/* Process-wide WGL state stored on _GLFWlibrary. */
typedef struct _GLFWlibraryWGL {
    HINSTANCE instance;
    PFN_wglCreateContext      CreateContext;
    PFN_wglDeleteContext      DeleteContext;
    PFN_wglGetProcAddress     GetProcAddress;
    PFN_wglGetCurrentDC       GetCurrentDC;
    PFN_wglGetCurrentContext  GetCurrentContext;
    PFN_wglMakeCurrent        MakeCurrent;
    PFN_wglShareLists         ShareLists;

    PFNWGLSWAPINTERVALEXTPROC            SwapIntervalEXT;
    PFNWGLGETPIXELFORMATATTRIBIVARBPROC GetPixelFormatAttribivARB;
    PFNWGLGETEXTENSIONSSTRINGEXTPROC    GetExtensionsStringEXT;
    PFNWGLGETEXTENSIONSSTRINGARBPROC    GetExtensionsStringARB;
    PFNWGLCREATECONTEXTATTRIBSARBPROC   CreateContextAttribsARB;

    bool EXT_swap_control;
    bool ARB_multisample;
    bool ARB_framebuffer_sRGB;
    bool EXT_framebuffer_sRGB;
    bool ARB_pixel_format;
    bool ARB_create_context;
    bool ARB_create_context_profile;
    bool EXT_create_context_es2_profile;
    bool ARB_create_context_robustness;
    bool ARB_create_context_no_error;
    bool ARB_context_flush_control;
} _GLFWlibraryWGL;

#define _GLFW_PLATFORM_CONTEXT_STATE         _GLFWcontextWGL wgl;
#define _GLFW_PLATFORM_LIBRARY_CONTEXT_STATE _GLFWlibraryWGL wgl;

bool _glfwInitWGL(void);
void _glfwTerminateWGL(void);
bool _glfwCreateContextWGL(_GLFWwindow* window,
                           const _GLFWctxconfig* ctxconfig,
                           const _GLFWfbconfig* fbconfig);
