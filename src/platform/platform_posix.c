#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <glob.h>

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

void platform_drain_input(void)
{
    /* No-op on POSIX — stdin doesn't have residual CLI data */
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

/* --- Handle-based file I/O --- */

#define PLATFORM_FILE_TABLE_SIZE 64

static FILE *file_table[PLATFORM_FILE_TABLE_SIZE];
static int file_table_init = 0;

static void file_table_ensure_init(void)
{
    if (!file_table_init) {
        int i;
        for (i = 0; i < PLATFORM_FILE_TABLE_SIZE; i++)
            file_table[i] = NULL;
        file_table_init = 1;
    }
}

PlatformFile platform_file_open(const char *path, int mode)
{
    FILE *f;
    const char *fmode;
    int i;

    file_table_ensure_init();

    switch (mode) {
    case PLATFORM_FILE_READ:   fmode = "r";  break;
    case PLATFORM_FILE_WRITE:  fmode = "w";  break;
    case PLATFORM_FILE_APPEND: fmode = "a";  break;
    default: return PLATFORM_FILE_INVALID;
    }

    f = fopen(path, fmode);
    if (!f) return PLATFORM_FILE_INVALID;

    /* Find free slot (slot 0 is reserved as INVALID) */
    for (i = 1; i < PLATFORM_FILE_TABLE_SIZE; i++) {
        if (file_table[i] == NULL) {
            file_table[i] = f;
            return (PlatformFile)i;
        }
    }

    /* No free slots */
    fclose(f);
    return PLATFORM_FILE_INVALID;
}

void platform_file_close(PlatformFile fh)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh]) {
        fclose(file_table[fh]);
        file_table[fh] = NULL;
    }
}

int platform_file_getchar(PlatformFile fh)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh])
        return fgetc(file_table[fh]);
    return -1;
}

int platform_file_write_string(PlatformFile fh, const char *str)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh])
        return fputs(str, file_table[fh]) >= 0 ? 0 : -1;
    return -1;
}

int platform_file_write_char(PlatformFile fh, int ch)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh])
        return fputc(ch, file_table[fh]) != EOF ? 0 : -1;
    return -1;
}

int platform_file_eof(PlatformFile fh)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh])
        return feof(file_table[fh]) ? 1 : 0;
    return 1;
}

long platform_file_position(PlatformFile fh)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh])
        return ftell(file_table[fh]);
    return -1;
}

int platform_file_set_position(PlatformFile fh, long pos)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh])
        return fseek(file_table[fh], pos, SEEK_SET) == 0 ? 0 : -1;
    return -1;
}

long platform_file_length(PlatformFile fh)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh]) {
        long cur = ftell(file_table[fh]);
        long end;
        fseek(file_table[fh], 0, SEEK_END);
        end = ftell(file_table[fh]);
        fseek(file_table[fh], cur, SEEK_SET);
        return end;
    }
    return -1;
}

uint32_t platform_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
}

void platform_sleep_ms(uint32_t milliseconds)
{
    if (milliseconds > 0)
        usleep(milliseconds * 1000);
}

uint32_t platform_universal_time(void)
{
    /* CL universal time: seconds since 1900-01-01 00:00:00 UTC
     * Unix epoch: 1970-01-01 00:00:00 UTC
     * Difference: 70 years = 2208988800 seconds */
    time_t t = time(NULL);
    return (uint32_t)((unsigned long)t + 2208988800UL);
}

int platform_file_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) ? 1 : 0;
}

int platform_file_is_directory(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

int platform_file_delete(const char *path)
{
    return (unlink(path) == 0) ? 0 : -1;
}

int platform_file_rename(const char *oldpath, const char *newpath)
{
    return (rename(oldpath, newpath) == 0) ? 0 : -1;
}

uint32_t platform_file_mtime(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    /* Convert Unix time_t to CL universal time */
    return (uint32_t)((unsigned long)st.st_mtime + 2208988800UL);
}

int platform_mkdir(const char *path)
{
    return (mkdir(path, 0755) == 0 || errno == EEXIST) ? 0 : -1;
}

const char *platform_getenv(const char *name, char *buf, int bufsize)
{
    (void)buf; (void)bufsize;
    return getenv(name);
}

int platform_getcwd(char *buf, int bufsize)
{
    if (getcwd(buf, (size_t)bufsize) != NULL)
        return (int)strlen(buf);
    return 0;
}

int platform_system(const char *command)
{
    int status = system(command);
    if (status == -1) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

char **platform_directory(const char *pattern, int *count_out)
{
    glob_t g;
    char **result;
    int i;

    *count_out = 0;
    if (glob(pattern, 0, NULL, &g) != 0) {
        return NULL;
    }
    result = (char **)malloc(((size_t)g.gl_pathc + 1) * sizeof(char *));
    if (!result) { globfree(&g); return NULL; }
    for (i = 0; i < (int)g.gl_pathc; i++) {
        result[i] = strdup(g.gl_pathv[i]);
    }
    result[g.gl_pathc] = NULL;
    *count_out = (int)g.gl_pathc;
    globfree(&g);
    return result;
}

void platform_init(void)
{
    /* Nothing needed on POSIX */
}

void platform_shutdown(void)
{
    /* Nothing needed on POSIX */
}
