/* codeheap.c — executable memory for the AArch64 JIT (codeheap.h). */

#include "jit/codeheap.h"

#ifdef JIT_A64

#include <string.h>
#include "platform/platform.h"
#include "platform/platform_thread.h"

#define CH_CHUNK  (1u << 20)
#define CH_ALIGN  16u
#define CH_MAGIC  0xC0DEB10Cu
#define CH_SPLIT  64u       /* split a free block only if this much is left */

/* Every block starts with this header, CH_ALIGN bytes long, so the code
 * that follows it is 16-byte aligned too.  It lives inside the chunk and
 * is written only inside a write window.  Chunks have two views (one
 * memfd mapped twice on Linux, the same MAP_JIT address on macOS): block
 * pointers and the free list are in the WRITABLE view, and only the entry
 * handed out by install, and taken back by free, is in the executable one. */
typedef struct CHBlock {
    uint32_t size;          /* whole block, header included */
    uint32_t magic;         /* CH_MAGIC while handed out, 0 while free */
    struct CHBlock *next;   /* free-list link */
} CHBlock;

typedef char ch_header_is_aligned[(sizeof(CHBlock) == CH_ALIGN) ? 1 : -1];

/* Chunk bookkeeping is ordinary memory, never executable. */
typedef struct CHChunk {
    uint8_t *base;          /* writable view */
    uint8_t *exec;          /* executable view of the same bytes */
    uint32_t size;
    uint32_t used;          /* bump front */
    struct CHChunk *next;
} CHChunk;

static void    *ch_lock = NULL;
static CHChunk *ch_chunks = NULL;
static CHBlock *ch_free = NULL;
static uint32_t ch_mapped = 0;
static uint32_t ch_live = 0;

/* Called once at boot (cl_jit_backend_init), before any thread exists. */
void cl_codeheap_init(void)
{
    if (ch_lock == NULL) platform_mutex_init(&ch_lock);
}

static CHChunk *ch_new_chunk(uint32_t need)
{
    CHChunk *c;
    uint32_t size = (need > CH_CHUNK) ? ((need + CH_CHUNK - 1) & ~(CH_CHUNK - 1)) : CH_CHUNK;
    void *rw = NULL;
    uint8_t *exec = (uint8_t *)platform_jit_map(size, &rw);
    if (exec == NULL) return NULL;
    c = (CHChunk *)platform_alloc(sizeof(CHChunk));
    if (c == NULL) { platform_jit_unmap(exec, rw, size); return NULL; }
    c->base = (uint8_t *)rw;
    c->exec = exec;
    c->size = size;
    c->used = 0;
    c->next = ch_chunks;
    ch_chunks = c;
    ch_mapped += size;
    return c;
}

/* The chunk whose writable view holds P. */
static CHChunk *ch_chunk_of(const void *p)
{
    CHChunk *c;
    for (c = ch_chunks; c != NULL; c = c->next)
        if ((const uint8_t *)p >= c->base && (const uint8_t *)p < c->base + c->size)
            return c;
    return NULL;
}

/* Take a block of SIZE bytes (header included) and store its chunk in
 * *OWNER.  Called with the lock held and the write window open. */
static CHBlock *ch_take(uint32_t size, CHChunk **owner)
{
    CHBlock **link = &ch_free, *b;
    CHChunk *c;
    for (b = ch_free; b != NULL; link = &b->next, b = b->next) {
        if (b->size < size) continue;
        *link = b->next;
        if (b->size - size >= CH_SPLIT) {
            CHBlock *rest = (CHBlock *)((uint8_t *)b + size);
            rest->size = b->size - size;
            rest->magic = 0;
            rest->next = ch_free;
            ch_free = rest;
            b->size = size;
        }
        *owner = ch_chunk_of(b);
        return b;
    }
    c = ch_chunks;
    if (c == NULL || c->size - c->used < size) {
        c = ch_new_chunk(size);
        if (c == NULL) return NULL;
    }
    b = (CHBlock *)(c->base + c->used);
    b->size = size;
    c->used += size;
    *owner = c;
    return b;
}

void *cl_codeheap_install(const uint8_t *code, uint32_t len)
{
    uint32_t size;
    CHBlock *b;
    CHChunk *c = NULL;
    uint8_t *entry = NULL;

    if (code == NULL || len == 0 || len > 0x7FFFFFF0u - CH_ALIGN) return NULL;
    size = (len + (uint32_t)sizeof(CHBlock) + CH_ALIGN - 1) & ~(CH_ALIGN - 1);
    if (ch_lock == NULL) return NULL;
    platform_mutex_lock(ch_lock);
    platform_jit_write_begin();
    b = ch_take(size, &c);
    if (b != NULL) {
        b->magic = CH_MAGIC;
        b->next = NULL;
        memcpy(b + 1, code, len);
        entry = c->exec + ((uint8_t *)(b + 1) - c->base);
        ch_live += b->size;
    }
    platform_jit_write_end();
    platform_mutex_unlock(ch_lock);
    if (b == NULL) return NULL;
    platform_jit_flush(entry, len);
    return entry;
}

/* The block whose code starts at ENTRY (executable view), as a writable-
 * view pointer, or NULL when ENTRY is not inside a used part of a chunk. */
static CHBlock *ch_block_of(const void *entry)
{
    const CHChunk *c;
    for (c = ch_chunks; c != NULL; c = c->next)
        if ((const uint8_t *)entry >= c->exec + sizeof(CHBlock) &&
            (const uint8_t *)entry < c->exec + c->used)
            return (CHBlock *)(c->base + ((const uint8_t *)entry - c->exec)) - 1;
    return NULL;
}

void cl_codeheap_free(void *entry)
{
    CHBlock *b;
    if (entry == NULL || ch_lock == NULL) return;
    platform_mutex_lock(ch_lock);
    b = ch_block_of(entry);
    if (b != NULL && b->magic == CH_MAGIC) {
        platform_jit_write_begin();
        b->magic = 0;
        b->next = ch_free;
        ch_free = b;
        platform_jit_write_end();
        ch_live -= b->size;
    }
    platform_mutex_unlock(ch_lock);
}

void cl_codeheap_shutdown(void)
{
    CHChunk *c = ch_chunks;
    while (c != NULL) {
        CHChunk *next = c->next;
        platform_jit_unmap(c->exec, c->base, c->size);
        platform_free(c);
        c = next;
    }
    ch_chunks = NULL;
    ch_free = NULL;
    ch_mapped = ch_live = 0;
    if (ch_lock != NULL) {
        platform_mutex_destroy(ch_lock);
        ch_lock = NULL;
    }
}

uint32_t cl_codeheap_mapped_bytes(void) { return ch_mapped; }
uint32_t cl_codeheap_live_bytes(void)   { return ch_live; }

#endif /* JIT_A64 */
