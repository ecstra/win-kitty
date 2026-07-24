/*
 * wincompat/win_prelude.h — force-included first on Windows (gcc -include).
 *
 * Bridges POSIX type names that kitty's C code expects to their MSVCRT/MinGW
 * equivalents, and defines the few POSIX types MinGW omits. Kept tiny and
 * header-only so it can sit ahead of every translation unit.
 */
#pragma once

/* Winsock2 MUST be included before <windows.h>, so pull the whole Win32 surface
 * in here at the very front of every translation unit. This also supplies
 * struct sockaddr / socklen_t / struct pollfd / POLL* used across kitty. */
#ifndef NOMINMAX
#define NOMINMAX   /* stop Win32 defining min()/max() macros that clash with kitty's functions */
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

/* Win32 pollutes the global namespace with generic names that kitty also uses.
 * Rename kitty's uses out of the way. These MUST come AFTER the Win32 headers
 * above, so Win32's own declarations keep their names and only kitty's are
 * rewritten. Add new lines here as the compile surfaces more collisions. */
#define mouse_event kitty_mouse_event
#define POINT       kitty_POINT
#define WORD        kitty_WORD
#define Ellipse     kitty_Ellipse
#undef hyper   /* rpcndr.h: #define hyper __int64 — kitty uses 'hyper' as a field name */
#undef small   /* rpcndr.h: #define small char   — keep 'small' usable as an identifier */

#include <locale.h>
#include <sys/types.h>   /* MinGW provides pid_t, off_t, ssize_t here */

/* POSIX xlocale name -> MSVCRT name (used by data-types.h get_c_locale). */
typedef _locale_t locale_t;

/* MinGW's <sys/types.h> omits these Unix id types. */
#ifndef _UID_T_DEFINED
#define _UID_T_DEFINED
typedef int uid_t;
typedef int gid_t;
#endif

/* MinGW spells the signal set _sigset_t and has no POSIX siginfo_t. Provide
 * minimal versions; real signal handling is stubbed on Windows (Stage 4). */
typedef unsigned long sigset_t;
union sigval { int sival_int; void *sival_ptr; };
typedef struct {
    int si_signo; int si_code; int si_pid; int si_status; int si_uid;
    void *si_addr; union sigval si_value;
} siginfo_t;

/* Windows has no Unix credentials; kitty uses these only for home-dir/logging.
 * Stub to 0 (there is a single interactive user). */
static inline __attribute__((unused)) uid_t geteuid(void) { return 0; }
static inline __attribute__((unused)) uid_t getuid(void)  { return 0; }
static inline __attribute__((unused)) gid_t getegid(void) { return 0; }
static inline __attribute__((unused)) gid_t getgid(void)  { return 0; }

/* POSIX advisory file locking (from <unistd.h>) — a no-op stub for the compile
 * stage; kitty only uses it for the single-instance lock file. */
#ifndef F_LOCK
#define F_ULOCK 0
#define F_LOCK  1
#define F_TLOCK 2
#define F_TEST  3
#endif
static inline __attribute__((unused)) int
lockf(int fd, int cmd, off_t len) { (void)fd; (void)cmd; (void)len; return 0; }

/* POSIX libc functions MinGW lacks. Declared for the compile stage; implemented
 * in wincompat.c later (aligned alloc, positioned read, secure RNG). */
int     posix_memalign(void **memptr, size_t alignment, size_t size);
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
void    arc4random_buf(void *buf, size_t n);
unsigned int arc4random(void);
int     pipe2(int pipefd[2], int flags);
int     ttyname_r(int fd, char *buf, size_t buflen);
char   *strndup(const char *s, size_t n);

/* Locale-aware strtod: MinGW spells it _strtod_l and locale_t == _locale_t. */
#define strtod_l _strtod_l
/* Max length of a ctermid() string; Windows has no controlling terminal path. */
#ifndef L_ctermid
#define L_ctermid 32
#endif

/* Normally emitted by kitty's code generation; placeholder for standalone
 * compilation. The real build (setup.py) defines the true version string. */
#ifndef XT_VERSION
#define XT_VERSION "0.48.0"
#endif
/* Also normally codegen'd (list of kittens wrapped into the binary). */
#ifndef WRAPPED_KITTENS
#define WRAPPED_KITTENS ""
#endif

