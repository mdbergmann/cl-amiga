/* runtime.c — C helpers invoked by JIT-emitted m68k code.
 *
 * Each helper has a stable C ABI (args on stack, result in D0) so that
 * JIT'd native code can call it via plain JSR.  Slow paths for the
 * inline fixnum templates land here; the bytecode VM's hot path
 * (CL_FIXNUM_P fast lane) is mirrored *inline* in native code, with
 * fall-through to these helpers when the fast lane bails (non-fixnum
 * operand, or fixnum overflow detected via BVS).
 *
 * GC: see runtime.h.  These helpers may allocate, which may GC.  The
 * conservative m68k-stack scan with offset validation in
 * mem.c::gc_scan_jit_native_stack roots cached operand-stack values
 * across JSR boundaries, so allocating helpers (cl_jit_runtime_add
 * bignum overflow, cl_jit_runtime_cons, cl_jit_runtime_call into a
 * Lisp callee that allocates, …) are safe for general workloads.
 */

#ifdef JIT_M68K

#include "jit/runtime.h"
#include "core/types.h"
#include "core/float.h"      /* CL_NUMBER_P, CL_REALP */
#include "core/error.h"
#include "core/symbol.h"     /* SYM_STAR_PACKAGE, SYM_TYPE_ERROR, KW_DATUM, KW_EXPECTED_TYPE */
#include "core/printer.h"    /* cl_prin1_to_string (OP_ASSERT_TYPE diagnostic) */
#include "core/package.h"    /* cl_sync_current_package_from_dynamic */
#include "core/thread.h"     /* cl_symbol_value / cl_set_symbol_value */
#include "core/vm.h"         /* cl_dynbind_restore_to, CL_MAX_DYN_BINDINGS, CL_NLXFrame */
#include "core/mem.h"        /* cl_heap.arena_size */
#include "core/compiler.h"   /* cl_compiler_mark / cl_compiler_restore_to,
                              * cl_amiga_ffi_call_dispatch */
#include "core/string_utils.h" /* cl_string_length, cl_string_set_char_at */
#include <setjmp.h>
#include <string.h>          /* memcpy for mv_values preservation */

/* Forward decls for the existing C-runtime arithmetic — same helpers
 * the bytecode VM falls through to from its OP_ADD / OP_LT slow paths.
 * Keeping the JIT slow path identical to the VM's keeps behaviour
 * (and error messages) consistent across the two execution modes. */
extern CL_Obj cl_arith_add(CL_Obj a, CL_Obj b);
extern CL_Obj cl_arith_sub(CL_Obj a, CL_Obj b);
extern CL_Obj cl_arith_mul(CL_Obj a, CL_Obj b);
extern int    cl_arith_compare(CL_Obj a, CL_Obj b);
extern int    cl_numeric_equal(CL_Obj a, CL_Obj b);
extern CL_Obj cl_car(CL_Obj obj);
extern CL_Obj cl_cdr(CL_Obj obj);
extern CL_Obj cl_cons(CL_Obj car, CL_Obj cdr);

/* From src/core/vm.c — the universal call entry point.  Handles C
 * builtins directly, sets up a stub frame for bytecode/closures, and
 * dispatches through cl_vm_run (which itself routes to native_code if
 * the callee carries one). */
extern CL_Obj cl_vm_apply(CL_Obj func, CL_Obj *args, int nargs);

/* From src/core/symbol.c — diagnostic helpers used by FLOAD's slow
 * paths to format the unbound-function error the same way the VM
 * does. */
extern const char *cl_symbol_name(CL_Obj sym);
extern CL_Obj cl_symbol_value(CL_Obj sym);

/* Address of libc `setjmp`, baked into the JSR.abs.l emitted for
 * OP_BLOCK_PUSH.  The setjmp call must originate from the JIT'd
 * function's own stack frame (so a later longjmp restores SP to the
 * correct point) — that means a JSR direct to setjmp, *not* a JSR
 * into a wrapper helper that calls setjmp and returns: such a wrapper
 * would let its frame disappear before longjmp could rewind to it,
 * which is undefined behaviour per C99 §7.13.1.1.  Captured at
 * init-time rather than recomputed on every emit. */
uint32_t cl_jit_setjmp_addr;

void cl_jit_runtime_init(void)
{
    cl_jit_setjmp_addr = (uint32_t)(uintptr_t)&setjmp;
}

/* Slow-path `+` (2 args).  Matches the VM's OP_ADD slow path: type-
 * check both operands as NUMBER, then call cl_arith_add which handles
 * fixnum, bignum, ratio, and float combinations. */
CL_Obj cl_jit_runtime_add(CL_Obj a, CL_Obj b)
{
    if (!CL_NUMBER_P(a)) cl_signal_type_error(a, "NUMBER", "+");
    if (!CL_NUMBER_P(b)) cl_signal_type_error(b, "NUMBER", "+");
    return cl_arith_add(a, b);
}

/* Slow-path `-` (2 args).  Matches the VM's OP_SUB slow path. */
CL_Obj cl_jit_runtime_sub(CL_Obj a, CL_Obj b)
{
    if (!CL_NUMBER_P(a)) cl_signal_type_error(a, "NUMBER", "-");
    if (!CL_NUMBER_P(b)) cl_signal_type_error(b, "NUMBER", "-");
    return cl_arith_sub(a, b);
}

/* Slow-path `<` (2 args), returns CL_T or CL_NIL.  Matches the VM's
 * OP_LT slow path: type-check as REAL (CLHS 12.1.4.1 rejects
 * complex), then cl_arith_compare for the cross-type compare. */
CL_Obj cl_jit_runtime_lt(CL_Obj a, CL_Obj b)
{
    if (!CL_REALP(a)) cl_signal_type_error(a, "REAL", "<");
    if (!CL_REALP(b)) cl_signal_type_error(b, "REAL", "<");
    return cl_arith_compare(a, b) < 0 ? CL_T : CL_NIL;
}

CL_Obj cl_jit_runtime_gt(CL_Obj a, CL_Obj b)
{
    if (!CL_REALP(a)) cl_signal_type_error(a, "REAL", ">");
    if (!CL_REALP(b)) cl_signal_type_error(b, "REAL", ">");
    return cl_arith_compare(a, b) > 0 ? CL_T : CL_NIL;
}

CL_Obj cl_jit_runtime_le(CL_Obj a, CL_Obj b)
{
    if (!CL_REALP(a)) cl_signal_type_error(a, "REAL", "<=");
    if (!CL_REALP(b)) cl_signal_type_error(b, "REAL", "<=");
    return cl_arith_compare(a, b) <= 0 ? CL_T : CL_NIL;
}

CL_Obj cl_jit_runtime_ge(CL_Obj a, CL_Obj b)
{
    if (!CL_REALP(a)) cl_signal_type_error(a, "REAL", ">=");
    if (!CL_REALP(b)) cl_signal_type_error(b, "REAL", ">=");
    return cl_arith_compare(a, b) >= 0 ? CL_T : CL_NIL;
}

/* Slow-path `=` (2 args).  Accepts NUMBER (not just REAL — `=` is
 * defined for complex per CLHS 12.1.4.1).  Falls through to
 * cl_numeric_equal which handles cross-type compares. */
CL_Obj cl_jit_runtime_numeq(CL_Obj a, CL_Obj b)
{
    if (!CL_NUMBER_P(a)) cl_signal_type_error(a, "NUMBER", "=");
    if (!CL_NUMBER_P(b)) cl_signal_type_error(b, "NUMBER", "=");
    return cl_numeric_equal(a, b) ? CL_T : CL_NIL;
}

/* Slow-path `*` (2 args).  No fixnum inline fast path on the JIT side
 * — `*` is rare in tight inner loops, and the MULS.L encoding would
 * roughly double the size of asm_m68k.c for marginal benefit.  All MUL
 * traffic from JIT'd code lands here; the helper itself preserves the
 * VM's inline fixnum fast path inside cl_arith_mul, so fixnum-only
 * MULs are still about as fast as bytecode (just one extra JSR per
 * call instead of inline). */
CL_Obj cl_jit_runtime_mul(CL_Obj a, CL_Obj b)
{
    if (!CL_NUMBER_P(a)) cl_signal_type_error(a, "NUMBER", "*");
    if (!CL_NUMBER_P(b)) cl_signal_type_error(b, "NUMBER", "*");
    return cl_arith_mul(a, b);
}

/* Backing for OP_CAR / OP_CDR — pure pass-through to cl_car / cl_cdr,
 * which already handle NIL→NIL, LIST type-errors with the same
 * diagnostic the bytecode VM prints, and the unbound-variable case.
 * Non-allocating, so GC-safe even without precise stack scanning. */
