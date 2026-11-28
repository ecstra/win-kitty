/*
 * win_process_info.c — read a process's working directory on Windows.
 *
 * There is no /proc on Windows, so cwd_of_process reads the target process's
 * PEB (via NtQueryInformationProcess) and then its RTL_USER_PROCESS_PARAMETERS
 * to pull CurrentDirectory.DosPath. kitty uses this to show the shell's folder
 * in tab titles and to open new tabs and windows in the same directory. It only
 * works for a same-architecture target, which is fine here as kitty and the
 * shells it runs are all 64 bit.
 */

#include "../data-types.h"   /* Python.h, UNUSED (windows.h comes via win_prelude) */
#include <winternl.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <shellapi.h>

/* Offset of CurrentDirectory (a CURDIR: UNICODE_STRING DosPath + HANDLE) inside
 * RTL_USER_PROCESS_PARAMETERS on x64. winternl.h does not expose this field, and
 * the layout is a stable part of the Windows x64 ABI. */
#define CURDIR_OFFSET_X64 0x38

typedef NTSTATUS (NTAPI *PFN_NtQueryInformationProcess)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

static PyObject*
cwd_of_process(PyObject *self UNUSED, PyObject *pid_) {
    if (!PyLong_Check(pid_)) { PyErr_SetString(PyExc_TypeError, "pid must be an int"); return NULL; }
    long pid = PyLong_AsLong(pid_);
    if (pid < 0) { PyErr_SetString(PyExc_TypeError, "pid cannot be negative"); return NULL; }

    static PFN_NtQueryInformationProcess NtQIP = NULL;
    if (!NtQIP) {
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        if (nt) NtQIP = (PFN_NtQueryInformationProcess)(void*)GetProcAddress(nt, "NtQueryInformationProcess");
        if (!NtQIP) { PyErr_SetString(PyExc_OSError, "NtQueryInformationProcess unavailable"); return NULL; }
    }

    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, (DWORD)pid);
    if (!h) { PyErr_SetFromWindowsErr(0); return NULL; }

    PyObject *ans = NULL;
    PROCESS_BASIC_INFORMATION pbi;
    PVOID params = NULL;
    UNICODE_STRING us;
    ZeroMemory(&pbi, sizeof pbi);
    ZeroMemory(&us, sizeof us);

    /* PEB.ProcessParameters, then RTL_USER_PROCESS_PARAMETERS.CurrentDirectory.DosPath */
    if (NtQIP(h, ProcessBasicInformation, &pbi, sizeof pbi, NULL) == 0 && pbi.PebBaseAddress
        && ReadProcessMemory(h, (char*)pbi.PebBaseAddress + offsetof(PEB, ProcessParameters), &params, sizeof params, NULL) && params
        && ReadProcessMemory(h, (char*)params + CURDIR_OFFSET_X64, &us, sizeof us, NULL) && us.Buffer && us.Length) {
        wchar_t *buf = malloc((size_t)us.Length + sizeof(wchar_t));
        if (buf) {
            if (ReadProcessMemory(h, us.Buffer, buf, us.Length, NULL)) {
                size_t n = us.Length / sizeof(wchar_t);
                /* The stored path keeps a trailing separator; drop it, but leave a
                 * drive root like C:\ intact. */
                while (n > 0 && (buf[n-1] == L'\\' || buf[n-1] == L'/') && !(n == 3 && buf[1] == L':')) n--;
                ans = PyUnicode_FromWideChar(buf, (Py_ssize_t)n);
            }
            free(buf);
        } else PyErr_NoMemory();
    }
    CloseHandle(h);
    if (!ans && !PyErr_Occurred()) PyErr_SetString(PyExc_OSError, "Failed to read the process working directory");
    return ans;
}

/* What a window's tab title and kitten @ ls show of the programs in it, which
 * Linux reads from /proc. Each needs only PROCESS_QUERY_LIMITED_INFORMATION, so
 * it also works for programs started elevated, such as by sudo. */

static HANDLE
open_process(PyObject *pid_) {
    if (!PyLong_Check(pid_)) { PyErr_SetString(PyExc_TypeError, "pid must be an int"); return NULL; }
    long pid = PyLong_AsLong(pid_);
    if (pid < 0) { PyErr_SetString(PyExc_TypeError, "pid cannot be negative"); return NULL; }
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) { PyErr_SetFromWindowsErr(0); return NULL; }
    // A process that has exited stays open while anything holds a handle to
    // it, as kitty does for each window's program until the window closes
    DWORD code;
    if (GetExitCodeProcess(h, &code) && code != STILL_ACTIVE) {
        CloseHandle(h);
        PyErr_Format(PyExc_ProcessLookupError, "The process %ld has exited", pid);
        return NULL;
    }
    return h;
}

