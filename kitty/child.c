/*
 * child.c
 * Copyright (C) 2018 Kovid Goyal <kovid at kovidgoyal.net>
 *
 * Distributed under terms of the GPL3 license.
 */

#include "data-types.h"
#include "safe-wrappers.h"
#include <errno.h>
#include <string.h>

#ifndef _WIN32
/* ======================================================================
 *  POSIX implementation (fork + pty slave)
 * ==================================================================== */
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>

#define EXTRA_ENV_BUFFER_SIZE 64

static char**
serialize_string_tuple(PyObject *src, Py_ssize_t extra) {
    const Py_ssize_t sz = PyTuple_GET_SIZE(src);
    size_t required_size = sizeof(char*) * (1 + sz + extra);
    required_size += extra * EXTRA_ENV_BUFFER_SIZE;
    void *block = calloc(required_size, 1);
    if (!block) { PyErr_NoMemory(); return NULL; }
    char **ans = block;
    for (Py_ssize_t i = 0; i < sz; i++) {
        PyObject *x = PyTuple_GET_ITEM(src, i);
        if (!PyUnicode_Check(x)) { free(block); PyErr_SetString(PyExc_TypeError, "string tuple must have only strings"); return NULL; }
        ans[i] = (char*)PyUnicode_AsUTF8(x);
        if (!ans[i]) { free(block); return NULL; }
    }
    return ans;
}

static void
write_to_stderr(const char *text) {
    size_t sz = strlen(text);
    size_t written = 0;
    while(written < sz) {
        ssize_t amt = write(2, text + written, sz - written);
        if (amt == 0) break;
        if (amt < 0) {
            if (errno == EAGAIN || errno == EINTR) continue;
            break;
        }
        written += amt;
    }
}

#define exit_on_err(m) { write_to_stderr(m); write_to_stderr(": "); write_to_stderr(strerror(errno)); exit(EXIT_FAILURE); }

static void
wait_for_terminal_ready(int fd) {
    char data;
    while(1) {
        int ret = read(fd, &data, 1);
        if (ret == -1 && (errno == EINTR || errno == EAGAIN)) continue;
        break;
    }
}

