/*
 * float_math.c — Math functions: sqrt, exp, log, expt, trig (float-aware)
 *
 * These functions accept any numeric argument and return floats.
 * Type contagion: double-float input → double-float result.
 */

#include "builtins.h"
#include "bignum.h"
#include "float.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "../platform/platform.h"
#include <string.h>
#include <math.h>

/* Helper to register a builtin */
static void defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_cl);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    s->value = fn;
}

static void check_number(CL_Obj obj, const char *op)
{
    if (!CL_NUMBER_P(obj))
        cl_error(CL_ERR_TYPE, "%s: not a number", op);
}

/* Return a float result with proper type contagion.
 * If any input was double-float, result is double-float. */
static CL_Obj make_result(double val, int is_double)
{
    if (is_double)
        return cl_make_double_float(val);
    return cl_make_single_float((float)val);
}

/* (sqrt number) → float
 * Signals error for negative arguments (no complex numbers). */
static CL_Obj bi_sqrt(CL_Obj *args, int n)
{
    double val, result;
    CL_UNUSED(n);
    check_number(args[0], "SQRT");
    val = cl_to_double(args[0]);
    if (val < 0.0)
        cl_error(CL_ERR_TYPE, "SQRT: argument must be non-negative");
    result = sqrt(val);
    return make_result(result, CL_DOUBLE_FLOAT_P(args[0]));
}

/* (exp number) → float
 * Returns e^number. */
static CL_Obj bi_exp(CL_Obj *args, int n)
{
    double val, result;
    CL_UNUSED(n);
    check_number(args[0], "EXP");
    val = cl_to_double(args[0]);
    result = exp(val);
    return make_result(result, CL_DOUBLE_FLOAT_P(args[0]));
}

/* (log number &optional base) → float
 * One arg: natural logarithm.
 * Two args: logarithm base <base> = ln(number)/ln(base).
 * Signals error for non-positive arguments (no complex numbers). */
static CL_Obj bi_log(CL_Obj *args, int n)
{
    double val, result;
    int is_double;

    check_number(args[0], "LOG");
    val = cl_to_double(args[0]);
    if (val <= 0.0)
        cl_error(CL_ERR_TYPE, "LOG: argument must be positive");
    is_double = CL_DOUBLE_FLOAT_P(args[0]);

    if (n >= 2) {
        double base;
        check_number(args[1], "LOG");
        base = cl_to_double(args[1]);
        if (base <= 0.0)
            cl_error(CL_ERR_TYPE, "LOG: base must be positive");
        if (base == 1.0)
            cl_error(CL_ERR_DIVZERO, "LOG: base must not be 1");
        is_double = is_double || CL_DOUBLE_FLOAT_P(args[1]);
        result = log(val) / log(base);
    } else {
        result = log(val);
    }
    return make_result(result, is_double);
}

/* (expt base power) → number
 * Integer base + non-negative integer power: exact integer result.
 * Otherwise: float result via pow().
 * Signals error for negative base with non-integer power (no complex). */
static CL_Obj bi_expt_float(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    check_number(args[0], "EXPT");
    check_number(args[1], "EXPT");

    /* Both integers, non-negative exponent: use exact integer arithmetic */
    if (CL_INTEGER_P(args[0]) && CL_INTEGER_P(args[1])
        && !cl_arith_minusp(args[1])) {
        return cl_arith_expt(args[0], args[1]);
    }

    /* Float path */
    {
        double base = cl_to_double(args[0]);
        double power = cl_to_double(args[1]);
        double result;
        int is_double = CL_DOUBLE_FLOAT_P(args[0])
                     || CL_DOUBLE_FLOAT_P(args[1]);

        /* Special cases */
        if (power == 0.0)
            return make_result(1.0, is_double);

        if (base == 0.0) {
            if (power < 0.0)
                cl_error(CL_ERR_DIVZERO, "EXPT: division by zero");
            return make_result(0.0, is_double);
        }

        /* Negative base with non-integer power → complex (unsupported) */
        if (base < 0.0 && floor(power) != power)
            cl_error(CL_ERR_TYPE,
                "EXPT: negative base with non-integer exponent "
                "requires complex numbers");

        result = pow(base, power);
        return make_result(result, is_double);
    }
}

/* ================================================================
 * Trigonometric functions (step 12)
 * ================================================================ */

/* (sin number) → float */
static CL_Obj bi_sin(CL_Obj *args, int n)
{
    double val;
    CL_UNUSED(n);
    check_number(args[0], "SIN");
    val = cl_to_double(args[0]);
    return make_result(sin(val), CL_DOUBLE_FLOAT_P(args[0]));
}

/* (cos number) → float */
static CL_Obj bi_cos(CL_Obj *args, int n)
{
    double val;
    CL_UNUSED(n);
    check_number(args[0], "COS");
    val = cl_to_double(args[0]);
    return make_result(cos(val), CL_DOUBLE_FLOAT_P(args[0]));
}

/* (tan number) → float */
static CL_Obj bi_tan(CL_Obj *args, int n)
{
    double val;
    CL_UNUSED(n);
    check_number(args[0], "TAN");
    val = cl_to_double(args[0]);
    return make_result(tan(val), CL_DOUBLE_FLOAT_P(args[0]));
}

/* (asin number) → float
 * Domain: [-1, 1]. Signals error otherwise. */
static CL_Obj bi_asin(CL_Obj *args, int n)
{
    double val;
    CL_UNUSED(n);
    check_number(args[0], "ASIN");
    val = cl_to_double(args[0]);
    if (val < -1.0 || val > 1.0)
        cl_error(CL_ERR_TYPE, "ASIN: argument must be in [-1, 1]");
    return make_result(asin(val), CL_DOUBLE_FLOAT_P(args[0]));
}

/* (acos number) → float
 * Domain: [-1, 1]. Signals error otherwise. */
static CL_Obj bi_acos(CL_Obj *args, int n)
{
    double val;
    CL_UNUSED(n);
    check_number(args[0], "ACOS");
    val = cl_to_double(args[0]);
    if (val < -1.0 || val > 1.0)
        cl_error(CL_ERR_TYPE, "ACOS: argument must be in [-1, 1]");
    return make_result(acos(val), CL_DOUBLE_FLOAT_P(args[0]));
}

/* (atan y &optional x) → float
 * One arg: arctangent of y.
 * Two args: atan2(y, x). */
static CL_Obj bi_atan(CL_Obj *args, int n)
{
    double val;
    int is_double;

    check_number(args[0], "ATAN");
    is_double = CL_DOUBLE_FLOAT_P(args[0]);

    if (n >= 2) {
        double x;
        check_number(args[1], "ATAN");
        is_double = is_double || CL_DOUBLE_FLOAT_P(args[1]);
        val = cl_to_double(args[0]);
        x = cl_to_double(args[1]);
        return make_result(atan2(val, x), is_double);
    }

    val = cl_to_double(args[0]);
    return make_result(atan(val), is_double);
}

void cl_float_math_init(void)
{
    defun("SQRT", bi_sqrt, 1, 1);
    defun("EXP", bi_exp, 1, 1);
    defun("LOG", bi_log, 1, 2);
    /* Overwrites the integer-only EXPT from builtins_arith */
    defun("EXPT", bi_expt_float, 2, 2);
    /* Step 12: trigonometric functions */
    defun("SIN",  bi_sin,  1, 1);
    defun("COS",  bi_cos,  1, 1);
    defun("TAN",  bi_tan,  1, 1);
    defun("ASIN", bi_asin, 1, 1);
    defun("ACOS", bi_acos, 1, 1);
    defun("ATAN", bi_atan, 1, 2);
}
