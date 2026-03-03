#include "builtins.h"
#include "bignum.h"
#include "float.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "vm.h"
#include "error.h"
#include "../platform/platform.h"
#include <string.h>
#include <math.h>

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
    if (!CL_NUMBER_P(obj))
        cl_error(CL_ERR_TYPE, "%s: not a number", op);
}

static void check_integer(CL_Obj obj, const char *op)
{
    if (!CL_INTEGER_P(obj))
        cl_error(CL_ERR_TYPE, "%s: not an integer", op);
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
    int i, has_float = 0;
    check_number(args[0], "/");
    /* Check if any argument is a float */
    for (i = 0; i < n; i++) {
        check_number(args[i], "/");
        if (CL_FLOATP(args[i])) has_float = 1;
    }
    if (n == 1) {
        if (has_float)
            return cl_float_div(cl_make_single_float(1.0f), args[0]);
        return cl_arith_truncate(CL_MAKE_FIXNUM(1), args[0]);
    }
    result = args[0];
    CL_GC_PROTECT(result);
    for (i = 1; i < n; i++) {
        if (has_float)
            result = cl_float_div(result, args[i]);
        else
            result = cl_arith_truncate(result, args[i]);
    }
    CL_GC_UNPROTECT(1);
    return result;
}

static CL_Obj bi_truncate(CL_Obj *args, int n)
{
    check_integer(args[0], "TRUNCATE");
    if (n == 1) return args[0];  /* (truncate integer) = identity for integers */
    check_integer(args[1], "TRUNCATE");
    return cl_arith_truncate(args[0], args[1]);
}

