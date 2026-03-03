#include "builtins.h"
#include "bignum.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "../platform/platform.h"
#include <string.h>

/* Helper to register a builtin */
static void defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_cl);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    s->value = fn;
}

static void check_number(CL_Obj obj, const char *op)
{
    if (!CL_INTEGER_P(obj))
        cl_error(CL_ERR_TYPE, "%s: not a number", op);
}

/* --- Arithmetic --- */

static CL_Obj bi_add(CL_Obj *args, int n)
{
    CL_Obj result = CL_MAKE_FIXNUM(0);
    int i;
    CL_GC_PROTECT(result);
    for (i = 0; i < n; i++) {
        check_number(args[i], "+");
        result = cl_arith_add(result, args[i]);
    }
    CL_GC_UNPROTECT(1);
    return result;
}

static CL_Obj bi_sub(CL_Obj *args, int n)
{
    CL_Obj result;
    int i;
    if (n == 0) return CL_MAKE_FIXNUM(0);
    check_number(args[0], "-");
    if (n == 1) return cl_arith_negate(args[0]);
    result = args[0];
    CL_GC_PROTECT(result);
    for (i = 1; i < n; i++) {
        check_number(args[i], "-");
        result = cl_arith_sub(result, args[i]);
    }
    CL_GC_UNPROTECT(1);
    return result;
}

static CL_Obj bi_mul(CL_Obj *args, int n)
{
    CL_Obj result = CL_MAKE_FIXNUM(1);
    int i;
    CL_GC_PROTECT(result);
    for (i = 0; i < n; i++) {
        check_number(args[i], "*");
        result = cl_arith_mul(result, args[i]);
    }
    CL_GC_UNPROTECT(1);
    return result;
}

static CL_Obj bi_div(CL_Obj *args, int n)
{
    CL_Obj result;
    int i;
    check_number(args[0], "/");
    if (n == 1) {
        /* (/ x) = (/ 1 x) */
        return cl_arith_truncate(CL_MAKE_FIXNUM(1), args[0]);
    }
    result = args[0];
    CL_GC_PROTECT(result);
    for (i = 1; i < n; i++) {
        check_number(args[i], "/");
        result = cl_arith_truncate(result, args[i]);
    }
    CL_GC_UNPROTECT(1);
    return result;
}

static CL_Obj bi_truncate(CL_Obj *args, int n)
{
    check_number(args[0], "TRUNCATE");
    if (n == 1) return args[0];  /* (truncate integer) = identity for integers */
    check_number(args[1], "TRUNCATE");
    return cl_arith_truncate(args[0], args[1]);
}

static CL_Obj bi_rem(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "REM");
    check_number(args[1], "REM");
    /* REM: truncation remainder (sign follows dividend) */
    if (CL_FIXNUM_P(args[0]) && CL_FIXNUM_P(args[1])) {
        int32_t va = CL_FIXNUM_VAL(args[0]);
        int32_t vb = CL_FIXNUM_VAL(args[1]);
        if (vb == 0) cl_error(CL_ERR_DIVZERO, "Division by zero");
        return CL_MAKE_FIXNUM(va % vb);
    }
    {
        CL_Obj q = cl_arith_truncate(args[0], args[1]);
        return cl_arith_sub(args[0], cl_arith_mul(q, args[1]));
    }
}

static CL_Obj bi_mod(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "MOD");
    check_number(args[1], "MOD");
    return cl_arith_mod(args[0], args[1]);
}

static CL_Obj bi_1plus(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "1+");
    return cl_arith_add(args[0], CL_MAKE_FIXNUM(1));
}

static CL_Obj bi_1minus(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "1-");
    return cl_arith_sub(args[0], CL_MAKE_FIXNUM(1));
}

/* --- Comparison --- */

static CL_Obj bi_numeq(CL_Obj *args, int n)
{
    int i;
    check_number(args[0], "=");
    for (i = 1; i < n; i++) {
        check_number(args[i], "=");
        if (cl_arith_compare(args[0], args[i]) != 0)
            return CL_NIL;
    }
    return SYM_T;
}

