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

long platform_file_position(PlatformFile fh)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh])
        return (long)Seek(file_table[fh], 0, OFFSET_CURRENT);
    return -1;
}

int platform_file_set_position(PlatformFile fh, long pos)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh])
        return Seek(file_table[fh], pos, OFFSET_BEGINNING) >= 0 ? 0 : -1;
    return -1;
}

long platform_file_length(PlatformFile fh)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh]) {
        long cur = Seek(file_table[fh], 0, OFFSET_CURRENT);
        long end;
        Seek(file_table[fh], 0, OFFSET_END);
        end = Seek(file_table[fh], 0, OFFSET_CURRENT);
        Seek(file_table[fh], cur, OFFSET_BEGINNING);
        return end;
    }
    return -1;
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
    char clean[256];
    int len;

    /* Strip trailing slash — AmigaOS CreateDir fails with trailing '/' */
    len = strlen(path);
    if (len > 0 && len < (int)sizeof(clean)) {
        memcpy(clean, path, (size_t)len + 1);
        if (len > 1 && clean[len - 1] == '/')
            clean[len - 1] = '\0';
    } else {
        /* Path too long or empty */
        return -1;
    }

    /* Check if it already exists */
    lock = Lock((STRPTR)clean, ACCESS_READ);
    if (lock) {
        UnLock(lock);
        return 0; /* Already exists */
    }
    lock = CreateDir((STRPTR)clean);
    if (lock) {
        UnLock(lock);
        return 0;
    }
    return -1;
}

const char *platform_getenv(const char *name, char *buf, int bufsize)
{
    LONG len = GetVar((STRPTR)name, buf, bufsize, 0);
    if (len < 0) return NULL;
    return buf;
}

int platform_getcwd(char *buf, int bufsize)
{
    BPTR lock = Lock("", ACCESS_READ);
    if (lock) {
        if (NameFromLock(lock, (STRPTR)buf, bufsize)) {
            UnLock(lock);
            return (int)strlen(buf);
        }
        UnLock(lock);
    }
    return 0;
}

int platform_system(const char *command)
{
    LONG rc = SystemTagList((STRPTR)command, NULL);
    return (int)rc;
}

/* Convert Unix glob pattern to AmigaDOS pattern.
   '*' -> '#?', rest passed through. */
static void unix_to_amiga_pattern(const char *src, char *dst, int dstsize)
{
    int si = 0, di = 0;
    while (src[si] && di < dstsize - 3) {
        if (src[si] == '*') {
            dst[di++] = '#';
            dst[di++] = '?';
            si++;
        } else {
            dst[di++] = src[si++];
        }
    }
    dst[di] = '\0';
}

char **platform_directory(const char *pattern, int *count_out)
{
    struct AnchorPath *ap;
    char amiga_pat[512];
    char **result = NULL;
    int count = 0;
    int capacity = 16;
    LONG rc;

    *count_out = 0;

    unix_to_amiga_pattern(pattern, amiga_pat, (int)sizeof(amiga_pat));

    /* AnchorPath must be longword-aligned and zeroed */
    ap = (struct AnchorPath *)AllocVec(sizeof(struct AnchorPath) + 256,
                                       MEMF_CLEAR);
    if (!ap) return NULL;
    ap->ap_Strlen = 256; /* space for full path */

    result = (char **)malloc((size_t)(capacity + 1) * sizeof(char *));
    if (!result) { FreeVec(ap); return NULL; }

    rc = MatchFirst((STRPTR)amiga_pat, ap);
    while (rc == 0) {
        /* Skip directories — we want files matching the pattern */
        if (!(ap->ap_Info.fib_DirEntryType > 0)) {
            if (count >= capacity) {
                capacity *= 2;
                result = (char **)realloc(result,
                             (size_t)(capacity + 1) * sizeof(char *));
                if (!result) break;
            }
            result[count] = strdup((char *)ap->ap_Buf);
            count++;
        }
        rc = MatchNext(ap);
    }
    MatchEnd(ap);
    FreeVec(ap);

    if (result) {
        result[count] = NULL;
        *count_out = count;
    }
    return result;
}

