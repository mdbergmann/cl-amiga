/*
 * test_float_dtoa.c — exact, FPU-independent float <-> decimal conversion.
 *
 * Validates float_dtoa.c (integer-only shortest dtoa + correctly rounded
 * parse) and the "%g"-compatible layout in cl_float_shortest_g.  On the
 * host, libc's strtod/printf are correctly rounded, so they double as an
 * oracle for the deterministic fuzz loops: every value the exact converter
 * prints must strtod back to the same bits, and every decimal string must
 * parse to the same bits libc produces.  On a machine whose libc/FPU is
 * degraded (the Vampire FPGA divides with ~42 mantissa bits), those loops
 * are exactly the checks the old snprintf/strtod printer failed.
 */

#include "test.h"
#include "../src/core/float.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static uint64_t dbits(double d)  { uint64_t u; memcpy(&u, &d, sizeof u); return u; }
static double   bitsd(uint64_t u){ double d;   memcpy(&d, &u, sizeof d); return d; }
static uint32_t fbits(float f)   { uint32_t u; memcpy(&u, &f, sizeof u); return u; }
static float    bitsf(uint32_t u){ float f;    memcpy(&f, &u, sizeof f); return f; }

/* ---------- shortest digits: exact digit/exponent vectors ---------- */

static void check_dtoa_d(double v, const char *want_digits, int32_t want_k)
{
    char digits[20];
    int32_t k = 0;
    int n = cl_dtoa_shortest(v, 1, digits, &k);
    digits[n] = '\0';
    ASSERT(n > 0);
    ASSERT_STR_EQ(digits, want_digits);
    ASSERT_EQ_INT((int)k, (int)want_k);
}

TEST(dtoa_digit_vectors)
{
    check_dtoa_d(bitsd(0x400921FB54442D18ULL), "3141592653589793", 1); /* pi */
    check_dtoa_d(0.1,     "1", 0);
    check_dtoa_d(0.5,     "5", 0);
    check_dtoa_d(5.0,     "5", 1);
    check_dtoa_d(100.0,   "1", 3);
    check_dtoa_d(1e22,    "1", 23);   /* largest exactly-representable 10^n */
    check_dtoa_d(bitsd(0x3FD5555555555555ULL), "3333333333333333", 0); /* 1/3 */
    check_dtoa_d(bitsd(0x0000000000000001ULL), "5", -323);  /* min subnormal */
    check_dtoa_d(bitsd(0x000FFFFFFFFFFFFFULL), "2225073858507201", -307);
    check_dtoa_d(bitsd(0x7FEFFFFFFFFFFFFFULL), "17976931348623157", 309); /* DBL_MAX */
    check_dtoa_d(bitsd(0x0010000000000000ULL), "22250738585072014", -307); /* min normal */
    /* Powers of two have an asymmetric gap: 2^10 = 1024 must not shorten
       to "1e3" (1000 would read back to a different float, but the
       asymmetric interval is what makes e.g. 9e999... cases subtle). */
    check_dtoa_d(1024.0, "1024", 4);
    check_dtoa_d(0.3,    "3", 0);
    check_dtoa_d(bitsd(0x3FD3333333333334ULL), "30000000000000004", 0); /* 0.1+0.2 */
}

TEST(dtoa_single_vectors)
{
    char digits[20];
    int32_t k = 0;
    int n;

    n = cl_dtoa_shortest((double)bitsf(0x40490FDBu), 0, digits, &k); /* pi f32 */
    digits[n] = '\0';
    ASSERT_STR_EQ(digits, "31415927");
    ASSERT_EQ_INT((int)k, 1);

    n = cl_dtoa_shortest((double)bitsf(0x00000001u), 0, digits, &k); /* min sub */
    digits[n] = '\0';
    ASSERT_STR_EQ(digits, "1");
    ASSERT_EQ_INT((int)k, -44);

    n = cl_dtoa_shortest((double)bitsf(0x7F7FFFFFu), 0, digits, &k); /* FLT_MAX */
    digits[n] = '\0';
    ASSERT_STR_EQ(digits, "34028235");
    ASSERT_EQ_INT((int)k, 39);

    n = cl_dtoa_shortest(1234.567, 0, digits, &k);
    digits[n] = '\0';
    ASSERT_STR_EQ(digits, "1234567");
    ASSERT_EQ_INT((int)k, 4);
}

/* ---------- cl_float_shortest_g: exact "%g"-shape strings ---------- */

