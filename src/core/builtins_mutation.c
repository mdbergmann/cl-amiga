#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "compiler.h"
#include "../platform/platform.h"
#include "../platform/platform_thread.h"
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
        cl_signal_type_error(args[0], "CONS", "RPLACA");
    cell = (CL_Cons *)CL_OBJ_TO_PTR(args[0]);
    cell->car = args[1];
    return args[0];  /* CL spec: returns the cons */
}

static CL_Obj bi_rplacd(CL_Obj *args, int n)
{
    CL_Cons *cell;
    CL_UNUSED(n);
    if (!CL_CONS_P(args[0]))
        cl_signal_type_error(args[0], "CONS", "RPLACD");
    cell = (CL_Cons *)CL_OBJ_TO_PTR(args[0]);
    cell->cdr = args[1];
    return args[0];  /* CL spec: returns the cons */
}

/* --- Symbol accessors --- */

static CL_Obj bi_symbol_value(CL_Obj *args, int n)
{
    CL_Obj val;
    CL_UNUSED(n);
    if (!CL_SYMBOL_OR_NIL_P(args[0]))
        cl_signal_type_error(args[0], "SYMBOL", "SYMBOL-VALUE");
    val = cl_symbol_value(args[0]);
    if (val == CL_UNBOUND)
        cl_signal_unbound_variable(args[0]);
    return val;
}

static CL_Obj bi_symbol_function(CL_Obj *args, int n)
{
    CL_Symbol *s;
    CL_UNUSED(n);
    if (!CL_SYMBOL_OR_NIL_P(args[0]))
        cl_signal_type_error(args[0], "SYMBOL", "SYMBOL-FUNCTION");
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    if (s->function == CL_UNBOUND)
        cl_signal_undefined_function(args[0]);
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
    if (!CL_SYMBOL_OR_NIL_P(args[0]))
        cl_signal_type_error(args[0], "SYMBOL", "SET");
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    if (s->flags & CL_SYM_CONSTANT)
        cl_error(CL_ERR_GENERAL, "Cannot assign to constant variable: %s",
                 cl_symbol_name(args[0]));
    cl_set_symbol_value(args[0], args[1]);
    return args[1];
}

static CL_Obj bi_set_symbol_function(CL_Obj *args, int n)
{
    CL_Symbol *s;
    CL_UNUSED(n);
    if (!CL_SYMBOL_OR_NIL_P(args[0]))
        cl_signal_type_error(args[0], "SYMBOL", "%SET-SYMBOL-FUNCTION");
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    s->function = args[1];
    return args[1];
}

/* --- Boundp --- */

static CL_Obj bi_boundp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_SYMBOL_OR_NIL_P(args[0]))
        cl_signal_type_error(args[0], "SYMBOL", "BOUNDP");
    return cl_symbol_boundp(args[0]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_symbol_constant_p(CL_Obj *args, int n)
{
    CL_Symbol *s;
    CL_UNUSED(n);
    if (!CL_SYMBOL_P(args[0]))
        return CL_NIL;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    return (s->flags & CL_SYM_CONSTANT) ? SYM_T : CL_NIL;
}

extern CL_Obj setf_fn_table;

/* Helper: if name is (setf sym), look up the setf-fn symbol in setf_fn_table.
   Returns CL_NIL if not found. */
static CL_Obj lookup_setf_fn_sym(CL_Obj name)
{
    if (CL_CONS_P(name) && cl_car(name) == SYM_SETF) {
        CL_Obj accessor = cl_car(cl_cdr(name));
        CL_Obj entry;
        if (!CL_SYMBOL_P(accessor))
            return CL_NIL;
        cl_tables_rdlock();
        entry = setf_fn_table;
        while (!CL_NULL_P(entry)) {
            CL_Obj pair = cl_car(entry);
            if (cl_car(pair) == accessor) {
                CL_Obj result = cl_cdr(pair);
                cl_tables_rwunlock();
                return result;
            }
            entry = cl_cdr(entry);
        }
        cl_tables_rwunlock();
        return CL_NIL;
    }
    return CL_NIL;
}

static CL_Obj bi_fboundp(CL_Obj *args, int n)
{
    extern int cl_macro_p(CL_Obj name);
    extern CL_Obj bi_special_operator_p(CL_Obj *args, int n);
    CL_Symbol *s;
    CL_UNUSED(n);
    if (CL_CONS_P(args[0])) {
        /* (fboundp '(setf name)) — only a function binding is meaningful here. */
        CL_Obj setf_sym = lookup_setf_fn_sym(args[0]);
        if (CL_NULL_P(setf_sym)) return CL_NIL;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(setf_sym);
        return (s->function != CL_UNBOUND && !CL_NULL_P(s->function))
            ? SYM_T : CL_NIL;
    }
    if (!CL_SYMBOL_OR_NIL_P(args[0]))
        cl_signal_type_error(args[0], "SYMBOL", "FBOUNDP");
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    /* CLHS: fboundp is true for functions, macros, AND special operators. */
    if (s->function != CL_UNBOUND && !CL_NULL_P(s->function))
        return SYM_T;
    if (cl_macro_p(args[0]))
        return SYM_T;
    if (bi_special_operator_p(args, 1) == SYM_T)
        return SYM_T;
    return CL_NIL;
}

static CL_Obj bi_fmakunbound(CL_Obj *args, int n)
{
    CL_Symbol *s;
    CL_UNUSED(n);
    if (CL_CONS_P(args[0])) {
        /* (fmakunbound '(setf name)) */
        CL_Obj setf_sym = lookup_setf_fn_sym(args[0]);
        if (!CL_NULL_P(setf_sym)) {
            s = (CL_Symbol *)CL_OBJ_TO_PTR(setf_sym);
            s->function = CL_UNBOUND;
        }
        return args[0];
    }
    if (!CL_SYMBOL_OR_NIL_P(args[0]))
        cl_signal_type_error(args[0], "SYMBOL", "FMAKUNBOUND");
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    s->function = CL_UNBOUND;
    return args[0];
}

static CL_Obj bi_makunbound(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_SYMBOL_OR_NIL_P(args[0]))
        cl_signal_type_error(args[0], "SYMBOL", "MAKUNBOUND");
    cl_set_symbol_value(args[0], CL_UNBOUND);
    return args[0];
}

