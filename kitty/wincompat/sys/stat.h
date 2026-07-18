/*
 * wincompat/sys/stat.h — pulls MinGW's real <sys/stat.h>, then remaps POSIX
 * mkdir(path, mode) onto MinGW's single-argument _mkdir(path).
 *
 * Found ahead of the system header via -I kitty/wincompat; include_next
 * continues to the real one. The macro is defined AFTER the real declarations
 * so it can't corrupt MinGW's own prototypes.
 */
#pragma once
#include_next <sys/stat.h>
#include <direct.h>   /* _mkdir */

#undef mkdir
#define mkdir(path, mode) _mkdir(path)
