#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
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
    s->value = fn;
}

/* --- Arithmetic --- */

static CL_Obj bi_add(CL_Obj *args, int n)
{
    int32_t sum = 0;
    int i;
    for (i = 0; i < n; i++) {
        if (!CL_FIXNUM_P(args[i]))
            cl_error(CL_ERR_TYPE, "+: not a number");
        sum += CL_FIXNUM_VAL(args[i]);
    }
    return CL_MAKE_FIXNUM(sum);
}

static CL_Obj bi_sub(CL_Obj *args, int n)
{
    int32_t result;
    int i;
    if (n == 0) return CL_MAKE_FIXNUM(0);
    if (!CL_FIXNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "-: not a number");
    if (n == 1) return CL_MAKE_FIXNUM(-CL_FIXNUM_VAL(args[0]));
    result = CL_FIXNUM_VAL(args[0]);
    for (i = 1; i < n; i++) {
        if (!CL_FIXNUM_P(args[i]))
            cl_error(CL_ERR_TYPE, "-: not a number");
        result -= CL_FIXNUM_VAL(args[i]);
    }
    return CL_MAKE_FIXNUM(result);
}

static CL_Obj bi_mul(CL_Obj *args, int n)
{
    int32_t product = 1;
    int i;
    for (i = 0; i < n; i++) {
        if (!CL_FIXNUM_P(args[i]))
            cl_error(CL_ERR_TYPE, "*: not a number");
        product *= CL_FIXNUM_VAL(args[i]);
    }
    return CL_MAKE_FIXNUM(product);
}

static CL_Obj bi_div(CL_Obj *args, int n)
{
    int32_t result;
    int i;
    if (!CL_FIXNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "/: not a number");
    result = CL_FIXNUM_VAL(args[0]);
    for (i = 1; i < n; i++) {
        if (!CL_FIXNUM_P(args[i]))
            cl_error(CL_ERR_TYPE, "/: not a number");
        if (CL_FIXNUM_VAL(args[i]) == 0)
            cl_error(CL_ERR_DIVZERO, "Division by zero");
        result /= CL_FIXNUM_VAL(args[i]);
    }
    return CL_MAKE_FIXNUM(result);
}

static CL_Obj bi_mod(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_FIXNUM_P(args[0]) || !CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE, "MOD: not a number");
    if (CL_FIXNUM_VAL(args[1]) == 0)
        cl_error(CL_ERR_DIVZERO, "Division by zero");
    return CL_MAKE_FIXNUM(CL_FIXNUM_VAL(args[0]) % CL_FIXNUM_VAL(args[1]));
}

static CL_Obj bi_1plus(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_FIXNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "1+: not a number");
    return CL_MAKE_FIXNUM(CL_FIXNUM_VAL(args[0]) + 1);
}

static CL_Obj bi_1minus(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_FIXNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "1-: not a number");
    return CL_MAKE_FIXNUM(CL_FIXNUM_VAL(args[0]) - 1);
}

/* --- Comparison --- */

static CL_Obj bi_numeq(CL_Obj *args, int n)
{
    int i;
    if (!CL_FIXNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "=: not a number");
    for (i = 1; i < n; i++) {
        if (!CL_FIXNUM_P(args[i]))
            cl_error(CL_ERR_TYPE, "=: not a number");
        if (CL_FIXNUM_VAL(args[i]) != CL_FIXNUM_VAL(args[0]))
            return CL_NIL;
    }
    return SYM_T;
}

static CL_Obj bi_lt(CL_Obj *args, int n)
{
    int i;
    for (i = 0; i < n - 1; i++) {
        if (!CL_FIXNUM_P(args[i]) || !CL_FIXNUM_P(args[i+1]))
            cl_error(CL_ERR_TYPE, "<: not a number");
        if (!(CL_FIXNUM_VAL(args[i]) < CL_FIXNUM_VAL(args[i+1])))
            return CL_NIL;
    }
    return SYM_T;
}

static CL_Obj bi_gt(CL_Obj *args, int n)
{
    int i;
    for (i = 0; i < n - 1; i++) {
        if (!CL_FIXNUM_P(args[i]) || !CL_FIXNUM_P(args[i+1]))
            cl_error(CL_ERR_TYPE, ">: not a number");
        if (!(CL_FIXNUM_VAL(args[i]) > CL_FIXNUM_VAL(args[i+1])))
            return CL_NIL;
    }
    return SYM_T;
}

