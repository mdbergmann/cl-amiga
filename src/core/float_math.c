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

/* m68k-amigaos-gcc's <math.h> lacks M_PI (POSIX extension). */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
        cl_signal_type_error(obj, "NUMBER", op);
}

/* Return a float result with proper type contagion.
 * If any input was double-float, result is double-float. */
static CL_Obj make_result(double val, int is_double)
{
    if (is_double)
        return cl_make_double_float(val);
    return cl_make_single_float((float)val);
}

/* Signal FLOATING-POINT-OVERFLOW if `val` is +/-inf when stored at the
 * target precision (single- or double-float).  Per CLHS, EXP/EXPT must
 * raise this for inputs that produce a non-representable result. */
static void check_fp_overflow(double val, int is_double, const char *fn_name,
                              CL_Obj operand)
{
    int overflowed = is_double ? isinf(val) : isinf((double)(float)val);
    if (overflowed) {
        CL_Obj op_sym = cl_intern_in(fn_name, (uint32_t)strlen(fn_name),
                                     cl_package_cl);
        CL_Obj operands;
        CL_GC_PROTECT(op_sym);
        CL_GC_PROTECT(operand);
        operands = cl_cons(operand, CL_NIL);
        CL_GC_UNPROTECT(2);
        cl_signal_arith_error(SYM_FLOATING_POINT_OVERFLOW, op_sym,
                              operands, fn_name);
    }
}

/* Same for floating-point underflow: result rounds to 0 in the target
 * precision when the true value is non-zero. */
static void check_fp_underflow(double val, double original_arg, int is_double,
                               const char *fn_name, CL_Obj operand)
{
    int underflowed;
    if (original_arg == 0.0) return;
    underflowed = is_double ? (val == 0.0) : ((float)val == 0.0f);
    if (underflowed) {
        CL_Obj op_sym = cl_intern_in(fn_name, (uint32_t)strlen(fn_name),
                                     cl_package_cl);
        CL_Obj operands;
        CL_GC_PROTECT(op_sym);
        CL_GC_PROTECT(operand);
        operands = cl_cons(operand, CL_NIL);
        CL_GC_UNPROTECT(2);
        cl_signal_arith_error(SYM_FLOATING_POINT_UNDERFLOW, op_sym,
                              operands, fn_name);
    }
}

/* ----------------------------------------------------------------
 * Complex-double helpers, used by EXP / LOG / trig / hyperbolic.
 * Defined here (before bi_sqrt) so all complex-aware builtins below
 * can share them.
 * ---------------------------------------------------------------- */

typedef struct { double re, im; } cdbl;

static void to_cdbl(CL_Obj x, cdbl *out, int *want_double, int *is_complex)
{
    if (CL_COMPLEX_P(x)) {
        CL_Complex *cx = (CL_Complex *)CL_OBJ_TO_PTR(x);
        out->re = cl_to_double(cx->realpart);
        out->im = cl_to_double(cx->imagpart);
        *want_double = CL_DOUBLE_FLOAT_P(cx->realpart) ||
                       CL_DOUBLE_FLOAT_P(cx->imagpart);
        *is_complex = 1;
    } else {
        out->re = cl_to_double(x);
        out->im = 0.0;
        *want_double = CL_DOUBLE_FLOAT_P(x);
        *is_complex = 0;
    }
}

static CL_Obj make_complex_double_result(double re, double im, int is_double)
{
    CL_Obj r_obj, i_obj, c;
    if (is_double) {
        r_obj = cl_make_double_float(re);
        CL_GC_PROTECT(r_obj);
        i_obj = cl_make_double_float(im);
    } else {
        r_obj = cl_make_single_float((float)re);
        CL_GC_PROTECT(r_obj);
        i_obj = cl_make_single_float((float)im);
    }
    CL_GC_PROTECT(i_obj);
    c = cl_make_complex(r_obj, i_obj);
    CL_GC_UNPROTECT(2);
    return c;
}

