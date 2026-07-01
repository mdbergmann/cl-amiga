/*
 * stream.c — CL Stream heap type and output buffer management
 */

#include "stream.h"
#include "symbol.h"
#include "mem.h"
#include "error.h"
#include "package.h"
#include "vm.h"
#include "string_utils.h"
#include "../platform/platform.h"
#include "../platform/platform_thread.h"
#include <string.h>
#ifdef DEBUG_STREAM
#include <stdio.h>
#endif

/* Singleton console streams */
CL_Obj cl_stdin_stream = 0;
CL_Obj cl_stdout_stream = 0;
CL_Obj cl_stderr_stream = 0;

/* --- Output buffer side table --- */

typedef struct {
    char    *data;
    uint32_t capacity;
    uint32_t length;
} CL_OutBuf;

static CL_OutBuf outbuf_table[CL_STREAM_BUF_TABLE_SIZE];
static int stream_initialized = 0;

/* GC mark bitmap for outbuf slots.  The outbuf side table is reclaimed by the
 * GC driven by STREAM LIVENESS, not by finalizing dead corpses: freeing a
 * slot from a dead stream's st->out_buf_handle is unsafe because that handle
 * may already have been reused by a *live* string-output-stream (handles are
 * recycled the moment a slot's data is freed).  Re-finalizing the stale corpse
 * would then free the live stream's buffer, silently truncating its output
 * (observed as chunga READ-LINE* returning "" for a non-empty HTTP header
 * under concurrent GC, desyncing drakma's response framing).  Instead, each GC
 * clears this bitmap, marks the slot of every LIVE output stream, then frees
 * any slot still allocated but unmarked. */
static uint8_t outbuf_inuse[CL_STREAM_BUF_TABLE_SIZE];

/* Creation-window pin.  A freshly allocated outbuf is not yet referenced by
 * any stream object, so the liveness-driven reclaim above would free it out
 * from under the stream being built — a window a *preemptive* stop-the-world
 * GC (AmigaOS task suspension) can hit between cl_stream_alloc_outbuf returning
 * the handle and the caller storing it in st->out_buf_handle.  alloc pins the
 * slot; the caller unpins once the live, GC-reachable stream owns it.  Pinned
 * slots are never reclaimed; mark-begin does NOT clear pins. */
static uint8_t outbuf_pinned[CL_STREAM_BUF_TABLE_SIZE];

/* Mutex protecting outbuf_table and cbuf_table slot allocation/deallocation */
static void *cl_stream_table_mutex = NULL;

/* I/O serialisation locks.
 *
 * A single global lock is wrong here: it is held across the *blocking* read()/
 * write() syscalls (a socket read can park indefinitely waiting for the peer),
 * so one parked thread would wedge every other stream operation — including an
 * unrelated thread writing to the console, or the reply being written back to
 * the very socket that is being read.  That is the SLY :spawn deadlock.
 *
 * Locks are therefore split by stream and by direction, so a blocking syscall
 * only ever holds a lock that no independent operation needs:
 *   - console reads vs. writes are independent (a parked stdin read must not
 *     block another thread's stdout output);
 *   - each socket has its own read and write lock, keyed by socket handle, so
 *     reading a socket never blocks writing it (full-duplex), and one client
 *     connection never blocks another;
 *   - files share one lock (regular-file I/O does not park indefinitely).
 * The locks still serialise concurrent same-direction access, preserving
 * UTF-8 decode atomicity and preventing interleaved console output. */
static void *cl_console_read_mutex = NULL;
static void *cl_console_write_mutex = NULL;
static void *cl_file_io_mutex = NULL;

/* Per-socket read/write locks, keyed by socket handle.  The lock MUST be per
 * handle, not striped (handle % N): a thread parked in a blocking read on one
 * socket must never stall I/O on a different socket, and reading a socket must
 * never block writing it (full duplex) — striping would reintroduce exactly the
 * cross-connection blocking this split was built to avoid (the SLY deadlock).
 *
 * The POSIX socket table grows without a fixed ceiling, so this table grows to
 * match it: a segmented directory of lazily-allocated, never-freed blocks of
 * read/write mutex pairs.  Because blocks are never freed and a block pointer is
 * published only once (fully initialised), a looked-up lock stays valid for the
 * life of the process and the I/O hot path reads it without the table mutex.
 * Amiga handles stay within the first two blocks (handles 1..63). */
typedef struct {
    void *read_mutex;
    void *write_mutex;
} SockLockPair;

#define CL_SOCK_LOCK_BLOCK_SHIFT 5
#define CL_SOCK_LOCK_BLOCK_SIZE  (1 << CL_SOCK_LOCK_BLOCK_SHIFT)   /* 32 handles/block */
#define CL_SOCK_LOCK_BLOCK_MASK  (CL_SOCK_LOCK_BLOCK_SIZE - 1)
#define CL_SOCK_LOCK_MAX_BLOCKS  2048                              /* up to 65536 handles */

static SockLockPair * volatile cl_sock_lock_dir[CL_SOCK_LOCK_MAX_BLOCKS];

/* Resolve a socket handle to its lock pair, lazily allocating and initialising
 * the covering block on first use (under the table mutex, so two threads racing
 * the same new block don't double-allocate it).  Returns NULL for the reserved
 * handle 0, an out-of-range handle, or on allocation failure (caller then runs
 * unlocked, as it did before any locks existed). */
static SockLockPair *sock_lock_pair(uint32_t h)
{
    uint32_t blk = h >> CL_SOCK_LOCK_BLOCK_SHIFT;
    SockLockPair *b;
    if (h == 0 || blk >= CL_SOCK_LOCK_MAX_BLOCKS) return NULL;
    b = cl_sock_lock_dir[blk];
    if (!b) {
        platform_mutex_lock(cl_stream_table_mutex);
        b = cl_sock_lock_dir[blk];          /* re-check after acquiring the lock */
        if (!b) {
            b = (SockLockPair *)platform_alloc((uint32_t)(CL_SOCK_LOCK_BLOCK_SIZE *
                                                          sizeof(SockLockPair)));
            if (b) {
                int i;
                for (i = 0; i < CL_SOCK_LOCK_BLOCK_SIZE; i++) {
                    b[i].read_mutex = NULL;
                    b[i].write_mutex = NULL;
                    platform_mutex_init(&b[i].read_mutex);
                    platform_mutex_init(&b[i].write_mutex);
                }
                cl_sock_lock_dir[blk] = b;  /* publish only after full init */
            }
        }
        platform_mutex_unlock(cl_stream_table_mutex);
        if (!b) return NULL;
    }
    return &b[h & CL_SOCK_LOCK_BLOCK_MASK];
}