CL_Obj cl_jit_runtime_car(CL_Obj obj) { return cl_car(obj); }
CL_Obj cl_jit_runtime_cdr(CL_Obj obj) { return cl_cdr(obj); }

/* Backing for OP_FLOAD — mirror the VM's lookup: validate that the
 * baked-in constant really is a symbol (the JIT-time check in
 * walker_compile rejects non-symbols before we get here, but stay
 * defensive in case constants[] is mutated after compile), then
 * return s->function (or fall back to s->value for labels/flet
 * value bindings, same as OP_FLOAD).  Signals undefined-function
 * with the same diagnostic the VM emits.  Non-allocating, so this
 * step is GC-safe.  The follow-up OP_CALL is where allocation
 * lives. */
CL_Obj cl_jit_runtime_fload(CL_Obj sym)
{
    CL_Symbol *s;
    CL_Obj fval;

    if (!CL_SYMBOL_P(sym))
        cl_error(CL_ERR_TYPE,
                 "OP_FLOAD: JIT call site has non-symbol constant 0x%08x",
                 (unsigned)sym);

    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    fval = s->function;
    if (fval != CL_UNBOUND) return fval;

    /* labels / flet bind into the value cell, not the function cell. */
    fval = cl_symbol_value(sym);
    if (fval != CL_UNBOUND) return fval;

    cl_error(CL_ERR_UNDEFINED, "Undefined function: %s",
             cl_symbol_name(sym));
    return CL_NIL;   /* unreachable; cl_error longjmps */
}

/* Backing for OP_GLOAD: look up the symbol's dynamic value (thread-
 * local binding first, then the global value cell) and return it.
 * Mirrors the VM's OP_GLOAD: signals UNBOUND-VARIABLE with the same
 * diagnostic when the symbol has no value, otherwise returns the
 * value untouched.  Non-allocating, so always GC-safe. */
CL_Obj cl_jit_runtime_gload(CL_Obj sym)
{
    CL_Obj val = cl_symbol_value(sym);
    if (val == CL_UNBOUND)
        cl_error(CL_ERR_UNBOUND, "Unbound variable: %s",
                 cl_symbol_name(sym));
    return val;
}

/* Backing for OP_GSTORE: store `val` into the symbol's dynamic value
 * (thread-local binding if any, else the global value cell).  Returns
 * `val` so the JIT emitter can leave it as TOS without a separate
 * peek (matches the VM's "store does not pop" semantics for the
 * peek-then-helper path).  Mirrors OP_GSTORE's *PACKAGE* sync so
 * `(setq *package* ...)` updates cl_package_current the same way the
 * bytecode VM does.  Non-allocating, so always GC-safe. */
CL_Obj cl_jit_runtime_gstore(CL_Obj sym, CL_Obj val)
{
    cl_set_symbol_value(sym, val);
    if (sym == SYM_STAR_PACKAGE)
        cl_sync_current_package_from_dynamic();
    return val;
}

/* Backing for OP_FSTORE: write `val` into the symbol's function cell.
 * Mirrors the VM's OP_FSTORE byte-for-byte — peek semantics on the
 * caller side, plain field write here.  Non-allocating, so always
 * GC-safe; the JIT side reuses the OP_GSTORE peek pattern (flush
 * cache so TOS lives at (a7), push it as the C arg, leave it in
 * place after the JSR drops only the C args). */
CL_Obj cl_jit_runtime_fstore(CL_Obj sym, CL_Obj val)
{
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = val;
    return val;
}

/* Backing for OP_DYNBIND: save current TLV of `sym` in the dyn-bind
 * stack, then install `new_val` as the new TLV.  Mirrors the VM's
 * OP_DYNBIND exactly: overflow check against CL_MAX_DYN_BINDINGS,
 * record (symbol, old_value) — old_value is CL_TLV_ABSENT if there
 * was no prior binding — install new TLV via cl_tlv_set, and sync
 * cl_package_current when the symbol is *PACKAGE*.  Non-allocating
 * (dyn_stack and tlv_table are preallocated), so always GC-safe.
 * The matching OP_DYNUNBIND undoes one entry per binding pushed. */
void cl_jit_runtime_dynbind(CL_Obj sym, CL_Obj new_val)
{
    CL_Thread *thr = CT;
    CL_Obj old_tlv = cl_tlv_get(thr, sym);
    if (cl_dyn_top >= CL_MAX_DYN_BINDINGS)
        cl_error(CL_ERR_OVERFLOW, "Dynamic binding stack overflow");
    cl_dyn_stack[cl_dyn_top].symbol = sym;
    cl_dyn_stack[cl_dyn_top].old_value = old_tlv;
    cl_dyn_top++;
    cl_tlv_set(thr, sym, new_val);
    if (sym == SYM_STAR_PACKAGE)
        cl_sync_current_package_from_dynamic();
}

/* Backing for OP_DYNUNBIND: restore the last `count` entries from the
 * dyn-bind stack.  Wrapper around cl_dynbind_restore_to so the JIT
 * doesn't have to compute (cl_dyn_top - count) in m68k code.  The
 * restore helper itself handles per-symbol TLV write-back and the
 * *PACKAGE* sync when a *PACKAGE* binding unwinds.  Non-allocating. */
void cl_jit_runtime_dynunbind(uint32_t count)
{
    cl_dynbind_restore_to(cl_dyn_top - (int)count);
}

/* Backing for OP_CALL.  See runtime.h for the operand-stack layout
 * the JIT delivers.  Reverse-copies args into a stack-local CL_Obj[]
 * because cl_vm_apply expects args[0..N-1] = arg0..argN-1 in natural
 * order, while the m68k operand stack has argN-1 at the lowest
 * address.  The copy is bounded by OP_CALL's u8 nargs limit (256),
 * so a fixed-size buffer is sufficient.
 *
 * GC caveat — same as every allocating slow path in this file:
 * operand-stack slots and LINK-frame locals on the m68k stack
 * aren't reached by the current collector's root scan.  cl_vm_apply
 * may allocate (cons frames, format strings, the callee's own
 * arena objects), which may trigger GC.  Workloads whose live
 * unscanned m68k-stack values never overlap an allocation window
 * stay safe; the test suite's 2456 passes show this is the common
 * case in practice, but `(let ((x (alloc))) (other-call x))` has
 * a real exposure window between OP_STORE x and the next OP_LOAD x.
 * Conservative m68k-stack scanning at safepoints is the spec'd
 * fix, tracked under §"Open design choices" in
 * specs/native-backend.md. */
CL_Obj cl_jit_runtime_call(CL_Obj *operand_top, uint32_t nargs)
{
    CL_Obj args[256];
    CL_Obj func;
    uint32_t i;

    if (nargs > 255) nargs = 255;   /* defensive — OP_CALL is u8 */

    /* operand_top[0] = argN-1, [1] = argN-2, ..., [N-1] = arg0,
     * [N] = func.  Walk down to reverse into args[]. */
    for (i = 0; i < nargs; i++) {
        args[i] = operand_top[nargs - 1 - i];
    }
    func = operand_top[nargs];

    return cl_vm_apply(func, args, (int)nargs);
}

/* Backing for OP_STRUCT_REF: read slot at `idx` from `obj`.  Mirrors
 * the VM's OP_STRUCT_REF exactly: validate STRUCTURE type then check
 * the index is in range, both signaling with the same messages.
 * Non-allocating — always GC-safe. */
CL_Obj cl_jit_runtime_struct_ref(CL_Obj obj, uint32_t idx)
{
    CL_Struct *st;
    if (!CL_STRUCT_P(obj))
        cl_signal_type_error(obj, "STRUCTURE", "%STRUCT-REF");
    st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
    if (idx >= st->n_slots)
        cl_error(CL_ERR_ARGS,
                 "%%STRUCT-REF: index %u out of range (n_slots=%u)",
                 (unsigned)idx, (unsigned)st->n_slots);
    return st->slots[idx];
}

/* Backing for OP_STRUCT_SET.  Same shape as the VM: type-check,
 * bounds-check, write, and return the stored value (matching CL's
 * `setf` semantics where the assignment expression's value is the
 * new value).  Non-allocating — always GC-safe. */
CL_Obj cl_jit_runtime_struct_set(CL_Obj obj, uint32_t idx, CL_Obj val)
{
    CL_Struct *st;
    if (!CL_STRUCT_P(obj))
        cl_signal_type_error(obj, "STRUCTURE", "%STRUCT-SET");
    st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
    if (idx >= st->n_slots)
        cl_error(CL_ERR_ARGS,
                 "%%STRUCT-SET: index %u out of range (n_slots=%u)",
                 (unsigned)idx, (unsigned)st->n_slots);
    st->slots[idx] = val;
    return val;
}

