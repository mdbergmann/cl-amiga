#include "platform.h"
#include "platform_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <glob.h>
#include <limits.h>
#include <dlfcn.h>
#include <pthread.h>
#include <ffi.h>

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
    char *r;
    /* Blocking stdin read — bracket with a GC safe region exactly like the
     * socket syscalls below.  cl_repl() (and the SLDB/inspect prompts) park
     * here in fgets, often the main thread under a `tail -f /dev/null | clamiga`
     * launcher.  Without the bracket, a stop-the-world GC fired by any other
     * thread (e.g. a :spawn worker printing a backtrace into SLDB) waits
     * forever for this thread to reach a safepoint it can never reach. */
    cl_gc_enter_safe_region();
    r = fgets(buf, bufsize, stdin);
    cl_gc_leave_safe_region();
    if (!r)
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
    int c;
    /* Same blocking-stdin rationale as platform_read_line: the CONSOLE stream's
     * read-char parks here, so bracket it as a GC safe region. */
    cl_gc_enter_safe_region();
    c = getchar();
    cl_gc_leave_safe_region();
    return c;
}

void platform_ungetchar(int ch)
{
    ungetc(ch, stdin);
}

void platform_drain_input(void)
{
    /* No-op on POSIX — stdin doesn't have residual CLI data */
}

int platform_stdin_is_interactive(void)
{
    return isatty(fileno(stdin)) ? 1 : 0;
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

/* --- I/O buffer for sockets --- */

#define PLATFORM_IOBUF_SIZE 4096

typedef struct {
    char *rbuf;     /* read buffer (malloc'd) */
    int   rpos;     /* current read position */
    int   rlen;     /* valid bytes in read buffer */
    char *wbuf;     /* write buffer (malloc'd) */
    int   wlen;     /* pending bytes in write buffer */
} IOBuf;

static IOBuf *iobuf_alloc(void)
{
    IOBuf *b = (IOBuf *)malloc(sizeof(IOBuf));
    if (!b) return NULL;
    b->rbuf = (char *)malloc(PLATFORM_IOBUF_SIZE);
    b->wbuf = (char *)malloc(PLATFORM_IOBUF_SIZE);
    if (!b->rbuf || !b->wbuf) {
        free(b->rbuf);
        free(b->wbuf);
        free(b);
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
        free(b->rbuf);
        free(b->wbuf);
        free(b);
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

int platform_file_write_buf(PlatformFile fh, const char *buf, uint32_t len)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh])
        return (fwrite(buf, 1, (size_t)len, file_table[fh]) == (size_t)len) ? 0 : -1;
    return -1;
}

