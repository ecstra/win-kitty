/*
 * wincompat/sys/wait.h — the sliver of Unix process-wait kitty uses, mapped for
 * Windows. A child's "status" is just its exit code (no signals), so the W*
 * decoders collapse accordingly. waitpid() is implemented over the process
 * HANDLE (WaitForSingleObject + GetExitCodeProcess) in a later stage.
 */
#pragma once
#include <sys/types.h>

#define WNOHANG   1
#define WUNTRACED 2

#define WIFEXITED(s)    (1)
#define WEXITSTATUS(s)  ((s) & 0xff)
#define WIFSIGNALED(s)  (0)
#define WTERMSIG(s)     (0)
#define WIFSTOPPED(s)   (0)
#define WSTOPSIG(s)     (0)
#define WIFCONTINUED(s) (0)
#define WCOREDUMP(s)    (0)

pid_t waitpid(pid_t pid, int *status, int options);
pid_t wait(int *status);
