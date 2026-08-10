/*
 * float_dtoa.c — exact, FPU-independent float <-> decimal text conversion.
 *
 * Why this exists: the printer/reader used to delegate to libc's %g/strtod,
 * whose internal arithmetic runs on whatever float machinery the platform
 * has.  On Amiga, the "soft-float" build routes every double operation
 * through mathieeedoubbas.library, which on real FPU hardware IS the FPU —
 * and FPGA-accelerated machines (Vampire/Apollo "68882") divide with only
 * ~42 mantissa bits and ignore FPCR entirely, so strtod there misparses
 * literals by several ulps and no printed precision 1..17 round-trips.
 *
 * Everything in this file uses integer arithmetic only — floats are touched
 * exclusively as bit patterns through memcpy unions.  Results are therefore
 * bit-identical on every platform and correct regardless of FPU quality:
 *
 *   cl_dtoa_shortest()     Steele-White / Burger-Dybvig shortest round-trip
 *                          digit generation (binary -> decimal).
 *   cl_parse_float_exact() correctly rounded decimal -> IEEE binary
 *                          (David Gay's bound: 768 significant digits plus
 *                          a sticky flag decide every double correctly).
 *
 * Big integers are fixed-size stack buffers: no heap, no GC interaction,
 * MT-safe.  Buffer bounds are guaranteed by the decimal-exponent range
 * guards in the parser (|10^k| clamped to the representable range before
 * any big arithmetic) and by the value range of IEEE doubles in the digit
 * generator; every op still carries a poison flag (n < 0) as a backstop,
 * and callers fall back to the old libc path if it ever trips.
 */

#include "float.h"
#include <string.h>

/* ================================================================
 * Fixed-size big integers, 32-bit limbs, little-endian limb order
 * ================================================================
 *
 * Size bound: the parser's worst case is D * 5^K for a 310-digit integer
 * part (~1030 bits + 720 bits) or D << s with s sized so the quotient has
 * ~57 bits (numerator <= bits(5^1093) + ~60 = ~2600 bits).  The digit
 * generator peaks near 2^1075 scaled by one decimal digit step (~1100
 * bits).  112 limbs = 3584 bits covers all of it with margin.
 */

#define DT_LIMBS 112

typedef struct {
    int32_t  n;              /* limbs used; 0 = value zero; -1 = poisoned */
    uint32_t d[DT_LIMBS];
} DtBig;

static void dt_zero(DtBig *b) { b->n = 0; }

/* Half-split like bitlen_u64 above: a `while (v) { ...; v >>= 32; }` loop
 * is the same 64-bit shift-loop shape the m68k toolchain miscompiles. */
static void dt_from_u64(DtBig *b, uint64_t v)
{
    uint32_t lo = (uint32_t)v;
    uint32_t hi = (uint32_t)(v >> 32);
    b->n = 0;
    if (lo | hi) b->d[b->n++] = lo;
    if (hi)      b->d[b->n++] = hi;
}

static void dt_copy(DtBig *dst, const DtBig *src)
{
    int32_t i;
    dst->n = src->n;
    for (i = 0; i < src->n; i++)
        dst->d[i] = src->d[i];
}

static int dt_is_zero(const DtBig *b) { return b->n == 0; }

/* b *= m (m > 0) */
static void dt_mul_u32(DtBig *b, uint32_t m)
{
    uint64_t carry = 0;
    int32_t i;
    if (b->n < 0) return;
    for (i = 0; i < b->n; i++) {
        uint64_t t = (uint64_t)b->d[i] * m + carry;
        b->d[i] = (uint32_t)t;
        carry = t >> 32;
    }
    while (carry) {
        if (b->n >= DT_LIMBS) { b->n = -1; return; }
        b->d[b->n++] = (uint32_t)carry;
        carry >>= 32;
    }
}

/* b += v (small) */
static void dt_add_u32(DtBig *b, uint32_t v)
{
    uint64_t carry = v;
    int32_t i;
    if (b->n < 0) return;
    for (i = 0; i < b->n && carry; i++) {
        uint64_t t = (uint64_t)b->d[i] + carry;
        b->d[i] = (uint32_t)t;
        carry = t >> 32;
    }
    if (carry) {
        if (b->n >= DT_LIMBS) { b->n = -1; return; }
        b->d[b->n++] = (uint32_t)carry;
    }
}

