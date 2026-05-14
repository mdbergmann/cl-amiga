/* runtime.h — C helpers callable from JIT-emitted m68k code.
 *
 * These form the boundary between native code and the existing C
 * runtime.  Every helper is callable from the bytecode VM too — no
 * duplicated logic.  See specs/native-backend.md §"Runtime helpers".
 *
 * Currently exposed:
 *   - cl_jit_runtime_add  — slow-path Lisp `+` (2 args).  Used by
 *     OP_ADD's fixnum-fast-path bailout for non-fixnums or overflow.
 *   - cl_jit_runtime_sub  — slow-path Lisp `-` (2 args).
 *   - cl_jit_runtime_lt   — slow-path Lisp `<` (2 args), returns
 *     CL_T or CL_NIL.
 *   - cl_jit_runtime_gt / _le / _ge  — slow-path Lisp `>` / `<=` / `>=`.
 *   - cl_jit_runtime_numeq — slow-path Lisp `=`.
 *   - cl_jit_runtime_mul — slow-path Lisp `*` (2 args).  Mirrors VM's
 *     OP_MUL: NUMBER type-check both args, then cl_arith_mul for the
 *     cross-type compute.  May allocate (bignum); see GC caveat below.
 *     No `_div` companion yet — today's compiler never emits OP_DIV
 *     (`/` goes through the function-call path), so a JIT slow path
 *     for it would be unreachable.
 *   - cl_jit_runtime_car / _cdr — backing for OP_CAR / OP_CDR.  Direct
 *     pass-through to cl_car / cl_cdr, which already handle NIL→NIL,
 *     LIST type-error, and the unbound-variable diagnostic.
 *     Non-allocating, so always GC-safe.
 *   - cl_jit_runtime_gload — backing for OP_GLOAD.  Takes a SYMBOL,
 *     returns its dynamic value via cl_symbol_value (per-thread TLV
 *     binding then global cell).  Signals UNBOUND-VARIABLE with the
 *     VM's diagnostic on miss.  Non-allocating, so always GC-safe.
 *   - cl_jit_runtime_gstore — backing for OP_GSTORE.  Stores into the
 *     symbol's dynamic value via cl_set_symbol_value and syncs
 *     cl_package_current when the symbol is *PACKAGE*.  Returns the
 *     stored value so the emitter can leave it as TOS without a
 *     separate peek.  Non-allocating, so always GC-safe.
 *   - cl_jit_runtime_dynbind — backing for OP_DYNBIND.  Saves the
 *     symbol's current TLV in the dyn-bind stack and installs a new
 *     one (with cl_set_package sync when sym is *PACKAGE*).  Errors
 *     out on dyn-stack overflow.  Non-allocating, so always GC-safe.
 *   - cl_jit_runtime_dynunbind — backing for OP_DYNUNBIND.  Restores
 *     the last `count` entries via cl_dynbind_restore_to.
 *     Non-allocating, so always GC-safe.
 *   - cl_jit_runtime_fload — backing for OP_FLOAD.  Takes a SYMBOL
 *     (the JIT bakes constants[idx] into the call site as a literal
 *     CL_Obj), returns its function value or signals undefined-
 *     function with the VM's diagnostic.  Non-allocating, so the JIT
 *     side of the call is GC-safe.
 *   - cl_jit_runtime_call — backing for OP_CALL.  Takes (operand_top,
 *     nargs): the caller has placed [func, arg0..argN-1] on the m68k
 *     operand stack with argN-1 at the lowest address; operand_top
 *     points at argN-1.  The helper reverse-copies the args into a
 *     stack-local CL_Obj[256] (matches OP_CALL's u8 nargs limit) and
 *     dispatches via cl_vm_apply, so closures, builtins, and
 *     JIT-compiled callees all route through the existing call path.
 *     Returns the callee's primary value in D0; the m68k operand
 *     stack is unchanged across the helper, the caller pops func+args
 *     and pushes the result with a single LEA.
 *   - cl_jit_runtime_struct_ref / _set — backing for OP_STRUCT_REF /
 *     OP_STRUCT_SET.  Validate type + bounds, then read/write the slot
 *     at a baked-in u8 index.  Non-allocating, so always GC-safe.
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

CL_Obj cl_jit_runtime_add  (CL_Obj a, CL_Obj b);
CL_Obj cl_jit_runtime_sub  (CL_Obj a, CL_Obj b);
CL_Obj cl_jit_runtime_lt   (CL_Obj a, CL_Obj b);
CL_Obj cl_jit_runtime_gt   (CL_Obj a, CL_Obj b);
CL_Obj cl_jit_runtime_le   (CL_Obj a, CL_Obj b);
CL_Obj cl_jit_runtime_ge   (CL_Obj a, CL_Obj b);
CL_Obj cl_jit_runtime_numeq(CL_Obj a, CL_Obj b);

CL_Obj cl_jit_runtime_mul  (CL_Obj a, CL_Obj b);

CL_Obj cl_jit_runtime_car  (CL_Obj obj);
CL_Obj cl_jit_runtime_cdr  (CL_Obj obj);

CL_Obj cl_jit_runtime_gload (CL_Obj sym);
CL_Obj cl_jit_runtime_gstore(CL_Obj sym, CL_Obj val);

void   cl_jit_runtime_dynbind  (CL_Obj sym, CL_Obj new_val);
void   cl_jit_runtime_dynunbind(uint32_t count);

CL_Obj cl_jit_runtime_fload(CL_Obj sym);
CL_Obj cl_jit_runtime_call (CL_Obj *operand_top, uint32_t nargs);

CL_Obj cl_jit_runtime_struct_ref(CL_Obj obj, uint32_t idx);
CL_Obj cl_jit_runtime_struct_set(CL_Obj obj, uint32_t idx, CL_Obj val);

#endif /* JIT_M68K */

#endif /* CL_JIT_RUNTIME_H */
