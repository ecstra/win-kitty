//========================================================================
// GLFW 3.4 Win32 port for kitty - joystick (not supported)
//========================================================================

#include "internal.h"

// kitty does not use joysticks. The platform joystick entry points live in
// win32_init.c as no-ops; this translation unit exists only to satisfy the
// source list in source-info.json.
