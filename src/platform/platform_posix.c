#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *platform_alloc(unsigned long size)
{
    void *p = malloc((size_t)size);
    if (p) {
        memset(p, 0, (size_t)size);
    }
    return p;
}

void platform_free(void *ptr)
{
    free(ptr);
}

void platform_write_string(const char *str)
{
    fputs(str, stdout);
    fflush(stdout);
}

int platform_read_line(char *buf, int bufsize)
{
    if (!fgets(buf, bufsize, stdin))
        return 0;
    /* Strip trailing newline */
    {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n')
            buf[len - 1] = '\0';
    }
    return 1;
}

int platform_getchar(void)
{
    return getchar();
}

void platform_ungetchar(int ch)
{
    ungetc(ch, stdin);
}

void platform_init(void)
{
    /* Nothing needed on POSIX */
}

void platform_shutdown(void)
{
    /* Nothing needed on POSIX */
}
