/* codegen_m68k.c — per-opcode m68k emitters.
 *
 * Skeleton placeholder.  Real emitters arrive opcode-by-opcode; see
 * specs/native-backend.md §"Per-opcode emitter shape" for the canonical
 * example (OP_ADD with fixnum fast path).
 */

#ifdef JIT_M68K

#include "jit/codegen_m68k.h"

void cl_jit_codegen_init(void)
{
    /* Future: build the per-opcode emitter dispatch table. */
}

#endif /* JIT_M68K */
