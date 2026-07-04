#include "platform.h"
#include "platform_thread.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include <stdlib.h>   /* malloc/realloc (platform_directory) */

/* GC stop-the-world cooperation (defined in core/thread.c).  Forward-declared
 * here rather than #including core/thread.h so the platform layer stays free of
 * core/VM type dependencies.  A thread parked in a blocking socket syscall
 * cannot reach a GC safepoint; bracketing the syscall with these marks the
 * thread as "stopped" for the duration so a concurrent stop-the-world GC does
 * not deadlock waiting on it.  Both are no-ops on threads not registered with
 * the MP subsystem (e.g. the main thread before any mp:make-thread). */
extern void cl_gc_enter_safe_region(void);
extern void cl_gc_leave_safe_region(void);

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
    char *r;
    if (!in)
        return 0;
    /* Blocking stdin read — bracket with a GC safe region exactly like the
     * socket syscalls.  cl_repl() parks here in FGets; without the bracket a
     * stop-the-world GC from a :spawn worker waits forever for this thread to
     * reach a safepoint it can never reach (the SLY REPL hang). */
    cl_gc_enter_safe_region();
    r = FGets(in, buf, bufsize);
    cl_gc_leave_safe_region();
    if (!r)
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
    int c;
    if (!in)
        return -1;
    /* Same blocking-stdin rationale as platform_read_line: the CONSOLE stream's
     * read-char parks here, so bracket it as a GC safe region. */
    cl_gc_enter_safe_region();
    c = FGetC(in);
    cl_gc_leave_safe_region();
    return c;
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