static CL_Obj bi_rem(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_integer(args[0], "REM");
    check_integer(args[1], "REM");
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
    check_integer(args[0], "MOD");
    check_integer(args[1], "MOD");
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
    check_integer(args[0], "EVENP");
    return cl_arith_evenp(args[0]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_oddp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_integer(args[0], "ODDP");
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
    check_integer(args[0], "GCD");
    result = cl_arith_abs(args[0]);
    CL_GC_PROTECT(result);
    for (i = 1; i < n; i++) {
        check_integer(args[i], "GCD");
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
    check_integer(args[0], "LCM");
    result = cl_arith_abs(args[0]);
    CL_GC_PROTECT(result);
    for (i = 1; i < n; i++) {
        CL_Obj b, g;
        check_integer(args[i], "LCM");
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
    check_integer(args[0], "EXPT");
    check_integer(args[1], "EXPT");
    return cl_arith_expt(args[0], args[1]);
}

static CL_Obj bi_isqrt(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_integer(args[0], "ISQRT");
    return cl_arith_isqrt(args[0]);
}

static CL_Obj bi_integer_length(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_integer(args[0], "INTEGER-LENGTH");
    return CL_MAKE_FIXNUM(cl_arith_integer_length(args[0]));
}

static CL_Obj bi_ash(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_integer(args[0], "ASH");
    check_integer(args[1], "ASH");
    return cl_arith_ash(args[0], args[1]);
}

static CL_Obj bi_logand(CL_Obj *args, int n)
{
    CL_Obj result = CL_MAKE_FIXNUM(-1);
    int i;
    CL_GC_PROTECT(result);
    for (i = 0; i < n; i++) {
        check_integer(args[i], "LOGAND");
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
        check_integer(args[i], "LOGIOR");
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
        check_integer(args[i], "LOGXOR");
        result = cl_arith_logxor(result, args[i]);
    }
    CL_GC_UNPROTECT(1);
    return result;
}

static CL_Obj bi_lognot(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_integer(args[0], "LOGNOT");
    return cl_arith_lognot(args[0]);
}

static CL_Obj bi_numberp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_NUMBER_P(args[0]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_integerp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_INTEGER_P(args[0]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_floatp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_FLOATP(args[0]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_realp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_REALP(args[0]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_rationalp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_INTEGER_P(args[0]) ? SYM_T : CL_NIL;
}

/* --- Float-specific functions (Step 9) --- */

static void check_float(CL_Obj obj, const char *op)
{
    if (!CL_FLOATP(obj))
        cl_error(CL_ERR_TYPE, "%s: not a float", op);
}

/* (float number &optional prototype) */
static CL_Obj bi_float(CL_Obj *args, int n)
{
    CL_Obj num = args[0];
    check_number(num, "FLOAT");
    if (n >= 2) {
        /* Prototype determines result type */
        check_float(args[1], "FLOAT");
        if (CL_DOUBLE_FLOAT_P(args[1]))
            return cl_make_double_float(cl_to_double(num));
        return cl_make_single_float(cl_to_float(num));
    }
    /* No prototype: floats unchanged, integers → single-float */
    if (CL_FLOATP(num))
        return num;
    return cl_make_single_float(cl_to_float(num));
}

/* (float-digits float) → fixnum */
static CL_Obj bi_float_digits(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_float(args[0], "FLOAT-DIGITS");
    if (CL_DOUBLE_FLOAT_P(args[0]))
        return CL_MAKE_FIXNUM(53);
    return CL_MAKE_FIXNUM(24);
}

/* (float-radix float) → fixnum (always 2) */
static CL_Obj bi_float_radix(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_float(args[0], "FLOAT-RADIX");
    return CL_MAKE_FIXNUM(2);
}

/* (float-sign float-1 &optional float-2) */
static CL_Obj bi_float_sign(CL_Obj *args, int n)
{
    double sign_val, mag;
    int is_double;
    check_float(args[0], "FLOAT-SIGN");
    sign_val = cl_to_double(args[0]);
    if (n >= 2) {
        check_float(args[1], "FLOAT-SIGN");
        mag = cl_to_double(args[1]);
        if (mag < 0.0) mag = -mag;
        is_double = CL_DOUBLE_FLOAT_P(args[1]);
    } else {
        mag = 1.0;
        is_double = CL_DOUBLE_FLOAT_P(args[0]);
    }
    if (sign_val < 0.0) mag = -mag;
    /* Handle negative zero */
    else if (sign_val == 0.0 && (1.0 / sign_val) < 0.0) mag = -mag;
    if (is_double)
        return cl_make_double_float(mag);
    return cl_make_single_float((float)mag);
}

/* (decode-float float) → significand, exponent, sign  (3 values) */
static CL_Obj bi_decode_float(CL_Obj *args, int n)
{
    double val, sig;
    int exp;
    CL_Obj sig_obj, exp_obj, sign_obj;
    int is_double;
    CL_UNUSED(n);
    check_float(args[0], "DECODE-FLOAT");
    is_double = CL_DOUBLE_FLOAT_P(args[0]);
    val = cl_to_double(args[0]);
    if (val == 0.0) {
        sig_obj = is_double ? cl_make_double_float(0.0) : cl_make_single_float(0.0f);
        exp_obj = CL_MAKE_FIXNUM(0);
        /* Sign preserves -0.0 */
        if ((1.0 / val) < 0.0)
            sign_obj = is_double ? cl_make_double_float(-1.0) : cl_make_single_float(-1.0f);
        else
            sign_obj = is_double ? cl_make_double_float(1.0) : cl_make_single_float(1.0f);
    } else {
        double abs_val = val < 0.0 ? -val : val;
        sig = frexp(abs_val, &exp);
        /* frexp returns [0.5, 1.0) which matches CL spec for decode-float */
        sig_obj = is_double ? cl_make_double_float(sig) : cl_make_single_float((float)sig);
        exp_obj = CL_MAKE_FIXNUM(exp);
        sign_obj = is_double
            ? cl_make_double_float(val < 0.0 ? -1.0 : 1.0)
            : cl_make_single_float(val < 0.0 ? -1.0f : 1.0f);
    }
    cl_mv_count = 3;
    cl_mv_values[0] = sig_obj;
    cl_mv_values[1] = exp_obj;
    cl_mv_values[2] = sign_obj;
    return sig_obj;
}

/* Helper: convert a positive double integer value to CL_Obj (fixnum or bignum).
   Value must be a non-negative whole number ≤ 2^53. */
static CL_Obj double_int_to_obj(double val)
{
    if (val <= (double)CL_FIXNUM_MAX)
        return CL_MAKE_FIXNUM((int32_t)val);
    /* Split into hi*65536 + lo using 16-bit chunks (like bignum limbs) */
    {
        uint32_t lo, mid, hi;
        CL_Obj result, term;
        lo = (uint32_t)fmod(val, 65536.0);
        val = (val - lo) / 65536.0;
        mid = (uint32_t)fmod(val, 65536.0);
        val = (val - mid) / 65536.0;
        hi = (uint32_t)val;
        /* result = hi * 65536^2 + mid * 65536 + lo */
        result = CL_MAKE_FIXNUM((int32_t)lo);
        CL_GC_PROTECT(result);
        term = cl_arith_mul(CL_MAKE_FIXNUM((int32_t)mid), CL_MAKE_FIXNUM(65536));
        result = cl_arith_add(result, term);
        if (hi > 0) {
            /* hi * 65536 * 65536 = hi * (65536^2) */
            term = cl_arith_mul(CL_MAKE_FIXNUM((int32_t)hi), CL_MAKE_FIXNUM(65536));
            term = cl_arith_mul(term, CL_MAKE_FIXNUM(65536));
            result = cl_arith_add(result, term);
        }
        CL_GC_UNPROTECT(1);
        return result;
    }
}

/* (integer-decode-float float) → significand(int), exponent(int), sign(int) */
static CL_Obj bi_integer_decode_float(CL_Obj *args, int n)
{
    double val;
    int is_double;
    CL_Obj sig_obj, exp_obj, sign_obj;
    CL_UNUSED(n);
    check_float(args[0], "INTEGER-DECODE-FLOAT");
    is_double = CL_DOUBLE_FLOAT_P(args[0]);
    val = cl_to_double(args[0]);
    if (val == 0.0) {
        sig_obj = CL_MAKE_FIXNUM(0);
        exp_obj = CL_MAKE_FIXNUM(0);
        sign_obj = CL_MAKE_FIXNUM((1.0 / val) < 0.0 ? -1 : 1);
    } else {
        double abs_val = val < 0.0 ? -val : val;
        int exp;
        double sig = frexp(abs_val, &exp);
        if (is_double) {
            /* Scale significand to integer: sig is in [0.5, 1.0), multiply by 2^53 */
            double int_sig = ldexp(sig, 53);
            int32_t adj_exp = exp - 53;
            sig_obj = double_int_to_obj(int_sig);
            exp_obj = CL_MAKE_FIXNUM(adj_exp);
        } else {
            /* Scale significand to integer: sig is in [0.5, 1.0), multiply by 2^24 */
            double int_sig = ldexp(sig, 24);
            int32_t adj_exp = exp - 24;
            int32_t isig = (int32_t)int_sig;
            sig_obj = CL_MAKE_FIXNUM(isig);
            exp_obj = CL_MAKE_FIXNUM(adj_exp);
        }
        sign_obj = CL_MAKE_FIXNUM(val < 0.0 ? -1 : 1);
    }
    cl_mv_count = 3;
    cl_mv_values[0] = sig_obj;
    cl_mv_values[1] = exp_obj;
    cl_mv_values[2] = sign_obj;
    return sig_obj;
}

/* (scale-float float integer) */
static CL_Obj bi_scale_float(CL_Obj *args, int n)
{
    double val, result;
    int32_t scale;
    CL_UNUSED(n);
    check_float(args[0], "SCALE-FLOAT");
    check_integer(args[1], "SCALE-FLOAT");
    val = cl_to_double(args[0]);
    scale = CL_FIXNUM_P(args[1]) ? CL_FIXNUM_VAL(args[1])
        : (int32_t)cl_to_double(args[1]);
    result = ldexp(val, scale);
    if (CL_DOUBLE_FLOAT_P(args[0]))
        return cl_make_double_float(result);
    return cl_make_single_float((float)result);
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
    defun("FLOATP", bi_floatp, 1, 1);
    defun("REALP", bi_realp, 1, 1);
    defun("RATIONALP", bi_rationalp, 1, 1);

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

    /* Float-specific functions */
    defun("FLOAT", bi_float, 1, 2);
    defun("FLOAT-DIGITS", bi_float_digits, 1, 1);
    defun("FLOAT-RADIX", bi_float_radix, 1, 1);
    defun("FLOAT-SIGN", bi_float_sign, 1, 2);
    defun("DECODE-FLOAT", bi_decode_float, 1, 1);
    defun("INTEGER-DECODE-FLOAT", bi_integer_decode_float, 1, 1);
    defun("SCALE-FLOAT", bi_scale_float, 2, 2);

    /* Initialize bignum subsystem */
    cl_bignum_init();
}
