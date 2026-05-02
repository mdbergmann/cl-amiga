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

/* --- I/O buffer --- */

#define PLATFORM_IOBUF_SIZE 4096

typedef struct {
    char *rbuf;     /* read buffer (AllocVec'd) */
    int   rpos;     /* current read position */
    int   rlen;     /* valid bytes in read buffer */
    char *wbuf;     /* write buffer (AllocVec'd) */
    int   wlen;     /* pending bytes in write buffer */
} IOBuf;

static IOBuf *iobuf_alloc(void)
{
    IOBuf *b = (IOBuf *)AllocVec(sizeof(IOBuf), MEMF_CLEAR);
    if (!b) return NULL;
    b->rbuf = (char *)AllocVec(PLATFORM_IOBUF_SIZE, 0);
    b->wbuf = (char *)AllocVec(PLATFORM_IOBUF_SIZE, 0);
    if (!b->rbuf || !b->wbuf) {
        if (b->rbuf) FreeVec(b->rbuf);
        if (b->wbuf) FreeVec(b->wbuf);
        FreeVec(b);
        return NULL;
    }
    b->rpos = 0;
    b->rlen = 0;
    b->wlen = 0;
    return b;
}

static void iobuf_free(IOBuf *b)
{
    if (b) {
        if (b->rbuf) FreeVec(b->rbuf);
        if (b->wbuf) FreeVec(b->wbuf);
        FreeVec(b);
    }
}

/* --- Handle-based file I/O --- */

/* On Amiga, BPTR is a LONG (32-bit). We store them directly as uint32_t.
 * Slot 0 is reserved as PLATFORM_FILE_INVALID. */

#define PLATFORM_FILE_TABLE_SIZE 64

static BPTR file_table[PLATFORM_FILE_TABLE_SIZE];
static IOBuf *file_buf[PLATFORM_FILE_TABLE_SIZE];
static int file_table_init = 0;

static void file_table_ensure_init(void)
{
    if (!file_table_init) {
        int i;
        for (i = 0; i < PLATFORM_FILE_TABLE_SIZE; i++) {
            file_table[i] = 0;
            file_buf[i] = NULL;
        }
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
            file_buf[i] = iobuf_alloc();
            return (PlatformFile)i;
        }
    }

    Close(fh);
    return PLATFORM_FILE_INVALID;
}

/* Flush file write buffer to disk */
static int file_flush_wbuf(PlatformFile fh)
{
    IOBuf *b;
    LONG written;
    if (fh == 0 || fh >= PLATFORM_FILE_TABLE_SIZE) return -1;
    b = file_buf[fh];
    if (!b || b->wlen == 0) return 0;
    written = Write(file_table[fh], (APTR)b->wbuf, (LONG)b->wlen);
    if (written != (LONG)b->wlen) return -1;
    b->wlen = 0;
    return 0;
}

void platform_file_close(PlatformFile fh)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh]) {
        file_flush_wbuf(fh);
        Close(file_table[fh]);
        file_table[fh] = 0;
        iobuf_free(file_buf[fh]);
        file_buf[fh] = NULL;
    }
}

int platform_file_getchar(PlatformFile fh)
{
    IOBuf *b;
    if (fh == 0 || fh >= PLATFORM_FILE_TABLE_SIZE || !file_table[fh])
        return -1;
    b = file_buf[fh];
    if (b) {
        if (b->rpos < b->rlen)
            return (unsigned char)b->rbuf[b->rpos++];
        /* Refill read buffer */
        {
            LONG n = Read(file_table[fh], (APTR)b->rbuf, PLATFORM_IOBUF_SIZE);
            if (n <= 0) return -1;
            b->rpos = 1;
            b->rlen = (int)n;
            return (unsigned char)b->rbuf[0];
        }
    }
    return FGetC(file_table[fh]);
}

int platform_file_write_string(PlatformFile fh, const char *str)
{
    LONG len;
    if (fh == 0 || fh >= PLATFORM_FILE_TABLE_SIZE || !file_table[fh])
        return -1;
    len = strlen(str);
    return platform_file_write_buf(fh, str, (uint32_t)len);
}

