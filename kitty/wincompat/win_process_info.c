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

static PyMethodDef module_methods[] = {
    {"cwd_of_process", (PyCFunction)cwd_of_process, METH_O, ""},
    {NULL, NULL, 0, NULL}
};

bool
init_win_process_info(PyObject *module) {
    return PyModule_AddFunctions(module, module_methods) == 0;
}
