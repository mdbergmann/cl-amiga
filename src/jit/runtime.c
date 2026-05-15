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
#include "core/symbol.h"     /* SYM_STAR_PACKAGE */
#include "core/package.h"    /* cl_sync_current_package_from_dynamic */
#include "core/thread.h"     /* cl_symbol_value / cl_set_symbol_value */
#include "core/vm.h"         /* cl_dynbind_restore_to, CL_MAX_DYN_BINDINGS */

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

void cl_jit_runtime_init(void)
{
    /* Future: code-cache allocator, signal handler for native traps. */
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

#endif /* JIT_M68K */
