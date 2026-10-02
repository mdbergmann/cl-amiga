/* runtime.h — C helpers callable from JIT-emitted native code that every
 * backend shares (runtime.c).  The walkers' own helpers are declared in
 * runtime_m68k.h (m68k-stack frames) and runtime_vmstack.h (frames in
 * cl_vm.stack).
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
 *   - cl_jit_runtime_call_site — the miss path of OP_CALL's call site
 *     (below).  Takes (operand_top, nargs, site): the caller has placed [func, arg0..argN-1] on the m68k
 *     operand stack with argN-1 at the lowest address; operand_top
 *     points at argN-1.  A builtin, an FFI stub or a native callee is
 *     dispatched directly from the operand stack (jit_dispatch: the
 *     arguments copied once onto the rooted VM stack, then the C
 *     function or cl_jit_invoke); every other callee is reverse-copied
 *     into a stack-local CL_Obj[256] (OP_CALL's u8 nargs limit) and
 *     handed to cl_vm_apply, the existing call path for interpreted
 *     callees and generic functions.  cl_vm_apply answers promoted
 *     reader-GF calls from the CLOS inline cache before unwrapping the
 *     GF (the JIT-side equivalent of the interpreter's OP_CALL reader
 *     probe), so JIT'd accessor calls never enter the VM on a hit.
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

#if defined(JIT_M68K) || defined(JIT_A64)

#include "core/types.h"
#include "core/mem.h"     /* cl_heap.arena_size (cl_jit_bytecode_of) */

CL_Obj cl_jit_runtime_add  (CL_Obj a, CL_Obj b);
CL_Obj cl_jit_runtime_sub  (CL_Obj a, CL_Obj b);
CL_Obj cl_jit_runtime_lt   (CL_Obj a, CL_Obj b);
CL_Obj cl_jit_runtime_gt   (CL_Obj a, CL_Obj b);
CL_Obj cl_jit_runtime_le   (CL_Obj a, CL_Obj b);
CL_Obj cl_jit_runtime_ge   (CL_Obj a, CL_Obj b);
CL_Obj cl_jit_runtime_numeq(CL_Obj a, CL_Obj b);

CL_Obj cl_jit_runtime_mul  (CL_Obj a, CL_Obj b);

/* Slow-path `/` (2 args).  NUMBER type-check both operands, then defer
 * to `cl_arith_div` which handles the cross-type math (fixnum exact /
 * inexact-to-ratio / float).  Same shape as `_mul`: no inline fixnum
 * fast path on the JIT side — `/` is rare in tight inner loops and
 * `cl_arith_div`'s own fixnum dispatch already costs only a few
 * instructions per call. */
CL_Obj cl_jit_runtime_div  (CL_Obj a, CL_Obj b);

CL_Obj cl_jit_runtime_car  (CL_Obj obj);
CL_Obj cl_jit_runtime_cdr  (CL_Obj obj);

CL_Obj cl_jit_runtime_gload (CL_Obj sym);
CL_Obj cl_jit_runtime_gstore(CL_Obj sym, CL_Obj val);

/* Backing for OP_FSTORE: write `val` into sym->function (the function
 * cell, not the value cell — that's OP_GSTORE).  Returns `val` so the
 * walker can leave it as TOS without a separate peek.  Non-allocating. */
CL_Obj cl_jit_runtime_fstore(CL_Obj sym, CL_Obj val);

void   cl_jit_runtime_dynbind  (CL_Obj sym, CL_Obj new_val);
void   cl_jit_runtime_dynunbind(uint32_t count);

/* OP_PROGV_BIND backing.  Pops `symbols_list` and `values_list` from
 * the operand stack (caller-side via cache_pop_to_dn), snapshots the
 * current cl_dyn_top, then walks both lists in lockstep:
 *   - each car of symbols_list must be a SYMBOL (else cl_signal_type_error);
 *   - paired value is car of values_list, or CL_UNBOUND if values_list
 *     runs out (CLHS PROGV: "If too few values are supplied … the
 *     remaining symbols are bound and then made to have no value");
 *   - dyn-stack overflow → cl_error.
 * Returns the saved dyn_top as a CL_MAKE_FIXNUM so the walker can push
 * it as the mark for the matching OP_PROGV_UNBIND.  Non-allocating on
 * the success path, but error signaling may allocate a condition — the
 * walker cache_flushes before the JSR. */
CL_Obj cl_jit_runtime_progv_bind(CL_Obj symbols_list, CL_Obj values_list);

/* OP_PROGV_UNBIND backing.  Restore dyn-bindings down to `mark_obj`
 * (CL_FIXNUM-encoded, untag in helper) and return `result` so the
 * walker can leave the body result as the new TOS.  Non-allocating on
 * the normal path; cl_set_package sync inside cl_dynbind_restore_to
 * may signal, so the walker cache_flushes before the JSR — same
 * discipline as OP_DYNUNBIND. */
CL_Obj cl_jit_runtime_progv_unbind(CL_Obj mark_obj, CL_Obj result);

