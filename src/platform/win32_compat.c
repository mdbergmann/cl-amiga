/*
 * Windows implementations of the POSIX shims declared in win32_compat.h.
 * See that header for why these exist.
 */

#include "win32_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <io.h>
#include <fcntl.h>
#include <string.h>

/* dlerror() reports the failure of the LAST dl* call on this thread, so the
 * message buffer is thread-local like errno — two MP threads loading foreign
 * libraries concurrently must not overwrite each other's diagnostic. */
static __thread char dl_errbuf[256];
static __thread int  dl_err_pending = 0;

static void dl_set_error(const char *what, const char *name)
{
    DWORD code = GetLastError();
    char msg[160];
    DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
                             FORMAT_MESSAGE_IGNORE_INSERTS,
                             NULL, code,
                             MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                             msg, (DWORD)sizeof(msg), NULL);
    if (n == 0)
        snprintf(msg, sizeof(msg), "Win32 error %lu", (unsigned long)code);
    else {
        /* FormatMessage terminates with CRLF — trim it, the caller prints
         * the message inline in a larger sentence. */
        while (n > 0 && (msg[n - 1] == '\r' || msg[n - 1] == '\n'))
            msg[--n] = '\0';
    }
    snprintf(dl_errbuf, sizeof(dl_errbuf), "%s(\"%s\"): %s",
             what, name ? name : "", msg);
    dl_err_pending = 1;
}

void *cl_win_dlopen(const char *name, int flags)
{
    HMODULE h;
    (void)flags;
    /* dlopen(NULL) means "the running program" — GetModuleHandle(NULL) is
     * the exact counterpart, and must NOT be FreeLibrary'd later, which is
     * why cl_win_dlclose skips it. */
    if (!name) {
        h = GetModuleHandleA(NULL);
        if (!h) dl_set_error("GetModuleHandleA", NULL);
        return (void *)h;
    }
    /* LoadLibraryA takes Windows separators; a caller may hand us a
     * Lisp-side path using '/', which LoadLibrary also accepts, so no
     * rewriting is needed here. */
    h = LoadLibraryA(name);
    if (!h) {
        dl_set_error("LoadLibraryA", name);
        return NULL;
    }
    return (void *)h;
}

void *cl_win_dlsym(void *lib, const char *name)
{
    FARPROC p;
    HMODULE h = (HMODULE)lib;
    if (!h) {
        /* dlsym(RTLD_DEFAULT, ...): search the process's already-loaded
         * modules.  Windows has no global symbol namespace, so approximate
         * it with the executable itself plus the C runtime — enough for the
         * "call a libc function without naming a library" idiom that CFFI's
         * default-library lookups rely on. */
        static const char *const fallbacks[] = { NULL, "ucrtbase.dll",
                                                 "msvcrt.dll", "kernel32.dll" };
        size_t i;
        for (i = 0; i < sizeof(fallbacks) / sizeof(fallbacks[0]); i++) {
            HMODULE m = fallbacks[i] ? GetModuleHandleA(fallbacks[i])
                                     : GetModuleHandleA(NULL);
            if (!m) continue;
            p = GetProcAddress(m, name);
            if (p) return (void *)(void (*)(void))p;
        }
        snprintf(dl_errbuf, sizeof(dl_errbuf),
                 "dlsym(RTLD_DEFAULT, \"%s\"): symbol not found in the "
                 "executable or the C runtime", name ? name : "");
        dl_err_pending = 1;
        return NULL;
    }
    p = GetProcAddress(h, name);
    if (!p) {
        dl_set_error("GetProcAddress", name);
        return NULL;
    }
    return (void *)(void (*)(void))p;
}

int cl_win_dlclose(void *lib)
{
    HMODULE h = (HMODULE)lib;
    if (!h) return 0;
    if (h == GetModuleHandleA(NULL))
        return 0;                      /* never unload the program itself */
    return FreeLibrary(h) ? 0 : -1;
}

const char *cl_win_dlerror(void)
{
    /* POSIX contract: the error is consumed by the read (a second call with
     * no intervening failure returns NULL).  platform_ffi_dlsym relies on
     * this to tell "symbol resolved to NULL" from "lookup failed". */
    if (!dl_err_pending)
        return NULL;
    dl_err_pending = 0;
    return dl_errbuf;
}

/* ---- setenv / unsetenv ----
 * Note the one place Windows cannot match POSIX: an EMPTY value deletes the
 * variable instead of defining it as "".  Nothing in clamiga sets an empty
 * environment variable, and the alternative (leaving the CRT and Win32 views
 * disagreeing) would be worse. */
int cl_win_setenv(const char *name, const char *value, int overwrite)
{
    if (!name || !*name || strchr(name, '=')) return -1;
    if (!overwrite && getenv(name) != NULL) return 0;
    if (_putenv_s(name, value ? value : "") != 0) return -1;
    SetEnvironmentVariableA(name, (value && *value) ? value : NULL);
    return 0;
}

int cl_win_unsetenv(const char *name)
{
    if (!name || !*name || strchr(name, '=')) return -1;
    _putenv_s(name, "");
    SetEnvironmentVariableA(name, NULL);
    return 0;
}

void *cl_win_memmem(const void *hay, size_t haylen,
                    const void *needle, size_t needlelen)
{
    const unsigned char *h = (const unsigned char *)hay;
    const unsigned char *n = (const unsigned char *)needle;
    size_t i;
    if (needlelen == 0) return (void *)h;
    if (haylen < needlelen) return NULL;
    for (i = 0; i + needlelen <= haylen; i++) {
        if (h[i] == n[0] && memcmp(h + i, n, needlelen) == 0)
            return (void *)(h + i);
    }
    return NULL;
}

int cl_win_pipe(int fds[2])
{
    return _pipe(fds, 65536, _O_BINARY);
}
