/* codebuf.c — growable byte buffer for native-code emitters.
 *
 * See codebuf.h.  Backed by platform_alloc / platform_free so the
 * emitted code lives in the same allocation pool as the rest of the
 * runtime (and is executable on AmigaOS without any extra dance).
 */

#include "jit/codebuf.h"
#include "platform/platform.h"

#include <string.h>

/* Smallest growth step.  Keeps us from thrashing the allocator on
 * the first few single-byte emits but stays modest so a CodeBuf
 * that's never used after init costs almost nothing. */
#define CB_MIN_GROW 64

void cb_init(CodeBuf *cb, uint32_t initial_cap)
{
    cb->pos = 0;
    cb->cap = 0;
    cb->oom = 0;
    cb->buf = NULL;
    if (initial_cap > 0) {
        cb->buf = (uint8_t *)platform_alloc(initial_cap);
        if (cb->buf == NULL) {
            cb->oom = 1;
        } else {
            cb->cap = initial_cap;
        }
    }
}

void cb_free(CodeBuf *cb)
{
    if (cb->buf) platform_free(cb->buf);
    cb->buf = NULL;
    cb->pos = 0;
    cb->cap = 0;
    cb->oom = 0;
}

/* Ensure room for `n` more bytes.  Sets cb->oom on failure and
 * returns 0; callers shouldn't write in that case. */
static int cb_reserve(CodeBuf *cb, uint32_t n)
{
    uint32_t need;
    uint32_t new_cap;
    uint8_t *new_buf;

    if (cb->oom) return 0;

    need = cb->pos + n;
    if (need <= cb->cap) return 1;

    new_cap = cb->cap ? cb->cap : CB_MIN_GROW;
    while (new_cap < need) {
        uint32_t doubled = new_cap * 2;
        /* Defensive: overflow → bail to OOM rather than wrap. */
        if (doubled < new_cap) { cb->oom = 1; return 0; }
        new_cap = doubled;
    }

    new_buf = (uint8_t *)platform_alloc(new_cap);
    if (new_buf == NULL) { cb->oom = 1; return 0; }
    if (cb->buf) {
        memcpy(new_buf, cb->buf, cb->pos);
        platform_free(cb->buf);
    }
    cb->buf = new_buf;
    cb->cap = new_cap;
    return 1;
}

void cb_emit_u8(CodeBuf *cb, uint8_t v)
{
    if (!cb_reserve(cb, 1)) return;
    cb->buf[cb->pos++] = v;
}

void cb_emit_u16(CodeBuf *cb, uint16_t v)
{
    if (!cb_reserve(cb, 2)) return;
    cb->buf[cb->pos++] = (uint8_t)(v >> 8);
    cb->buf[cb->pos++] = (uint8_t)(v & 0xFF);
}

void cb_emit_u32(CodeBuf *cb, uint32_t v)
{
    if (!cb_reserve(cb, 4)) return;
    cb->buf[cb->pos++] = (uint8_t)(v >> 24);
    cb->buf[cb->pos++] = (uint8_t)(v >> 16);
    cb->buf[cb->pos++] = (uint8_t)(v >> 8);
    cb->buf[cb->pos++] = (uint8_t)(v & 0xFF);
}

void cb_emit_bytes(CodeBuf *cb, const uint8_t *p, uint32_t n)
{
    if (!cb_reserve(cb, n)) return;
    memcpy(cb->buf + cb->pos, p, n);
    cb->pos += n;
}

uint32_t cb_len(const CodeBuf *cb)  { return cb->pos; }
uint8_t *cb_data(CodeBuf *cb)       { return cb->buf; }

uint8_t *cb_finish(CodeBuf *cb, uint32_t *out_len)
{
    uint8_t *out;
    if (cb->oom) {
        if (cb->buf) { platform_free(cb->buf); cb->buf = NULL; }
        cb->pos = cb->cap = 0;
        if (out_len) *out_len = 0;
        return NULL;
    }
    out = cb->buf;
    if (out_len) *out_len = cb->pos;
    cb->buf = NULL;
    cb->pos = cb->cap = 0;
    return out;
}