int platform_stdin_is_interactive(void)
{
    /* IsInteractive() returns TRUE only for a real console/CON: handle, FALSE
     * for files, pipes and NIL: — exactly the gate the debugger needs. */
    BPTR in = Input();
    if (!in) return 0;
    return IsInteractive(in) ? 1 : 0;
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

/* Serialises slot claim (open) and slot release (close) so two threads can
 * never race a claim onto the same index or double-Close one slot (mirrors
 * the POSIX file_table_mutex / socket_table_mutex).  Per-byte read/write
 * paths on a caller-owned slot run unlocked.  Initialised on first use,
 * which is single-threaded (boot loads files before any worker exists). */
static void *file_table_mutex = NULL;

static void file_table_lock(void)
{
    if (file_table_mutex) platform_mutex_lock(file_table_mutex);
}

static void file_table_unlock(void)
{
    if (file_table_mutex) platform_mutex_unlock(file_table_mutex);
}

static void file_table_ensure_init(void)
{
    if (!file_table_init) {
        int i;
        for (i = 0; i < PLATFORM_FILE_TABLE_SIZE; i++) {
            file_table[i] = 0;
            file_buf[i] = NULL;
        }
        platform_mutex_init(&file_table_mutex);
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

    /* Copy the path to C memory, then bracket the Open in a GC safe
     * region: Open on floppy/slow media blocks for real, and a peer's
     * stop-the-world GC would stall for its whole duration.  The copy is
     * mandatory — `path` may point into the moving Lisp arena, and a
     * peer compaction inside the safe region can relocate it.  Oversized
     * paths run unbracketed from the original pointer. */
    {
        char pathbuf[1024];
        size_t plen = strlen(path);
        if (plen < sizeof(pathbuf)) {
            memcpy(pathbuf, path, plen + 1);
            cl_gc_enter_safe_region();
            fh = Open((STRPTR)pathbuf, amode);
            if (fh && mode == PLATFORM_FILE_APPEND)
                Seek(fh, 0, OFFSET_END);
            cl_gc_leave_safe_region();
        } else {
            fh = Open((STRPTR)path, amode);
            if (fh && mode == PLATFORM_FILE_APPEND)
                Seek(fh, 0, OFFSET_END);
        }
    }
    if (!fh) return PLATFORM_FILE_INVALID;

    /* Find free slot (slot 0 reserved) — claim under the table mutex so
     * two concurrent opens never claim the same index. */
    file_table_lock();
    for (i = 1; i < PLATFORM_FILE_TABLE_SIZE; i++) {
        if (file_table[i] == 0) {
            file_table[i] = fh;
            file_buf[i] = iobuf_alloc();
            file_table_unlock();
            return (PlatformFile)i;
        }
    }
    file_table_unlock();

    Close(fh);
    return PLATFORM_FILE_INVALID;
}

/* Flush file write buffer to disk */
static int file_flush_wbuf(PlatformFile fh)
{
    IOBuf *b;
    LONG written;
    BPTR h;
    if (fh == 0 || fh >= PLATFORM_FILE_TABLE_SIZE) return -1;
    b = file_buf[fh];
    if (!b || b->wlen == 0) return 0;
    h = file_table[fh];
    /* The 4KB Write to floppy/slow media blocks; wbuf is C memory, so
     * bracketing is safe (a peer compaction cannot move it). */
    cl_gc_enter_safe_region();
    written = Write(h, (APTR)b->wbuf, (LONG)b->wlen);
    cl_gc_leave_safe_region();
    if (written != (LONG)b->wlen) return -1;
    b->wlen = 0;
    return 0;
}

void platform_file_close(PlatformFile fh)
{
    BPTR h = 0;
    IOBuf *b = NULL;
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE) {
        /* Detach under the mutex (double-close protection: only one caller
         * observes the non-zero slot); flush + Close outside it — DOS
         * Close/Write can block, and holding the table lock across them
         * would stall every concurrent open/close. */
        file_table_lock();
        h = file_table[fh];
        b = file_buf[fh];
        file_table[fh] = 0;
        file_buf[fh] = NULL;
        file_table_unlock();
    }
    if (h) {
        if (b && b->wlen > 0)
            Write(h, (APTR)b->wbuf, (LONG)b->wlen);
        Close(h);
    }
    iobuf_free(b);
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
        /* Refill read buffer.  The 4KB Read from floppy/slow media
         * blocks; rbuf is C memory, so bracketing is safe.  The per-char
         * fast path above stays bracket-free (hot LOAD path). */
        {
            BPTR h = file_table[fh];
            LONG n;
            cl_gc_enter_safe_region();
            n = Read(h, (APTR)b->rbuf, PLATFORM_IOBUF_SIZE);
            cl_gc_leave_safe_region();
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
    /* Fallback (no IOBuf — allocation failed at open): chunk through a
     * C stack buffer so the blocking Write can be bracketed.  Each copy
     * happens OUTSIDE the safe region; the syscall inside it reads only
     * the C copy.  NOTE: `buf` is re-read across earlier chunks' safe
     * regions, so multi-chunk writes require C-memory input — which
     * holds for every current caller (the stream layer chunks arena
     * strings through rooted CL_Objs before reaching platform level;
     * the FASL writer passes platform memory). */
    {
        char chunk[512];
        uint32_t pos = 0;
        BPTR h = file_table[fh];
        while (pos < len) {
            uint32_t nb = len - pos;
            LONG written;
            if (nb > (uint32_t)sizeof(chunk)) nb = (uint32_t)sizeof(chunk);
            memcpy(chunk, buf + pos, nb);
            cl_gc_enter_safe_region();
            written = Write(h, (APTR)chunk, (LONG)nb);
            cl_gc_leave_safe_region();
            if (written != (LONG)nb) return -1;
            pos += nb;
        }
        return 0;
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
        BPTR h = file_table[fh];
        int rc;
        /* Flush writes and invalidate read buffer on seek */
        if (b) {
            file_flush_wbuf(fh);
            b->rpos = 0;
            b->rlen = 0;
        }
        /* The Seek hits the disk on AmigaDOS — bracket it. */
        cl_gc_enter_safe_region();
        rc = Seek(h, pos, OFFSET_BEGINNING) >= 0 ? 0 : -1;
        cl_gc_leave_safe_region();
        return rc;
    }
    return -1;
}

long platform_file_length(PlatformFile fh)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh]) {
        BPTR h = file_table[fh];
        long cur, end;
        cl_gc_enter_safe_region();
        cur = Seek(h, 0, OFFSET_CURRENT);
        Seek(h, 0, OFFSET_END);
        end = Seek(h, 0, OFFSET_CURRENT);
        Seek(h, cur, OFFSET_BEGINNING);
        cl_gc_leave_safe_region();
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
    if (ticks > 0) {
        /* A sleeping task cannot reach a safepoint — without the safe
         * region every peer's stop-the-world GC stalls for the whole
         * nap.  Safe regions nest (safe_region_depth). */
        cl_gc_enter_safe_region();
        Delay(ticks);
        cl_gc_leave_safe_region();
    }
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
    LONG rc;
    /* `command` typically points into a Lisp string's arena data
     * (EXT:SYSTEM-COMMAND passes s->data) and SystemTagList blocks for
     * the child's whole runtime — copy to C memory FIRST, then bracket
     * (a peer compaction inside the safe region can move the source).
     * OOM fallback: run unbracketed from the original pointer. */
    size_t clen = strlen(command);
    char *cmdbuf = (char *)malloc(clen + 1);
    if (cmdbuf) {
        memcpy(cmdbuf, command, clen + 1);
        cl_gc_enter_safe_region();
        rc = SystemTagList((STRPTR)cmdbuf, NULL);
        cl_gc_leave_safe_region();
        free(cmdbuf);
    } else {
        rc = SystemTagList((STRPTR)command, NULL);
    }
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

    /* Directory enumeration hits the disk per entry; everything touched
     * inside (amiga_pat, ap, result — all C memory) is compaction-proof,
     * so bracket the whole walk.  MatchFirst/MatchNext do the blocking. */
    cl_gc_enter_safe_region();
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
    cl_gc_leave_safe_region();
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

/* --- TCP Socket I/O via bsdsocket.library — single-owner reactor model ---
 *
 * AmigaOS bsdsocket.library is task-specific: the library base AND every
 * socket fd are owned by the task that created them, so a socket cannot be
 * used from a different task.  clamiga threads are separate AmigaOS processes
 * (CreateNewProc), so naive cross-thread socket I/O blocks forever.
 *
 * To give POSIX-like semantics (any thread may use any socket, including one
 * thread reading while another writes the same socket), ALL bsdsocket calls
 * are funnelled to a single dedicated "reactor" process that exclusively owns
 * SocketBase, the socket table, and every fd.  Other threads never touch
 * bsdsocket: their platform_socket_* calls marshal a request to the reactor
 * over an Exec message port and block on the reply (inside a GC safe region).
 * The reactor multiplexes all sockets with WaitSelect so one slow/blocked
 * socket never stalls the others.  AmigaOS's single shared address space lets
 * the reactor recv/send directly into the caller's IOBuf by pointer — no data
 * copy between tasks.
 */

#if PLATFORM_MORPHOS
#include <proto/socket.h>
/* MorphOS's proto/socket.h brings in sockaddr_in but not struct hostent;
 * gethostbyname() needs <netdb.h> for the full definition. */
#include <netdb.h>
#else
#include <proto/bsdsocket.h>
#endif
#include <exec/ports.h>
#include <exec/tasks.h>
#include <dos/dostags.h>
#include <sys/time.h>   /* struct timeval for WaitSelect zero-timeout poll */

/* Address-length type for accept/getsockname/getsockopt.  The MorphOS
 * socket.library API takes LONG* (and does not declare socklen_t), whereas
 * Roadshow/bsdsocket on classic AmigaOS provides socklen_t.  Use a portable
 * alias so the same call sites compile cleanly on both. */
#if PLATFORM_MORPHOS
typedef LONG cl_socklen_t;
#else
typedef socklen_t cl_socklen_t;
#endif

/* Roadshow's <netinet/in.h> defines INADDR_ANY but leaves INADDR_LOOPBACK to
 * <arpa/inet.h>, which we don't pull in.  Provide the standard value. */
#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK ((uint32_t)0x7f000001UL)
#endif
/* The -noixemul SDK headers omit these — supply the BSD values bsdsocket uses. */
#ifndef EWOULDBLOCK
#define EWOULDBLOCK 35
#endif
#ifndef EINPROGRESS
#define EINPROGRESS 36
#endif
#ifndef FIONBIO
#define FIONBIO 0x8004667EUL   /* _IOW('f', 126, int) — set non-blocking */
#endif

struct Library *SocketBase = NULL;   /* opened by, and only used from, the reactor */
static LONG socket_errno = 0;

/* Fixed cap: bounded by bsdsocket.library's per-task descriptor table (Roadshow
 * default ~64).  Going higher would require SocketBaseTags(SBTC_DTABLESIZE).
 * The host (platform_posix.c) has no such ceiling and grows its table on demand. */
#define PLATFORM_SOCKET_TABLE_SIZE 64

/* Reactor-owned: fd per slot (-1 = free), IOBuf per slot (NULL for listeners).
 * Slot 0 is reserved as the INVALID handle.  Only the reactor mutates fds; the
 * IOBuf bytes are shared with the requesting thread, but never concurrently —
 * the request/reply handshake serialises access. */
static LONG   socket_table[PLATFORM_SOCKET_TABLE_SIZE];
static IOBuf *socket_buf[PLATFORM_SOCKET_TABLE_SIZE];
/* Per-socket read/write timeouts in milliseconds (0 = none).  Written by
 * client threads via platform_socket_set_timeout and read by the client stubs
 * to stamp each request; stream.c serialises per socket+direction. */
static int    socket_rtimeout[PLATFORM_SOCKET_TABLE_SIZE];
static int    socket_wtimeout[PLATFORM_SOCKET_TABLE_SIZE];

/* ---- Minimal fd_set (the toolchain headers don't provide one) ----
 * Standard BSD layout: bit n in word n/32, which is what WaitSelect expects. */
#define CL_FDSET_WORDS 8   /* up to 256 fds */
typedef struct { uint32_t bits[CL_FDSET_WORDS]; } CL_fdset;
#define CL_FD_ZERO(s)    memset((s), 0, sizeof(*(s)))
#define CL_FD_SET(n,s)   ((s)->bits[(unsigned)(n) >> 5] |= (1UL << ((unsigned)(n) & 31)))
#define CL_FD_ISSET(n,s) (((s)->bits[(unsigned)(n) >> 5] >> ((unsigned)(n) & 31)) & 1U)

/* ---- Reactor request protocol ---- */
enum {
    REQ_CONNECT = 1, REQ_LISTEN, REQ_ACCEPT,
    REQ_READFILL, REQ_WRITE, REQ_CLOSE, REQ_SHUTDOWN, REQ_POLL
};

typedef struct SockReq {
    struct Message msg;          /* mn_ReplyPort = caller's stack reply port */
    int            op;
    PlatformSocket slot;         /* target slot (read/write/close; listener for accept) */
    const char    *host;         /* connect */
    int            port;         /* connect / listen */
    int            loopback;     /* listen */
    char          *buf;          /* readfill destination / write source */
    uint32_t       len;          /* readfill capacity / write length */
    int            timeout_ms;   /* readfill/write: 0 = block forever, else deadline */
    volatile int            result;    /* readfill: bytes (0=EOF, -2=timeout); else 0=ok/-1=err/-2=timeout */
    volatile PlatformSocket out_slot;  /* connect/listen/accept: new slot */
    volatile int            out_port;  /* listen: bound port */
} SockReq;

/* ---- Reactor state (all touched only by the reactor task) ---- */
static struct Process *reactor_proc = NULL;
static struct MsgPort *reactor_port = NULL;   /* request port, owned by reactor */
static struct Task    *reactor_boot_task = NULL;
static BYTE            reactor_boot_sig = -1;
static void           *reactor_init_mutex = NULL;

/* Parked op per slot+direction (stream.c serialises per socket+direction, so
 * at most one outstanding op each way).  pend_wpos tracks bytes already sent
 * for a partially-completed write. */
static SockReq *pend_read[PLATFORM_SOCKET_TABLE_SIZE];
static SockReq *pend_write[PLATFORM_SOCKET_TABLE_SIZE];
static uint32_t pend_wpos[PLATFORM_SOCKET_TABLE_SIZE];
/* Absolute deadline (platform_time_ms) for a parked read/write; valid only
 * when the matching pend_*_has_deadline flag is set.  Lets the reactor time a
 * parked op out instead of waiting on it forever. */
static uint32_t pend_read_deadline[PLATFORM_SOCKET_TABLE_SIZE];
static uint32_t pend_write_deadline[PLATFORM_SOCKET_TABLE_SIZE];
static int      pend_read_has_deadline[PLATFORM_SOCKET_TABLE_SIZE];
static int      pend_write_has_deadline[PLATFORM_SOCKET_TABLE_SIZE];

/* Stamp a parked op's deadline from its requested timeout (ms). */
static void reactor_arm_deadline(int slot, int timeout_ms, int is_write)
{
    if (timeout_ms > 0) {
        uint32_t dl = platform_time_ms() + (uint32_t)timeout_ms;
        if (is_write) { pend_write_deadline[slot] = dl; pend_write_has_deadline[slot] = 1; }
        else          { pend_read_deadline[slot]  = dl; pend_read_has_deadline[slot]  = 1; }
    } else {
        if (is_write) pend_write_has_deadline[slot] = 0;
        else          pend_read_has_deadline[slot]  = 0;
    }
}

/* ===== Reactor-side helpers (run in the reactor task; may call bsdsocket) ===== */

static void reactor_set_nonblock(LONG fd)
{
    LONG one = 1;
    IoctlSocket(fd, FIONBIO, (char *)&one);
}

/* Claim a free slot for fd; allocate an IOBuf unless with_buf==0 (listeners). */
static int reactor_alloc_slot(LONG fd, int with_buf)
{
    int i;
    for (i = 1; i < PLATFORM_SOCKET_TABLE_SIZE; i++) {
        if (socket_table[i] == -1) {
            socket_buf[i] = with_buf ? iobuf_alloc() : NULL;
            if (with_buf && !socket_buf[i]) return -1;
            socket_table[i] = fd;
            return i;
        }
    }
    return -1;
}

static void reactor_free_slot(int slot)
{
    if (socket_buf[slot]) { iobuf_free(socket_buf[slot]); socket_buf[slot] = NULL; }
    socket_table[slot] = -1;
    pend_wpos[slot] = 0;
    pend_read_has_deadline[slot] = 0;
    pend_write_has_deadline[slot] = 0;
    socket_rtimeout[slot] = 0;
    socket_wtimeout[slot] = 0;
}

static void reactor_reply(SockReq *req) { ReplyMsg(&req->msg); }

/* recv into the caller's read buffer; complete or park.  result>0 = bytes,
 * 0 = EOF, -1 = error. */
static void reactor_try_read(SockReq *req)
{
    int slot = (int)req->slot;
    LONG fd = socket_table[slot];
    LONG n;
    if (fd < 0) { req->result = -1; reactor_reply(req); return; }
    n = recv(fd, req->buf, (LONG)req->len, 0);
    if (n > 0)                       { req->result = (int)n; reactor_reply(req); }
    else if (n == 0)                 { req->result = 0;      reactor_reply(req); } /* EOF */
    else if (Errno() == EWOULDBLOCK) { pend_read[slot] = req;                      /* park */
                                       reactor_arm_deadline(slot, req->timeout_ms, 0); }
    else                             { req->result = -1;     reactor_reply(req); }
}

/* Non-blocking readiness probe (REQ_POLL), backing CL:LISTEN on socket streams.
 * Buffered data is checked caller-side; here we probe only the fd with a
 * zero-timeout WaitSelect.  result: 1 = readable now (data, or for a listener a
 * pending connection), 0 = would block, 2 = EOF (peer closed), -1 = invalid.
 * A readable CONNECTION socket is MSG_PEEK'd to tell data from EOF; a listener
 * has no IOBuf and is never peeked (recv on a listen fd is invalid). */
static void reactor_try_poll(SockReq *req)
{
    int slot = (int)req->slot;
    LONG fd = socket_table[slot];
    CL_fdset rset;
    struct timeval tv;
    LONG r;
    if (fd < 0) { req->result = -1; reactor_reply(req); return; }
    CL_FD_ZERO(&rset);
    CL_FD_SET(fd, &rset);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    r = WaitSelect(fd + 1, &rset, NULL, NULL, &tv, NULL);
    if (r <= 0 || !CL_FD_ISSET(fd, &rset)) { req->result = 0; reactor_reply(req); return; }
    if (!socket_buf[slot]) { req->result = 1; reactor_reply(req); return; }  /* listener: pending conn */
    {
        char peek;
        LONG pn = recv(fd, &peek, 1, MSG_PEEK);
        if (pn > 0)                      req->result = 1;   /* real data */
        else if (pn == 0)                req->result = 2;   /* EOF */
        else if (Errno() == EWOULDBLOCK) req->result = 0;   /* spurious */
        else                             req->result = 2;
    }
    reactor_reply(req);
}

/* send from the caller's write buffer (continuing at pend_wpos); complete or park. */
static void reactor_try_write(SockReq *req)
{
    int slot = (int)req->slot;
    LONG fd = socket_table[slot];
    LONG n;
    uint32_t off;
    if (fd < 0) { req->result = -1; pend_wpos[slot] = 0; reactor_reply(req); return; }
    off = pend_wpos[slot];
    n = send(fd, req->buf + off, (LONG)(req->len - off), 0);
    if (n >= 0) {
        off += (uint32_t)n;
        pend_wpos[slot] = off;
        if (off >= req->len) { pend_wpos[slot] = 0; req->result = 0; reactor_reply(req); }
        else                 { pend_write[slot] = req;     /* more to send — park */
                               reactor_arm_deadline(slot, req->timeout_ms, 1); }
    } else if (Errno() == EWOULDBLOCK) {
        pend_write[slot] = req;                            /* park */
        reactor_arm_deadline(slot, req->timeout_ms, 1);
    } else {
        pend_wpos[slot] = 0; req->result = -1; reactor_reply(req);
    }
}

static void reactor_try_accept(SockReq *req)
{
    int slot = (int)req->slot;            /* listener slot */
    LONG lfd = socket_table[slot];
    struct sockaddr_in caddr;
    cl_socklen_t clen = sizeof(caddr);
    LONG fd;
    if (lfd < 0) { req->result = -1; req->out_slot = PLATFORM_SOCKET_INVALID; reactor_reply(req); return; }
    fd = accept(lfd, (struct sockaddr *)&caddr, &clen);
    if (fd >= 0) {
        int ns;
        reactor_set_nonblock(fd);
        ns = reactor_alloc_slot(fd, 1);
        if (ns < 0) { CloseSocket(fd); req->result = -1; req->out_slot = PLATFORM_SOCKET_INVALID; }
        else        { req->result = 0;  req->out_slot = (PlatformSocket)ns; }
        reactor_reply(req);
    } else if (Errno() == EWOULDBLOCK) {
        pend_read[slot] = req;                              /* park on listener readable */
    } else {
        req->result = -1; req->out_slot = PLATFORM_SOCKET_INVALID; reactor_reply(req);
    }
}

static void reactor_finish_connect(SockReq *req)
{
    int slot = (int)req->slot;
    LONG err = 0;
    cl_socklen_t elen = sizeof(err);
    getsockopt(socket_table[slot], SOL_SOCKET, SO_ERROR, (char *)&err, &elen);
    if (err == 0) {
        req->result = 0; req->out_slot = (PlatformSocket)slot;
    } else {
        CloseSocket(socket_table[slot]);
        reactor_free_slot(slot);
        req->result = -1; req->out_slot = PLATFORM_SOCKET_INVALID;
    }
    reactor_reply(req);
}

static void reactor_start_connect(SockReq *req)
{
    struct hostent *he;
    struct sockaddr_in addr;
    LONG fd, rc;
    int slot;

    /* DNS: dotted-quad (e.g. loopback) resolves locally and does not block;
     * a real hostname lookup can briefly stall the reactor — acceptable. */
    he = gethostbyname((STRPTR)req->host);
    if (!he) { req->result = -1; req->out_slot = PLATFORM_SOCKET_INVALID; reactor_reply(req); return; }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { req->result = -1; req->out_slot = PLATFORM_SOCKET_INVALID; reactor_reply(req); return; }
    reactor_set_nonblock(fd);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)req->port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);

    rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc == 0) {                                          /* connected immediately */
        slot = reactor_alloc_slot(fd, 1);
        if (slot < 0) { CloseSocket(fd); req->result = -1; req->out_slot = PLATFORM_SOCKET_INVALID; }
        else          { req->result = 0;  req->out_slot = (PlatformSocket)slot; }
        reactor_reply(req);
    } else if (Errno() == EINPROGRESS || Errno() == EWOULDBLOCK) {
        slot = reactor_alloc_slot(fd, 1);                   /* reserve slot, park */
        if (slot < 0) { CloseSocket(fd); req->result = -1; req->out_slot = PLATFORM_SOCKET_INVALID; reactor_reply(req); return; }
        req->slot = (PlatformSocket)slot;
        pend_write[slot] = req;
        /* Bound the handshake when a connect timeout was requested; the reactor
         * reaps it in reactor_expire_deadlines if the peer never replies. */
        reactor_arm_deadline(slot, req->timeout_ms, 1);
    } else {
        CloseSocket(fd); req->result = -1; req->out_slot = PLATFORM_SOCKET_INVALID; reactor_reply(req);
    }
}

