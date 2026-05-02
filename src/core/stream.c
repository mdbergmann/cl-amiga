/*
 * stream.c — CL Stream heap type and output buffer management
 */

#include "stream.h"
#include "symbol.h"
#include "mem.h"
#include "error.h"
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

/* Mutex protecting outbuf_table and cbuf_table slot allocation/deallocation */
static void *cl_stream_table_mutex = NULL;

/* Mutex protecting console/file/socket I/O (prevents interleaved output) */
static void *cl_stream_io_mutex = NULL;

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
    if (!cl_stream_io_mutex)
        platform_mutex_init(&cl_stream_io_mutex);

    for (i = 0; i < CL_STREAM_BUF_TABLE_SIZE; i++) {
        outbuf_table[i].data = NULL;
        outbuf_table[i].capacity = 0;
        outbuf_table[i].length = 0;
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

/* --- Synonym stream resolution --- */

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

/* --- Stream I/O operations --- */

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

#ifdef CL_WIDE_STRINGS
/* Decode a UTF-8 character from a byte-oriented stream.
 * first_byte is already read; reads continuation bytes as needed. */
static int stream_decode_utf8(CL_Stream *st, int first_byte)
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
        int b = stream_read_raw_byte(st);
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
    int ch, need_lock;

    stream = resolve_synonym(stream);
    if (CL_NULL_P(stream)) return -1;
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);

    if (!(st->flags & CL_STREAM_FLAG_OPEN))
        return -1;
    if (!(st->direction & CL_STREAM_INPUT))
        return -1;

    need_lock = CL_MT() && (st->stream_type == CL_STREAM_CONSOLE ||
                             st->stream_type == CL_STREAM_FILE ||
                             st->stream_type == CL_STREAM_SOCKET);
    if (need_lock) platform_mutex_lock(cl_stream_io_mutex);

    /* Check for pushed-back character (already decoded code point) */
    if (st->unread_char != -1) {
        ch = st->unread_char;
        st->unread_char = -1;
        if (need_lock) platform_mutex_unlock(cl_stream_io_mutex);
        return ch;
    }

    /* String streams return code points directly (no UTF-8 layer) */
    if (st->stream_type == CL_STREAM_STRING) {
        if (CL_NULL_P(st->string_buf)) {
            if (need_lock) platform_mutex_unlock(cl_stream_io_mutex);
            return -1;
        }
        if (st->position >= st->out_buf_len) {
            if (need_lock) platform_mutex_unlock(cl_stream_io_mutex);
            return -1;
        }
        ch = cl_string_char_at(st->string_buf, st->position++);
        if (need_lock) platform_mutex_unlock(cl_stream_io_mutex);
        return ch;
    }

    /* Byte-oriented streams: read raw byte, then UTF-8 decode */
    ch = stream_read_raw_byte(st);

#ifdef CL_WIDE_STRINGS
    /* Decode UTF-8 multi-byte sequences for byte-oriented streams */
    if (ch > 0x7F && ch != -1)
        ch = stream_decode_utf8(st, ch);
#endif

    if (ch == -1)
        st->flags |= CL_STREAM_FLAG_EOF;
    if (need_lock) platform_mutex_unlock(cl_stream_io_mutex);
    return ch;
}

/* Read one raw byte from stream — no UTF-8 decoding.
 * For binary (unsigned-byte 8) streams. */