static cdbl c_add(cdbl a, cdbl b) { cdbl r; r.re = a.re+b.re; r.im = a.im+b.im; return r; }
static cdbl c_sub(cdbl a, cdbl b) { cdbl r; r.re = a.re-b.re; r.im = a.im-b.im; return r; }
static cdbl c_mul(cdbl a, cdbl b)
{
    cdbl r;
    r.re = a.re * b.re - a.im * b.im;
    r.im = a.re * b.im + a.im * b.re;
    return r;
}
static cdbl c_div(cdbl a, cdbl b)
{
    cdbl r;
    double d = b.re * b.re + b.im * b.im;
    r.re = (a.re * b.re + a.im * b.im) / d;
    r.im = (a.im * b.re - a.re * b.im) / d;
    return r;
}

static cdbl c_sqrt(cdbl z)
{
    cdbl r;
    double mag = sqrt(z.re * z.re + z.im * z.im);
    r.re = sqrt((mag + z.re) * 0.5);
    r.im = sqrt((mag - z.re) * 0.5);
    if (z.im < 0.0) r.im = -r.im;
    return r;
}

static cdbl c_exp(cdbl z)
{
    cdbl r;
    double ea = exp(z.re);
    r.re = ea * cos(z.im);
    r.im = ea * sin(z.im);
    return r;
}

static cdbl c_log(cdbl z)
{
    cdbl r;
    r.re = 0.5 * log(z.re * z.re + z.im * z.im);
    r.im = atan2(z.im, z.re);
    return r;
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

/* (exp number) → number
 * Real → real (e^x).  Complex → complex via exp(a+bi)=e^a(cos b + i sin b). */
static CL_Obj bi_exp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "EXP");
    if (CL_COMPLEX_P(args[0])) {
        cdbl z, r;
        int is_double, is_cx;
        to_cdbl(args[0], &z, &is_double, &is_cx);
        r = c_exp(z);
        return make_complex_double_result(r.re, r.im, is_double);
    }
    {
        double val = cl_to_double(args[0]);
        int is_dbl = CL_DOUBLE_FLOAT_P(args[0]);
        double r = exp(val);
        check_fp_overflow(r, is_dbl, "EXP", args[0]);
        check_fp_underflow(r, val, is_dbl, "EXP", args[0]);
        return make_result(r, is_dbl);
    }
}

/* (log number &optional base) → number
 * Negative real or complex input → complex result (CLHS 12.1.5.3).
 * Two-arg form computes log(n)/log(base); both legs go through this
 * dispatcher so a negative `n` or `base` correctly produces complex.
 * (log 1 base) returns the appropriate zero (real or complex 0). */
static CL_Obj bi_log(CL_Obj *args, int n)
{
    cdbl z, r;
    int is_double, is_cx;
    check_number(args[0], "LOG");

    to_cdbl(args[0], &z, &is_double, &is_cx);

    if (n >= 2) {
        cdbl b;
        int b_dbl, b_cx;
        cdbl numer, denom, q;
        check_number(args[1], "LOG");
        to_cdbl(args[1], &b, &b_dbl, &b_cx);
        is_double = is_double || b_dbl;
        if (z.re == 0.0 && z.im == 0.0)
            cl_error(CL_ERR_DIVZERO, "LOG: argument must be non-zero");
        if (b.re == 1.0 && b.im == 0.0)
            cl_error(CL_ERR_DIVZERO, "LOG: base must not be 1");
        numer = c_log(z);
        denom = c_log(b);
        q = c_div(numer, denom);
        /* Real result if both inputs were real-positive */
        if (!is_cx && !b_cx && z.re > 0.0 && b.re > 0.0)
            return make_result(q.re, is_double);
        return make_complex_double_result(q.re, q.im, is_double);
    }

    /* One-arg LOG */
    if (z.re == 0.0 && z.im == 0.0)
        cl_error(CL_ERR_DIVZERO, "LOG: argument must be non-zero");
    if (!is_cx && z.re > 0.0)
        return make_result(log(z.re), is_double);
    r = c_log(z);
    return make_complex_double_result(r.re, r.im, is_double);
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

    /* (expt 0 y) — CLHS 12.1.5.3.  When the base is zero (rational,
     * float, or canonical complex zero) and the exponent's real part
     * is strictly positive, the result is the contagion-canonical zero
     * — the same value (* base exponent) would produce.  This catches
     * complex exponents that the integer/ratio/float paths below would
     * pass to cl_to_double() and signal "Not a number: COMPLEX". */
    if (cl_arith_zerop(args[0])) {
        CL_Obj exp_re = CL_COMPLEX_P(args[1])
                        ? ((CL_Complex *)CL_OBJ_TO_PTR(args[1]))->realpart
                        : args[1];
        if (!cl_arith_zerop(exp_re) && !cl_arith_minusp(exp_re))
            return cl_arith_mul(args[0], args[1]);
        if (cl_arith_minusp(exp_re))
            cl_error(CL_ERR_DIVZERO,
                     "EXPT: 0 raised to a non-positive real-part power");
        /* exp_re is zero and exponent is non-zero (purely imaginary):
         * mathematically undefined; fall through to diagnostic error. */
        if (!cl_arith_zerop(args[1]))
            cl_error(CL_ERR_GENERAL,
                     "EXPT: 0 raised to a purely imaginary power");
        /* (expt 0 0) = 1 with appropriate float contagion: 1 + 0*base. */
        return cl_arith_add(CL_MAKE_FIXNUM(1),
                            cl_arith_mul(args[0], CL_MAKE_FIXNUM(0)));
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

        /* Negative base with non-integer power → complex principal
         * value (CLHS 12.1.5.3): exp(power * log(base)) where log of a
         * negative real is a complex with imag = pi. */
        if (base < 0.0 && floor(power) != power) {
            double log_re = log(-base);
            double log_im = M_PI;
            double scaled_re = power * log_re;
            double scaled_im = power * log_im;
            double exp_re = exp(scaled_re);
            double r_re = exp_re * cos(scaled_im);
            double r_im = exp_re * sin(scaled_im);
            if (is_double)
                return cl_make_complex(cl_make_double_float(r_re),
                                       cl_make_double_float(r_im));
            return cl_make_complex(cl_make_single_float((float)r_re),
                                   cl_make_single_float((float)r_im));
        }

        result = pow(base, power);
        check_fp_overflow(result, is_double, "EXPT", args[0]);
        check_fp_underflow(result, base, is_double, "EXPT", args[0]);
        return make_result(result, is_double);
    }
}