int platform_file_write_char(PlatformFile fh, int ch)
{
    IOBuf *b;
    if (fh == 0 || fh >= PLATFORM_FILE_TABLE_SIZE || !file_table[fh])
        return -1;
    b = file_buf[fh];
    if (b) {
        b->wbuf[b->wlen++] = (char)ch;
        if (b->wlen >= PLATFORM_IOBUF_SIZE)
            return file_flush_wbuf(fh);
        return 0;
    }
    return (FPutC(file_table[fh], ch) != -1) ? 0 : -1;
}

int platform_file_write_buf(PlatformFile fh, const char *buf, uint32_t len)
{
    IOBuf *b;
    if (fh == 0 || fh >= PLATFORM_FILE_TABLE_SIZE || !file_table[fh])
        return -1;
    b = file_buf[fh];
    if (b) {
        uint32_t pos = 0;
        while (pos < len) {
            int avail = PLATFORM_IOBUF_SIZE - b->wlen;
            int chunk = (int)(len - pos);
            if (chunk > avail) chunk = avail;
            memcpy(b->wbuf + b->wlen, buf + pos, (size_t)chunk);
            b->wlen += chunk;
            pos += (uint32_t)chunk;
            if (b->wlen >= PLATFORM_IOBUF_SIZE) {
                if (file_flush_wbuf(fh) != 0) return -1;
            }
        }
        return 0;
    }
    /* Fallback: direct write */
    {
        LONG written = Write(file_table[fh], (APTR)buf, (LONG)len);
        return (written == (LONG)len) ? 0 : -1;
    }
}

int platform_file_flush(PlatformFile fh)
{
    return file_flush_wbuf(fh);
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
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh]) {
        long pos = (long)Seek(file_table[fh], 0, OFFSET_CURRENT);
        /* Adjust for buffered but unread data */
        IOBuf *b = file_buf[fh];
        if (b && b->rlen > 0)
            pos -= (long)(b->rlen - b->rpos);
        /* Adjust for buffered but unflushed writes */
        if (b && b->wlen > 0)
            pos += (long)b->wlen;
        return pos;
    }
    return -1;
}

int platform_file_set_position(PlatformFile fh, long pos)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh]) {
        IOBuf *b = file_buf[fh];
        /* Flush writes and invalidate read buffer on seek */
        if (b) {
            file_flush_wbuf(fh);
            b->rpos = 0;
            b->rlen = 0;
        }
        return Seek(file_table[fh], pos, OFFSET_BEGINNING) >= 0 ? 0 : -1;
    }
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
    /* AmigaOS Rename() fails if target exists; delete target first to match
       POSIX rename() semantics (atomic overwrite) */
    BPTR lock = Lock((STRPTR)newpath, ACCESS_READ);
    if (lock) {
        UnLock(lock);
        DeleteFile((STRPTR)newpath);
    }
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
        if (count >= capacity) {
            capacity *= 2;
            result = (char **)realloc(result,
                         (size_t)(capacity + 1) * sizeof(char *));
            if (!result) break;
        }
        if (ap->ap_Info.fib_DirEntryType > 0) {
            /* Directory entry — append '/' so parser creates directory pathname */
            size_t slen = strlen((char *)ap->ap_Buf);
            char *dname = (char *)malloc(slen + 2);
            if (dname) {
                memcpy(dname, ap->ap_Buf, slen);
                dname[slen] = '/';
                dname[slen + 1] = '\0';
                result[count] = dname;
                count++;
            }
        } else {
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
    BPTR lock;
    int has_volume = 0;
    const char *p;

    /* AmigaOS path syntax: an absolute path begins with a volume or assign
     * name followed by ':' (e.g. "T:foo", "Ram Disk:bar", "DH0:lisp/x").
     * If the user already supplied such a prefix, preserve it instead of
     * canonicalizing through NameFromLock — otherwise PROBE-FILE,
     * RENAME-FILE etc. return paths the user can't recognize
     * (e.g. "T:foo" → "Ram Disk:T/foo"). */
    for (p = path; *p; p++) {
        if (*p == ':') { has_volume = 1; break; }
        if (*p == '/') break;  /* relative path with subdir component */
    }

    lock = Lock((STRPTR)path, ACCESS_READ);
    if (!lock) return NULL;

    if (has_volume) {
        size_t plen = strlen(path);
        UnLock(lock);
        if ((int)(plen + 1) > bufsize) return NULL;
        memcpy(buf, path, plen + 1);
        return buf;
    }

    if (!NameFromLock(lock, (STRPTR)buf, (LONG)bufsize)) {
        UnLock(lock);
        return NULL;
    }
    UnLock(lock);
    return buf;
}