int platform_file_flush(PlatformFile fh)
{
    if (fh > 0 && fh < PLATFORM_FILE_TABLE_SIZE && file_table[fh])
        return (fflush(file_table[fh]) == 0) ? 0 : -1;
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
    /* GLOB_MARK appends '/' to directory entries so callers can
       distinguish directories from files (needed for CL DIRECTORY). */
    if (glob(pattern, GLOB_MARK, NULL, &g) != 0) {
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

const char *platform_realpath(const char *path, char *buf, int bufsize)
{
    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) return NULL;
    strncpy(buf, resolved, (size_t)bufsize - 1);
    buf[bufsize - 1] = '\0';
    return buf;
}

const char *platform_expand_home(const char *path, char *buf, int bufsize)
{
    const char *home;
    size_t hlen, plen;

    if (!path || path[0] != '~') return path;
    /* Only expand ~/... or bare ~ (not ~user) */
    if (path[1] != '\0' && path[1] != '/') return path;

    home = getenv("HOME");
    if (!home) home = "/";
    hlen = strlen(home);
    plen = strlen(path + 1); /* everything after ~ */

    if ((int)(hlen + plen + 1) > bufsize) return path;
    memcpy(buf, home, hlen);
    /* path+1 is either "" or "/rest..." — copy including NUL */
    memcpy(buf + hlen, path + 1, plen + 1);
    return buf;
}

/* --- TCP Socket I/O --- */

#define PLATFORM_SOCKET_TABLE_SIZE 16

static int socket_table[PLATFORM_SOCKET_TABLE_SIZE];
static IOBuf *socket_buf[PLATFORM_SOCKET_TABLE_SIZE];
/* Per-socket read/write timeouts in milliseconds; 0 = block indefinitely. */
static int socket_rtimeout[PLATFORM_SOCKET_TABLE_SIZE];
static int socket_wtimeout[PLATFORM_SOCKET_TABLE_SIZE];
static int socket_table_init = 0;

/* Serialises slot claim (connect/listen/accept) and slot free (close) so a
 * threaded server — e.g. an accept loop on one thread while another connects
 * — can never race two claims onto the same table index.  Only the table
 * mutation is guarded; the blocking syscalls (connect/accept/flush/close) and
 * the per-byte read/write paths (each on its own caller-owned slot) run
 * unlocked.  Initialised on first use, which is single-threaded. */
static void *socket_table_mutex = NULL;

static void socket_table_lock(void)
{
    if (socket_table_mutex) platform_mutex_lock(socket_table_mutex);
}

static void socket_table_unlock(void)
{
    if (socket_table_mutex) platform_mutex_unlock(socket_table_mutex);
}

static void socket_table_ensure_init(void)
{
    if (!socket_table_init) {
        int i;
        for (i = 0; i < PLATFORM_SOCKET_TABLE_SIZE; i++) {
            socket_table[i] = -1;
            socket_buf[i] = NULL;
            socket_rtimeout[i] = 0;
            socket_wtimeout[i] = 0;
        }
        platform_mutex_init(&socket_table_mutex);
        socket_table_init = 1;
    }
}

PlatformSocket platform_socket_connect(const char *host, int port)
{
    struct hostent *he;
    struct sockaddr_in addr;
    int fd, i;

    socket_table_ensure_init();

    /* DNS resolution can block on the network — stay GC-cooperative. */
    cl_gc_enter_safe_region();
    he = gethostbyname(host);
    cl_gc_leave_safe_region();
    if (!he) return PLATFORM_SOCKET_INVALID;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return PLATFORM_SOCKET_INVALID;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);

    {
        int rc;
        cl_gc_enter_safe_region();
        rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
        cl_gc_leave_safe_region();
        if (rc < 0) {
            close(fd);
            return PLATFORM_SOCKET_INVALID;
        }
    }

    /* Find free slot (slot 0 reserved as INVALID) */
    socket_table_lock();
    for (i = 1; i < PLATFORM_SOCKET_TABLE_SIZE; i++) {
        if (socket_table[i] == -1) {
            socket_table[i] = fd;
            socket_buf[i] = iobuf_alloc();
            socket_table_unlock();
            return (PlatformSocket)i;
        }
    }
    socket_table_unlock();

    close(fd);
    return PLATFORM_SOCKET_INVALID;
}

/* Flush socket write buffer to the wire */
/* Forward declaration: readiness wait shared by read and write paths. */
static int socket_wait_ready(int fd, int timeout_ms, int want_write);

static int socket_flush_wbuf(PlatformSocket sh)
{
    IOBuf *b;
    int fd, wtimeout;
    ssize_t total = 0;
    if (sh == 0 || sh >= PLATFORM_SOCKET_TABLE_SIZE) return -1;
    b = socket_buf[sh];
    if (!b || b->wlen == 0) return 0;
    fd = socket_table[sh];
    wtimeout = socket_wtimeout[sh];
    /* write() can block when the peer's receive window is full — bracket the
     * whole drain loop so a slow reader cannot stall a stop-the-world GC. */
    cl_gc_enter_safe_region();
    if (wtimeout > 0) {
        /* Timed drain.  A blocking write() would stall until ALL bytes fit even
         * after select() reports only a sliver of space, so use non-blocking
         * send() + select() instead.  All waits share ONE absolute deadline:
         * select() can spuriously report a loopback socket writable while
         * send() still returns EAGAIN, so a per-call timeout could spin forever
         * — the shared deadline guarantees the loop terminates within wtimeout. */
        uint32_t deadline = platform_time_ms() + (uint32_t)wtimeout;
        while (total < b->wlen) {
            ssize_t n = send(fd, b->wbuf + total, (size_t)(b->wlen - total), MSG_DONTWAIT);
            if (n > 0) { total += n; continue; }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                int32_t rem = (int32_t)(deadline - platform_time_ms());
                int rr;
                if (rem <= 0) { cl_gc_leave_safe_region(); return PLATFORM_SOCKET_TIMEOUT; }
                rr = socket_wait_ready(fd, rem, 1);
                if (rr < 0)  { cl_gc_leave_safe_region(); return -1; }
                if (rr == 0) { cl_gc_leave_safe_region(); return PLATFORM_SOCKET_TIMEOUT; }
                continue;
            }
            cl_gc_leave_safe_region();
            return -1;   /* n == 0 or a real send() error */
        }
    } else {
        while (total < b->wlen) {
            ssize_t n = write(fd, b->wbuf + total, (size_t)(b->wlen - total));
            if (n <= 0) { cl_gc_leave_safe_region(); return -1; }
            total += n;
        }
    }
    cl_gc_leave_safe_region();
    b->wlen = 0;
    return 0;
}