/* ================================================================
 * Trigonometric / hyperbolic functions — complex-aware
 *
 * Strategy: every function accepts real or complex input.  For real
 * inputs whose mathematical result is real and finite, we return a
 * float of the appropriate kind; otherwise we promote to complex via
 * the standard half-plane formulas.  Complex computations all happen
 * in double; the result is downconverted to single-float only when
 * neither input component was double.
 * ================================================================ */

/* sin(a+bi) = sin(a)cosh(b) + i cos(a)sinh(b) */
static cdbl c_sin(cdbl z)
{
    cdbl r;
    r.re = sin(z.re) * cosh(z.im);
    r.im = cos(z.re) * sinh(z.im);
    return r;
}

/* cos(a+bi) = cos(a)cosh(b) - i sin(a)sinh(b) */
static cdbl c_cos(cdbl z)
{
    cdbl r;
    r.re =  cos(z.re) * cosh(z.im);
    r.im = -sin(z.re) * sinh(z.im);
    return r;
}

/* tan(z) = sin(z)/cos(z) */
static cdbl c_tan(cdbl z) { return c_div(c_sin(z), c_cos(z)); }

/* sinh(a+bi) = sinh(a)cos(b) + i cosh(a)sin(b) */
static cdbl c_sinh(cdbl z)
{
    cdbl r;
    r.re = sinh(z.re) * cos(z.im);
    r.im = cosh(z.re) * sin(z.im);
    return r;
}

/* cosh(a+bi) = cosh(a)cos(b) + i sinh(a)sin(b) */
static cdbl c_cosh(cdbl z)
{
    cdbl r;
    r.re = cosh(z.re) * cos(z.im);
    r.im = sinh(z.re) * sin(z.im);
    return r;
}

static cdbl c_tanh(cdbl z) { return c_div(c_sinh(z), c_cosh(z)); }

/* Inverse hyperbolic / trig via standard log/sqrt identities.
 *   asinh(z) = log(z + sqrt(z² + 1))
 *   acosh(z) = log(z + sqrt(z+1)*sqrt(z-1))
 *   atanh(z) = (1/2) log((1+z)/(1-z))
 *   asin(z)  = -i * asinh(i z)         (real-axis match)
 *            = -i * log(i z + sqrt(1 - z²))
 *   acos(z)  = π/2 - asin(z)
 *   atan(z)  = (i/2) * log((i-z)/(i+z))
 */
