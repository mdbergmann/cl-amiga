/* runtime.h — C helpers callable from JIT-emitted m68k code.
 *
 * These form the boundary between native code and the existing C
 * runtime.  Every helper is callable from the bytecode VM too — no
 * duplicated logic.  See specs/native-backend.md §"Runtime helpers".
 *
 * Currently exposed:
 *   - cl_jit_runtime_add  — slow-path Lisp `+` (2 args).  Used by
 *     OP_ADD's fixnum-fast-path bailout for non-fixnums or overflow.
 *   - cl_jit_runtime_lt   — slow-path Lisp `<` (2 args), returns
 *     CL_T or CL_NIL.
 *
 * GC caveat: both helpers may allocate (bignum result on fixnum
 * overflow), which may trigger GC.  Operand-stack values live on the
 * m68k stack and are not yet rooted — calls from JIT'd code into
 * allocating helpers are unsafe for general Lisp programs.  The JIT
 * is currently only safe for pure-fixnum workloads where the slow
 * path never fires.  Conservative m68k-stack scanning is the spec'd
 * fix; tracked under §"Open design choices" in
 * specs/native-backend.md.
 */

#ifndef CL_JIT_RUNTIME_H
#define CL_JIT_RUNTIME_H

#ifdef JIT_M68K

#include "core/types.h"

void   cl_jit_runtime_init(void);

CL_Obj cl_jit_runtime_add(CL_Obj a, CL_Obj b);
CL_Obj cl_jit_runtime_lt (CL_Obj a, CL_Obj b);

#endif /* JIT_M68K */

#endif /* CL_JIT_RUNTIME_H */