static PyObject*
spawn(PyObject *self UNUSED, PyObject *args) {
    PyObject *argv_p, *env_p, *handled_signals_p, *pass_fds;
    int master, slave, stdin_read_fd, stdin_write_fd, ready_read_fd, ready_write_fd, forward_stdio;
    const char *kitten_exe;
    char *cwd, *exe;
    if (!PyArg_ParseTuple(args, "ssO!O!iiiiiiO!spO!", &exe, &cwd, &PyTuple_Type, &argv_p, &PyTuple_Type, &env_p, &master, &slave, &stdin_read_fd, &stdin_write_fd, &ready_read_fd, &ready_write_fd, &PyTuple_Type, &handled_signals_p, &kitten_exe, &forward_stdio, &PyTuple_Type, &pass_fds)) return NULL;
    char name[2048] = {0};
    if (ttyname_r(slave, name, sizeof(name) - 1) != 0) { PyErr_SetFromErrno(PyExc_OSError); return NULL; }
    char **argv = serialize_string_tuple(argv_p, 0);
    if (!argv) return NULL;
    char **env = serialize_string_tuple(env_p, 1);
    if (!env) { free(argv); return NULL; }
    int handled_signals[16] = {0}, num_handled_signals = MIN((int)arraysz(handled_signals), PyTuple_GET_SIZE(handled_signals_p));
    for (Py_ssize_t i = 0; i < num_handled_signals; i++) handled_signals[i] = PyLong_AsLong(PyTuple_GET_ITEM(handled_signals_p, i));

#if PY_VERSION_HEX >= 0x03070000
    PyOS_BeforeFork();
#endif
    pid_t pid = fork();
    switch(pid) {
        case 0: {
            // child
#if PY_VERSION_HEX >= 0x03070000
            PyOS_AfterFork_Child();
#endif
            const struct sigaction act = {.sa_handler=SIG_DFL};

#define SA(which)  if (sigaction(which, &act, NULL) != 0) exit_on_err("sigaction() in child process failed");
            for (int si = 0; si < num_handled_signals; si++) { SA(handled_signals[si]); }
            // See _Py_RestoreSignals in signalmodule.c for a list of signals python nukes
#ifdef SIGPIPE
            SA(SIGPIPE)
#endif
#ifdef SIGXFSZ
            SA(SIGXFSZ);
#endif
#ifdef SIGXFZ
            SA(SIGXFZ);
#endif
#undef SA
            sigset_t signals; sigemptyset(&signals);
            if (sigprocmask(SIG_SETMASK, &signals, NULL) != 0) exit_on_err("sigprocmask() in child process failed");
            // Use only signal-safe functions (man 7 signal-safety)
            if (chdir(cwd) != 0) {
                if (access(".", X_OK) != 0) { // existing cwd does not exist or dont have permissions for it
                    if (chdir("/") != 0) {} // ignore failure to chdir to /
                }
            };
            if (setsid() == -1) exit_on_err("setsid() in child process failed");

            // Establish the controlling terminal (see man 7 credentials)
            int tfd = safe_open(name, O_RDWR | O_CLOEXEC, 0);
            if (tfd == -1) exit_on_err("Failed to open controlling terminal");
            // On BSD open() does not establish the controlling terminal
            if (ioctl(tfd, TIOCSCTTY, 0) == -1) exit_on_err("Failed to set controlling terminal with TIOCSCTTY");
            safe_close(tfd, __FILE__, __LINE__);

            fd_set passed_fds; FD_ZERO(&passed_fds); bool has_preserved_fds = false;
            if (forward_stdio) {
                int fd = safe_dup(STDOUT_FILENO);
                if (fd < 0) exit_on_err("dup() failed for forwarded STDOUT");
                FD_SET(fd, &passed_fds);
                size_t s = PyTuple_GET_SIZE(env_p);
                env[s] = (char*)(env + (s + 2));
                snprintf(env[s], EXTRA_ENV_BUFFER_SIZE, "KITTY_STDIO_FORWARDED=%d", fd);
                fd = safe_dup(STDERR_FILENO);
                if (fd < 0) exit_on_err("dup() failed for forwarded STDERR");
                FD_SET(fd, &passed_fds);
                has_preserved_fds = true;
            }

            for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(pass_fds); i++) {
                PyObject *pfd = PyTuple_GET_ITEM(pass_fds, i);
                if (!PyLong_Check(pfd)) exit_on_err("pass_fds must contain only integers");
                int fd = PyLong_AsLong(pfd);
                if (fd > -1 && fd < FD_SETSIZE) {
                    FD_SET(fd, &passed_fds);
                    has_preserved_fds = true;
                }
            }
            // Redirect stdin/stdout/stderr to the pty
            if (safe_dup2(slave, STDOUT_FILENO) == -1) exit_on_err("dup2() failed for fd number 1");
            if (safe_dup2(slave, STDERR_FILENO) == -1) exit_on_err("dup2() failed for fd number 2");
            if (stdin_read_fd > -1) {
                if (safe_dup2(stdin_read_fd, STDIN_FILENO) == -1) exit_on_err("dup2() failed for fd number 0");
                safe_close(stdin_read_fd, __FILE__, __LINE__);
                safe_close(stdin_write_fd, __FILE__, __LINE__);
            } else {
                if (safe_dup2(slave, STDIN_FILENO) == -1) exit_on_err("dup2() failed for fd number 0");
            }
            safe_close(slave, __FILE__, __LINE__);
            safe_close(master, __FILE__, __LINE__);

            // Wait for READY_SIGNAL which indicates kitty has setup the screen object
            safe_close(ready_write_fd, __FILE__, __LINE__);
            wait_for_terminal_ready(ready_read_fd);
            safe_close(ready_read_fd, __FILE__, __LINE__);

            // Close any extra fds inherited from parent
            if (has_preserved_fds) { for (int c = 3; c < 256; c++) { if (!FD_ISSET(c, &passed_fds)) safe_close(c, __FILE__, __LINE__); } }
            else for (int c = 3; c < 256; c++) { safe_close(c, __FILE__, __LINE__); }

            extern char **environ;
            environ = env;
            execvp(exe, argv);
            // Report the failure and exec kitten instead, so that we are not left
            // with a forked but not exec'ed process
            write_to_stderr("Failed to launch child: ");
            write_to_stderr(exe);
            write_to_stderr("\nWith error: ");
            write_to_stderr(strerror(errno));
            write_to_stderr("\n");
            execlp(kitten_exe, "kitten", "__hold_till_enter__", NULL);
            exit(EXIT_FAILURE);
            break;
        }
        case -1: {
#if PY_VERSION_HEX >= 0x03070000
            int saved_errno = errno;
            PyOS_AfterFork_Parent();
            errno = saved_errno;
#endif
            PyErr_SetFromErrno(PyExc_OSError);
            break;
        }
        default:
#if PY_VERSION_HEX >= 0x03070000
            PyOS_AfterFork_Parent();
#endif
            break;
    }