static cdbl c_asinh(cdbl z)
{
    cdbl one = {1.0, 0.0};
    cdbl t = c_add(c_mul(z, z), one);
    return c_log(c_add(z, c_sqrt(t)));
}

static cdbl c_acosh(cdbl z)
{
    cdbl one = {1.0, 0.0};
    cdbl s1 = c_sqrt(c_add(z, one));
    cdbl s2 = c_sqrt(c_sub(z, one));
    return c_log(c_add(z, c_mul(s1, s2)));
}

static cdbl c_atanh(cdbl z)
{
    cdbl one = {1.0, 0.0};
    cdbl half = {0.5, 0.0};
    return c_mul(half, c_log(c_div(c_add(one, z), c_sub(one, z))));
}

static cdbl c_asin(cdbl z)
{
    cdbl one = {1.0, 0.0};
    cdbl iz; iz.re = -z.im; iz.im = z.re;            /* iz */
    cdbl t = c_sub(one, c_mul(z, z));
    cdbl s = c_sqrt(t);
    cdbl arg = c_add(iz, s);
    cdbl l = c_log(arg);
    cdbl r; r.re = l.im; r.im = -l.re;               /* -i * l */
    return r;
}

static cdbl c_acos(cdbl z)
{
    cdbl pi_2 = {M_PI / 2.0, 0.0};
    return c_sub(pi_2, c_asin(z));
}

static cdbl c_atan(cdbl z)
{
    cdbl i_const = {0.0, 1.0};
    cdbl half_i  = {0.0, 0.5};
    return c_mul(half_i, c_log(c_div(c_sub(i_const, z), c_add(i_const, z))));
}

/* (sin number).  Real → real; complex → complex. */
static CL_Obj bi_sin(CL_Obj *args, int n)
{
    cdbl z, r;
    int is_double, is_cx;
    CL_UNUSED(n);
    check_number(args[0], "SIN");
    to_cdbl(args[0], &z, &is_double, &is_cx);
    if (!is_cx) return make_result(sin(z.re), is_double);
    r = c_sin(z);
    return make_complex_double_result(r.re, r.im, is_double);
}

static CL_Obj bi_cos(CL_Obj *args, int n)
{
    cdbl z, r;
    int is_double, is_cx;
    CL_UNUSED(n);
    check_number(args[0], "COS");
    to_cdbl(args[0], &z, &is_double, &is_cx);
    if (!is_cx) return make_result(cos(z.re), is_double);
    r = c_cos(z);
    return make_complex_double_result(r.re, r.im, is_double);
}

static CL_Obj bi_tan(CL_Obj *args, int n)
{
    cdbl z, r;
    int is_double, is_cx;
    CL_UNUSED(n);
    check_number(args[0], "TAN");
    to_cdbl(args[0], &z, &is_double, &is_cx);
    if (!is_cx) return make_result(tan(z.re), is_double);
    r = c_tan(z);
    return make_complex_double_result(r.re, r.im, is_double);
}

/* asin: real input outside [-1,1] promotes to complex per CLHS 12.1.5.3. */
static CL_Obj bi_asin(CL_Obj *args, int n)
{
    cdbl z, r;
    int is_double, is_cx;
    CL_UNUSED(n);
    check_number(args[0], "ASIN");
    to_cdbl(args[0], &z, &is_double, &is_cx);
    if (!is_cx && z.re >= -1.0 && z.re <= 1.0)
        return make_result(asin(z.re), is_double);
    r = c_asin(z);
    return make_complex_double_result(r.re, r.im, is_double);
}

static CL_Obj bi_acos(CL_Obj *args, int n)
{
    cdbl z, r;
    int is_double, is_cx;
    CL_UNUSED(n);
    check_number(args[0], "ACOS");
    to_cdbl(args[0], &z, &is_double, &is_cx);
    if (!is_cx && z.re >= -1.0 && z.re <= 1.0)
        return make_result(acos(z.re), is_double);
    r = c_acos(z);
    return make_complex_double_result(r.re, r.im, is_double);
}

/* (atan y &optional x).  One arg accepts complex; two-arg form
 * is atan2 over reals (CLHS REAL-only). */
