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
 * operand stack lives on m68k SP and isn't rooted — pure-fixnum
 * workloads (where the slow path is never hit) are the only currently
 * safe use.
 */

#ifdef JIT_M68K

#include "jit/runtime.h"
#include "core/types.h"
#include "core/float.h"      /* CL_NUMBER_P, CL_REALP */
#include "core/error.h"

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

#endif /* JIT_M68K */
