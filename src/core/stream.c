/*
 * stream.c — CL Stream heap type and output buffer management
 */

#include "stream.h"
#include "mem.h"
#include "../platform/platform.h"
#include <string.h>

/* --- Output buffer side table --- */

typedef struct {
    char    *data;
    uint32_t capacity;
    uint32_t length;
} CL_OutBuf;

static CL_OutBuf outbuf_table[CL_STREAM_BUF_TABLE_SIZE];
static int stream_initialized = 0;

void cl_stream_init(void)
{
    int i;
    for (i = 0; i < CL_STREAM_BUF_TABLE_SIZE; i++) {
        outbuf_table[i].data = NULL;
        outbuf_table[i].capacity = 0;
        outbuf_table[i].length = 0;
    }
    stream_initialized = 1;
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