#undef exit_on_err
    free(argv);
    free(env);
    if (PyErr_Occurred()) return NULL;
    return PyLong_FromLong(pid);
}

static PyMethodDef module_methods[] = {
    METHODB(spawn, METH_VARARGS),
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

bool
init_child(PyObject *module) {
    PyModule_AddIntMacro(module, CLD_KILLED);
    PyModule_AddIntMacro(module, CLD_STOPPED);
    PyModule_AddIntMacro(module, CLD_EXITED);
    PyModule_AddIntMacro(module, CLD_CONTINUED);
    if (PyModule_AddFunctions(module, module_methods) != 0) return false;
    return true;
}

#else  /* _WIN32 */
/* ======================================================================
 *  Windows implementation (ConPTY: CreatePseudoConsole + CreateProcess)
 *
 *  There is no fork()/openpty() on Windows. A "pty" here is a pseudoconsole
 *  plus two anonymous pipes; the parent-side pipe HANDLEs are bridged to CRT
 *  int fds via _open_osfhandle so the rest of kitty can hold them as fds.
 *  The Windows event loop (Stage 4) reads/writes those handles with overlapped
 *  I/O; reaping waits on the stored process HANDLE.
 * ==================================================================== */
#include <windows.h>
#include <io.h>
#include <fcntl.h>

typedef VOID* HPCON;
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif
#ifndef CLD_EXITED
#define CLD_EXITED    1
#define CLD_KILLED    2
#define CLD_DUMPED    3
#define CLD_STOPPED   5
#define CLD_CONTINUED 6
#endif
typedef HRESULT (WINAPI *PFN_CreatePseudoConsole)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
typedef HRESULT (WINAPI *PFN_ResizePseudoConsole)(HPCON, COORD);
typedef VOID    (WINAPI *PFN_ClosePseudoConsole)(HPCON);
static PFN_CreatePseudoConsole pCreatePseudoConsole;
static PFN_ResizePseudoConsole pResizePseudoConsole;
static PFN_ClosePseudoConsole  pClosePseudoConsole;

static bool
load_conpty(void) {
    if (pCreatePseudoConsole) return true;
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    pCreatePseudoConsole = (PFN_CreatePseudoConsole)(void*)GetProcAddress(k, "CreatePseudoConsole");
    pResizePseudoConsole = (PFN_ResizePseudoConsole)(void*)GetProcAddress(k, "ResizePseudoConsole");
    pClosePseudoConsole  = (PFN_ClosePseudoConsole)(void*)GetProcAddress(k, "ClosePseudoConsole");
    return pCreatePseudoConsole != NULL;
}

#define MAX_PTYS 128
typedef struct {
    bool   in_use;
    HPCON  hpc;
    HANDLE in_write;   /* parent writes child stdin here */
    HANDLE out_read;   /* parent reads child stdout here */
    HANDLE process;    /* child process handle, for reaping */
    int    read_fd;    /* CRT fd wrapping out_read */
    int    write_fd;   /* CRT fd wrapping in_write */
} PtyEntry;
static PtyEntry ptys[MAX_PTYS];

static wchar_t*
utf8_to_wide(const char *s) {
    if (!s) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = calloc((size_t)n, sizeof(wchar_t));
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

/* Join argv into a single command line, quoting args that need it (simplified
 * CommandLineToArgvW rules: wrap in quotes, double embedded quotes/backslashes
 * before a closing quote). */
static wchar_t*
build_command_line(PyObject *argv_p) {
    Py_ssize_t n = PyTuple_GET_SIZE(argv_p);
    size_t cap = 256, len = 0;
    wchar_t *buf = malloc(cap * sizeof(wchar_t));
    if (!buf) return NULL;
#define PUT(ch) do { if (len + 2 >= cap) { cap *= 2; wchar_t *nb = realloc(buf, cap * sizeof(wchar_t)); if (!nb) { free(buf); return NULL; } buf = nb; } buf[len++] = (ch); } while (0)
    for (Py_ssize_t i = 0; i < n; i++) {
        if (i) PUT(L' ');
        const char *a8 = PyUnicode_AsUTF8(PyTuple_GET_ITEM(argv_p, i));
        wchar_t *a = utf8_to_wide(a8 ? a8 : "");
        if (!a) { free(buf); return NULL; }
        bool need_quote = (a[0] == 0);
        for (wchar_t *p = a; *p; p++) if (*p == L' ' || *p == L'\t' || *p == L'"') { need_quote = true; break; }
        if (!need_quote) { for (wchar_t *p = a; *p; p++) PUT(*p); free(a); continue; }
        PUT(L'"');
        for (wchar_t *p = a; ; p++) {
            unsigned backslashes = 0;
            while (*p == L'\\') { backslashes++; p++; }
            if (*p == 0) { for (unsigned b = 0; b < backslashes * 2; b++) PUT(L'\\'); break; }
            if (*p == L'"') { for (unsigned b = 0; b < backslashes * 2 + 1; b++) PUT(L'\\'); PUT(L'"'); }
            else { for (unsigned b = 0; b < backslashes; b++) PUT(L'\\'); PUT(*p); }
        }
        PUT(L'"');
        free(a);
    }
    PUT(0);
#undef PUT
    return buf;
}

/* Build a CREATE_UNICODE_ENVIRONMENT block: "K=V\0K=V\0\0" */
static wchar_t*
build_env_block(PyObject *env_p) {
    Py_ssize_t n = PyTuple_GET_SIZE(env_p);
    size_t cap = 256, len = 0;
    wchar_t *buf = malloc(cap * sizeof(wchar_t));
    if (!buf) return NULL;
    for (Py_ssize_t i = 0; i < n; i++) {
        const char *e8 = PyUnicode_AsUTF8(PyTuple_GET_ITEM(env_p, i));
        wchar_t *e = utf8_to_wide(e8 ? e8 : "");
        if (!e) { free(buf); return NULL; }
        size_t l = wcslen(e) + 1;  /* include NUL */
        while (len + l + 1 >= cap) { cap *= 2; wchar_t *nb = realloc(buf, cap * sizeof(wchar_t)); if (!nb) { free(e); free(buf); return NULL; } buf = nb; }
        memcpy(buf + len, e, l * sizeof(wchar_t));
        len += l;
        free(e);
    }
    buf[len] = 0;  /* final terminating NUL after the last entry */
    return buf;
}

/* open_pty(cols, rows) -> (read_fd, write_fd, pty_id) */
static PyObject*
open_pty(PyObject *self UNUSED, PyObject *args) {
    int cols = 80, rows = 24;
    if (!PyArg_ParseTuple(args, "ii", &cols, &rows)) return NULL;
    if (!load_conpty()) { PyErr_SetString(PyExc_RuntimeError, "ConPTY unavailable (needs Windows 10 1809+)"); return NULL; }
    // Prepare the process, once, so ConPTY children attach to the pseudoconsole:
    //  1. Detach any inherited console (kitty is a GUI app; a console would be
    //     inherited by the child instead of the pty).
    //  2. Mark our standard handles non-inheritable. When kitty is launched with
    //     redirected stdio (files/pipes), those handles are inheritable and the
    //     ConPTY child picks them up as its stdio instead of the pty, so the
    //     child's real output bypasses us entirely. Clearing the inherit flag
    //     forces the child onto the pseudoconsole.
    static bool spawn_env_prepared = false;
    if (!spawn_env_prepared) {
        spawn_env_prepared = true;
        if (GetConsoleWindow()) FreeConsole();
        const DWORD std_ids[3] = { STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE };
        for (int i = 0; i < 3; i++) {
            HANDLE h = GetStdHandle(std_ids[i]);
            if (h && h != INVALID_HANDLE_VALUE) SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0);
        }
    }
    int id = -1;
    for (int i = 0; i < MAX_PTYS; i++) if (!ptys[i].in_use) { id = i; break; }
    if (id < 0) { PyErr_SetString(PyExc_RuntimeError, "too many open ptys"); return NULL; }

    HANDLE in_read = NULL, in_write = NULL, out_read = NULL, out_write = NULL;
    if (!CreatePipe(&in_read, &in_write, NULL, 0) || !CreatePipe(&out_read, &out_write, NULL, 0)) {
        PyErr_SetFromWindowsErr(0);
        if (in_read) CloseHandle(in_read); if (in_write) CloseHandle(in_write);
        if (out_read) CloseHandle(out_read); if (out_write) CloseHandle(out_write);
        return NULL;
    }
    HPCON hpc = NULL;
    COORD size = { (SHORT)cols, (SHORT)rows };
    HRESULT hr = pCreatePseudoConsole(size, in_read, out_write, 0, &hpc);
    CloseHandle(in_read); CloseHandle(out_write);  /* owned by the console now */
    if (FAILED(hr)) {
        CloseHandle(in_write); CloseHandle(out_read);
        PyErr_Format(PyExc_OSError, "CreatePseudoConsole failed: 0x%08lx", (unsigned long)hr);
        return NULL;
    }
    int read_fd  = _open_osfhandle((intptr_t)out_read, _O_RDONLY | _O_BINARY);
    int write_fd = _open_osfhandle((intptr_t)in_write, _O_WRONLY | _O_BINARY);
    ptys[id].in_use = true; ptys[id].hpc = hpc;
    ptys[id].in_write = in_write; ptys[id].out_read = out_read; ptys[id].process = NULL;
    ptys[id].read_fd = read_fd; ptys[id].write_fd = write_fd;
    return Py_BuildValue("iii", read_fd, write_fd, id);
}

/* spawn(pty_id, exe, cwd, argv_tuple, env_tuple) -> pid */
static PyObject*
spawn(PyObject *self UNUSED, PyObject *args) {
    int id; const char *exe, *cwd; PyObject *argv_p, *env_p;
    if (!PyArg_ParseTuple(args, "issO!O!", &id, &exe, &cwd, &PyTuple_Type, &argv_p, &PyTuple_Type, &env_p)) return NULL;
    if (id < 0 || id >= MAX_PTYS || !ptys[id].in_use) { PyErr_SetString(PyExc_ValueError, "invalid pty id"); return NULL; }

    wchar_t *cmdline = build_command_line(argv_p);
    wchar_t *envblock = build_env_block(env_p);
    wchar_t *wcwd = utf8_to_wide(cwd);
    wchar_t *wexe = (exe && exe[0]) ? utf8_to_wide(exe) : NULL;
    if (!cmdline || !envblock) { free(cmdline); free(envblock); free(wcwd); free(wexe); return PyErr_NoMemory(); }

    STARTUPINFOEXW si; ZeroMemory(&si, sizeof si); si.StartupInfo.cb = sizeof si;
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &bytes);
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, bytes);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof pi);
    BOOL ok = FALSE;
    if (si.lpAttributeList &&
        InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &bytes) &&
        UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, ptys[id].hpc, sizeof(HPCON), NULL, NULL)) {
        // Windows propagates the parent's standard handles to a child when
        // STARTF_USESTDHANDLES is not set, and this overrides the pseudoconsole:
        // if kitty's stdio is redirected (files/pipes) the child writes there
        // instead of the pty. Blank our std handles across the spawn so the
        // pseudoconsole is the only stdio the child can attach to, then restore.
        HANDLE saved[3] = { GetStdHandle(STD_INPUT_HANDLE), GetStdHandle(STD_OUTPUT_HANDLE), GetStdHandle(STD_ERROR_HANDLE) };
        SetStdHandle(STD_INPUT_HANDLE, NULL);
        SetStdHandle(STD_OUTPUT_HANDLE, NULL);
        SetStdHandle(STD_ERROR_HANDLE, NULL);
        ok = CreateProcessW(wexe, cmdline, NULL, NULL, FALSE,
                            EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                            envblock, wcwd, &si.StartupInfo, &pi);
        SetStdHandle(STD_INPUT_HANDLE, saved[0]);
        SetStdHandle(STD_OUTPUT_HANDLE, saved[1]);
        SetStdHandle(STD_ERROR_HANDLE, saved[2]);
    }
    DWORD err = ok ? 0 : GetLastError();
    if (si.lpAttributeList) { DeleteProcThreadAttributeList(si.lpAttributeList); HeapFree(GetProcessHeap(), 0, si.lpAttributeList); }
    free(cmdline); free(envblock); free(wcwd); free(wexe);
    if (!ok) { PyErr_SetFromWindowsErr((int)err); return NULL; }

    CloseHandle(pi.hThread);
    ptys[id].process = pi.hProcess;   /* kept for reaping (Stage 4) */
    return PyLong_FromUnsignedLong(GetProcessId(pi.hProcess));
}