const char *platform_expand_home(const char *path, char *buf, int bufsize)
{
    size_t plen;

    if (!path || path[0] != '~') return path;
    /* Only expand ~/... or bare ~ (not ~user) */
    if (path[1] != '\0' && path[1] != '/') return path;

    /* ~ maps to PROGDIR: on AmigaOS */
    if (path[1] == '\0') {
        /* bare ~ -> "PROGDIR:" */
        if (bufsize < 9) return path;
        memcpy(buf, "PROGDIR:", 9);
        return buf;
    }
    /* ~/rest -> PROGDIR:rest */
    plen = strlen(path + 2); /* skip ~/ */
    if ((int)(8 + plen + 1) > bufsize) return path;
    memcpy(buf, "PROGDIR:", 8);
    memcpy(buf + 8, path + 2, plen + 1);
    return buf;
}

/* --- TCP Socket I/O via bsdsocket.library (AmiTCP/Roadshow/Miami) --- */

#include <proto/bsdsocket.h>

struct Library *SocketBase = NULL;

#define PLATFORM_SOCKET_TABLE_SIZE 16

static LONG socket_table[PLATFORM_SOCKET_TABLE_SIZE];
static IOBuf *socket_buf[PLATFORM_SOCKET_TABLE_SIZE];
static int socket_table_init = 0;

static void socket_table_ensure_init(void)
{
    if (!socket_table_init) {
        int i;
        for (i = 0; i < PLATFORM_SOCKET_TABLE_SIZE; i++) {
            socket_table[i] = -1;
            socket_buf[i] = NULL;
        }
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
            socket_buf[i] = iobuf_alloc();
            return (PlatformSocket)i;
        }
    }

    CloseSocket(fd);
    return PLATFORM_SOCKET_INVALID;
}

/* Flush socket write buffer to the wire */
static int socket_flush_wbuf(PlatformSocket sh)
{
    IOBuf *b;
    LONG fd, total = 0;
    if (sh == 0 || sh >= PLATFORM_SOCKET_TABLE_SIZE) return -1;
    b = socket_buf[sh];
    if (!b || b->wlen == 0) return 0;
    fd = socket_table[sh];
    while (total < (LONG)b->wlen) {
        LONG n = send(fd, (APTR)(b->wbuf + total), (LONG)(b->wlen - total), 0);
        if (n <= 0) return -1;
        total += n;
    }
    b->wlen = 0;
    return 0;
}

void platform_socket_close(PlatformSocket sh)
{
    if (sh > 0 && sh < PLATFORM_SOCKET_TABLE_SIZE && socket_table[sh] >= 0) {
        socket_flush_wbuf(sh);
        CloseSocket(socket_table[sh]);
        socket_table[sh] = -1;
        iobuf_free(socket_buf[sh]);
        socket_buf[sh] = NULL;
    }
}

int platform_socket_read(PlatformSocket sh)
{
    IOBuf *b;
    if (sh == 0 || sh >= PLATFORM_SOCKET_TABLE_SIZE || socket_table[sh] < 0)
        return -1;
    b = socket_buf[sh];
    if (b) {
        if (b->rpos < b->rlen)
            return (unsigned char)b->rbuf[b->rpos++];
        /* Refill read buffer */
        {
            LONG n = recv(socket_table[sh], (APTR)b->rbuf, PLATFORM_IOBUF_SIZE, 0);
            if (n <= 0) return -1;
            b->rpos = 1;
            b->rlen = (int)n;
            return (unsigned char)b->rbuf[0];
        }
    }
    /* Fallback: no buffer */
    {
        unsigned char byte;
        LONG n = recv(socket_table[sh], &byte, 1, 0);
        if (n <= 0) return -1;
        return (int)byte;
    }
}

int platform_socket_write(PlatformSocket sh, int byte)
{
    IOBuf *b;
    if (sh == 0 || sh >= PLATFORM_SOCKET_TABLE_SIZE || socket_table[sh] < 0)
        return -1;
    b = socket_buf[sh];
    if (b) {
        b->wbuf[b->wlen++] = (char)byte;
        if (b->wlen >= PLATFORM_IOBUF_SIZE)
            return socket_flush_wbuf(sh);
        return 0;
    }
    /* Fallback: no buffer */
    {
        unsigned char bb = (unsigned char)byte;
        LONG n = send(socket_table[sh], &bb, 1, 0);
        return (n == 1) ? 0 : -1;
    }
}

