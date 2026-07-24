// systemd.c is Linux only and excluded from the Windows build. Provide its
// module init as a no-op so the extension links. See systemd.c for the real one.
#include "../data-types.h"

bool init_systemd_module(PyObject *m) { (void) m; return true; }
