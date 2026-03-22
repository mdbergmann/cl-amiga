/*
 * bignum.c — Arbitrary-precision integer arithmetic
 *
 * 16-bit limbs, little-endian order.
 * Multiplication inner loop uses only uint32_t intermediates.
 * All results normalized: demoted to fixnum when they fit.
 */

#include "bignum.h"
#include "ratio.h"
#include "float.h"
#include "mem.h"
#include "symbol.h"
#include "package.h"
#include "error.h"
#include "../platform/platform.h"
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Internal helpers on raw limb arrays
 * ================================================================ */

/* Compare magnitudes: returns -1, 0, +1 */
static int bignum_compare_mag(const uint16_t *a, uint32_t a_len,
                               const uint16_t *b, uint32_t b_len)
{
    uint32_t i;
    if (a_len != b_len)
        return a_len > b_len ? 1 : -1;
    for (i = a_len; i > 0; i--) {
        if (a[i-1] != b[i-1])
            return a[i-1] > b[i-1] ? 1 : -1;
    }
    return 0;
}

/* Unsigned addition: result must have space for max(a_len, b_len) + 1 limbs.
 * Returns number of result limbs. */
static uint32_t bignum_add_mag(const uint16_t *a, uint32_t a_len,
                                const uint16_t *b, uint32_t b_len,
                                uint16_t *result)
{
    uint32_t i, max_len;
    uint32_t carry = 0;

    /* Ensure a is longer */
    if (a_len < b_len) {
        const uint16_t *tmp = a; a = b; b = tmp;
        { uint32_t t = a_len; a_len = b_len; b_len = t; }
    }
    max_len = a_len;

    for (i = 0; i < b_len; i++) {
        uint32_t sum = (uint32_t)a[i] + (uint32_t)b[i] + carry;
        result[i] = (uint16_t)(sum & 0xFFFF);
        carry = sum >> 16;
    }
    for (; i < a_len; i++) {
        uint32_t sum = (uint32_t)a[i] + carry;
        result[i] = (uint16_t)(sum & 0xFFFF);
        carry = sum >> 16;
    }
    if (carry) {
        result[max_len] = (uint16_t)carry;
        return max_len + 1;
    }
    return max_len;
}

/* Unsigned subtraction: a >= b required. Returns number of result limbs. */
static uint32_t bignum_sub_mag(const uint16_t *a, uint32_t a_len,
                                const uint16_t *b, uint32_t b_len,
                                uint16_t *result)
{
    uint32_t i;
    uint32_t borrow = 0;

    for (i = 0; i < b_len; i++) {
        uint32_t diff = (uint32_t)a[i] - (uint32_t)b[i] - borrow;
        result[i] = (uint16_t)(diff & 0xFFFF);
        borrow = (diff >> 16) & 1;
    }
    for (; i < a_len; i++) {
        uint32_t diff = (uint32_t)a[i] - borrow;
        result[i] = (uint16_t)(diff & 0xFFFF);
        borrow = (diff >> 16) & 1;
    }
    /* Strip leading zeros */
    while (a_len > 1 && result[a_len - 1] == 0)
        a_len--;
    return a_len;
}

/* Schoolbook multiplication: result must have space for a_len + b_len limbs.
 * Returns number of result limbs (normalized). */
static uint32_t bignum_mul_mag(const uint16_t *a, uint32_t a_len,
                                const uint16_t *b, uint32_t b_len,
                                uint16_t *result)
{
    uint32_t i, j;
    uint32_t r_len = a_len + b_len;

    memset(result, 0, r_len * sizeof(uint16_t));

    for (i = 0; i < a_len; i++) {
        uint32_t carry = 0;
        for (j = 0; j < b_len; j++) {
            uint32_t prod = (uint32_t)a[i] * (uint32_t)b[j]
                          + (uint32_t)result[i + j] + carry;
            result[i + j] = (uint16_t)(prod & 0xFFFF);
            carry = prod >> 16;
        }
        result[i + b_len] += (uint16_t)carry;
    }

    /* Strip leading zeros */
    while (r_len > 1 && result[r_len - 1] == 0)
        r_len--;
    return r_len;
}

#ifdef PLATFORM_POSIX
/* ================================================================
 * 32-bit limb operations for 64-bit hosts.
 * On 64-bit hosts, uint64_t intermediates are native — using 32-bit
 * limbs halves the limb count and gives ~4x speedup for multiplication.
 * The CL_Bignum heap format stays 16-bit; we pack/unpack at boundaries.
 * ================================================================ */

/* Pack 16-bit limbs into 32-bit limbs (pairs).  Returns number of 32-bit limbs. */
static uint32_t pack_16_to_32(const uint16_t *src, uint32_t n16, uint32_t *dst)
{
    uint32_t n32 = (n16 + 1) / 2;
    uint32_t i;
    for (i = 0; i < n16 / 2; i++)
        dst[i] = (uint32_t)src[2*i] | ((uint32_t)src[2*i+1] << 16);
    if (n16 & 1)
        dst[n32 - 1] = (uint32_t)src[n16 - 1];
    return n32;
}

/* Unpack 32-bit limbs back to 16-bit limbs.  Returns number of 16-bit limbs. */
static uint32_t unpack_32_to_16(const uint32_t *src, uint32_t n32, uint16_t *dst)
{
    uint32_t i;
    uint32_t n16 = n32 * 2;
    for (i = 0; i < n32; i++) {
        dst[2*i]     = (uint16_t)(src[i] & 0xFFFF);
        dst[2*i + 1] = (uint16_t)(src[i] >> 16);
    }
    /* Strip trailing zero limbs */
    while (n16 > 1 && dst[n16 - 1] == 0)
        n16--;
    return n16;
}

/* Schoolbook multiplication with 32-bit limbs and 64-bit intermediates */
static uint32_t bignum_mul_mag32(const uint32_t *a, uint32_t a_len,
                                  const uint32_t *b, uint32_t b_len,
                                  uint32_t *result)
{
    uint32_t i, j;
    uint32_t r_len = a_len + b_len;

    memset(result, 0, r_len * sizeof(uint32_t));

    for (i = 0; i < a_len; i++) {
        uint64_t carry = 0;
        for (j = 0; j < b_len; j++) {
            uint64_t prod = (uint64_t)a[i] * (uint64_t)b[j]
                          + (uint64_t)result[i + j] + carry;
            result[i + j] = (uint32_t)(prod & 0xFFFFFFFFULL);
            carry = prod >> 32;
        }
        result[i + b_len] += (uint32_t)carry;
    }

    while (r_len > 1 && result[r_len - 1] == 0)
        r_len--;
    return r_len;
}
#endif /* PLATFORM_POSIX */

/* Single-limb division: divide a[a_len] by d (uint16_t).
 * Quotient in quot, returns remainder. */
static uint16_t bignum_div_single(const uint16_t *a, uint32_t a_len,
                                   uint16_t d, uint16_t *quot)
{
    uint32_t i;
    uint32_t rem = 0;

    for (i = a_len; i > 0; i--) {
        uint32_t cur = (rem << 16) | (uint32_t)a[i - 1];
        quot[i - 1] = (uint16_t)(cur / d);
        rem = cur % d;
    }
    return (uint16_t)rem;
}

/* Multi-limb division (Knuth Algorithm D, simplified).
 * Computes quotient and remainder: a / b.
 * quot must have space for a_len - b_len + 1 limbs.
 * rem must have space for b_len limbs.
 * Returns quotient length (normalized). */
