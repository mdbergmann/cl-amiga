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

char *platform_file_read(const char *path, unsigned long *size_out)
{
    BPTR fh;
    LONG fsize, nread;
    char *buf;

    *size_out = 0;
    fh = Open((STRPTR)path, MODE_OLDFILE);
    if (!fh) return NULL;

    /* Seek to end to get size */
    Seek(fh, 0, OFFSET_END);
    fsize = Seek(fh, 0, OFFSET_BEGINNING);

    if (fsize <= 0) { Close(fh); return NULL; }

    buf = (char *)AllocVec(fsize + 1, MEMF_CLEAR);
    if (!buf) { Close(fh); return NULL; }

    nread = Read(fh, buf, fsize);
    Close(fh);

    if (nread != fsize) {
        FreeVec(buf);
        return NULL;
    }
    buf[fsize] = '\0';
    *size_out = (unsigned long)fsize;
    return buf;
}

uint32_t platform_time_ms(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    /* ds_Minute: minutes since midnight, ds_Tick: 1/50s ticks since last minute */
    return (uint32_t)(ds.ds_Minute * 60000UL + ds.ds_Tick * 20UL);
}

void platform_init(void)
{
    /* Nothing needed — dos.library is auto-opened by vbcc startup */
}

void platform_shutdown(void)
{
    /* Nothing needed */
}
