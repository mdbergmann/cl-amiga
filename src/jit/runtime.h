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

/* Backing for OP_FSTORE: write `val` into sym->function (the function
 * cell, not the value cell — that's OP_GSTORE).  Returns `val` so the
 * walker can leave it as TOS without a separate peek.  Non-allocating. */
CL_Obj cl_jit_runtime_fstore(CL_Obj sym, CL_Obj val);

void   cl_jit_runtime_dynbind  (CL_Obj sym, CL_Obj new_val);
void   cl_jit_runtime_dynunbind(uint32_t count);

CL_Obj cl_jit_runtime_fload(CL_Obj sym);
CL_Obj cl_jit_runtime_call (CL_Obj *operand_top, uint32_t nargs);

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

/* OP_UWPROT / OP_UWPOP / OP_UWRETHROW.  Same JSR-setjmp-inline shape
 * as BLOCK_PUSH; alloc reserves the NLX slot without committing it,
 * the walker emits JSR setjmp, then commit bumps cl_nlx_top.  On the
 * longjmp arrival the post_longjmp helper restores all the marks the
 * VM's OP_UWPROT longjmp arm would restore.
 *
 *   uwprot_alloc       — fill cl_nlx_stack[cl_nlx_top] (type=UWPROT,
 *                         marks, vm_sp/vm_fp); return &nlx->buf.
 *   uwprot_commit      — cl_nlx_top++.
 *   uwprot_post_longjmp — restore marks/mv_count/mv_values from frame.
 *   uwprot_pop          — normal-exit pop (search-backward for UWPROT
 *                         frame, clear cl_pending_throw).
 *   uwprot_rethrow      — cl_pending_throw==1 → find catch/block and
 *                         longjmp; ==2 → cl_error with saved state;
 *                         ==0 → nop.  May not return. */
void  *cl_jit_runtime_uwprot_alloc(void);
void   cl_jit_runtime_uwprot_commit(void);
void   cl_jit_runtime_uwprot_pop(void);
void   cl_jit_runtime_uwprot_post_longjmp(void);
void   cl_jit_runtime_uwprot_rethrow(void);

/* OP_MV_TO_LIST helper: build a list from cl_mv_values, returning it.
 * Matches the VM's quirk where cl_mv_count==0 with non-NIL primary is
 * treated as a single-value list. */
CL_Obj cl_jit_runtime_mv_to_list(CL_Obj primary);

/* Kw prologue for JIT'd functions whose lambda-list carries &key.
 * Mirrors the matching code in vm.c::OP_CALL normal path:
 * NIL-initializes the frame's slot area, copies positional args into
 * the matching slots, then performs keyword matching right-to-left so
 * the leftmost duplicate keyword wins (CLHS 3.4.1.4.1).  Signals
 * CL_ERR_ARGS on odd argument count or unknown keyword unless
 * :allow-other-keys is enabled.
 *
 *   bc     - the callee's bytecode (read-only metadata).
 *   nargs  - actual number of caller-supplied arguments.
 *   args   - pointer to the raw arg vector (`&cl_vm.stack[sp-nargs]`).
 *   frame  - pointer to the JIT frame's locals area; the walker LEAs
 *            `-(4*n_locals)(a6)` into this pointer so frame[i]
 *            corresponds to JIT slot i (forward layout — frame[0] is
 *            the lowest-addressed slot).
 *
 * Non-allocating, so passing `bc` as a raw pointer is safe — there is
 * no GC opportunity that would relocate the bytecode header.  May
 * call cl_error which longjmps out of the JIT frame; the unwind path
 * keeps GC depth tracking consistent via the CL_ErrorFrame snapshot,
 * so no manual cleanup is required.  See the walker gate for the
 * shape restrictions (&key only, no &rest / &optional / upvalues). */
void cl_jit_runtime_kw_prologue(CL_Bytecode *bc, uint32_t nargs,
                                CL_Obj *args, CL_Obj *frame);

/* Backing for OP_AMIGA_CALL — resolves the library-base symbol to a
 * foreign-pointer address (errors like the VM's OP_AMIGA_CALL on
 * unbound/wrong-type), reverse-copies n_args from the JIT's m68k
 * operand stack into a stack-local buffer, then calls
 * cl_amiga_ffi_call_dispatch.  Allocates only if the dispatch result
 * exceeds CL_FIXNUM_MAX (bignum box) and is therefore reached by the
 * conservative scan via the caller's flushed operand-stack values. */
CL_Obj cl_jit_runtime_amiga_call(CL_Obj base_sym, int32_t offset,
                                 uint32_t regspec, uint32_t n_args,
                                 CL_Obj *operand_top);

/* OP_HANDLER_PUSH / OP_HANDLER_POP / OP_RESTART_PUSH / OP_RESTART_POP.
 * Pure push/pop on the per-thread handler/restart binding stacks; no
 * setjmp involved (handlers run as ordinary calls dispatched by
 * cl_signal_condition).  Overflow goes through cl_error so the walker
 * cache-flushes before the JSR, same as OP_DYNBIND. */
void cl_jit_runtime_handler_push(CL_Obj type_sym, CL_Obj handler);
void cl_jit_runtime_handler_pop(uint32_t count);
void cl_jit_runtime_restart_push(CL_Obj name_sym, CL_Obj handler, CL_Obj tag);
void cl_jit_runtime_restart_pop(uint32_t count);

/* Address of libc setjmp, captured at init time and baked into the
 * BLOCK_PUSH emit as a JSR.abs.l immediate. */
extern uint32_t cl_jit_setjmp_addr;

#endif /* JIT_M68K */

#endif /* CL_JIT_RUNTIME_H */