static uint32_t bignum_divmod(const uint16_t *a, uint32_t a_len,
                               const uint16_t *b, uint32_t b_len,
                               uint16_t *quot, uint16_t *rem)
{
    uint32_t q_len;

    /* Single-limb divisor: use fast path */
    if (b_len == 1) {
        uint16_t r;
        q_len = a_len;
        r = bignum_div_single(a, a_len, b[0], quot);
        rem[0] = r;
        while (q_len > 1 && quot[q_len - 1] == 0)
            q_len--;
        return q_len;
    }

    /* Multi-limb: Knuth Algorithm D */
    {
        /* Normalize: shift so that b's top limb has high bit set */
        uint16_t *u, *v;
        uint32_t n = b_len, m = a_len - b_len;
        uint32_t shift = 0;
        uint16_t bh = b[b_len - 1];
        uint32_t i, j;

        while (bh < 0x8000) { bh <<= 1; shift++; }

        /* Allocate normalized copies on stack (limited size for safety) */
        {
            uint16_t u_buf[256], v_buf[128]; /* Should be enough for practical use */
            if (a_len + 1 > 256 || b_len > 128) {
                /* Fallback: very large division — should not happen in practice */
                memset(quot, 0, (m + 1) * sizeof(uint16_t));
                memset(rem, 0, b_len * sizeof(uint16_t));
                return 1;
            }
            u = u_buf;
            v = v_buf;

            /* Shift b left by 'shift' bits */
            {
                uint32_t carry = 0;
                for (i = 0; i < n; i++) {
                    uint32_t val = ((uint32_t)b[i] << shift) | carry;
                    v[i] = (uint16_t)(val & 0xFFFF);
                    carry = val >> 16;
                }
            }

            /* Shift a left by 'shift' bits */
            {
                uint32_t carry = 0;
                for (i = 0; i < a_len; i++) {
                    uint32_t val = ((uint32_t)a[i] << shift) | carry;
                    u[i] = (uint16_t)(val & 0xFFFF);
                    carry = val >> 16;
                }
                u[a_len] = (uint16_t)carry;
            }

            memset(quot, 0, (m + 1) * sizeof(uint16_t));

            for (j = m + 1; j > 0; j--) {
                uint32_t qhat, rhat;
                uint32_t idx = j - 1;  /* current quotient digit index */

                /* Estimate qhat */
                {
                    uint32_t u_hi = ((uint32_t)u[idx + n] << 16) | (uint32_t)u[idx + n - 1];
                    qhat = u_hi / (uint32_t)v[n - 1];
                    rhat = u_hi % (uint32_t)v[n - 1];
                }

                /* Refine qhat */
                while (qhat >= 0x10000 ||
                       (n >= 2 && qhat * (uint32_t)v[n - 2] >
                        ((rhat << 16) | (uint32_t)u[idx + n - 2]))) {
                    qhat--;
                    rhat += (uint32_t)v[n - 1];
                    if (rhat >= 0x10000) break;
                }

                /* Multiply and subtract */
                {
                    uint32_t carry2 = 0;
                    int32_t borrow2 = 0;
                    for (i = 0; i < n; i++) {
                        uint32_t prod = qhat * (uint32_t)v[i] + carry2;
                        carry2 = prod >> 16;
                        {
                            int32_t diff = (int32_t)(uint32_t)u[idx + i]
                                         - (int32_t)(prod & 0xFFFF) + borrow2;
                            u[idx + i] = (uint16_t)(diff & 0xFFFF);
                            borrow2 = diff >> 16;
                            if (borrow2 < -1) borrow2 = -1;
                        }
                    }
                    {
                        int32_t diff = (int32_t)(uint32_t)u[idx + n]
                                     - (int32_t)carry2 + borrow2;
                        u[idx + n] = (uint16_t)(diff & 0xFFFF);
                        borrow2 = diff >> 16;
                    }

                    quot[idx] = (uint16_t)qhat;

                    /* If negative, add back */
                    if (borrow2 < 0) {
                        uint32_t carry3 = 0;
                        quot[idx]--;
                        for (i = 0; i < n; i++) {
                            uint32_t sum = (uint32_t)u[idx + i] + (uint32_t)v[i] + carry3;
                            u[idx + i] = (uint16_t)(sum & 0xFFFF);
                            carry3 = sum >> 16;
                        }
                        u[idx + n] += (uint16_t)carry3;
                    }
                }
            }

            /* Un-shift remainder */
            if (shift > 0) {
                uint32_t carry = 0;
                for (i = n; i > 0; i--) {
                    uint32_t val = ((uint32_t)u[i-1]) | (carry << 16);
                    rem[i-1] = (uint16_t)(val >> shift);
                    carry = u[i-1] & (uint16_t)((1u << shift) - 1);
                }
            } else {
                memcpy(rem, u, n * sizeof(uint16_t));
            }
        }

        q_len = m + 1;
        while (q_len > 1 && quot[q_len - 1] == 0)
            q_len--;
        return q_len;
    }
}

/* ================================================================
 * Bignum ↔ fixnum conversion and normalization
 * ================================================================ */

/* Convert fixnum or bignum to bignum limbs.
 * Returns a pointer to the bignum's limbs and sets *len and *sign.
 * For fixnums, stores limbs in tmp_buf (which must have 2 elements). */
static const uint16_t *to_limbs(CL_Obj obj, uint32_t *len, uint32_t *sign,
                                 uint16_t *tmp_buf)
{
    if (CL_FIXNUM_P(obj)) {
        int32_t val = CL_FIXNUM_VAL(obj);
        uint32_t uval;
        if (val < 0) {
            *sign = 1;
            uval = (uint32_t)(-(int32_t)val);
        } else {
            *sign = 0;
            uval = (uint32_t)val;
        }
        tmp_buf[0] = (uint16_t)(uval & 0xFFFF);
        tmp_buf[1] = (uint16_t)(uval >> 16);
        *len = (tmp_buf[1] != 0) ? 2 : (tmp_buf[0] != 0 ? 1 : 1);
        return tmp_buf;
    } else {
        CL_Bignum *bn = (CL_Bignum *)CL_OBJ_TO_PTR(obj);
        *len = bn->length;
        *sign = bn->sign;
        return bn->limbs;
    }
}

CL_Obj cl_bignum_normalize(CL_Obj obj)
{
    CL_Bignum *bn;
    uint32_t len;
    uint32_t val;
    int32_t sval;

    if (!CL_BIGNUM_P(obj)) return obj;

    bn = (CL_Bignum *)CL_OBJ_TO_PTR(obj);
    len = bn->length;

    /* Strip leading zero limbs */
    while (len > 1 && bn->limbs[len - 1] == 0)
        len--;
    bn->length = len;

    /* Check if fits in fixnum */
    if (len == 1) {
        val = bn->limbs[0];
    } else if (len == 2) {
        val = (uint32_t)bn->limbs[0] | ((uint32_t)bn->limbs[1] << 16);
    } else {
        return obj;  /* Too large for fixnum */
    }

    /* Zero is always positive fixnum */
    if (val == 0) return CL_MAKE_FIXNUM(0);

    if (bn->sign == 0) {
        /* Positive: must fit in CL_FIXNUM_MAX (0x3FFFFFFF) */
        if (val <= (uint32_t)CL_FIXNUM_MAX)
            return CL_MAKE_FIXNUM((int32_t)val);
    } else {
        /* Negative: must fit in -CL_FIXNUM_MIN (0x40000000) */
        if (val <= (uint32_t)0x40000000u) {
            sval = -(int32_t)val;
            return CL_MAKE_FIXNUM(sval);
        }
    }
    return obj;
}

CL_Obj cl_bignum_from_int32(int32_t val)
{
    CL_Obj obj;
    CL_Bignum *bn;
    uint32_t uval;
    uint32_t sign;

    /* Try fixnum first */
    if (val >= CL_FIXNUM_MIN && val <= CL_FIXNUM_MAX)
        return CL_MAKE_FIXNUM(val);

    sign = val < 0 ? 1 : 0;
    uval = val < 0 ? (uint32_t)(-(int32_t)val) : (uint32_t)val;

    obj = cl_make_bignum(2, sign);
    bn = (CL_Bignum *)CL_OBJ_TO_PTR(obj);
    bn->limbs[0] = (uint16_t)(uval & 0xFFFF);
    bn->limbs[1] = (uint16_t)(uval >> 16);
    return cl_bignum_normalize(obj);
}

/* Create a bignum from unsigned 32-bit value (public, sign=0) */
CL_Obj cl_bignum_from_uint32(uint32_t val)
{
    CL_Obj obj;
    CL_Bignum *bn;

    if (val == 0) return CL_MAKE_FIXNUM(0);
    if (val <= (uint32_t)CL_FIXNUM_MAX)
        return CL_MAKE_FIXNUM((int32_t)val);

    obj = cl_make_bignum(2, 0);
    bn = (CL_Bignum *)CL_OBJ_TO_PTR(obj);
    bn->limbs[0] = (uint16_t)(val & 0xFFFF);
    bn->limbs[1] = (uint16_t)(val >> 16);
    return cl_bignum_normalize(obj);
}

/* Create a bignum from unsigned 32-bit value with sign */
static CL_Obj bignum_from_uint32(uint32_t val, uint32_t sign)
{
    CL_Obj obj;
    CL_Bignum *bn;

    if (val == 0) return CL_MAKE_FIXNUM(0);

    /* Check if fits in fixnum */
    if (sign == 0 && val <= (uint32_t)CL_FIXNUM_MAX)
        return CL_MAKE_FIXNUM((int32_t)val);
    if (sign == 1 && val <= (uint32_t)0x40000000u)
        return CL_MAKE_FIXNUM(-(int32_t)val);

    obj = cl_make_bignum(2, sign);
    bn = (CL_Bignum *)CL_OBJ_TO_PTR(obj);
    bn->limbs[0] = (uint16_t)(val & 0xFFFF);
    bn->limbs[1] = (uint16_t)(val >> 16);
    return cl_bignum_normalize(obj);
}

