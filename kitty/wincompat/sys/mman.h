/*
 * wincompat/sys/mman.h — minimal POSIX mmap surface for the Windows port.
 *
 * Declarations + constants only; the bodies are implemented over
 * CreateFileMapping / MapViewOfFile in wincompat.c (later stage). Enough for
 * the files that include <sys/mman.h> to compile.
 */
#pragma once
#include <sys/types.h>
#include <stddef.h>

#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_FAILED    ((void *) -1)

#define MS_ASYNC      0x1
#define MS_INVALIDATE 0x2
#define MS_SYNC       0x4

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int   munmap(void *addr, size_t length);
int   msync(void *addr, size_t length, int flags);
int   mlock(const void *addr, size_t len);
int   munlock(const void *addr, size_t len);

/* POSIX shared memory (kitty's disk-cache / graphics use these). Implemented
 * over CreateFileMapping in a later stage; declared here so callers compile. */
int   shm_open(const char *name, int oflag, int mode);
int   shm_unlink(const char *name);
