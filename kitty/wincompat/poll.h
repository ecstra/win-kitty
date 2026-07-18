/*
 * wincompat/poll.h — POSIX <poll.h> for the Windows port of kitty.
 *
 * struct pollfd and the POLL* constants come from <winsock2.h> (pulled in by
 * win_prelude.h). Winsock ships WSAPoll(), not POSIX poll(), so we declare
 * poll() here; it is implemented in wincompat.c (Stage 4) over a mix of socket
 * and pipe handles via WaitForMultipleObjects.
 */
#pragma once
#include <winsock2.h>

#ifndef _NFDS_T_DEFINED
#define _NFDS_T_DEFINED
typedef unsigned long nfds_t;
#endif

/* Implemented in wincompat/wincompat.c (Stage 4). */
int poll(struct pollfd *fds, nfds_t nfds, int timeout);