static CL_Obj bi_lt(CL_Obj *args, int n)
{
    int i;
    for (i = 0; i < n - 1; i++) {
        check_number(args[i], "<");
        check_number(args[i+1], "<");
        if (cl_arith_compare(args[i], args[i+1]) >= 0)
            return CL_NIL;
    }
    return SYM_T;
}

static CL_Obj bi_gt(CL_Obj *args, int n)
{
    int i;
    for (i = 0; i < n - 1; i++) {
        check_number(args[i], ">");
        check_number(args[i+1], ">");
        if (cl_arith_compare(args[i], args[i+1]) <= 0)
            return CL_NIL;
    }
    return SYM_T;
}

static CL_Obj bi_le(CL_Obj *args, int n)
{
    int i;
    for (i = 0; i < n - 1; i++) {
        check_number(args[i], "<=");
        check_number(args[i+1], "<=");
        if (cl_arith_compare(args[i], args[i+1]) > 0)
            return CL_NIL;
    }
    return SYM_T;
}

static CL_Obj bi_ge(CL_Obj *args, int n)
{
    int i;
    for (i = 0; i < n - 1; i++) {
        check_number(args[i], ">=");
        check_number(args[i+1], ">=");
        if (cl_arith_compare(args[i], args[i+1]) < 0)
            return CL_NIL;
    }
    return SYM_T;
}

/* --- Numeric predicates --- */

static CL_Obj bi_zerop(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "ZEROP");
    return cl_arith_zerop(args[0]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_plusp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "PLUSP");
    return cl_arith_plusp(args[0]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_minusp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "MINUSP");
    return cl_arith_minusp(args[0]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_evenp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "EVENP");
    return cl_arith_evenp(args[0]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_oddp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "ODDP");
    return cl_arith_evenp(args[0]) ? CL_NIL : SYM_T;
}

static CL_Obj bi_abs(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "ABS");
    return cl_arith_abs(args[0]);
}

static CL_Obj bi_max(CL_Obj *args, int n)
{
    CL_Obj result;
    int i;
    check_number(args[0], "MAX");
    result = args[0];
    for (i = 1; i < n; i++) {
        check_number(args[i], "MAX");
        if (cl_arith_compare(args[i], result) > 0)
            result = args[i];
    }
    return result;
}

static CL_Obj bi_min(CL_Obj *args, int n)
{
    CL_Obj result;
    int i;
    check_number(args[0], "MIN");
    result = args[0];
    for (i = 1; i < n; i++) {
        check_number(args[i], "MIN");
        if (cl_arith_compare(args[i], result) < 0)
            result = args[i];
    }
    return result;
}

/* --- Extended integer functions --- */

static CL_Obj bi_gcd(CL_Obj *args, int n)
{
    CL_Obj result;
    int i;
    if (n == 0) return CL_MAKE_FIXNUM(0);
    check_number(args[0], "GCD");
    result = cl_arith_abs(args[0]);
    CL_GC_PROTECT(result);
    for (i = 1; i < n; i++) {
        check_number(args[i], "GCD");
        result = cl_arith_gcd(result, args[i]);
    }
    CL_GC_UNPROTECT(1);
    return result;
}

static CL_Obj bi_lcm(CL_Obj *args, int n)
{
    CL_Obj result;
    int i;
    if (n == 0) return CL_MAKE_FIXNUM(1);
    check_number(args[0], "LCM");
    result = cl_arith_abs(args[0]);
    CL_GC_PROTECT(result);
    for (i = 1; i < n; i++) {
        CL_Obj b, g;
        check_number(args[i], "LCM");
        b = cl_arith_abs(args[i]);
        if (cl_arith_zerop(result) || cl_arith_zerop(b)) {
            result = CL_MAKE_FIXNUM(0);
            break;
        }
        g = cl_arith_gcd(result, b);
        result = cl_arith_mul(cl_arith_truncate(result, g), b);
    }
    CL_GC_UNPROTECT(1);
    return result;
}

static CL_Obj bi_expt(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "EXPT");
    check_number(args[1], "EXPT");
    return cl_arith_expt(args[0], args[1]);
}

