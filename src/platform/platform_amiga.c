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

void platform_drain_input(void)
{
    /* AmigaOS CLI leaks command line text to Input() (stdin).
     * Drain any pending chars before the interactive REPL starts. */
    BPTR in = Input();
    if (!in) return;
    while (WaitForChar(in, 1000)) {  /* 1ms timeout per char */
        if (FGetC(in) < 0) break;
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

/* --- Handle-based file I/O --- */

/* On Amiga, BPTR is a LONG (32-bit). We store them directly as uint32_t.
 * Slot 0 is reserved as PLATFORM_FILE_INVALID. */

#define PLATFORM_FILE_TABLE_SIZE 64

static BPTR file_table[PLATFORM_FILE_TABLE_SIZE];
static int file_table_init = 0;

static void file_table_ensure_init(void)
{
    if (!file_table_init) {
        int i;
        for (i = 0; i < PLATFORM_FILE_TABLE_SIZE; i++)
            file_table[i] = 0;
        file_table_init = 1;
    }
}

PlatformFile platform_file_open(const char *path, int mode)
{
    BPTR fh;
    LONG amode;
    int i;

    file_table_ensure_init();

    switch (mode) {
    case PLATFORM_FILE_READ:   amode = MODE_OLDFILE;  break;
    case PLATFORM_FILE_WRITE:  amode = MODE_NEWFILE;  break;
    case PLATFORM_FILE_APPEND: amode = MODE_READWRITE; break;
    default: return PLATFORM_FILE_INVALID;
    }

    fh = Open((STRPTR)path, amode);
    if (!fh) return PLATFORM_FILE_INVALID;

    /* For append mode, seek to end */
    if (mode == PLATFORM_FILE_APPEND) {
        Seek(fh, 0, OFFSET_END);
    }

    /* Find free slot (slot 0 reserved) */
    for (i = 1; i < PLATFORM_FILE_TABLE_SIZE; i++) {
        if (file_table[i] == 0) {
            file_table[i] = fh;
            return (PlatformFile)i;
        }
    }

    Close(fh);
    return PLATFORM_FILE_INVALID;
}

void platform_file_close(PlatformFile fh)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh]) {
        Close(file_table[fh]);
        file_table[fh] = 0;
    }
}

int platform_file_getchar(PlatformFile fh)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh])
        return FGetC(file_table[fh]);
    return -1;
}

int platform_file_write_string(PlatformFile fh, const char *str)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh]) {
        LONG len = strlen(str);
        LONG written = Write(file_table[fh], (APTR)str, len);
        return (written == len) ? 0 : -1;
    }
    return -1;
}

int platform_file_write_char(PlatformFile fh, int ch)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh])
        return (FPutC(file_table[fh], ch) != -1) ? 0 : -1;
    return -1;
}

int platform_file_eof(PlatformFile fh)
{
    /* Amiga: no direct feof equivalent; we detect EOF via FGetC returning -1 */
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh])
        return 0;  /* Can't know without reading */
    return 1;
}

uint32_t platform_time_ms(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    /* ds_Minute: minutes since midnight, ds_Tick: 1/50s ticks since last minute */
    return (uint32_t)(ds.ds_Minute * 60000UL + ds.ds_Tick * 20UL);
}

void platform_sleep_ms(uint32_t milliseconds)
{
    /* Delay() takes ticks (1/50s = 20ms each). Round up. */
    LONG ticks = (LONG)((milliseconds + 19) / 20);
    if (ticks > 0)
        Delay(ticks);
}

uint32_t platform_universal_time(void)
{
    /* AmigaOS epoch: 1978-01-01 00:00:00 UTC
     * CL universal time epoch: 1900-01-01 00:00:00 UTC
     * Difference: 78 years = 2461449600 seconds
     *   (1900-1978: 78 years, 19 leap years: 78*365 + 19 = 28489 days * 86400) */
    struct DateStamp ds;
    uint32_t amiga_secs;
    DateStamp(&ds);
    amiga_secs = (uint32_t)(ds.ds_Days * 86400UL + ds.ds_Minute * 60UL + ds.ds_Tick / 50UL);
    return amiga_secs + 2461449600UL;
}

int platform_file_exists(const char *path)
{
    BPTR lock = Lock((STRPTR)path, ACCESS_READ);
    if (lock) {
        UnLock(lock);
        return 1;
    }
    return 0;
}

int platform_file_is_directory(const char *path)
{
    BPTR lock = Lock((STRPTR)path, ACCESS_READ);
    if (lock) {
        struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocVec(sizeof(struct FileInfoBlock), MEMF_CLEAR);
        int result = 0;
        if (fib) {
            if (Examine(lock, fib)) {
                result = (fib->fib_DirEntryType > 0) ? 1 : 0;
            }
            FreeVec(fib);
        }
        UnLock(lock);
        return result;
    }
    return 0;
}

int platform_file_delete(const char *path)
{
    return DeleteFile((STRPTR)path) ? 0 : -1;
}

int platform_file_rename(const char *oldpath, const char *newpath)
{
    return Rename((STRPTR)oldpath, (STRPTR)newpath) ? 0 : -1;
}

uint32_t platform_file_mtime(const char *path)
{
    BPTR lock = Lock((STRPTR)path, ACCESS_READ);
    if (lock) {
        struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocVec(sizeof(struct FileInfoBlock), MEMF_CLEAR);
        uint32_t result = 0;
        if (fib) {
            if (Examine(lock, fib)) {
                /* fib_Date is a DateStamp relative to Amiga epoch (1978-01-01) */
                uint32_t amiga_secs = (uint32_t)(fib->fib_Date.ds_Days * 86400UL
                    + fib->fib_Date.ds_Minute * 60UL
                    + fib->fib_Date.ds_Tick / 50UL);
                result = amiga_secs + 2461449600UL;
            }
            FreeVec(fib);
        }
        UnLock(lock);
        return result;
    }
    return 0;
}

int platform_mkdir(const char *path)
{
    BPTR lock;
    /* Check if it already exists */
    lock = Lock((STRPTR)path, ACCESS_READ);
    if (lock) {
        UnLock(lock);
        return 0; /* Already exists */
    }
    lock = CreateDir((STRPTR)path);
    if (lock) {
        UnLock(lock);
        return 0;
    }
    return -1;
}

void platform_init(void)
{
    /* Nothing needed — dos.library is auto-opened by vbcc startup */
}

void platform_shutdown(void)
{
    /* Nothing needed */
}
