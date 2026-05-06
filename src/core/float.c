/*
 * float.c — IEEE 754 floating-point conversions and arithmetic
 *
 * Contagion rule: if either operand is double-float, result is double-float;
 * otherwise single-float. Integer operands are promoted to float.
 */

#include "float.h"
#include "ratio.h"
#include "bignum.h"
#include "mem.h"
#include "error.h"
#include <math.h>

/* ================================================================
 * Conversion helpers
 * ================================================================ */

double cl_to_double(CL_Obj obj)
{
    if (CL_FIXNUM_P(obj))
        return (double)CL_FIXNUM_VAL(obj);

    if (CL_SINGLE_FLOAT_P(obj))
        return (double)((CL_SingleFloat *)CL_OBJ_TO_PTR(obj))->value;

    if (CL_DOUBLE_FLOAT_P(obj))
        return ((CL_DoubleFloat *)CL_OBJ_TO_PTR(obj))->value;

    if (CL_BIGNUM_P(obj)) {
        CL_Bignum *bn = (CL_Bignum *)CL_OBJ_TO_PTR(obj);
        double result = 0.0;
        double base = 1.0;
        uint32_t i;
        for (i = 0; i < bn->length; i++) {
            result += (double)bn->limbs[i] * base;
            base *= 65536.0;
        }
        return bn->sign ? -result : result;
    }

    if (CL_RATIO_P(obj))
        return cl_ratio_to_double(obj);

    cl_error(CL_ERR_TYPE, "Not a number: %s", cl_type_name(obj));
    return 0.0; /* Not reached */
}

float cl_to_float(CL_Obj obj)
{
    return (float)cl_to_double(obj);
}

/* ================================================================
 * Contagion: determine result type from two operands
 * Returns 1 if result should be double-float, 0 for single-float.
 * ================================================================ */

static int want_double(CL_Obj a, CL_Obj b)
{
    return CL_DOUBLE_FLOAT_P(a) || CL_DOUBLE_FLOAT_P(b);
}

static CL_Obj make_float_result(double val, int is_double)
{
    if (is_double)
        return cl_make_double_float(val);
    return cl_make_single_float((float)val);
}

/* ================================================================
 * Float arithmetic
 * ================================================================ */

CL_Obj cl_float_add(CL_Obj a, CL_Obj b)
{
    int dbl = want_double(a, b);
    double da = cl_to_double(a);
    double db = cl_to_double(b);
    return make_float_result(da + db, dbl);
}

CL_Obj cl_float_sub(CL_Obj a, CL_Obj b)
{
    int dbl = want_double(a, b);
    double da = cl_to_double(a);
    double db = cl_to_double(b);
    return make_float_result(da - db, dbl);
}

CL_Obj cl_float_mul(CL_Obj a, CL_Obj b)
{
    int dbl = want_double(a, b);
    double da = cl_to_double(a);
    double db = cl_to_double(b);
    return make_float_result(da * db, dbl);
}

CL_Obj cl_float_div(CL_Obj a, CL_Obj b)
{
    int dbl = want_double(a, b);
    double da = cl_to_double(a);
    double db = cl_to_double(b);
    if (db == 0.0)
        cl_error(CL_ERR_DIVZERO, "Division by zero");
    return make_float_result(da / db, dbl);
}

CL_Obj cl_float_negate(CL_Obj a)
{
    if (CL_DOUBLE_FLOAT_P(a))
        return cl_make_double_float(-((CL_DoubleFloat *)CL_OBJ_TO_PTR(a))->value);
    return cl_make_single_float(-cl_to_float(a));
}

/* ================================================================
 * Float comparison and predicates
 * ================================================================ */

/* Exact IEEE-bit-pattern rational: returns CL_Obj that represents d
   exactly as M * 2^E (integer for E >= 0, ratio for E < 0).
   Caller must have already excluded NaN / +-Infinity.  d may be 0. */