/* --- C-buffer input stream side table --- */

#define CL_CBUF_TABLE_SIZE 8

static struct {
    const char *data;
    uint32_t len;
} cbuf_table[CL_CBUF_TABLE_SIZE];

void cl_stream_init(void)
{
    int i;
    CL_Symbol *s;

    if (!cl_stream_table_mutex)
        platform_mutex_init(&cl_stream_table_mutex);
    if (!cl_console_read_mutex)
        platform_mutex_init(&cl_console_read_mutex);
    if (!cl_console_write_mutex)
        platform_mutex_init(&cl_console_write_mutex);
    if (!cl_file_io_mutex)
        platform_mutex_init(&cl_file_io_mutex);
    /* Per-socket lock blocks are allocated lazily on first use (sock_lock_pair). */

    for (i = 0; i < CL_STREAM_BUF_TABLE_SIZE; i++) {
        outbuf_table[i].data = NULL;
        outbuf_table[i].capacity = 0;
        outbuf_table[i].length = 0;
        outbuf_inuse[i] = 0;
        outbuf_pinned[i] = 0;
    }
    stream_initialized = 1;

    /* Create singleton console streams */
    cl_stdin_stream = cl_make_stream(CL_STREAM_INPUT, CL_STREAM_CONSOLE);
    CL_GC_PROTECT(cl_stdin_stream);

    cl_stdout_stream = cl_make_stream(CL_STREAM_OUTPUT, CL_STREAM_CONSOLE);
    CL_GC_PROTECT(cl_stdout_stream);

    cl_stderr_stream = cl_make_stream(CL_STREAM_OUTPUT, CL_STREAM_CONSOLE);
    CL_GC_UNPROTECT(2);

    /* Bind standard stream variables to console streams */
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STANDARD_INPUT);
    s->value = cl_stdin_stream;

    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STANDARD_OUTPUT);
    s->value = cl_stdout_stream;

    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_ERROR_OUTPUT);
    s->value = cl_stderr_stream;

    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_TRACE_OUTPUT);
    s->value = cl_stdout_stream;

    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_DEBUG_IO);
    s->value = cl_stdout_stream;

    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_QUERY_IO);
    s->value = cl_stdout_stream;

    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_TERMINAL_IO);
    s->value = cl_stdout_stream;
}

void cl_stream_shutdown(void)
{
    int i;
    for (i = 0; i < CL_STREAM_BUF_TABLE_SIZE; i++) {
        if (outbuf_table[i].data) {
            platform_free(outbuf_table[i].data);
            outbuf_table[i].data = NULL;
        }
    }
    stream_initialized = 0;
}

CL_Obj cl_make_stream(uint32_t direction, uint32_t stream_type)
{
    CL_Stream *st = (CL_Stream *)cl_alloc(TYPE_STREAM, sizeof(CL_Stream));
    if (!st) return CL_NIL;
    st->direction = direction;
    st->stream_type = stream_type;
    st->flags = CL_STREAM_FLAG_OPEN;
    st->handle_id = 0;
    st->string_buf = CL_NIL;
    st->position = 0;
    st->out_buf_handle = 0;
    st->out_buf_size = 0;
    st->out_buf_len = 0;
    st->unread_char = -1;
    st->element_type = CL_NIL;  /* Will be set to CHARACTER symbol later */
    st->charpos = 0;
    st->line = 1;
    st->read_timeout_ms = 0;
    st->write_timeout_ms = 0;
    return CL_PTR_TO_OBJ(st);
}

uint32_t cl_stream_alloc_outbuf(uint32_t initial_size)
{
    int i, retry;
    char *data;

    if (!stream_initialized) cl_stream_init();
    if (initial_size == 0) initial_size = 256;

    if (CL_MT()) platform_mutex_lock(cl_stream_table_mutex);
    for (retry = 0; retry < 2; retry++) {
    /* Slot 0 is reserved as invalid */
    for (i = 1; i < CL_STREAM_BUF_TABLE_SIZE; i++) {
        if (outbuf_table[i].data == NULL) {
            data = (char *)platform_alloc(initial_size);
            if (!data) {
                if (CL_MT()) platform_mutex_unlock(cl_stream_table_mutex);
                return 0;
            }
            outbuf_table[i].data = data;
            outbuf_table[i].capacity = initial_size;
            outbuf_table[i].length = 0;
            outbuf_pinned[i] = 1;  /* protect until the owning stream is set */
#ifdef DEBUG_STREAM
            {
                char dbg[64];
                int used = 0, j;
                for (j = 1; j < CL_STREAM_BUF_TABLE_SIZE; j++)
                    if (outbuf_table[j].data) used++;
                snprintf(dbg, sizeof(dbg), "[STREAM] alloc slot %d (%d/%d used)\n",
                         i, used, CL_STREAM_BUF_TABLE_SIZE - 1);
                platform_write_string(dbg);
            }
#endif
            if (CL_MT()) platform_mutex_unlock(cl_stream_table_mutex);
            return (uint32_t)i;
        }
    }
    /* All slots occupied — run GC to finalize dead streams, then retry */
    if (retry == 0) {
        extern void cl_gc(void);
        if (CL_MT()) platform_mutex_unlock(cl_stream_table_mutex);
        cl_gc();
        if (CL_MT()) platform_mutex_lock(cl_stream_table_mutex);
    }
    } /* end retry loop */
    if (CL_MT()) platform_mutex_unlock(cl_stream_table_mutex);
#ifdef DEBUG_STREAM
    platform_write_string("[STREAM] outbuf table FULL after GC\n");
#endif
    return 0;  /* No free slots even after GC */
}

void cl_stream_free_outbuf(uint32_t handle)
{
    if (handle > 0 && handle < CL_STREAM_BUF_TABLE_SIZE &&
        outbuf_table[handle].data) {
        if (CL_MT()) platform_mutex_lock(cl_stream_table_mutex);
        platform_free(outbuf_table[handle].data);
        outbuf_table[handle].data = NULL;
        outbuf_table[handle].capacity = 0;
        outbuf_table[handle].length = 0;
        outbuf_pinned[handle] = 0;
#ifdef DEBUG_STREAM
        {
            char dbg[64];
            int used = 0, j;
            for (j = 1; j < CL_STREAM_BUF_TABLE_SIZE; j++)
                if (outbuf_table[j].data) used++;
            snprintf(dbg, sizeof(dbg), "[STREAM] free  slot %d (%d/%d used)\n",
                     (int)handle, used, CL_STREAM_BUF_TABLE_SIZE - 1);
            platform_write_string(dbg);
        }
#endif
        if (CL_MT()) platform_mutex_unlock(cl_stream_table_mutex);
    }
}