void platform_socket_close(PlatformSocket sh)
{
    if (sh > 0 && sh < PLATFORM_SOCKET_TABLE_SIZE && socket_table[sh] >= 0) {
        int fd;
        IOBuf *buf;
        socket_flush_wbuf(sh);
        /* Detach the slot under the lock, then do the blocking close() and
         * free() outside it. */
        socket_table_lock();
        fd = socket_table[sh];
        buf = socket_buf[sh];
        socket_table[sh] = -1;
        socket_buf[sh] = NULL;
        socket_rtimeout[sh] = 0;
        socket_wtimeout[sh] = 0;
        socket_table_unlock();
        if (fd >= 0) close(fd);
        iobuf_free(buf);
    }
}

void platform_socket_set_timeout(PlatformSocket sh, int read_ms, int write_ms)
{
    if (sh == 0 || sh >= PLATFORM_SOCKET_TABLE_SIZE) return;
    socket_table_lock();
    socket_rtimeout[sh] = (read_ms  > 0) ? read_ms  : 0;
    socket_wtimeout[sh] = (write_ms > 0) ? write_ms : 0;
    socket_table_unlock();
}

/* Wait until `fd` is readable (want_write==0) or writable (want_write==1),
 * up to timeout_ms.  Returns 1 ready, 0 timed out, -1 error/invalid.  The
 * select() blocks, so the caller must already be in a GC safe region.
 * An absolute deadline is computed from timeout_ms so that EINTR retries
 * consume elapsed time rather than resetting the full timeout each iteration. */
static int socket_wait_ready(int fd, int timeout_ms, int want_write)
{
    fd_set fds;
    struct timeval tv;
    int r;
    uint32_t deadline;
    int32_t rem;
    if (fd < 0 || fd >= FD_SETSIZE)
        return -1;
    deadline = platform_time_ms() + (uint32_t)timeout_ms;
    for (;;) {
        rem = (int32_t)(deadline - platform_time_ms());
        if (rem <= 0) return 0;             /* deadline elapsed */
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_sec  = rem / 1000;
        tv.tv_usec = (rem % 1000) * 1000;
        r = select(fd + 1, want_write ? NULL : &fds,
                          want_write ? &fds : NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;   /* retry, deadline unchanged */
            return -1;
        }
        if (r == 0) return 0;               /* timed out */
        return FD_ISSET(fd, &fds) ? 1 : 0;
    }
}

int platform_socket_read(PlatformSocket sh)
{
    IOBuf *b;
    int rtimeout;
    if (sh == 0 || sh >= PLATFORM_SOCKET_TABLE_SIZE || socket_table[sh] < 0)
        return -1;
    b = socket_buf[sh];
    rtimeout = socket_rtimeout[sh];
    if (b) {
        if (b->rpos < b->rlen)
            return (unsigned char)b->rbuf[b->rpos++];
        /* Refill read buffer.  The read() blocks until data arrives, so bracket
         * it as a GC safe region; capture the fd first since a concurrent close
         * could clear the slot while we are parked. */
        {
            int fd = socket_table[sh];
            ssize_t n;
            cl_gc_enter_safe_region();
            if (rtimeout > 0) {
                int rr = socket_wait_ready(fd, rtimeout, 0);
                if (rr <= 0) {
                    cl_gc_leave_safe_region();
                    return (rr == 0) ? PLATFORM_SOCKET_TIMEOUT : -1;
                }
            }
            n = read(fd, b->rbuf, PLATFORM_IOBUF_SIZE);
            cl_gc_leave_safe_region();
            if (n <= 0) return -1;
            b->rpos = 1;
            b->rlen = (int)n;
            return (unsigned char)b->rbuf[0];
        }
    }
    /* Fallback: no buffer */
    {
        int fd = socket_table[sh];
        unsigned char byte;
        ssize_t n;
        cl_gc_enter_safe_region();
        if (rtimeout > 0) {
            int rr = socket_wait_ready(fd, rtimeout, 0);
            if (rr <= 0) {
                cl_gc_leave_safe_region();
                return (rr == 0) ? PLATFORM_SOCKET_TIMEOUT : -1;
            }
        }
        n = read(fd, &byte, 1);
        cl_gc_leave_safe_region();
        if (n <= 0) return -1;
        return (int)byte;
    }
}

/* Non-blocking readiness probe, used to back CL:LISTEN on socket streams.
 * Returns:
 *    1  data is available — a read returns a byte now (or, for a listener
 *       slot, a client connection is pending and accept() won't block);
 *    0  a read/accept would block (nothing ready yet);
 *    2  at end of file — the peer closed and a read returns EOF immediately;
 *   -1  invalid handle.
 * Uses select() with a zero timeout (never blocks, no GC safe region needed).
 * select() reports a half-closed peer as "readable", so for a connection
 * socket we MSG_PEEK one byte to tell real data (1) from EOF (2); a listener
 * has no IOBuf and is never peeked (recv on a listen fd is invalid) — a
 * readable listener always means a pending connection. */
