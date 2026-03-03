#ifndef CL_BIGNUM_H
#define CL_BIGNUM_H

#include "types.h"

/*
 * Bignum (arbitrary-precision integer) support.
 *
 * Uses 16-bit limbs in little-endian order.
 * Multiplication needs only uint32_t intermediates (no uint64_t).
 * All results are normalized: demoted to fixnum if they fit.
 */

/* Allocation */
CL_Obj cl_bignum_normalize(CL_Obj obj);
CL_Obj cl_bignum_from_int32(int32_t val);

/* Unified arithmetic dispatch (handles fixnum/bignum mix) */
CL_Obj cl_arith_add(CL_Obj a, CL_Obj b);
CL_Obj cl_arith_sub(CL_Obj a, CL_Obj b);
CL_Obj cl_arith_mul(CL_Obj a, CL_Obj b);
CL_Obj cl_arith_truncate(CL_Obj a, CL_Obj b);
CL_Obj cl_arith_mod(CL_Obj a, CL_Obj b);
CL_Obj cl_arith_negate(CL_Obj a);
CL_Obj cl_arith_abs(CL_Obj a);
int    cl_arith_compare(CL_Obj a, CL_Obj b);
int    cl_arith_zerop(CL_Obj a);
int    cl_arith_plusp(CL_Obj a);
int    cl_arith_minusp(CL_Obj a);

/* Value equality for eql */
int cl_bignum_equal(CL_Obj a, CL_Obj b);

/* Hash for hash tables */
uint32_t cl_bignum_hash(CL_Obj obj);

/* String conversion */
CL_Obj cl_bignum_from_string(const char *str, int len, int negative);
void cl_bignum_print(CL_Obj obj, void (*out)(const char *));

/* Extended integer operations */
CL_Obj cl_arith_gcd(CL_Obj a, CL_Obj b);
CL_Obj cl_arith_expt(CL_Obj base, CL_Obj exp);
CL_Obj cl_arith_isqrt(CL_Obj n);
CL_Obj cl_arith_ash(CL_Obj n, CL_Obj count);
CL_Obj cl_arith_logand(CL_Obj a, CL_Obj b);
CL_Obj cl_arith_logior(CL_Obj a, CL_Obj b);
CL_Obj cl_arith_logxor(CL_Obj a, CL_Obj b);
CL_Obj cl_arith_lognot(CL_Obj a);
int    cl_arith_integer_length(CL_Obj n);
int    cl_arith_evenp(CL_Obj a);

/* Initialize bignum subsystem (intern MOST-POSITIVE-FIXNUM etc.) */
void cl_bignum_init(void);

#endif /* CL_BIGNUM_H */