char *cl_stream_outbuf_data(uint32_t handle)
{
    if (handle > 0 && handle < CL_STREAM_BUF_TABLE_SIZE)
        return outbuf_table[handle].data;
    return NULL;
}

/* --- Mark-driven outbuf reclamation (see outbuf_inuse comment above) --- */

/* Called once at the start of every GC mark phase: clear the in-use bitmap. */
void cl_stream_outbuf_gc_mark_begin(void)
{
    uint32_t i;
    for (i = 0; i < CL_STREAM_BUF_TABLE_SIZE; i++)
        outbuf_inuse[i] = 0;
}

/* Called while marking each LIVE output stream: pin its outbuf slot. */
void cl_stream_outbuf_gc_mark_use(uint32_t handle)
{
    if (handle > 0 && handle < CL_STREAM_BUF_TABLE_SIZE)
        outbuf_inuse[handle] = 1;
}

/* Release the creation-window pin once a live stream owns the slot; from here
 * on the slot is protected by the mark phase (cl_stream_outbuf_gc_mark_use). */
void cl_stream_outbuf_unpin(uint32_t handle)
{
    if (handle > 0 && handle < CL_STREAM_BUF_TABLE_SIZE)
        outbuf_pinned[handle] = 0;
}

/* Called after marking completes (world stopped, so no table mutex needed —
 * mirrors gc_finalize_dead): free every allocated slot that no live stream
 * pinned.  These belong to streams that became unreachable this cycle. */
void cl_stream_outbuf_gc_reclaim(void)
{
    uint32_t i;
    for (i = 1; i < CL_STREAM_BUF_TABLE_SIZE; i++) {
        if (outbuf_table[i].data && !outbuf_inuse[i] && !outbuf_pinned[i]) {
            platform_free(outbuf_table[i].data);
            outbuf_table[i].data = NULL;
            outbuf_table[i].capacity = 0;
            outbuf_table[i].length = 0;
        }
    }
}

uint32_t cl_stream_outbuf_len(uint32_t handle)
{
    if (handle > 0 && handle < CL_STREAM_BUF_TABLE_SIZE)
        return outbuf_table[handle].length;
    return 0;
}

static void outbuf_grow(uint32_t handle, uint32_t needed)
{
    CL_OutBuf *buf = &outbuf_table[handle];
    uint32_t new_cap = buf->capacity;
    char *new_data;

    while (new_cap < needed)
        new_cap *= 2;

    new_data = (char *)platform_alloc(new_cap);
    if (!new_data) return;  /* Allocation failure — silently drop */
    memcpy(new_data, buf->data, buf->length);
    platform_free(buf->data);
    buf->data = new_data;
    buf->capacity = new_cap;
}

void cl_stream_outbuf_putchar(CL_Stream *st, int ch)
{
    uint32_t h = st->out_buf_handle;
    CL_OutBuf *buf;
    if (h == 0 || h >= CL_STREAM_BUF_TABLE_SIZE) return;
    buf = &outbuf_table[h];
    if (!buf->data) return;

    if (buf->length + 1 >= buf->capacity)
        outbuf_grow(h, buf->length + 2);

    buf->data[buf->length++] = (char)ch;
    buf->data[buf->length] = '\0';
    st->out_buf_len = buf->length;
    st->out_buf_size = buf->capacity;
}

void cl_stream_outbuf_write(CL_Stream *st, const char *str)
{
    uint32_t h = st->out_buf_handle;
    uint32_t len;
    CL_OutBuf *buf;
    if (h == 0 || h >= CL_STREAM_BUF_TABLE_SIZE) return;
    buf = &outbuf_table[h];
    if (!buf->data || !str) return;

    len = (uint32_t)strlen(str);
    if (buf->length + len >= buf->capacity)
        outbuf_grow(h, buf->length + len + 1);

    memcpy(buf->data + buf->length, str, len);
    buf->length += len;
    buf->data[buf->length] = '\0';
    st->out_buf_len = buf->length;
    st->out_buf_size = buf->capacity;
}

void cl_stream_outbuf_reset(uint32_t handle)
{
    if (handle > 0 && handle < CL_STREAM_BUF_TABLE_SIZE &&
        outbuf_table[handle].data) {
        outbuf_table[handle].length = 0;
        outbuf_table[handle].data[0] = '\0';
    }
}

/* --- Synonym / two-way stream resolution --- */

static CL_Obj resolve_synonym(CL_Obj stream)
{
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    while (st->stream_type == CL_STREAM_SYNONYM) {
        CL_Obj sym = st->string_buf;  /* symbol stored here */
        stream = cl_symbol_value(sym);
        if (CL_NULL_P(stream) || !CL_HEAP_P(stream))
            return CL_NIL;
        st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    }
    return stream;
}

/* Resolve synonym and two-way stream wrappers to a concrete stream.
 * `writing` selects the output child of a two-way stream (else input child). */
static CL_Obj resolve_stream(CL_Obj stream, int writing)
{
    CL_Stream *st;
    if (CL_NULL_P(stream) || !CL_STREAM_P(stream)) return CL_NIL;
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    for (;;) {
        if (st->stream_type == CL_STREAM_SYNONYM) {
            stream = cl_symbol_value(st->string_buf);
            if (CL_NULL_P(stream) || !CL_STREAM_P(stream))
                return CL_NIL;
            st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
        } else if (st->stream_type == CL_STREAM_TWO_WAY) {
            stream = writing ? st->element_type : st->string_buf;
            if (CL_NULL_P(stream) || !CL_STREAM_P(stream))
                return CL_NIL;
            st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
        } else {
            return stream;
        }
    }
}

/* --- Stream I/O operations --- */

/* Return the I/O serialisation lock for `st` in the given direction, or NULL
 * when no lock is needed (single-threaded, an in-memory stream type, or an
 * out-of-range socket handle).  `writing` selects the write lock; otherwise the
 * read lock.  See the lock declarations above for why locks are split this way. */
static void *stream_lock_for(CL_Stream *st, int writing)
{
    if (!CL_MT()) return NULL;
    switch (st->stream_type) {
    case CL_STREAM_CONSOLE:
        return writing ? cl_console_write_mutex : cl_console_read_mutex;
    case CL_STREAM_FILE:
        return cl_file_io_mutex;
    case CL_STREAM_SOCKET: {
        SockLockPair *p = sock_lock_pair(st->handle_id);
        if (!p) return NULL;
        return writing ? p->write_mutex : p->read_mutex;
    }
    default:
        return NULL;
    }
}