int platform_socket_data_available(PlatformSocket sh)
{
    IOBuf *b;
    int fd;
    fd_set rset;
    struct timeval tv;
    int r;
    if (sh == 0 || sh >= PLATFORM_SOCKET_TABLE_SIZE || socket_table[sh] < 0)
        return -1;
    b = socket_buf[sh];
    if (b && b->rpos < b->rlen)
        return 1;                       /* already-buffered bytes */
    fd = socket_table[sh];
    if (fd < 0 || fd >= FD_SETSIZE)
        return -1;
    FD_ZERO(&rset);
    FD_SET(fd, &rset);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    r = select(fd + 1, &rset, NULL, NULL, &tv);
    if (r < 0) {
        if (errno == EINTR) return 0;   /* treat as "not ready yet" */
        return -1;
    }
    if (r == 0 || !FD_ISSET(fd, &rset))
        return 0;                       /* would block */
    if (!b)
        return 1;                       /* listener: connection pending */
    /* Connection socket is readable: distinguish data from a closed peer. */
    {
        char peek;
        ssize_t pn = recv(fd, &peek, 1, MSG_PEEK);
        if (pn > 0)  return 1;          /* real data waiting */
        if (pn == 0) return 2;          /* peer closed => EOF */
        if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
            return 0;                   /* spurious wakeup */
        return 2;                       /* error => report as EOF-ish */
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
        int fd = socket_table[sh];
        unsigned char bb = (unsigned char)byte;
        ssize_t n;
        cl_gc_enter_safe_region();
        n = write(fd, &bb, 1);
        cl_gc_leave_safe_region();
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
                int fr = socket_flush_wbuf(sh);
                if (fr != 0) return fr;     /* propagate -1 (error) or -2 (timeout) */
            }
        }
        return 0;
    }
    /* Fallback: direct write */
    {
        ssize_t total = 0;
        int fd = socket_table[sh];
        cl_gc_enter_safe_region();
        while ((uint32_t)total < len) {
            ssize_t n = write(fd, buf + total, (size_t)(len - (uint32_t)total));
            if (n <= 0) { cl_gc_leave_safe_region(); return -1; }
            total += n;
        }
        cl_gc_leave_safe_region();
        return 0;
    }
}

int platform_socket_flush(PlatformSocket sh)
{
    return socket_flush_wbuf(sh);
}

PlatformSocket platform_socket_listen(int port, int loopback, int *actual_port)
{
    struct sockaddr_in addr;
    int fd, i, on = 1;

    socket_table_ensure_init();

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return PLATFORM_SOCKET_INVALID;

    /* Allow immediate rebind after the server restarts (TIME_WAIT). */
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(loopback ? INADDR_LOOPBACK : INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return PLATFORM_SOCKET_INVALID;
    }
    if (listen(fd, 4) < 0) {
        close(fd);
        return PLATFORM_SOCKET_INVALID;
    }
    if (actual_port) {
        socklen_t alen = sizeof(addr);
        if (getsockname(fd, (struct sockaddr *)&addr, &alen) == 0)
            *actual_port = ntohs(addr.sin_port);
        else
            *actual_port = port;
    }

    /* Listener occupies a slot but needs no IOBuf — it is never read/written. */
    socket_table_lock();
    for (i = 1; i < PLATFORM_SOCKET_TABLE_SIZE; i++) {
        if (socket_table[i] == -1) {
            socket_table[i] = fd;
            socket_buf[i] = NULL;
            socket_table_unlock();
            return (PlatformSocket)i;
        }
    }
    socket_table_unlock();

    close(fd);
    return PLATFORM_SOCKET_INVALID;
}

PlatformSocket platform_socket_accept(PlatformSocket listener)
{
    struct sockaddr_in caddr;
    socklen_t clen = sizeof(caddr);
    int fd, i;

    if (listener == 0 || listener >= PLATFORM_SOCKET_TABLE_SIZE ||
        socket_table[listener] < 0)
        return PLATFORM_SOCKET_INVALID;

    /* accept() blocks — must run outside the table lock, and as a GC safe
     * region so a thread parked here waiting for a client does not stall a
     * concurrent stop-the-world GC.  This is the SLY read-loop deadlock. */
    {
        int lfd = socket_table[listener];
        cl_gc_enter_safe_region();
        fd = accept(lfd, (struct sockaddr *)&caddr, &clen);
        cl_gc_leave_safe_region();
    }
    if (fd < 0) return PLATFORM_SOCKET_INVALID;

    socket_table_lock();
    for (i = 1; i < PLATFORM_SOCKET_TABLE_SIZE; i++) {
        if (socket_table[i] == -1) {
            socket_table[i] = fd;
            socket_buf[i] = iobuf_alloc();
            socket_table_unlock();
            return (PlatformSocket)i;
        }
    }
    socket_table_unlock();

    close(fd);
    return PLATFORM_SOCKET_INVALID;
}