/* Build a bignum from limb array, normalize */
static CL_Obj bignum_from_limbs(const uint16_t *limbs, uint32_t len,
                                 uint32_t sign)
{
    CL_Obj obj;
    CL_Bignum *bn;

    /* Strip leading zeros */
    while (len > 1 && limbs[len - 1] == 0)
        len--;

    /* Zero? */
    if (len == 1 && limbs[0] == 0)
        return CL_MAKE_FIXNUM(0);

    obj = cl_make_bignum(len, sign);
    bn = (CL_Bignum *)CL_OBJ_TO_PTR(obj);
    memcpy(bn->limbs, limbs, len * sizeof(uint16_t));
    return cl_bignum_normalize(obj);
}

/* ================================================================
 * Comparison
 * ================================================================ */

int cl_bignum_equal(CL_Obj a, CL_Obj b)
{
    CL_Bignum *ba, *bb;
    uint32_t i;

    if (!CL_BIGNUM_P(a) || !CL_BIGNUM_P(b)) return 0;

    ba = (CL_Bignum *)CL_OBJ_TO_PTR(a);
    bb = (CL_Bignum *)CL_OBJ_TO_PTR(b);

    if (ba->sign != bb->sign) return 0;
    if (ba->length != bb->length) return 0;
    for (i = 0; i < ba->length; i++) {
        if (ba->limbs[i] != bb->limbs[i]) return 0;
    }
    return 1;
}

uint32_t cl_bignum_hash(CL_Obj obj)
{
    CL_Bignum *bn = (CL_Bignum *)CL_OBJ_TO_PTR(obj);
    uint32_t h = 0;
    uint32_t i;

    /* Hash sign */
    h = ((h << 5) | (h >> 27)) ^ bn->sign;

    /* Hash limbs — rotate-XOR (no multiply, fast on 68020) */
    for (i = 0; i < bn->length; i++) {
        h = ((h << 5) | (h >> 27)) ^ bn->limbs[i];
    }
    return h;
}

/* ================================================================
 * Arithmetic dispatch
 * ================================================================ */

/* Ensure integer type, signal error if not */
static void check_integer(CL_Obj obj, const char *op)
{
    if (!CL_FIXNUM_P(obj) && !CL_BIGNUM_P(obj))
        cl_error(CL_ERR_TYPE, "%s: not an integer", op);
}

CL_Obj cl_arith_add(CL_Obj a, CL_Obj b)
{
    /* Float path: either operand is float → float result */
    if (CL_FLOATP(a) || CL_FLOATP(b))
        return cl_float_add(a, b);

    /* Ratio path: either operand is ratio → ratio result */
    if (CL_RATIO_P(a) || CL_RATIO_P(b))
        return cl_ratio_add(a, b);

    /* Fast path: both fixnums */
    if (CL_FIXNUM_P(a) && CL_FIXNUM_P(b)) {
        int32_t va = CL_FIXNUM_VAL(a);
        int32_t vb = CL_FIXNUM_VAL(b);
        int32_t sum = va + vb;
        if (sum >= CL_FIXNUM_MIN && sum <= CL_FIXNUM_MAX)
            return CL_MAKE_FIXNUM(sum);
        /* Overflow: fall through to bignum path */
    }

    {
        uint16_t ta[2], tb[2];
        uint32_t a_len, b_len, a_sign, b_sign;
        const uint16_t *al, *bl;
        uint16_t result_stack[256];
        uint16_t *result;
        uint32_t r_len, max_len;
        int heap_result = 0;

        al = to_limbs(a, &a_len, &a_sign, ta);
        bl = to_limbs(b, &b_len, &b_sign, tb);

        max_len = (a_len > b_len ? a_len : b_len) + 1;
        if (max_len <= 256) {
            result = result_stack;
        } else {
            result = (uint16_t *)platform_alloc(max_len * sizeof(uint16_t));
            heap_result = 1;
        }

        if (a_sign == b_sign) {
            /* Same sign: add magnitudes */
            CL_Obj res;
            r_len = bignum_add_mag(al, a_len, bl, b_len, result);
            res = bignum_from_limbs(result, r_len, a_sign);
            if (heap_result) platform_free(result);
            return res;
        } else {
            /* Different signs: subtract smaller from larger magnitude */
            int cmp = bignum_compare_mag(al, a_len, bl, b_len);
            CL_Obj res;
            if (cmp == 0) { if (heap_result) platform_free(result); return CL_MAKE_FIXNUM(0); }
            if (cmp > 0) {
                r_len = bignum_sub_mag(al, a_len, bl, b_len, result);
                res = bignum_from_limbs(result, r_len, a_sign);
            } else {
                r_len = bignum_sub_mag(bl, b_len, al, a_len, result);
                res = bignum_from_limbs(result, r_len, b_sign);
            }
            if (heap_result) platform_free(result);
            return res;
        }
    }
}

CL_Obj cl_arith_sub(CL_Obj a, CL_Obj b)
{
    /* Float path */
    if (CL_FLOATP(a) || CL_FLOATP(b))
        return cl_float_sub(a, b);

    /* Ratio path */
    if (CL_RATIO_P(a) || CL_RATIO_P(b))
        return cl_ratio_sub(a, b);

    /* Fast path: both fixnums */
    if (CL_FIXNUM_P(a) && CL_FIXNUM_P(b)) {
        int32_t va = CL_FIXNUM_VAL(a);
        int32_t vb = CL_FIXNUM_VAL(b);
        int32_t diff = va - vb;
        if (diff >= CL_FIXNUM_MIN && diff <= CL_FIXNUM_MAX)
            return CL_MAKE_FIXNUM(diff);
    }

    {
        uint16_t ta[2], tb[2];
        uint32_t a_len, b_len, a_sign, b_sign;
        const uint16_t *al, *bl;
        uint16_t result_stack[256];
        uint16_t *result;
        uint32_t r_len, max_len;
        int heap_result = 0;

        al = to_limbs(a, &a_len, &a_sign, ta);
        bl = to_limbs(b, &b_len, &b_sign, tb);

        /* Flip sign of b, then add */
        b_sign = b_sign ? 0 : 1;

        max_len = (a_len > b_len ? a_len : b_len) + 1;
        if (max_len <= 256) {
            result = result_stack;
        } else {
            result = (uint16_t *)platform_alloc(max_len * sizeof(uint16_t));
            heap_result = 1;
        }

        if (a_sign == b_sign) {
            CL_Obj res;
            r_len = bignum_add_mag(al, a_len, bl, b_len, result);
            res = bignum_from_limbs(result, r_len, a_sign);
            if (heap_result) platform_free(result);
            return res;
        } else {
            int cmp = bignum_compare_mag(al, a_len, bl, b_len);
            CL_Obj res;
            if (cmp == 0) { if (heap_result) platform_free(result); return CL_MAKE_FIXNUM(0); }
            if (cmp > 0) {
                r_len = bignum_sub_mag(al, a_len, bl, b_len, result);
                res = bignum_from_limbs(result, r_len, a_sign);
            } else {
                r_len = bignum_sub_mag(bl, b_len, al, a_len, result);
                res = bignum_from_limbs(result, r_len, b_sign);
            }
            if (heap_result) platform_free(result);
            return res;
        }
    }
}