CL_Obj cl_jit_runtime_fload(CL_Obj sym);

/* OP_APPLY backing.  Mirrors the VM's OP_APPLY semantics: walks the
 * arglist into a stack-local CL_Obj[64] (max 64 args, matching the VM),
 * resolves `func` if it's a SYMBOL, then delegates to `cl_vm_apply`
 * which handles builtins, closures, and JIT-compiled callees uniformly.
 * Allocates (cl_vm_apply may cons frames, invoke user code), so the
 * walker cache_flushes before the JSR — the conservative m68k-stack
 * scan keeps cached operand-stack residuals reachable across the call. */
CL_Obj cl_jit_runtime_apply(CL_Obj func, CL_Obj arglist);

CL_Obj cl_jit_runtime_struct_ref(CL_Obj obj, uint32_t idx);
CL_Obj cl_jit_runtime_struct_set(CL_Obj obj, uint32_t idx, CL_Obj val);

CL_Obj cl_jit_runtime_cons(CL_Obj car, CL_Obj cdr);

/* OP_LIST backing.  Builds a list of `n` elements from the JIT'd
 * caller's flushed operand stack — operand_top[0] is TOS (last in
 * list), operand_top[n-1] is bottom (head).  Each cl_cons may
 * allocate; the conservative scan + JIT-stack forwarding pass keep
 * the operand_top slots valid across collections, and the helper
 * CL_GC_PROTECTs the partially-built list. */
CL_Obj cl_jit_runtime_list(uint32_t n, CL_Obj *operand_top);

/* OP_RPLACA backing.  Type-checks `cons_obj` (signal like the VM),
 * writes new_car, returns new_car as the new TOS.  Non-allocating. */
CL_Obj cl_jit_runtime_rplaca(CL_Obj cons_obj, CL_Obj new_car);

/* OP_RPLACD backing.  Mirror of cl_jit_runtime_rplaca for the cdr
 * slot.  Resets cl_mv_count = 1 like the VM op. */
CL_Obj cl_jit_runtime_rplacd(CL_Obj cons_obj, CL_Obj new_cdr);

/* OP_ARGC backing.  Returns CL_MAKE_FIXNUM of the nargs the innermost
 * JIT-entry was invoked with (sourced from
 * CL_Thread.jit_current_nargs, set by cl_jit_invoke).  Resets
 * cl_mv_count = 1. */
CL_Obj cl_jit_runtime_argc(void);

/* OP_MV_LOAD backing.  Returns cl_mv_values[index] if index <
 * cl_mv_count, NIL otherwise.  Matches vm.c::OP_MV_LOAD: does NOT
 * reset cl_mv_count (so consecutive MV_LOAD reads see the same
 * value buffer). */
CL_Obj cl_jit_runtime_mv_load(uint32_t index);

/* OP_NTH_VALUE backing.  Pops primary + idx_obj (caller passes both
 * by value in C-ABI order = idx then primary in m68k push order).
 * idx must be a fixnum (cl_error on type mismatch).  idx == 0
 * returns primary; idx > 0 reads cl_mv_values[idx] with NIL fallback
 * when idx >= cl_mv_count.  Resets cl_mv_count = 1. */
CL_Obj cl_jit_runtime_nth_value(CL_Obj idx_obj, CL_Obj primary);

/* OP_ASSERT_TYPE backing.  Peek-only: caller passes the value and
 * the type-spec CL_Obj; helper does cl_typep, allocates a
 * type-error condition + signals on mismatch, returns normally on
 * pass.  Cache flush before the JSR is mandatory — the condition
 * allocation may GC. */
void cl_jit_runtime_assert_type(CL_Obj val, CL_Obj type_spec);

/* OP_ASET backing.  Same dispatch as the VM's OP_ASET: bit-vector,
 * simple-string, and general-vector are valid destinations with
 * destination-dependent value type checks.  Returns `val` so the
 * walker can push it as the new TOS.  Non-allocating; may longjmp
 * out via cl_error on type / bounds violations. */
CL_Obj cl_jit_runtime_aset(CL_Obj vec_obj, CL_Obj idx_obj, CL_Obj val);

/* String-scan fast path (opcodes.h 0xC0-0xC4, specs/performance.md 4.4).
 *   aref       — OP_AREF: cl_vector_ref1 with the accessor kind.
 *   chareq     — OP_CHAREQ's slow path (one operand not a character:
 *                CHAR='s type error).
 *   cmp_kind   — OP_CMP_BR's slow path: cl_vm_compare_kind as T/NIL.
 *   push_local — OP_PUSH_LOCAL: *slot = (cons item *slot), returns it.
 *                `slot` points into the JIT'd frame on the m68k stack,
 *                which the conservative scan reaches, so the list stays
 *                live across the allocation.
 *   pop_local  — OP_POP_LOCAL: returns (car *slot), *slot = (cdr *slot);
 *                CAR's type error on a non-list. */