/* Read a single raw byte from a byte-oriented stream.
 * Caller must hold the I/O mutex if needed.
 * Does NOT handle UTF-8 decoding — use cl_stream_read_char for that. */
static int stream_read_raw_byte(CL_Stream *st)
{
    switch (st->stream_type) {
    case CL_STREAM_CONSOLE:
        return platform_getchar();
    case CL_STREAM_FILE:
        return platform_file_getchar((PlatformFile)st->handle_id);
    case CL_STREAM_CBUF: {
        uint32_t idx = st->handle_id;
        if (idx == 0 || idx >= CL_CBUF_TABLE_SIZE || !cbuf_table[idx].data)
            return -1;
        if (st->position >= st->out_buf_len)
            return -1;
        return (unsigned char)cbuf_table[idx].data[st->position++];
    }
    case CL_STREAM_SOCKET:
        return platform_socket_read((PlatformSocket)st->handle_id);
    default:
        return -1;
    }
}

/* Raise EXT:SOCKET-TIMEOUT when a socket read/write missed its deadline.
 * cl_error longjmps, so callers MUST release any held iolock first.
 * timeout_ms must be captured from st->read/write_timeout_ms BEFORE any
 * platform call that enters a GC safe region, since compaction can move st. */
static CL_NORETURN void stream_raise_timeout(int is_write, uint32_t timeout_ms)
{
    cl_error(CL_ERR_TIMEOUT, "%s on socket stream timed out after %u ms",
             is_write ? "WRITE" : "READ",
             (unsigned)timeout_ms);
}

#ifdef CL_WIDE_STRINGS
/* Decode a UTF-8 character from a byte-oriented stream.
 * first_byte is already read; reads continuation bytes as needed.
 * Takes the stream as a CL_Obj (not a CL_Stream *) because each
 * continuation-byte read may block inside a GC safe region, where a
 * concurrent STW-GC can compact the arena and relocate the stream object —
 * so the pointer is re-derived from the (GC-protected) offset on every read. */
static int stream_decode_utf8(CL_Obj stream, int first_byte)
{
    unsigned char b0 = (unsigned char)first_byte;
    int cp, expected, i;

    if (b0 <= 0x7F)
        return b0;

    if ((b0 & 0xE0) == 0xC0) {
        cp = b0 & 0x1F;
        expected = 2;
    } else if ((b0 & 0xF0) == 0xE0) {
        cp = b0 & 0x0F;
        expected = 3;
    } else if ((b0 & 0xF8) == 0xF0) {
        cp = b0 & 0x07;
        expected = 4;
    } else {
        return 0xFFFD;  /* Invalid lead byte → replacement char */
    }

    for (i = 1; i < expected; i++) {
        int b;
        CL_GC_PROTECT(stream);   /* read may compact; forward the offset */
        b = stream_read_raw_byte((CL_Stream *)CL_OBJ_TO_PTR(stream));
        CL_GC_UNPROTECT(1);
        if (b == -1 || (b & 0xC0) != 0x80) {
            return 0xFFFD;  /* Truncated or invalid continuation */
        }
        cp = (cp << 6) | (b & 0x3F);
    }

    /* Reject overlong encodings */
    if ((expected == 2 && cp < 0x80) ||
        (expected == 3 && cp < 0x800) ||
        (expected == 4 && cp < 0x10000))
        return 0xFFFD;

    /* Reject surrogates and out-of-range */
    if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
        return 0xFFFD;

    return cp;
}
#endif /* CL_WIDE_STRINGS */

int cl_stream_read_char(CL_Obj stream)
{
    CL_Stream *st;
    int ch;
    void *iolock;

    stream = resolve_stream(stream, 0);
    if (CL_NULL_P(stream)) return -1;
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);

    if (!(st->flags & CL_STREAM_FLAG_OPEN))
        return -1;
    if (!(st->direction & CL_STREAM_INPUT))
        return -1;

    iolock = stream_lock_for(st, 0);
    if (iolock) platform_mutex_lock(iolock);

    /* Check for pushed-back character (already decoded code point) */
    if (st->unread_char != -1) {
        ch = st->unread_char;
        st->unread_char = -1;
        if (iolock) platform_mutex_unlock(iolock);
        return ch;
    }

    /* String streams return code points directly (no UTF-8 layer) */
    if (st->stream_type == CL_STREAM_STRING) {
        if (CL_NULL_P(st->string_buf)) {
            if (iolock) platform_mutex_unlock(iolock);
            return -1;
        }
        if (st->position >= st->out_buf_len) {
            if (iolock) platform_mutex_unlock(iolock);
            return -1;
        }
        ch = cl_string_char_at(st->string_buf, st->position++);
        if (iolock) platform_mutex_unlock(iolock);
        return ch;
    }

    /* Byte-oriented streams: read raw byte, then UTF-8 decode.
     * platform_socket_read enters cl_gc_enter/leave_safe_region, so a
     * concurrent STW-GC may compact the arena DURING the read and relocate
     * this stream object.  `stream` is an unrooted C local, so its offset goes
     * stale too — GC-protect it across the read and re-derive `st` from the
     * (forwarded) offset before touching any st-> field.  Without this the
     * `st->flags |= EOF` write below (hit on every Connection:close EOF) would
     * scribble into whatever object slid into the old location. */
    {
        uint32_t rto = st->read_timeout_ms;  /* capture while st is valid */
        CL_GC_PROTECT(stream);
        ch = stream_read_raw_byte(st);
        st = (CL_Stream *)CL_OBJ_TO_PTR(stream);   /* st may have moved */

#ifdef CL_WIDE_STRINGS
        /* Decode UTF-8 multi-byte sequences for byte-oriented streams.  A LATIN-1
         * stream is 8-bit transparent: the raw byte 0..255 IS the code point, so
         * skip UTF-8 decoding entirely. */
        if (ch > 0x7F && ch != -1 && !(st->flags & CL_STREAM_FLAG_LATIN1)) {
            ch = stream_decode_utf8(stream, ch);
            st = (CL_Stream *)CL_OBJ_TO_PTR(stream);   /* decode read more bytes */
        }
#endif
        CL_GC_UNPROTECT(1);

        if (ch == PLATFORM_SOCKET_TIMEOUT) {   /* deadline elapsed, not EOF */
            if (iolock) platform_mutex_unlock(iolock);
            stream_raise_timeout(0, rto);
        }
    }
    if (ch == -1)
        st->flags |= CL_STREAM_FLAG_EOF;
    if (iolock) platform_mutex_unlock(iolock);
    return ch;
}

