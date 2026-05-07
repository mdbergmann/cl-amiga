/*
 * float_math.c — Math functions: sqrt, exp, log, expt, trig (float-aware)
 *
 * These functions accept any numeric argument and return floats.
 * Type contagion: double-float input → double-float result.
 */

#include "builtins.h"
#include "bignum.h"
#include "float.h"
#include "ratio.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
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

/* Return a float result with proper type contagion.
 * If any input was double-float, result is double-float. */
static CL_Obj make_result(double val, int is_double)
{
    if (is_double)
        return cl_make_double_float(val);
    return cl_make_single_float((float)val);
}

/* (sqrt number) → number
 * Per CLHS: real >= 0 → real (float kind preserved; integer-perfect-
 *   square stays exact integer);
 *   real < 0 → complex (#C(0 sqrt(-x))) of matching float type;
 *   complex → complex via:
 *     sqrt(a+bi) = sqrt((|c|+a)/2) + sign(b)*i*sqrt((|c|-a)/2). */
static CL_Obj bi_sqrt(CL_Obj *args, int n)
{
    CL_Obj x = args[0];
    CL_UNUSED(n);
    check_number(x, "SQRT");

    if (CL_COMPLEX_P(x)) {
        CL_Complex *cx = (CL_Complex *)CL_OBJ_TO_PTR(x);
        double a = cl_to_double(cx->realpart);
        double b = cl_to_double(cx->imagpart);
        double mag = sqrt(a * a + b * b);
        double re = sqrt((mag + a) * 0.5);
        double im = sqrt((mag - a) * 0.5);
        int want_double = CL_DOUBLE_FLOAT_P(cx->realpart) ||
                          CL_DOUBLE_FLOAT_P(cx->imagpart);
        if (b < 0.0) im = -im;
        if (want_double)
            return cl_make_complex(cl_make_double_float(re),
                                   cl_make_double_float(im));
        return cl_make_complex(cl_make_single_float((float)re),
                               cl_make_single_float((float)im));
    }

    /* Integer perfect-square fast path: (sqrt 4) → 2 (integer), (sqrt 9) → 3. */
    if (CL_INTEGER_P(x) && !cl_arith_minusp(x)) {
        CL_Obj isq, isq_sq;
        CL_GC_PROTECT(x);
        isq = cl_arith_isqrt(x);
        CL_GC_PROTECT(isq);
        isq_sq = cl_arith_mul(isq, isq);
        if (cl_arith_compare(isq_sq, x) == 0) {
            CL_GC_UNPROTECT(2);
            return isq;
        }
        CL_GC_UNPROTECT(2);
    }

    {
        double val = cl_to_double(x);
        int is_dbl = CL_DOUBLE_FLOAT_P(x);
        if (val < 0.0) {
            /* Negative real: result is pure-imaginary complex. */
            double im = sqrt(-val);
            CL_Obj zero = is_dbl ? cl_make_double_float(0.0)
                                 : cl_make_single_float(0.0f);
            CL_Obj imag = is_dbl ? cl_make_double_float(im)
                                 : cl_make_single_float((float)im);
            CL_GC_PROTECT(zero);
            CL_GC_PROTECT(imag);
            {
                CL_Obj r = cl_make_complex(zero, imag);
                CL_GC_UNPROTECT(2);
                return r;
            }
        }
        return make_result(sqrt(val), is_dbl);
    }
}

/* (exp number) → float
 * Returns e^number. */
static CL_Obj bi_exp(CL_Obj *args, int n)
{
    double val, result;
    CL_UNUSED(n);
    check_number(args[0], "EXP");
    val = cl_to_double(args[0]);
    result = exp(val);
    return make_result(result, CL_DOUBLE_FLOAT_P(args[0]));
}

/* (log number &optional base) → float
 * One arg: natural logarithm.
 * Two args: logarithm base <base> = ln(number)/ln(base).
 * Signals error for non-positive arguments (no complex numbers). */
