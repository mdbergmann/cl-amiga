/* codeheap.h — executable memory for the AArch64 JIT.
 *
 * Apple Silicon pages are 16 KB, so one mapping per compiled function would
 * waste most of each page.  Functions are carved instead from 1 MB chunks
 * (platform_jit_map: MAP_JIT on macOS, a memfd mapped writable and
 * executable at two addresses on Linux), first fit from a free list, 16-byte aligned,
 * under one mutex because threads compile concurrently.  A block bigger than
 * a chunk gets a chunk of its own.  Every chunk is unmapped by
 * cl_codeheap_shutdown.  See specs/native-backend-a64.md, "Executable memory".
 */

#ifndef CL_JIT_CODEHEAP_H
#define CL_JIT_CODEHEAP_H

#include <stdint.h>

/* Create the heap's lock; no memory is mapped until the first install.
 * Called once at boot, before any other thread exists. */
void     cl_codeheap_init(void);

/* Copy LEN bytes of finished code into executable memory and make them
 * visible to instruction fetch.  Returns the entry address, or NULL when
 * no memory could be mapped (the function then stays interpreted). */
void    *cl_codeheap_install(const uint8_t *code, uint32_t len);

/* Return a block from cl_codeheap_install to the free list.  NULL is a
 * no-op; a pointer the heap did not hand out is refused, not freed. */
void     cl_codeheap_free(void *entry);

/* Unmap every chunk (process exit; nothing may run JIT code afterwards). */
void     cl_codeheap_shutdown(void);

/* Diagnostics for the tests: bytes mapped, bytes in live blocks. */
uint32_t cl_codeheap_mapped_bytes(void);
uint32_t cl_codeheap_live_bytes(void);

#endif /* CL_JIT_CODEHEAP_H */