/* Read one raw byte from stream — no UTF-8 decoding.
 * For binary (unsigned-byte 8) streams. */
int cl_stream_read_byte(CL_Obj stream)
{
    CL_Stream *st;
    int ch;
    void *iolock;

    stream = resolve_stream(stream, 0);
    if (CL_NULL_P(stream)) return -1;
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);

    if (!(st->flags & CL_STREAM_FLAG_OPEN))
        return -1;
    if (!(st->direction & CL_STREAM_INPUT))
        return -1;

    iolock = stream_lock_for(st, 0);
    if (iolock) platform_mutex_lock(iolock);

    /* Check for pushed-back byte */
    if (st->unread_char != -1) {
        ch = st->unread_char;
        st->unread_char = -1;
        if (iolock) platform_mutex_unlock(iolock);
        return ch;
    }

    /* String streams: return code point (for string-as-bytes) */
    if (st->stream_type == CL_STREAM_STRING) {
        if (CL_NULL_P(st->string_buf)) {
            if (iolock) platform_mutex_unlock(iolock);
            return -1;
        }
        if (st->position >= st->out_buf_len) {
            if (iolock) platform_mutex_unlock(iolock);
            return -1;
        }
        ch = cl_string_char_at(st->string_buf, st->position++);
        if (iolock) platform_mutex_unlock(iolock);
        return ch;
    }

    /* Raw byte read — no UTF-8 decoding.  Same GC hazard as cl_stream_read_char:
     * the read may compact and relocate st, so protect the offset across it and
     * re-derive st before the EOF-flag write. */
    {
        uint32_t rto = st->read_timeout_ms;  /* capture while st is valid */
        CL_GC_PROTECT(stream);
        ch = stream_read_raw_byte(st);
        st = (CL_Stream *)CL_OBJ_TO_PTR(stream);   /* st may have moved */
        CL_GC_UNPROTECT(1);
        if (ch == PLATFORM_SOCKET_TIMEOUT) {   /* deadline elapsed, not EOF */
            if (iolock) platform_mutex_unlock(iolock);
            stream_raise_timeout(0, rto);
        }
    }
    if (ch == -1)
        st->flags |= CL_STREAM_FLAG_EOF;
    if (iolock) platform_mutex_unlock(iolock);
    return ch;
}

void cl_stream_write_char(CL_Obj stream, int ch)
{
    CL_Stream *st;
    void *iolock;
    int wr = 0;   /* socket write result, for timeout detection */
#ifdef CL_WIDE_STRINGS
    /* A LATIN-1 stream is 8-bit transparent: write the low byte raw, no UTF-8.
     * `encode` is non-zero only when this char must be UTF-8-encoded. */
    int encode;
#endif

    stream = resolve_stream(stream, 1);
    if (CL_NULL_P(stream)) return;
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);

    if (!(st->flags & CL_STREAM_FLAG_OPEN))
        return;
    if (!(st->direction & CL_STREAM_OUTPUT))
        return;

#ifdef CL_WIDE_STRINGS
    encode = (ch > 0x7F) && !(st->flags & CL_STREAM_FLAG_LATIN1);
#endif

    iolock = stream_lock_for(st, 1);
    if (iolock) platform_mutex_lock(iolock);

    /* Capture timeout before the socket write: platform_socket_write* may call
     * socket_flush_wbuf which enters a GC safe region; a concurrent STW-GC can
     * compact the arena, making st stale.  Read the field now while st is valid,
     * GC-protect the offset across the write, and re-derive st before the
     * st->charpos update below. */
    {
        uint32_t wto = st->write_timeout_ms;
        CL_GC_PROTECT(stream);

        switch (st->stream_type) {
        case CL_STREAM_CONSOLE: {
#ifdef CL_WIDE_STRINGS
            if (encode) {
                char utf8[5];
                int nb = cl_utf8_encode(ch, utf8);
                if (nb > 0) { utf8[nb] = '\0'; platform_write_string(utf8); }
            } else
#endif
            {
                char buf[2];
                buf[0] = (char)ch;
                buf[1] = '\0';
                platform_write_string(buf);
            }
            break;
        }
        case CL_STREAM_FILE:
#ifdef CL_WIDE_STRINGS
            if (encode) {
                char utf8[4];
                int nb = cl_utf8_encode(ch, utf8);
                if (nb > 0)
                    platform_file_write_buf((PlatformFile)st->handle_id, utf8, (uint32_t)nb);
            } else
#endif
            platform_file_write_char((PlatformFile)st->handle_id, ch);
            break;
        case CL_STREAM_STRING:
            /* String output buffers store raw bytes; encode UTF-8 for non-ASCII */
#ifdef CL_WIDE_STRINGS
            if (encode) {
                char utf8[4];
                int nb = cl_utf8_encode(ch, utf8);
                int j;
                for (j = 0; j < nb; j++)
                    cl_stream_outbuf_putchar(st, (unsigned char)utf8[j]);
            } else
#endif
            cl_stream_outbuf_putchar(st, ch);
            break;
        case CL_STREAM_SOCKET:
#ifdef CL_WIDE_STRINGS
            if (encode) {
                char utf8[4];
                int nb = cl_utf8_encode(ch, utf8);
                if (nb > 0)
                    wr = platform_socket_write_buf((PlatformSocket)st->handle_id, utf8, (uint32_t)nb);
            } else
#endif
            wr = platform_socket_write((PlatformSocket)st->handle_id, ch);
            break;
        }

        st = (CL_Stream *)CL_OBJ_TO_PTR(stream);   /* a socket flush may have moved st */
        CL_GC_UNPROTECT(1);

        if (ch == '\n')
            st->charpos = 0;
        else
            st->charpos++;

        if (iolock) platform_mutex_unlock(iolock);
        if (wr == PLATFORM_SOCKET_TIMEOUT)
            stream_raise_timeout(1, wto);
    }
}

/* Write one raw byte to stream — no UTF-8 encoding.
 * For binary (unsigned-byte 8) streams. */
