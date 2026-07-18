/*
 * wincompat/termios.h — POSIX terminal control for the Windows port of kitty.
 *
 * On Windows the pseudoconsole (ConPTY) owns line discipline, so the tc*()
 * calls become no-ops and the struct/constants exist only so the Unix-side code
 * compiles. Values are conventional Linux ones; nothing here reads them yet.
 */
#pragma once
#include <sys/types.h>
#include <string.h>   /* memset for cfmakeraw/tcgetattr stubs */

typedef unsigned char  cc_t;
typedef unsigned int   speed_t;
typedef unsigned int   tcflag_t;

#define NCCS 32
struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_line;
    cc_t     c_cc[NCCS];
    speed_t  c_ispeed;
    speed_t  c_ospeed;
};

/* c_cc indices */
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6
#define VSTART 8
#define VSTOP 9
#define VSUSP 10
#define VREPRINT 12
#define VWERASE 14
#define VLNEXT 15

/* c_iflag */
#define IGNBRK 0x0001
#define BRKINT 0x0002
#define IGNPAR 0x0004
#define INPCK 0x0010
#define ISTRIP 0x0020
#define INLCR 0x0040
#define IGNCR 0x0080
#define ICRNL 0x0100
#define IXON 0x0400
#define IXANY 0x0800
#define IXOFF 0x1000
#define IUTF8 0x4000

/* c_oflag */
#define OPOST 0x0001
#define ONLCR 0x0004

/* c_cflag */
#define CSIZE 0x0030
#define CS8 0x0030
#define CREAD 0x0080
#define PARENB 0x0100
#define CLOCAL 0x0800

/* c_lflag */
#define ISIG 0x0001
#define ICANON 0x0002
#define ECHO 0x0008
#define ECHOE 0x0010
#define ECHOK 0x0020
#define ECHONL 0x0040
#define NOFLSH 0x0080
#define IEXTEN 0x8000

/* tcsetattr optional actions */
#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2

/* tcflush queue selectors */
#define TCIFLUSH 0
#define TCOFLUSH 1
#define TCIOFLUSH 2

/* a couple of baud constants for cfset*speed callers */
#define B0 0
#define B9600 9600
#define B38400 38400
#define B115200 115200

static inline __attribute__((unused)) int
tcgetattr(int fd, struct termios *t) { (void)fd; if (t) memset(t, 0, sizeof *t); return 0; }
static inline __attribute__((unused)) int
tcsetattr(int fd, int actions, const struct termios *t) { (void)fd; (void)actions; (void)t; return 0; }
static inline __attribute__((unused)) int
tcflush(int fd, int queue) { (void)fd; (void)queue; return 0; }
static inline __attribute__((unused)) int
tcdrain(int fd) { (void)fd; return 0; }
static inline __attribute__((unused)) speed_t
cfgetispeed(const struct termios *t) { return t ? t->c_ispeed : 0; }
static inline __attribute__((unused)) speed_t
cfgetospeed(const struct termios *t) { return t ? t->c_ospeed : 0; }
static inline __attribute__((unused)) int
cfsetispeed(struct termios *t, speed_t s) { if (t) t->c_ispeed = s; return 0; }
static inline __attribute__((unused)) int
cfsetospeed(struct termios *t, speed_t s) { if (t) t->c_ospeed = s; return 0; }
static inline __attribute__((unused)) void
cfmakeraw(struct termios *t) { (void)t; }
