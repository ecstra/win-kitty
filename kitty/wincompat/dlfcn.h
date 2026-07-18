/*
 * wincompat/dlfcn.h — POSIX dynamic loader mapped onto the Win32 loader.
 * dlopen->LoadLibrary, dlsym->GetProcAddress, dlclose->FreeLibrary.
 */
#pragma once
#include <windows.h>
#include <stdint.h>

#define RTLD_LAZY   0
#define RTLD_NOW    0
#define RTLD_GLOBAL 0
#define RTLD_LOCAL  0
#define RTLD_DEFAULT ((void *)0)

static inline __attribute__((unused)) void *
dlopen(const char *filename, int flags) {
    (void)flags;
    return filename ? (void *)LoadLibraryA(filename) : (void *)GetModuleHandle(NULL);
}

static inline __attribute__((unused)) void *
dlsym(void *handle, const char *symbol) {
    return (void *)(uintptr_t)GetProcAddress((HMODULE)handle, symbol);
}

static inline __attribute__((unused)) int
dlclose(void *handle) {
    return FreeLibrary((HMODULE)handle) ? 0 : -1;
}

static inline __attribute__((unused)) char *
dlerror(void) { return NULL; }