void cl_stream_write_byte(CL_Obj stream, int byte)
{
    CL_Stream *st;
    void *iolock;
    int wr = 0;   /* socket write result, for timeout detection */

    stream = resolve_stream(stream, 1);
    if (CL_NULL_P(stream)) return;
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);

    if (!(st->flags & CL_STREAM_FLAG_OPEN))
        return;
    if (!(st->direction & CL_STREAM_OUTPUT))
        return;

    iolock = stream_lock_for(st, 1);
    if (iolock) platform_mutex_lock(iolock);

    {
        uint32_t wto = st->write_timeout_ms;  /* capture before safe region */

        switch (st->stream_type) {
        case CL_STREAM_CONSOLE: {
            char buf[2];
            buf[0] = (char)(unsigned char)byte;
            buf[1] = '\0';
            platform_write_string(buf);
            break;
        }
        case CL_STREAM_FILE:
            platform_file_write_char((PlatformFile)st->handle_id, byte);
            break;
        case CL_STREAM_STRING:
            cl_stream_outbuf_putchar(st, byte);
            break;
        case CL_STREAM_SOCKET:
            wr = platform_socket_write((PlatformSocket)st->handle_id, byte);
            break;
        }

        if (iolock) platform_mutex_unlock(iolock);
        if (wr == PLATFORM_SOCKET_TIMEOUT)
            stream_raise_timeout(1, wto);
    }
}

void cl_stream_write_string(CL_Obj stream, const char *str, uint32_t len)
{
    CL_Stream *st;
    uint32_t i;
    void *iolock;
    int wr = 0;   /* socket write result, for timeout detection */

    stream = resolve_stream(stream, 1);
    if (CL_NULL_P(stream)) return;
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);

    if (!(st->flags & CL_STREAM_FLAG_OPEN))
        return;
    if (!(st->direction & CL_STREAM_OUTPUT))
        return;

    iolock = stream_lock_for(st, 1);
    if (iolock) platform_mutex_lock(iolock);

    {
        uint32_t wto = st->write_timeout_ms;  /* capture before safe region */
        CL_GC_PROTECT(stream);   /* socket flush may compact; re-derive st after */

        switch (st->stream_type) {
        case CL_STREAM_CONSOLE: {
            /* Chunk to stack buffer for platform_write_string (needs NUL) */
            char tmp[257];
            uint32_t pos = 0;
            while (pos < len) {
                uint32_t chunk = len - pos;
                if (chunk > 256) chunk = 256;
                memcpy(tmp, str + pos, chunk);
                tmp[chunk] = '\0';
                platform_write_string(tmp);
                pos += chunk;
            }
            break;
        }
        case CL_STREAM_FILE:
            platform_file_write_buf((PlatformFile)st->handle_id, str, len);
            break;
        case CL_STREAM_STRING:
            for (i = 0; i < len; i++)
                cl_stream_outbuf_putchar(st, (unsigned char)str[i]);
            break;
        case CL_STREAM_SOCKET:
            wr = platform_socket_write_buf((PlatformSocket)st->handle_id, str, len);
            break;
        }

        st = (CL_Stream *)CL_OBJ_TO_PTR(stream);   /* a socket flush may have moved st */
        CL_GC_UNPROTECT(1);

        /* Update charpos: find last newline */
        {
            uint32_t last_nl = 0xFFFFFFFF;
            for (i = 0; i < len; i++) {
                if (str[i] == '\n')
                    last_nl = i;
            }
            if (last_nl != 0xFFFFFFFF)
                st->charpos = len - last_nl - 1;
            else
                st->charpos += len;
        }

        if (iolock) platform_mutex_unlock(iolock);
        if (wr == PLATFORM_SOCKET_TIMEOUT)
            stream_raise_timeout(1, wto);
    }
}

int cl_stream_peek_char(CL_Obj stream)
{
    stream = resolve_stream(stream, 0);
    if (CL_NULL_P(stream)) return -1;
    {
        int ch = cl_stream_read_char(stream);
        if (ch != -1) {
            CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
            st->unread_char = ch;
        }
        return ch;
    }
}

void cl_stream_unread_char(CL_Obj stream, int ch)
{
    CL_Stream *st;
    stream = resolve_stream(stream, 0);
    if (CL_NULL_P(stream)) return;
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    st->unread_char = ch;
}

void cl_stream_close(CL_Obj stream)
{
    CL_Stream *st;
    void *iolock;
    stream = resolve_synonym(stream);
    if (CL_NULL_P(stream)) return;
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    if (!(st->flags & CL_STREAM_FLAG_OPEN))
        return;

    /* Closing flushes and tears down the write side; take the write lock.  A
     * concurrent reader parked on this socket holds only the read lock, so
     * close never waits on it — the read() returns once the fd is closed. */
    iolock = (st->stream_type == CL_STREAM_FILE ||
              st->stream_type == CL_STREAM_SOCKET)
                 ? stream_lock_for(st, 1) : NULL;
    if (iolock) platform_mutex_lock(iolock);

    st->flags &= ~CL_STREAM_FLAG_OPEN;

    /* Capture the handle/type before any teardown call: platform_socket_flush
     * and platform_socket_close (and the file equivalents) enter GC safe
     * regions, so a concurrent STW-GC can compact and relocate st between the
     * flush and the close.  Reading st->handle_id for the close THROUGH a stale
     * st could yield a garbage slot number and close an UNRELATED live socket,
     * corrupting another in-flight connection (intermittent "No status line"). */
    {
        uint32_t stype  = st->stream_type;
        uint32_t handle = st->handle_id;
        uint32_t obh    = st->out_buf_handle;
        uint32_t dir    = st->direction;
        switch (stype) {
        case CL_STREAM_FILE:
            if (handle != 0) {
                platform_file_flush((PlatformFile)handle);
                platform_file_close((PlatformFile)handle);
            }
            break;
        case CL_STREAM_STRING:
            if ((dir & CL_STREAM_OUTPUT) && obh != 0)
                cl_stream_free_outbuf(obh);
            break;
        case CL_STREAM_CBUF:
            if (handle > 0 && handle < CL_CBUF_TABLE_SIZE) {
                cbuf_table[handle].data = NULL;
                cbuf_table[handle].len = 0;
            }
            break;
        case CL_STREAM_SOCKET:
            if (handle != 0) {
                platform_socket_flush((PlatformSocket)handle);
                platform_socket_close((PlatformSocket)handle);
            }
            break;
        default:
            break;
        }
    }

    if (iolock) platform_mutex_unlock(iolock);
}

CL_Obj cl_make_string_input_stream(CL_Obj string, uint32_t start, uint32_t end)
{
    CL_Obj s;
    CL_Stream *st;

    /* GC-protect `string` across cl_make_stream: it allocates the stream
     * object and may compact the heap, relocating the source string. Without
     * protection, `string` holds a stale offset and st->string_buf would point
     * at moved/garbage memory, corrupting every subsequent read from the
     * stream (e.g. read-from-string dropping or mangling list elements). */
    CL_GC_PROTECT(string);
    s = cl_make_stream(CL_STREAM_INPUT, CL_STREAM_STRING);
    if (CL_NULL_P(s)) { CL_GC_UNPROTECT(1); return CL_NIL; }

    st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    st->string_buf = string;
    st->position = start;
    st->out_buf_len = end;  /* Reuse as end limit */
    CL_GC_UNPROTECT(1);
    return s;
}