static CL_Obj double_to_exact_rational(double d)
{
    union { double d; uint64_t u; } u;
    int sign;
    int exp_field;
    uint64_t frac;
    int E;
    uint64_t M;
    CL_Obj m_obj;

    if (d == 0.0)
        return CL_MAKE_FIXNUM(0);

    u.d = d;
    sign      = (int)(u.u >> 63);
    exp_field = (int)((u.u >> 52) & 0x7FF);
    frac      = u.u & (((uint64_t)1 << 52) - 1);

    if (exp_field == 0) {
        /* Subnormal: value = (-1)^s * frac * 2^(-1074) */
        M = frac;
        E = -1074;
    } else {
        /* Normal: value = (-1)^s * (2^52 + frac) * 2^(exp_field - 1023 - 52) */
        M = ((uint64_t)1 << 52) | frac;
        E = exp_field - 1023 - 52;
    }

    /* Strip trailing zero bits to keep the ratio in lowest terms cheap. */
    while (M != 0 && (M & 1) == 0) {
        M >>= 1;
        E++;
    }

    /* Build CL_Obj for the (positive) mantissa M (up to 53 bits). */
    {
        uint32_t lo = (uint32_t)M;
        uint32_t hi = (uint32_t)(M >> 32);
        if (hi == 0) {
            m_obj = cl_bignum_from_uint32(lo);
            m_obj = cl_bignum_normalize(m_obj);
        } else {
            CL_Obj h, h_shifted, lo_obj;
            h = cl_bignum_from_uint32(hi);
            CL_GC_PROTECT(h);
            h_shifted = cl_arith_ash(h, CL_MAKE_FIXNUM(32));
            CL_GC_UNPROTECT(1);
            CL_GC_PROTECT(h_shifted);
            lo_obj = cl_bignum_from_uint32(lo);
            CL_GC_UNPROTECT(1);
            CL_GC_PROTECT(h_shifted); CL_GC_PROTECT(lo_obj);
            m_obj = cl_arith_add(h_shifted, lo_obj);
            CL_GC_UNPROTECT(2);
        }
    }

    if (sign) {
        CL_GC_PROTECT(m_obj);
        m_obj = cl_arith_negate(m_obj);
        CL_GC_UNPROTECT(1);
    }

    if (E >= 0) {
        return cl_arith_ash(m_obj, CL_MAKE_FIXNUM(E));
    } else {
        CL_Obj den;
        CL_GC_PROTECT(m_obj);
        den = cl_arith_ash(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(-E));
        CL_GC_UNPROTECT(1);
        CL_GC_PROTECT(m_obj); CL_GC_PROTECT(den);
        {
            CL_Obj r = cl_make_ratio_normalized(m_obj, den);
            CL_GC_UNPROTECT(2);
            return r;
        }
    }
}

CL_Obj cl_float_to_exact_rational(CL_Obj f)
{
    double d = cl_to_double(f);
    if (d != d)
        cl_error(CL_ERR_TYPE, "Cannot convert NaN to rational");
    if (isinf(d))
        cl_error(CL_ERR_TYPE, "Cannot convert infinity to rational");
    return double_to_exact_rational(d);
}

int cl_float_compare(CL_Obj a, CL_Obj b)
{
    /* Both floats: compare as doubles directly (lossless within float domain). */
    if (CL_FLOATP(a) && CL_FLOATP(b)) {
        double da = cl_to_double(a);
        double db = cl_to_double(b);
        if (da < db) return -1;
        if (da > db) return 1;
        return 0;
    }

    /* Mixed: one operand is a float, the other is a rational (integer/ratio).
       Coerce-to-double loses precision for bignums and ratios that don't
       round-trip through double. Convert the float to its exact rational
       and dispatch to the rational comparator. */
    {
        CL_Obj fobj = CL_FLOATP(a) ? a : b;
        CL_Obj robj = CL_FLOATP(a) ? b : a;
        int swap   = CL_FLOATP(a) ? 0 : 1;
        double d   = cl_to_double(fobj);
        int rsign;
        int dsign;
        CL_Obj exact;
        int r;

        /* NaN: per IEEE, NaN is unordered. CL doesn't standardize NaN, but
           returning 0 (=) would lie. Follow Allegro/SBCL: treat as not equal
           to anything by returning a non-zero result that biases toward !=.
           Simplest: NaN = anything is false; NaN < / > anything is also false.
           cl_arith_compare callers test against 0/<0/>0; returning 1 here
           makes (= NaN x) -> NIL while (< NaN x) -> NIL too (since 1 >= 0
           fails the < condition). But (> NaN x) would return T. There's no
           clean answer in a tri-valued comparison API; document the choice. */
        if (d != d) {
            /* Treat NaN as "incomparable" by returning a sign that defeats
               any equality test (=, /=) but happens to make ordered tests
               agree with float NaN semantics on the side ordering checks. */
            return 1;
        }

        /* Infinity: bypass exact conversion. */
        if (isinf(d)) {
            int isneg = d < 0.0;
            r = isneg ? -1 : 1;
            return swap ? -r : r;
        }

        /* Quick sign comparison — avoids building any heap objects in the
           common case where signs differ or one side is zero. */
        dsign = (d < 0.0) ? -1 : (d > 0.0 ? 1 : 0);
        rsign = cl_arith_zerop(robj) ? 0 : (cl_arith_minusp(robj) ? -1 : 1);
        if (dsign != rsign) {
            r = (dsign < rsign) ? -1 : 1;
            return swap ? -r : r;
        }
        if (dsign == 0) return 0;  /* both zero */

        exact = double_to_exact_rational(d);
        CL_GC_PROTECT(exact); CL_GC_PROTECT(robj);
        r = cl_arith_compare(exact, robj);
        CL_GC_UNPROTECT(2);
        return swap ? -r : r;
    }
}

int cl_float_zerop(CL_Obj a)
{
    return cl_to_double(a) == 0.0;
}

int cl_float_plusp(CL_Obj a)
{
    return cl_to_double(a) > 0.0;
}

int cl_float_minusp(CL_Obj a)
{
    return cl_to_double(a) < 0.0;
}