/* b += a */
static void dt_add(DtBig *b, const DtBig *a)
{
    uint64_t carry = 0;
    int32_t i, top;
    if (b->n < 0 || a->n < 0) { b->n = -1; return; }
    top = (a->n > b->n) ? a->n : b->n;
    if (top > DT_LIMBS) { b->n = -1; return; }
    for (i = 0; i < top; i++) {
        uint64_t t = carry;
        if (i < b->n) t += b->d[i];
        if (i < a->n) t += a->d[i];
        b->d[i] = (uint32_t)t;
        carry = t >> 32;
    }
    b->n = top;
    if (carry) {
        if (b->n >= DT_LIMBS) { b->n = -1; return; }
        b->d[b->n++] = (uint32_t)carry;
    }
}

/* a -= b; requires a >= b (caller compares first) */
static void dt_sub(DtBig *a, const DtBig *b)
{
    int64_t borrow = 0;
    int32_t i;
    if (a->n < 0 || b->n < 0) { a->n = -1; return; }
    for (i = 0; i < a->n; i++) {
        int64_t t = (int64_t)a->d[i] - borrow - (i < b->n ? b->d[i] : 0);
        if (t < 0) { t += ((int64_t)1 << 32); borrow = 1; }
        else borrow = 0;
        a->d[i] = (uint32_t)t;
    }
    while (a->n > 0 && a->d[a->n - 1] == 0)
        a->n--;
}

/* -1 / 0 / +1 for a <=> b */
static int dt_cmp(const DtBig *a, const DtBig *b)
{
    int32_t i;
    if (a->n != b->n) return (a->n < b->n) ? -1 : 1;
    for (i = a->n - 1; i >= 0; i--) {
        if (a->d[i] != b->d[i])
            return (a->d[i] < b->d[i]) ? -1 : 1;
    }
    return 0;
}

/* b <<= k bits */
static void dt_shl(DtBig *b, int32_t k)
{
    int32_t limbs = k / 32, bits = k % 32, i;
    if (b->n < 0 || b->n == 0 || k == 0) return;
    if (b->n + limbs + 1 > DT_LIMBS) { b->n = -1; return; }
    if (bits) {
        uint32_t carry = 0;
        for (i = 0; i < b->n; i++) {
            uint32_t t = b->d[i];
            b->d[i] = (t << bits) | carry;
            carry = t >> (32 - bits);
        }
        if (carry) b->d[b->n++] = carry;
    }
    if (limbs) {
        for (i = b->n - 1; i >= 0; i--)
            b->d[i + limbs] = b->d[i];
        for (i = 0; i < limbs; i++)
            b->d[i] = 0;
        b->n += limbs;
    }
}

static int32_t bitlen_u32(uint32_t v)
{
    int32_t n = 0;
    while (v) { n++; v >>= 1; }
    return n;
}

/* NOTE (m68k workaround): computed from 32-bit halves on purpose.  The
 * obvious `while (v) { n++; v >>= 1; }` over a uint64_t is MISCOMPILED by
 * m68k-amigaos-gcc 6.5 at -O1/-Os: whenever the low 32 bits are zero the
 * loop terminates after one iteration and returns 1 (verified on real
 * hardware; the -O2 flexible-array bug in Makefile.cross is the same
 * toolchain's sibling).  Half-splitting avoids the 64-bit shift-loop
 * pattern entirely — and is faster everywhere anyway. */
static int32_t bitlen_u64(uint64_t v)
{
    uint32_t hi = (uint32_t)(v >> 32);
    if (hi) return 32 + bitlen_u32(hi);
    return bitlen_u32((uint32_t)v);
}

static int32_t dt_bitlen(const DtBig *b)
{
    if (b->n <= 0) return 0;
    return (b->n - 1) * 32 + bitlen_u32(b->d[b->n - 1]);
}

/* bit i (0 = LSB); out-of-range reads as 0 */
static int dt_bit(const DtBig *b, int32_t i)
{
    if (i < 0 || i >= b->n * 32) return 0;
    return (b->d[i / 32] >> (i % 32)) & 1;
}

/* 1 iff any of bits 0..i-1 is set */
static int dt_low_bits_nonzero(const DtBig *b, int32_t i)
{
    int32_t limbs = i / 32, bits = i % 32, j;
    if (b->n <= 0) return 0;
    for (j = 0; j < limbs && j < b->n; j++)
        if (b->d[j]) return 1;
    if (bits && limbs < b->n && (b->d[limbs] & ((1u << bits) - 1)))
        return 1;
    return 0;
}

