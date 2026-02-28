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

char *platform_file_read(const char *path, unsigned long *size_out)
{
    FILE *f;
    long fsize;
    char *buf;

    *size_out = 0;
    f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0) { fclose(f); return NULL; }

    buf = (char *)malloc((size_t)fsize + 1);
    if (!buf) { fclose(f); return NULL; }

    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[fsize] = '\0';
    *size_out = (unsigned long)fsize;
    fclose(f);
    return buf;
}

void platform_init(void)
{
    /* Nothing needed on POSIX */
}

void platform_shutdown(void)
{
    /* Nothing needed on POSIX */
}