/* Backing for OP_CONS: thin pass-through to cl_cons.  The bytecode
 * VM's OP_CONS is exactly two pops + cl_cons + push, so reproducing
 * that here keeps semantics identical.  Allocates one CL_Cons; the
 * conservative scan reaches the JIT'd caller's cached operand-stack
 * values across this call (see file banner). */
CL_Obj cl_jit_runtime_cons(CL_Obj car, CL_Obj cdr)
{
    return cl_cons(car, cdr);
}

/* Backing for OP_LIST: build a freshly-consed list from `n` operand-
 * stack values.  `operand_top[0]` is the TOS (last pushed), so it lands
 * at the tail of the list; `operand_top[n-1]` is the bottom (first
 * pushed), so it lands at the head.  Mirrors the VM's OP_LIST loop
 * (pop n times, cons each onto the accumulator).
 *
 * GC: each cl_cons may allocate and compact.  Two safety nets:
 *
 *   (a) `list` is a C local — CL_GC_PROTECT keeps it tracked across
 *       allocations so the partially-built list head isn't swept.
 *   (b) `operand_top` points at the JIT'd caller's flushed operand
 *       stack on the m68k stack.  The conservative scan reaches it
 *       (offset-validated) and the compactor's forwarding pass
 *       (gc_forward_jit_native_stack) rewrites those slots in place
 *       — so `operand_top[i]` stays valid across cl_cons even when a
 *       collection moves the referenced object. */
CL_Obj cl_jit_runtime_list(uint32_t n, CL_Obj *operand_top)
{
    CL_Obj list = CL_NIL;
    uint32_t i;
    CL_GC_PROTECT(list);
    for (i = 0; i < n; i++) {
        list = cl_cons(operand_top[i], list);
    }
    CL_GC_UNPROTECT(1);
    return list;
}

/* Backing for OP_RPLACA.  Type-check `cons_obj` (signal like the VM),
 * write `new_car`, return `new_car` so the JIT emitter pushes it as
 * the new TOS.  Non-allocating, so always GC-safe. */
CL_Obj cl_jit_runtime_rplaca(CL_Obj cons_obj, CL_Obj new_car)
{
    CL_Cons *cell;
    if (!CL_CONS_P(cons_obj))
        cl_error(CL_ERR_TYPE, "RPLACA: not a cons");
    cell = (CL_Cons *)CL_OBJ_TO_PTR(cons_obj);
    cell->car = new_car;
    cl_mv_count = 1;
    return new_car;
}

/* Mirror of cl_jit_runtime_rplaca for the cdr slot.  See vm.c::OP_RPLACD. */
CL_Obj cl_jit_runtime_rplacd(CL_Obj cons_obj, CL_Obj new_cdr)
{
    CL_Cons *cell;
    if (!CL_CONS_P(cons_obj))
        cl_error(CL_ERR_TYPE, "RPLACD: not a cons");
    cell = (CL_Cons *)CL_OBJ_TO_PTR(cons_obj);
    cell->cdr = new_cdr;
    cl_mv_count = 1;
    return new_cdr;
}

/* OP_ARGC.  cl_jit_invoke stashed the nargs of the innermost native
 * entry into CT->jit_current_nargs before calling into the m68k code;
 * we read it back here.  Bypasses the VM's `frame->nargs` channel
 * since JIT'd code has no CL_Frame at all. */
CL_Obj cl_jit_runtime_argc(void)
{
    cl_mv_count = 1;
    return CL_MAKE_FIXNUM(CT->jit_current_nargs);
}

/* OP_MV_LOAD.  No mv_count reset (matches vm.c). */
CL_Obj cl_jit_runtime_mv_load(uint32_t index)
{
    return (int)index < cl_mv_count ? cl_mv_values[index] : CL_NIL;
}

/* OP_NTH_VALUE.  Argument order: idx_obj passed first (at 4(a7)
 * after JSR), primary second (at 8(a7)).  The walker emits the C-ABI
 * pushes in that order — see emit_op_nth_value. */
CL_Obj cl_jit_runtime_nth_value(CL_Obj idx_obj, CL_Obj primary)
{
    int idx;
    CL_Obj result;
    if (!CL_FIXNUM_P(idx_obj))
        cl_error(CL_ERR_TYPE, "NTH-VALUE: index must be a number");
    idx = CL_FIXNUM_VAL(idx_obj);
    if (idx == 0)
        result = primary;
    else
        result = (idx > 0 && idx < cl_mv_count) ? cl_mv_values[idx] : CL_NIL;
    cl_mv_count = 1;
    return result;
}

/* OP_ASSERT_TYPE.  Mirrors vm.c byte-for-byte: build a TYPE-ERROR
 * condition with :datum and :expected-type, signal it, and if the
 * handler returns (or no handler is bound) fall through to a
 * formatted cl_error so the user sees the offending value and the
 * expected type.  Allocating — caller must cache-flush before JSR. */
void cl_jit_runtime_assert_type(CL_Obj val, CL_Obj type_spec)
{
    CL_Obj slots = CL_NIL;
    CL_Obj cond;
    char buf[128];
    char tbuf[64];
    if (cl_typep(val, type_spec)) return;
    CL_GC_PROTECT(slots);
    slots = cl_cons(cl_cons(KW_EXPECTED_TYPE, type_spec), slots);
    slots = cl_cons(cl_cons(KW_DATUM, val), slots);
    CL_GC_UNPROTECT(1);
    cond = cl_make_condition(SYM_TYPE_ERROR, slots, CL_NIL);
    cl_signal_condition(cond);
    cl_prin1_to_string(val, buf, sizeof(buf));
    cl_prin1_to_string(type_spec, tbuf, sizeof(tbuf));
    cl_error(CL_ERR_TYPE, "THE: value %s is not of type %s", buf, tbuf);
}

/* Backing for OP_ASET.  Mirrors the VM's OP_ASET dispatch byte-for-
 * byte: bit-vector, simple-string, and general-vector are all valid
 * destinations; the value type-check is destination-dependent
 * (FIXNUM 0/1 for bit-vector, CHARACTER for string, any object for
 * vector).  Returns `val` (CLHS 4.7 setf semantics — the assigned
 * value is the result of the form).  Non-allocating; cl_error
 * longjmps on type/bounds violations the same way the VM does. */
CL_Obj cl_jit_runtime_aset(CL_Obj vec_obj, CL_Obj idx_obj, CL_Obj val)
{
    int32_t idx;
    if (!CL_FIXNUM_P(idx_obj))
        cl_error(CL_ERR_TYPE, "ASET: index must be a number");
    idx = CL_FIXNUM_VAL(idx_obj);
    if (CL_BIT_VECTOR_P(vec_obj)) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(vec_obj);
        int32_t v;
        if (idx < 0 || (uint32_t)idx >= cl_bv_active_length(bv))
            cl_error(CL_ERR_ARGS, "ASET: index %d out of range", (int)idx);
        if (!CL_FIXNUM_P(val))
            cl_error(CL_ERR_TYPE, "ASET: value must be 0 or 1 for bit vector");
        v = CL_FIXNUM_VAL(val);
        if (v != 0 && v != 1)
            cl_error(CL_ERR_TYPE, "ASET: value must be 0 or 1 for bit vector");
        cl_bv_set_bit(bv, (uint32_t)idx, v);
    } else if (CL_ANY_STRING_P(vec_obj)) {
        uint32_t slen = cl_string_length(vec_obj);
        if (idx < 0 || (uint32_t)idx >= slen)
            cl_error(CL_ERR_ARGS, "ASET: index %d out of range (0-%lu)",
                     (int)idx, (unsigned long)(slen - 1));
        if (!CL_CHAR_P(val))
            cl_error(CL_ERR_TYPE, "ASET: value must be a character for string");
        cl_string_set_char_at(vec_obj, (uint32_t)idx, CL_CHAR_VAL(val));
    } else if (CL_VECTOR_P(vec_obj)) {
        CL_Vector *vec = (CL_Vector *)CL_OBJ_TO_PTR(vec_obj);
        if (idx < 0 || (uint32_t)idx >= vec->length)
            cl_error(CL_ERR_ARGS, "ASET: index %d out of range (0-%lu)",
                     (int)idx, (unsigned long)(vec->length - 1));
        cl_vector_data(vec)[idx] = val;
    } else {
        cl_error(CL_ERR_TYPE, "ASET: not a vector");
    }
    return val;
}

/* Backings for OP_MAKE_CELL / OP_CELL_REF / OP_CELL_SET_LOCAL.  Match
 * the VM dispatch byte-for-byte.  cl_make_cell already CL_GC_PROTECTs
 * `val` across its allocation, so the JIT side only has to flush the
 * cache before the JSR (so any cached operand-stack values land where
 * the conservative scan can see them). */
CL_Obj cl_jit_runtime_make_cell(CL_Obj val)
{
    return cl_make_cell(val);
}

