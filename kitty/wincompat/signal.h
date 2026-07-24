/*
 * wincompat/signal.h — pulls MinGW's real signal.h (signal/raise + the standard
 * SIG* numbers) and adds the POSIX signal surface kitty references: the extra
 * signal numbers, struct sigaction, and sigaction/sigprocmask prototypes.
 *
 * Included by nearly every file (Python.h pulls <signal.h>), so it stays purely
 * ADDITIVE — guarded constants, one struct, prototypes only (no inline bodies
 * that could collide with winpthreads). None of these fire on Windows; the real
 * equivalents (SIGCHLD -> process-exit wait, SIGWINCH -> resize) live in the
 * Stage-4 event loop, which implements these prototypes in wincompat.c.
 */
#pragma once
#include_next <signal.h>

#ifndef SIGHUP
#define SIGHUP    1
#endif
#ifndef SIGQUIT
#define SIGQUIT   3
#endif
#ifndef SIGKILL
#define SIGKILL   9
#endif
#ifndef SIGUSR1
#define SIGUSR1  10
#endif
#ifndef SIGUSR2
#define SIGUSR2  12
#endif
#ifndef SIGPIPE
#define SIGPIPE  13
#endif
#ifndef SIGCHLD
#define SIGCHLD  17
#endif
#ifndef SIGCONT
#define SIGCONT  18
#endif
#ifndef SIGSTOP
#define SIGSTOP  19
#endif
#ifndef SIGTSTP
#define SIGTSTP  20
#endif
#ifndef SIGTTIN
#define SIGTTIN  21
#endif
#ifndef SIGTTOU
#define SIGTTOU  22
#endif
#ifndef SIGWINCH
#define SIGWINCH 28
#endif

#ifndef SIG_BLOCK
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#endif
#ifndef SA_RESTART
#define SA_RESTART 0x10000000
#define SA_SIGINFO 0x00000004
#endif

struct sigaction {
    void (*sa_handler)(int);
    void (*sa_sigaction)(int, siginfo_t *, void *);
    sigset_t sa_mask;
    int      sa_flags;
};

/* Implemented in wincompat.c (Stage 4). Prototypes only, to avoid colliding
 * with winpthreads' own pthread_sigmask etc. */
int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