/* Top `count` bits of b (bitlen must be >= count), as u64 (count <= 64). */
static uint64_t dt_top_bits(const DtBig *b, int32_t count)
{
    int32_t blen = dt_bitlen(b);
    int32_t shift = blen - count;      /* >= 0 */
    uint64_t v = 0;
    int32_t i;
    for (i = count - 1; i >= 0; i--)
        v = (v << 1) | (uint64_t)dt_bit(b, shift + i);
    return v;
}

/* 5^k for k <= 13 fits in uint32_t (5^13 = 1220703125). */
static const uint32_t dt_pow5_small[14] = {
    1u, 5u, 25u, 125u, 625u, 3125u, 15625u, 78125u, 390625u,
    1953125u, 9765625u, 48828125u, 244140625u, 1220703125u
};

static void dt_mul_pow5(DtBig *b, int32_t k)
{
    while (k >= 13) { dt_mul_u32(b, dt_pow5_small[13]); k -= 13; }
    if (k > 0) dt_mul_u32(b, dt_pow5_small[k]);
}

static void dt_mul_pow10(DtBig *b, int32_t k)
{
    while (k >= 9) { dt_mul_u32(b, 1000000000u); k -= 9; }
    while (k-- > 0) dt_mul_u32(b, 10u);
}

/* ================================================================
 * Shortest round-trip digits (Steele-White / Burger-Dybvig)
 * ================================================================ */

/*
 * Generate the shortest decimal digit string that reads back to exactly
 * the float with mantissa f (hidden bit included for normals), binary
 * exponent e (value = f * 2^e, value > 0), under round-to-nearest-even.
 * `unequal` is 1 when the gap to the next-smaller float is half the gap
 * to the next-larger one (f a power of two, not at minimum exponent).
 *
 * Writes ASCII digits (no trailing zeros) and the decimal exponent k with
 * value ~= 0.digits * 10^k.  Returns the digit count, or 0 if a big-int
 * buffer overflowed (mathematically unreachable; callers fall back).
 */
static int dragon4_shortest(uint64_t f, int32_t e, int unequal,
                            char *digits, int32_t *dec_exp)
{
    DtBig R, S, MP, MM, T;
    int even = ((f & 1) == 0);
    int32_t k, n = 0;
    int c;

    /* Scaled representation: value = R/S, half-gap up = MP/S, down = MM/S. */
    if (e >= 0) {
        dt_from_u64(&MM, 1); dt_shl(&MM, e);           /* 2^e */
        dt_copy(&MP, &MM);
        dt_from_u64(&R, f); dt_shl(&R, e + 1);
        dt_from_u64(&S, 2);
        if (unequal) {
            dt_shl(&R, 1); dt_shl(&MP, 1);
            dt_from_u64(&S, 4);
        }
    } else {
        dt_from_u64(&MM, 1);
        dt_from_u64(&MP, 1);
        dt_from_u64(&R, f); dt_shl(&R, 1);
        dt_from_u64(&S, 1); dt_shl(&S, 1 - e);
        if (unequal) {
            dt_shl(&R, 1); dt_shl(&MP, 1);
            dt_shl(&S, 1);
        }
    }

    /* Estimate k = ceil(log10(value)), then fix up exactly below.
     * floor((blen-1) * log10(2)) via integer scaling; the fixup loops
     * tolerate an estimate that is off by one in either direction. */
    k = (int32_t)(((int64_t)(bitlen_u64(f) + e - 1) * 30103) / 100000) + 1;
    if (k >= 0) {
        dt_mul_pow10(&S, k);
    } else {
        dt_mul_pow10(&R, -k);
        dt_mul_pow10(&MP, -k);
        dt_mul_pow10(&MM, -k);
    }

    /* Fix up so the first generated digit is in 1..9: the loop conditions
     * mirror the generate loop's `high` test exactly (strictness depends
     * on `even`, i.e. whether boundaries themselves round back to f). */
    for (;;) {                       /* value (+half gap) >= 1: shift right */
        dt_copy(&T, &R); dt_add(&T, &MP);
        c = dt_cmp(&T, &S);
        if (c > 0 || (c == 0 && even)) { dt_mul_u32(&S, 10); k++; continue; }
        break;
    }
    for (;;) {                       /* first digit would be 0: shift left */
        dt_copy(&T, &R); dt_add(&T, &MP); dt_mul_u32(&T, 10);
        c = dt_cmp(&T, &S);
        if (c < 0 || (c == 0 && !even)) {
            dt_mul_u32(&R, 10); dt_mul_u32(&MP, 10); dt_mul_u32(&MM, 10);
            k--;
            continue;
        }
        break;
    }

    if (R.n < 0 || S.n < 0 || MP.n < 0 || MM.n < 0)
        return 0;

    /* Digit generation.  Terminates when rounding the emitted prefix down
     * (low) or up (high) lands inside (value - MM, value + MP), i.e. the
     * prefix already reads back to exactly this float. */
    for (;;) {
        int d = 0, low, high;
        dt_mul_u32(&R, 10); dt_mul_u32(&MP, 10); dt_mul_u32(&MM, 10);
        if (R.n < 0 || MP.n < 0 || MM.n < 0) return 0;
        while (dt_cmp(&R, &S) >= 0) { dt_sub(&R, &S); d++; }  /* d in 0..9 */

        c = dt_cmp(&R, &MM);
        low = even ? (c <= 0) : (c < 0);
        dt_copy(&T, &R); dt_add(&T, &MP);
        c = dt_cmp(&T, &S);
        high = even ? (c >= 0) : (c > 0);

        if (!low && !high) {
            digits[n++] = (char)('0' + d);
            if (n >= 18) return 0;   /* unreachable: shortest <= 17 digits */
            continue;
        }
        if (low && high) {
            /* Both neighbors read back: emit whichever is closer (tie: the
             * even digit), comparing remainder R against S/2 via 2R vs S. */
            dt_copy(&T, &R); dt_add(&T, &R);
            c = dt_cmp(&T, &S);
            if (c > 0 || (c == 0 && (d & 1))) d++;
        } else if (high) {
            d++;
        }
        digits[n++] = (char)('0' + d);
        break;
    }

    /* d+1 may have produced ':' (digit 10): propagate the carry. */
    if (digits[n - 1] == '0' + 10) {
        int32_t i = n - 1;
        for (;;) {
            digits[i] = '0';
            if (i == 0) {
                /* 999... rolled all the way over: value is 1.000 * 10^(k+1) */
                digits[0] = '1';
                n = 1;
                k++;
                break;
            }
            i--;
            digits[i]++;
            if (digits[i] <= '9') break;
        }
    }

    while (n > 1 && digits[n - 1] == '0')
        n--;

    *dec_exp = k;
    return (int)n;
}