static CL_Obj bi_log(CL_Obj *args, int n)
{
    double val, result;
    int is_double;

    check_number(args[0], "LOG");
    val = cl_to_double(args[0]);
    if (val <= 0.0)
        cl_error(CL_ERR_TYPE, "LOG: argument must be positive");
    is_double = CL_DOUBLE_FLOAT_P(args[0]);

    if (n >= 2) {
        double base;
        check_number(args[1], "LOG");
        base = cl_to_double(args[1]);
        if (base <= 0.0)
            cl_error(CL_ERR_TYPE, "LOG: base must be positive");
        if (base == 1.0)
            cl_error(CL_ERR_DIVZERO, "LOG: base must not be 1");
        is_double = is_double || CL_DOUBLE_FLOAT_P(args[1]);
        result = log(val) / log(base);
    } else {
        result = log(val);
    }
    return make_result(result, is_double);
}

/* (expt base power) → number
 * Integer base + non-negative integer power: exact integer result.
 * Otherwise: float result via pow().
 * Signals error for negative base with non-integer power (no complex). */
static CL_Obj bi_expt_float(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "EXPT");
    check_number(args[1], "EXPT");

    /* Both integers, non-negative exponent: use exact integer arithmetic */
    if (CL_INTEGER_P(args[0]) && CL_INTEGER_P(args[1])
        && !cl_arith_minusp(args[1])) {
        return cl_arith_expt(args[0], args[1]);
    }

    /* Complex base with integer exponent: repeated squaring via
     * cl_arith_mul.  Each cl_arith_mul applies the canonical-complex
     * rules so e.g. (expt #C(0 2) 2) → -4 (rational, integer-zero imag),
     * and (expt c 0) for float complex gives #C(1.0 0.0). */
    if (CL_COMPLEX_P(args[0]) && CL_INTEGER_P(args[1])) {
        CL_Obj base = args[0];
        CL_Obj exp_obj = args[1];
        int negate_exp = 0;
        CL_Obj result;

        CL_GC_PROTECT(base);
        CL_GC_PROTECT(exp_obj);
        if (cl_arith_minusp(exp_obj)) {
            negate_exp = 1;
            exp_obj = cl_arith_negate(exp_obj);
        }
        CL_GC_PROTECT(exp_obj);  /* re-protect after potential reassignment */

        /* Build "one" of base's float kind so (expt c 0) follows
         * float-contagion rules per CLHS 12.1.5.3. */
        result = cl_arith_add(CL_MAKE_FIXNUM(1),
                              cl_arith_mul(base, CL_MAKE_FIXNUM(0)));
        CL_GC_PROTECT(result);

        /* Square-and-multiply over fixnum-integer exponents.  Bignum
         * exponents on a complex base are rare and would require a
         * different loop; punt for now. */
        if (CL_FIXNUM_P(exp_obj)) {
            int32_t e = CL_FIXNUM_VAL(exp_obj);
            CL_Obj b = base;
            CL_GC_PROTECT(b);
            while (e > 0) {
                if (e & 1) result = cl_arith_mul(result, b);
                e >>= 1;
                if (e > 0) b = cl_arith_mul(b, b);
            }
            CL_GC_UNPROTECT(1);
        } else {
            CL_GC_UNPROTECT(4);
            cl_error(CL_ERR_GENERAL,
                     "EXPT: bignum exponent on complex base not supported");
        }

        if (negate_exp) {
            CL_Obj one = CL_MAKE_FIXNUM(1);
            result = cl_arith_div(one, result);
        }
        CL_GC_UNPROTECT(4);
        return result;
    }

    /* Ratio base with integer exponent: exact rational result */
    if (CL_RATIO_P(args[0]) && CL_INTEGER_P(args[1])) {
        CL_Obj num = cl_numerator(args[0]);
        CL_Obj den = cl_denominator(args[0]);
        CL_Obj exp = args[1];
        CL_Obj rn, rd;
        /* Negative exponent: (a/b)^(-n) = (b/a)^n */
        if (cl_arith_minusp(exp)) {
            CL_Obj tmp = num;
            num = den;
            den = tmp;
            CL_GC_PROTECT(num); CL_GC_PROTECT(den);
            exp = cl_arith_negate(exp);
            CL_GC_UNPROTECT(2);
        }
        CL_GC_PROTECT(num); CL_GC_PROTECT(den); CL_GC_PROTECT(exp);
        rn = cl_arith_expt(num, exp);
        CL_GC_UNPROTECT(3);
        CL_GC_PROTECT(rn); CL_GC_PROTECT(den); CL_GC_PROTECT(exp);
        rd = cl_arith_expt(den, exp);
        CL_GC_UNPROTECT(3);
        return cl_make_ratio_normalized(rn, rd);
    }

    /* Integer base with negative integer exponent: produce ratio */
    if (CL_INTEGER_P(args[0]) && CL_INTEGER_P(args[1])
        && cl_arith_minusp(args[1])) {
        CL_Obj base = args[0];
        CL_Obj exp;
        CL_Obj powered;
        CL_GC_PROTECT(base);
        exp = cl_arith_negate(args[1]);
        CL_GC_UNPROTECT(1);
        CL_GC_PROTECT(base); CL_GC_PROTECT(exp);
        powered = cl_arith_expt(base, exp);
        CL_GC_UNPROTECT(2);
        return cl_make_ratio_normalized(CL_MAKE_FIXNUM(1), powered);
    }

    /* Float path */
    {
        double base = cl_to_double(args[0]);
        double power = cl_to_double(args[1]);
        double result;
        int is_double = CL_DOUBLE_FLOAT_P(args[0])
                     || CL_DOUBLE_FLOAT_P(args[1]);

        /* Special cases */
        if (power == 0.0)
            return make_result(1.0, is_double);

        if (base == 0.0) {
            if (power < 0.0)
                cl_error(CL_ERR_DIVZERO, "EXPT: division by zero");
            return make_result(0.0, is_double);
        }

        /* Negative base with non-integer power → complex (unsupported) */
        if (base < 0.0 && floor(power) != power)
            cl_error(CL_ERR_TYPE,
                "EXPT: negative base with non-integer exponent "
                "requires complex numbers");

        result = pow(base, power);
        return make_result(result, is_double);
    }
}

