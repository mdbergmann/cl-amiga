/* runtime.c — C helpers callable from JIT-emitted code.
 *
 * Skeleton placeholder.  Slow-path arithmetic, generic CALL, GC
 * safepoint, type-error throw all land here.
 */

#ifdef JIT_M68K

#include "jit/runtime.h"

void cl_jit_runtime_init(void)
{
    /* Future: code-cache allocator, signal handler for native traps. */
}

#endif /* JIT_M68K */