CL_Obj cl_jit_runtime_cell_ref(CL_Obj cell_obj)
{
    CL_Cell *cell = (CL_Cell *)CL_OBJ_TO_PTR(cell_obj);
    return cell->value;
}

CL_Obj cl_jit_runtime_cell_set(CL_Obj cell_obj, CL_Obj val)
{
    CL_Cell *cell = (CL_Cell *)CL_OBJ_TO_PTR(cell_obj);
    cell->value = val;
    return val;
}

/* OP_CLOSURE backing.  Allocates a CL_Closure sized to hold n_upvals
 * upvalue slots, populates bytecode + upvalues, returns the tagged obj.
 * Walker has already filtered out captures with is_local=0 (would
 * require parent-closure upvalues, impossible under the current n_upvalues==0
 * gate) and computed values[] from the parent's frame slots.
 *
 * GC: cl_alloc may compact.  Both `tmpl` (a CL_Bytecode pointer) and the
 * incoming values are reachable via the caller's flushed operand-stack
 * frame on the m68k stack — the caller stages values right above A7 so
 * the conservative scan finds them, and we receive `tmpl` as a raw C
 * pointer baked from the constants pool.  The tmpl pointer itself is
 * stable across GC because cl_alloc relocates the data but the
 * walker's call site re-derives `tmpl` from `constants[idx]` per call,
 * so even after compaction the next dispatch picks up the new address.
 * Inside this helper we don't dereference `tmpl` until after the
 * allocation has consumed any pre-existing slack — but we DO need
 * tmpl_bc->n_upvalues for the size, which we read BEFORE allocation.
 * That's the same access pattern the VM uses (see OP_CLOSURE in vm.c). */
CL_Obj cl_jit_runtime_make_closure(CL_Obj tmpl_obj, uint32_t n_upvals,
                                   CL_Obj *values)
{
    CL_Closure *cl;
    uint32_t i;

    cl = (CL_Closure *)cl_alloc(TYPE_CLOSURE,
        sizeof(CL_Closure) + n_upvals * sizeof(CL_Obj));
    if (!cl) return CL_NIL;
    cl->bytecode = tmpl_obj;
    for (i = 0; i < n_upvals; i++) {
        cl->upvalues[i] = values[i];
    }
    return CL_PTR_TO_OBJ(cl);
}

/* OP_UPVAL backing.  Reads func_obj's closure slot `index` if it's
 * a closure; CL_NIL otherwise (same fallback the VM uses — see
 * core/vm.c OP_UPVAL).  Non-allocating, so the JIT side doesn't
 * cache_flush before the JSR.  Index is u8 in the bytecode and
 * promoted to uint32_t at the C boundary. */
CL_Obj cl_jit_runtime_upval_ref(CL_Obj func_obj, uint32_t index)
{
    CL_Closure *cl;
    if (!CL_CLOSURE_P(func_obj)) return CL_NIL;
    cl = (CL_Closure *)CL_OBJ_TO_PTR(func_obj);
    return cl->upvalues[index];
}

/* OP_CELL_SET_UPVAL backing.  Reads the cell at func_obj's upvalue
 * slot `index` and writes `val` into it.  Returns `val` (matches
 * setf-style semantics, though the walker discards the result —
 * OP_CELL_SET_UPVAL is peek-only on the operand stack).  Non-
 * closure func_obj is a no-op, mirroring the VM's else-fall-through
 * for the "shouldn't happen" path. */
CL_Obj cl_jit_runtime_cell_set_upval(CL_Obj func_obj, uint32_t index,
                                     CL_Obj val)
{
    CL_Closure *cl;
    CL_Obj cell_obj;
    CL_Cell *cell;
    if (!CL_CLOSURE_P(func_obj)) return val;
    cl = (CL_Closure *)CL_OBJ_TO_PTR(func_obj);
    cell_obj = cl->upvalues[index];
    if (!CL_CELL_P(cell_obj)) {
        cl_error(CL_ERR_TYPE,
                 "OP_CELL_SET_UPVAL: upvalue[%u] is not a cell "
                 "(internal compiler error)", (unsigned)index);
    }
    cell = (CL_Cell *)CL_OBJ_TO_PTR(cell_obj);
    cell->value = val;
    return val;
}

/* OP_TAILCALL self-TCO guard.  Called from the walker-emitted
 * arity-matching tail-call site to decide whether the runtime func
 * value would dispatch back into this same bytecode — in which case
 * the emitter's bra-back-to-entry path is safe.  Three cases that
 * count as "self":
 *   - func IS self_bc directly (rare in practice: defun always
 *     wraps via OP_CLOSURE, but local function bindings may store
 *     bare bytecodes);
 *   - func is a CL_Closure whose `bytecode` field is self_bc — the
 *     dominant case for top-level defuns;
 *   - anything else (builtin, foreign, non-heap, different bytecode,
 *     redefined symbol pointing elsewhere): 0 → walker falls back to
 *     cl_jit_runtime_call, semantics match the bytecode VM.
 *
 * Non-allocating; safe under any GC state.  Returns plain int so
 * the m68k caller can `tst.l d0; beq fallback` after the JSR. */
int cl_jit_runtime_is_self_tco(CL_Obj func, CL_Obj self_bc)
{
    if (func == self_bc) return 1;
    if (!CL_HEAP_P(func)) return 0;
    if (func >= cl_heap.arena_size) return 0;
    {
        void *p = CL_OBJ_TO_PTR(func);
        if (CL_HDR_TYPE(p) == TYPE_CLOSURE) {
            CL_Closure *c = (CL_Closure *)p;
            return (c->bytecode == self_bc) ? 1 : 0;
        }
    }
    return 0;
}

/* Backing for OP_MV_RESET.  Bytecode VM does `cl_mv_count = 1` (= a
 * single store into the current thread's CL_Thread.mv_count field).
 * The walker doesn't have CT cached in an A-register and the broader
 * "reset on every value-producing opcode" approach previously broke
 * CLOS (see specs/native-backend.md + the jit-mv-count memory), so
 * we route only the *explicit* OP_MV_RESET — the one the compiler
 * emits between (and …)/(or …) arms — through this helper.  Matches
 * bytecode-VM semantics exactly without re-opening the broader
 * question.  Non-allocating; cache regs stay valid across the JSR. */
void cl_jit_runtime_mv_reset(void)
{
    cl_mv_count = 1;
}

/* --- OP_BLOCK_PUSH / OP_BLOCK_POP / OP_BLOCK_RETURN ----------------------
 *
 * Block / return-from NLX is implemented by emitting `setjmp` *inline*
 * from the JIT'd function's own frame.  The four helpers below split
 * the work the bytecode VM does in one switch-arm so the JIT can
 * sandwich its own JSR setjmp in the middle:
 *
 *   1. `block_alloc`  — fill in cl_nlx_stack[cl_nlx_top]'s metadata
 *      (tag, marks, vm_sp/vm_fp snapshot, mv_count baseline) and return
 *      a pointer to its `buf` field.  Does NOT bump cl_nlx_top: the
 *      slot is "reserved but not yet live", so an unrelated cl_error
 *      between alloc and setjmp can't unwind through a half-initialised
 *      frame.
 *   2. The JIT then emits `JSR setjmp` with that buf pointer.  setjmp
 *      saves the JIT frame's SP/PC/callee-saved regs into the buf.
 *   3. `block_commit` — bump cl_nlx_top once setjmp returns 0 (normal
 *      path).  A single MOVE-style increment; the helper is here so
 *      the per-thread `cl_nlx_top` macro stays the single point of
 *      truth for the indirection through CT.
 *   4. `block_pop`  — search-backward decrement of cl_nlx_top mirroring
 *      VM's OP_BLOCK_POP (handles the case where intervening
 *      TAGBODY/UWPROT frames leaked past a tail-call boundary).
 *   5. `block_post_longjmp` — after setjmp returns non-zero, restore
 *      marks (dyn / handler / restart / gc-root / compiler), restore
 *      mv_count and mv_values, and return the block's stored result
 *      so the JIT can push it on the operand stack.
 *   6. `block_return` — find the matching block frame on the NLX stack,
 *      stash result + mv_state in it, and longjmp.  If an UWPROT frame
 *      is interposed, divert the longjmp to that frame's buf and stash
 *      the pending throw in cl_pending_* (same protocol the VM uses,
 *      so UWPROT cleanup runs and then the rethrow chains back to the
 *      matching block).  CL_NORETURN — never returns to the caller.
 *
 * GC: helpers don't allocate Lisp objects.  cl_error on a malformed
 * RETURN-FROM (no matching block) and the overflow check in
 * block_alloc do allocate condition objects but those paths divert
 * via the existing CL_CATCH chain — same shape as cl_jit_runtime_call's
 * error paths.  The conservative scan reaches operand-stack values the
 * JIT spilled before its JSR; cache_flush at the BLOCK_PUSH branch
 * boundary keeps that invariant. */