int cl_dtoa_shortest(double value, int is_double, char *digits, int32_t *dec_exp)
{
    uint64_t f;
    int32_t e;
    int unequal;

    if (is_double) {
        uint64_t bits;
        uint32_t expf;
        uint64_t mant;
        memcpy(&bits, &value, sizeof bits);
        expf = (uint32_t)((bits >> 52) & 0x7FF);
        mant = bits & 0x000FFFFFFFFFFFFFULL;
        if (expf == 0) {              /* subnormal: uniform spacing */
            f = mant;
            e = -1074;
            unequal = 0;
        } else {
            f = mant | 0x0010000000000000ULL;
            e = (int32_t)expf - 1075;
            unequal = (mant == 0 && expf > 1);
        }
    } else {
        /* value holds a single-float exactly; recover its 32-bit pattern.
         * The narrowing cast is exact for any exactly-representable value,
         * even on degraded FPUs — no rounding decision is involved. */
        float sf = (float)value;
        uint32_t bits, expf, mant;
        memcpy(&bits, &sf, sizeof bits);
        expf = (bits >> 23) & 0xFF;
        mant = bits & 0x007FFFFF;
        if (expf == 0) {
            f = mant;
            e = -149;
            unequal = 0;
        } else {
            f = mant | 0x00800000u;
            e = (int32_t)expf - 150;
            unequal = (mant == 0 && expf > 1);
        }
    }

    if (f == 0) return 0;             /* zero: caller handles */
    return dragon4_shortest(f, e, unequal, digits, dec_exp);
}

/* ================================================================
 * Correctly rounded decimal -> binary (exact strtod core)
 * ================================================================ */

/* Per-target IEEE parameters. */
typedef struct {
    int32_t mant_bits;   /* 53 / 24, hidden bit included */
    int32_t emin_sub;    /* exponent of the smallest subnormal ulp */
    int32_t emax;        /* max unbiased exponent of the leading bit */
    int32_t bias;
    int32_t L_over;      /* leading decimal exponent that forces inf */
    int32_t L_under;     /* leading decimal exponent that forces 0 */
    int32_t mant_shift;  /* mantissa field width (mant_bits - 1) */
} DtTarget;

