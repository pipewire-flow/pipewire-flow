/* SPDX-License-Identifier: MIT */

#include <dlfcn.h>
#include <string.h>

#include "tpw_log_internal.h"

/* This file fills the table, so it needs the real names, not the redirects. */
#define TPW_PW_DL_NO_REDIRECT
#include "tpw_pw_dl.h"

/* The runtime name every libpipewire-0.3 since 0.3.0 installs under. */
#define TPW_PW_LIBRARY "libpipewire-0.3.so.0"

struct tpw_pw_fns tpw_pw_fns;

static void* g_pw_handle;

int tpw_pw_dl_open(void)
{
    g_pw_handle = dlopen(TPW_PW_LIBRARY, RTLD_NOW | RTLD_LOCAL);
    if (!g_pw_handle) {
        tpw_log_error("cannot load %s: %s", TPW_PW_LIBRARY, dlerror());
        return -1;
    }

    /* Stored through a void* lvalue, the POSIX way to turn dlsym()'s result
     * into a function pointer. */
#define TPW_PW_FN_RESOLVE(name)                                                     \
    *(void**)&tpw_pw_fns.name = dlsym(g_pw_handle, #name);                          \
    if (!tpw_pw_fns.name) {                                                         \
        tpw_log_error("%s lacks %s; PipeWire 0.3.50 or later is needed", TPW_PW_LIBRARY, #name); \
        tpw_pw_dl_close();                                                          \
        return -1;                                                                  \
    }
    TPW_PW_FUNCS(TPW_PW_FN_RESOLVE)
#undef TPW_PW_FN_RESOLVE

    return 0;
}

void tpw_pw_dl_close(void)
{
    memset(&tpw_pw_fns, 0, sizeof(tpw_pw_fns));
    if (g_pw_handle) {
        dlclose(g_pw_handle);
        g_pw_handle = NULL;
    }
}