/* Mirror of vm.c's static nlx_frame_is_stale: an NLX frame whose
 * target VM frame has been reused (typically by a tail call that
 * landed past it) is "stale" — its longjmp target no longer
 * corresponds to an active stack frame, so the UWPROT-interposition
 * loop must skip it.  Pure JIT'd code never reuses vm_fp during its
 * execution, so this only matters when JIT'd code calls bytecode that
 * tail-calls within the same vm_fp slot. */
static int jit_nlx_frame_is_stale(CL_NLXFrame *nlx)
{
    CL_Frame *target;
    if (nlx->vm_fp <= 0) return 0;
    target = &cl_vm.frames[nlx->vm_fp - 1];
    return target->code != nlx->code;
}

void *cl_jit_runtime_block_alloc(CL_Obj tag)
{
    CL_NLXFrame *nlx;
    if (cl_nlx_top >= CL_MAX_NLX_FRAMES)
        cl_error(CL_ERR_OVERFLOW, "NLX stack overflow");
    nlx = &cl_nlx_stack[cl_nlx_top];
    nlx->type           = CL_NLX_BLOCK;
    nlx->vm_sp          = cl_vm.sp;
    nlx->vm_fp          = cl_vm.fp;
    nlx->tag            = tag;
    nlx->result         = CL_NIL;
    nlx->catch_ip       = 0;
    nlx->offset         = 0;
    nlx->code           = NULL;
    nlx->constants      = NULL;
    nlx->bytecode       = CL_NIL;
    nlx->base_fp        = 0;
    nlx->dyn_mark       = cl_dyn_top;
    nlx->handler_mark   = cl_handler_top;
    nlx->restart_mark   = cl_restart_top;
    nlx->gc_root_mark   = gc_root_count;
    nlx->compiler_mark  = cl_compiler_mark();
    nlx->mv_count       = 1;
    return &nlx->buf;
}

void cl_jit_runtime_block_commit(void)
{
    cl_nlx_top++;
}

void cl_jit_runtime_block_pop(void)
{
    /* Same search-backward semantics as VM's OP_BLOCK_POP: a tail call
     * inside the block body may have leaked an intervening frame, so a
     * blind --top would unwind to the wrong slot. */
    int bi;
    for (bi = cl_nlx_top - 1; bi >= 0; bi--) {
        if (cl_nlx_stack[bi].type == CL_NLX_BLOCK) {
            cl_nlx_top = bi;
            return;
        }
    }
    if (cl_nlx_top > 0) cl_nlx_top--;
}

CL_Obj cl_jit_runtime_block_post_longjmp(void)
{
    CL_NLXFrame *nlx = &cl_nlx_stack[cl_nlx_top];
    CL_Obj result;
    int mi;

    /* Restore cl_vm.sp / cl_vm.fp to the state captured at BLOCK_PUSH.
     * The longjmp may have fired from arbitrarily deep VM execution
     * (e.g. an inner lambda invoked via cl_jit_runtime_call →
     * cl_vm_apply that did a return-from across the closure boundary).
     * cl_vm_apply's normal-exit restore is skipped on longjmp, so SP/FP
     * are left at the inner lambda's last position.  Subsequent OP_CALL
     * dispatches from JIT'd code or the cl_jit_invoke caller's cleanup
     * (sp -= nargs+1; push result) then operate on a stale stack and
     * silently overwrite live operands a few frames up — the symptom we
     * hit is that a literal pushed by the caller before the JIT'd call
     * is no longer where the post-call code expects it.  The VM's
     * matching path does the same restore (see vm.c OP_BLOCK_RETURN). */
    cl_vm.sp = nlx->vm_sp;
    cl_vm.fp = nlx->vm_fp;

    cl_dynbind_restore_to(nlx->dyn_mark);
    cl_handler_top  = nlx->handler_mark;
    cl_restart_top  = nlx->restart_mark;
    gc_root_count   = nlx->gc_root_mark;
    cl_compiler_restore_to(nlx->compiler_mark);
    cl_mv_count = nlx->mv_count;
    for (mi = 0; mi < cl_mv_count && mi < CL_MAX_MV; mi++)
        cl_mv_values[mi] = nlx->mv_values[mi];
    result = nlx->result;
    return result;
}

void cl_jit_runtime_block_return(CL_Obj tag, CL_Obj value)
{
    int i, j, mi;
    for (i = cl_nlx_top - 1; i >= 0; i--) {
        if (cl_nlx_stack[i].type == CL_NLX_BLOCK &&
            cl_nlx_stack[i].tag == tag) {
            /* Check for an interposing UWPROT frame.  If present, the
             * spec requires its cleanup to run first; we longjmp to
             * the UWPROT and leave a pending-throw record that the
             * UWPROT epilogue uses to rethrow once cleanup is done. */
            for (j = cl_nlx_top - 1; j > i; j--) {
                if (cl_nlx_stack[j].type == CL_NLX_UWPROT &&
                    !jit_nlx_frame_is_stale(&cl_nlx_stack[j])) {
                    cl_pending_throw = 1;
                    cl_pending_tag = tag;
                    cl_pending_value = value;
                    cl_nlx_top = j;
                    CL_LONGJMP(cl_nlx_stack[j].buf, 1);
                }
            }
            cl_nlx_stack[i].result   = value;
            cl_nlx_stack[i].mv_count = cl_mv_count;
            for (mi = 0; mi < cl_mv_count && mi < CL_MAX_MV; mi++)
                cl_nlx_stack[i].mv_values[mi] = cl_mv_values[mi];
            cl_nlx_top = i;
            CL_LONGJMP(cl_nlx_stack[i].buf, 1);
        }
    }
    cl_error(CL_ERR_GENERAL, "RETURN-FROM: no block named %s",
             CL_NULL_P(tag) ? "NIL" : cl_symbol_name(tag));
}

/* --- OP_CATCH / OP_UNCATCH ----------------------------------------------
 *
 * Mirrors OP_BLOCK_PUSH's JIT-inline-setjmp protocol (alloc → JSR
 * setjmp → branch on D0; commit on the zero arm, post_longjmp + push
 * result + BRA-to-landing on the non-zero arm).  Two surface
 * differences from BLOCK:
 *
 *   - The tag is a runtime value (popped from the operand stack), not
 *     a const-pool index.  catch_alloc therefore takes the tag as its
 *     sole argument, same shape as block_alloc.
 *
 *   - The pop helper looks for CL_NLX_CATCH (not CL_NLX_BLOCK), with
 *     the same search-backward leakage tolerance as block_pop.
 *
 * The longjmp arrival site is byte-identical to block_post_longjmp:
 * restore SP/FP and the dyn/handler/restart/gc/compiler marks plus
 * mv_count/mv_values from the NLX frame.  catch frames live on the
 * same NLX stack as block/tagbody/uwprot, so a throw from any
 * intervening JIT'd or VM'd code finds and longjmps to the matching
 * buf.  No special THROW opcode is needed — `throw` is a regular CL
 * builtin (bi_throw in builtins_io.c) that calls longjmp on the
 * matching buf, and the longjmp returns into whichever side captured
 * the setjmp. */

void *cl_jit_runtime_catch_alloc(CL_Obj tag)
{
    CL_NLXFrame *nlx;
    if (cl_nlx_top >= CL_MAX_NLX_FRAMES)
        cl_error(CL_ERR_OVERFLOW, "NLX stack overflow");
    nlx = &cl_nlx_stack[cl_nlx_top];
    nlx->type           = CL_NLX_CATCH;
    nlx->vm_sp          = cl_vm.sp;
    nlx->vm_fp          = cl_vm.fp;
    nlx->tag            = tag;
    nlx->result         = CL_NIL;
    /* VM-channel fields unused for JIT-owned frames — our JSR setjmp
     * captured the frame, so longjmp returns into native code. */
    nlx->catch_ip       = 0;
    nlx->offset         = 0;
    nlx->code           = NULL;
    nlx->constants      = NULL;
    nlx->bytecode       = CL_NIL;
    nlx->base_fp        = 0;
    nlx->dyn_mark       = cl_dyn_top;
    nlx->handler_mark   = cl_handler_top;
    nlx->restart_mark   = cl_restart_top;
    nlx->gc_root_mark   = gc_root_count;
    nlx->compiler_mark  = cl_compiler_mark();
    nlx->mv_count       = 1;
    return &nlx->buf;
}

void cl_jit_runtime_catch_commit(void)
{
    cl_nlx_top++;
}