/* CLHS special-operator-p: true if SYMBOL names a CL special operator.
 * The set matches compile_expr() in compiler.c — any symbol dispatched
 * there as a special form is reported here.  Keep the two lists in sync
 * when adding or removing special-form compilation branches. */
CL_Obj bi_special_operator_p(CL_Obj *args, int n)
{
    CL_Obj sym;
    CL_UNUSED(n);
    if (!CL_SYMBOL_OR_NIL_P(args[0]))
        cl_signal_type_error(args[0], "SYMBOL", "SPECIAL-OPERATOR-P");
    sym = args[0];
    /* The 25 ANSI special operators (CLHS 3.1.2.1.2.1).  LAMBDA is NOT
     * one of them — it expands via a macro to FUNCTION (3.1.2.1.2.4) —
     * so it must not be reported here.  Any addition to this set must
     * be matched by the dispatch in compile_expr(). */
    if (sym == SYM_QUOTE || sym == SYM_IF || sym == SYM_PROGN
        || sym == SYM_LET || sym == SYM_LETSTAR
        || sym == SYM_SETQ || sym == SYM_FUNCTION
        || sym == SYM_BLOCK || sym == SYM_RETURN_FROM
        || sym == SYM_FLET || sym == SYM_LABELS
        || sym == SYM_TAGBODY || sym == SYM_GO
        || sym == SYM_CATCH || sym == SYM_THROW
        || sym == SYM_UNWIND_PROTECT
        || sym == SYM_MULTIPLE_VALUE_CALL || sym == SYM_MULTIPLE_VALUE_PROG1
        || sym == SYM_EVAL_WHEN || sym == SYM_LOAD_TIME_VALUE
        || sym == SYM_LOCALLY || sym == SYM_PROGV
        || sym == SYM_MACROLET || sym == SYM_SYMBOL_MACROLET
        || sym == SYM_THE)
        return SYM_T;
    return CL_NIL;
}

static CL_Obj bi_register_setf_function(CL_Obj *args, int n)
{
    /* (%register-setf-function accessor-sym setf-fn-sym) */
    CL_UNUSED(n);
    cl_register_setf_function(args[0], args[1]);
    return args[0];
}

extern CL_Obj setf_expander_table;
extern CL_Obj setf_table;

/* (%get-defsetf-setter accessor-sym) — look up defsetf setter for accessor */
static CL_Obj bi_get_defsetf_setter(CL_Obj *args, int n)
{
    CL_Obj name = args[0];
    CL_Obj result = CL_NIL;
    CL_Obj entry;
    CL_UNUSED(n);
    cl_tables_rdlock();
    entry = setf_table;
    while (!CL_NULL_P(entry)) {
        CL_Obj pair = cl_car(entry);
        if (cl_car(pair) == name) {
            result = cl_cdr(pair);
            break;
        }
        entry = cl_cdr(entry);
    }
    cl_tables_rwunlock();
    return result;
}

static CL_Obj bi_register_setf_expander(CL_Obj *args, int n)
{
    /* (%register-setf-expander accessor-sym expander-fn) */
    CL_Obj pair;
    CL_UNUSED(n);
    pair = cl_cons(args[0], args[1]);
    cl_tables_wrlock();
    setf_expander_table = cl_cons(pair, setf_expander_table);
    cl_tables_rwunlock();
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
    cl_register_builtin("%SYMBOL-CONSTANT-P", bi_symbol_constant_p, 1, 1, cl_package_clamiga);
    defun("FBOUNDP", bi_fboundp, 1, 1);
    defun("FMAKUNBOUND", bi_fmakunbound, 1, 1);
    defun("MAKUNBOUND", bi_makunbound, 1, 1);
    defun("SPECIAL-OPERATOR-P", bi_special_operator_p, 1, 1);
    cl_register_builtin("%REGISTER-SETF-FUNCTION", bi_register_setf_function, 2, 2, cl_package_clamiga);
    cl_register_builtin("%REGISTER-SETF-EXPANDER", bi_register_setf_expander, 2, 2, cl_package_clamiga);
    cl_register_builtin("%GET-DEFSETF-SETTER", bi_get_defsetf_setter, 1, 1, cl_package_clamiga);
}