static CL_Obj bi_isqrt(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "ISQRT");
    return cl_arith_isqrt(args[0]);
}

static CL_Obj bi_integer_length(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "INTEGER-LENGTH");
    return CL_MAKE_FIXNUM(cl_arith_integer_length(args[0]));
}

static CL_Obj bi_ash(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "ASH");
    check_number(args[1], "ASH");
    return cl_arith_ash(args[0], args[1]);
}

static CL_Obj bi_logand(CL_Obj *args, int n)
{
    CL_Obj result = CL_MAKE_FIXNUM(-1);
    int i;
    CL_GC_PROTECT(result);
    for (i = 0; i < n; i++) {
        check_number(args[i], "LOGAND");
        result = cl_arith_logand(result, args[i]);
    }
    CL_GC_UNPROTECT(1);
    return result;
}

static CL_Obj bi_logior(CL_Obj *args, int n)
{
    CL_Obj result = CL_MAKE_FIXNUM(0);
    int i;
    CL_GC_PROTECT(result);
    for (i = 0; i < n; i++) {
        check_number(args[i], "LOGIOR");
        result = cl_arith_logior(result, args[i]);
    }
    CL_GC_UNPROTECT(1);
    return result;
}

static CL_Obj bi_logxor(CL_Obj *args, int n)
{
    CL_Obj result = CL_MAKE_FIXNUM(0);
    int i;
    CL_GC_PROTECT(result);
    for (i = 0; i < n; i++) {
        check_number(args[i], "LOGXOR");
        result = cl_arith_logxor(result, args[i]);
    }
    CL_GC_UNPROTECT(1);
    return result;
}

static CL_Obj bi_lognot(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "LOGNOT");
    return cl_arith_lognot(args[0]);
}

static CL_Obj bi_numberp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_INTEGER_P(args[0]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_integerp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_INTEGER_P(args[0]) ? SYM_T : CL_NIL;
}

/* --- Registration --- */

void cl_builtins_arith_init(void)
{
    /* Arithmetic */
    defun("+", bi_add, 0, -1);
    defun("-", bi_sub, 1, -1);
    defun("*", bi_mul, 0, -1);
    defun("/", bi_div, 1, -1);
    defun("TRUNCATE", bi_truncate, 1, 2);
    defun("REM", bi_rem, 2, 2);
    defun("MOD", bi_mod, 2, 2);
    defun("1+", bi_1plus, 1, 1);
    defun("1-", bi_1minus, 1, 1);

    /* Comparison */
    defun("=", bi_numeq, 1, -1);
    defun("<", bi_lt, 1, -1);
    defun(">", bi_gt, 1, -1);
    defun("<=", bi_le, 1, -1);
    defun(">=", bi_ge, 1, -1);

    /* Numeric predicates */
    defun("ZEROP", bi_zerop, 1, 1);
    defun("PLUSP", bi_plusp, 1, 1);
    defun("MINUSP", bi_minusp, 1, 1);
    defun("EVENP", bi_evenp, 1, 1);
    defun("ODDP", bi_oddp, 1, 1);
    defun("ABS", bi_abs, 1, 1);
    defun("MAX", bi_max, 1, -1);
    defun("MIN", bi_min, 1, -1);
    defun("NUMBERP", bi_numberp, 1, 1);
    defun("INTEGERP", bi_integerp, 1, 1);

    /* Extended integer functions */
    defun("GCD", bi_gcd, 0, -1);
    defun("LCM", bi_lcm, 0, -1);
    defun("EXPT", bi_expt, 2, 2);
    defun("ISQRT", bi_isqrt, 1, 1);
    defun("INTEGER-LENGTH", bi_integer_length, 1, 1);
    defun("ASH", bi_ash, 2, 2);
    defun("LOGAND", bi_logand, 0, -1);
    defun("LOGIOR", bi_logior, 0, -1);
    defun("LOGXOR", bi_logxor, 0, -1);
    defun("LOGNOT", bi_lognot, 1, 1);

    /* Initialize bignum subsystem */
    cl_bignum_init();
}
