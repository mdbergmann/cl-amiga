#include "builtins.h"
#include "bignum.h"
#include "ratio.h"
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

static void check_real(CL_Obj obj, const char *op)
{
    if (!CL_REALP(obj))
        cl_error(CL_ERR_TYPE, "%s: argument is not a real number", op);
}

/* --- Rounding helpers --- */

/* Forward declaration (defined later in this file for integer-decode-float) */
static CL_Obj double_int_to_obj(double val);

/* Convert a whole-number double (positive or negative) to fixnum or bignum.
   For magnitudes beyond ~2^48 the old multi-limb chunking lost high bits
   (FLOOR / TRUNCATE of MOST-POSITIVE-SINGLE-FLOAT returned int32 garbage),
   so fall back to the exact IEEE-bit-pattern rational which is bignum-safe
   across the entire double-float range. The caller guarantees val is whole,
   so the result is an integer. */
static CL_Obj double_to_integer(double val)
{
    if (val >= (double)CL_FIXNUM_MIN && val <= (double)CL_FIXNUM_MAX)
        return CL_MAKE_FIXNUM((int32_t)val);
    return cl_float_to_exact_rational(cl_make_double_float(val));
}

/* Round-to-nearest-even (banker's rounding) */
static double cl_round_even(double val)
{
    double down = floor(val);
    double frac = val - down;
    if (frac > 0.5) return down + 1.0;
    if (frac < 0.5) return down;
    /* Exactly 0.5: round to nearest even */
    if (fmod(down, 2.0) == 0.0) return down;
    return down + 1.0;
}

enum { ROUND_FLOOR, ROUND_CEILING, ROUND_TRUNCATE, ROUND_ROUND };

/* Apply rounding mode to a double */
static double apply_round(double val, int mode)
{
    switch (mode) {
    case ROUND_FLOOR:    return floor(val);
    case ROUND_CEILING:  return ceil(val);
    case ROUND_TRUNCATE: return (val >= 0.0) ? floor(val) : ceil(val);
    case ROUND_ROUND:    return cl_round_even(val);
    }
    return val;
}

/* Core rounding: floor/ceiling/truncate/round with 1 or 2 args.
   If float_quotient is true, quotient is returned as float (f-variants). */