CL_Obj cl_make_cbuf_input_stream(const char *data, uint32_t len)
{
    CL_Obj s = cl_make_stream(CL_STREAM_INPUT, CL_STREAM_CBUF);
    CL_Stream *st;
    int i;
    if (CL_NULL_P(s)) return CL_NIL;

    /* Find a free slot in the cbuf table */
    if (CL_MT()) platform_mutex_lock(cl_stream_table_mutex);
    for (i = 1; i < CL_CBUF_TABLE_SIZE; i++) {
        if (cbuf_table[i].data == NULL) {
            cbuf_table[i].data = data;
            cbuf_table[i].len = len;
            if (CL_MT()) platform_mutex_unlock(cl_stream_table_mutex);
            st = (CL_Stream *)CL_OBJ_TO_PTR(s);
            st->handle_id = (uint32_t)i;
            st->position = 0;
            st->out_buf_len = len;  /* End limit */
            return s;
        }
    }
    if (CL_MT()) platform_mutex_unlock(cl_stream_table_mutex);
    return CL_NIL;  /* No free slots */
}

CL_Obj cl_make_string_output_stream(void)
{
    CL_Obj s = cl_make_stream(CL_STREAM_OUTPUT, CL_STREAM_STRING);
    CL_Stream *st;
    uint32_t h;
    if (CL_NULL_P(s)) return CL_NIL;

    /* cl_stream_alloc_outbuf runs a GC (sweep + possible compaction) when the
     * outbuf table is full.  `s` is a freshly-built stream not yet reachable
     * from any root, so that GC would sweep it (or compaction would relocate
     * it, leaving this C local with a stale offset) — and `st` derived before
     * the call would dangle.  Protect `s` across the alloc and derive `st`
     * afterward.  This is the deterministic corruption behind the periodic
     * "argument is not a stream" failures when many string-output-streams are
     * created in a loop (e.g. chunga read-line* per HTTP request). */
    CL_GC_PROTECT(s);
    h = cl_stream_alloc_outbuf(256);
    if (h == 0) {
        CL_GC_UNPROTECT(1);
        cl_error(0,
                 "String output stream buffer table exhausted (%d slots)",
                 CL_STREAM_BUF_TABLE_SIZE - 1);
        return CL_NIL;
    }
    st = (CL_Stream *)CL_OBJ_TO_PTR(s);   /* derive after alloc — s may have moved */
    st->out_buf_handle = h;
    st->out_buf_size = 256;
    /* The (still GC-protected) stream now owns the slot, so the mark phase will
     * keep it alive — release the creation-window pin. */
    cl_stream_outbuf_unpin(h);
    CL_GC_UNPROTECT(1);
    return s;
}

CL_Obj cl_get_output_stream_string(CL_Obj stream)
{
    CL_Stream *st;
    char *data;
    uint32_t len;
    CL_Obj result;

    /* Building the result string ALLOCATES (cl_make_string / cl_utf8_to_cl_string),
     * which can trigger a compacting GC — either this thread's own, or a peer
     * thread's stop-the-world compaction while we are parked at cl_alloc's
     * safepoint.  That relocates the stream object, so the `st` raw pointer
     * (and the `stream` C-local offset) captured before the alloc go stale.
     * Using the stale `st` afterward for cl_stream_outbuf_reset(st->out_buf_handle)
     * and the st->out_buf_len/charpos writes reads a garbage handle and scribbles
     * into whatever slid into the stream's old slot — under multi-thread FORMAT
     * this reset a PEER's live outbuf, so `(format nil …)` returned corrupt/
     * cross-contaminated results.  Protect `stream` so the compactor forwards it,
     * and re-derive `st` after the allocation.  `data` points into the
     * platform-alloc'd (non-arena) outbuf, so it stays valid across the move. */
    CL_GC_PROTECT(stream);
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    data = cl_stream_outbuf_data(st->out_buf_handle);
    len = cl_stream_outbuf_len(st->out_buf_handle);
    if (!data || len == 0)
        result = cl_make_string("", 0);
    else {
#ifdef CL_WIDE_STRINGS
        /* Output buffer contains UTF-8 bytes; decode to CL string */
        result = cl_utf8_to_cl_string(data, len);
#else
        result = cl_make_string(data, len);
#endif
    }

    /* Re-derive st: the alloc above may have relocated the stream object. */
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    cl_stream_outbuf_reset(st->out_buf_handle);
    st->out_buf_len = 0;
    st->charpos = 0;
    CL_GC_UNPROTECT(1);
    return result;
}

CL_Obj cl_make_socket_stream(const char *host, int port, int connect_ms)
{
    PlatformSocket sh = platform_socket_connect(host, port, connect_ms);
    CL_Obj s;
    CL_Stream *st;
    if (sh == PLATFORM_SOCKET_INVALID)
        return CL_NIL;
    s = cl_make_stream(CL_STREAM_IO, CL_STREAM_SOCKET);
    if (CL_NULL_P(s)) {
        platform_socket_close(sh);
        return CL_NIL;
    }
    st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    st->handle_id = (uint32_t)sh;
    return s;
}

CL_Obj cl_make_listen_stream(int port, int loopback, int *actual_port)
{
    int bound_port = 0;
    PlatformSocket sh = platform_socket_listen(port, loopback, &bound_port);
    CL_Obj s;
    CL_Stream *st;
    if (sh == PLATFORM_SOCKET_INVALID)
        return CL_NIL;
    if (actual_port)
        *actual_port = bound_port;
    /* A listener is only ever accept()ed on, never read/written; flag it
     * INPUT so an accidental write-char is rejected as a direction error. */
    s = cl_make_stream(CL_STREAM_INPUT, CL_STREAM_SOCKET);
    if (CL_NULL_P(s)) {
        platform_socket_close(sh);
        return CL_NIL;
    }
    st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    st->handle_id = (uint32_t)sh;
    /* `position` is unused for socket streams (no read/write cursor); reuse it
     * to record the bound local port so cl_listen_stream_local_port can return
     * it — essential when port==0 lets the OS pick an ephemeral port. */
    st->position = (uint32_t)bound_port;
    return s;
}

