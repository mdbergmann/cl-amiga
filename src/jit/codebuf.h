/* codebuf.h — growable byte buffer for native-code emitters.
 *
 * CodeBuf is the canvas every instruction emitter writes into.  It's
 * pure byte-level mechanics (no m68k knowledge): grow on overflow,
 * big-endian multi-byte writes (matches the m68k ISA), sticky OOM so
 * a sequence of emits can be checked once at the end.
 *
 * Portable C99 — compiled into both host and cross builds so unit
 * tests exercise it on the host without dragging in the m68k bits.
 */

#ifndef CL_JIT_CODEBUF_H
#define CL_JIT_CODEBUF_H

#include <stdint.h>

typedef struct {
    uint8_t  *buf;
    uint32_t  pos;     /* bytes written */
    uint32_t  cap;     /* allocated capacity */
    int       oom;     /* sticky: once set, every emit is a no-op */
} CodeBuf;

/* Initialise with the given initial capacity.  Cap may be 0 — the
 * first emit will allocate.  The struct itself is owned by the caller. */
void     cb_init(CodeBuf *cb, uint32_t initial_cap);

/* Release the buffer.  Safe to call on a freshly cb_init'd CodeBuf. */
void     cb_free(CodeBuf *cb);

/* Each emitter is a no-op once cb->oom is set, so callers can batch
 * a sequence of emits and inspect cb->oom once at the end. */
void     cb_emit_u8 (CodeBuf *cb, uint8_t  v);
void     cb_emit_u16(CodeBuf *cb, uint16_t v);   /* big-endian (m68k) */
void     cb_emit_u32(CodeBuf *cb, uint32_t v);   /* big-endian (m68k) */
void     cb_emit_bytes(CodeBuf *cb, const uint8_t *p, uint32_t n);

uint32_t cb_len(const CodeBuf *cb);
uint8_t *cb_data(CodeBuf *cb);

/* Transfer ownership of the buffer to the caller.  Returns NULL if
 * cb->oom is set (and frees any partial allocation).  On success the
 * CodeBuf is reset to a fresh empty state — caller owns the returned
 * pointer and must free it via platform_free.  *out_len receives the
 * code length in bytes (may be NULL). */
uint8_t *cb_finish(CodeBuf *cb, uint32_t *out_len);

#endif /* CL_JIT_CODEBUF_H */