int cl_stream_read_byte(CL_Obj stream)
{
    CL_Stream *st;
    int ch, need_lock;

    stream = resolve_synonym(stream);
    if (CL_NULL_P(stream)) return -1;
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);

    if (!(st->flags & CL_STREAM_FLAG_OPEN))
        return -1;
    if (!(st->direction & CL_STREAM_INPUT))
        return -1;

    need_lock = CL_MT() && (st->stream_type == CL_STREAM_CONSOLE ||
                             st->stream_type == CL_STREAM_FILE ||
                             st->stream_type == CL_STREAM_SOCKET);
    if (need_lock) platform_mutex_lock(cl_stream_io_mutex);

    /* Check for pushed-back byte */
    if (st->unread_char != -1) {
        ch = st->unread_char;
        st->unread_char = -1;
        if (need_lock) platform_mutex_unlock(cl_stream_io_mutex);
        return ch;
    }

    /* String streams: return code point (for string-as-bytes) */
    if (st->stream_type == CL_STREAM_STRING) {
        if (CL_NULL_P(st->string_buf)) {
            if (need_lock) platform_mutex_unlock(cl_stream_io_mutex);
            return -1;
        }
        if (st->position >= st->out_buf_len) {
            if (need_lock) platform_mutex_unlock(cl_stream_io_mutex);
            return -1;
        }
        ch = cl_string_char_at(st->string_buf, st->position++);
        if (need_lock) platform_mutex_unlock(cl_stream_io_mutex);
        return ch;
    }

    /* Raw byte read — no UTF-8 decoding */
    ch = stream_read_raw_byte(st);

    if (ch == -1)
        st->flags |= CL_STREAM_FLAG_EOF;
    if (need_lock) platform_mutex_unlock(cl_stream_io_mutex);
    return ch;
}

void cl_stream_write_char(CL_Obj stream, int ch)
{
    CL_Stream *st;
    int need_lock;

    stream = resolve_synonym(stream);
    if (CL_NULL_P(stream)) return;
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);

    if (!(st->flags & CL_STREAM_FLAG_OPEN))
        return;
    if (!(st->direction & CL_STREAM_OUTPUT))
        return;

    need_lock = CL_MT() && (st->stream_type == CL_STREAM_CONSOLE ||
                             st->stream_type == CL_STREAM_FILE ||
                             st->stream_type == CL_STREAM_SOCKET);
    if (need_lock) platform_mutex_lock(cl_stream_io_mutex);

    switch (st->stream_type) {
    case CL_STREAM_CONSOLE: {
#ifdef CL_WIDE_STRINGS
        if (ch > 0x7F) {
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
        if (ch > 0x7F) {
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
        if (ch > 0x7F) {
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
        if (ch > 0x7F) {
            char utf8[4];
            int nb = cl_utf8_encode(ch, utf8);
            if (nb > 0)
                platform_socket_write_buf((PlatformSocket)st->handle_id, utf8, (uint32_t)nb);
        } else
#endif
        platform_socket_write((PlatformSocket)st->handle_id, ch);
        break;
    }

    if (ch == '\n')
        st->charpos = 0;
    else
        st->charpos++;

    if (need_lock) platform_mutex_unlock(cl_stream_io_mutex);
}

/* Write one raw byte to stream — no UTF-8 encoding.
 * For binary (unsigned-byte 8) streams. */
void cl_stream_write_byte(CL_Obj stream, int byte)
{
    CL_Stream *st;
    int need_lock;

    stream = resolve_synonym(stream);
    if (CL_NULL_P(stream)) return;
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);

    if (!(st->flags & CL_STREAM_FLAG_OPEN))
        return;
    if (!(st->direction & CL_STREAM_OUTPUT))
        return;

    need_lock = CL_MT() && (st->stream_type == CL_STREAM_CONSOLE ||
                             st->stream_type == CL_STREAM_FILE ||
                             st->stream_type == CL_STREAM_SOCKET);
    if (need_lock) platform_mutex_lock(cl_stream_io_mutex);

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
        platform_socket_write((PlatformSocket)st->handle_id, byte);
        break;
    }

    if (need_lock) platform_mutex_unlock(cl_stream_io_mutex);
}

void cl_stream_write_string(CL_Obj stream, const char *str, uint32_t len)
{
    CL_Stream *st;
    uint32_t i;
    int need_lock;

    stream = resolve_synonym(stream);
    if (CL_NULL_P(stream)) return;
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);

    if (!(st->flags & CL_STREAM_FLAG_OPEN))
        return;
    if (!(st->direction & CL_STREAM_OUTPUT))
        return;

    need_lock = CL_MT() && (st->stream_type == CL_STREAM_CONSOLE ||
                             st->stream_type == CL_STREAM_FILE ||
                             st->stream_type == CL_STREAM_SOCKET);
    if (need_lock) platform_mutex_lock(cl_stream_io_mutex);

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
        platform_socket_write_buf((PlatformSocket)st->handle_id, str, len);
        break;
    }

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

    if (need_lock) platform_mutex_unlock(cl_stream_io_mutex);
}

