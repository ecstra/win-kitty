/*
 * wincompat.c — implementations of the POSIX functions the Windows port of
 * kitty relies on. Prototypes live in win_prelude.h and the sys/* shims.
 *
 * The centerpiece is poll(): kitty's event loop waits on a mix of ConPTY pipe
 * fds (bridged via _open_osfhandle) and winsock sockets. Windows has no single
 * readiness primitive for both, so poll() checks pipes with PeekNamedPipe and
 * sockets with WSAPoll, spinning at a short interval until ready or timeout.
 */
#define _WIN32_WINNT 0x0A00
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <stdio.h>

#include "win_prelude.h"  /* uid_t/gid_t + the prototypes this file implements */
#include "poll.h"         /* struct pollfd, nfds_t */

/* ---- keep _get_osfhandle from aborting on socket fds ---------------------- */
static void
noop_invalid_parameter(const wchar_t *a, const wchar_t *b, const wchar_t *c,
                       unsigned d, uintptr_t e) { (void)a;(void)b;(void)c;(void)d;(void)e; }

static HANDLE
handle_for_fd(int fd) {
    static int installed = 0;
    if (!installed) { _set_invalid_parameter_handler(noop_invalid_parameter); installed = 1; }
    intptr_t h = _get_osfhandle(fd);
    return (h == -1 || h == -2) ? INVALID_HANDLE_VALUE : (HANDLE)h;
}

/* ---- poll() --------------------------------------------------------------- */
/* Whatever this sleeps between samples is pure latency on the keystroke path:
 * the shell's echo is sitting in the pipe, already readable, for the whole
 * interval. It used to be a flat Sleep(5) -- which at Windows' default 15.625
 * ms timer granularity really sleeps ~15.8 ms, so a keystroke waited an average
 * 8 ms just to be noticed. glfw now raises the process timer resolution to 1 ms
 * (win32_init.c), which makes these sleeps worth what they say.
 *
 * On top of that, back off adaptively rather than at one fixed interval. Just
 * after activity the next byte is usually microseconds away, so re-sample in a
 * yielding spin before paying for a sleep at all; once the terminal has been
 * quiet for a while, drop back to the cheap interval so an idle kitty is not a
 * spinning CPU. The proper fix is overlapped reads on the pipes, which would
 * remove the sampling entirely, but that means restructuring the child monitor.
 *
 * Timeout accounting uses QueryPerformanceCounter, not GetTickCount64: the tick
 * count advances in whole timer ticks, so it cannot express a 3 ms timeout. */
#define POLL_SPIN_MS   0.3     /* yield-spin this long before the first sleep */
#define POLL_HOT_MS    250.0   /* activity newer than this: spin, then Sleep(1) */
#define POLL_WARM_MS   2000.0  /* ... newer than this: Sleep(1), no spin       */

static LONGLONG
qpc_freq(void) {
    static LONGLONG f;
    if (!f) { LARGE_INTEGER x; QueryPerformanceFrequency(&x); f = x.QuadPart; }
    return f;
}

static LONGLONG
qpc_now(void) { LARGE_INTEGER x; QueryPerformanceCounter(&x); return x.QuadPart; }

static double
qpc_ms(LONGLONG from, LONGLONG to) { return (double)(to - from) * 1000.0 / (double)qpc_freq(); }