static const DtTarget dt_double_target = { 53, -1074, 1023, 1023, 310, -327, 52 };
static const DtTarget dt_single_target = { 24, -149,  127,  127,  40,  -48,  23 };

/* Gay's bound: 768 significant decimal digits + sticky decide every
 * correctly rounded IEEE double (singles need far fewer). */
#define DT_MAX_SIG_DIGITS 768

/*
 * Round (M + 0.<sticky>) * 2^exp2 (M > 0, at most ~57 bits) to the target
 * format under round-to-nearest-even and return the assembled IEEE bit
 * pattern (without sign).
 */
static uint64_t dt_round_assemble(uint64_t M, int32_t exp2, int sticky,
                                  const DtTarget *t)
{
    int32_t mlen = bitlen_u64(M);
    int32_t E = mlen - 1 + exp2;     /* unbiased exponent of leading bit */
    int32_t keep, drop, ulp_exp;
    uint64_t mant;

    /* Normal numbers keep mant_bits; subnormals lose one bit per exponent
     * step below emin_norm (= emin_sub + mant_bits - 1). */
    keep = t->mant_bits;
    if (E < t->emin_sub + t->mant_bits - 1) {
        keep = E - t->emin_sub + 1;
        if (keep < 0)
            return 0;                 /* below half the smallest subnormal */
    }

    drop = mlen - keep;
    if (drop <= 0) {
        mant = M << (-drop);
        if (sticky) {
            /* Value has a fractional tail below the ulp: with drop <= 0 the
             * guard bit is 0, so RNE always rounds down — mant stands. */
        }
    } else {
        uint64_t rest_mask = (drop > 1) ? ((1ULL << (drop - 1)) - 1) : 0;
        int guard = (int)((M >> (drop - 1)) & 1);
        int rest = ((M & rest_mask) != 0) || sticky;
        mant = M >> drop;
        if (guard && (rest || (mant & 1)))
            mant++;
    }

    if (mant == 0)
        return 0;

    ulp_exp = exp2 + drop;
    /* Rounding may have carried into the next power of two. */
    while (bitlen_u64(mant) > t->mant_bits) {
        mant >>= 1;                   /* low bit is 0 after a carry-out */
        ulp_exp++;
    }

    E = ulp_exp + t->mant_bits - 1;
    if (bitlen_u64(mant) == t->mant_bits) {
        /* Normal (or subnormal that rounded up to the normal boundary). */
        if (E > t->emax)              /* overflow -> infinity */
            return (uint64_t)(2 * t->bias + 1) << t->mant_shift;
        return ((uint64_t)(E + t->bias) << t->mant_shift)
               | (mant & ((1ULL << t->mant_shift) - 1));
    }
    /* Subnormal: exponent field 0, mantissa stored as-is. */
    return mant;
}

/*
 * Parse a float token: [+-] digits [. digits] [E [+-] digits]
 * (exponent marker must already be normalized to 'E' or 'e').
 * The whole string up to NUL must be consumed.
 *
 * On success returns 1 and stores the IEEE bit pattern (double bits when
 * is_double, else single bits in the low 32) in *bits_out.  Returns 0 on
 * syntax error or (unreachable in practice) internal overflow — callers
 * fall back to the legacy libc path.
 */