CL_Obj cl_arith_mul(CL_Obj a, CL_Obj b)
{
    /* Float path */
    if (CL_FLOATP(a) || CL_FLOATP(b))
        return cl_float_mul(a, b);

    /* Ratio path */
    if (CL_RATIO_P(a) || CL_RATIO_P(b))
        return cl_ratio_mul(a, b);

    /* Fast path: both fixnums, check for overflow */
    if (CL_FIXNUM_P(a) && CL_FIXNUM_P(b)) {
        int32_t va = CL_FIXNUM_VAL(a);
        int32_t vb = CL_FIXNUM_VAL(b);
        /* Use wider intermediate to detect overflow */
        uint32_t ua, ub;
        uint32_t prod_lo;
        int neg;

        if (va == 0 || vb == 0) return CL_MAKE_FIXNUM(0);
        if (va == 1) return b;
        if (vb == 1) return a;

        /* Compute 32x32 → check if fits in 31 bits */
        neg = (va < 0) != (vb < 0);
        ua = va < 0 ? (uint32_t)(-va) : (uint32_t)va;
        ub = vb < 0 ? (uint32_t)(-vb) : (uint32_t)vb;

        /* Check for 32-bit overflow: if either > 0xFFFF and the other > 1,
         * the product might not fit in 32 bits. Use 16x16 decomposition. */
        {
            uint32_t a_hi = ua >> 16, a_lo = ua & 0xFFFF;
            uint32_t b_hi = ub >> 16, b_lo = ub & 0xFFFF;

            /* If both have high parts, guaranteed > 32 bits */
            if (a_hi && b_hi) goto bignum_mul;

            prod_lo = a_lo * b_lo;
            if (a_hi) prod_lo += (a_hi * b_lo) << 16;
            else if (b_hi) prod_lo += (a_lo * b_hi) << 16;

            /* Check for overflow in those additions or if cross terms overflow */
            if (a_hi && a_hi * b_lo > 0xFFFF) goto bignum_mul;
            if (b_hi && a_lo * b_hi > 0xFFFF) goto bignum_mul;

            /* Check fixnum range */
            if (!neg && prod_lo <= (uint32_t)CL_FIXNUM_MAX)
                return CL_MAKE_FIXNUM((int32_t)prod_lo);
            if (neg && prod_lo <= (uint32_t)0x40000000u)
                return CL_MAKE_FIXNUM(-(int32_t)prod_lo);

            /* Fits in uint32_t but not fixnum */
            return bignum_from_uint32(prod_lo, neg ? 1 : 0);
        }
    }

bignum_mul:
    {
        uint16_t ta[2], tb[2];
        uint32_t a_len, b_len, a_sign, b_sign;
        const uint16_t *al, *bl;
        uint32_t r_sign;

        al = to_limbs(a, &a_len, &a_sign, ta);
        bl = to_limbs(b, &b_len, &b_sign, tb);

        /* Check for zero */
        if (a_len == 1 && al[0] == 0) return CL_MAKE_FIXNUM(0);
        if (b_len == 1 && bl[0] == 0) return CL_MAKE_FIXNUM(0);

        r_sign = a_sign ^ b_sign;

#ifdef PLATFORM_POSIX
        /* 32-bit limb fast path: pack 16→32, multiply with uint64_t, unpack */
        {
            uint32_t a32_len = (a_len + 1) / 2;
            uint32_t b32_len = (b_len + 1) / 2;
            uint32_t r32_max = a32_len + b32_len;
            uint32_t r32_len;

            /* Use stack buffers for small operands, heap for large */
            if (r32_max <= 256) {
                uint32_t a32[256], b32[256], r32[256];
                uint16_t r16[512];
                uint32_t r16_len;

                a32_len = pack_16_to_32(al, a_len, a32);
                b32_len = pack_16_to_32(bl, b_len, b32);
                r32_len = bignum_mul_mag32(a32, a32_len, b32, b32_len, r32);
                r16_len = unpack_32_to_16(r32, r32_len, r16);
                return bignum_from_limbs(r16, r16_len, r_sign);
            } else {
                uint32_t *ha32 = (uint32_t *)platform_alloc(a32_len * sizeof(uint32_t));
                uint32_t *hb32 = (uint32_t *)platform_alloc(b32_len * sizeof(uint32_t));
                uint32_t *hr32;
                uint16_t *hr16;
                uint32_t r16_len;
                CL_Obj res;

                a32_len = pack_16_to_32(al, a_len, ha32);
                b32_len = pack_16_to_32(bl, b_len, hb32);
                hr32 = (uint32_t *)platform_alloc(r32_max * sizeof(uint32_t));
                r32_len = bignum_mul_mag32(ha32, a32_len, hb32, b32_len, hr32);
                platform_free(ha32);
                platform_free(hb32);

                hr16 = (uint16_t *)platform_alloc(r32_len * 2 * sizeof(uint16_t));
                r16_len = unpack_32_to_16(hr32, r32_len, hr16);
                platform_free(hr32);

                res = bignum_from_limbs(hr16, r16_len, r_sign);
                platform_free(hr16);
                return res;
            }
        }
#else
        /* 16-bit limb path (Amiga / 68020) */
        {
            uint16_t result[256];
            uint32_t r_len;

            if (a_len + b_len > 256) {
                uint16_t *heap_result = (uint16_t *)platform_alloc((a_len + b_len) * sizeof(uint16_t));
                CL_Obj res;
                r_len = bignum_mul_mag(al, a_len, bl, b_len, heap_result);
                res = bignum_from_limbs(heap_result, r_len, r_sign);
                platform_free(heap_result);
                return res;
            }

            r_len = bignum_mul_mag(al, a_len, bl, b_len, result);
            return bignum_from_limbs(result, r_len, r_sign);
        }
#endif
    }
}

CL_Obj cl_arith_truncate(CL_Obj a, CL_Obj b)
{
    check_integer(b, "TRUNCATE");

    /* Fast path: both fixnums */
    if (CL_FIXNUM_P(a) && CL_FIXNUM_P(b)) {
        int32_t va = CL_FIXNUM_VAL(a);
        int32_t vb = CL_FIXNUM_VAL(b);
        if (vb == 0) cl_error(CL_ERR_DIVZERO, "Division by zero");
        return CL_MAKE_FIXNUM(va / vb);  /* C truncation toward zero */
    }

    {
        uint16_t ta[2], tb[2];
        uint32_t a_len, b_len, a_sign, b_sign, r_sign;
        const uint16_t *al, *bl;
        uint16_t quot_stack[256], rem_stack[256];
        uint16_t *quot, *rem;
        uint32_t q_len;
        int heap_bufs = 0;

        al = to_limbs(a, &a_len, &a_sign, ta);
        bl = to_limbs(b, &b_len, &b_sign, tb);

        /* Check for division by zero */
        if (b_len == 1 && bl[0] == 0)
            cl_error(CL_ERR_DIVZERO, "Division by zero");

        /* If |a| < |b|, quotient is 0 */
        if (bignum_compare_mag(al, a_len, bl, b_len) < 0)
            return CL_MAKE_FIXNUM(0);

        if (a_len > 256) {
            quot = (uint16_t *)platform_alloc(a_len * sizeof(uint16_t));
            rem = (uint16_t *)platform_alloc(a_len * sizeof(uint16_t));
            heap_bufs = 1;
        } else {
            quot = quot_stack;
            rem = rem_stack;
        }

        q_len = bignum_divmod(al, a_len, bl, b_len, quot, rem);
        r_sign = a_sign ^ b_sign;
        {
            CL_Obj res = bignum_from_limbs(quot, q_len, r_sign);
            if (heap_bufs) { platform_free(quot); platform_free(rem); }
            return res;
        }
    }
}

CL_Obj cl_arith_mod(CL_Obj a, CL_Obj b)
{
    CL_Obj rem_obj;

    check_integer(b, "MOD");

    /* Fast path: both fixnums */
    if (CL_FIXNUM_P(a) && CL_FIXNUM_P(b)) {
        int32_t va = CL_FIXNUM_VAL(a);
        int32_t vb = CL_FIXNUM_VAL(b);
        int32_t r;
        if (vb == 0) cl_error(CL_ERR_DIVZERO, "Division by zero");
        r = va % vb;
        /* CL mod: result has same sign as divisor */
        if (r != 0 && ((r < 0) != (vb < 0)))
            r += vb;
        return CL_MAKE_FIXNUM(r);
    }

    {
        uint16_t ta[2], tb[2];
        uint32_t a_len, b_len, a_sign, b_sign;
        const uint16_t *al, *bl;
        uint16_t quot_stack[256], rem_stack[256];
        uint16_t *quot, *rem;
        uint32_t rem_len;
        int rem_is_zero;
        int heap_bufs = 0;

        al = to_limbs(a, &a_len, &a_sign, ta);
        bl = to_limbs(b, &b_len, &b_sign, tb);

        if (b_len == 1 && bl[0] == 0)
            cl_error(CL_ERR_DIVZERO, "Division by zero");

        /* If |a| < |b|, remainder is a (with sign of a) */
        if (bignum_compare_mag(al, a_len, bl, b_len) < 0) {
            rem_obj = a;
        } else {
            if (a_len > 256) {
                quot = (uint16_t *)platform_alloc(a_len * sizeof(uint16_t));
                rem = (uint16_t *)platform_alloc(a_len * sizeof(uint16_t));
                heap_bufs = 1;
            } else {
                quot = quot_stack;
                rem = rem_stack;
            }
            bignum_divmod(al, a_len, bl, b_len, quot, rem);
            /* Find actual remainder length */
            rem_len = b_len;
            while (rem_len > 1 && rem[rem_len - 1] == 0)
                rem_len--;
            rem_obj = bignum_from_limbs(rem, rem_len, a_sign);
            if (heap_bufs) { platform_free(quot); platform_free(rem); }
        }

        /* CL mod: result has same sign as divisor.
         * If rem != 0 and signs differ, adjust: rem = rem + b */
        rem_is_zero = CL_FIXNUM_P(rem_obj) && CL_FIXNUM_VAL(rem_obj) == 0;
        if (!rem_is_zero) {
            /* Check sign mismatch */
            int rem_neg = CL_FIXNUM_P(rem_obj) ? CL_FIXNUM_VAL(rem_obj) < 0
                          : ((CL_Bignum *)CL_OBJ_TO_PTR(rem_obj))->sign;
            int b_neg = CL_FIXNUM_P(b) ? CL_FIXNUM_VAL(b) < 0
                        : ((CL_Bignum *)CL_OBJ_TO_PTR(b))->sign != 0;
            if (rem_neg != b_neg) {
                rem_obj = cl_arith_add(rem_obj, b);
            }
        }

        return rem_obj;
    }
}