/* ================================================================
 * Trigonometric functions (step 12)
 * ================================================================ */

/* (sin number) → float */
static CL_Obj bi_sin(CL_Obj *args, int n)
{
    double val;
    CL_UNUSED(n);
    check_number(args[0], "SIN");
    val = cl_to_double(args[0]);
    return make_result(sin(val), CL_DOUBLE_FLOAT_P(args[0]));
}

/* (cos number) → float */
static CL_Obj bi_cos(CL_Obj *args, int n)
{
    double val;
    CL_UNUSED(n);
    check_number(args[0], "COS");
    val = cl_to_double(args[0]);
    return make_result(cos(val), CL_DOUBLE_FLOAT_P(args[0]));
}

/* (tan number) → float */
static CL_Obj bi_tan(CL_Obj *args, int n)
{
    double val;
    CL_UNUSED(n);
    check_number(args[0], "TAN");
    val = cl_to_double(args[0]);
    return make_result(tan(val), CL_DOUBLE_FLOAT_P(args[0]));
}

/* (asin number) → float
 * Domain: [-1, 1]. Signals error otherwise. */
static CL_Obj bi_asin(CL_Obj *args, int n)
{
    double val;
    CL_UNUSED(n);
    check_number(args[0], "ASIN");
    val = cl_to_double(args[0]);
    if (val < -1.0 || val > 1.0)
        cl_error(CL_ERR_TYPE, "ASIN: argument must be in [-1, 1]");
    return make_result(asin(val), CL_DOUBLE_FLOAT_P(args[0]));
}

/* (acos number) → float
 * Domain: [-1, 1]. Signals error otherwise. */
static CL_Obj bi_acos(CL_Obj *args, int n)
{
    double val;
    CL_UNUSED(n);
    check_number(args[0], "ACOS");
    val = cl_to_double(args[0]);
    if (val < -1.0 || val > 1.0)
        cl_error(CL_ERR_TYPE, "ACOS: argument must be in [-1, 1]");
    return make_result(acos(val), CL_DOUBLE_FLOAT_P(args[0]));
}