static CL_Obj bi_atan(CL_Obj *args, int n)
{
    cdbl z, r;
    int is_double, is_cx;
    check_number(args[0], "ATAN");

    if (n >= 2) {
        double y, x;
        check_number(args[1], "ATAN");
        if (!CL_REALP(args[0]))
            cl_signal_type_error(args[0], "REAL", "ATAN");
        if (!CL_REALP(args[1]))
            cl_signal_type_error(args[1], "REAL", "ATAN");
        y = cl_to_double(args[0]);
        x = cl_to_double(args[1]);
        return make_result(atan2(y, x),
                           CL_DOUBLE_FLOAT_P(args[0]) ||
                           CL_DOUBLE_FLOAT_P(args[1]));
    }

    to_cdbl(args[0], &z, &is_double, &is_cx);
    if (!is_cx) return make_result(atan(z.re), is_double);
    r = c_atan(z);
    return make_complex_double_result(r.re, r.im, is_double);
}

/* Hyperbolic — same pattern. */
static CL_Obj bi_sinh(CL_Obj *args, int n)
{
    cdbl z, r;
    int is_double, is_cx;
    CL_UNUSED(n);
    check_number(args[0], "SINH");
    to_cdbl(args[0], &z, &is_double, &is_cx);
    if (!is_cx) return make_result(sinh(z.re), is_double);
    r = c_sinh(z);
    return make_complex_double_result(r.re, r.im, is_double);
}

static CL_Obj bi_cosh(CL_Obj *args, int n)
{
    cdbl z, r;
    int is_double, is_cx;
    CL_UNUSED(n);
    check_number(args[0], "COSH");
    to_cdbl(args[0], &z, &is_double, &is_cx);
    if (!is_cx) return make_result(cosh(z.re), is_double);
    r = c_cosh(z);
    return make_complex_double_result(r.re, r.im, is_double);
}

static CL_Obj bi_tanh(CL_Obj *args, int n)
{
    cdbl z, r;
    int is_double, is_cx;
    CL_UNUSED(n);
    check_number(args[0], "TANH");
    to_cdbl(args[0], &z, &is_double, &is_cx);
    if (!is_cx) return make_result(tanh(z.re), is_double);
    r = c_tanh(z);
    return make_complex_double_result(r.re, r.im, is_double);
}

static CL_Obj bi_asinh(CL_Obj *args, int n)
{
    cdbl z, r;
    int is_double, is_cx;
    CL_UNUSED(n);
    check_number(args[0], "ASINH");
    to_cdbl(args[0], &z, &is_double, &is_cx);
    if (!is_cx) return make_result(asinh(z.re), is_double);
    r = c_asinh(z);
    return make_complex_double_result(r.re, r.im, is_double);
}

/* acosh: real input < 1 → complex result (CLHS 12.1.5.3). */
static CL_Obj bi_acosh(CL_Obj *args, int n)
{
    cdbl z, r;
    int is_double, is_cx;
    CL_UNUSED(n);
    check_number(args[0], "ACOSH");
    to_cdbl(args[0], &z, &is_double, &is_cx);
    if (!is_cx && z.re >= 1.0) return make_result(acosh(z.re), is_double);
    r = c_acosh(z);
    return make_complex_double_result(r.re, r.im, is_double);
}

/* atanh: real input outside (-1,1) → complex. */
static CL_Obj bi_atanh(CL_Obj *args, int n)
{
    cdbl z, r;
    int is_double, is_cx;
    CL_UNUSED(n);
    check_number(args[0], "ATANH");
    to_cdbl(args[0], &z, &is_double, &is_cx);
    if (!is_cx && z.re > -1.0 && z.re < 1.0)
        return make_result(atanh(z.re), is_double);
    r = c_atanh(z);
    return make_complex_double_result(r.re, r.im, is_double);
}

/* (cis radians) → exp(i*radians) = cos(radians) + i sin(radians).
 * Domain: real argument; result is always a complex. */
static CL_Obj bi_cis(CL_Obj *args, int n)
{
    double a;
    int is_double;
    CL_UNUSED(n);
    check_number(args[0], "CIS");
    if (!CL_REALP(args[0]))
        cl_signal_type_error(args[0], "REAL", "CIS");
    a = cl_to_double(args[0]);
    is_double = CL_DOUBLE_FLOAT_P(args[0]);
    return make_complex_double_result(cos(a), sin(a), is_double);
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
    defun("CIS",   bi_cis,   1, 1);
}