void cl_jit_runtime_catch_pop(void)
{
    /* Same search-backward semantics as VM's OP_UNCATCH: a tail call
     * inside the catch body may have leaked an intervening BLOCK /
     * TAGBODY / UWPROT frame, so a blind --top would unwind the wrong
     * slot. */
    int ci;
    for (ci = cl_nlx_top - 1; ci >= 0; ci--) {
        if (cl_nlx_stack[ci].type == CL_NLX_CATCH) {
            cl_nlx_top = ci;
            return;
        }
    }
    if (cl_nlx_top > 0) cl_nlx_top--;
}

CL_Obj cl_jit_runtime_catch_post_longjmp(void)
{
    /* Same SP/FP + marks + MV restore as block_post_longjmp.  See that
     * helper's commentary for why SP/FP must be rewound here too: a
     * throw from arbitrarily-deep VM execution skips cl_vm_apply's
     * normal-exit restore, leaving SP/FP at the inner callee's last
     * position. */
    CL_NLXFrame *nlx = &cl_nlx_stack[cl_nlx_top];
    CL_Obj result;
    int mi;

    cl_vm.sp = nlx->vm_sp;
    cl_vm.fp = nlx->vm_fp;

    cl_dynbind_restore_to(nlx->dyn_mark);
    cl_handler_top  = nlx->handler_mark;
    cl_restart_top  = nlx->restart_mark;
    gc_root_count   = nlx->gc_root_mark;
    cl_compiler_restore_to(nlx->compiler_mark);
    cl_mv_count = nlx->mv_count;
    for (mi = 0; mi < cl_mv_count && mi < CL_MAX_MV; mi++)
        cl_mv_values[mi] = nlx->mv_values[mi];
    result = nlx->result;
    return result;
}

/* --- OP_UWPROT / OP_UWPOP / OP_UWRETHROW --------------------------------
 *
 * Same JIT-inline-setjmp protocol as BLOCK_PUSH.  Five helpers split the
 * work the VM does in one switch arm:
 *
 *   1. `uwprot_alloc` — reserve cl_nlx_stack[cl_nlx_top] without
 *      committing it.  Captures vm_sp/vm_fp and the dyn/handler/restart/
 *      gc-root/compiler marks so the longjmp epilogue can rewind them.
 *      code/constants/bytecode/catch_ip/offset are zero-filled: VM's
 *      OP_UWPROT longjmp arm consumes those, but our longjmp lands in
 *      JIT'd code (via the JSR setjmp the walker emits), so they stay
 *      dead for JIT-owned frames.
 *   2. JIT then emits `JSR setjmp` so the captured frame is the JIT'd
 *      function's own stack frame.
 *   3. `uwprot_commit` — bump cl_nlx_top (mirror of block_commit).
 *   4. `uwprot_pop` — normal-exit pop: search-backward to the matching
 *      UWPROT frame (tail-call leakage compatibility, same as VM's
 *      OP_UWPOP), then clear cl_pending_throw.
 *   5. `uwprot_post_longjmp` — restore the marks captured at alloc.
 *      Unlike block_post_longjmp this does NOT touch cl_mv_count or
 *      cl_mv_values: the protected form's MVs are explicitly captured
 *      by OP_MV_TO_LIST inserted by compile_unwind_protect, and the
 *      throw site may already have set up MV state we must not clobber.
 *   6. `uwprot_rethrow` — implementation of OP_UWRETHROW.  Three
 *      branches based on cl_pending_throw:
 *        0: no pending throw — nop.
 *        1: pending THROW/RETURN-FROM — find matching catch/block/
 *           tagbody.  If an interposing UWPROT is still on the stack,
 *           longjmp to it instead (its cleanup runs first); else
 *           longjmp directly to the target.  No matching target →
 *           cl_error.
 *        2: pending error (cl_error caught by us) — find next
 *           interposing UWPROT and longjmp; if none, restore
 *           cl_error_code/cl_error_msg from the pending slots and
 *           longjmp to the outermost cl_error_frames entry (matches
 *           vm.c's OP_UWRETHROW pending==2 branch, minus the
 *           cl_vm.fp/sp restore that the JIT doesn't track).
 *      Cases 1 and 2 do not return; case 0 returns to the JIT caller. */
void *cl_jit_runtime_uwprot_alloc(void)
{
    CL_NLXFrame *nlx;
    CL_Frame    *cur;
    if (cl_nlx_top >= CL_MAX_NLX_FRAMES)
        cl_error(CL_ERR_OVERFLOW, "NLX stack overflow");
    nlx = &cl_nlx_stack[cl_nlx_top];
    /* Snapshot the current VM frame's code/constants/bytecode so the
     * staleness check (target->code == nlx->code) used by every site
     * that scans for interposing UWPROT frames — cl_error_unwind,
     * block_return, uwprot_rethrow — treats this frame as live.  The
     * VM's OP_UWPROT does the same; without it, JIT-owned UWPROT
     * frames appear stale, throws bypass cleanup, and the
     * walker-uwp-throw test fails (cleanup count stays 0). */
    cur = (cl_vm.fp > 0) ? &cl_vm.frames[cl_vm.fp - 1] : NULL;
    nlx->type           = CL_NLX_UWPROT;
    nlx->vm_sp          = cl_vm.sp;
    nlx->vm_fp          = cl_vm.fp;
    nlx->tag            = CL_NIL;
    nlx->result         = CL_NIL;
    nlx->catch_ip       = 0;
    nlx->offset         = 0;
    nlx->code           = cur ? cur->code      : NULL;
    nlx->constants      = cur ? cur->constants : NULL;
    nlx->bytecode       = cur ? cur->bytecode  : CL_NIL;
    nlx->base_fp        = 0;
    nlx->dyn_mark       = cl_dyn_top;
    nlx->handler_mark   = cl_handler_top;
    nlx->restart_mark   = cl_restart_top;
    nlx->gc_root_mark   = gc_root_count;
    nlx->compiler_mark  = cl_compiler_mark();
    nlx->mv_count       = 1;
    return &nlx->buf;
}

void cl_jit_runtime_uwprot_commit(void)
{
    cl_nlx_top++;
}

void cl_jit_runtime_uwprot_pop(void)
{
    int ui;
    for (ui = cl_nlx_top - 1; ui >= 0; ui--) {
        if (cl_nlx_stack[ui].type == CL_NLX_UWPROT) {
            cl_nlx_top = ui;
            goto done;
        }
    }
    if (cl_nlx_top > 0) cl_nlx_top--;
done:
    cl_pending_throw = 0;
}

void cl_jit_runtime_uwprot_post_longjmp(void)
{
    /* cl_nlx_top has been set to this frame's index by whatever throw
     * site triggered the longjmp (cl_error, block_return, throw, …).
     * Read the frame's saved marks and restore. */
    CL_NLXFrame *nlx = &cl_nlx_stack[cl_nlx_top];
    /* Same SP/FP restore rationale as cl_jit_runtime_block_post_longjmp:
     * a longjmp from deep inside cl_vm_run (via cl_jit_runtime_call)
     * bypasses cl_vm_apply's saved_sp/base_fp restore.  The cleanup
     * forms run next in JIT'd code; they will themselves invoke OP_CALL
     * → cl_jit_runtime_call which needs cl_vm.sp at the UWPROT-time
     * baseline so its own cl_vm_apply book-keeping doesn't drift. */
    cl_vm.sp = nlx->vm_sp;
    cl_vm.fp = nlx->vm_fp;
    cl_dynbind_restore_to(nlx->dyn_mark);
    cl_handler_top  = nlx->handler_mark;
    cl_restart_top  = nlx->restart_mark;
    gc_root_count   = nlx->gc_root_mark;
    cl_compiler_restore_to(nlx->compiler_mark);
    /* Intentionally don't touch cl_mv_count / cl_mv_values: the
     * protected-form's MVs are captured via OP_MV_TO_LIST in the
     * compiled cleanup epilogue, and the throw site may have arranged
     * its own MV state that the cleanup forms must observe. */
}