static void reactor_do_listen(SockReq *req)
{
    struct sockaddr_in addr;
    LONG fd, on = 1;
    int slot;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { req->result = -1; req->out_slot = PLATFORM_SOCKET_INVALID; reactor_reply(req); return; }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
    reactor_set_nonblock(fd);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)req->port);
    addr.sin_addr.s_addr = htonl(req->loopback ? INADDR_LOOPBACK : INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(fd, 4) < 0) {
        CloseSocket(fd); req->result = -1; req->out_slot = PLATFORM_SOCKET_INVALID; reactor_reply(req); return;
    }
    {
        cl_socklen_t alen = sizeof(addr);
        if (getsockname(fd, (struct sockaddr *)&addr, &alen) == 0)
            req->out_port = ntohs(addr.sin_port);
        else
            req->out_port = req->port;
    }
    slot = reactor_alloc_slot(fd, 0);                       /* listener: no IOBuf */
    if (slot < 0) { CloseSocket(fd); req->result = -1; req->out_slot = PLATFORM_SOCKET_INVALID; }
    else          { req->result = 0;  req->out_slot = (PlatformSocket)slot; }
    reactor_reply(req);
}

static void reactor_do_close(SockReq *req)
{
    int slot = (int)req->slot;
    /* Cancel any op parked on this slot — the waiting thread gets -1 and must
     * not touch the IOBuf afterwards (it is about to be freed). */
    if (pend_read[slot])  { pend_read[slot]->result  = -1; reactor_reply(pend_read[slot]);  pend_read[slot]  = NULL; }
    if (pend_write[slot]) { pend_write[slot]->result = -1; reactor_reply(pend_write[slot]); pend_write[slot] = NULL; }
    if (socket_table[slot] >= 0) {
        CloseSocket(socket_table[slot]);
        reactor_free_slot(slot);
    }
    req->result = 0;
    reactor_reply(req);
}

