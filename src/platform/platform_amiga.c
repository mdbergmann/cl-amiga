#include "platform.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

void *platform_alloc(unsigned long size)
{
    void *p = AllocVec(size, MEMF_CLEAR);
    return p;
}

void platform_free(void *ptr)
{
    if (ptr) {
        FreeVec(ptr);
    }
}

void platform_write_string(const char *str)
{
    BPTR out = Output();
    if (out) {
        Write(out, (APTR)str, strlen(str));
    }
}

int platform_read_line(char *buf, int bufsize)
{
    BPTR in = Input();
    if (!in)
        return 0;
    if (!FGets(in, buf, bufsize))
        return 0;
    /* Strip trailing newline */
    {
        int len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n')
            buf[len - 1] = '\0';
    }
    return 1;
}

int platform_getchar(void)
{
    BPTR in = Input();
    if (!in)
        return -1;
    return FGetC(in);
}

void platform_ungetchar(int ch)
{
    BPTR in = Input();
    if (in) {
        UnGetC(in, ch);
    }
}

void platform_init(void)
{
    /* Nothing needed — dos.library is auto-opened by vbcc startup */
}

void platform_shutdown(void)
{
    /* Nothing needed */
}
