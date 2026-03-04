#include "ratio.h"
#include "mem.h"
#include "bignum.h"
#include "float.h"
#include "error.h"
#include "../platform/platform.h"

/* --- Accessors --- */

CL_Obj cl_numerator(CL_Obj obj)
{
    if (CL_RATIO_P(obj)) {
        CL_Ratio *r = (CL_Ratio *)CL_OBJ_TO_PTR(obj);
        return r->numerator;
    }
    /* For integers, numerator is the integer itself */
    return obj;
}

CL_Obj cl_denominator(CL_Obj obj)
{
    if (CL_RATIO_P(obj)) {
        CL_Ratio *r = (CL_Ratio *)CL_OBJ_TO_PTR(obj);
        return r->denominator;
    }
    /* For integers, denominator is 1 */
    return CL_MAKE_FIXNUM(1);
}

/* --- Normalization --- */

CL_Obj cl_make_ratio_normalized(CL_Obj num, CL_Obj den)
{
    CL_Obj g;

    /* Division by zero */
    if (cl_arith_zerop(den)) {
        cl_error(CL_ERR_DIVZERO, "division by zero");
        return CL_NIL;
    }

    /* 0/n = 0 */
    if (cl_arith_zerop(num)) {
        return CL_MAKE_FIXNUM(0);
    }

    /* Ensure positive denominator */
    if (cl_arith_minusp(den)) {
        CL_GC_PROTECT(num);
        CL_GC_PROTECT(den);
        num = cl_arith_negate(num);
        CL_GC_UNPROTECT(2);

        CL_GC_PROTECT(num);
        den = cl_arith_negate(den);
        CL_GC_UNPROTECT(1);
    }

    /* Reduce by GCD */
    CL_GC_PROTECT(num);
    CL_GC_PROTECT(den);
    g = cl_arith_gcd(cl_arith_abs(num), den);
    CL_GC_UNPROTECT(2);

    CL_GC_PROTECT(num);
    CL_GC_PROTECT(den);
    CL_GC_PROTECT(g);
    num = cl_arith_truncate(num, g);
    CL_GC_UNPROTECT(3);

    CL_GC_PROTECT(num);
    CL_GC_PROTECT(den);
    CL_GC_PROTECT(g);
    den = cl_arith_truncate(den, g);
    CL_GC_UNPROTECT(3);

    /* If denominator is 1, return integer */
    if (CL_FIXNUM_P(den) && CL_FIXNUM_VAL(den) == 1) {
        return num;
    }

    return cl_make_ratio(num, den);
}

/* --- Arithmetic --- */

CL_Obj cl_ratio_add(CL_Obj a, CL_Obj b)
{
    /* a_n/a_d + b_n/b_d = (a_n*b_d + b_n*a_d) / (a_d*b_d) */
    CL_Obj an = cl_numerator(a);
    CL_Obj ad = cl_denominator(a);
    CL_Obj bn = cl_numerator(b);
    CL_Obj bd = cl_denominator(b);
    CL_Obj num, den;

    CL_GC_PROTECT(an); CL_GC_PROTECT(ad);
    CL_GC_PROTECT(bn); CL_GC_PROTECT(bd);

    num = cl_arith_add(cl_arith_mul(an, bd), cl_arith_mul(bn, ad));
    CL_GC_UNPROTECT(4);

    CL_GC_PROTECT(num);
    CL_GC_PROTECT(ad);
    CL_GC_PROTECT(bd);
    den = cl_arith_mul(ad, bd);
    CL_GC_UNPROTECT(3);

    return cl_make_ratio_normalized(num, den);
}

CL_Obj cl_ratio_sub(CL_Obj a, CL_Obj b)
{
    CL_Obj an = cl_numerator(a);
    CL_Obj ad = cl_denominator(a);
    CL_Obj bn = cl_numerator(b);
    CL_Obj bd = cl_denominator(b);
    CL_Obj num, den;

    CL_GC_PROTECT(an); CL_GC_PROTECT(ad);
    CL_GC_PROTECT(bn); CL_GC_PROTECT(bd);

    num = cl_arith_sub(cl_arith_mul(an, bd), cl_arith_mul(bn, ad));
    CL_GC_UNPROTECT(4);

    CL_GC_PROTECT(num);
    CL_GC_PROTECT(ad);
    CL_GC_PROTECT(bd);
    den = cl_arith_mul(ad, bd);
    CL_GC_UNPROTECT(3);

    return cl_make_ratio_normalized(num, den);
}

CL_Obj cl_ratio_mul(CL_Obj a, CL_Obj b)
{
    /* a_n/a_d * b_n/b_d = (a_n*b_n) / (a_d*b_d) */
    CL_Obj an = cl_numerator(a);
    CL_Obj ad = cl_denominator(a);
    CL_Obj bn = cl_numerator(b);
    CL_Obj bd = cl_denominator(b);
    CL_Obj num, den;

    CL_GC_PROTECT(an); CL_GC_PROTECT(ad);
    CL_GC_PROTECT(bn); CL_GC_PROTECT(bd);

    num = cl_arith_mul(an, bn);
    CL_GC_UNPROTECT(4);

    CL_GC_PROTECT(num);
    CL_GC_PROTECT(ad);
    CL_GC_PROTECT(bd);
    den = cl_arith_mul(ad, bd);
    CL_GC_UNPROTECT(3);

    return cl_make_ratio_normalized(num, den);
}