CL_Obj cl_jit_runtime_aref(CL_Obj vec_obj, CL_Obj idx_obj, uint32_t kind);
CL_Obj cl_jit_runtime_chareq(CL_Obj a, CL_Obj b);
CL_Obj cl_jit_runtime_cmp_kind(CL_Obj a, CL_Obj b, uint32_t cmp);
CL_Obj cl_jit_runtime_push_local(CL_Obj item, CL_Obj *slot);
CL_Obj cl_jit_runtime_pop_local(CL_Obj *slot);

/* OP_MAKE_CELL / OP_CELL_REF / OP_CELL_SET_LOCAL backings.  Mirror the
 * VM cases exactly: make_cell allocates a fresh CL_Cell wrapping `val`,
 * cell_ref dereferences cell->value, cell_set writes cell->value and
 * returns it. */
CL_Obj cl_jit_runtime_make_cell(CL_Obj val);
CL_Obj cl_jit_runtime_cell_ref (CL_Obj cell_obj);
CL_Obj cl_jit_runtime_cell_set (CL_Obj cell_obj, CL_Obj val);

/* OP_CLOSURE backing.  Allocates CL_Closure(tmpl, upvalues[n_upvals])
 * and copies values[0..n_upvals-1] into the upvalues array.  The walker
 * builds the `values` array on the m68k stack by emitting per-capture
 * loads from the parent frame (capture descriptors with is_local=1) or
 * — when the enclosing function is itself a closure (n_upvalues > 0) —
 * from the parent's upvalues via OP_UPVAL-style helper reads. */
CL_Obj cl_jit_runtime_make_closure(CL_Obj tmpl_obj, uint32_t n_upvals,
                                   CL_Obj *values);

/* OP_UPVAL / OP_CELL_SET_UPVAL backings.  Both take `func_obj` (the
 * function object the JIT'd frame was entered with — closure or raw
 * bytecode, sourced from 8(a6)).  upval_ref returns CL_NIL for the
 * non-closure case so a plain bytecode JIT-invoked outside any closure
 * dispatch doesn't trap on a missing closure (matches VM semantics —
 * see core/vm.c OP_UPVAL).  cell_set_upval mirrors the VM's
 * type-check (cl_error if the slot isn't a CL_Cell) and is peek-only:
 * the walker leaves TOS in place. */
CL_Obj cl_jit_runtime_upval_ref(CL_Obj func_obj, uint32_t index);
CL_Obj cl_jit_runtime_cell_set_upval(CL_Obj func_obj, uint32_t index,
                                     CL_Obj val);

/* Self-TCO predicate.  Returns 1 if `func` is the function value
 * that, when called, would dispatch back into `self_bc` (i.e., it
 * either is `self_bc` directly, or is a closure wrapping `self_bc`).
 * 0 otherwise — including for non-heap values, builtins, and other
 * bytecodes.  Called once per arity-matching OP_TAILCALL site; lets
 * the walker decide between the native-TCO bra and the helper-call
 * fallback without dereferencing closures inline in m68k. */
int cl_jit_runtime_is_self_tco(CL_Obj func, CL_Obj self_bc);

/* Native loop poll: what the interpreter does on every backward jump (GC
 * safepoint, pending interrupt, Ctrl-C), run once every
 * CL_JIT_LOOP_POLL_EVERY loop iterations.  The walker emits, at every loop
 * header (cache depth 0, so every live value is on the m68k stack for the
 * conservative scan):
 *
 *     subq.w  #1,jit_loop_ctr(a3)
 *     bcc.w   .skip                  ; no borrow: not yet
 *     jsr     cl_jit_runtime_loop_poll
 *   .skip:
 *
 * The helper resets the countdown first, then may collect garbage, run an
 * interrupt function or enter the debugger -- any of which may leave
 * non-locally, as from any other helper. */
#define CL_JIT_LOOP_POLL_EVERY 256
void cl_jit_runtime_loop_poll(void);

/* Backing for OP_MV_RESET — sets cl_mv_count = 1 on the current
 * thread.  Non-allocating, doesn't touch the operand stack: a plain
 * JSR with no cache flush needed. */
void cl_jit_runtime_mv_reset(void);

/* Raw CL_Bytecode* a callee (bytecode or closure) carries, or NULL.  Both
 * walkers' call paths classify callees with it.  A raw pointer: re-derive it
 * after anything that may compact (a safepoint, an allocation). */
static inline CL_Bytecode *cl_jit_bytecode_of(CL_Obj func, uint32_t ftype)
{
    if (ftype == TYPE_CLOSURE) {
        CL_Obj bco = ((CL_Closure *)CL_OBJ_TO_PTR(func))->bytecode;
        if (CL_HEAP_P(bco) && bco < cl_heap.arena_size && CL_BYTECODE_P(bco))
            return (CL_Bytecode *)CL_OBJ_TO_PTR(bco);
        return NULL;
    }
    return (CL_Bytecode *)CL_OBJ_TO_PTR(func);
}

#endif /* JIT_M68K || JIT_A64 */

#endif /* CL_JIT_RUNTIME_H */