void cl_jit_runtime_uwprot_rethrow(void)
{
    int p = cl_pending_throw;
    if (p == 0) return;

    if (p == 1) {
        CL_Obj ptag = cl_pending_tag;
        CL_Obj pval = cl_pending_value;
        int i, j;
        for (i = cl_nlx_top - 1; i >= 0; i--) {
            if ((cl_nlx_stack[i].type == CL_NLX_CATCH ||
                 cl_nlx_stack[i].type == CL_NLX_BLOCK ||
                 cl_nlx_stack[i].type == CL_NLX_TAGBODY) &&
                cl_nlx_stack[i].tag == ptag) {
                for (j = cl_nlx_top - 1; j > i; j--) {
                    if (cl_nlx_stack[j].type == CL_NLX_UWPROT &&
                        !jit_nlx_frame_is_stale(&cl_nlx_stack[j])) {
                        cl_nlx_top = j;
                        CL_LONGJMP(cl_nlx_stack[j].buf, 1);
                    }
                }
                cl_pending_throw = 0;
                cl_nlx_stack[i].result = pval;
                cl_nlx_top = i;
                CL_LONGJMP(cl_nlx_stack[i].buf, 1);
            }
        }
        cl_pending_throw = 0;
        cl_error(CL_ERR_GENERAL, "No catch for tag during re-throw");
    } else {
        /* p == 2: pending error.  Search for next UWPROT, else replay
         * the original error through cl_error_frames.  This skips the
         * cl_vm.fp/sp reset the VM's OP_UWRETHROW does — the JIT
         * doesn't keep per-helper base_fp, and the outermost
         * cl_error_frames longjmp target restores VM state itself. */
        int i;
        for (i = cl_nlx_top - 1; i >= 0; i--) {
            if (cl_nlx_stack[i].type == CL_NLX_UWPROT &&
                !jit_nlx_frame_is_stale(&cl_nlx_stack[i])) {
                cl_nlx_top = i;
                CL_LONGJMP(cl_nlx_stack[i].buf, 1);
            }
        }
        {
            int err_code = cl_pending_error_code;
            cl_pending_throw = 0;
            cl_nlx_top = 0;
            cl_dynbind_restore_to(0);
            cl_handler_top = 0;
            cl_restart_top = 0;
            cl_error_code = err_code;
            strncpy(cl_error_msg, cl_pending_error_msg,
                    sizeof(cl_error_msg) - 1);
            cl_error_msg[sizeof(cl_error_msg) - 1] = '\0';
            if (cl_error_frame_top > 0) {
                CL_LONGJMP(cl_error_frames[cl_error_frame_top - 1].buf,
                           err_code);
            }
            /* No error frame — fatal, same as vm.c. */
            cl_error(err_code, "%s", cl_error_msg);  /* exits via the no-frame path */
        }
    }
}

/* Backing for OP_MV_TO_LIST.  Mirrors vm.c exactly (including the
 * cl_mv_count == 0 with non-NIL primary quirk that preserves the
 * primary as a single-element list through the unwind-protect MV
 * round-trip).  Allocates one or more conses; the conservative scan
 * roots the JIT'd caller's cached operand-stack values across the
 * call.  Resets cl_mv_count to 1 like the VM op so subsequent ops see
 * single-value semantics. */
CL_Obj cl_jit_runtime_mv_to_list(CL_Obj primary)
{
    CL_Obj list = CL_NIL;
    if (cl_mv_count == 0) {
        if (!CL_NULL_P(primary))
            list = cl_cons(primary, CL_NIL);
    } else if (cl_mv_count == 1) {
        list = cl_cons(primary, CL_NIL);
    } else {
        int i;
        for (i = cl_mv_count - 1; i >= 0; i--)
            list = cl_cons(cl_mv_values[i], list);
    }
    cl_mv_count = 1;
    return list;
}

/* See runtime.h for the contract.  Implementation tracks vm.c's
 * normal-call kw matcher (the "Normal call: push new frame" branch
 * in OP_CALL) closely so behaviour stays in lock-step.
 *
 * Non-allocating by design: the only allocation in the VM's matcher
 * is cl_cons for the &rest list, and the walker gate refuses to
 * JIT-compile bytecodes with &rest precisely so the helper can take
 * bc as a raw CL_Bytecode pointer.  If &rest support is added later,
 * the parameter must change to a CL_Obj (so the m68k-stack scan can
 * forward it across compaction) and bc must be re-derived after each
 * cl_cons; see the equivalent dance in vm.c around line 1830. */
void cl_jit_runtime_kw_prologue(CL_Bytecode *bc, uint32_t nargs,
                                CL_Obj *args, CL_Obj *frame)
{
    uint32_t i;
    uint32_t arity        = (uint32_t)(bc->arity & 0x7FFFu);
    uint32_t n_opt        = bc->n_optional;
    int      has_key      = (bc->flags & 1) != 0;
    int      allow        = (bc->flags & 2) != 0;
    uint32_t n_locals     = bc->n_locals;
    uint32_t n_positional = arity + n_opt;
    uint32_t n_extra;

    /* Defensive clamp — OP_CALL already enforces u8 nargs but match
     * cl_vm_apply's behaviour rather than trusting the caller. */
    if (nargs > 255) nargs = 255;

    /* NIL-initialize every frame slot.  Mirrors the VM's
     * `while (sp < bp + n_locals) push(NIL)` so any slot the body
     * reads before writing observes NIL, and so the suppliedp slots
     * default to NIL for keys whose argument is omitted. */
    for (i = 0; i < n_locals; i++) frame[i] = CL_NIL;

    /* Required + optional positional args copy across directly. */
    {
        uint32_t copy_n = (nargs < n_positional) ? nargs : n_positional;
        for (i = 0; i < copy_n; i++) frame[i] = args[i];
    }

    n_extra = (nargs > n_positional) ? (nargs - n_positional) : 0;

    if (!has_key) return;

    /* Odd-arg-count check per CLHS 3.4.1.4. */
    if (n_extra & 1u)
        cl_error(CL_ERR_ARGS, "odd number of keyword arguments");

    /* Scan for an explicit `:allow-other-keys t` from the caller. */
    if (!allow) {
        uint32_t k;
        for (k = 0; k + 1 < n_extra; k += 2) {
            if (args[n_positional + k] == KW_ALLOW_OTHER_KEYS &&
                !CL_NULL_P(args[n_positional + k + 1])) {
                allow = 1;
                break;
            }
        }
    }

    /* Match pairs right-to-left so the leftmost duplicate keyword
     * wins (CLHS 3.4.1.4.1). */
    if (n_extra >= 2) {
        int32_t last_ki = (int32_t)n_extra - 2;
        int32_t ki;
        if (last_ki & 1) last_ki--;
        for (ki = last_ki; ki >= 0; ki -= 2) {
            CL_Obj key = args[n_positional + ki];
            CL_Obj val = args[n_positional + ki + 1];
            int j;
            int found = 0;
            for (j = 0; j < bc->n_keys; j++) {
                if (key == bc->key_syms[j]) {
                    frame[bc->key_slots[j]] = val;
                    if (bc->key_suppliedp_slots)
                        frame[bc->key_suppliedp_slots[j]] = CL_T;
                    found = 1;
                    break;
                }
            }
            if (!found && key != KW_ALLOW_OTHER_KEYS && !allow)
                cl_error(CL_ERR_ARGS, "Unknown keyword argument: %s",
                         cl_symbol_name(key));
        }
    }
}

/* Backing for OP_AMIGA_CALL — mirrors the VM's dispatch in vm.c::
 * OP_AMIGA_CALL one-for-one so behaviour and error messages stay
 * identical between the bytecode and JIT paths.
 *
 *   base_sym   — the library-base symbol (baked into the JIT'd call
 *                site as a CL_Obj literal from the bytecode's
 *                constants[] table).
 *   offset     — LVO offset from the library base (i16 widened to i32
 *                by the caller; passed as int32_t for a clean 4-byte
 *                push slot).
 *   regspec    — packed register spec, bit 28 = void-p.
 *   n_args     — number of register args (0..7, validated by dispatch).
 *   operand_top — points at the most-recently-pushed arg on the m68k
 *                 operand stack.  Args lie at operand_top[0..n_args-1]
 *                 with operand_top[0] = argN-1 (the last pushed) and
 *                 operand_top[n_args-1] = arg0 (the first pushed).
 *
 * Reverse-copy into a stack-local CL_Obj[8] buffer so the dispatch
 * helper sees args in the same order the bytecode VM would
 * (`&cl_vm.stack[cl_vm.sp - n_args]` is bottom-to-top: buf[0] = arg0,
 * buf[n_args-1] = argN-1).  The conservative m68k-stack scan still
 * reaches the original args at operand_top, so even if dispatch
 * allocates (cl_make_bignum when the result exceeds CL_FIXNUM_MAX) the
 * caller's operand-stack values stay rooted across the call. */