CL_Obj cl_arith_negate(CL_Obj a)
{
    if (CL_FLOATP(a))
        return cl_float_negate(a);

    if (CL_RATIO_P(a))
        return cl_ratio_negate(a);

    if (CL_FIXNUM_P(a)) {
        int32_t val = CL_FIXNUM_VAL(a);
        /* CL_FIXNUM_MIN negated overflows fixnum range */
        if (val == CL_FIXNUM_MIN)
            return bignum_from_uint32((uint32_t)0x40000000u, 0);
        return CL_MAKE_FIXNUM(-val);
    }
    {
        CL_Bignum *bn = (CL_Bignum *)CL_OBJ_TO_PTR(a);
        CL_Obj result = cl_make_bignum(bn->length, bn->sign ? 0 : 1);
        CL_Bignum *rbn = (CL_Bignum *)CL_OBJ_TO_PTR(result);
        memcpy(rbn->limbs, bn->limbs, bn->length * sizeof(uint16_t));
        return cl_bignum_normalize(result);
    }
}

CL_Obj cl_arith_abs(CL_Obj a)
{
    if (CL_FLOATP(a)) {
        double v = cl_to_double(a);
        if (v < 0.0) v = -v;
        if (CL_DOUBLE_FLOAT_P(a))
            return cl_make_double_float(v);
        return cl_make_single_float((float)v);
    }

    if (CL_RATIO_P(a))
        return cl_ratio_abs(a);

    if (CL_FIXNUM_P(a)) {
        int32_t val = CL_FIXNUM_VAL(a);
        if (val == CL_FIXNUM_MIN)
            return bignum_from_uint32((uint32_t)0x40000000u, 0);
        return CL_MAKE_FIXNUM(val < 0 ? -val : val);
    }
    {
        CL_Bignum *bn = (CL_Bignum *)CL_OBJ_TO_PTR(a);
        if (bn->sign == 0) return a;
        {
            CL_Obj result = cl_make_bignum(bn->length, 0);
            CL_Bignum *rbn = (CL_Bignum *)CL_OBJ_TO_PTR(result);
            memcpy(rbn->limbs, bn->limbs, bn->length * sizeof(uint16_t));
            return result;
        }
    }
}

int cl_arith_compare(CL_Obj a, CL_Obj b)
{
    /* Float path */
    if (CL_FLOATP(a) || CL_FLOATP(b))
        return cl_float_compare(a, b);

    /* Ratio path */
    if (CL_RATIO_P(a) || CL_RATIO_P(b))
        return cl_ratio_compare(a, b);

    /* Fast path: both fixnums */
    if (CL_FIXNUM_P(a) && CL_FIXNUM_P(b)) {
        int32_t va = CL_FIXNUM_VAL(a);
        int32_t vb = CL_FIXNUM_VAL(b);
        if (va < vb) return -1;
        if (va > vb) return 1;
        return 0;
    }

    {
        uint16_t ta[2], tb[2];
        uint32_t a_len, b_len, a_sign, b_sign;
        const uint16_t *al, *bl;

        al = to_limbs(a, &a_len, &a_sign, ta);
        bl = to_limbs(b, &b_len, &b_sign, tb);

        /* Different signs */
        if (a_sign != b_sign) {
            /* Check for zero */
            int a_zero = (a_len == 1 && al[0] == 0);
            int b_zero = (b_len == 1 && bl[0] == 0);
            if (a_zero && b_zero) return 0;
            return a_sign ? -1 : 1;
        }

        /* Same sign: compare magnitudes */
        {
            int cmp = bignum_compare_mag(al, a_len, bl, b_len);
            if (a_sign) cmp = -cmp;  /* Negative: reverse order */
            return cmp;
        }
    }
}

int cl_arith_zerop(CL_Obj a)
{
    if (CL_FLOATP(a)) return cl_float_zerop(a);
    if (CL_RATIO_P(a)) return cl_ratio_zerop(a);
    if (CL_FIXNUM_P(a)) return CL_FIXNUM_VAL(a) == 0;
    /* Normalized bignums are never zero (they'd be fixnum 0) */
    return 0;
}

int cl_arith_plusp(CL_Obj a)
{
    if (CL_FLOATP(a)) return cl_float_plusp(a);
    if (CL_RATIO_P(a)) return cl_ratio_plusp(a);
    if (CL_FIXNUM_P(a)) return CL_FIXNUM_VAL(a) > 0;
    return ((CL_Bignum *)CL_OBJ_TO_PTR(a))->sign == 0;
}

int cl_arith_minusp(CL_Obj a)
{
    if (CL_FLOATP(a)) return cl_float_minusp(a);
    if (CL_RATIO_P(a)) return cl_ratio_minusp(a);
    if (CL_FIXNUM_P(a)) return CL_FIXNUM_VAL(a) < 0;
    return ((CL_Bignum *)CL_OBJ_TO_PTR(a))->sign != 0;
}

/* ================================================================
 * String conversion
 * ================================================================ */

CL_Obj cl_bignum_from_string(const char *str, int len, int negative)
{
    /* Horner's method: accumulator = accumulator * 10 + digit */
    uint16_t acc[256];
    uint32_t acc_len = 1;
    int i;

    memset(acc, 0, sizeof(acc));

    for (i = 0; i < len; i++) {
        uint32_t digit;
        uint32_t carry = 0;
        uint32_t j;

        if (str[i] < '0' || str[i] > '9') continue;
        digit = (uint32_t)(str[i] - '0');

        /* Multiply acc by 10 */
        for (j = 0; j < acc_len; j++) {
            uint32_t prod = (uint32_t)acc[j] * 10 + carry;
            acc[j] = (uint16_t)(prod & 0xFFFF);
            carry = prod >> 16;
        }
        if (carry) {
            if (acc_len < 256) acc[acc_len++] = (uint16_t)carry;
        }

        /* Add digit */
        carry = digit;
        for (j = 0; j < acc_len && carry; j++) {
            uint32_t sum = (uint32_t)acc[j] + carry;
            acc[j] = (uint16_t)(sum & 0xFFFF);
            carry = sum >> 16;
        }
        if (carry) {
            if (acc_len < 256) acc[acc_len++] = (uint16_t)carry;
        }
    }

    return bignum_from_limbs(acc, acc_len, negative ? 1 : 0);
}

void cl_bignum_print(CL_Obj obj, void (*out)(const char *))
{
    cl_bignum_print_base(obj, 10, out);
}

void cl_bignum_print_base(CL_Obj obj, int32_t base, void (*out)(const char *))
{
    static const char digit_chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    CL_Bignum *bn = (CL_Bignum *)CL_OBJ_TO_PTR(obj);
    uint16_t work_stack[256];
    uint16_t *work;
    uint32_t work_len;
    char digits_stack[1024];
    char *digits;
    uint32_t digits_cap;
    int dpos = 0;
    int i;
    int heap_work = 0, heap_digits = 0;

    if (base < 2) base = 10;
    if (base > 36) base = 10;

    /* Use stack buffers for small bignums, heap for large */
    if (bn->length <= 256) {
        work = work_stack;
    } else {
        work = (uint16_t *)platform_alloc(bn->length * sizeof(uint16_t));
        heap_work = 1;
    }
    /* Each 16-bit limb can produce up to 16 binary digits; base 2 is worst case */
    digits_cap = bn->length * 16 + 2;
    if (digits_cap <= 1024) {
        digits = digits_stack;
    } else {
        digits = (char *)platform_alloc(digits_cap);
        heap_digits = 1;
    }

    memcpy(work, bn->limbs, bn->length * sizeof(uint16_t));
    work_len = bn->length;

    if (work_len == 1 && work[0] == 0) {
        out("0");
        goto cleanup;
    }

    /* Extract single digits by repeated division by base */
    while (work_len > 1 || work[0] != 0) {
        uint16_t rem;
        rem = bignum_div_single(work, work_len, (uint16_t)base, work);
        while (work_len > 1 && work[work_len - 1] == 0)
            work_len--;
        if ((uint32_t)dpos < digits_cap - 1)
            digits[dpos++] = digit_chars[rem];
    }

    /* Output sign */
    if (bn->sign) out("-");

    /* Output digits in reverse (dpos-1 is most significant) */
    {
        char outbuf[2];
        outbuf[1] = '\0';
        for (i = dpos - 1; i >= 0; i--) {
            outbuf[0] = digits[i];
            out(outbuf);
        }
    }

cleanup:
    if (heap_work) platform_free(work);
    if (heap_digits) platform_free(digits);
}