/* No controlling terminal on Windows; ctermid yields an empty path. */
static inline __attribute__((unused)) char *
ctermid(char *s) { static char buf[L_ctermid]; char *p = s ? s : buf; p[0] = 0; return p; }

/* Secure memory wipe (kitty's disk-cache) — Win32 provides SecureZeroMemory. */
#ifndef explicit_bzero
#define explicit_bzero(ptr, len) SecureZeroMemory((ptr), (len))
#endif

/* Signal-set ops on our unsigned-long sigset_t (masks fed to stubbed signal code). */
static inline __attribute__((unused)) int sigemptyset(sigset_t *s) { if (s) *s = 0; return 0; }
static inline __attribute__((unused)) int sigfillset(sigset_t *s) { if (s) *s = ~0UL; return 0; }
static inline __attribute__((unused)) int sigaddset(sigset_t *s, int n) { if (s) *s |= 1UL << (n & 31); return 0; }
static inline __attribute__((unused)) int sigdelset(sigset_t *s, int n) { if (s) *s &= ~(1UL << (n & 31)); return 0; }
static inline __attribute__((unused)) int sigismember(const sigset_t *s, int n) { return s ? (int)((*s >> (n & 31)) & 1) : 0; }

/* POSIX xlocale mapped onto MSVCRT's _*_locale. */
#ifndef LC_ALL_MASK
#define LC_CTYPE_MASK 1
#define LC_NUMERIC_MASK 2
#define LC_TIME_MASK 4
#define LC_COLLATE_MASK 8
#define LC_MONETARY_MASK 16
#define LC_MESSAGES_MASK 32
#define LC_ALL_MASK 0x3f
#endif
#ifndef LC_GLOBAL_LOCALE
#define LC_GLOBAL_LOCALE ((locale_t)-1)
#endif
static inline __attribute__((unused)) locale_t
newlocale(int mask, const char *locale, locale_t base) { (void)mask; (void)base; return _create_locale(LC_ALL, locale ? locale : "C"); }
static inline __attribute__((unused)) void
freelocale(locale_t loc) { if (loc) _free_locale(loc); }
static inline __attribute__((unused)) locale_t
uselocale(locale_t loc) { (void)loc; return (locale_t)0; }

/* mkostemp = mkstemp + open flags; MinGW has mkstemp, so drop the flags. */
#ifndef mkostemp
#define mkostemp(tmpl, flags) mkstemp(tmpl)
#endif

/* No sigqueue on Windows — child-monitor.c falls back to kill(). Both kill and
 * the pipe helpers are implemented in wincompat.c (Stage 4). */
#define NO_SIGQUEUE 1
int kill(pid_t pid, int sig);
int pipe2(int pipefd[2], int flags);
int socketpair(int domain, int type, int protocol, int sv[2]);
pid_t waitpid(pid_t pid, int *status, int options);
int ioctl(int fd, unsigned long request, ...);
int ttyname_r(int fd, char *buf, size_t buflen);
pid_t getpgid(pid_t pid);
int killpg(pid_t pgrp, int sig);
int getpeereid(int socket, uid_t *euid, gid_t *egid);
#ifndef SHUT_RD
#define SHUT_RD   0   /* winsock SD_RECEIVE */
#define SHUT_WR   1   /* winsock SD_SEND */
#define SHUT_RDWR 2   /* winsock SD_BOTH */
#endif

/* POSIX realpath -> MSVCRT _fullpath, which allocates when the buffer is NULL. */
#ifndef realpath
#define realpath(path, resolved) _fullpath((resolved), (path), 260)
#endif

/* Launcher POSIX calls MinGW lacks; implemented in wincompat.c. fork/setsid
 * have no Windows equivalent and only serve the daemonize path, which a normal
 * foreground launch never takes. */
int pipe(int pipefd[2]);
int fork(void);
int setsid(void);
int unsetenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int setlinebuf(void *stream);

/* Symlink and dir ops used only on dnd.c's drag-drop file path (stubbed). */
#define lstat stat
ssize_t readlink(const char *path, char *buf, size_t bufsiz);
int symlinkat(const char *target, int newdirfd, const char *linkpath);
int mkdirat(int dirfd, const char *path, int mode);
