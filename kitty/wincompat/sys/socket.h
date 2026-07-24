/*
 * wincompat/sys/socket.h — POSIX socket API mapped onto Winsock2.
 *
 * socket/connect/bind/accept/struct sockaddr/socklen_t all come from
 * <winsock2.h>/<ws2tcpip.h> (already pulled in first by win_prelude.h).
 * NOTE: on Windows a socket is a SOCKET (not an int fd) and is closed with
 * closesocket(); kitty's int-fd assumptions are reconciled in Stage 4.
 */
#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>

#ifndef _SA_FAMILY_T_DEFINED
#define _SA_FAMILY_T_DEFINED
typedef ADDRESS_FAMILY sa_family_t;
#endif