void platform_init(void)
{
    /* Nothing needed on POSIX */
}

void platform_shutdown(void)
{
    /* Nothing needed on POSIX */
}

void platform_cache_clear(void *addr, uint32_t len)
{
    (void)addr; (void)len;
    /* POSIX hosts running clamiga don't execute JIT-emitted m68k code;
     * the JIT only compiles when -DJIT_M68K is on (cross build). */
}

/* =============================================================
 * Generic FFI: foreign memory (POSIX implementation)
 *
 * On 64-bit POSIX, pointers don't fit in uint32_t, so we use a
 * side table that maps handles (1-based indices) to real pointers.
 * ============================================================= */

/*
 * On 64-bit POSIX a real pointer doesn't fit in the 32-bit CL_ForeignPtr
 * address field, so foreign pointers carry a 1-based handle into this side
 * table.  The table holds both memory we allocated (OWNED — freed by
 * platform_ffi_free) and externally-owned pointers we merely reference
 * (registered via platform_ffi_register — released, never freed, by
 * platform_ffi_release).
 *
 * The table grows on demand and recycles slots through a free-list so heavy
 * pointer arithmetic (CFFI inc-pointer / mem-aref) doesn't exhaust it.  A
 * mutex guards concurrent access from MP threads; the GC finalizer runs
 * world-stopped so it could skip the lock, but taking it there is harmless.
 */
typedef struct {
    void    *ptr;        /* real pointer; NULL when the slot is free */
    uint32_t size;       /* allocation size for OWNED memory, else 0 */
    uint32_t next_free;  /* free-list link (1-based), 0 = end; valid when free */
} FFIMemEntry;

static FFIMemEntry *ffi_mem_table   = NULL;
static uint32_t     ffi_mem_cap     = 0;   /* allocated slot count */
static uint32_t     ffi_mem_hi      = 0;   /* high-water: slots ever handed out */
static uint32_t     ffi_mem_free    = 0;   /* free-list head (1-based), 0 = empty */
static pthread_mutex_t ffi_mem_lock = PTHREAD_MUTEX_INITIALIZER;

/* Insert PTR (size for owned, 0 for external) and return a fresh 1-based
 * handle, or 0 on out-of-memory.  Caller holds ffi_mem_lock. */
static uint32_t ffi_table_insert_locked(void *ptr, uint32_t size)
{
    uint32_t h;
    if (ffi_mem_free != 0) {
        h = ffi_mem_free;
        ffi_mem_free = ffi_mem_table[h - 1].next_free;
    } else {
        if (ffi_mem_hi >= ffi_mem_cap) {
            uint32_t newcap = ffi_mem_cap ? ffi_mem_cap * 2 : 1024;
            FFIMemEntry *t = (FFIMemEntry *)realloc(ffi_mem_table,
                                                    (size_t)newcap * sizeof(FFIMemEntry));
            if (!t) return 0;
            ffi_mem_table = t;
            ffi_mem_cap = newcap;
        }
        h = ++ffi_mem_hi;  /* 1-based */
    }
    ffi_mem_table[h - 1].ptr = ptr;
    ffi_mem_table[h - 1].size = size;
    ffi_mem_table[h - 1].next_free = 0;
    return h;
}

/* Return a slot to the free-list (does NOT free ptr).  Caller holds lock. */
static void ffi_table_release_locked(uint32_t handle)
{
    if (handle == 0 || handle > ffi_mem_hi) return;
    if (ffi_mem_table[handle - 1].ptr == NULL) return;  /* already free */
    ffi_mem_table[handle - 1].ptr = NULL;
    ffi_mem_table[handle - 1].size = 0;
    ffi_mem_table[handle - 1].next_free = ffi_mem_free;
    ffi_mem_free = handle;
}

uint32_t platform_ffi_alloc(uint32_t size)
{
    void *p;
    uint32_t h;
    if (size == 0) return 0;
    p = malloc((size_t)size);
    if (!p) return 0;
    memset(p, 0, (size_t)size);
    pthread_mutex_lock(&ffi_mem_lock);
    h = ffi_table_insert_locked(p, size);
    pthread_mutex_unlock(&ffi_mem_lock);
    if (h == 0) free(p);  /* table grow failed */
    return h;
}