static CL_Obj do_rounding(CL_Obj *args, int n, int mode,
                           const char *name, int float_quotient)
{
    CL_Obj num = args[0];
    check_number(num, name);

    if (n == 1) {
        /* Single argument */
        if (CL_INTEGER_P(num)) {
            CL_Obj q, r;
            if (float_quotient) {
                q = cl_make_single_float((float)cl_to_double(num));
                r = CL_MAKE_FIXNUM(0);
            } else {
                q = num;
                r = CL_MAKE_FIXNUM(0);
            }
            cl_mv_count = 2;
            cl_mv_values[0] = q;
            cl_mv_values[1] = r;
            return q;
        }
        /* Ratio: (floor p/q) = (floor p q), remainder is ratio */
        if (CL_RATIO_P(num)) {
            CL_Obj pair[2];
            pair[0] = cl_numerator(num);
            pair[1] = cl_denominator(num);
            return do_rounding(pair, 2, mode, name, float_quotient);
        }
        /* Float argument */
        {
            double val = cl_to_double(num);
            double dq = apply_round(val, mode);
            double dr = val - dq;
            int is_dbl = CL_DOUBLE_FLOAT_P(num);
            CL_Obj qobj, robj;
            if (float_quotient)
                qobj = is_dbl ? cl_make_double_float(dq)
                              : cl_make_single_float((float)dq);
            else
                qobj = double_to_integer(dq);
            robj = is_dbl ? cl_make_double_float(dr)
                          : cl_make_single_float((float)dr);
            cl_mv_count = 2;
            cl_mv_values[0] = qobj;
            cl_mv_values[1] = robj;
            return qobj;
        }
    }

    /* Two arguments */
    check_number(args[1], name);

    if (CL_RATIONAL_P(num) && CL_RATIONAL_P(args[1])) {
        /* Rational path: exact arithmetic.
         * For ratios, cross-multiply to get integer division:
         * (floor a_n/a_d  b_n/b_d) = (floor a_n*b_d  a_d*b_n)
         * then remainder = num - q * args[1] */
        CL_Obj int_a, int_b, q, r;
        int_a = cl_arith_mul(cl_numerator(num), cl_denominator(args[1]));
        CL_GC_PROTECT(int_a);
        int_b = cl_arith_mul(cl_denominator(num), cl_numerator(args[1]));
        CL_GC_UNPROTECT(1);

        q = cl_arith_truncate(int_a, int_b);
        r = cl_arith_sub(num, cl_arith_mul(q, args[1]));

        /* Adjust based on rounding mode */
        if (mode != ROUND_TRUNCATE && !cl_arith_zerop(r)) {
            if (mode == ROUND_FLOOR) {
                if (cl_arith_minusp(r) != cl_arith_minusp(args[1])) {
                    q = cl_arith_sub(q, CL_MAKE_FIXNUM(1));
                    r = cl_arith_add(r, args[1]);
                }
            } else if (mode == ROUND_CEILING) {
                if (cl_arith_minusp(r) == cl_arith_minusp(args[1])) {
                    q = cl_arith_add(q, CL_MAKE_FIXNUM(1));
                    r = cl_arith_sub(r, args[1]);
                }
            } else { /* ROUND_ROUND */
                CL_Obj abs_2r = cl_arith_abs(cl_arith_add(r, r));
                CL_Obj abs_b = cl_arith_abs(args[1]);
                int cmp = cl_arith_compare(abs_2r, abs_b);
                if (cmp > 0 || (cmp == 0 && !cl_arith_evenp(q))) {
                    if (cl_arith_minusp(num) == cl_arith_minusp(args[1])) {
                        q = cl_arith_add(q, CL_MAKE_FIXNUM(1));
                        r = cl_arith_sub(r, args[1]);
                    } else {
                        q = cl_arith_sub(q, CL_MAKE_FIXNUM(1));
                        r = cl_arith_add(r, args[1]);
                    }
                }
            }
        }

        if (float_quotient)
            q = cl_make_single_float((float)cl_to_double(q));

        cl_mv_count = 2;
        cl_mv_values[0] = q;
        cl_mv_values[1] = r;
        return q;
    }

    /* Float path */
    {
        double a = cl_to_double(num);
        double b = cl_to_double(args[1]);
        double dq, dr;
        int is_dbl = CL_DOUBLE_FLOAT_P(num) || CL_DOUBLE_FLOAT_P(args[1]);
        CL_Obj qobj, robj;
        if (b == 0.0) cl_error(CL_ERR_DIVZERO, "Division by zero");
        dq = apply_round(a / b, mode);
        dr = a - dq * b;
        if (float_quotient)
            qobj = is_dbl ? cl_make_double_float(dq)
                          : cl_make_single_float((float)dq);
        else
            qobj = double_to_integer(dq);
        robj = is_dbl ? cl_make_double_float(dr)
                      : cl_make_single_float((float)dr);
        cl_mv_count = 2;
        cl_mv_values[0] = qobj;
        cl_mv_values[1] = robj;
        return qobj;
    }
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
    for (i = 0; i < n; i++)
        check_number(args[i], "/");
    if (n == 1)
        return cl_arith_div(CL_MAKE_FIXNUM(1), args[0]);
    result = args[0];
    CL_GC_PROTECT(result);
    for (i = 1; i < n; i++)
        result = cl_arith_div(result, args[i]);
    CL_GC_UNPROTECT(1);
    return result;
}

/* --- Rounding builtins --- */

static CL_Obj bi_floor(CL_Obj *args, int n)
{ return do_rounding(args, n, ROUND_FLOOR, "FLOOR", 0); }

static CL_Obj bi_ceiling(CL_Obj *args, int n)
{ return do_rounding(args, n, ROUND_CEILING, "CEILING", 0); }

static CL_Obj bi_truncate(CL_Obj *args, int n)
{ return do_rounding(args, n, ROUND_TRUNCATE, "TRUNCATE", 0); }

static CL_Obj bi_round(CL_Obj *args, int n)
{ return do_rounding(args, n, ROUND_ROUND, "ROUND", 0); }

static CL_Obj bi_ffloor(CL_Obj *args, int n)
{ return do_rounding(args, n, ROUND_FLOOR, "FFLOOR", 1); }