CL_Obj cl_jit_runtime_amiga_call(CL_Obj base_sym, int32_t offset,
                                 uint32_t regspec, uint32_t n_args,
                                 CL_Obj *operand_top)
{
    CL_Obj args_buf[8];
    CL_Obj base_val;
    CL_ForeignPtr *bfp;
    uint32_t i;

    base_val = cl_symbol_value(base_sym);
    if (base_val == CL_UNBOUND)
        cl_error(CL_ERR_UNBOUND,
                 "OP_AMIGA_CALL: unbound library base %s",
                 cl_symbol_name(base_sym));
    if (!CL_FOREIGN_POINTER_P(base_val))
        cl_error(CL_ERR_TYPE,
                 "OP_AMIGA_CALL: %s is not a foreign pointer",
                 cl_symbol_name(base_sym));
    bfp = (CL_ForeignPtr *)CL_OBJ_TO_PTR(base_val);

    /* dispatch caps at 7; the buffer is sized to 8 anyway. */
    if (n_args > 7)
        cl_error(CL_ERR_ARGS,
                 "OP_AMIGA_CALL: too many register args (max 7), got %u",
                 (unsigned)n_args);

    for (i = 0; i < n_args; i++)
        args_buf[i] = operand_top[n_args - 1 - i];

    return cl_amiga_ffi_call_dispatch(bfp->address, (int16_t)offset,
                                      regspec, (int)n_args, args_buf);
}

/* --- OP_HANDLER_PUSH / OP_HANDLER_POP / OP_RESTART_PUSH / OP_RESTART_POP ---
 *
 * Pure push/pop on the per-thread handler / restart binding stacks.
 * Unlike OP_BLOCK_PUSH / OP_UWPROT these don't capture a setjmp frame
 * — they just register a binding that cl_signal_condition (handler)
 * or find-restart (restart) will walk later.  No JIT-side longjmp
 * choreography needed; the helpers are byte-for-byte mirrors of the
 * VM cases in core/vm.c.
 *
 * Allocation: the overflow guard calls cl_error which allocates a
 * condition.  The walker cache-flushes before the JSR (same as
 * OP_DYNBIND) so cached operand-stack values stay rooted on the
 * conservatively-scanned m68k stack across the (rare) error path. */

void cl_jit_runtime_handler_push(CL_Obj type_sym, CL_Obj handler)
{
    if (cl_handler_top >= CL_MAX_HANDLER_BINDINGS)
        cl_error(CL_ERR_OVERFLOW, "Handler stack overflow");
    cl_handler_stack[cl_handler_top].type_name = type_sym;
    cl_handler_stack[cl_handler_top].handler = handler;
    cl_handler_stack[cl_handler_top].handler_mark = cl_handler_top;
    cl_handler_top++;
}

void cl_jit_runtime_handler_pop(uint32_t count)
{
    cl_handler_top -= (int)count;
    if (cl_handler_top < 0) cl_handler_top = 0;
}

void cl_jit_runtime_restart_push(CL_Obj name_sym, CL_Obj handler, CL_Obj tag)
{
    if (cl_restart_top >= CL_MAX_RESTART_BINDINGS)
        cl_error(CL_ERR_OVERFLOW, "Restart stack overflow");
    cl_restart_stack[cl_restart_top].name = name_sym;
    cl_restart_stack[cl_restart_top].handler = handler;
    cl_restart_stack[cl_restart_top].tag = tag;
    cl_restart_top++;
}

void cl_jit_runtime_restart_pop(uint32_t count)
{
    cl_restart_top -= (int)count;
    if (cl_restart_top < 0) cl_restart_top = 0;
}

/* --- OP_TAGBODY_PUSH / OP_TAGBODY_POP / OP_TAGBODY_GO ----------------------
 *
 * Mirrors OP_BLOCK_PUSH's JIT-inline-setjmp protocol with two twists:
 *
 *   1. The longjmp arrival path *re-arms* the NLX frame (bumps
 *      cl_nlx_top after restoring marks) so the same tagbody stays
 *      live for repeated GO from inner closures.  See vm.c OP_TAGBODY_
 *      PUSH's `else` arm — cl_nlx_top++ runs both in the setjmp==0
 *      path and the setjmp!=0 path.
 *
 *   2. The longjmp arrival path returns the *tag index* (a small
 *      fixnum picked by the compiler at TAGBODY_PUSH time) so the
 *      dispatch shim the compiler emits right after PUSH can route
 *      to the right tag body via JTRUE.  The walker emits a
 *      MOVE.L D0,-(A7) right after the post_longjmp helper to land
 *      that index on the operand stack.
 *
 * GO can cross into a tagbody set up by VM code (the parent
 * function's body executed by the interpreter) or by JIT code (the
 * parent was itself walker-compiled).  Both are reachable because
 * the longjmp restores the captured setjmp/stack frame regardless
 * of who emitted it.  Helpers below are byte-for-byte mirrors of
 * vm.c::OP_TAGBODY_* so the behavior matches across VM/JIT mixes. */

void *cl_jit_runtime_tagbody_alloc(CL_Obj tagbody_id)
{
    CL_NLXFrame *nlx;
    if (cl_nlx_top >= CL_MAX_NLX_FRAMES)
        cl_error(CL_ERR_OVERFLOW, "NLX stack overflow");
    nlx = &cl_nlx_stack[cl_nlx_top];
    nlx->type           = CL_NLX_TAGBODY;
    nlx->vm_sp          = cl_vm.sp;
    nlx->vm_fp          = cl_vm.fp;
    nlx->tag            = tagbody_id;
    nlx->result         = CL_NIL;
    /* JIT lands its longjmp in native code via the JSR setjmp the
     * walker emits; the VM's catch_ip/offset/code/constants/bytecode
     * channel is unused for JIT-owned frames. */
    nlx->catch_ip       = 0;
    nlx->offset         = 0;
    nlx->code           = NULL;
    nlx->constants      = NULL;
    nlx->bytecode       = CL_NIL;
    nlx->base_fp        = 0;
    nlx->dyn_mark       = cl_dyn_top;
    nlx->handler_mark   = cl_handler_top;
    nlx->restart_mark   = cl_restart_top;
    nlx->gc_root_mark   = gc_root_count;
    nlx->compiler_mark  = cl_compiler_mark();
    nlx->mv_count       = 1;
    return &nlx->buf;
}

void cl_jit_runtime_tagbody_commit(void)
{
    cl_nlx_top++;
}

void cl_jit_runtime_tagbody_pop(void)
{
    /* Search-backward decrement (matches VM OP_TAGBODY_POP): a tail
     * call inside the body may have leaked an intervening frame. */
    int bi;
    for (bi = cl_nlx_top - 1; bi >= 0; bi--) {
        if (cl_nlx_stack[bi].type == CL_NLX_TAGBODY) {
            cl_nlx_top = bi;
            return;
        }
    }
    if (cl_nlx_top > 0) cl_nlx_top--;
}

CL_Obj cl_jit_runtime_tagbody_post_longjmp(void)
{
    /* cl_nlx_top was set to this frame's index by GO before the
     * longjmp; read the saved marks and restore.  Same SP/FP restore
     * rationale as block_post_longjmp (longjmp may have come from
     * deep inside cl_vm_run via cl_jit_runtime_call). */
    CL_NLXFrame *nlx = &cl_nlx_stack[cl_nlx_top];
    CL_Obj tag_index;

    cl_vm.sp = nlx->vm_sp;
    cl_vm.fp = nlx->vm_fp;

    cl_dynbind_restore_to(nlx->dyn_mark);
    cl_handler_top = nlx->handler_mark;
    cl_restart_top = nlx->restart_mark;
    gc_root_count  = nlx->gc_root_mark;
    cl_compiler_restore_to(nlx->compiler_mark);
    cl_mv_count    = 1;

    tag_index = nlx->result;
    /* Re-arm: a tagbody stays usable for repeated GO until the
     * matching OP_TAGBODY_POP runs.  Bumping cl_nlx_top here mirrors
     * vm.c OP_TAGBODY_PUSH's setjmp!=0 arm. */
    cl_nlx_top++;
    return tag_index;
}

void cl_jit_runtime_tagbody_go(CL_Obj tagbody_id, CL_Obj tag_index)
{
    int i, j;
    for (i = cl_nlx_top - 1; i >= 0; i--) {
        if (cl_nlx_stack[i].type == CL_NLX_TAGBODY &&
            cl_nlx_stack[i].tag == tagbody_id) {
            /* UWPROT interposition: if a non-stale UWPROT frame
             * sits between top and the target, divert there and
             * record the pending throw so cleanup runs before the
             * actual transfer. */
            for (j = cl_nlx_top - 1; j > i; j--) {
                if (cl_nlx_stack[j].type == CL_NLX_UWPROT &&
                    !jit_nlx_frame_is_stale(&cl_nlx_stack[j])) {
                    cl_pending_throw = 1;
                    cl_pending_tag = tagbody_id;
                    cl_pending_value = tag_index;
                    cl_nlx_top = j;
                    CL_LONGJMP(cl_nlx_stack[j].buf, 1);
                }
            }
            cl_nlx_stack[i].result = tag_index;
            cl_nlx_top = i;
            CL_LONGJMP(cl_nlx_stack[i].buf, 1);
        }
    }
    cl_error(CL_ERR_GENERAL, "GO: tagbody frame not found");
}

#endif /* JIT_M68K */