static void reactor_close_all(void)
{
    int i;
    for (i = 1; i < PLATFORM_SOCKET_TABLE_SIZE; i++) {
        if (pend_read[i])  { pend_read[i]->result  = -1; reactor_reply(pend_read[i]);  pend_read[i]  = NULL; }
        if (pend_write[i]) { pend_write[i]->result = -1; reactor_reply(pend_write[i]); pend_write[i] = NULL; }
        if (socket_table[i] >= 0) { CloseSocket(socket_table[i]); reactor_free_slot(i); }
    }
}

static void reactor_handle(SockReq *req)
{
    switch (req->op) {
    case REQ_CONNECT:  reactor_start_connect(req); break;
    case REQ_LISTEN:   reactor_do_listen(req);     break;
    case REQ_ACCEPT:   reactor_try_accept(req);    break;
    case REQ_READFILL: reactor_try_read(req);      break;
    case REQ_WRITE:    reactor_try_write(req);     break;
    case REQ_POLL:     reactor_try_poll(req);      break;
    case REQ_CLOSE:    reactor_do_close(req);      break;
    default:           req->result = -1; reactor_reply(req); break;
    }
}

static void reactor_resume_read(int slot)
{
    SockReq *req = pend_read[slot];
    pend_read[slot] = NULL;
    pend_read_has_deadline[slot] = 0;
    if (req->op == REQ_ACCEPT) reactor_try_accept(req);
    else                       reactor_try_read(req);
}

