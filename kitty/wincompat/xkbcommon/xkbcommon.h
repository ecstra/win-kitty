/*
 * wincompat/xkbcommon/xkbcommon.h — minimal shim.
 *
 * kitty's keys.c (on non-Apple platforms) pulls in xkbcommon only for two
 * XF86 keysym constants used in a switch. Real keyboard handling on Windows is
 * Win32 virtual-key based (the remainder of Stage 5); this just lets keys.c
 * compile without the (unavailable-on-mingw) xkbcommon library.
 */
#pragma once

#ifndef XKB_KEY_XF86WakeUp
#define XKB_KEY_XF86WakeUp 0x1008ff2b
#endif
#ifndef XKB_KEY_XF86Fn
#define XKB_KEY_XF86Fn 0x1008ff2d
#endif