const char *platform_realpath(const char *path, char *buf, int bufsize)
{
    BPTR lock = Lock((STRPTR)path, ACCESS_READ);
    if (!lock) return NULL;
    if (!NameFromLock(lock, (STRPTR)buf, (LONG)bufsize)) {
        UnLock(lock);
        return NULL;
    }
    UnLock(lock);
    return buf;
}

/* --- TCP Socket I/O via bsdsocket.library (AmiTCP/Roadshow/Miami) --- */

#include <proto/bsdsocket.h>

struct Library *SocketBase = NULL;

#define PLATFORM_SOCKET_TABLE_SIZE 16

static LONG socket_table[PLATFORM_SOCKET_TABLE_SIZE];
static int socket_table_init = 0;

static void socket_table_ensure_init(void)
{
    if (!socket_table_init) {
        int i;
        for (i = 0; i < PLATFORM_SOCKET_TABLE_SIZE; i++)
            socket_table[i] = -1;
        socket_table_init = 1;
    }
}

static LONG socket_errno = 0;

static int bsdsocket_open(void)
{
    if (SocketBase) return 1;
    SocketBase = OpenLibrary("bsdsocket.library", 3);
    if (!SocketBase) return 0;
    /* Required by some TCP stacks (AmiTCP) for per-task errno */
    SetErrnoPtr(&socket_errno, sizeof(socket_errno));
    return 1;
}

PlatformSocket platform_socket_connect(const char *host, int port)
{
    struct hostent *he;
    struct sockaddr_in addr;
    LONG fd;
    int i;

    socket_table_ensure_init();

    if (!bsdsocket_open())
        return PLATFORM_SOCKET_INVALID;

    he = gethostbyname((STRPTR)host);
    if (!he) return PLATFORM_SOCKET_INVALID;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return PLATFORM_SOCKET_INVALID;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        CloseSocket(fd);
        return PLATFORM_SOCKET_INVALID;
    }

    /* Find free slot (slot 0 reserved as INVALID) */
    for (i = 1; i < PLATFORM_SOCKET_TABLE_SIZE; i++) {
        if (socket_table[i] == -1) {
            socket_table[i] = fd;
            return (PlatformSocket)i;
        }
    }

    CloseSocket(fd);
    return PLATFORM_SOCKET_INVALID;
}

void platform_socket_close(PlatformSocket sh)
{
    if (sh > 0 && sh < PLATFORM_SOCKET_TABLE_SIZE && socket_table[sh] >= 0) {
        CloseSocket(socket_table[sh]);
        socket_table[sh] = -1;
    }
}

int platform_socket_read(PlatformSocket sh)
{
    unsigned char byte;
    LONG n;
    if (sh == 0 || sh >= PLATFORM_SOCKET_TABLE_SIZE || socket_table[sh] < 0)
        return -1;
    n = recv(socket_table[sh], &byte, 1, 0);
    if (n <= 0) return -1;
    return (int)byte;
}

int platform_socket_write(PlatformSocket sh, int byte)
{
    unsigned char b = (unsigned char)byte;
    LONG n;
    if (sh == 0 || sh >= PLATFORM_SOCKET_TABLE_SIZE || socket_table[sh] < 0)
        return -1;
    n = send(socket_table[sh], &b, 1, 0);
    return (n == 1) ? 0 : -1;
}

int platform_socket_write_buf(PlatformSocket sh, const char *buf, uint32_t len)
{
    LONG total = 0;
    LONG fd;
    if (sh == 0 || sh >= PLATFORM_SOCKET_TABLE_SIZE || socket_table[sh] < 0)
        return -1;
    fd = socket_table[sh];
    while ((uint32_t)total < len) {
        LONG n = send(fd, (APTR)(buf + total), (LONG)(len - (uint32_t)total), 0);
        if (n <= 0) return -1;
        total += n;
    }
    return 0;
}

int platform_socket_flush(PlatformSocket sh)
{
    /* TCP sockets don't need explicit flush */
    (void)sh;
    return 0;
}

void platform_init(void)
{
    /* Nothing needed — dos.library is auto-opened by startup */
}

void platform_shutdown(void)
{
    if (SocketBase) {
        /* Close any remaining open sockets */
        int i;
        for (i = 1; i < PLATFORM_SOCKET_TABLE_SIZE; i++) {
            if (socket_table[i] >= 0) {
                CloseSocket(socket_table[i]);
                socket_table[i] = -1;
            }
        }
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
}