static void reactor_resume_write(int slot)
{
    SockReq *req = pend_write[slot];
    pend_write[slot] = NULL;
    pend_write_has_deadline[slot] = 0;
    if (req->op == REQ_CONNECT) reactor_finish_connect(req);
    else                        reactor_try_write(req);
}

/* Time out any parked op whose deadline has elapsed: hand the waiting thread
 * PLATFORM_SOCKET_TIMEOUT and clear the park slot. */
static void reactor_expire_deadlines(uint32_t now)
{
    int i;
    for (i = 1; i < PLATFORM_SOCKET_TABLE_SIZE; i++) {
        if (pend_read[i] && pend_read_has_deadline[i] &&
            (int32_t)(pend_read_deadline[i] - now) <= 0) {
            SockReq *req = pend_read[i];
            pend_read[i] = NULL; pend_read_has_deadline[i] = 0;
            req->result = PLATFORM_SOCKET_TIMEOUT;
            req->out_slot = PLATFORM_SOCKET_INVALID;   /* in case it was an accept */
            reactor_reply(req);
        }
        if (pend_write[i] && pend_write_has_deadline[i] &&
            (int32_t)(pend_write_deadline[i] - now) <= 0) {
            SockReq *req = pend_write[i];
            pend_write[i] = NULL; pend_write_has_deadline[i] = 0;
            pend_wpos[i] = 0;
            if (req->op == REQ_CONNECT) {
                /* Handshake never completed: tear down the reserved socket and
                 * report failure (out_slot INVALID), matching the error path of
                 * reactor_finish_connect. */
                if (socket_table[i] >= 0) { CloseSocket(socket_table[i]); reactor_free_slot(i); }
                req->result = -1;
                req->out_slot = PLATFORM_SOCKET_INVALID;
            } else {
                req->result = PLATFORM_SOCKET_TIMEOUT;
            }
            reactor_reply(req);
        }
    }
}