/* The child-monitor writes to a child using its single "fd", which on Windows is
 * the pty's read side. Map it to the pty's write side (the ConPTY input pipe). */
int
windows_pty_write_fd_for(int read_fd) {
    for (int i = 0; i < MAX_PTYS; i++) if (ptys[i].in_use && ptys[i].read_fd == read_fd) return ptys[i].write_fd;
    return -1;
}

/* Resize the pseudoconsole backing the pty whose read side is read_fd. Used by
 * the child-monitor's TIOCSWINSZ path (there is no ioctl on Windows). */
int
windows_pty_resize_for(int read_fd, int cols, int rows) {
    for (int i = 0; i < MAX_PTYS; i++) {
        if (ptys[i].in_use && ptys[i].read_fd == read_fd) {
            if (pResizePseudoConsole && ptys[i].hpc && cols > 0 && rows > 0) {
                COORD size = { (SHORT)cols, (SHORT)rows };
                pResizePseudoConsole(ptys[i].hpc, size);
            }
            return 0;
        }
    }
    return -1;
}

/* resize_pty(pty_id, cols, rows) */
static PyObject*
resize_pty(PyObject *self UNUSED, PyObject *args) {
    int id, cols, rows;
    if (!PyArg_ParseTuple(args, "iii", &id, &cols, &rows)) return NULL;
    if (id < 0 || id >= MAX_PTYS || !ptys[id].in_use) { PyErr_SetString(PyExc_ValueError, "invalid pty id"); return NULL; }
    if (pResizePseudoConsole) {
        COORD size = { (SHORT)cols, (SHORT)rows };
        pResizePseudoConsole(ptys[id].hpc, size);
    }
    Py_RETURN_NONE;
}

