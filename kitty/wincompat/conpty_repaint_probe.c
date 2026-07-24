/*
 * conpty_repaint_probe.c - measure what a ConPTY emits for one line edit.
 *
 * Written for the pwsh cursor jumping and flicker investigation, see
 * docs/handover/pwsh-cursor.md. Spawns a shell on a pseudoconsole, types a
 * scripted sequence, and dumps every read from the output pipe with a
 * timestamp, so a logical redraw that conhost splits across frames is visible
 * as two dated writes rather than one.
 *
 * What it found, on Windows 11 build 26200:
 *
 *   - pwsh splits every PSReadLine repaint in two. The first write is the
 *     cursor hide on its own, the second carries the text and the cursor show.
 *     Over 80 repaints at held-key rate the gap was min 8.27ms, mean 9.70ms,
 *     max 12.85ms.
 *   - cmd does not split at all. It writes one byte per keystroke and
 *     "\x08 \x08" per backspace, and never touches cursor visibility.
 *   - PSEUDOCONSOLE_PASSTHROUGH_MODE changes none of this. Output at flags=8 is
 *     byte for byte identical to flags=0.
 *
 * Usage: conpty_repaint_probe.exe <flags> <shell> <outfile> [key_ms] [cols] [rows]
 *   flags:  0 = default, 8 = PSEUDOCONSOLE_PASSTHROUGH_MODE
 *   key_ms: gap between synthesised keystrokes (30 approximates a held key)
 *
 * Build: gcc conpty_repaint_probe.c -o conpty_repaint_probe.exe
 */
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef VOID* HPCON;
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif
typedef HRESULT (WINAPI *PFN_CreatePseudoConsole)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
typedef VOID    (WINAPI *PFN_ClosePseudoConsole)(HPCON);

#define MAX_CHUNKS 4096
typedef struct { double t; DWORD n; unsigned char buf[8192]; } Chunk;
static Chunk chunks[MAX_CHUNKS];
static volatile LONG nchunks = 0;
static HANDLE out_read = NULL;
static LARGE_INTEGER freq, t0;

static double
now_ms(void) {
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
}

static DWORD WINAPI
pump_output(LPVOID unused) {
    (void)unused;
    for (;;) {
        LONG i = nchunks;
        if (i >= MAX_CHUNKS) break;
        DWORD n = 0;
        if (!ReadFile(out_read, chunks[i].buf, sizeof chunks[i].buf, &n, NULL) || n == 0) break;
        chunks[i].t = now_ms();
        chunks[i].n = n;
        InterlockedIncrement(&nchunks);
    }
    return 0;
}

/* Render bytes so control sequences are legible: ESC as \e, other controls hex. */
static void
dump_escaped(FILE *f, const unsigned char *p, DWORD n) {
    for (DWORD i = 0; i < n; i++) {
        unsigned char c = p[i];
        if (c == 0x1b) fputs("\\e", f);
        else if (c == '\r') fputs("\\r", f);
        else if (c == '\n') fputs("\\n", f);
        else if (c == '\\') fputs("\\\\", f);
        else if (c >= 0x20 && c < 0x7f) fputc(c, f);
        else fprintf(f, "\\x%02x", c);
    }
}

static void
type_key(HANDLE h, const char *s, int settle_ms) {
    DWORD w = 0;
    WriteFile(h, s, (DWORD)strlen(s), &w, NULL);
    Sleep(settle_ms);
}

