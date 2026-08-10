#ifndef CL_FLOAT_H
#define CL_FLOAT_H

#include "types.h"

/*
 * IEEE 754 floating-point support: single-float (32-bit) and double-float (64-bit).
 *
 * Both types are heap-allocated (32-bit tagged pointer leaves no room for
 * immediate floats). On Amiga, the default build uses software float;
 * a separate 68040/68881 build generates hardware FPU instructions.
 */

/* --- Single-float (IEEE 754 binary32) --- */

typedef struct {
    CL_Header hdr;      /* 4 bytes */
    float value;         /* 4 bytes IEEE 754 */
} CL_SingleFloat;       /* 8 bytes total */

/* --- Double-float (IEEE 754 binary64) --- */

typedef struct {
    CL_Header hdr;      /* 4 bytes */
    double value;        /* 8 bytes IEEE 754 */
} CL_DoubleFloat;       /* 12 bytes total */

/* --- Predicates --- */

#define CL_SINGLE_FLOAT_P(obj) \
    (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_SINGLE_FLOAT)

#define CL_DOUBLE_FLOAT_P(obj) \
    (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_DOUBLE_FLOAT)

#define CL_FLOATP(obj) (CL_SINGLE_FLOAT_P(obj) || CL_DOUBLE_FLOAT_P(obj))

#define CL_REALP(obj)   (CL_INTEGER_P(obj) || CL_RATIO_P(obj) || CL_FLOATP(obj))
#define CL_NUMBER_P(obj) (CL_REALP(obj) || CL_COMPLEX_P(obj))

/* --- Allocation (defined in mem.c) --- */

CL_Obj cl_make_single_float(float value);
CL_Obj cl_make_double_float(double value);

/* --- Conversion --- */

double cl_to_double(CL_Obj obj);
float  cl_to_float(CL_Obj obj);

/* Write into BUF the shortest decimal string (printf "%g" style, may use an
   exponent) that reads back to exactly VALUE at the requested precision class:
   is_double != 0 for double-float (binary64), 0 for single-float (binary32).
   This is what the CL printer needs — "%g" alone defaults to 6 significant
   digits and silently drops precision (e.g. 1234.567f0 -> "1234.57", which is
   a *different* float).  Returns the string length. */
int cl_float_shortest_g(char *buf, int bufsz, double value, int is_double);

/* --- Exact, FPU-independent decimal conversion (float_dtoa.c) ---
 *
 * Pure integer arithmetic — bit-identical results on every platform,
 * immune to degraded FPUs (Vampire/Apollo FDIV) and libc quality. */

/* Shortest round-trip digits for finite nonzero VALUE (sign ignored):
   fills DIGITS (ASCII, no trailing zeros, capacity >= 18) and *DEC_EXP with
   value ~= 0.digits * 10^dec_exp.  Returns digit count, or 0 for zero /
   internal overflow (callers fall back to the libc path). */
int cl_dtoa_shortest(double value, int is_double, char *digits, int32_t *dec_exp);

/* Correctly rounded parse of "[+-]digits[.digits][E[+-]digits]" (marker
   already normalized to E/e; entire string must be consumed).  Stores IEEE
   bits (double bits, or single bits in the low 32) in *BITS_OUT.  Returns 1
   on success, 0 on syntax error or (unreachable) internal overflow. */
int cl_parse_float_exact(const char *s, int is_double, uint64_t *bits_out);

/* --- Float arithmetic (called from cl_arith_* when either operand is float) --- */

CL_Obj cl_float_add(CL_Obj a, CL_Obj b);
CL_Obj cl_float_sub(CL_Obj a, CL_Obj b);
CL_Obj cl_float_mul(CL_Obj a, CL_Obj b);
CL_Obj cl_float_div(CL_Obj a, CL_Obj b);
CL_Obj cl_float_negate(CL_Obj a);
int    cl_float_compare(CL_Obj a, CL_Obj b);
int    cl_float_zerop(CL_Obj a);
int    cl_float_plusp(CL_Obj a);
int    cl_float_minusp(CL_Obj a);

/* Exact IEEE-bit-pattern rational of a finite float (integer or ratio).
   Errors for NaN/Infinity. */
CL_Obj cl_float_to_exact_rational(CL_Obj f);

#endif /* CL_FLOAT_H */