/* close_pty(pty_id) */
static PyObject*
close_pty(PyObject *self UNUSED, PyObject *args) {
    int id;
    if (!PyArg_ParseTuple(args, "i", &id)) return NULL;
    if (id < 0 || id >= MAX_PTYS || !ptys[id].in_use) Py_RETURN_NONE;
    if (pClosePseudoConsole && ptys[id].hpc) pClosePseudoConsole(ptys[id].hpc);
    if (ptys[id].process) CloseHandle(ptys[id].process);
    /* read_fd/write_fd own out_read/in_write via _open_osfhandle; the fd owner
     * closes them with _close(). */
    memset(&ptys[id], 0, sizeof(PtyEntry));
    Py_RETURN_NONE;
}

static PyMethodDef module_methods[] = {
    METHODB(open_pty, METH_VARARGS),
    METHODB(spawn, METH_VARARGS),
    METHODB(resize_pty, METH_VARARGS),
    METHODB(close_pty, METH_VARARGS),
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

bool
init_child(PyObject *module) {
    PyModule_AddIntMacro(module, CLD_KILLED);
    PyModule_AddIntMacro(module, CLD_STOPPED);
    PyModule_AddIntMacro(module, CLD_EXITED);
    PyModule_AddIntMacro(module, CLD_CONTINUED);
    if (PyModule_AddFunctions(module, module_methods) != 0) return false;
    return true;
}

#endif  /* _WIN32 */
