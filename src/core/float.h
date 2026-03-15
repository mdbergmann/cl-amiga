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

#endif /* CL_FLOAT_H */
