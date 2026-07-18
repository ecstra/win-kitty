/*
 * wincompat/dirent.h — POSIX directory iteration over the Win32 FindFirstFile
 * API. MinGW ships a <dirent.h> but its struct dirent has no d_type, which
 * dnd.c relies on, so this replaces it entirely (only dnd.c uses dirent).
 */
#pragma once
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8
#define DT_LNK     10

struct dirent {
    long          d_ino;
    unsigned char d_type;
    char          d_name[260];
};

typedef struct {
    HANDLE          handle;
    WIN32_FIND_DATAW data;
    struct dirent   entry;
    int             first;
} DIR;

static inline DIR *
opendir(const char *name) {
    char pattern[520];
    snprintf(pattern, sizeof pattern, "%s\\*", name);
    wchar_t wpattern[520];
    if (MultiByteToWideChar(CP_UTF8, 0, pattern, -1, wpattern, 520) == 0) return NULL;
    DIR *d = (DIR*) calloc(1, sizeof(DIR));
    if (!d) return NULL;
    d->handle = FindFirstFileW(wpattern, &d->data);
    if (d->handle == INVALID_HANDLE_VALUE) { free(d); return NULL; }
    d->first = 1;
    return d;
}

static inline struct dirent *
readdir(DIR *d) {
    if (!d) return NULL;
    if (!d->first && !FindNextFileW(d->handle, &d->data)) return NULL;
    d->first = 0;
    WideCharToMultiByte(CP_UTF8, 0, d->data.cFileName, -1, d->entry.d_name, sizeof d->entry.d_name, NULL, NULL);
    d->entry.d_type = (d->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;
    return &d->entry;
}

static inline int
closedir(DIR *d) {
    if (!d) return -1;
    if (d->handle != INVALID_HANDLE_VALUE) FindClose(d->handle);
    free(d);
    return 0;
}
