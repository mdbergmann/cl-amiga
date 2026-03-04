#ifndef CL_RATIO_H
#define CL_RATIO_H

#include "types.h"

/*
 * Ratio (exact fraction) support.
 *
 * Ratios are always stored in lowest terms with positive denominator.
 * If the result would have denominator 1 or numerator 0, an integer
 * is returned instead.
 */

/* Create a normalized ratio: reduces by GCD, ensures positive denominator,
 * demotes to integer if den=1 or num=0. Both num and den must be integers. */
CL_Obj cl_make_ratio_normalized(CL_Obj num, CL_Obj den);

/* Arithmetic — both args can be integer or ratio */
CL_Obj cl_ratio_add(CL_Obj a, CL_Obj b);
CL_Obj cl_ratio_sub(CL_Obj a, CL_Obj b);
CL_Obj cl_ratio_mul(CL_Obj a, CL_Obj b);
CL_Obj cl_ratio_div(CL_Obj a, CL_Obj b);
CL_Obj cl_ratio_negate(CL_Obj a);
CL_Obj cl_ratio_abs(CL_Obj a);

/* Comparison */
int cl_ratio_compare(CL_Obj a, CL_Obj b);
int cl_ratio_zerop(CL_Obj a);
int cl_ratio_plusp(CL_Obj a);
int cl_ratio_minusp(CL_Obj a);

/* Equality and hashing */
int cl_ratio_equal(CL_Obj a, CL_Obj b);
uint32_t cl_ratio_hash(CL_Obj obj);

/* Conversion */
double cl_ratio_to_double(CL_Obj obj);

/* Accessors — work on both ratios and integers */
CL_Obj cl_numerator(CL_Obj obj);
CL_Obj cl_denominator(CL_Obj obj);

#endif /* CL_RATIO_H */
