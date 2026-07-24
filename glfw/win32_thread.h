//========================================================================
// GLFW 3.4 Win32 port for kitty - threads (TLS + mutex)
//========================================================================
#pragma once

typedef struct _GLFWtlsWin32 {
    bool  allocated;
    DWORD index;
} _GLFWtlsWin32;

typedef struct _GLFWmutexWin32 {
    bool             allocated;
    CRITICAL_SECTION section;
} _GLFWmutexWin32;

#define _GLFW_PLATFORM_TLS_STATE   _GLFWtlsWin32   win32
#define _GLFW_PLATFORM_MUTEX_STATE _GLFWmutexWin32 win32
