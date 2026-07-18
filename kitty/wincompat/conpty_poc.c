/*
 * conpty_poc.c — Stage 3 derisk for the Windows port of kitty.
 *
 * Proves the core mechanism that will replace kitty's Unix forkpty()/child.c:
 * spawn a process attached to a real Win32 *pseudoconsole* (ConPTY) and pump
 * its output. No fork(), no PTY — the Windows way.
 *
 *   Unix (child.c)            ->   Windows (this)
 *   ---------------------------------------------------
 *   openpty()                 ->   CreatePseudoConsole()
 *   fork() + execvp()         ->   CreateProcessW(EXTENDED_STARTUPINFO_PRESENT)
 *   TIOCSWINSZ ioctl          ->   ResizePseudoConsole()
 *   read()/write() on pty fd  ->   ReadFile()/WriteFile() on the pipe HANDLEs
 *
 * ConPTY functions are resolved at runtime from kernel32 so this builds even on
 * MinGW toolchains whose headers predate the ConPTY API. Needs Windows 10 1809+.
 *
 * Build: gcc conpty_poc.c -o conpty_poc.exe
 */
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>

/* ConPTY types/consts, declared here so we don't depend on SDK header vintage. */
typedef VOID* HPCON;
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif
typedef HRESULT (WINAPI *PFN_CreatePseudoConsole)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
typedef HRESULT (WINAPI *PFN_ResizePseudoConsole)(HPCON, COORD);
typedef VOID    (WINAPI *PFN_ClosePseudoConsole)(HPCON);

static HANDLE g_out_read = NULL;

/* Reader thread: drain the pseudoconsole's output pipe to our stdout. */
static DWORD WINAPI
pump_output(LPVOID unused) {
    (void)unused;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(g_out_read, buf, sizeof buf, &n, NULL) && n > 0) {
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, &written, NULL);
    }
    return 0;
}

int
main(void) {
    /* 1. Two anonymous pipes: one feeds input into the console, one carries its output out. */
    HANDLE in_read = NULL, in_write = NULL, out_write = NULL;
    if (!CreatePipe(&in_read, &in_write, NULL, 0) ||
        !CreatePipe(&g_out_read, &out_write, NULL, 0)) {
        fprintf(stderr, "CreatePipe failed: %lu\n", GetLastError());
        return 1;
    }

    /* 2. Resolve the ConPTY API at runtime. */
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    PFN_CreatePseudoConsole pCreate =
        (PFN_CreatePseudoConsole)(void*)GetProcAddress(k32, "CreatePseudoConsole");
    PFN_ClosePseudoConsole pClose =
        (PFN_ClosePseudoConsole)(void*)GetProcAddress(k32, "ClosePseudoConsole");
    if (!pCreate) {
        fprintf(stderr, "ConPTY unavailable — needs Windows 10 1809+\n");
        return 2;
    }

    /* 3. Build the pseudoconsole. It reads from in_read, writes to out_write. */
    HPCON hpc = NULL;
    COORD size = { 100, 30 };
    HRESULT hr = pCreate(size, in_read, out_write, 0, &hpc);
    if (FAILED(hr)) {
        fprintf(stderr, "CreatePseudoConsole failed: hr=0x%08lx\n", (unsigned long)hr);
        return 3;
    }
    /* The console owns its ends now; the parent keeps in_write + g_out_read. */
    CloseHandle(in_read);
    CloseHandle(out_write);

    /* 4. STARTUPINFOEX carrying the pseudoconsole as a process/thread attribute. */
    STARTUPINFOEXW si;
    ZeroMemory(&si, sizeof si);
    si.StartupInfo.cb = sizeof si;
    SIZE_T attr_bytes = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_bytes);
    si.lpAttributeList =
        (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attr_bytes);
    if (!si.lpAttributeList ||
        !InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_bytes) ||
        !UpdateProcThreadAttribute(si.lpAttributeList, 0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   hpc, sizeof hpc, NULL, NULL)) {
        fprintf(stderr, "ProcThreadAttribute setup failed: %lu\n", GetLastError());
        return 4;
    }

    /* 5. Spawn the child attached to the pseudoconsole. */
    wchar_t cmdline[] =
        L"cmd.exe /c ver & echo [ConPTY OK] child ran inside a pseudoconsole";
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof pi);
    BOOL ok = CreateProcessW(NULL, cmdline, NULL, NULL, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                             &si.StartupInfo, &pi);
    if (!ok) {
        fprintf(stderr, "CreateProcessW failed: %lu\n", GetLastError());
        return 5;
    }

    /* 6. Pump output on a thread; wait for the child; then tear the console down
     *    (closing out_write, which unblocks the reader). */
    HANDLE reader = CreateThread(NULL, 0, pump_output, NULL, 0, NULL);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    if (pClose) pClose(hpc);            /* closes the console's out_write end */
    WaitForSingleObject(reader, 2000);  /* let the reader drain + exit */

    printf("\n--- child exited, code=%lu; ConPTY spawn/pump verified ---\n",
           (unsigned long)code);

    DeleteProcThreadAttributeList(si.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(reader);
    CloseHandle(in_write);
    CloseHandle(g_out_read);
    return 0;
}