/* (atan y &optional x) → float
 * One arg: arctangent of y.
 * Two args: atan2(y, x). */
static CL_Obj bi_atan(CL_Obj *args, int n)
{
    double val;
    int is_double;

    check_number(args[0], "ATAN");
    is_double = CL_DOUBLE_FLOAT_P(args[0]);

    if (n >= 2) {
        double x;
        check_number(args[1], "ATAN");
        is_double = is_double || CL_DOUBLE_FLOAT_P(args[1]);
        val = cl_to_double(args[0]);
        x = cl_to_double(args[1]);
        return make_result(atan2(val, x), is_double);
    }

    val = cl_to_double(args[0]);
    return make_result(atan(val), is_double);
}

/* Hyperbolic functions (CLHS 12.1.4 / 12.1.5).  Each accepts any real and
 * returns a float; double-float in → double-float out, otherwise single. */
static CL_Obj bi_sinh(CL_Obj *args, int n)
{
    double val;
    CL_UNUSED(n);
    check_number(args[0], "SINH");
    val = cl_to_double(args[0]);
    return make_result(sinh(val), CL_DOUBLE_FLOAT_P(args[0]));
}

static CL_Obj bi_cosh(CL_Obj *args, int n)
{
    double val;
    CL_UNUSED(n);
    check_number(args[0], "COSH");
    val = cl_to_double(args[0]);
    return make_result(cosh(val), CL_DOUBLE_FLOAT_P(args[0]));
}

static CL_Obj bi_tanh(CL_Obj *args, int n)
{
    double val;
    CL_UNUSED(n);
    check_number(args[0], "TANH");
    val = cl_to_double(args[0]);
    return make_result(tanh(val), CL_DOUBLE_FLOAT_P(args[0]));
}

/* (asinh x) — defined for all reals. */
static CL_Obj bi_asinh(CL_Obj *args, int n)
{
    double val;
    CL_UNUSED(n);
    check_number(args[0], "ASINH");
    val = cl_to_double(args[0]);
    return make_result(asinh(val), CL_DOUBLE_FLOAT_P(args[0]));
}

/* (acosh x) — domain x >= 1 for real result. */
static CL_Obj bi_acosh(CL_Obj *args, int n)
{
    double val;
    CL_UNUSED(n);
    check_number(args[0], "ACOSH");
    val = cl_to_double(args[0]);
    if (val < 1.0)
        cl_error(CL_ERR_TYPE, "ACOSH: argument must be >= 1");
    return make_result(acosh(val), CL_DOUBLE_FLOAT_P(args[0]));
}

/* (atanh x) — domain (-1, 1) for finite real result. */
static CL_Obj bi_atanh(CL_Obj *args, int n)
{
    double val;
    CL_UNUSED(n);
    check_number(args[0], "ATANH");
    val = cl_to_double(args[0]);
    if (val <= -1.0 || val >= 1.0)
        cl_error(CL_ERR_TYPE, "ATANH: argument must be in (-1, 1)");
    return make_result(atanh(val), CL_DOUBLE_FLOAT_P(args[0]));
}

void cl_float_math_init(void)
{
    defun("SQRT", bi_sqrt, 1, 1);
    defun("EXP", bi_exp, 1, 1);
    defun("LOG", bi_log, 1, 2);
    /* Overwrites the integer-only EXPT from builtins_arith */
    defun("EXPT", bi_expt_float, 2, 2);
    /* Step 12: trigonometric functions */
    defun("SIN",  bi_sin,  1, 1);
    defun("COS",  bi_cos,  1, 1);
    defun("TAN",  bi_tan,  1, 1);
    defun("ASIN", bi_asin, 1, 1);
    defun("ACOS", bi_acos, 1, 1);
    defun("ATAN", bi_atan, 1, 2);
    /* Hyperbolic */
    defun("SINH",  bi_sinh,  1, 1);
    defun("COSH",  bi_cosh,  1, 1);
    defun("TANH",  bi_tanh,  1, 1);
    defun("ASINH", bi_asinh, 1, 1);
    defun("ACOSH", bi_acosh, 1, 1);
    defun("ATANH", bi_atanh, 1, 1);
}