static void reactor_loop(void)
{
    ULONG portsig = 1UL << reactor_port->mp_SigBit;
    int running = 1;

    while (running) {
        CL_fdset rset, wset;
        int maxfd = -1, i;
        ULONG sigs = portsig;
        int have_deadline = 0;
        uint32_t earliest = 0;

        CL_FD_ZERO(&rset);
        CL_FD_ZERO(&wset);
        for (i = 1; i < PLATFORM_SOCKET_TABLE_SIZE; i++) {
            LONG fd = socket_table[i];
            if (fd < 0) continue;
            if (pend_read[i])  { CL_FD_SET(fd, &rset); if (fd > maxfd) maxfd = fd; }
            if (pend_write[i]) { CL_FD_SET(fd, &wset); if (fd > maxfd) maxfd = fd; }
            /* Track the soonest deadline so WaitSelect wakes to expire it. */
            if (pend_read[i] && pend_read_has_deadline[i]) {
                if (!have_deadline || (int32_t)(pend_read_deadline[i] - earliest) < 0)
                    { earliest = pend_read_deadline[i]; have_deadline = 1; }
            }
            if (pend_write[i] && pend_write_has_deadline[i]) {
                if (!have_deadline || (int32_t)(pend_write_deadline[i] - earliest) < 0)
                    { earliest = pend_write_deadline[i]; have_deadline = 1; }
            }
        }

        if (maxfd < 0) {
            /* No parked socket ops — just wait for the next request. */
            Wait(portsig);
        } else if (have_deadline) {
            /* Bound the wait by the soonest deadline; clamp to >= 0. */
            uint32_t now = platform_time_ms();
            int32_t rem = (int32_t)(earliest - now);
            struct timeval tv;
            if (rem < 0) rem = 0;
            tv.tv_sec  = rem / 1000;
            tv.tv_usec = (rem % 1000) * 1000;
            WaitSelect(maxfd + 1, &rset, &wset, NULL, &tv, &sigs);
            for (i = 1; i < PLATFORM_SOCKET_TABLE_SIZE; i++) {
                LONG fd = socket_table[i];
                if (fd < 0) continue;
                if (pend_read[i]  && CL_FD_ISSET(fd, &rset)) reactor_resume_read(i);
                if (pend_write[i] && CL_FD_ISSET(fd, &wset)) reactor_resume_write(i);
            }
            reactor_expire_deadlines(platform_time_ms());
        } else {
            WaitSelect(maxfd + 1, &rset, &wset, NULL, NULL, &sigs);
            for (i = 1; i < PLATFORM_SOCKET_TABLE_SIZE; i++) {
                LONG fd = socket_table[i];
                if (fd < 0) continue;
                if (pend_read[i]  && CL_FD_ISSET(fd, &rset)) reactor_resume_read(i);
                if (pend_write[i] && CL_FD_ISSET(fd, &wset)) reactor_resume_write(i);
            }
        }

        /* Drain new requests (the port signal may or may not be set in sigs
         * after WaitSelect; always poll the port to be safe). */
        {
            struct Message *m;
            while ((m = GetMsg(reactor_port)) != NULL) {
                SockReq *req = (SockReq *)m;
                if (req->op == REQ_SHUTDOWN) {
                    reactor_close_all();
                    running = 0;
                    reactor_reply(req);
                } else {
                    reactor_handle(req);
                }
            }
        }
    }
}

/* Reactor process entry: open SocketBase + request port (owned here), signal
 * the booting thread, then run the loop until REQ_SHUTDOWN. */
static void reactor_entry(void)
{
    struct MsgPort *port = CreateMsgPort();
    int ok = 0;
    int i;

    for (i = 0; i < PLATFORM_SOCKET_TABLE_SIZE; i++) {
        socket_table[i] = -1; socket_buf[i] = NULL;
        pend_read[i] = NULL; pend_write[i] = NULL; pend_wpos[i] = 0;
        pend_read_has_deadline[i] = 0; pend_write_has_deadline[i] = 0;
        socket_rtimeout[i] = 0; socket_wtimeout[i] = 0;
    }

    if (port) {
        SocketBase = OpenLibrary("bsdsocket.library", 3);
        if (SocketBase) {
            SetErrnoPtr(&socket_errno, sizeof(socket_errno));
            reactor_port = port;   /* publish to clients */
            ok = 1;
        } else {
            DeleteMsgPort(port);
        }
    }

    Signal(reactor_boot_task, 1UL << reactor_boot_sig);  /* boot handshake done */
    if (!ok) return;

    reactor_loop();

    CloseLibrary(SocketBase);
    SocketBase = NULL;
    DeleteMsgPort(reactor_port);
    reactor_port = NULL;
}

/* Lazily spin up the reactor.  Guarded by a mutex so concurrent first-uses
 * from different threads race safely.  Returns 1 if the reactor is ready. */
static int reactor_ensure(void)
{
    if (reactor_port) return 1;
    platform_mutex_lock(reactor_init_mutex);
    if (!reactor_port) {
        reactor_boot_task = FindTask(NULL);
        reactor_boot_sig = AllocSignal(-1);
        if (reactor_boot_sig >= 0) {
            reactor_proc = CreateNewProcTags(
                NP_Entry,     (ULONG)reactor_entry,
                NP_StackSize, (ULONG)32768,
                NP_Name,      (ULONG)"clamiga_sockets",
                TAG_DONE);
            if (reactor_proc)
                Wait(1UL << reactor_boot_sig);   /* until reactor publishes the port */
            FreeSignal(reactor_boot_sig);
            reactor_boot_sig = -1;
        }
    }
    platform_mutex_unlock(reactor_init_mutex);
    return reactor_port != NULL;
}

