/* runtime.h — C helpers callable from JIT-emitted m68k code.
 *
 * These form the boundary between native code and the existing C
 * runtime.  Every helper is callable from the bytecode VM too — no
 * duplicated logic.  See specs/native-backend.md §"Runtime helpers".
 *
 * Skeleton: only the init hook exists.
 */

#ifndef CL_JIT_RUNTIME_H
#define CL_JIT_RUNTIME_H

#ifdef JIT_M68K

#include "core/types.h"

void cl_jit_runtime_init(void);

/* Future stable-ABI helpers (placeholder list — not yet defined):
 *
 *   CL_Obj cl_jit_runtime_add(CL_Obj a, CL_Obj b);
 *   CL_Obj cl_jit_runtime_call(CL_Obj fn, int nargs);
 *   void   cl_jit_runtime_signal_type_error(CL_Obj v, CL_Obj expected);
 *   void   cl_jit_runtime_safepoint(void);
 *   CL_Obj cl_jit_runtime_make_cell(CL_Obj v);
 */

#endif /* JIT_M68K */

#endif /* CL_JIT_RUNTIME_H */
