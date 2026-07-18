/*
 * wincompat/sys/ioctl.h — the sliver of the Unix ioctl surface kitty uses on a
 * pty: window size (TIOCSWINSZ/TIOCGWINSZ) and controlling-tty (TIOCSCTTY).
 *
 * On Windows these map to the pseudoconsole: TIOCSWINSZ -> ResizePseudoConsole,
 * TIOCSCTTY is a no-op. ioctl() itself is stubbed for the compile stage.
 */
#pragma once
#include <sys/types.h>

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

/* Conventional Linux request values; nothing dispatches on them yet. */
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define TIOCSCTTY  0x540E
#define FIONBIO    0x5421
#define FIONREAD   0x541B

int ioctl(int fd, unsigned long request, ...);