int cl_parse_float_exact(const char *s, int is_double, uint64_t *bits_out)
{
    const DtTarget *t = is_double ? &dt_double_target : &dt_single_target;
    DtBig D, N, V, rem;
    uint64_t sign = 0, M;
    int32_t K = 0, expval = 0, ndig = 0, L, exp2, s2;
    int sticky = 0, started = 0, any_digit = 0, expneg = 0;
    const char *p = s;

    if (*p == '+' || *p == '-') {
        if (*p == '-') sign = 1;
        p++;
    }

    dt_zero(&D);
    for (; *p >= '0' && *p <= '9'; p++) {           /* integer part */
        any_digit = 1;
        if (!started && *p == '0') continue;
        started = 1;
        if (ndig < DT_MAX_SIG_DIGITS) {
            dt_mul_u32(&D, 10);
            dt_add_u32(&D, (uint32_t)(*p - '0'));
            ndig++;
        } else {
            sticky |= (*p != '0');
            K++;                       /* dropped integer digit */
        }
    }
    if (*p == '.') {
        p++;
        for (; *p >= '0' && *p <= '9'; p++) {       /* fraction part */
            any_digit = 1;
            if (!started && *p == '0') { K--; continue; }
            started = 1;
            if (ndig < DT_MAX_SIG_DIGITS) {
                dt_mul_u32(&D, 10);
                dt_add_u32(&D, (uint32_t)(*p - '0'));
                ndig++;
                K--;
            } else {
                sticky |= (*p != '0');
            }
        }
    }
    if (!any_digit)
        return 0;
    if (*p == 'E' || *p == 'e') {
        int expdigs = 0;
        p++;
        if (*p == '+' || *p == '-') {
            if (*p == '-') expneg = 1;
            p++;
        }
        for (; *p >= '0' && *p <= '9'; p++) {
            expdigs = 1;
            if (expval < 100000)       /* saturate; range guards decide */
                expval = expval * 10 + (*p - '0');
        }
        if (!expdigs)
            return 0;
        if (expneg) expval = -expval;
    }
    if (*p != '\0')
        return 0;

    if (dt_is_zero(&D)) {              /* 0, 0.000, 0e17, ... */
        *bits_out = is_double ? (sign << 63) : (sign << 31);
        return 1;
    }
    if (D.n < 0)
        return 0;

    K += expval;
    L = K + ndig - 1;                  /* decimal exponent of leading digit */
    if (L >= t->L_over) {              /* certain overflow */
        uint64_t inf = (uint64_t)(2 * t->bias + 1) << t->mant_shift;
        *bits_out = is_double ? ((sign << 63) | inf) : ((sign << 31) | inf);
        return 1;
    }
    if (L <= t->L_under) {             /* certain underflow */
        *bits_out = is_double ? (sign << 63) : (sign << 31);
        return 1;
    }

    /* value = D * 10^K = D * 5^K * 2^K.  The range guards above bound K:
     * upward K < L_over, downward |K| <= |L_under| + 767. */
    if (K >= 0) {
        int32_t nb;
        dt_copy(&N, &D);
        dt_mul_pow5(&N, K);
        if (N.n < 0) return 0;
        nb = dt_bitlen(&N);
        if (nb <= 57) {
            M = dt_top_bits(&N, nb);
            exp2 = K;
        } else {
            M = dt_top_bits(&N, 57);
            sticky |= dt_low_bits_nonzero(&N, nb - 57);
            exp2 = K + (nb - 57);
        }
    } else {
        /* Long division N / V by aligned compare-subtract, collecting the
         * top 57 quotient bits; everything further down only feeds sticky. */
        int32_t KK = -K, nb, vb, qpos, qcnt;
        uint64_t q64 = 0;
        dt_from_u64(&V, 1);
        dt_mul_pow5(&V, KK);
        if (V.n < 0) return 0;
        vb = dt_bitlen(&V);
        s2 = vb - dt_bitlen(&D) + t->mant_bits + 4;
        if (s2 < 0) s2 = 0;
        dt_copy(&N, &D);
        dt_shl(&N, s2);
        if (N.n < 0) return 0;
        nb = dt_bitlen(&N);

        /* rem = top vb bits of N, then feed remaining bits one at a time. */
        dt_zero(&rem);
        {
            int32_t i;
            for (i = nb - 1; i >= nb - vb; i--) {
                dt_shl(&rem, 1);
                if (dt_bit(&N, i)) dt_add_u32(&rem, 1);
            }
        }
        qcnt = 0;
        if (dt_cmp(&rem, &V) >= 0) { dt_sub(&rem, &V); q64 = 1; }
        qcnt = 1;
        qpos = nb - vb - 1;
        while (qpos >= 0 && qcnt < 57) {
            dt_shl(&rem, 1);
            if (dt_bit(&N, qpos)) dt_add_u32(&rem, 1);
            q64 <<= 1;
            if (dt_cmp(&rem, &V) >= 0) { dt_sub(&rem, &V); q64 |= 1; }
            qcnt++;
            qpos--;
        }
        if (rem.n < 0) return 0;
        /* Bits at positions <= qpos remain unprocessed: they (plus any
         * remainder) only add value strictly below the last collected
         * quotient bit. */
        sticky |= !dt_is_zero(&rem);
        if (qpos >= 0)
            sticky |= dt_low_bits_nonzero(&N, qpos + 1);
        exp2 = K - s2 + (qpos + 1);
        M = q64;
        if (M == 0) return 0;          /* cannot happen: quotient >= 2^55 */
    }

    {
        uint64_t mag = dt_round_assemble(M, exp2, sticky, t);
        *bits_out = is_double ? ((sign << 63) | mag) : ((sign << 31) | mag);
    }
    return 1;
}