static CL_Obj bi_fceiling(CL_Obj *args, int n)
{ return do_rounding(args, n, ROUND_CEILING, "FCEILING", 1); }

static CL_Obj bi_ftruncate(CL_Obj *args, int n)
{ return do_rounding(args, n, ROUND_TRUNCATE, "FTRUNCATE", 1); }

static CL_Obj bi_fround(CL_Obj *args, int n)
{ return do_rounding(args, n, ROUND_ROUND, "FROUND", 1); }

/* (rem number divisor) — truncation remainder (sign follows dividend) */
static CL_Obj bi_rem(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "REM");
    check_number(args[1], "REM");
    if (CL_FLOATP(args[0]) || CL_FLOATP(args[1])) {
        double a = cl_to_double(args[0]);
        double b = cl_to_double(args[1]);
        double r;
        int is_dbl = CL_DOUBLE_FLOAT_P(args[0]) || CL_DOUBLE_FLOAT_P(args[1]);
        if (b == 0.0) cl_error(CL_ERR_DIVZERO, "Division by zero");
        r = fmod(a, b);
        return is_dbl ? cl_make_double_float(r) : cl_make_single_float((float)r);
    }
    /* Ratio case: rem(a,b) = a - truncate(a,b) * b */
    if (CL_RATIO_P(args[0]) || CL_RATIO_P(args[1])) {
        /* Compute a/b as ratio, then truncate to integer */
        CL_Obj quot = cl_ratio_div(args[0], args[1]);
        CL_Obj a = args[0], b = args[1];
        CL_Obj trunc_q;
        if (CL_INTEGER_P(quot)) {
            trunc_q = quot;
        } else {
            /* Truncate ratio to integer: truncate(num/den) */
            trunc_q = cl_arith_truncate(cl_numerator(quot), cl_denominator(quot));
        }
        CL_GC_PROTECT(a); CL_GC_PROTECT(b); CL_GC_PROTECT(trunc_q);
        {
            CL_Obj result = cl_arith_sub(a, cl_arith_mul(trunc_q, b));
            CL_GC_UNPROTECT(3);
            return result;
        }
    }
    /* Integer case */
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

/* (mod number divisor) — floor remainder (sign follows divisor) */
static CL_Obj bi_mod(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "MOD");
    check_number(args[1], "MOD");
    if (CL_FLOATP(args[0]) || CL_FLOATP(args[1])) {
        double a = cl_to_double(args[0]);
        double b = cl_to_double(args[1]);
        double r;
        int is_dbl = CL_DOUBLE_FLOAT_P(args[0]) || CL_DOUBLE_FLOAT_P(args[1]);
        if (b == 0.0) cl_error(CL_ERR_DIVZERO, "Division by zero");
        r = fmod(a, b);
        /* CL mod: result has same sign as divisor */
        if (r != 0.0 && ((r < 0.0) != (b < 0.0)))
            r += b;
        return is_dbl ? cl_make_double_float(r) : cl_make_single_float((float)r);
    }
    /* Ratio case: mod(a,b) = a - floor(a,b) * b */
    if (CL_RATIO_P(args[0]) || CL_RATIO_P(args[1])) {
        CL_Obj quot = cl_ratio_div(args[0], args[1]);
        CL_Obj a = args[0], b = args[1];
        CL_Obj floor_q;
        if (CL_INTEGER_P(quot)) {
            floor_q = quot;
        } else {
            /* Floor ratio: floor(num/den) */
            CL_Obj num = cl_numerator(quot);
            CL_Obj den = cl_denominator(quot);
            CL_Obj trunc_q = cl_arith_truncate(num, den);
            /* Adjust: if result is negative and not exact, subtract 1 */
            CL_GC_PROTECT(trunc_q); CL_GC_PROTECT(num); CL_GC_PROTECT(den);
            {
                CL_Obj check = cl_arith_mul(trunc_q, den);
                CL_GC_UNPROTECT(3);
                CL_GC_PROTECT(trunc_q); CL_GC_PROTECT(check);
                if (cl_arith_compare(check, num) != 0 && cl_arith_minusp(quot)) {
                    CL_GC_UNPROTECT(2);
                    floor_q = cl_arith_sub(trunc_q, CL_MAKE_FIXNUM(1));
                } else {
                    CL_GC_UNPROTECT(2);
                    floor_q = trunc_q;
                }
            }
        }
        CL_GC_PROTECT(a); CL_GC_PROTECT(b); CL_GC_PROTECT(floor_q);
        {
            CL_Obj result = cl_arith_sub(a, cl_arith_mul(floor_q, b));
            CL_GC_UNPROTECT(3);
            return result;
        }
    }
    /* Integer case */
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
        if (!cl_numeric_equal(args[0], args[i]))
            return CL_NIL;
    }
    return SYM_T;
}

