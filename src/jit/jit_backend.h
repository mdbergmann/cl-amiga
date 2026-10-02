/* jit_backend.h — the seam between jit_common.c and a native backend.
 *
 * jit_common.c owns everything that does not depend on the CPU: the hot-call
 * policy (specs/lazy-jit.md), the on/off switches and the counters.  A
 * backend (jit_m68k.c for m68k, jit_a64.c for AArch64) supplies the code
 * generator and the entry into native code, and reads/updates the shared
 * state below.  Internal to src/jit/; the rest of the runtime uses jit.h.
 */

#ifndef CL_JIT_BACKEND_H
#define CL_JIT_BACKEND_H

#include "core/types.h"

/* %JIT-SET-ACTIVE / --no-jit: compile nothing new while 0. */
extern int      cl_jitc_active;
/* %JIT-SET-FRAMES: cl_jit_invoke pushes a shadow CL_Frame per call. */
extern int      cl_jitc_shadow_frames;
/* cl_jit_invoke entries since boot (%JIT-INVOKE-COUNT). */
extern uint32_t cl_jitc_invoke_count;
/* Native code installed since boot, in bytes (cumulative). */
extern uint32_t cl_jitc_native_bytes;

/* One-time backend setup, called by cl_jit_init before the JIT is active. */
void cl_jit_backend_init(void);

/* Process exit (cl_jit_shutdown): release the backend's own mappings. */
void cl_jit_backend_shutdown(void);

/* Translate BC to native code, or leave bc->native_code NULL when the
 * backend declines it.  REPLACE: drop code BC already carries and install
 * the new one (cl_jit_compile); without it (the hot path) code a peer
 * thread installed meanwhile is kept and this compile's result dropped. */
void cl_jit_backend_compile(CL_Bytecode *bc, int replace);

#endif /* CL_JIT_BACKEND_H */
