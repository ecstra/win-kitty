/*
 * wincompat/sys/un.h — struct sockaddr_un for AF_UNIX sockets, which Windows
 * has supported since Windows 10 1803. Prefer the SDK's <afunix.h>; fall back
 * to a local definition on older MinGW headers.
 */
#pragma once
#include <winsock2.h>

#if defined(__has_include)
#  if __has_include(<afunix.h>)
#    include <afunix.h>
#    define KITTY_HAVE_AFUNIX 1
#  endif
#endif

#ifndef KITTY_HAVE_AFUNIX
#  ifndef UNIX_PATH_MAX
#    define UNIX_PATH_MAX 108
#  endif
struct sockaddr_un {
    ADDRESS_FAMILY sun_family;          /* AF_UNIX */
    char           sun_path[UNIX_PATH_MAX];
};
#endif