int platform_socket_write_buf(PlatformSocket sh, const char *buf, uint32_t len)
{
    IOBuf *b;
    if (sh == 0 || sh >= PLATFORM_SOCKET_TABLE_SIZE || socket_table[sh] < 0)
        return -1;
    b = socket_buf[sh];
    if (b) {
        uint32_t pos = 0;
        while (pos < len) {
            int avail = PLATFORM_IOBUF_SIZE - b->wlen;
            int chunk = (int)(len - pos);
            if (chunk > avail) chunk = avail;
            memcpy(b->wbuf + b->wlen, buf + pos, (size_t)chunk);
            b->wlen += chunk;
            pos += (uint32_t)chunk;
            if (b->wlen >= PLATFORM_IOBUF_SIZE) {
                if (socket_flush_wbuf(sh) != 0) return -1;
            }
        }
        return 0;
    }
    /* Fallback: direct send */
    {
        LONG total = 0;
        LONG fd = socket_table[sh];
        while ((uint32_t)total < len) {
            LONG n = send(fd, (APTR)(buf + total), (LONG)(len - (uint32_t)total), 0);
            if (n <= 0) return -1;
            total += n;
        }
        return 0;
    }
}

int platform_socket_flush(PlatformSocket sh)
{
    return socket_flush_wbuf(sh);
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
                socket_flush_wbuf((PlatformSocket)i);
                CloseSocket(socket_table[i]);
                socket_table[i] = -1;
                iobuf_free(socket_buf[i]);
                socket_buf[i] = NULL;
            }
        }
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
}

/* =============================================================
 * Generic FFI: foreign memory (Amiga implementation)
 *
 * On Amiga (32-bit), handles ARE raw addresses — no side table needed.
 * ============================================================= */

uint32_t platform_ffi_alloc(uint32_t size)
{
    void *p;
    if (size == 0) return 0;
    p = AllocVec(size, MEMF_CLEAR);
    return (uint32_t)p;
}

void platform_ffi_free(uint32_t handle, uint32_t size)
{
    (void)size;
    if (handle == 0) return;
    FreeVec((void *)handle);
}

void *platform_ffi_resolve(uint32_t handle)
{
    return (void *)handle;
}

uint32_t platform_ffi_peek32(uint32_t handle, uint32_t offset)
{
    return *(volatile uint32_t *)((uint8_t *)handle + offset);
}

uint16_t platform_ffi_peek16(uint32_t handle, uint32_t offset)
{
    return *(volatile uint16_t *)((uint8_t *)handle + offset);
}

uint8_t platform_ffi_peek8(uint32_t handle, uint32_t offset)
{
    return *(volatile uint8_t *)((uint8_t *)handle + offset);
}

void platform_ffi_poke32(uint32_t handle, uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)((uint8_t *)handle + offset) = val;
}

void platform_ffi_poke16(uint32_t handle, uint32_t offset, uint16_t val)
{
    *(volatile uint16_t *)((uint8_t *)handle + offset) = val;
}

void platform_ffi_poke8(uint32_t handle, uint32_t offset, uint8_t val)
{
    *(volatile uint8_t *)((uint8_t *)handle + offset) = val;
}

/* =============================================================
 * Amiga-specific FFI: shared library calls
 * ============================================================= */

uint32_t platform_amiga_open_library(const char *name, uint32_t version)
{
    struct Library *lib = OpenLibrary((CONST_STRPTR)name, (ULONG)version);
    if (!lib) return 0;
    return (uint32_t)lib;
}

void platform_amiga_close_library(uint32_t lib_base)
{
    if (lib_base == 0) return;
    CloseLibrary((struct Library *)lib_base);
}

/* platform_amiga_call() is implemented in ffi_dispatch_m68k.s
 * (68k assembly trampoline for register-based library calls) */

uint32_t platform_amiga_alloc_chip(uint32_t size)
{
    void *p;
    if (size == 0) return 0;
    p = AllocMem(size, MEMF_CHIP | MEMF_CLEAR);
    return (uint32_t)p;
}

void platform_amiga_free_chip(uint32_t addr, uint32_t size)
{
    if (addr == 0) return;
    FreeMem((void *)addr, size);
}