int
poll(struct pollfd *fds, nfds_t nfds, int timeout_ms) {
    /* Thread local: one thread going quiet should not make another one sleepy. */
    static __thread LONGLONG last_ready_at;
    const LONGLONG start = qpc_now();
    LONGLONG spin_until = 0;
    int spin_window_set = 0;
    for (;;) {
        int ready = 0;
        WSAPOLLFD spoll[64];
        int sidx[64], sn = 0;
        for (nfds_t i = 0; i < nfds; i++) {
            fds[i].revents = 0;
            if (fds[i].fd < 0) continue;
            HANDLE h = handle_for_fd((int)fds[i].fd);
            if (h == INVALID_HANDLE_VALUE) {                 /* a socket */
                if (sn < 64) { spoll[sn].fd = (SOCKET)fds[i].fd; spoll[sn].events = fds[i].events; spoll[sn].revents = 0; sidx[sn] = (int)i; sn++; }
            } else {                                          /* a pipe/file */
                if (fds[i].events & POLLIN) {
                    DWORD avail = 0;
                    if (PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) {
                        if (avail > 0) { fds[i].revents |= POLLIN; ready++; }
                    } else {
                        DWORD e = GetLastError();
                        if (e == ERROR_BROKEN_PIPE || e == ERROR_PIPE_NOT_CONNECTED) { fds[i].revents |= POLLHUP; ready++; }
                        else { fds[i].revents |= POLLIN; ready++; }  /* not a pipe (regular file): treat as readable */
                    }
                }
                if (fds[i].events & POLLOUT) { fds[i].revents |= POLLOUT; ready++; }  /* assume writable */
            }
        }
        if (sn > 0) {
            int r = WSAPoll(spoll, sn, 0);
            if (r > 0) for (int k = 0; k < sn; k++) if (spoll[k].revents) { fds[sidx[k]].revents = spoll[k].revents; ready++; }
        }
        if (ready > 0) { last_ready_at = qpc_now(); return ready; }
        if (timeout_ms == 0) return 0;
        const LONGLONG now = qpc_now();
        if (timeout_ms > 0 && qpc_ms(start, now) >= (double)timeout_ms) return 0;
        const double idle_ms = last_ready_at ? qpc_ms(last_ready_at, now) : POLL_WARM_MS;
        if (idle_ms < POLL_HOT_MS) {
            if (!spin_window_set) {
                spin_until = now + (LONGLONG)((double)qpc_freq() * POLL_SPIN_MS / 1000.0);
                spin_window_set = 1;
            }
            if (now < spin_until) { SwitchToThread(); continue; }
            Sleep(1);
        } else Sleep(idle_ms < POLL_WARM_MS ? 1 : 5);
    }
}

/* ---- pipes / sockets ------------------------------------------------------ */
int
pipe2(int fds[2], int flags) {
    int f = _O_BINARY | ((flags & _O_NOINHERIT) ? _O_NOINHERIT : 0);
    if (_pipe(fds, 65536, f) != 0) return -1;
    if (flags & O_NONBLOCK) {
        /* Put both ends in PIPE_NOWAIT so read() on an empty pipe returns 0
         * rather than blocking. This keeps drain_fd() (read until empty) from
         * wedging the child-monitor thread on the wakeup/signal self-pipes. */
        for (int i = 0; i < 2; i++) {
            HANDLE h = (HANDLE)_get_osfhandle(fds[i]);
            if (h != INVALID_HANDLE_VALUE && h != (HANDLE)(intptr_t)-2) {
                DWORD mode = PIPE_NOWAIT;
                SetNamedPipeHandleState(h, &mode, NULL, NULL);
            }
        }
    }
    return 0;
}

int pipe(int fds[2]) { return _pipe(fds, 65536, _O_BINARY); }
ssize_t readlink(const char *path, char *buf, size_t bufsiz) { (void) path; (void) buf; (void) bufsiz; errno = ENOSYS; return -1; }
int symlinkat(const char *target, int newdirfd, const char *linkpath) { (void) target; (void) newdirfd; (void) linkpath; errno = ENOSYS; return -1; }
int mkdirat(int dirfd, const char *path, int mode) { (void) dirfd; (void) mode; return CreateDirectoryA(path, NULL) ? 0 : -1; }
int fork(void) { return -1; }
int setsid(void) { return -1; }
int setlinebuf(void *stream) { return setvbuf((FILE*) stream, NULL, _IOLBF, 0); }
int unsetenv(const char *name) {
    char buf[512];
    snprintf(buf, sizeof buf, "%s=", name);
    return _putenv(buf);
}
int setenv(const char *name, const char *value, int overwrite) {
    (void) overwrite;
    char *buf = malloc(strlen(name) + strlen(value) + 2);
    if (!buf) return -1;
    sprintf(buf, "%s=%s", name, value);
    int rc = _putenv(buf);   /* _putenv copies, so the buffer can be freed */
    free(buf);
    return rc;
}