static CL_Obj bi_le(CL_Obj *args, int n)
{
    int i;
    for (i = 0; i < n - 1; i++) {
        if (!CL_FIXNUM_P(args[i]) || !CL_FIXNUM_P(args[i+1]))
            cl_error(CL_ERR_TYPE, "<=: not a number");
        if (!(CL_FIXNUM_VAL(args[i]) <= CL_FIXNUM_VAL(args[i+1])))
            return CL_NIL;
    }
    return SYM_T;
}

static CL_Obj bi_ge(CL_Obj *args, int n)
{
    int i;
    for (i = 0; i < n - 1; i++) {
        if (!CL_FIXNUM_P(args[i]) || !CL_FIXNUM_P(args[i+1]))
            cl_error(CL_ERR_TYPE, ">=: not a number");
        if (!(CL_FIXNUM_VAL(args[i]) >= CL_FIXNUM_VAL(args[i+1])))
            return CL_NIL;
    }
    return SYM_T;
}

/* --- Numeric predicates --- */

static CL_Obj bi_zerop(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_FIXNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "ZEROP: not a number");
    return CL_FIXNUM_VAL(args[0]) == 0 ? SYM_T : CL_NIL;
}

static CL_Obj bi_plusp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_FIXNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "PLUSP: not a number");
    return CL_FIXNUM_VAL(args[0]) > 0 ? SYM_T : CL_NIL;
}

static CL_Obj bi_minusp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_FIXNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "MINUSP: not a number");
    return CL_FIXNUM_VAL(args[0]) < 0 ? SYM_T : CL_NIL;
}

static CL_Obj bi_abs(CL_Obj *args, int n)
{
    int32_t v;
    CL_UNUSED(n);
    if (!CL_FIXNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "ABS: not a number");
    v = CL_FIXNUM_VAL(args[0]);
    return CL_MAKE_FIXNUM(v < 0 ? -v : v);
}

static CL_Obj bi_max(CL_Obj *args, int n)
{
    int32_t m;
    int i;
    if (!CL_FIXNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "MAX: not a number");
    m = CL_FIXNUM_VAL(args[0]);
    for (i = 1; i < n; i++) {
        int32_t v;
        if (!CL_FIXNUM_P(args[i]))
            cl_error(CL_ERR_TYPE, "MAX: not a number");
        v = CL_FIXNUM_VAL(args[i]);
        if (v > m) m = v;
    }
    return CL_MAKE_FIXNUM(m);
}

static CL_Obj bi_min(CL_Obj *args, int n)
{
    int32_t m;
    int i;
    if (!CL_FIXNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "MIN: not a number");
    m = CL_FIXNUM_VAL(args[0]);
    for (i = 1; i < n; i++) {
        int32_t v;
        if (!CL_FIXNUM_P(args[i]))
            cl_error(CL_ERR_TYPE, "MIN: not a number");
        v = CL_FIXNUM_VAL(args[i]);
        if (v < m) m = v;
    }
    return CL_MAKE_FIXNUM(m);
}

/* --- Registration --- */

void cl_builtins_arith_init(void)
{
    /* Arithmetic */
    defun("+", bi_add, 0, -1);
    defun("-", bi_sub, 1, -1);
    defun("*", bi_mul, 0, -1);
    defun("/", bi_div, 1, -1);
    defun("MOD", bi_mod, 2, 2);
    defun("1+", bi_1plus, 1, 1);
    defun("1-", bi_1minus, 1, 1);

    /* Comparison */
    defun("=", bi_numeq, 1, -1);
    defun("<", bi_lt, 1, -1);
    defun(">", bi_gt, 1, -1);
    defun("<=", bi_le, 1, -1);
    defun(">=", bi_ge, 1, -1);

    /* Numeric predicates */
    defun("ZEROP", bi_zerop, 1, 1);
    defun("PLUSP", bi_plusp, 1, 1);
    defun("MINUSP", bi_minusp, 1, 1);
    defun("ABS", bi_abs, 1, 1);
    defun("MAX", bi_max, 1, -1);
    defun("MIN", bi_min, 1, -1);
}