static void check_g_d(double v, const char *want)
{
    char buf[48];
    cl_float_shortest_g(buf, (int)sizeof buf, v, 1);
    ASSERT_STR_EQ(buf, want);
}

static void check_g_s(float v, const char *want)
{
    char buf[48];
    cl_float_shortest_g(buf, (int)sizeof buf, (double)v, 0);
    ASSERT_STR_EQ(buf, want);
}

TEST(shortest_g_format)
{
    check_g_d(bitsd(0x400921FB54442D18ULL), "3.141592653589793");
    check_g_d(5.0,      "5");
    check_g_d(100.0,    "100");
    check_g_d(0.1,      "0.1");
    check_g_d(0.0001,   "0.0001");        /* X = -4: still fixed, like %g */
    check_g_d(0.00001,  "1e-05");         /* X = -5: scientific */
    check_g_d(1e15,     "1e+15");         /* X = 15 >= P: scientific */
    check_g_d(999999999999999.0, "999999999999999");   /* X = 14: fixed */
    check_g_d(1e22,     "1e+22");
    check_g_d(-2.5,     "-2.5");
    check_g_d(0.0,      "0");
    check_g_d(-0.0,     "-0");
    check_g_d(bitsd(0x0000000000000001ULL), "5e-324");
    check_g_d(bitsd(0x7FEFFFFFFFFFFFFFULL), "1.7976931348623157e+308");
    check_g_d(123456789012345678.0, "1.2345678901234568e+17");
    check_g_d(12345678901234.567,   "12345678901234.566");

    check_g_s(1234.567f, "1234.567");
    check_g_s(5.0f,      "5");
    check_g_s(1e-5f,     "1e-05");
    check_g_s(100000.0f, "100000");       /* X = 5 < 6: fixed */
    check_g_s(1000000.0f,"1e+06");        /* X = 6 >= 6: scientific */
    check_g_s(-0.0f,     "-0");
}

/* ---------- exact parse: hard deterministic vectors ---------- */

static void check_parse_d(const char *s, uint64_t want)
{
    uint64_t got = 0;
    int ok = cl_parse_float_exact(s, 1, &got);
    ASSERT(ok);
    if (got != want) {
        printf("  parse \"%s\": got %016llx want %016llx\n",
               s, (unsigned long long)got, (unsigned long long)want);
        test_current_failed = 1;
    }
}

static void check_parse_s(const char *s, uint32_t want)
{
    uint64_t got = 0;
    int ok = cl_parse_float_exact(s, 0, &got);
    ASSERT(ok);
    if ((uint32_t)got != want) {
        printf("  parse-f \"%s\": got %08lx want %08lx\n",
               s, (unsigned long)(uint32_t)got, (unsigned long)want);
        test_current_failed = 1;
    }
}

