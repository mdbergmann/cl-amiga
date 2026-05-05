/*
 * builtins_random.c — Random number generation: random, make-random-state,
 *                     *random-state*, random-state-p
 *
 * Algorithm: xorshift128 — 16-byte state (4 x uint32_t), fast on 68020.
 */

#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "float.h"
#include "bignum.h"
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
}

/* --- xorshift128 PRNG --- */

static uint32_t xorshift128_next(CL_RandomState *rs)
{
    uint32_t t = rs->s[3];
    uint32_t s = rs->s[0];
    rs->s[3] = rs->s[2];
    rs->s[2] = rs->s[1];
    rs->s[1] = s;
    t ^= t << 11;
    t ^= t >> 8;
    rs->s[0] = t ^ s ^ (s >> 19);
    return rs->s[0];
}

/* Get the current *random-state* value */
static CL_RandomState *get_default_random_state(void)
{
    return (CL_RandomState *)CL_OBJ_TO_PTR(cl_symbol_value(SYM_RANDOM_STATE));
}

/* --- (random limit &optional random-state) --- */

static CL_Obj bi_random(CL_Obj *args, int n)
{
    CL_Obj limit = args[0];
    CL_RandomState *rs;

    /* Get random state */
    if (n >= 2 && !CL_NULL_P(args[1])) {
        if (!CL_RANDOM_STATE_P(args[1]))
            cl_error(CL_ERR_TYPE, "RANDOM: second argument must be a random-state");
        rs = (CL_RandomState *)CL_OBJ_TO_PTR(args[1]);
    } else {
        rs = get_default_random_state();
    }

    /* Integer limit (fixnum) */
    if (CL_FIXNUM_P(limit)) {
        int32_t lim = CL_FIXNUM_VAL(limit);
        uint32_t r;
        if (lim <= 0)
            cl_error(CL_ERR_ARGS, "RANDOM: limit must be positive");
        r = xorshift128_next(rs);
        return CL_MAKE_FIXNUM((int32_t)(r % (uint32_t)lim));
    }

    /* Integer limit (bignum) */
    if (CL_BIGNUM_P(limit)) {
        CL_Bignum *bn = (CL_Bignum *)CL_OBJ_TO_PTR(limit);
        CL_Obj result;
        uint32_t i;
        CL_Bignum *rbn;
        if (bn->sign != 0)
            cl_error(CL_ERR_ARGS, "RANDOM: limit must be positive");

        /* Generate a random bignum with same number of limbs, then reduce mod limit */
        CL_GC_PROTECT(limit);
        result = cl_make_bignum(bn->length, 0);
        CL_GC_UNPROTECT(1);
        rbn = (CL_Bignum *)CL_OBJ_TO_PTR(result);
        /* Re-read bn pointer after potential GC */
        bn = (CL_Bignum *)CL_OBJ_TO_PTR(limit);
        for (i = 0; i < bn->length; i++) {
            rbn->limbs[i] = (uint16_t)(xorshift128_next(rs) & 0xFFFF);
        }
        /* Mask top limb to same bit width as limit's top limb */
        if (bn->length > 0) {
            uint16_t top = bn->limbs[bn->length - 1];
            uint16_t mask = 0xFFFF;
            if (top > 0) {
                uint16_t t = top;
                mask = 0;
                while (t > 0) {
                    mask = (mask << 1) | 1;
                    t >>= 1;
                }
            }
            rbn->limbs[bn->length - 1] &= mask;
        }
        /* Compute result mod limit using: r - truncate(r/limit) * limit */
        {
            CL_Obj r_obj = cl_bignum_normalize(result);
            CL_Obj q, rem;
            CL_GC_PROTECT(r_obj);
            CL_GC_PROTECT(limit);
            q = cl_arith_truncate(r_obj, limit);
            CL_GC_PROTECT(q);
            rem = cl_arith_sub(r_obj, cl_arith_mul(q, limit));
            CL_GC_UNPROTECT(3);
            return rem;
        }
    }

    /* Float limit (single-float) */
    if (CL_SINGLE_FLOAT_P(limit)) {
        float lim = ((CL_SingleFloat *)CL_OBJ_TO_PTR(limit))->value;
        uint32_t r;
        float fval;
        if (lim <= 0.0f)
            cl_error(CL_ERR_ARGS, "RANDOM: limit must be positive");
        r = xorshift128_next(rs);
        fval = ((float)(r >> 8)) / 16777216.0f * lim;  /* 24-bit mantissa */
        return cl_make_single_float(fval);
    }

    /* Float limit (double-float) */
    if (CL_DOUBLE_FLOAT_P(limit)) {
        double lim = ((CL_DoubleFloat *)CL_OBJ_TO_PTR(limit))->value;
        uint32_t r1, r2;
        double dval;
        if (lim <= 0.0)
            cl_error(CL_ERR_ARGS, "RANDOM: limit must be positive");
        r1 = xorshift128_next(rs);
        r2 = xorshift128_next(rs);
        /* Combine two 32-bit values for ~53-bit precision */
        dval = (((double)(r1 >> 5)) * 67108864.0 + (double)(r2 >> 6))
               / 9007199254740992.0 * lim;
        return cl_make_double_float(dval);
    }

    cl_error(CL_ERR_TYPE, "RANDOM: limit must be a positive integer or float");
    return CL_NIL;
}

/* --- (make-random-state &optional state) --- */

static CL_Obj bi_make_random_state(CL_Obj *args, int n)
{
    CL_Obj arg = (n > 0) ? args[0] : CL_NIL;
    CL_RandomState *src;
    CL_RandomState *dst;
    CL_Obj result;

    /* arg = T: create fresh state seeded from time */
    if (arg == CL_T) {
        return cl_make_random_state(platform_time_ms() ^ platform_universal_time());
    }

    /* arg = NIL or no arg: copy *random-state* */
    if (CL_NULL_P(arg)) {
        src = get_default_random_state();
        result = cl_make_random_state(0);
        dst = (CL_RandomState *)CL_OBJ_TO_PTR(result);
        dst->s[0] = src->s[0];
        dst->s[1] = src->s[1];
        dst->s[2] = src->s[2];
        dst->s[3] = src->s[3];
        return result;
    }

    /* arg = random-state: copy it */
    if (CL_RANDOM_STATE_P(arg)) {
        src = (CL_RandomState *)CL_OBJ_TO_PTR(arg);
        result = cl_make_random_state(0);
        dst = (CL_RandomState *)CL_OBJ_TO_PTR(result);
        /* Re-read src after potential GC */
        src = (CL_RandomState *)CL_OBJ_TO_PTR(arg);
        dst->s[0] = src->s[0];
        dst->s[1] = src->s[1];
        dst->s[2] = src->s[2];
        dst->s[3] = src->s[3];
        return result;
    }

    cl_error(CL_ERR_TYPE, "MAKE-RANDOM-STATE: argument must be T, NIL, or a random-state");
    return CL_NIL;
}

/* --- (random-state-p obj) --- */

static CL_Obj bi_random_state_p(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_RANDOM_STATE_P(args[0]) ? CL_T : CL_NIL;
}

/* --- Registration --- */

void cl_builtins_random_init(void)
{
    defun("RANDOM", bi_random, 1, 2);
    defun("MAKE-RANDOM-STATE", bi_make_random_state, 0, 1);
    defun("RANDOM-STATE-P", bi_random_state_p, 1, 1);
}