static CL_Obj bi_numneq(CL_Obj *args, int n)
{
    int i, j;
    for (i = 0; i < n; i++) {
        check_number(args[i], "/=");
        for (j = i + 1; j < n; j++) {
            check_number(args[j], "/=");
            if (cl_numeric_equal(args[i], args[j]))
                return CL_NIL;
        }
    }
    return SYM_T;
}

/* The ordered comparators (<, <=, >, >=) require real arguments per
   CLHS 12.1.4.1 — complex is not in the domain. */
static CL_Obj bi_lt(CL_Obj *args, int n)
{
    int i;
    for (i = 0; i < n - 1; i++) {
        check_real(args[i], "<");
        check_real(args[i+1], "<");
        if (cl_arith_compare(args[i], args[i+1]) >= 0)
            return CL_NIL;
    }
    return SYM_T;
}

static CL_Obj bi_gt(CL_Obj *args, int n)
{
    int i;
    for (i = 0; i < n - 1; i++) {
        check_real(args[i], ">");
        check_real(args[i+1], ">");
        if (cl_arith_compare(args[i], args[i+1]) <= 0)
            return CL_NIL;
    }
    return SYM_T;
}

static CL_Obj bi_le(CL_Obj *args, int n)
{
    int i;
    for (i = 0; i < n - 1; i++) {
        check_real(args[i], "<=");
        check_real(args[i+1], "<=");
        if (cl_arith_compare(args[i], args[i+1]) > 0)
            return CL_NIL;
    }
    return SYM_T;
}

