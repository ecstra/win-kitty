/*
 * wincompat/pwd.h — minimal <pwd.h> replacement for the Windows port of kitty.
 *
 * Windows has no /etc/passwd. getpwuid/getpwnam are declared here and given a
 * best-effort implementation (current user via GetUserName + known folders) in
 * wincompat/wincompat.c. Declarations only for now so launcher/utils.h compiles.
 */
#pragma once
#include "win_prelude.h"   /* uid_t, gid_t */
#ifdef __cplusplus
extern "C" {
#endif

struct passwd {
    char  *pw_name;    /* user name */
    char  *pw_passwd;  /* always "x" on Windows */
    uid_t  pw_uid;
    gid_t  pw_gid;
    char  *pw_gecos;   /* real name */
    char  *pw_dir;     /* home dir (USERPROFILE) */
    char  *pw_shell;   /* COMSPEC */
};

struct passwd *getpwuid(uid_t uid);
struct passwd *getpwnam(const char *name);

#ifdef __cplusplus
}
#endif