uint32_t platform_ffi_register(void *ptr)
{
    uint32_t h;
    if (ptr == NULL) return 0;  /* canonical null pointer */
    pthread_mutex_lock(&ffi_mem_lock);
    h = ffi_table_insert_locked(ptr, 0);
    pthread_mutex_unlock(&ffi_mem_lock);
    return h;
}

void platform_ffi_free(uint32_t handle, uint32_t size)
{
    void *p = NULL;
    (void)size;
    if (handle == 0) return;
    pthread_mutex_lock(&ffi_mem_lock);
    if (handle <= ffi_mem_hi)
        p = ffi_mem_table[handle - 1].ptr;
    ffi_table_release_locked(handle);
    pthread_mutex_unlock(&ffi_mem_lock);
    if (p) free(p);
}

void platform_ffi_release(uint32_t handle)
{
    pthread_mutex_lock(&ffi_mem_lock);
    ffi_table_release_locked(handle);
    pthread_mutex_unlock(&ffi_mem_lock);
}

void *platform_ffi_resolve(uint32_t handle)
{
    void *p;
    if (handle == 0) return NULL;
    pthread_mutex_lock(&ffi_mem_lock);
    p = (handle <= ffi_mem_hi) ? ffi_mem_table[handle - 1].ptr : NULL;
    pthread_mutex_unlock(&ffi_mem_lock);
    return p;
}

uint32_t platform_ffi_peek32(uint32_t handle, uint32_t offset)
{
    void *base = platform_ffi_resolve(handle);
    if (!base) return 0;
    return *(uint32_t *)((uint8_t *)base + offset);
}

uint16_t platform_ffi_peek16(uint32_t handle, uint32_t offset)
{
    void *base = platform_ffi_resolve(handle);
    if (!base) return 0;
    return *(uint16_t *)((uint8_t *)base + offset);
}

uint8_t platform_ffi_peek8(uint32_t handle, uint32_t offset)
{
    void *base = platform_ffi_resolve(handle);
    if (!base) return 0;
    return *(uint8_t *)((uint8_t *)base + offset);
}

void platform_ffi_poke32(uint32_t handle, uint32_t offset, uint32_t val)
{
    void *base = platform_ffi_resolve(handle);
    if (!base) return;
    *(uint32_t *)((uint8_t *)base + offset) = val;
}

void platform_ffi_poke16(uint32_t handle, uint32_t offset, uint16_t val)
{
    void *base = platform_ffi_resolve(handle);
    if (!base) return;
    *(uint16_t *)((uint8_t *)base + offset) = val;
}

void platform_ffi_poke8(uint32_t handle, uint32_t offset, uint8_t val)
{
    void *base = platform_ffi_resolve(handle);
    if (!base) return;
    *(uint8_t *)((uint8_t *)base + offset) = val;
}

/* =============================================================
 * Dynamic libraries + foreign function calls (POSIX: dlopen + libffi)
 * ============================================================= */

uint32_t platform_ffi_dlopen(const char *name)
{
    /* RTLD_GLOBAL so symbols become visible to subsequent default-namespace
     * lookups (matches how CFFI expects use-foreign-library to behave). */
    void *h = dlopen(name, RTLD_NOW | RTLD_GLOBAL);
    if (!h) return 0;
    return platform_ffi_register(h);
}

uint32_t platform_ffi_dlsym(uint32_t lib_handle, const char *name)
{
    void *sym;
    void *lib = (lib_handle == 0) ? RTLD_DEFAULT : platform_ffi_resolve(lib_handle);
    if (lib_handle != 0 && lib == NULL) return 0;
    dlerror();  /* clear */
    sym = dlsym(lib, name);
    if (sym == NULL && dlerror() != NULL) return 0;
    return platform_ffi_register(sym);
}

void platform_ffi_dlclose(uint32_t lib_handle)
{
    void *lib = platform_ffi_resolve(lib_handle);
    if (lib) dlclose(lib);
    platform_ffi_release(lib_handle);
}

/* Map a CLFFIType to the corresponding libffi ffi_type. */
static ffi_type *ffi_type_for(CLFFIType t)
{
    switch (t) {
    case CL_FFI_VOID:    return &ffi_type_void;
    case CL_FFI_I8:      return &ffi_type_sint8;
    case CL_FFI_U8:      return &ffi_type_uint8;
    case CL_FFI_I16:     return &ffi_type_sint16;
    case CL_FFI_U16:     return &ffi_type_uint16;
    case CL_FFI_I32:     return &ffi_type_sint32;
    case CL_FFI_U32:     return &ffi_type_uint32;
    case CL_FFI_I64:     return &ffi_type_sint64;
    case CL_FFI_U64:     return &ffi_type_uint64;
    case CL_FFI_FLOAT:   return &ffi_type_float;
    case CL_FFI_DOUBLE:  return &ffi_type_double;
    case CL_FFI_POINTER: return &ffi_type_pointer;
    }
    return &ffi_type_void;
}