/* Not in the UCRT64 headers. The class is there from Windows 8.1 and returns a
 * UNICODE_STRING without reading the other process's memory. */
#define PROCESS_COMMAND_LINE_INFORMATION ((PROCESSINFOCLASS)60)

static PyObject*
cmdline_of_process(PyObject *self UNUSED, PyObject *pid_) {
    static PFN_NtQueryInformationProcess NtQIP = NULL;
    if (!NtQIP) {
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        if (nt) NtQIP = (PFN_NtQueryInformationProcess)(void*)GetProcAddress(nt, "NtQueryInformationProcess");
        if (!NtQIP) { PyErr_SetString(PyExc_OSError, "NtQueryInformationProcess unavailable"); return NULL; }
    }
    HANDLE h = open_process(pid_);
    if (!h) return NULL;
    ULONG needed = 0;
    NtQIP(h, PROCESS_COMMAND_LINE_INFORMATION, NULL, 0, &needed);
    UNICODE_STRING *us = needed ? malloc(needed) : NULL;
    if (!us || NtQIP(h, PROCESS_COMMAND_LINE_INFORMATION, us, needed, &needed) != 0) {
        free(us); CloseHandle(h);
        PyErr_SetString(PyExc_OSError, "Failed to read the process command line");
        return NULL;
    }
    CloseHandle(h);
    PyObject *ans = PyList_New(0);
    /* CommandLineToArgvW turns an empty line into this process's own path */
    const size_t n = us->Length / sizeof(wchar_t);
    wchar_t *line = n ? calloc(n + 1, sizeof(wchar_t)) : NULL;
    if (ans && line) {
        memcpy(line, us->Buffer, n * sizeof(wchar_t));
        int argc = 0;
        wchar_t **argv = CommandLineToArgvW(line, &argc);
        for (int i = 0; argv && i < argc; i++) {
            PyObject *arg = PyUnicode_FromWideChar(argv[i], -1);
            if (!arg || PyList_Append(ans, arg) != 0) { Py_XDECREF(arg); Py_CLEAR(ans); break; }
            Py_DECREF(arg);
        }
        if (argv) LocalFree(argv);
    }
    free(line); free(us);
    return ans;
}

static PyObject*
abspath_of_process(PyObject *self UNUSED, PyObject *pid_) {
    HANDLE h = open_process(pid_);
    if (!h) return NULL;
    wchar_t path[32768];
    DWORD sz = sizeof(path) / sizeof(path[0]);
    const BOOL ok = QueryFullProcessImageNameW(h, 0, path, &sz);
    CloseHandle(h);
    if (!ok) return PyErr_SetFromWindowsErr(0);
    return PyUnicode_FromWideChar(path, sz);
}

/* PROCESS_MEMORY_COUNTERS_EX2, which the UCRT64 psapi.h does not declare yet */
typedef struct {
    DWORD cb, PageFaultCount;
    SIZE_T PeakWorkingSetSize, WorkingSetSize, QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage, PagefileUsage, PeakPagefileUsage;
    SIZE_T PrivateUsage, PrivateWorkingSetSize;
    ULONG64 SharedCommitUsage;
} MemoryCountersEx2;

/* The private working set is the memory Task Manager shows, and leaves out the
 * DLL pages processes share, which a working set counts once per process.
 * Windows builds older than 22H2 do not have it and get the working set. */
static PyObject*
memory_of_process(PyObject *self UNUSED, PyObject *pid_) {
    HANDLE h = open_process(pid_);
    if (!h) return NULL;
    MemoryCountersEx2 m = {.cb = sizeof(m)};
    SIZE_T ans;
    if (GetProcessMemoryInfo(h, (PROCESS_MEMORY_COUNTERS*)&m, sizeof(m))) ans = m.PrivateWorkingSetSize;
    else {
        PROCESS_MEMORY_COUNTERS c = {.cb = sizeof(c)};
        if (!GetProcessMemoryInfo(h, &c, sizeof(c))) { CloseHandle(h); return PyErr_SetFromWindowsErr(0); }
        ans = c.WorkingSetSize;
    }
    CloseHandle(h);
    return PyLong_FromSize_t(ans);
}