CL_Obj cl_ratio_div(CL_Obj a, CL_Obj b)
{
    /* (a_n/a_d) / (b_n/b_d) = (a_n*b_d) / (a_d*b_n) */
    CL_Obj an = cl_numerator(a);
    CL_Obj ad = cl_denominator(a);
    CL_Obj bn = cl_numerator(b);
    CL_Obj bd = cl_denominator(b);
    CL_Obj num, den;

    if (cl_arith_zerop(bn)) {
        cl_error(CL_ERR_DIVZERO, "division by zero");
        return CL_NIL;
    }

    CL_GC_PROTECT(an); CL_GC_PROTECT(ad);
    CL_GC_PROTECT(bn); CL_GC_PROTECT(bd);

    num = cl_arith_mul(an, bd);
    CL_GC_UNPROTECT(4);

    CL_GC_PROTECT(num);
    CL_GC_PROTECT(ad);
    CL_GC_PROTECT(bn);
    den = cl_arith_mul(ad, bn);
    CL_GC_UNPROTECT(3);

    return cl_make_ratio_normalized(num, den);
}

CL_Obj cl_ratio_negate(CL_Obj a)
{
    if (CL_RATIO_P(a)) {
        CL_Ratio *r = (CL_Ratio *)CL_OBJ_TO_PTR(a);
        CL_Obj neg_num = cl_arith_negate(r->numerator);
        return cl_make_ratio(neg_num, r->denominator);
    }
    return cl_arith_negate(a);
}

CL_Obj cl_ratio_abs(CL_Obj a)
{
    if (CL_RATIO_P(a)) {
        CL_Ratio *r = (CL_Ratio *)CL_OBJ_TO_PTR(a);
        CL_Obj abs_num = cl_arith_abs(r->numerator);
        return cl_make_ratio(abs_num, r->denominator);
    }
    return cl_arith_abs(a);
}

/* --- Comparison --- */

int cl_ratio_compare(CL_Obj a, CL_Obj b)
{
    /* a_n/a_d vs b_n/b_d => compare a_n*b_d vs b_n*a_d
     * (denominators are always positive, so inequality direction is preserved) */
    CL_Obj an = cl_numerator(a);
    CL_Obj ad = cl_denominator(a);
    CL_Obj bn = cl_numerator(b);
    CL_Obj bd = cl_denominator(b);
    CL_Obj lhs, rhs;

    CL_GC_PROTECT(an); CL_GC_PROTECT(ad);
    CL_GC_PROTECT(bn); CL_GC_PROTECT(bd);

    lhs = cl_arith_mul(an, bd);
    rhs = cl_arith_mul(bn, ad);
    CL_GC_UNPROTECT(4);

    return cl_arith_compare(lhs, rhs);
}

int cl_ratio_zerop(CL_Obj a)
{
    if (CL_RATIO_P(a)) {
        CL_Ratio *r = (CL_Ratio *)CL_OBJ_TO_PTR(a);
        return cl_arith_zerop(r->numerator);
    }
    return cl_arith_zerop(a);
}

int cl_ratio_plusp(CL_Obj a)
{
    if (CL_RATIO_P(a)) {
        CL_Ratio *r = (CL_Ratio *)CL_OBJ_TO_PTR(a);
        /* Denominator is always positive, so sign = sign of numerator */
        return cl_arith_plusp(r->numerator);
    }
    return cl_arith_plusp(a);
}

int cl_ratio_minusp(CL_Obj a)
{
    if (CL_RATIO_P(a)) {
        CL_Ratio *r = (CL_Ratio *)CL_OBJ_TO_PTR(a);
        return cl_arith_minusp(r->numerator);
    }
    return cl_arith_minusp(a);
}

/* --- Equality and Hashing --- */

int cl_ratio_equal(CL_Obj a, CL_Obj b)
{
    CL_Ratio *ra, *rb;
    if (!CL_RATIO_P(a) || !CL_RATIO_P(b)) return 0;
    ra = (CL_Ratio *)CL_OBJ_TO_PTR(a);
    rb = (CL_Ratio *)CL_OBJ_TO_PTR(b);
    /* Normalized ratios: equal iff both components equal */
    return (cl_arith_compare(ra->numerator, rb->numerator) == 0 &&
            cl_arith_compare(ra->denominator, rb->denominator) == 0);
}

uint32_t cl_ratio_hash(CL_Obj obj)
{
    CL_Ratio *r;
    uint32_t h;
    if (!CL_RATIO_P(obj)) return 0;
    r = (CL_Ratio *)CL_OBJ_TO_PTR(obj);
    /* Combine hashes of numerator and denominator */
    h = 0x9e3779b9u;
    if (CL_FIXNUM_P(r->numerator))
        h ^= (uint32_t)CL_FIXNUM_VAL(r->numerator);
    else
        h ^= cl_bignum_hash(r->numerator);
    h *= 0x01000193u;
    if (CL_FIXNUM_P(r->denominator))
        h ^= (uint32_t)CL_FIXNUM_VAL(r->denominator);
    else
        h ^= cl_bignum_hash(r->denominator);
    return h;
}

/* --- Conversion --- */

double cl_ratio_to_double(CL_Obj obj)
{
    CL_Ratio *r;
    if (!CL_RATIO_P(obj)) return 0.0;
    r = (CL_Ratio *)CL_OBJ_TO_PTR(obj);
    return cl_to_double(r->numerator) / cl_to_double(r->denominator);
}