int platform_ffi_call(void *fn, CLFFIType ret_type, CLFFIValue *ret_val,
                      int nargs, int nfixed,
                      const CLFFIType *arg_types, const CLFFIValue *arg_vals)
{
    ffi_cif cif;
    ffi_type *atypes[CL_FFI_MAX_ARGS];
    void *avalues[CL_FFI_MAX_ARGS];
    /* libffi widens any integral return narrower than ffi_arg to ffi_arg and
     * writes that many bytes, so the return buffer must be at least that big. */
    union { CLFFIValue v; ffi_arg pad; } rc;
    ffi_status st;
    int i;

    if (!fn) return -1;
    if (nargs < 0 || nargs > CL_FFI_MAX_ARGS) return -1;

    for (i = 0; i < nargs; i++) {
        atypes[i] = ffi_type_for(arg_types[i]);
        /* arg_vals is const; libffi wants a non-const pointer to each value
         * but never writes through it for arguments. */
        avalues[i] = (void *)&arg_vals[i];
    }

    if (nfixed >= 0 && nfixed < nargs) {
        st = ffi_prep_cif_var(&cif, FFI_DEFAULT_ABI, (unsigned)nfixed,
                              (unsigned)nargs, ffi_type_for(ret_type), atypes);
    } else {
        st = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, (unsigned)nargs,
                          ffi_type_for(ret_type), atypes);
    }
    if (st != FFI_OK) return -1;

    rc.pad = 0;
    ffi_call(&cif, FFI_FN(fn), &rc, avalues);

    if (ret_val) {
        /* For small integer returns libffi stored an ffi_arg; narrow it. */
        switch (ret_type) {
        case CL_FFI_VOID:    break;
        case CL_FFI_I8:      ret_val->i8  = (int8_t)(ffi_sarg)rc.pad; break;
        case CL_FFI_U8:      ret_val->u8  = (uint8_t)(ffi_arg)rc.pad; break;
        case CL_FFI_I16:     ret_val->i16 = (int16_t)(ffi_sarg)rc.pad; break;
        case CL_FFI_U16:     ret_val->u16 = (uint16_t)(ffi_arg)rc.pad; break;
        case CL_FFI_I32:     ret_val->i32 = (int32_t)(ffi_sarg)rc.pad; break;
        case CL_FFI_U32:     ret_val->u32 = (uint32_t)(ffi_arg)rc.pad; break;
        case CL_FFI_I64:     ret_val->i64 = rc.v.i64; break;
        case CL_FFI_U64:     ret_val->u64 = rc.v.u64; break;
        case CL_FFI_FLOAT:   ret_val->f   = rc.v.f; break;
        case CL_FFI_DOUBLE:  ret_val->d   = rc.v.d; break;
        case CL_FFI_POINTER: ret_val->p   = rc.v.p; break;
        }
    }
    return 0;
}

/* --- Callbacks: Lisp functions exposed as C function pointers --- */

typedef struct {
    ffi_cif    cif;
    ffi_type  *atypes[CL_FFI_MAX_ARGS];
    CLFFIType  ret_type;
    CLFFIType  arg_types[CL_FFI_MAX_ARGS];
    int        nargs;
    platform_ffi_cb_handler handler;
    void      *user_data;
    ffi_closure *closure;
    void      *code;
} PosixClosure;

/* libffi entry point: decode raw C args, hand them to the generic handler,
 * then write the handler's result back where libffi expects it. */
