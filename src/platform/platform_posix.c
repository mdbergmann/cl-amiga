#include "platform.h"
#include "platform_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <glob.h>
#include <limits.h>

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
static int socket_flush_wbuf(PlatformSocket sh)
{
    IOBuf *b;
    int fd;
    ssize_t total = 0;
    if (sh == 0 || sh >= PLATFORM_SOCKET_TABLE_SIZE) return -1;
    b = socket_buf[sh];
    if (!b || b->wlen == 0) return 0;
    fd = socket_table[sh];
    /* write() can block when the peer's receive window is full — bracket the
     * whole drain loop so a slow reader cannot stall a stop-the-world GC. */
    cl_gc_enter_safe_region();
    while (total < b->wlen) {
        ssize_t n = write(fd, b->wbuf + total, (size_t)(b->wlen - total));
        if (n <= 0) { cl_gc_leave_safe_region(); return -1; }
        total += n;
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
        socket_table_unlock();
        if (fd >= 0) close(fd);
        iobuf_free(buf);
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
        /* Refill read buffer.  The read() blocks until data arrives, so bracket
         * it as a GC safe region; capture the fd first since a concurrent close
         * could clear the slot while we are parked. */
        {
            int fd = socket_table[sh];
            ssize_t n;
            cl_gc_enter_safe_region();
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
        n = read(fd, &byte, 1);
        cl_gc_leave_safe_region();
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
                if (socket_flush_wbuf(sh) != 0) return -1;
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

#define FFI_MEM_TABLE_SIZE 1024

static struct {
    void    *ptr;
    uint32_t size;
} ffi_mem_table[FFI_MEM_TABLE_SIZE];

uint32_t platform_ffi_alloc(uint32_t size)
{
    int i;
    void *p;
    if (size == 0) return 0;
    p = malloc((size_t)size);
    if (!p) return 0;
    memset(p, 0, (size_t)size);
    for (i = 0; i < FFI_MEM_TABLE_SIZE; i++) {
        if (!ffi_mem_table[i].ptr) {
            ffi_mem_table[i].ptr = p;
            ffi_mem_table[i].size = size;
            return (uint32_t)(i + 1);  /* 1-based handle */
        }
    }
    free(p);  /* Table full */
    return 0;
}

void platform_ffi_free(uint32_t handle, uint32_t size)
{
    (void)size;
    if (handle == 0 || handle > FFI_MEM_TABLE_SIZE) return;
    if (ffi_mem_table[handle - 1].ptr) {
        free(ffi_mem_table[handle - 1].ptr);
        ffi_mem_table[handle - 1].ptr = NULL;
        ffi_mem_table[handle - 1].size = 0;
    }
}

void *platform_ffi_resolve(uint32_t handle)
{
    if (handle == 0 || handle > FFI_MEM_TABLE_SIZE) return NULL;
    return ffi_mem_table[handle - 1].ptr;
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