int cl_stream_peek_char(CL_Obj stream)
{
    stream = resolve_synonym(stream);
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
    stream = resolve_synonym(stream);
    if (CL_NULL_P(stream)) return;
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    st->unread_char = ch;
}

void cl_stream_close(CL_Obj stream)
{
    CL_Stream *st;
    int need_lock;
    stream = resolve_synonym(stream);
    if (CL_NULL_P(stream)) return;
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    if (!(st->flags & CL_STREAM_FLAG_OPEN))
        return;

    need_lock = CL_MT() && (st->stream_type == CL_STREAM_FILE ||
                             st->stream_type == CL_STREAM_SOCKET);
    if (need_lock) platform_mutex_lock(cl_stream_io_mutex);

    st->flags &= ~CL_STREAM_FLAG_OPEN;

    switch (st->stream_type) {
    case CL_STREAM_FILE:
        if (st->handle_id != 0) {
            platform_file_flush((PlatformFile)st->handle_id);
            platform_file_close((PlatformFile)st->handle_id);
        }
        break;
    case CL_STREAM_STRING:
        if ((st->direction & CL_STREAM_OUTPUT) && st->out_buf_handle != 0)
            cl_stream_free_outbuf(st->out_buf_handle);
        break;
    case CL_STREAM_CBUF: {
        uint32_t idx = st->handle_id;
        if (idx > 0 && idx < CL_CBUF_TABLE_SIZE) {
            cbuf_table[idx].data = NULL;
            cbuf_table[idx].len = 0;
        }
        break;
    }
    case CL_STREAM_SOCKET:
        if (st->handle_id != 0) {
            platform_socket_flush((PlatformSocket)st->handle_id);
            platform_socket_close((PlatformSocket)st->handle_id);
        }
        break;
    default:
        break;
    }

    if (need_lock) platform_mutex_unlock(cl_stream_io_mutex);
}

CL_Obj cl_make_string_input_stream(CL_Obj string, uint32_t start, uint32_t end)
{
    CL_Obj s = cl_make_stream(CL_STREAM_INPUT, CL_STREAM_STRING);
    CL_Stream *st;
    if (CL_NULL_P(s)) return CL_NIL;

    st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    st->string_buf = string;
    st->position = start;
    st->out_buf_len = end;  /* Reuse as end limit */
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

    st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    h = cl_stream_alloc_outbuf(256);
    if (h == 0) {
        cl_error(0,
                 "String output stream buffer table exhausted (%d slots)",
                 CL_STREAM_BUF_TABLE_SIZE - 1);
        return CL_NIL;
    }
    st->out_buf_handle = h;
    st->out_buf_size = 256;
    return s;
}

CL_Obj cl_get_output_stream_string(CL_Obj stream)
{
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    char *data;
    uint32_t len;
    CL_Obj result;

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

    cl_stream_outbuf_reset(st->out_buf_handle);
    st->out_buf_len = 0;
    st->charpos = 0;
    return result;
}

CL_Obj cl_make_socket_stream(const char *host, int port)
{
    PlatformSocket sh = platform_socket_connect(host, port);
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

CL_Obj cl_make_synonym_stream(CL_Obj symbol)
{
    CL_Obj s = cl_make_stream(CL_STREAM_IO, CL_STREAM_SYNONYM);
    CL_Stream *st;
    if (CL_NULL_P(s)) return CL_NIL;
    st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    st->string_buf = symbol;  /* Store the symbol */
    return s;
}
