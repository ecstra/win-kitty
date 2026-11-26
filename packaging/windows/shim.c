/*
 * Tiny launcher shim placed in <install>\bin, which is what goes on PATH.
 * The real executables live in <install>\kitty\launcher together with their
 * MinGW runtime DLLs; putting THAT directory on PATH would expose those DLLs
 * to every other program on the system (breaking, for example, other MinGW
 * toolchains that resolve runtime DLLs via PATH). The shim forwards the
 * command line to the real binary and mirrors its exit code.
 *
 * Built statically (no runtime dependencies) as two console shims, kitten,
 * which always waits for the child, and kitty, which waits only for the
 * invocations that print and exit: see make-dist.sh. SHIM_KITTEN is set on the
 * command line. Both are console programs because a shell waits for one and
 * reads what it prints, while PowerShell neither waits for a GUI program nor
 * reads its output, so a GUI kitty shim lost the output of kitty +list-fonts.
 * Run, the Start menu and Explorer open the GUI launcher directly, through
 * the App Paths entry and shortcuts the installer writes, so no console shows.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef SHIM_KITTEN
#define TARGET_RELPATH L"\\kitty\\launcher\\kitten.exe"
#define SHIM_WAIT 1
#else
#define TARGET_RELPATH L"\\kitty\\launcher\\kitty.exe"
/* The console subsystem twin, for invocations that print instead of opening a
 * window. A GUI subsystem process cannot be captured by every shell even after
 * it attaches to the parent console: PowerShell reads nothing from one, so
 * kitty --version looked silent there while working under cmd. */
#define TARGET_CONSOLE_RELPATH L"\\kitty\\launcher\\kitty-console.exe"
#endif

static void append(wchar_t *dst, size_t cap, const wchar_t *src) {
    size_t n = lstrlenW(dst);
    lstrcpynW(dst + n, src, (int)(cap - n));
}

#ifndef SHIM_WAIT
static BOOL token_is(const wchar_t *s, const wchar_t *flag) {
    while (*flag) { if (*s != *flag) return FALSE; s++; flag++; }
    return *s == 0 || *s == L' ' || *s == L'\t';
}

/* Whether this invocation prints to the terminal and exits, rather than opening
 * a window. The version and help flags qualify, and so do the + entry points,
 * such as +list-fonts and +runpy, all but +open, which opens a window.
 *
 * Two things are needed for them to be seen from another terminal. The shim has
 * to wait, or it returns to the prompt first and the output lands under a
 * prompt that is already drawn, reading as though nothing was printed. And the
 * child has to be the console subsystem binary, because a GUI subsystem process
 * is not captured by every shell even once it has attached to the parent
 * console: PowerShell reads nothing from one, which is why the same command
 * worked under cmd and looked silent under pwsh. */
static BOOL prints_and_exits(const wchar_t *tail) {
    while (*tail == L' ' || *tail == L'\t') tail++;
    if (*tail == L'+') {
        tail++;
        while (*tail == L' ' || *tail == L'\t') tail++;  /* kitty + open is kitty +open */
        return !token_is(tail, L"open");
    }
    return token_is(tail, L"--version") || token_is(tail, L"-v")
        || token_is(tail, L"--help") || token_is(tail, L"-h");
}
#endif

int wmain(void) {
    wchar_t exe[MAX_PATH + 64] = {0};
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return 112;
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (!slash) return 112;
    *slash = 0;                       /* ...\bin */
    slash = wcsrchr(exe, L'\\');
    if (!slash) return 112;
    *slash = 0;                       /* install root */

    /* Reuse our own command line with the program name swapped: everything
     * after the first argument is forwarded verbatim. */
    const wchar_t *cl = GetCommandLineW();
    const wchar_t *rest = cl;
    if (*rest == L'"') { rest++; while (*rest && *rest != L'"') rest++; if (*rest) rest++; }
    else { while (*rest && *rest != L' ' && *rest != L'\t') rest++; }

#ifdef SHIM_WAIT
    const BOOL wait_for_child = TRUE;
    append(exe, MAX_PATH + 64, TARGET_RELPATH);
#else
    const BOOL wait_for_child = prints_and_exits(rest);
    append(exe, MAX_PATH + 64, wait_for_child ? TARGET_CONSOLE_RELPATH : TARGET_RELPATH);
#endif

    static wchar_t cmdline[32768];
    cmdline[0] = L'"'; cmdline[1] = 0;
    append(cmdline, 32768, exe);
    append(cmdline, 32768, L"\"");
    append(cmdline, 32768, rest);

    STARTUPINFOW si = { .cb = sizeof(si) };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(exe, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) return 113;
    DWORD code = 0;
    if (wait_for_child) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}