/* A loopback socketpair (Windows has no AF_UNIX socketpair). */
int
socketpair(int domain, int type, int protocol, int sv[2]) {
    (void)domain; (void)protocol;
    SOCKET listener = socket(AF_INET, type, 0);
    if (listener == INVALID_SOCKET) return -1;
    struct sockaddr_in addr; memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); addr.sin_port = 0;
    int len = sizeof addr;
    int rc = -1;
    SOCKET c = INVALID_SOCKET, s = INVALID_SOCKET;
    if (bind(listener, (struct sockaddr*)&addr, sizeof addr) == 0 &&
        listen(listener, 1) == 0 &&
        getsockname(listener, (struct sockaddr*)&addr, &len) == 0) {
        c = socket(AF_INET, type, 0);
        if (c != INVALID_SOCKET && connect(c, (struct sockaddr*)&addr, len) == 0) {
            s = accept(listener, NULL, NULL);
            if (s != INVALID_SOCKET) { sv[0] = (int)c; sv[1] = (int)s; rc = 0; }
        }
    }
    closesocket(listener);
    if (rc != 0) { if (c != INVALID_SOCKET) closesocket(c); if (s != INVALID_SOCKET) closesocket(s); }
    return rc;
}

/* ---- positioned / partial I/O -------------------------------------------- */
ssize_t
pread(int fd, void *buf, size_t count, off_t offset) {
    __int64 cur = _lseeki64(fd, 0, SEEK_CUR);
    if (_lseeki64(fd, offset, SEEK_SET) < 0) return -1;
    int n = _read(fd, buf, (unsigned)count);
    _lseeki64(fd, cur, SEEK_SET);
    return n;
}
ssize_t
pwrite(int fd, const void *buf, size_t count, off_t offset) {
    __int64 cur = _lseeki64(fd, 0, SEEK_CUR);
    if (_lseeki64(fd, offset, SEEK_SET) < 0) return -1;
    int n = _write(fd, buf, (unsigned)count);
    _lseeki64(fd, cur, SEEK_SET);
    return n;
}

/* ---- memory --------------------------------------------------------------- */
char *
strndup(const char *s, size_t n) {
    size_t len = 0; while (len < n && s[len]) len++;
    char *r = (char*)malloc(len + 1);
    if (r) { memcpy(r, s, len); r[len] = 0; }
    return r;
}
int
posix_memalign(void **memptr, size_t alignment, size_t size) {
    (void)alignment;   /* malloc is 16-byte aligned on x64; free()-compatible */
    void *p = malloc(size);
    if (!p) return 12; /* ENOMEM */
    *memptr = p; return 0;
}

/* ---- randomness (RtlGenRandom / SystemFunction036) ------------------------ */
BOOLEAN NTAPI SystemFunction036(PVOID, ULONG);
void
arc4random_buf(void *buf, size_t n) {
    if (!SystemFunction036(buf, (ULONG)n)) {
        unsigned char *p = (unsigned char*)buf;
        for (size_t i = 0; i < n; i++) p[i] = (unsigned char)(rand() & 0xff);
    }
}
unsigned int
arc4random(void) { unsigned int v; arc4random_buf(&v, sizeof v); return v; }

/* ---- signals (no real Unix signals on Windows) ---------------------------- */
int sigaction(int sig, const void *act, void *old) { (void)sig;(void)act;(void)old; return 0; }
int sigprocmask(int how, const void *set, void *old) { (void)how;(void)set;(void)old; return 0; }

/* ---- process control ------------------------------------------------------ */
int
kill(pid_t pid, int sig) {
    if (sig == 0) return 0;                       /* existence check: assume alive */
    HANDLE p = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
    if (!p) { errno = ESRCH; return -1; }
    BOOL ok = TerminateProcess(p, 128 + sig);
    DWORD err = ok ? 0 : GetLastError();
    CloseHandle(p);
    if (!ok) {
        /* Without this errno keeps whatever it held, and callers that print it
         * report "No error". TerminateProcess answers ERROR_ACCESS_DENIED for a
         * process that has already exited, which is ESRCH as far as a caller
         * asking "is it gone" is concerned, and is not worth reporting.
         *
         * Note this kills one process, not a tree. Children are held in a job
         * object instead, see create_kill_on_close_job in kitty/child.c. */
        errno = (err == ERROR_ACCESS_DENIED) ? ESRCH : EPERM;
        return -1;
    }
    return 0;
}
int killpg(pid_t pgrp, int sig) { return kill(pgrp, sig); }
pid_t getpgid(pid_t pid) { return pid; }
int getpeereid(int socket, uid_t *euid, gid_t *egid) { (void)socket; if (euid) *euid = 0; if (egid) *egid = 0; return 0; }

