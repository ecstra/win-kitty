Tools to create and manage shared memory (POSIX Shared memory) across all Unix
variants. Pure Go, no external dependencies. Implements Go versions of
shm_open() and shm_unlink() that interoperate with the libc versions.


Shared memory object names always start with a single leading slash, as
mandated by POSIX for portable names, on every platform. Functions that take a
name accept it with or without the leading slash.