static void posix_closure_tramp(ffi_cif *cif, void *ret, void **args, void *ud)
{
    PosixClosure *pc = (PosixClosure *)ud;
    CLFFIValue cargs[CL_FFI_MAX_ARGS];
    CLFFIValue cret;
    int i;
    (void)cif;

    for (i = 0; i < pc->nargs; i++) {
        switch (pc->arg_types[i]) {
        case CL_FFI_I8:      cargs[i].i8  = *(int8_t  *)args[i]; break;
        case CL_FFI_U8:      cargs[i].u8  = *(uint8_t *)args[i]; break;
        case CL_FFI_I16:     cargs[i].i16 = *(int16_t *)args[i]; break;
        case CL_FFI_U16:     cargs[i].u16 = *(uint16_t*)args[i]; break;
        case CL_FFI_I32:     cargs[i].i32 = *(int32_t *)args[i]; break;
        case CL_FFI_U32:     cargs[i].u32 = *(uint32_t*)args[i]; break;
        case CL_FFI_I64:     cargs[i].i64 = *(int64_t *)args[i]; break;
        case CL_FFI_U64:     cargs[i].u64 = *(uint64_t*)args[i]; break;
        case CL_FFI_FLOAT:   cargs[i].f   = *(float   *)args[i]; break;
        case CL_FFI_DOUBLE:  cargs[i].d   = *(double  *)args[i]; break;
        case CL_FFI_POINTER: cargs[i].p   = *(void   **)args[i]; break;
        case CL_FFI_VOID:    break;
        }
    }

    memset(&cret, 0, sizeof(cret));
    pc->handler(pc->user_data, cargs, &cret);

    /* libffi widens integral returns to ffi_arg. */
    switch (pc->ret_type) {
    case CL_FFI_VOID:    break;
    case CL_FFI_I8:      *(ffi_arg *)ret = (ffi_arg)(ffi_sarg)cret.i8;  break;
    case CL_FFI_U8:      *(ffi_arg *)ret = (ffi_arg)cret.u8;            break;
    case CL_FFI_I16:     *(ffi_arg *)ret = (ffi_arg)(ffi_sarg)cret.i16; break;
    case CL_FFI_U16:     *(ffi_arg *)ret = (ffi_arg)cret.u16;           break;
    case CL_FFI_I32:     *(ffi_arg *)ret = (ffi_arg)(ffi_sarg)cret.i32; break;
    case CL_FFI_U32:     *(ffi_arg *)ret = (ffi_arg)cret.u32;           break;
    case CL_FFI_I64:     *(int64_t  *)ret = cret.i64; break;
    case CL_FFI_U64:     *(uint64_t *)ret = cret.u64; break;
    case CL_FFI_FLOAT:   *(float    *)ret = cret.f;   break;
    case CL_FFI_DOUBLE:  *(double   *)ret = cret.d;   break;
    case CL_FFI_POINTER: *(void    **)ret = cret.p;   break;
    }
}

void *platform_ffi_make_closure(CLFFIType ret_type, int nargs,
                                const CLFFIType *arg_types,
                                platform_ffi_cb_handler handler,
                                void *user_data, void **out_closure)
{
    PosixClosure *pc;
    int i;
    if (nargs < 0 || nargs > CL_FFI_MAX_ARGS) return NULL;
    pc = (PosixClosure *)calloc(1, sizeof(PosixClosure));
    if (!pc) return NULL;
    pc->ret_type = ret_type;
    pc->nargs = nargs;
    pc->handler = handler;
    pc->user_data = user_data;
    for (i = 0; i < nargs; i++) {
        pc->arg_types[i] = arg_types[i];
        pc->atypes[i] = ffi_type_for(arg_types[i]);
    }
    pc->closure = (ffi_closure *)ffi_closure_alloc(sizeof(ffi_closure), &pc->code);
    if (!pc->closure) { free(pc); return NULL; }
    if (ffi_prep_cif(&pc->cif, FFI_DEFAULT_ABI, (unsigned)nargs,
                     ffi_type_for(ret_type), pc->atypes) != FFI_OK) {
        ffi_closure_free(pc->closure);
        free(pc);
        return NULL;
    }
    if (ffi_prep_closure_loc(pc->closure, &pc->cif, posix_closure_tramp,
                             pc, pc->code) != FFI_OK) {
        ffi_closure_free(pc->closure);
        free(pc);
        return NULL;
    }
    if (out_closure) *out_closure = pc;
    return pc->code;
}

void platform_ffi_free_closure(void *closure)
{
    PosixClosure *pc = (PosixClosure *)closure;
    if (!pc) return;
    if (pc->closure) ffi_closure_free(pc->closure);
    free(pc);
}

/* =============================================================
 * Amiga-specific FFI stubs (not available on POSIX)
 * ============================================================= */

uint32_t platform_amiga_open_library(const char *name, uint32_t version)
{
    (void)name; (void)version;
    return 0;  /* Not available on POSIX */
}

void platform_amiga_close_library(uint32_t lib_base)
{
    (void)lib_base;
}

uint32_t platform_amiga_call(uint32_t lib_base, int16_t offset,
                              uint32_t *regs, uint16_t reg_mask)
{
    (void)lib_base; (void)offset; (void)regs; (void)reg_mask;
    return 0;  /* Not available on POSIX */
}

uint32_t platform_amiga_alloc_chip(uint32_t size)
{
    /* On POSIX, chip memory is just regular memory */
    return platform_ffi_alloc(size);
}

void platform_amiga_free_chip(uint32_t addr, uint32_t size)
{
    platform_ffi_free(addr, size);
}