int cl_listen_stream_local_port(CL_Obj stream)
{
    CL_Stream *st;
    if (!CL_STREAM_P(stream))
        return -1;
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    /* Listeners are created with direction == CL_STREAM_INPUT exactly;
     * connected sockets are CL_STREAM_IO, so this rejects them. */
    if (st->stream_type != CL_STREAM_SOCKET ||
        st->direction != CL_STREAM_INPUT ||
        !(st->flags & CL_STREAM_FLAG_OPEN))
        return -1;
    return (int)st->position;
}

CL_Obj cl_socket_stream_accept(CL_Obj listener)
{
    CL_Stream *lst;
    PlatformSocket conn;
    CL_Obj s;
    CL_Stream *st;

    if (!CL_STREAM_P(listener))
        return CL_NIL;
    lst = (CL_Stream *)CL_OBJ_TO_PTR(listener);
    if (lst->stream_type != CL_STREAM_SOCKET ||
        !(lst->flags & CL_STREAM_FLAG_OPEN))
        return CL_NIL;

    /* Block until a client connects.  conn is a plain integer handle, so the
     * subsequent allocation cannot lose it; `listener` is GC-rooted by the
     * caller's argument slot. */
    conn = platform_socket_accept((PlatformSocket)lst->handle_id);
    if (conn == PLATFORM_SOCKET_INVALID)
        return CL_NIL;

    s = cl_make_stream(CL_STREAM_IO, CL_STREAM_SOCKET);
    if (CL_NULL_P(s)) {
        platform_socket_close(conn);
        return CL_NIL;
    }
    st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    st->handle_id = (uint32_t)conn;
    return s;
}

uint32_t cl_socket_stream_get_timeout(CL_Obj stream, int which)
{
    CL_Stream *st;
    stream = resolve_stream(stream, which ? 1 : 0);
    if (CL_NULL_P(stream)) return 0;
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    if (st->stream_type != CL_STREAM_SOCKET) return 0;
    return which ? st->write_timeout_ms : st->read_timeout_ms;
}

void cl_socket_stream_set_timeout(CL_Obj stream, int which, uint32_t ms)
{
    CL_Stream *st;
    stream = resolve_stream(stream, which ? 1 : 0);
    if (CL_NULL_P(stream)) return;
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    if (st->stream_type != CL_STREAM_SOCKET) return;
    if (which) st->write_timeout_ms = ms;
    else       st->read_timeout_ms = ms;
    /* Push both current values down to the platform layer in one call. */
    platform_socket_set_timeout((PlatformSocket)st->handle_id,
                                (int)st->read_timeout_ms,
                                (int)st->write_timeout_ms);
}

CL_Obj cl_make_synonym_stream(CL_Obj symbol)
{
    CL_Obj s = cl_make_stream(CL_STREAM_IO, CL_STREAM_SYNONYM);
    CL_Stream *st;
    if (CL_NULL_P(s)) return CL_NIL;
    st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    st->string_buf = symbol;  /* Store the symbol */
    return s;
}

CL_Obj cl_make_two_way_stream(CL_Obj input_stream, CL_Obj output_stream)
{
    CL_Obj s = cl_make_stream(CL_STREAM_IO, CL_STREAM_TWO_WAY);
    CL_Stream *st;
    if (CL_NULL_P(s)) return CL_NIL;
    st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    st->string_buf = input_stream;   /* input child */
    st->element_type = output_stream; /* output child */
    return s;
}

/* --- Stream-aware write helpers --- */

/* Resolve CL:WRITE-STRING's current function, or CL_NIL if unavailable.
 * gray-streams.lisp shadows WRITE-STRING in the COMMON-LISP package so that,
 * given a Gray stream, it dispatches to GRAY:STREAM-WRITE-STRING.  Looked up
 * fresh each call: the symbol lives in the package (a GC root), but its
 * function object may move under a compacting GC, so we must not cache it. */
static CL_Obj lisp_write_string_fn(void)
{
    CL_Obj pkg = cl_find_package("COMMON-LISP", 11);
    CL_Obj sym;
    CL_Symbol *s;
    if (CL_NULL_P(pkg)) return CL_NIL;
    sym = cl_package_find_symbol("WRITE-STRING", 12, pkg);
    if (CL_NULL_P(sym)) return CL_NIL;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    if (s->function == CL_UNBOUND) return CL_NIL;
    return s->function;
}

void cl_write_cstring_to_stream_sym(CL_Obj sym, const char *s)
{
    CL_Obj val;
    if (CL_NULL_P(sym)) {
        platform_write_string(s);
        return;
    }
    val = cl_symbol_value(sym);
    if (CL_STREAM_P(val)) {
        cl_stream_write_string(val, s, (uint32_t)strlen(s));
        return;
    }
    /* Non-native stream value (e.g. a Gray-streams CLOS instance, such as the
     * output stream SLY binds to *STANDARD-OUTPUT*).  The native stream path
     * cannot reach it; route through the Lisp WRITE-STRING wrapper so the
     * stream's STREAM-WRITE-STRING method runs.
     *
     * Guard against CL_UNBOUND: a C-level diagnostic/boot write may run before
     * cl_stream_init has bound the standard stream variables (e.g. the boot
     * timing log fired from cl_repl_init, or any minimal embedding that skips
     * stream init).  An unbound special is neither a stream nor NULL, so
     * without this check it would be handed to WRITE-STRING as a bogus stream
     * and raise "argument is not a stream" — fatal at that early stage.  Fall
     * through to platform_write_string instead. */
    if (!CL_NULL_P(val) && val != CL_UNBOUND) {
        CL_Obj fn = lisp_write_string_fn();
        if (!CL_NULL_P(fn)) {
            CL_Obj args[2];
            CL_GC_PROTECT(val);
            CL_GC_PROTECT(fn);
            args[0] = cl_make_string(s, (uint32_t)strlen(s));
            args[1] = val;
            cl_vm_apply(fn, args, 2);
            CL_GC_UNPROTECT(2);
            return;
        }
    }
    platform_write_string(s);
}

void cl_write_cstring_to_stdout(const char *s)
{
    cl_write_cstring_to_stream_sym(SYM_STANDARD_OUTPUT, s);
}

void cl_write_cstring_to_error(const char *s)
{
    cl_write_cstring_to_stream_sym(SYM_ERROR_OUTPUT, s);
}

void cl_write_cstring_to_debug_io(const char *s)
{
    cl_write_cstring_to_stream_sym(SYM_DEBUG_IO, s);
}

void cl_write_cstring_to_trace(const char *s)
{
    cl_write_cstring_to_stream_sym(SYM_TRACE_OUTPUT, s);
}
