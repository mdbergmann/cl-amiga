#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "compiler.h"
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

/* --- Mutation --- */

static CL_Obj bi_rplaca(CL_Obj *args, int n)
{
    CL_Cons *cell;
    CL_UNUSED(n);
    if (!CL_CONS_P(args[0]))
        cl_error(CL_ERR_TYPE, "RPLACA: not a cons");
    cell = (CL_Cons *)CL_OBJ_TO_PTR(args[0]);
    cell->car = args[1];
    return args[0];  /* CL spec: returns the cons */
}

static CL_Obj bi_rplacd(CL_Obj *args, int n)
{
    CL_Cons *cell;
    CL_UNUSED(n);
    if (!CL_CONS_P(args[0]))
        cl_error(CL_ERR_TYPE, "RPLACD: not a cons");
    cell = (CL_Cons *)CL_OBJ_TO_PTR(args[0]);
    cell->cdr = args[1];
    return args[0];  /* CL spec: returns the cons */
}

/* --- Symbol accessors --- */

static CL_Obj bi_symbol_value(CL_Obj *args, int n)
{
    CL_Symbol *s;
    CL_UNUSED(n);
    if (!CL_SYMBOL_P(args[0]))
        cl_error(CL_ERR_TYPE, "SYMBOL-VALUE: not a symbol");
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    if (s->value == CL_UNBOUND)
        cl_error(CL_ERR_UNBOUND, "SYMBOL-VALUE: unbound variable: %s",
                 cl_symbol_name(args[0]));
    return s->value;
}

static CL_Obj bi_symbol_function(CL_Obj *args, int n)
{
    CL_Symbol *s;
    CL_UNUSED(n);
    if (!CL_SYMBOL_P(args[0]))
        cl_error(CL_ERR_TYPE, "SYMBOL-FUNCTION: not a symbol");
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    if (s->function == CL_UNBOUND)
        cl_error(CL_ERR_UNDEFINED, "SYMBOL-FUNCTION: undefined function: %s",
                 cl_symbol_name(args[0]));
    return s->function;
}

/* --- Setf helpers --- */

static CL_Obj bi_setf_nth(CL_Obj *args, int n)
{
    /* (%setf-nth n list val) — walk list to nth cons, rplaca, return val */
    int32_t idx;
    CL_Obj list;
    CL_Cons *cell;
    CL_UNUSED(n);
    if (!CL_FIXNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "%SETF-NTH: index must be a number");
    idx = CL_FIXNUM_VAL(args[0]);
    list = args[1];
    while (idx > 0 && !CL_NULL_P(list)) {
        list = cl_cdr(list);
        idx--;
    }
    if (CL_NULL_P(list) || !CL_CONS_P(list))
        cl_error(CL_ERR_ARGS, "%SETF-NTH: index out of range");
    cell = (CL_Cons *)CL_OBJ_TO_PTR(list);
    cell->car = args[2];
    return args[2];
}

static CL_Obj bi_set_symbol_value(CL_Obj *args, int n)
{
    CL_Symbol *s;
    CL_UNUSED(n);
    if (!CL_SYMBOL_P(args[0]))
        cl_error(CL_ERR_TYPE, "SET: not a symbol");
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    if (s->flags & CL_SYM_CONSTANT)
        cl_error(CL_ERR_GENERAL, "Cannot assign to constant variable: %s",
                 cl_symbol_name(args[0]));
    s->value = args[1];
    return args[1];
}

static CL_Obj bi_set_symbol_function(CL_Obj *args, int n)
{
    CL_Symbol *s;
    CL_UNUSED(n);
    if (!CL_SYMBOL_P(args[0]))
        cl_error(CL_ERR_TYPE, "%SET-SYMBOL-FUNCTION: not a symbol");
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    s->function = args[1];
    return args[1];
}

/* --- Boundp --- */

static CL_Obj bi_boundp(CL_Obj *args, int n)
{
    CL_Symbol *s;
    CL_UNUSED(n);
    if (!CL_SYMBOL_P(args[0]))
        cl_error(CL_ERR_TYPE, "BOUNDP: not a symbol");
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    return (s->value != CL_UNBOUND) ? SYM_T : CL_NIL;
}

static CL_Obj bi_fboundp(CL_Obj *args, int n)
{
    CL_Symbol *s;
    CL_UNUSED(n);
    if (!CL_SYMBOL_P(args[0]))
        cl_error(CL_ERR_TYPE, "FBOUNDP: not a symbol");
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    return (s->function != CL_UNBOUND && !CL_NULL_P(s->function))
        ? SYM_T : CL_NIL;
}

static CL_Obj bi_fmakunbound(CL_Obj *args, int n)
{
    CL_Symbol *s;
    CL_UNUSED(n);
    if (!CL_SYMBOL_P(args[0]))
        cl_error(CL_ERR_TYPE, "FMAKUNBOUND: not a symbol");
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    s->function = CL_UNBOUND;
    return args[0];
}

static CL_Obj bi_register_setf_function(CL_Obj *args, int n)
{
    /* (%register-setf-function accessor-sym setf-fn-sym) */
    CL_UNUSED(n);
    cl_register_setf_function(args[0], args[1]);
    return args[0];
}

/* --- Registration --- */

void cl_builtins_mutation_init(void)
{
    /* Mutation */
    defun("RPLACA", bi_rplaca, 2, 2);
    defun("RPLACD", bi_rplacd, 2, 2);

    /* Symbol accessors */
    defun("SYMBOL-VALUE", bi_symbol_value, 1, 1);
    defun("SYMBOL-FUNCTION", bi_symbol_function, 1, 1);
    defun("FDEFINITION", bi_symbol_function, 1, 1);

    /* Setf helpers */
    cl_register_builtin("%SETF-NTH", bi_setf_nth, 3, 3, cl_package_clamiga);
    cl_register_builtin("%SET-SYMBOL-VALUE", bi_set_symbol_value, 2, 2, cl_package_clamiga);
    cl_register_builtin("%SET-SYMBOL-FUNCTION", bi_set_symbol_function, 2, 2, cl_package_clamiga);
    defun("SET", bi_set_symbol_value, 2, 2);

    /* Boundp / Fboundp */
    defun("BOUNDP", bi_boundp, 1, 1);
    defun("FBOUNDP", bi_fboundp, 1, 1);
    defun("FMAKUNBOUND", bi_fmakunbound, 1, 1);
    cl_register_builtin("%REGISTER-SETF-FUNCTION", bi_register_setf_function, 2, 2, cl_package_clamiga);
}
