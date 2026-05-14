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
extern int    cl_arith_compare(CL_Obj a, CL_Obj b);
extern int    cl_numeric_equal(CL_Obj a, CL_Obj b);

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

#endif /* JIT_M68K */
