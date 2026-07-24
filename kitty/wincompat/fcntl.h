/*
 * wincompat/fcntl.h — pulls MinGW's real fcntl.h (open + O_ flags) and adds the
 * POSIX fcntl(), F_GETFD/F_SETFD, FD_CLOEXEC and openat that MinGW omits.
 *
 * fcntl() is stubbed for the compile stage (the real fd-flag handling on
 * Windows is SetHandleInformation / _open noinherit, wired up later).
 */
#pragma once
#include_next <fcntl.h>

#ifndef F_GETFD
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#endif

#ifndef FD_CLOEXEC
#define FD_CLOEXEC 1
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC _O_NOINHERIT   /* MinGW's "don't inherit into children" flag */
#endif
#ifndef O_NONBLOCK
/* A private high bit (clear of the CRT _O_* flags, the highest of which is
 * _O_U8TEXT = 0x40000). pipe2() turns this into PIPE_NOWAIT on the pipe ends so
 * self-pipe drains via read() do not block the event loop. */
#define O_NONBLOCK 0x01000000
#endif

/* Flags with no Windows equivalent — harmless no-ops so open() calls compile. */
#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif

/* Stubs — enough to compile; real behaviour comes in a later stage. */
static inline __attribute__((unused)) int
fcntl(int fd, int cmd, ...) { (void)fd; (void)cmd; return 0; }

static inline __attribute__((unused)) int
openat(int dirfd, const char *path, int flags, ...) {
    (void)dirfd;                 /* approximated as AT_FDCWD for now */
    return open(path, flags);
}