/* ===== Client side (any thread): marshal a request and block on the reply ===== */

static void sock_call(SockReq *req)
{
    struct MsgPort rp;
    BYTE sig;

    req->result = -1;
    req->out_slot = PLATFORM_SOCKET_INVALID;
    if (!reactor_ensure()) return;

    sig = AllocSignal(-1);
    if (sig < 0) return;

    /* Stack-local reply port — valid for the whole call since we block until
     * the reactor replies. */
    rp.mp_Node.ln_Type = NT_MSGPORT;
    rp.mp_Node.ln_Pri  = 0;
    rp.mp_Node.ln_Name = NULL;
    rp.mp_Flags        = PA_SIGNAL;
    rp.mp_SigBit       = sig;
    rp.mp_SigTask      = FindTask(NULL);
    rp.mp_MsgList.lh_Head     = (struct Node *)&rp.mp_MsgList.lh_Tail;
    rp.mp_MsgList.lh_Tail     = NULL;
    rp.mp_MsgList.lh_TailPred = (struct Node *)&rp.mp_MsgList.lh_Head;

    req->msg.mn_Node.ln_Type = NT_MESSAGE;
    req->msg.mn_Length       = sizeof(*req);
    req->msg.mn_ReplyPort    = &rp;

    PutMsg(reactor_port, &req->msg);
    cl_gc_enter_safe_region();
    WaitPort(&rp);
    cl_gc_leave_safe_region();
    GetMsg(&rp);
    FreeSignal(sig);
}

/* Flush the slot's pending write buffer to the wire via the reactor. */
static int sock_flush(PlatformSocket sh)
{
    IOBuf *b = socket_buf[sh];
    SockReq req;
    if (!b || b->wlen == 0) return 0;
    memset(&req, 0, sizeof(req));
    req.op = REQ_WRITE; req.slot = sh; req.buf = b->wbuf; req.len = (uint32_t)b->wlen;
    req.timeout_ms = socket_wtimeout[sh];
    sock_call(&req);
    /* On timeout the bytes were not fully sent; drop the buffer either way (the
     * stream is being torn down) and report the distinct timeout code. */
    b->wlen = 0;
    if (req.result == PLATFORM_SOCKET_TIMEOUT) return PLATFORM_SOCKET_TIMEOUT;
    return req.result;
}

void platform_socket_set_timeout(PlatformSocket sh, int read_ms, int write_ms)
{
    if (sh == 0 || sh >= PLATFORM_SOCKET_TABLE_SIZE) return;
    socket_rtimeout[sh] = (read_ms  > 0) ? read_ms  : 0;
    socket_wtimeout[sh] = (write_ms > 0) ? write_ms : 0;
}

/* All public entry points run on arbitrary client threads.  They never call
 * bsdsocket — they marshal a request to the reactor and block on the reply.
 * Read/write buffering stays caller-side (in the slot's IOBuf), so only a
 * buffer refill/flush costs a reactor round-trip, not every byte. */

PlatformSocket platform_socket_connect(const char *host, int port, int connect_ms)
{
    SockReq req;
    /* Copy hostname onto the stack so sock_call's cl_gc_enter_safe_region()
     * cannot invalidate the pointer if the GC compacts the arena. */
    char host_buf[256];
    strncpy(host_buf, host, sizeof(host_buf) - 1);
    host_buf[sizeof(host_buf) - 1] = '\0';
    memset(&req, 0, sizeof(req));
    req.op = REQ_CONNECT; req.host = host_buf; req.port = port;
    /* connect_ms > 0 arms a deadline on the parked handshake (see
     * reactor_start_connect / reactor_expire_deadlines); 0 waits forever. */
    req.timeout_ms = connect_ms;
    sock_call(&req);
    return req.out_slot;
}

void platform_socket_close(PlatformSocket sh)
{
    SockReq req;
    if (sh == 0 || sh >= PLATFORM_SOCKET_TABLE_SIZE) return;
    sock_flush(sh);                       /* push any buffered output first */
    memset(&req, 0, sizeof(req));
    req.op = REQ_CLOSE; req.slot = sh;
    sock_call(&req);
}

int platform_socket_read(PlatformSocket sh)
{
    IOBuf *b;
    if (sh == 0 || sh >= PLATFORM_SOCKET_TABLE_SIZE) return -1;
    b = socket_buf[sh];
    if (!b) return -1;
    if (b->rpos < b->rlen)
        return (unsigned char)b->rbuf[b->rpos++];
    /* Refill: the reactor recv()s directly into rbuf (shared address space). */
    {
        SockReq req;
        memset(&req, 0, sizeof(req));
        req.op = REQ_READFILL; req.slot = sh;
        req.buf = b->rbuf; req.len = PLATFORM_IOBUF_SIZE;
        req.timeout_ms = socket_rtimeout[sh];
        sock_call(&req);
        if (req.result == PLATFORM_SOCKET_TIMEOUT) return PLATFORM_SOCKET_TIMEOUT;
        if (req.result <= 0) return -1;   /* EOF or error */
        b->rpos = 1;
        b->rlen = req.result;
        return (unsigned char)b->rbuf[0];
    }
}

int platform_socket_data_available(PlatformSocket sh)
{
    IOBuf *b;
    SockReq req;
    if (sh == 0 || sh >= PLATFORM_SOCKET_TABLE_SIZE) return -1;
    b = socket_buf[sh];
    if (b && b->rpos < b->rlen)
        return 1;                       /* already-buffered bytes */
    /* Probe the fd via the reactor (it owns the WaitSelect/recv calls). */
    memset(&req, 0, sizeof(req));
    req.op = REQ_POLL; req.slot = sh;
    sock_call(&req);
    return req.result;                  /* 1 ready, 0 would-block, 2 EOF, -1 invalid */
}

