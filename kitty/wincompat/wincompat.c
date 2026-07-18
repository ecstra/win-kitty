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
int
poll(struct pollfd *fds, nfds_t nfds, int timeout_ms) {
    ULONGLONG start = GetTickCount64();
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
        if (ready > 0) return ready;
        if (timeout_ms == 0) return 0;
        if (timeout_ms > 0 && (GetTickCount64() - start) >= (ULONGLONG)timeout_ms) return 0;
        Sleep(5);
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
    CloseHandle(p);
    return ok ? 0 : -1;
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

/* ---- mmap: anonymous only (kitty uses it for scratch buffers) ------------- */
#include "sys/mman.h"
void *
mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    (void)addr; (void)fd; (void)offset;
    if (flags & MAP_ANONYMOUS) {
        DWORD protect = (prot & PROT_WRITE) ? PAGE_READWRITE : PAGE_READONLY;
        void *p = VirtualAlloc(NULL, length, MEM_COMMIT | MEM_RESERVE, protect);
        return p ? p : MAP_FAILED;
    }
    return MAP_FAILED;   /* file-backed mmap not implemented yet */
}
int munmap(void *addr, size_t length) { (void)length; return VirtualFree(addr, 0, MEM_RELEASE) ? 0 : -1; }
int msync(void *addr, size_t length, int flags) { (void)addr;(void)length;(void)flags; return 0; }
int mlock(const void *addr, size_t len) { (void)addr;(void)len; return 0; }
int munlock(const void *addr, size_t len) { (void)addr;(void)len; return 0; }
int shm_open(const char *name, int oflag, int mode) { (void)name;(void)oflag;(void)mode; errno = ENOSYS; return -1; }
int shm_unlink(const char *name) { (void)name; return 0; }