/* ================================================================
 * Extended integer operations
 * ================================================================ */

CL_Obj cl_arith_gcd(CL_Obj a, CL_Obj b)
{
    /* Euclidean algorithm */
    a = cl_arith_abs(a);
    b = cl_arith_abs(b);

    if (cl_arith_zerop(a)) return b;
    if (cl_arith_zerop(b)) return a;

    /* Use: gcd(a,b) = gcd(b, a mod b) with truncate-rem */
    while (!cl_arith_zerop(b)) {
        CL_Obj temp;
        /* Compute remainder (truncate, not mod — but since both positive, same) */
        if (CL_FIXNUM_P(a) && CL_FIXNUM_P(b)) {
            int32_t va = CL_FIXNUM_VAL(a);
            int32_t vb = CL_FIXNUM_VAL(b);
            temp = CL_MAKE_FIXNUM(va % vb);
        } else {
            uint16_t ta2[2], tb2[2];
            uint32_t a_len2, b_len2, a_sign2, b_sign2;
            const uint16_t *al2, *bl2;
            uint16_t quot_stack[256], rem_stack[256];
            uint16_t *quot, *rem;
            uint32_t rem_len;
            int heap_bufs = 0;

            al2 = to_limbs(a, &a_len2, &a_sign2, ta2);
            bl2 = to_limbs(b, &b_len2, &b_sign2, tb2);

            if (bignum_compare_mag(al2, a_len2, bl2, b_len2) < 0) {
                temp = a;
            } else {
                if (a_len2 > 256) {
                    quot = (uint16_t *)platform_alloc(a_len2 * sizeof(uint16_t));
                    rem = (uint16_t *)platform_alloc(a_len2 * sizeof(uint16_t));
                    heap_bufs = 1;
                } else {
                    quot = quot_stack;
                    rem = rem_stack;
                }
                bignum_divmod(al2, a_len2, bl2, b_len2, quot, rem);
                rem_len = b_len2;
                while (rem_len > 1 && rem[rem_len - 1] == 0) rem_len--;
                temp = bignum_from_limbs(rem, rem_len, 0);
                if (heap_bufs) { platform_free(quot); platform_free(rem); }
            }
        }
        a = b;
        b = temp;
    }
    return a;
}

CL_Obj cl_arith_expt(CL_Obj base, CL_Obj exp)
{
    CL_Obj result;
    int exp_neg;

    check_integer(base, "EXPT");
    check_integer(exp, "EXPT");

    /* Negative exponent: integer expt returns 0 for |base| > 1 */
    exp_neg = cl_arith_minusp(exp);
    if (exp_neg) {
        /* base^(-n) = 1/base^n — for integers, 0 unless base is +-1 */
        if (cl_arith_compare(base, CL_MAKE_FIXNUM(1)) == 0)
            return CL_MAKE_FIXNUM(1);
        if (cl_arith_compare(base, CL_MAKE_FIXNUM(-1)) == 0) {
            /* (-1)^n: 1 if even, -1 if odd */
            return cl_arith_evenp(exp) ? CL_MAKE_FIXNUM(1) : CL_MAKE_FIXNUM(-1);
        }
        return CL_MAKE_FIXNUM(0);
    }

    if (cl_arith_zerop(exp)) return CL_MAKE_FIXNUM(1);

    /* Binary exponentiation */
    result = CL_MAKE_FIXNUM(1);
    CL_GC_PROTECT(result);
    CL_GC_PROTECT(base);
    CL_GC_PROTECT(exp);

    while (!cl_arith_zerop(exp)) {
        /* Check if exp is odd */
        if (cl_arith_evenp(exp) == 0) {
            result = cl_arith_mul(result, base);
        }
        base = cl_arith_mul(base, base);
        /* exp = exp / 2 */
        exp = cl_arith_truncate(exp, CL_MAKE_FIXNUM(2));
    }

    CL_GC_UNPROTECT(3);
    return result;
}

CL_Obj cl_arith_isqrt(CL_Obj n)
{
    CL_Obj x, x1;

    check_integer(n, "ISQRT");
    if (cl_arith_minusp(n))
        cl_error(CL_ERR_TYPE, "ISQRT: argument must be non-negative");

    if (cl_arith_zerop(n)) return CL_MAKE_FIXNUM(0);

    /* Newton's method: x_{n+1} = (x_n + n/x_n) / 2 */
    /* Initial guess: for fixnums, use a reasonable start */
    if (CL_FIXNUM_P(n)) {
        int32_t val = CL_FIXNUM_VAL(n);
        int32_t guess = 1;
        while (guess * guess < val && guess < 46341) guess *= 2;
        x = CL_MAKE_FIXNUM(guess);
    } else {
        /* For bignums, start with 2^(bit_length/2) */
        int bl = cl_arith_integer_length(n);
        x = cl_arith_ash(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(bl / 2));
    }

    CL_GC_PROTECT(n);
    CL_GC_PROTECT(x);

    /* Iterate */
    {
        int iters = 0;
        while (iters++ < 1000) {
            x1 = cl_arith_add(x, cl_arith_truncate(n, x));
            x1 = cl_arith_truncate(x1, CL_MAKE_FIXNUM(2));
            if (cl_arith_compare(x1, x) >= 0) break;
            x = x1;
        }
    }

    /* Ensure x*x <= n < (x+1)*(x+1) */
    while (cl_arith_compare(cl_arith_mul(x, x), n) > 0) {
        x = cl_arith_sub(x, CL_MAKE_FIXNUM(1));
    }

    CL_GC_UNPROTECT(2);
    return x;
}

int cl_arith_evenp(CL_Obj a)
{
    if (CL_FIXNUM_P(a))
        return (CL_FIXNUM_VAL(a) & 1) == 0;
    {
        CL_Bignum *bn = (CL_Bignum *)CL_OBJ_TO_PTR(a);
        return (bn->limbs[0] & 1) == 0;
    }
}

int cl_arith_integer_length(CL_Obj n)
{
    const uint16_t *limbs;
    uint32_t len;
    uint16_t top;
    int bits;

    if (CL_FIXNUM_P(n)) {
        int32_t val = CL_FIXNUM_VAL(n);
        uint32_t uval;
        if (val == 0) return 0;
        if (val < 0) {
            /* For negative: integer-length of (lognot n) = integer-length of (-n - 1) */
            if (val == -1) return 0;
            uval = (uint32_t)(-(int32_t)(val + 1));
        } else {
            uval = (uint32_t)val;
        }
        bits = 0;
        while (uval) { bits++; uval >>= 1; }
        return bits;
    }

    {
        CL_Bignum *bn = (CL_Bignum *)CL_OBJ_TO_PTR(n);
        limbs = bn->limbs;
        len = bn->length;
    }

    if (len == 0) return 0;

    top = limbs[len - 1];
    bits = (int)(len - 1) * 16;
    while (top) { bits++; top >>= 1; }

    return bits;
}