TEST(parse_vectors)
{
    check_parse_d("3.141592653589793",   0x400921FB54442D18ULL);
    check_parse_d("3.1415926535897931",  0x400921FB54442D18ULL);
    check_parse_d("0.1",                 0x3FB999999999999AULL);
    check_parse_d("2.5",                 0x4004000000000000ULL);
    check_parse_d("1E3",                 0x408F400000000000ULL);   /* 1000 */
    check_parse_d("-0.0",                0x8000000000000000ULL);
    check_parse_d("0.000",               0x0000000000000000ULL);
    check_parse_d("1.",                  0x3FF0000000000000ULL);
    check_parse_d(".5",                  0x3FE0000000000000ULL);

    /* The infamous halfway case that hung Java/PHP parsers: rounds DOWN
       to the largest subnormal, one ulp below the smallest normal. */
    check_parse_d("2.2250738585072011e-308", 0x000FFFFFFFFFFFFFULL);
    /* Smallest normal, from its exact 17-digit form. */
    check_parse_d("2.2250738585072014e-308", 0x0010000000000000ULL);
    /* Subnormal parsing. */
    check_parse_d("5e-324",              0x0000000000000001ULL);
    check_parse_d("4.9406564584124654e-324", 0x0000000000000001ULL);
    /* Below half the smallest subnormal: 0.  Above: min subnormal. */
    check_parse_d("2.4e-324",            0x0000000000000000ULL);
    check_parse_d("2.5e-324",            0x0000000000000001ULL);
    /* Overflow boundary: halfway between DBL_MAX and the next (absent)
       float is 1.7976931348623158...e308; at or above rounds to inf. */
    check_parse_d("1.7976931348623157e308", 0x7FEFFFFFFFFFFFFFULL);
    check_parse_d("1.8e308",             0x7FF0000000000000ULL);
    check_parse_d("1e309",               0x7FF0000000000000ULL);
    check_parse_d("-1e309",              0xFFF0000000000000ULL);
    check_parse_d("1e-330",              0x0000000000000000ULL);
    check_parse_d("1e999999999",         0x7FF0000000000000ULL);
    check_parse_d("1e-999999999",        0x0000000000000000ULL);

    /* 2 + 2^-51 exactly representable; a truncated expansion still rounds
       to it (nearest by a wide margin). */
    check_parse_d("2.0000000000000004440892098500626",  0x4000000000000001ULL);
    /* Round-to-nearest-EVEN tie: 2 + 2^-52 is exactly halfway between
       2.0 (even mantissa) and 2+2^-51 (odd) -> rounds down to 2.0. */
    check_parse_d("2.00000000000000022204460492503130808472633361816406250",
                  0x4000000000000000ULL);
    /* One digit appended beyond the exact tie: strictly above -> up. */
    check_parse_d("2.000000000000000222044604925031308084726333618164062501",
                  0x4000000000000001ULL);
    /* 1 + 2^-53 exactly (the tie): rounds to even = 1.0. */
    check_parse_d("1.00000000000000011102230246251565404236316680908203125",
                  0x3FF0000000000000ULL);
    /* One quantum above the tie must round up. */
    check_parse_d("1.000000000000000111022302462515654042363166809082031251",
                  0x3FF0000000000001ULL);

    /* Singles parse directly to binary32 (no double-rounding through a
       double).  This value reads differently under (float)strtod(...):
       decimal -> nearest double -> nearest float gives 0x3F800001, but the
       decimal is exactly the double halfway point and lies BELOW the
       single halfway point, so direct rounding gives 1.0f. */
    check_parse_s("1.000000059604644775390625", 0x3F800000u);
    check_parse_s("3.14159274",           0x40490FDBu);
    check_parse_s("3.4028235e38",         0x7F7FFFFFu);
    check_parse_s("3.5e38",               0x7F800000u);   /* inf */
    check_parse_s("1e-46",                0x00000000u);
    check_parse_s("1.4e-45",              0x00000001u);
    check_parse_s("-2.5",                 0xC0200000u);

    /* Syntax rejections (whole string must parse). */
    {
        uint64_t got;
        ASSERT(!cl_parse_float_exact("",      1, &got));
        ASSERT(!cl_parse_float_exact("+",     1, &got));
        ASSERT(!cl_parse_float_exact(".",     1, &got));
        ASSERT(!cl_parse_float_exact("1.5x",  1, &got));
        ASSERT(!cl_parse_float_exact("1e",    1, &got));
        ASSERT(!cl_parse_float_exact("1e+",   1, &got));
        ASSERT(!cl_parse_float_exact("e5",    1, &got));
    }
}

/* Very long digit strings: Gay's bound says 768 significant digits plus a
 * sticky flag decide every double.  Build the exact decimal expansion of
 * 1 + 2^-52 + 2^-53 (the halfway point between 1+2^-52 and 1+2^-51 —
 * every digit matters) and perturb the 768th-plus digits. */