int
main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s <flags> <shell> <outfile> [key_ms] [cols] [rows]\n", argv[0]); return 64; }
    DWORD flags = (DWORD)strtoul(argv[1], NULL, 0);
    const char *shell = argv[2];
    const char *outfile = argv[3];
    int key_ms = argc > 4 ? atoi(argv[4]) : 120;
    int cols   = argc > 5 ? atoi(argv[5]) : 100;
    int rows   = argc > 6 ? atoi(argv[6]) : 30;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    HANDLE in_read = NULL, in_write = NULL, out_write = NULL;
    if (!CreatePipe(&in_read, &in_write, NULL, 0) || !CreatePipe(&out_read, &out_write, NULL, 0)) {
        fprintf(stderr, "CreatePipe failed: %lu\n", GetLastError()); return 1;
    }

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    PFN_CreatePseudoConsole pCreate = (PFN_CreatePseudoConsole)(void*)GetProcAddress(k32, "CreatePseudoConsole");
    PFN_ClosePseudoConsole  pClose  = (PFN_ClosePseudoConsole)(void*)GetProcAddress(k32, "ClosePseudoConsole");
    if (!pCreate) { fprintf(stderr, "ConPTY unavailable\n"); return 2; }

    HPCON hpc = NULL;
    COORD size = { (SHORT)cols, (SHORT)rows };
    HRESULT hr = pCreate(size, in_read, out_write, flags, &hpc);
    if (FAILED(hr)) {
        fprintf(stderr, "CreatePseudoConsole(flags=0x%lx) failed: hr=0x%08lx\n",
                (unsigned long)flags, (unsigned long)hr);
        return 3;
    }
    fprintf(stderr, "CreatePseudoConsole(flags=0x%lx) OK\n", (unsigned long)flags);
    CloseHandle(in_read); CloseHandle(out_write);

    /* Windows hands a child the parent's standard handles when
     * STARTF_USESTDHANDLES is not set, and that beats the pseudoconsole: the
     * child would attach to our console instead of the pty. Detach the console
     * and blank the handles so the pseudoconsole is the only stdio on offer.
     * This mirrors what kitty's spawn() does in kitty/child.c. */
    fflush(stderr);
    if (GetConsoleWindow()) FreeConsole();
    SetStdHandle(STD_INPUT_HANDLE, NULL);
    SetStdHandle(STD_OUTPUT_HANDLE, NULL);
    SetStdHandle(STD_ERROR_HANDLE, NULL);

    STARTUPINFOEXA si; ZeroMemory(&si, sizeof si); si.StartupInfo.cb = sizeof si;
    SIZE_T attr_bytes = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_bytes);
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attr_bytes);
    if (!si.lpAttributeList ||
        !InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_bytes) ||
        !UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   hpc, sizeof hpc, NULL, NULL)) {
        fprintf(stderr, "attr setup failed: %lu\n", GetLastError()); return 4;
    }

    char cmdline[1024];
    snprintf(cmdline, sizeof cmdline, "%s", shell);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof pi);
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                        NULL, NULL, &si.StartupInfo, &pi)) {
        fprintf(stderr, "CreateProcessA failed: %lu\n", GetLastError()); return 5;
    }

    HANDLE reader = CreateThread(NULL, 0, pump_output, NULL, 0, NULL);

    /* Let the shell start and paint its first prompt. */
    Sleep(2500);
    LONG mark_prompt = nchunks;

    /* Type a word one keystroke at a time, then erase it one backspace at a
     * time. Each keystroke is one logical redraw of the line editor. */
    const char *word = "abcdefghijklmnopqrstuvwxyz";
    for (const char *c = word; *c; c++) { char s[2] = { *c, 0 }; type_key(in_write, s, key_ms); }
    LONG mark_typed = nchunks;
    for (size_t i = 0; i < strlen(word); i++) type_key(in_write, "\x7f", key_ms);
    LONG mark_erased = nchunks;
    /* Cursor movement across an existing line: left to the start, then right. */
    for (const char *c = word; *c; c++) { char s[2] = { *c, 0 }; type_key(in_write, s, key_ms); }
    for (size_t i = 0; i < strlen(word); i++) type_key(in_write, "\x1b[D", key_ms);
    for (size_t i = 0; i < strlen(word); i++) type_key(in_write, "\x1b[C", key_ms);
    LONG mark_erased2 = nchunks;

    type_key(in_write, "\x03", 300);   /* Ctrl-C to drop the line */
    type_key(in_write, "exit\r", 400);

    WaitForSingleObject(pi.hProcess, 3000);
    if (pClose) pClose(hpc);
    WaitForSingleObject(reader, 1500);

    FILE *f = fopen(outfile, "wb");
    if (!f) { fprintf(stderr, "cannot open %s\n", outfile); return 6; }
    fprintf(f, "# flags=0x%lx shell=%s chunks=%ld\n", (unsigned long)flags, shell, (long)nchunks);
    fprintf(f, "# marks: prompt_done=%ld typed=%ld erased=%ld moved=%ld key_ms=%d size=%dx%d\n",
            (long)mark_prompt, (long)mark_typed, (long)mark_erased, (long)mark_erased2,
            key_ms, cols, rows);

    /* A chunk that ends with the cursor hidden is a half-delivered repaint: a
     * terminal that renders on it paints a frame with no cursor. Measure how
     * long the rest of that repaint takes to arrive, since that is the window
     * kitty's input_delay would have to cover. */
    int splits = 0; double gmin = 1e9, gmax = 0, gsum = 0;
    double gaps[MAX_CHUNKS]; int ngaps = 0;
    for (LONG i = mark_prompt; i + 1 < nchunks; i++) {
        const char *hide = NULL, *show = NULL;
        for (DWORD j = 0; j + 5 < chunks[i].n; j++) {
            if (memcmp(chunks[i].buf + j, "\x1b[?25l", 6) == 0) hide = (const char*)chunks[i].buf + j;
            if (memcmp(chunks[i].buf + j, "\x1b[?25h", 6) == 0) show = (const char*)chunks[i].buf + j;
        }
        if (hide && (!show || show < hide)) {
            double g = chunks[i + 1].t - chunks[i].t;
            gaps[ngaps++] = g; splits++; gsum += g;
            if (g < gmin) gmin = g;
            if (g > gmax) gmax = g;
        }
    }
    fprintf(f, "# split repaints: %d", splits);
    if (splits) {
        int over3 = 0, over10 = 0, over20 = 0;
        for (int i = 0; i < ngaps; i++) {
            if (gaps[i] > 3) over3++;
            if (gaps[i] > 10) over10++;
            if (gaps[i] > 20) over20++;
        }
        fprintf(f, "  gap ms: min=%.2f mean=%.2f max=%.2f  >3ms=%d >10ms=%d >20ms=%d",
                gmin, gsum / splits, gmax, over3, over10, over20);
    }
    fputc('\n', f);

    for (LONG i = 0; i < nchunks; i++) {
        fprintf(f, "[%4ld] %9.2fms %5lu bytes: ", (long)i, chunks[i].t, (unsigned long)chunks[i].n);
        dump_escaped(f, chunks[i].buf, chunks[i].n);
        fputc('\n', f);
    }
    fclose(f);
    fprintf(stderr, "wrote %s (%ld chunks)\n", outfile, (long)nchunks);
    return 0;
}