static bool
creation_time_of(DWORD pid, ULONGLONG *ans) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    FILETIME created, exited, kernel, user;
    const BOOL ok = GetProcessTimes(h, &created, &exited, &kernel, &user);
    CloseHandle(h);
    if (ok) *ans = ((ULONGLONG)created.dwHighDateTime << 32) | created.dwLowDateTime;
    return ok;
}

/* Windows hands a pid out again once its process is gone, so a process that
 * names a parent started after it was started by an older one that has exited */
static PyObject*
children_of_process(PyObject *self UNUSED, PyObject *pid_) {
    if (!PyLong_Check(pid_)) { PyErr_SetString(PyExc_TypeError, "pid must be an int"); return NULL; }
    const DWORD pid = (DWORD)PyLong_AsUnsignedLong(pid_);
    if (PyErr_Occurred()) return NULL;
    ULONGLONG parent_created;
    if (!creation_time_of(pid, &parent_created)) return PyErr_SetFromWindowsErr(0);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return PyErr_SetFromWindowsErr(0);
    PyObject *ans = PyList_New(0);
    PROCESSENTRY32W e = {.dwSize = sizeof(e)};
    for (BOOL more = Process32FirstW(snap, &e); ans && more; more = Process32NextW(snap, &e)) {
        ULONGLONG created;
        if (e.th32ParentProcessID != pid || e.th32ProcessID == pid) continue;
        if (!creation_time_of(e.th32ProcessID, &created) || created < parent_created) continue;
        PyObject *child = PyLong_FromUnsignedLong(e.th32ProcessID);
        if (!child || PyList_Append(ans, child) != 0) Py_CLEAR(ans);
        Py_XDECREF(child);
    }
    CloseHandle(snap);
    return ans;
}

static PyObject*
ppid_of_process(PyObject *self UNUSED, PyObject *pid_) {
    if (!PyLong_Check(pid_)) { PyErr_SetString(PyExc_TypeError, "pid must be an int"); return NULL; }
    const DWORD pid = (DWORD)PyLong_AsUnsignedLong(pid_);
    if (PyErr_Occurred()) return NULL;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return PyErr_SetFromWindowsErr(0);
    PROCESSENTRY32W e = {.dwSize = sizeof(e)};
    long ppid = -1;
    for (BOOL more = Process32FirstW(snap, &e); more; more = Process32NextW(snap, &e)) {
        if (e.th32ProcessID == pid) { ppid = (long)e.th32ParentProcessID; break; }
    }
    CloseHandle(snap);
    if (ppid < 0) { PyErr_Format(PyExc_ProcessLookupError, "No process with pid %lu", pid); return NULL; }
    return PyLong_FromLong(ppid);
}

/* os.kill(pid, 0) sends Ctrl+C on Windows rather than checking the pid. A
 * process kitty may not open still exists, and one that exited stays openable
 * while handles to it are held. */
static PyObject*
process_is_alive(PyObject *self UNUSED, PyObject *pid_) {
    if (!PyLong_Check(pid_)) { PyErr_SetString(PyExc_TypeError, "pid must be an int"); return NULL; }
    const unsigned long pid = PyLong_AsUnsignedLong(pid_);
    if (PyErr_Occurred()) return NULL;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return PyBool_FromLong(GetLastError() != ERROR_INVALID_PARAMETER);
    DWORD code = 0;
    const BOOL ok = GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return PyBool_FromLong(!ok || code == STILL_ACTIVE);
}

/* Start times as FILETIME ticks, which is what tells processes apart by age on
 * Windows, for child.c's list of a window's processes */
bool
win_process_creation_time(DWORD pid, ULONGLONG *ans) { return creation_time_of(pid, ans); }

static PyMethodDef module_methods[] = {
    {"cwd_of_process", (PyCFunction)cwd_of_process, METH_O, ""},
    {"cmdline_of_process", (PyCFunction)cmdline_of_process, METH_O, ""},
    {"abspath_of_process", (PyCFunction)abspath_of_process, METH_O, ""},
    {"memory_of_process", (PyCFunction)memory_of_process, METH_O, ""},
    {"children_of_process", (PyCFunction)children_of_process, METH_O, ""},
    {"ppid_of_process", (PyCFunction)ppid_of_process, METH_O, ""},
    {"process_is_alive", (PyCFunction)process_is_alive, METH_O, ""},
    {NULL, NULL, 0, NULL}
};

bool
init_win_process_info(PyObject *module) {
    return PyModule_AddFunctions(module, module_methods) == 0;
}