CL_Obj cl_arith_ash(CL_Obj n, CL_Obj count)
{
    int shift;

    check_integer(n, "ASH");
    if (!CL_FIXNUM_P(count))
        cl_error(CL_ERR_TYPE, "ASH: shift count too large");

    shift = CL_FIXNUM_VAL(count);

    if (shift == 0) return n;

    if (shift > 0) {
        /* Left shift */
        uint16_t tn[2];
        uint32_t n_len, n_sign;
        const uint16_t *nl;
        uint32_t word_shift = (uint32_t)shift / 16;
        uint32_t bit_shift = (uint32_t)shift % 16;
        uint32_t r_len, i;
        uint16_t result_stack[256];
        uint16_t *result;
        int heap_result = 0;
        CL_Obj res;

        nl = to_limbs(n, &n_len, &n_sign, tn);
        r_len = n_len + word_shift + 1;

        if (r_len <= 256) {
            result = result_stack;
        } else {
            result = (uint16_t *)platform_alloc(r_len * sizeof(uint16_t));
            heap_result = 1;
        }

        memset(result, 0, r_len * sizeof(uint16_t));

        if (bit_shift == 0) {
            memcpy(result + word_shift, nl, n_len * sizeof(uint16_t));
        } else {
            uint32_t carry = 0;
            for (i = 0; i < n_len; i++) {
                uint32_t val = ((uint32_t)nl[i] << bit_shift) | carry;
                result[i + word_shift] = (uint16_t)(val & 0xFFFF);
                carry = val >> 16;
            }
            if (carry) result[n_len + word_shift] = (uint16_t)carry;
        }

        while (r_len > 1 && result[r_len - 1] == 0) r_len--;
        res = bignum_from_limbs(result, r_len, n_sign);
        if (heap_result) platform_free(result);
        return res;
    } else {
        /* Right shift (arithmetic: toward negative infinity) */
        uint16_t tn[2];
        uint32_t n_len, n_sign;
        const uint16_t *nl;
        uint32_t word_shift = (uint32_t)(-shift) / 16;
        uint32_t bit_shift = (uint32_t)(-shift) % 16;
        uint32_t r_len, i;
        uint16_t result[256];

        nl = to_limbs(n, &n_len, &n_sign, tn);

        if (word_shift >= n_len) {
            /* Shifted away entirely */
            return n_sign ? CL_MAKE_FIXNUM(-1) : CL_MAKE_FIXNUM(0);
        }

        r_len = n_len - word_shift;
        memset(result, 0, r_len * sizeof(uint16_t));

        if (bit_shift == 0) {
            memcpy(result, nl + word_shift, r_len * sizeof(uint16_t));
        } else {
            for (i = 0; i < r_len; i++) {
                uint32_t val = (uint32_t)nl[i + word_shift] >> bit_shift;
                if (i + word_shift + 1 < n_len) {
                    val |= ((uint32_t)nl[i + word_shift + 1] << (16 - bit_shift)) & 0xFFFF;
                }
                result[i] = (uint16_t)val;
            }
        }

        while (r_len > 1 && result[r_len - 1] == 0) r_len--;

        {
            CL_Obj res = bignum_from_limbs(result, r_len, n_sign);
            /* Arithmetic shift: for negatives, floor toward -inf */
            if (n_sign) {
                /* Check if any shifted-out bits were set */
                int any_set = 0;
                uint32_t ws;
                for (ws = 0; ws < word_shift && ws < n_len; ws++) {
                    if (nl[ws] != 0) { any_set = 1; break; }
                }
                if (!any_set && bit_shift > 0 && word_shift < n_len) {
                    if (nl[word_shift] & ((1u << bit_shift) - 1))
                        any_set = 1;
                }
                if (any_set) {
                    res = cl_arith_sub(res, CL_MAKE_FIXNUM(1));
                }
            }
            return res;
        }
    }
}

/* Helper: convert integer to a freshly allocated limb array for bitwise ops.
 * Uses two's complement for negative numbers.
 * Returns allocated length; caller should use the result buffer.
 * For negative numbers, computes two's complement. */
static uint32_t to_twos_complement(CL_Obj n, uint16_t *buf, uint32_t buf_size,
                                    int *is_negative)
{
    uint16_t tn[2];
    uint32_t n_len, n_sign;
    const uint16_t *nl;
    uint32_t i, len;

    nl = to_limbs(n, &n_len, &n_sign, tn);
    *is_negative = (int)n_sign;

    if (n_len + 1 > buf_size) n_len = buf_size - 1;
    len = n_len + 1; /* Extra limb for sign extension */
    if (len > buf_size) len = buf_size;

    memset(buf, 0, len * sizeof(uint16_t));
    memcpy(buf, nl, n_len * sizeof(uint16_t));

    if (n_sign) {
        /* Negate: invert and add 1 */
        uint32_t carry = 1;
        for (i = 0; i < len; i++) {
            uint32_t val = (uint32_t)(buf[i] ^ 0xFFFF) + carry;
            buf[i] = (uint16_t)(val & 0xFFFF);
            carry = val >> 16;
        }
    }

    return len;
}

/* Convert from two's complement back to sign-magnitude bignum */
static CL_Obj from_twos_complement(const uint16_t *buf, uint32_t len)
{
    /* If high bit of top limb is set, it's negative */
    int negative = (buf[len - 1] & 0x8000) != 0;
    uint16_t tmp[256];
    uint32_t i;

    if (len > 256) len = 256;

    if (!negative) {
        /* Strip leading zeros */
        while (len > 1 && buf[len - 1] == 0) len--;
        return bignum_from_limbs(buf, len, 0);
    }

    /* Negative: invert and add 1 */
    {
        uint32_t carry = 1;
        for (i = 0; i < len; i++) {
            uint32_t val = (uint32_t)(buf[i] ^ 0xFFFF) + carry;
            tmp[i] = (uint16_t)(val & 0xFFFF);
            carry = val >> 16;
        }
        while (len > 1 && tmp[len - 1] == 0) len--;
        return bignum_from_limbs(tmp, len, 1);
    }
}

CL_Obj cl_arith_logand(CL_Obj a, CL_Obj b)
{
    uint16_t ba[128], bb[128];
    int a_neg, b_neg;
    uint32_t a_len, b_len, max_len, i;
    uint16_t result[128];

    check_integer(a, "LOGAND");
    check_integer(b, "LOGAND");

    a_len = to_twos_complement(a, ba, 128, &a_neg);
    b_len = to_twos_complement(b, bb, 128, &b_neg);
    max_len = a_len > b_len ? a_len : b_len;
    if (max_len > 128) max_len = 128;

    /* Sign-extend shorter operand */
    {
        uint16_t a_ext = a_neg ? 0xFFFF : 0;
        uint16_t b_ext = b_neg ? 0xFFFF : 0;
        for (i = 0; i < max_len; i++) {
            uint16_t av = i < a_len ? ba[i] : a_ext;
            uint16_t bv = i < b_len ? bb[i] : b_ext;
            result[i] = av & bv;
        }
    }

    return from_twos_complement(result, max_len);
}

CL_Obj cl_arith_logior(CL_Obj a, CL_Obj b)
{
    uint16_t ba[128], bb[128];
    int a_neg, b_neg;
    uint32_t a_len, b_len, max_len, i;
    uint16_t result[128];

    check_integer(a, "LOGIOR");
    check_integer(b, "LOGIOR");

    a_len = to_twos_complement(a, ba, 128, &a_neg);
    b_len = to_twos_complement(b, bb, 128, &b_neg);
    max_len = a_len > b_len ? a_len : b_len;
    if (max_len > 128) max_len = 128;

    {
        uint16_t a_ext = a_neg ? 0xFFFF : 0;
        uint16_t b_ext = b_neg ? 0xFFFF : 0;
        for (i = 0; i < max_len; i++) {
            uint16_t av = i < a_len ? ba[i] : a_ext;
            uint16_t bv = i < b_len ? bb[i] : b_ext;
            result[i] = av | bv;
        }
    }

    return from_twos_complement(result, max_len);
}

CL_Obj cl_arith_logxor(CL_Obj a, CL_Obj b)
{
    uint16_t ba[128], bb[128];
    int a_neg, b_neg;
    uint32_t a_len, b_len, max_len, i;
    uint16_t result[128];

    check_integer(a, "LOGXOR");
    check_integer(b, "LOGXOR");

    a_len = to_twos_complement(a, ba, 128, &a_neg);
    b_len = to_twos_complement(b, bb, 128, &b_neg);
    max_len = a_len > b_len ? a_len : b_len;
    if (max_len > 128) max_len = 128;

    {
        uint16_t a_ext = a_neg ? 0xFFFF : 0;
        uint16_t b_ext = b_neg ? 0xFFFF : 0;
        for (i = 0; i < max_len; i++) {
            uint16_t av = i < a_len ? ba[i] : a_ext;
            uint16_t bv = i < b_len ? bb[i] : b_ext;
            result[i] = av ^ bv;
        }
    }

    return from_twos_complement(result, max_len);
}

CL_Obj cl_arith_lognot(CL_Obj a)
{
    check_integer(a, "LOGNOT");

    /* lognot(n) = -n - 1 = -(n+1) */
    return cl_arith_sub(CL_MAKE_FIXNUM(-1), a);
}

/* ================================================================
 * Bit counting / testing
 * ================================================================ */

/* Count 1-bits in a uint16_t */
static int popcount16(uint16_t v)
{
    int c = 0;
    while (v) { v &= v - 1; c++; }
    return c;
}

int cl_arith_logcount(CL_Obj n)
{
    /* For positive: count 1-bits. For negative: count 0-bits (= popcount of ~n = popcount of lognot(n)). */
    if (CL_FIXNUM_P(n)) {
        int32_t val = CL_FIXNUM_VAL(n);
        uint32_t uval = (val < 0) ? (uint32_t)~val : (uint32_t)val;
        int c = 0;
        while (uval) { uval &= uval - 1; c++; }
        return c;
    }
    {
        CL_Bignum *bn = (CL_Bignum *)CL_OBJ_TO_PTR(n);
        uint32_t i;
        int c = 0;
        if (bn->sign == 0) {
            /* Positive: count 1-bits in magnitude */
            for (i = 0; i < bn->length; i++)
                c += popcount16(bn->limbs[i]);
        } else {
            /* Negative: count 0-bits = popcount of two's complement negation - but easier:
               logcount(-n) = logcount(n-1) for negative n (CL spec).
               We compute two's complement and count 1-bits of the inverted form.
               Actually, for negative n, logcount(n) = logcount(lognot(n)) = logcount(-n-1).
               -n-1 has magnitude |n|-1. */
            uint16_t tmp[128];
            uint32_t len = bn->length;
            uint32_t borrow = 1;
            if (len > 128) len = 128;
            /* Compute magnitude - 1 */
            for (i = 0; i < len; i++) {
                uint32_t val = (uint32_t)bn->limbs[i] - borrow;
                tmp[i] = (uint16_t)(val & 0xFFFF);
                borrow = (val >> 16) & 1; /* borrow if underflow */
            }
            for (i = 0; i < len; i++)
                c += popcount16(tmp[i]);
        }
        return c;
    }
}