pid_t
waitpid(pid_t pid, int *status, int options) {
    HANDLE p = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, (DWORD)pid);
    if (!p) { errno = ECHILD; return -1; }
    DWORD wait = (options & 1 /*WNOHANG*/) ? 0 : INFINITE;
    DWORD r = WaitForSingleObject(p, wait);
    pid_t ret = 0;
    if (r == WAIT_OBJECT_0) { DWORD code = 0; GetExitCodeProcess(p, &code); if (status) *status = (int)(code & 0xff); ret = pid; }
    else if (r == WAIT_TIMEOUT) ret = 0;
    else ret = -1;
    CloseHandle(p);
    return ret;
}
pid_t wait(int *status) { return waitpid(-1, status, 0); }

/* ---- terminal / misc stubs ------------------------------------------------ */
int ioctl(int fd, unsigned long request, ...) { (void)fd;(void)request; return 0; }
int ttyname_r(int fd, char *buf, size_t buflen) { (void)fd; if (buf && buflen) snprintf(buf, buflen, "CON"); return 0; }

#include "pwd.h"
struct passwd *
getpwuid(uid_t uid) {
    static struct passwd pw;
    (void)uid;
    static char home[512];
    DWORD n = GetEnvironmentVariableA("USERPROFILE", home, sizeof home);
    pw.pw_name = "user"; pw.pw_passwd = "x"; pw.pw_uid = 0; pw.pw_gid = 0;
    pw.pw_gecos = "user"; pw.pw_dir = (n && n < sizeof home) ? home : "C:\\"; pw.pw_shell = "cmd.exe";
    return &pw;
}
struct passwd *getpwnam(const char *name) { (void)name; return getpwuid(0); }

/* ---- mmap: anonymous scratch buffers + read-only file mappings ------------ */
#include "sys/mman.h"
void *
mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    (void)addr;
    if (flags & MAP_ANONYMOUS) {
        DWORD protect = (prot & PROT_WRITE) ? PAGE_READWRITE : PAGE_READONLY;
        void *p = VirtualAlloc(NULL, length, MEM_COMMIT | MEM_RESERVE, protect);
        return p ? p : MAP_FAILED;
    }
    /* File-backed mapping (the graphics protocol reads image files this way). The
     * offset is 0 for those; a non-zero offset must be a multiple of the
     * allocation granularity, which MapViewOfFile enforces. */
    HANDLE fh = (HANDLE)_get_osfhandle(fd);
    if (fh == INVALID_HANDLE_VALUE || fh == (HANDLE)(intptr_t)-2) return MAP_FAILED;
    DWORD page = (prot & PROT_WRITE) ? PAGE_READWRITE : PAGE_READONLY;
    DWORD access = (prot & PROT_WRITE) ? FILE_MAP_WRITE : FILE_MAP_READ;
    HANDLE mh = CreateFileMappingW(fh, NULL, page, 0, 0, NULL);
    if (!mh) return MAP_FAILED;
    void *p = MapViewOfFile(mh, access, (DWORD)((ULONGLONG)offset >> 32), (DWORD)((ULONGLONG)offset & 0xFFFFFFFFu), length);
    CloseHandle(mh);  /* the view keeps the mapping alive */
    return p ? p : MAP_FAILED;
}
int munmap(void *addr, size_t length) {
    (void)length;
    /* File view (MapViewOfFile) vs anonymous (VirtualAlloc) - try the view first. */
    if (UnmapViewOfFile(addr)) return 0;
    return VirtualFree(addr, 0, MEM_RELEASE) ? 0 : -1;
}
int msync(void *addr, size_t length, int flags) { (void)addr;(void)length;(void)flags; return 0; }
int mlock(const void *addr, size_t len) { (void)addr;(void)len; return 0; }
int munlock(const void *addr, size_t len) { (void)addr;(void)len; return 0; }
int shm_open(const char *name, int oflag, int mode) { (void)name;(void)oflag;(void)mode; errno = ENOSYS; return -1; }
int shm_unlink(const char *name) { (void)name; return 0; }
