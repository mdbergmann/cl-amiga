/*
 * stream.c — CL Stream heap type and output buffer management
 */

#include "stream.h"
#include "symbol.h"
#include "mem.h"
#include "../platform/platform.h"
#include <string.h>

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
    return CL_PTR_TO_OBJ(st);
}

uint32_t cl_stream_alloc_outbuf(uint32_t initial_size)
{
    int i;
    char *data;

    if (!stream_initialized) cl_stream_init();
    if (initial_size == 0) initial_size = 256;

    /* Slot 0 is reserved as invalid */
    for (i = 1; i < CL_STREAM_BUF_TABLE_SIZE; i++) {
        if (outbuf_table[i].data == NULL) {
            data = (char *)platform_alloc(initial_size);
            if (!data) return 0;
            outbuf_table[i].data = data;
            outbuf_table[i].capacity = initial_size;
            outbuf_table[i].length = 0;
            return (uint32_t)i;
        }
    }
    return 0;  /* No free slots */
}

void cl_stream_free_outbuf(uint32_t handle)
{
    if (handle > 0 && handle < CL_STREAM_BUF_TABLE_SIZE &&
        outbuf_table[handle].data) {
        platform_free(outbuf_table[handle].data);
        outbuf_table[handle].data = NULL;
        outbuf_table[handle].capacity = 0;
        outbuf_table[handle].length = 0;
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

/* --- Stream I/O operations --- */

int cl_stream_read_char(CL_Obj stream)
{
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    int ch;

    if (!(st->flags & CL_STREAM_FLAG_OPEN))
        return -1;
    if (!(st->direction & CL_STREAM_INPUT))
        return -1;

    /* Check for pushed-back character */
    if (st->unread_char != -1) {
        ch = st->unread_char;
        st->unread_char = -1;
        return ch;
    }

    switch (st->stream_type) {
    case CL_STREAM_CONSOLE:
        ch = platform_getchar();
        break;
    case CL_STREAM_FILE:
        ch = platform_file_getchar((PlatformFile)st->handle_id);
        break;
    case CL_STREAM_STRING: {
        CL_String *s;
        if (CL_NULL_P(st->string_buf))
            return -1;
        s = (CL_String *)CL_OBJ_TO_PTR(st->string_buf);
        if (st->position >= st->out_buf_len)  /* out_buf_len stores end limit */
            return -1;
        ch = (unsigned char)s->data[st->position++];
        break;
    }
    case CL_STREAM_CBUF: {
        uint32_t idx = st->handle_id;
        if (idx == 0 || idx >= CL_CBUF_TABLE_SIZE || !cbuf_table[idx].data)
            return -1;
        if (st->position >= st->out_buf_len)
            return -1;
        ch = (unsigned char)cbuf_table[idx].data[st->position++];
        break;
    }
    default:
        return -1;
    }

    if (ch == -1)
        st->flags |= CL_STREAM_FLAG_EOF;
    return ch;
}

void cl_stream_write_char(CL_Obj stream, int ch)
{
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    char buf[2];

    if (!(st->flags & CL_STREAM_FLAG_OPEN))
        return;
    if (!(st->direction & CL_STREAM_OUTPUT))
        return;

    switch (st->stream_type) {
    case CL_STREAM_CONSOLE:
        buf[0] = (char)ch;
        buf[1] = '\0';
        platform_write_string(buf);
        break;
    case CL_STREAM_FILE:
        platform_file_write_char((PlatformFile)st->handle_id, ch);
        break;
    case CL_STREAM_STRING:
        cl_stream_outbuf_putchar(st, ch);
        break;
    }

    /* Track column position */
    if (ch == '\n')
        st->charpos = 0;
    else
        st->charpos++;
}

void cl_stream_write_string(CL_Obj stream, const char *str, uint32_t len)
{
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    uint32_t i;

    if (!(st->flags & CL_STREAM_FLAG_OPEN))
        return;
    if (!(st->direction & CL_STREAM_OUTPUT))
        return;

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
        for (i = 0; i < len; i++)
            platform_file_write_char((PlatformFile)st->handle_id, (unsigned char)str[i]);
        break;
    case CL_STREAM_STRING:
        for (i = 0; i < len; i++)
            cl_stream_outbuf_putchar(st, (unsigned char)str[i]);
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
}

int cl_stream_peek_char(CL_Obj stream)
{
    int ch = cl_stream_read_char(stream);
    if (ch != -1) {
        CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
        st->unread_char = ch;
    }
    return ch;
}

void cl_stream_unread_char(CL_Obj stream, int ch)
{
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    st->unread_char = ch;
}

void cl_stream_close(CL_Obj stream)
{
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    if (!(st->flags & CL_STREAM_FLAG_OPEN))
        return;

    st->flags &= ~CL_STREAM_FLAG_OPEN;

    switch (st->stream_type) {
    case CL_STREAM_FILE:
        if (st->handle_id != 0)
            platform_file_close((PlatformFile)st->handle_id);
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
    default:
        break;
    }
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
    for (i = 1; i < CL_CBUF_TABLE_SIZE; i++) {
        if (cbuf_table[i].data == NULL) {
            cbuf_table[i].data = data;
            cbuf_table[i].len = len;
            st = (CL_Stream *)CL_OBJ_TO_PTR(s);
            st->handle_id = (uint32_t)i;
            st->position = 0;
            st->out_buf_len = len;  /* End limit */
            return s;
        }
    }
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
    else
        result = cl_make_string(data, len);

    cl_stream_outbuf_reset(st->out_buf_handle);
    st->out_buf_len = 0;
    st->charpos = 0;
    return result;
}