static CL_Obj bi_ge(CL_Obj *args, int n)
{
    int i;
    for (i = 0; i < n - 1; i++) {
        check_real(args[i], ">=");
        check_real(args[i+1], ">=");
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

/* --- Bit manipulation --- */

static CL_Obj bi_logcount(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_integer(args[0], "LOGCOUNT");
    return CL_MAKE_FIXNUM(cl_arith_logcount(args[0]));
}

static CL_Obj bi_logbitp(CL_Obj *args, int n)
{
    int32_t index;
    CL_UNUSED(n);
    check_integer(args[0], "LOGBITP");
    check_integer(args[1], "LOGBITP");
    if (!CL_FIXNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "LOGBITP: index too large");
    index = CL_FIXNUM_VAL(args[0]);
    if (index < 0)
        cl_error(CL_ERR_TYPE, "LOGBITP: index must be non-negative");
    return cl_arith_logbitp(index, args[1]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_logtest(CL_Obj *args, int n)
{
    CL_Obj result;
    CL_UNUSED(n);
    check_integer(args[0], "LOGTEST");
    check_integer(args[1], "LOGTEST");
    result = cl_arith_logand(args[0], args[1]);
    return cl_arith_zerop(result) ? CL_NIL : SYM_T;
}

/* byte spec is (size . position) */
static CL_Obj bi_byte(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_integer(args[0], "BYTE");
    check_integer(args[1], "BYTE");
    return cl_cons(args[0], args[1]);
}

static CL_Obj bi_byte_size(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_CONS_P(args[0]))
        cl_error(CL_ERR_TYPE, "BYTE-SIZE: not a byte specifier");
    return cl_car(args[0]);
}

static CL_Obj bi_byte_position(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_CONS_P(args[0]))
        cl_error(CL_ERR_TYPE, "BYTE-POSITION: not a byte specifier");
    return cl_cdr(args[0]);
}

/* (ldb bytespec integer) — extract bit field */
static CL_Obj bi_ldb(CL_Obj *args, int n)
{
    CL_Obj size, pos, shifted, mask;
    CL_UNUSED(n);
    if (!CL_CONS_P(args[0]))
        cl_error(CL_ERR_TYPE, "LDB: not a byte specifier");
    check_integer(args[1], "LDB");
    size = cl_car(args[0]);
    pos = cl_cdr(args[0]);
    /* shifted = ash(integer, -position) */
    shifted = cl_arith_ash(args[1], cl_arith_negate(pos));
    /* mask = (1 << size) - 1 = ash(1, size) - 1 */
    CL_GC_PROTECT(shifted);
    mask = cl_arith_sub(cl_arith_ash(CL_MAKE_FIXNUM(1), size), CL_MAKE_FIXNUM(1));
    CL_GC_UNPROTECT(1);
    return cl_arith_logand(shifted, mask);
}

/* (dpb newbyte bytespec integer) — deposit bit field */
static CL_Obj bi_dpb(CL_Obj *args, int n)
{
    CL_Obj size, pos, mask, shifted_mask, cleared, new_bits;
    CL_UNUSED(n);
    check_integer(args[0], "DPB");
    if (!CL_CONS_P(args[1]))
        cl_error(CL_ERR_TYPE, "DPB: not a byte specifier");
    check_integer(args[2], "DPB");
    size = cl_car(args[1]);
    pos = cl_cdr(args[1]);
    /* mask = (1 << size) - 1 */
    mask = cl_arith_sub(cl_arith_ash(CL_MAKE_FIXNUM(1), size), CL_MAKE_FIXNUM(1));
    CL_GC_PROTECT(mask);
    /* shifted_mask = ash(mask, position) */
    shifted_mask = cl_arith_ash(mask, pos);
    CL_GC_UNPROTECT(1);
    CL_GC_PROTECT(shifted_mask);
    /* Clear the field in integer */
    cleared = cl_arith_logand(args[2], cl_arith_lognot(shifted_mask));
    CL_GC_UNPROTECT(1);
    CL_GC_PROTECT(cleared);
    /* Shift new value into position, masked */
    new_bits = cl_arith_ash(cl_arith_logand(args[0], mask), pos);
    CL_GC_UNPROTECT(1);
    return cl_arith_logior(cleared, new_bits);
}

/* (ldb-test bytespec integer) — test if field is nonzero */
static CL_Obj bi_ldb_test(CL_Obj *args, int n)
{
    CL_Obj val = bi_ldb(args, n);
    return cl_arith_zerop(val) ? CL_NIL : SYM_T;
}

/* (mask-field bytespec integer) — like ldb but preserves position */
static CL_Obj bi_mask_field(CL_Obj *args, int n)
{
    CL_Obj size, pos, mask, shifted_mask;
    CL_UNUSED(n);
    if (!CL_CONS_P(args[0]))
        cl_error(CL_ERR_TYPE, "MASK-FIELD: not a byte specifier");
    check_integer(args[1], "MASK-FIELD");
    size = cl_car(args[0]);
    pos = cl_cdr(args[0]);
    mask = cl_arith_sub(cl_arith_ash(CL_MAKE_FIXNUM(1), size), CL_MAKE_FIXNUM(1));
    CL_GC_PROTECT(mask);
    shifted_mask = cl_arith_ash(mask, pos);
    CL_GC_UNPROTECT(1);
    return cl_arith_logand(args[1], shifted_mask);
}

/* (deposit-field newbyte bytespec integer) — like dpb but from same position */
static CL_Obj bi_deposit_field(CL_Obj *args, int n)
{
    CL_Obj size, pos, mask, shifted_mask, cleared, new_bits;
    CL_UNUSED(n);
    check_integer(args[0], "DEPOSIT-FIELD");
    if (!CL_CONS_P(args[1]))
        cl_error(CL_ERR_TYPE, "DEPOSIT-FIELD: not a byte specifier");
    check_integer(args[2], "DEPOSIT-FIELD");
    size = cl_car(args[1]);
    pos = cl_cdr(args[1]);
    mask = cl_arith_sub(cl_arith_ash(CL_MAKE_FIXNUM(1), size), CL_MAKE_FIXNUM(1));
    CL_GC_PROTECT(mask);
    shifted_mask = cl_arith_ash(mask, pos);
    CL_GC_UNPROTECT(1);
    CL_GC_PROTECT(shifted_mask);
    /* Clear the field in integer */
    cleared = cl_arith_logand(args[2], cl_arith_lognot(shifted_mask));
    CL_GC_UNPROTECT(1);
    CL_GC_PROTECT(cleared);
    /* Extract field bits from newbyte at position (already in position) */
    new_bits = cl_arith_logand(args[0], shifted_mask);
    CL_GC_UNPROTECT(1);
    return cl_arith_logior(cleared, new_bits);
}

/* (boole op integer-1 integer-2) — boolean operation */
static CL_Obj bi_boole(CL_Obj *args, int n)
{
    int32_t op;
    CL_Obj a, b;
    CL_UNUSED(n);
    check_integer(args[0], "BOOLE");
    check_integer(args[1], "BOOLE");
    check_integer(args[2], "BOOLE");
    if (!CL_FIXNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "BOOLE: operation must be a fixnum");
    op = CL_FIXNUM_VAL(args[0]);
    a = args[1];
    b = args[2];
    switch (op) {
    case 0:  return CL_MAKE_FIXNUM(0);                        /* CLR */
    case 1:  return CL_MAKE_FIXNUM(-1);                       /* SET */
    case 2:  return a;                                          /* 1 */
    case 3:  return b;                                          /* 2 */
    case 4:  return cl_arith_lognot(a);                        /* C1 */
    case 5:  return cl_arith_lognot(b);                        /* C2 */
    case 6:  return cl_arith_logand(a, b);                     /* AND */
    case 7:  return cl_arith_logior(a, b);                     /* IOR */
    case 8:  return cl_arith_logxor(a, b);                     /* XOR */
    case 9:  return cl_arith_lognot(cl_arith_logxor(a, b));   /* EQV */
    case 10: return cl_arith_lognot(cl_arith_logand(a, b));   /* NAND */
    case 11: return cl_arith_lognot(cl_arith_logior(a, b));   /* NOR */
    case 12: return cl_arith_logand(cl_arith_lognot(a), b);   /* ANDC1 */
    case 13: return cl_arith_logand(a, cl_arith_lognot(b));   /* ANDC2 */
    case 14: return cl_arith_logior(cl_arith_lognot(a), b);   /* ORC1 */
    case 15: return cl_arith_logior(a, cl_arith_lognot(b));   /* ORC2 */
    default:
        cl_error(CL_ERR_ARGS, "BOOLE: invalid operation %d", op);
        return CL_NIL;
    }
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
    return CL_RATIONAL_P(args[0]) ? SYM_T : CL_NIL;
}

/* --- Complex number functions --- */

static CL_Obj bi_complex(CL_Obj *args, int n)
{
    CL_Obj real = args[0], imag;
    if (!CL_REALP(real))
        cl_error(CL_ERR_TYPE, "COMPLEX: not a real number: %s", cl_type_name(real));
    imag = (n >= 2) ? args[1] : CL_MAKE_FIXNUM(0);
    if (!CL_REALP(imag))
        cl_error(CL_ERR_TYPE, "COMPLEX: not a real number: %s", cl_type_name(imag));
    /* If imagpart is zero, return just the real part (CL spec) */
    if (CL_FIXNUM_P(imag) && CL_FIXNUM_VAL(imag) == 0)
        return real;
    return cl_make_complex(real, imag);
}

static CL_Obj bi_complexp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_COMPLEX_P(args[0]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_realpart(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (CL_COMPLEX_P(args[0]))
        return ((CL_Complex *)CL_OBJ_TO_PTR(args[0]))->realpart;
    if (CL_REALP(args[0]))
        return args[0];
    cl_error(CL_ERR_TYPE, "REALPART: not a number: %s", cl_type_name(args[0]));
    return CL_NIL;
}

static CL_Obj bi_imagpart(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (CL_COMPLEX_P(args[0]))
        return ((CL_Complex *)CL_OBJ_TO_PTR(args[0]))->imagpart;
    if (CL_REALP(args[0]))
        return CL_MAKE_FIXNUM(0);
    cl_error(CL_ERR_TYPE, "IMAGPART: not a number: %s", cl_type_name(args[0]));
    return CL_NIL;
}

static CL_Obj bi_conjugate(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (CL_COMPLEX_P(args[0])) {
        CL_Complex *cx = (CL_Complex *)CL_OBJ_TO_PTR(args[0]);
        /* Negate imaginary part */
        CL_Obj neg_imag = cl_arith_negate(cx->imagpart);
        return cl_make_complex(cx->realpart, neg_imag);
    }
    if (CL_REALP(args[0]))
        return args[0];
    cl_error(CL_ERR_TYPE, "CONJUGATE: not a number: %s", cl_type_name(args[0]));
    return CL_NIL;
}

/* --- Ratio accessors --- */

static CL_Obj bi_numerator(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_RATIONAL_P(args[0]))
        cl_error(CL_ERR_TYPE, "NUMERATOR: not a rational: %s", cl_type_name(args[0]));
    return cl_numerator(args[0]);
}

static CL_Obj bi_denominator(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_RATIONAL_P(args[0]))
        cl_error(CL_ERR_TYPE, "DENOMINATOR: not a rational: %s", cl_type_name(args[0]));
    return cl_denominator(args[0]);
}

/* (rational x) — exact rational equal in value to x.
   Per HyperSpec 12.2: when x is a float, the result is an exact
   rational representing the float's IEEE bit pattern (NOT a "simple"
   continued-fraction approximation — that's RATIONALIZE's job). */
static CL_Obj bi_rational(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (CL_RATIONAL_P(args[0]))
        return args[0];
    if (CL_FLOATP(args[0]))
        return cl_float_to_exact_rational(args[0]);
    cl_error(CL_ERR_TYPE, "RATIONAL: not a real number: %s", cl_type_name(args[0]));
    return CL_NIL;
}

/* (rationalize x) — same as rational for now */
static CL_Obj bi_rationalize(CL_Obj *args, int n)
{
    return bi_rational(args, n);
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
    defun("FLOOR", bi_floor, 1, 2);
    defun("CEILING", bi_ceiling, 1, 2);
    defun("TRUNCATE", bi_truncate, 1, 2);
    defun("ROUND", bi_round, 1, 2);
    defun("FFLOOR", bi_ffloor, 1, 2);
    defun("FCEILING", bi_fceiling, 1, 2);
    defun("FTRUNCATE", bi_ftruncate, 1, 2);
    defun("FROUND", bi_fround, 1, 2);
    defun("REM", bi_rem, 2, 2);
    defun("MOD", bi_mod, 2, 2);
    defun("1+", bi_1plus, 1, 1);
    defun("1-", bi_1minus, 1, 1);

    /* Comparison */
    defun("=", bi_numeq, 1, -1);
    defun("/=", bi_numneq, 1, -1);
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
    defun("LOGCOUNT", bi_logcount, 1, 1);
    defun("LOGBITP", bi_logbitp, 2, 2);
    defun("LOGTEST", bi_logtest, 2, 2);

    /* Byte operations */
    defun("BYTE", bi_byte, 2, 2);
    defun("BYTE-SIZE", bi_byte_size, 1, 1);
    defun("BYTE-POSITION", bi_byte_position, 1, 1);
    defun("LDB", bi_ldb, 2, 2);
    defun("DPB", bi_dpb, 3, 3);
    defun("LDB-TEST", bi_ldb_test, 2, 2);
    defun("MASK-FIELD", bi_mask_field, 2, 2);
    defun("DEPOSIT-FIELD", bi_deposit_field, 3, 3);
    defun("BOOLE", bi_boole, 3, 3);

    /* Complex number functions */
    defun("COMPLEX", bi_complex, 1, 2);
    defun("COMPLEXP", bi_complexp, 1, 1);
    defun("REALPART", bi_realpart, 1, 1);
    defun("IMAGPART", bi_imagpart, 1, 1);
    defun("CONJUGATE", bi_conjugate, 1, 1);

    /* Ratio accessors */
    defun("NUMERATOR", bi_numerator, 1, 1);
    defun("DENOMINATOR", bi_denominator, 1, 1);
    defun("RATIONAL", bi_rational, 1, 1);
    defun("RATIONALIZE", bi_rationalize, 1, 1);

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
