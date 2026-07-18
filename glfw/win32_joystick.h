//========================================================================
// GLFW 3.4 Win32 port for kitty - joystick (not supported, minimal state)
//========================================================================
#pragma once

typedef struct _GLFWjoystickWin32 {
    int unused;
} _GLFWjoystickWin32;

typedef struct _GLFWlibraryJoystickWin32 {
    int unused;
} _GLFWlibraryJoystickWin32;

#define _GLFW_PLATFORM_JOYSTICK_STATE         _GLFWjoystickWin32        win32
#define _GLFW_PLATFORM_LIBRARY_JOYSTICK_STATE _GLFWlibraryJoystickWin32 win32js;
#define _GLFW_PLATFORM_MAPPING_NAME "Windows"
