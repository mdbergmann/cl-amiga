/*
 * float.c — IEEE 754 floating-point conversions and arithmetic
 *
 * Contagion rule: if either operand is double-float, result is double-float;
 * otherwise single-float. Integer operands are promoted to float.
 */

#include "float.h"
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

int cl_float_compare(CL_Obj a, CL_Obj b)
{
    double da = cl_to_double(a);
    double db = cl_to_double(b);
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
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