int cl_arith_logbitp(int index, CL_Obj integer)
{
    /* Test bit at position index in integer (two's complement). */
    if (index < 0) return 0;

    if (CL_FIXNUM_P(integer)) {
        int32_t val = CL_FIXNUM_VAL(integer);
        if (index >= 31) return val < 0 ? 1 : 0; /* sign extension */
        return (val >> index) & 1;
    }
    {
        CL_Bignum *bn = (CL_Bignum *)CL_OBJ_TO_PTR(integer);
        uint32_t limb_idx = (uint32_t)index / 16;
        uint32_t bit_idx = (uint32_t)index % 16;

        if (bn->sign == 0) {
            /* Positive: just test the bit in magnitude */
            if (limb_idx >= bn->length) return 0;
            return (bn->limbs[limb_idx] >> bit_idx) & 1;
        } else {
            /* Negative: two's complement. Need to negate magnitude.
               In two's complement, bits of -n = ~(n-1) when n > 0.
               So bit i of -|m| = NOT(bit i of (|m|-1)).
               But we need to handle borrow propagation. */
            uint16_t tmp[128];
            uint32_t len = bn->length;
            uint32_t borrow = 1;
            uint32_t i;
            uint16_t val;
            if (len > 128) len = 128;
            /* Compute |m| - 1 in magnitude, only up to limb_idx+1 */
            {
                uint32_t compute_len = limb_idx + 1;
                if (compute_len > len) compute_len = len;
                for (i = 0; i < compute_len; i++) {
                    uint32_t v = (uint32_t)bn->limbs[i] - borrow;
                    tmp[i] = (uint16_t)(v & 0xFFFF);
                    borrow = (v >> 16) & 1;
                }
            }
            if (limb_idx >= len) {
                /* Beyond magnitude: two's complement sign extension = all 1s */
                return 1;
            }
            val = tmp[limb_idx];
            /* Two's complement: invert */
            return ((~val) >> bit_idx) & 1;
        }
    }
}

/* ================================================================
 * Initialization
 * ================================================================ */

/* Helper: intern a constant symbol with a fixnum value */
static void def_const(const char *name, uint32_t len, int32_t val,
                       CL_Obj *sym_out)
{
    CL_Obj sym = cl_intern_in(name, len, cl_package_cl);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->value = CL_MAKE_FIXNUM(val);
    s->flags |= CL_SYM_CONSTANT;
    if (sym_out) *sym_out = sym;
}

void cl_bignum_init(void)
{
    CL_Obj sym;
    CL_Symbol *s;

    /* MOST-POSITIVE-FIXNUM */
    sym = cl_intern_in("MOST-POSITIVE-FIXNUM", 20, cl_package_cl);
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->value = CL_MAKE_FIXNUM(CL_FIXNUM_MAX);
    s->flags |= CL_SYM_CONSTANT;

    /* MOST-NEGATIVE-FIXNUM */
    sym = cl_intern_in("MOST-NEGATIVE-FIXNUM", 20, cl_package_cl);
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->value = CL_MAKE_FIXNUM(CL_FIXNUM_MIN);
    s->flags |= CL_SYM_CONSTANT;

    /* BOOLE operation constants (CL spec values 0-15) */
    def_const("BOOLE-CLR",   9,  0, &SYM_BOOLE_CLR);
    def_const("BOOLE-SET",   9,  1, &SYM_BOOLE_SET);
    def_const("BOOLE-1",     7,  2, &SYM_BOOLE_1);
    def_const("BOOLE-2",     7,  3, &SYM_BOOLE_2);
    def_const("BOOLE-C1",    8,  4, &SYM_BOOLE_C1);
    def_const("BOOLE-C2",    8,  5, &SYM_BOOLE_C2);
    def_const("BOOLE-AND",   9,  6, &SYM_BOOLE_AND);
    def_const("BOOLE-IOR",   9,  7, &SYM_BOOLE_IOR);
    def_const("BOOLE-XOR",   9,  8, &SYM_BOOLE_XOR);
    def_const("BOOLE-EQV",   9,  9, &SYM_BOOLE_EQV);
    def_const("BOOLE-NAND",  10, 10, &SYM_BOOLE_NAND);
    def_const("BOOLE-NOR",   9,  11, &SYM_BOOLE_NOR);
    def_const("BOOLE-ANDC1", 11, 12, &SYM_BOOLE_ANDC1);
    def_const("BOOLE-ANDC2", 11, 13, &SYM_BOOLE_ANDC2);
    def_const("BOOLE-ORC1",  10, 14, &SYM_BOOLE_ORC1);
    def_const("BOOLE-ORC2",  10, 15, &SYM_BOOLE_ORC2);

    /* CHAR-CODE-LIMIT — upper exclusive bound on char-code values */
#ifdef CL_WIDE_STRINGS
    /* Full Unicode range: U+0000 to U+10FFFF (1114112 values) */
    def_const("CHAR-CODE-LIMIT", 15, 1114112, NULL);
#else
    def_const("CHAR-CODE-LIMIT", 15, 256, NULL);
#endif

    /* Float limit constants */
    {
        CL_Obj fsym;
        CL_Symbol *fs;

        fsym = cl_intern_in("MOST-POSITIVE-SINGLE-FLOAT", 26, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_single_float(3.4028235e+38f);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("MOST-NEGATIVE-SINGLE-FLOAT", 26, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_single_float(-3.4028235e+38f);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("MOST-POSITIVE-DOUBLE-FLOAT", 26, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_double_float(1.7976931348623157e+308);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("MOST-NEGATIVE-DOUBLE-FLOAT", 26, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_double_float(-1.7976931348623157e+308);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("LEAST-POSITIVE-SINGLE-FLOAT", 27, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_single_float(1.17549435e-38f);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("LEAST-NEGATIVE-SINGLE-FLOAT", 27, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_single_float(-1.17549435e-38f);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("LEAST-POSITIVE-DOUBLE-FLOAT", 27, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_double_float(2.2250738585072014e-308);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("LEAST-NEGATIVE-DOUBLE-FLOAT", 27, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_double_float(-2.2250738585072014e-308);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("SINGLE-FLOAT-EPSILON", 20, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_single_float(1.1920929e-7f);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("DOUBLE-FLOAT-EPSILON", 20, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_double_float(2.2204460492503131e-16);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("SHORT-FLOAT-EPSILON", 19, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_single_float(1.1920929e-7f);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("LONG-FLOAT-EPSILON", 18, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_double_float(2.2204460492503131e-16);
        fs->flags |= CL_SYM_CONSTANT;

        /* short-float/long-float aliases for most-positive/negative */
        fsym = cl_intern_in("MOST-POSITIVE-SHORT-FLOAT", 25, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_single_float(3.4028235e+38f);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("MOST-NEGATIVE-SHORT-FLOAT", 25, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_single_float(-3.4028235e+38f);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("MOST-POSITIVE-LONG-FLOAT", 24, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_double_float(1.7976931348623157e+308);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("MOST-NEGATIVE-LONG-FLOAT", 24, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_double_float(-1.7976931348623157e+308);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("LEAST-POSITIVE-SHORT-FLOAT", 26, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_single_float(1.17549435e-38f);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("LEAST-NEGATIVE-SHORT-FLOAT", 26, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_single_float(-1.17549435e-38f);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("LEAST-POSITIVE-LONG-FLOAT", 25, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_double_float(2.2250738585072014e-308);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("LEAST-NEGATIVE-LONG-FLOAT", 25, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_double_float(-2.2250738585072014e-308);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("SINGLE-FLOAT-NEGATIVE-EPSILON", 29, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_single_float(5.9604645e-8f);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("DOUBLE-FLOAT-NEGATIVE-EPSILON", 29, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_double_float(1.1102230246251566e-16);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("SHORT-FLOAT-NEGATIVE-EPSILON", 28, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_single_float(5.9604645e-8f);
        fs->flags |= CL_SYM_CONSTANT;

        fsym = cl_intern_in("LONG-FLOAT-NEGATIVE-EPSILON", 27, cl_package_cl);
        fs = (CL_Symbol *)CL_OBJ_TO_PTR(fsym);
        fs->value = cl_make_double_float(1.1102230246251566e-16);
        fs->flags |= CL_SYM_CONSTANT;
    }
}