int platform_socket_write(PlatformSocket sh, int byte)
{
    IOBuf *b;
    if (sh == 0 || sh >= PLATFORM_SOCKET_TABLE_SIZE) return -1;
    b = socket_buf[sh];
    if (!b) return -1;
    b->wbuf[b->wlen++] = (char)byte;
    if (b->wlen >= PLATFORM_IOBUF_SIZE)
        return sock_flush(sh);
    return 0;
}

int platform_socket_write_buf(PlatformSocket sh, const char *buf, uint32_t len)
{
    IOBuf *b;
    uint32_t pos = 0;
    if (sh == 0 || sh >= PLATFORM_SOCKET_TABLE_SIZE) return -1;
    b = socket_buf[sh];
    if (!b) return -1;
    while (pos < len) {
        int avail = PLATFORM_IOBUF_SIZE - b->wlen;
        int chunk = (int)(len - pos);
        if (chunk > avail) chunk = avail;
        memcpy(b->wbuf + b->wlen, buf + pos, (size_t)chunk);
        b->wlen += chunk;
        pos += (uint32_t)chunk;
        if (b->wlen >= PLATFORM_IOBUF_SIZE) {
            int fr = sock_flush(sh);
            if (fr != 0) return fr;     /* propagate -1 (error) or -2 (timeout) */
        }
    }
    return 0;
}

int platform_socket_flush(PlatformSocket sh)
{
    if (sh == 0 || sh >= PLATFORM_SOCKET_TABLE_SIZE) return -1;
    return sock_flush(sh);
}

PlatformSocket platform_socket_listen(int port, int loopback, int *actual_port)
{
    SockReq req;
    memset(&req, 0, sizeof(req));
    req.op = REQ_LISTEN; req.port = port; req.loopback = loopback;
    sock_call(&req);
    if (req.out_slot != PLATFORM_SOCKET_INVALID && actual_port)
        *actual_port = req.out_port;
    return req.out_slot;
}

PlatformSocket platform_socket_accept(PlatformSocket listener)
{
    SockReq req;
    if (listener == 0 || listener >= PLATFORM_SOCKET_TABLE_SIZE)
        return PLATFORM_SOCKET_INVALID;
    memset(&req, 0, sizeof(req));
    req.op = REQ_ACCEPT; req.slot = listener;
    sock_call(&req);
    return req.out_slot;
}

void platform_init(void)
{
    /* Nothing needed — dos.library is auto-opened by startup */
    platform_mutex_init(&reactor_init_mutex);
}

void platform_shutdown(void)
{
    /* Tell the reactor to close every socket, drop SocketBase, and exit.  The
     * reactor replies before tearing down, so this returns once it is done. */
    if (reactor_port) {
        SockReq req;
        memset(&req, 0, sizeof(req));
        req.op = REQ_SHUTDOWN;
        sock_call(&req);
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

uint32_t platform_ffi_register(void *ptr)
{
    /* Handles are raw addresses on Amiga — no side table. */
    return (uint32_t)ptr;
}

void platform_ffi_release(uint32_t handle)
{
    /* Nothing to reclaim — handles are raw addresses. */
    (void)handle;
}

/* Dynamic-library / generic-call FFI is not available on AmigaOS — the
 * Amiga path uses the library-vector model (platform_amiga_call) instead. */
uint32_t platform_ffi_dlopen(const char *name)
{
    (void)name;
    return 0;
}

uint32_t platform_ffi_dlsym(uint32_t lib_handle, const char *name)
{
    (void)lib_handle; (void)name;
    return 0;
}

void platform_ffi_dlclose(uint32_t lib_handle)
{
    (void)lib_handle;
}

int platform_ffi_call(void *fn, CLFFIType ret_type, CLFFIValue *ret_val,
                      int nargs, int nfixed,
                      const CLFFIType *arg_types, const CLFFIValue *arg_vals)
{
    (void)fn; (void)ret_type; (void)ret_val;
    (void)nargs; (void)nfixed; (void)arg_types; (void)arg_vals;
    return -1;  /* unsupported */
}

void *platform_ffi_make_closure(CLFFIType ret_type, int nargs,
                                const CLFFIType *arg_types,
                                platform_ffi_cb_handler handler,
                                void *user_data, void **out_closure)
{
    (void)ret_type; (void)nargs; (void)arg_types;
    (void)handler; (void)user_data;
    if (out_closure) *out_closure = NULL;
    return NULL;  /* unsupported */
}

void platform_ffi_free_closure(void *closure)
{
    (void)closure;
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
 * (68k assembly trampoline for register-based library calls).
 *
 * On MorphOS (PPC) there is no m68k register-based trampoline: MorphOS
 * shared libraries are PPC-native and use a completely different ABI, so
 * the d0-d7/a0-a5 register convention does not apply.  We provide a stub
 * here purely so the binary links; the AMIGA package's register-based
 * library-call FFI (defcfun / %ffi-call -> OP_AMIGA_CALL) is NOT yet
 * supported on MorphOS and needs a real PPC dispatch.  Until then the
 * stub behaves like the POSIX one (returns 0).  Everything else in the
 * runtime — the bytecode VM, reader, compiler, GC, threading, generic
 * FFI memory access — works normally. */
#ifdef PLATFORM_MORPHOS
uint32_t platform_amiga_call(uint32_t lib_base, int16_t offset,
                             uint32_t *regs, uint16_t reg_mask)
{
    (void)lib_base; (void)offset; (void)regs; (void)reg_mask;
    return 0;  /* TODO: PPC-native MorphOS library-call dispatch */
}
#endif

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

void platform_cache_clear(void *addr, uint32_t len)
{
    (void)addr; (void)len;
    /* CacheClearU flushes the full I and D caches.  Per-range
     * CacheClearE exists from V37 but takes more code and is no faster
     * on the buffer sizes the JIT produces.  Safe no-op on 68020/030
     * where the I-cache isn't write-back. */
    CacheClearU();
}
