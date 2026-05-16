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
 *   - cl_jit_runtime_cons — backing for OP_CONS.  Pass-through to
 *     cl_cons.  Allocates; the conservative m68k-stack scan
 *     (mem.c::gc_scan_jit_native_stack) keeps cached operand-stack
 *     values reachable across the allocation, so this is the first
 *     allocating opcode the walker handles directly.
 *
 * GC interaction: helpers in this file may allocate, which may GC.
 * Operand-stack values held on the m68k stack between cache flushes
 * are reached by the conservative scan added in 432572c — each
 * candidate offset is validated against a real arena header before
 * `gc_mark_obj` is called, so phantom marks at non-object bytes are
 * impossible.  The collector's sliding compactor remains free to run
 * because real heap offsets the scan finds are rewritten by the
 * compactor's existing reference-rewrite pass; coincidental integers
 * are never marked, so the compactor never touches them.
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

CL_Obj cl_jit_runtime_cons(CL_Obj car, CL_Obj cdr);

/* Self-TCO predicate.  Returns 1 if `func` is the function value
 * that, when called, would dispatch back into `self_bc` (i.e., it
 * either is `self_bc` directly, or is a closure wrapping `self_bc`).
 * 0 otherwise — including for non-heap values, builtins, and other
 * bytecodes.  Called once per arity-matching OP_TAILCALL site; lets
 * the walker decide between the native-TCO bra and the helper-call
 * fallback without dereferencing closures inline in m68k. */
int cl_jit_runtime_is_self_tco(CL_Obj func, CL_Obj self_bc);

/* Backing for OP_MV_RESET — sets cl_mv_count = 1 on the current
 * thread.  Non-allocating, doesn't touch the operand stack: a plain
 * JSR with no cache flush needed. */
void cl_jit_runtime_mv_reset(void);

/* OP_BLOCK_PUSH / OP_BLOCK_POP / OP_BLOCK_RETURN.  The walker emits
 * `JSR setjmp` inline between alloc and commit so the captured frame
 * belongs to the JIT'd function itself (necessary for longjmp to
 * rewind back here).  See runtime.c for the full protocol. */
void  *cl_jit_runtime_block_alloc(CL_Obj tag);
void   cl_jit_runtime_block_commit(void);
void   cl_jit_runtime_block_pop(void);
CL_Obj cl_jit_runtime_block_post_longjmp(void);
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void   cl_jit_runtime_block_return(CL_Obj tag, CL_Obj value);

/* Address of libc setjmp, captured at init time and baked into the
 * BLOCK_PUSH emit as a JSR.abs.l immediate. */
extern uint32_t cl_jit_setjmp_addr;

#endif /* JIT_M68K */

#endif /* CL_JIT_RUNTIME_H */