TEST(parse_long_digits)
{
    /* 2^-52 + 2^-53 = 3 * 2^-53; decimal expansion has 53 fractional
       digits ending in ...5.  Compute it in decimal by long division:
       3 / 2^53, digit by digit — pure integer math, self-contained. */
    char buf[900];
    int i;
    /* 3 * 5^53 has at most 40 digits; 3/2^53 = 3*5^53 / 10^53. */
    char num[64];
    /* compute 3 * 5^53 as a decimal string via repeated *5 */
    int len = 1;
    num[0] = 3;
    for (i = 0; i < 53; i++) {
        int c = 0, j;
        for (j = 0; j < len; j++) {
            int t = num[j] * 5 + c;
            num[j] = (char)(t % 10);
            c = t / 10;
        }
        while (c) { num[len++] = (char)(c % 10); c /= 10; }
    }
    /* value = 1 + 0.<zeros>digits with (53 - len) leading zeros */
    {
        int o = 0, z = 53 - len;
        buf[o++] = '1'; buf[o++] = '.';
        for (i = 0; i < z; i++) buf[o++] = '0';
        for (i = len - 1; i >= 0; i--) buf[o++] = (char)('0' + num[i]);
        buf[o] = '\0';
        /* Exact halfway: round to even -> mantissa ...ULL & ~1 end: the
           neighbors are 1+2^-52 (odd mantissa 1) and 1+2^-51 (mantissa 2,
           even): tie must pick 0x3FF0000000000002. */
        check_parse_d(buf, 0x3FF0000000000002ULL);
        /* Append 700 zeros then a '1': now strictly above the halfway
           point — must round up to mantissa 2 as well (same result here),
           but appending the digit to the *truncated-at-768* tail exercises
           the sticky path: use the DOWN-side tie instead. */
    }
    /* Down-side: 1 + 2^-53 exactly (tie -> even -> 1.0).  Its expansion is
       0.5^53 shifted: digits of 5^53 / 10^53. */
    {
        int o = 0, z, l2 = 1;
        char n2[64];
        n2[0] = 1;
        for (i = 0; i < 53; i++) {
            int c = 0, j;
            for (j = 0; j < l2; j++) {
                int t = n2[j] * 5 + c;
                n2[j] = (char)(t % 10);
                c = t / 10;
            }
            while (c) { n2[l2++] = (char)(c % 10); c /= 10; }
        }
        z = 53 - l2;
        buf[o++] = '1'; buf[o++] = '.';
        for (i = 0; i < z; i++) buf[o++] = '0';
        for (i = l2 - 1; i >= 0; i--) buf[o++] = (char)('0' + n2[i]);
        /* exact tie: round to even -> 1.0 */
        buf[o] = '\0';
        check_parse_d(buf, 0x3FF0000000000000ULL);
        /* pad with zeros out past 768 significant digits, then a final 1:
           value now strictly above the tie -> must round UP.  The '1' sits
           beyond the 768-digit window, so only the sticky flag can see it. */
        for (i = 0; i < 750; i++) buf[o++] = '0';
        buf[o++] = '1';
        buf[o] = '\0';
        check_parse_d(buf, 0x3FF0000000000001ULL);
    }
}

/* ---------- fuzz: libc as oracle (host strtod is correctly rounded) ---- */

static uint64_t lcg = 0x243F6A8885A308D3ULL;   /* fixed seed: deterministic */
static uint64_t rnd64(void)
{
    lcg = lcg * 6364136223846793005ULL + 1442695040888963407ULL;
    /* xorshift the top since LCG low bits are weak */
    return lcg ^ (lcg >> 29);
}

TEST(fuzz_dtoa_roundtrip_double)
{
    char digits[20], str[48];
    int32_t k;
    int i, n, j, o;
    for (i = 0; i < 20000; i++) {
        uint64_t bits = rnd64();
        double v, back;
        bits &= 0x7FFFFFFFFFFFFFFFULL;                 /* positive */
        if (((bits >> 52) & 0x7FF) == 0x7FF) continue; /* skip inf/nan */
        if (bits == 0) continue;
        v = bitsd(bits);
        n = cl_dtoa_shortest(v, 1, digits, &k);
        ASSERT(n >= 1 && n <= 17);
        /* render as d.ddddde<k-1> and let libc read it back */
        o = 0;
        str[o++] = digits[0];
        if (n > 1) {
            str[o++] = '.';
            for (j = 1; j < n; j++) str[o++] = digits[j];
        }
        o += sprintf(str + o, "e%d", (int)(k - 1));
        back = strtod(str, (char **)0);
        if (dbits(back) != bits) {
            printf("  roundtrip %016llx -> \"%s\" -> %016llx\n",
                   (unsigned long long)bits, str,
                   (unsigned long long)dbits(back));
            test_current_failed = 1;
            break;
        }
    }
}

TEST(fuzz_dtoa_roundtrip_single)
{
    char digits[20], str[48];
    int32_t k;
    int i, n, j, o;
    for (i = 0; i < 20000; i++) {
        uint32_t bits = (uint32_t)rnd64();
        float v, back;
        bits &= 0x7FFFFFFFu;
        if (((bits >> 23) & 0xFF) == 0xFF) continue;
        if (bits == 0) continue;
        v = bitsf(bits);
        n = cl_dtoa_shortest((double)v, 0, digits, &k);
        ASSERT(n >= 1 && n <= 9);
        o = 0;
        str[o++] = digits[0];
        if (n > 1) {
            str[o++] = '.';
            for (j = 1; j < n; j++) str[o++] = digits[j];
        }
        o += sprintf(str + o, "e%d", (int)(k - 1));
        back = strtof(str, (char **)0);
        if (fbits(back) != bits) {
            printf("  roundtrip-f %08lx -> \"%s\" -> %08lx\n",
                   (unsigned long)bits, str, (unsigned long)fbits(back));
            test_current_failed = 1;
            break;
        }
    }
}

TEST(fuzz_parse_matches_libc)
{
    char str[64];
    int i;
    for (i = 0; i < 20000; i++) {
        /* random digit strings: 1..19 int digits, 0..19 frac, exp -320..320 */
        int nint = 1 + (int)(rnd64() % 19);
        int nfrac = (int)(rnd64() % 20);
        int e = (int)(rnd64() % 641) - 320;
        int o = 0, j;
        uint64_t mine = 0;
        double libc_v;
        if (rnd64() & 1) str[o++] = '-';
        for (j = 0; j < nint; j++) str[o++] = (char)('0' + rnd64() % 10);
        if (nfrac) {
            str[o++] = '.';
            for (j = 0; j < nfrac; j++) str[o++] = (char)('0' + rnd64() % 10);
        }
        o += sprintf(str + o, "e%d", e);
        ASSERT(cl_parse_float_exact(str, 1, &mine));
        libc_v = strtod(str, (char **)0);
        if (mine != dbits(libc_v)) {
            printf("  parse \"%s\": mine %016llx libc %016llx\n",
                   str, (unsigned long long)mine,
                   (unsigned long long)dbits(libc_v));
            test_current_failed = 1;
            break;
        }
    }
}

TEST(fuzz_parse_single_matches_libc)
{
    char str[64];
    int i;
    for (i = 0; i < 20000; i++) {
        int nint = 1 + (int)(rnd64() % 10);
        int nfrac = (int)(rnd64() % 12);
        int e = (int)(rnd64() % 91) - 45;
        int o = 0, j;
        uint64_t mine = 0;
        float libc_v;
        if (rnd64() & 1) str[o++] = '-';
        for (j = 0; j < nint; j++) str[o++] = (char)('0' + rnd64() % 10);
        if (nfrac) {
            str[o++] = '.';
            for (j = 0; j < nfrac; j++) str[o++] = (char)('0' + rnd64() % 10);
        }
        o += sprintf(str + o, "e%d", e);
        ASSERT(cl_parse_float_exact(str, 0, &mine));
        libc_v = strtof(str, (char **)0);
        if ((uint32_t)mine != fbits(libc_v)) {
            printf("  parse-f \"%s\": mine %08lx libc %08lx\n",
                   str, (unsigned long)(uint32_t)mine,
                   (unsigned long)fbits(libc_v));
            test_current_failed = 1;
            break;
        }
    }
}

/* Every power of two across the double range: exercises the asymmetric-
 * gap setup (unequal branch) exhaustively, subnormals included. */
TEST(dtoa_all_powers_of_two)
{
    char digits[20], str[48];
    int32_t k;
    int e, n, j, o;
    for (e = -1074; e <= 1023; e++) {
        uint64_t bits;
        double v, back;
        if (e < -1022)
            bits = 1ULL << (e + 1074);                 /* subnormal */
        else
            bits = ((uint64_t)(e + 1023)) << 52;       /* normal */
        v = bitsd(bits);
        n = cl_dtoa_shortest(v, 1, digits, &k);
        ASSERT(n >= 1 && n <= 17);
        o = 0;
        str[o++] = digits[0];
        if (n > 1) {
            str[o++] = '.';
            for (j = 1; j < n; j++) str[o++] = digits[j];
        }
        o += sprintf(str + o, "e%d", (int)(k - 1));
        back = strtod(str, (char **)0);
        if (dbits(back) != bits) {
            printf("  pow2 e=%d: \"%s\" -> %016llx want %016llx\n",
                   e, str, (unsigned long long)dbits(back),
                   (unsigned long long)bits);
            test_current_failed = 1;
            break;
        }
    }
}

int main(void)
{
    test_init();
    RUN(dtoa_digit_vectors);
    RUN(dtoa_single_vectors);
    RUN(shortest_g_format);
    RUN(parse_vectors);
    RUN(parse_long_digits);
    RUN(fuzz_dtoa_roundtrip_double);
    RUN(fuzz_dtoa_roundtrip_single);
    RUN(fuzz_parse_matches_libc);
    RUN(fuzz_parse_single_matches_libc);
    RUN(dtoa_all_powers_of_two);
    REPORT();
}
