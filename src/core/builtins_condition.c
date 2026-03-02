/*
 * builtins_condition.c — Condition type infrastructure
 *
 * Provides condition type hierarchy, constructors, accessors,
 * and type predicates for the CL condition system.
 */

#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "vm.h"
#include "printer.h"
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

/*
 * Condition type hierarchy (CL spec):
 *
 * condition
 *   simple-condition
 *   warning
 *     simple-warning        (also simple-condition)
 *   serious-condition
 *     error
 *       simple-error        (also simple-condition)
 *       type-error
 *       unbound-variable
 *       undefined-function
 *       program-error
 *       control-error
 *       arithmetic-error
 *         division-by-zero
 *
 * Stored as alist: ((type-sym parent-sym ...) ...)
 * Multiple parents for types with multiple inheritance.
 */
static CL_Obj condition_hierarchy = CL_NIL;

/* Build the hierarchy alist during init */
static void build_hierarchy(void)
{
    /* Helper: push (type parent1 parent2 ...) onto hierarchy */
    /* Build bottom-up so hierarchy is a proper alist */

    /* division-by-zero -> arithmetic-error */
    condition_hierarchy = cl_cons(
        cl_cons(SYM_DIVISION_BY_ZERO,
                cl_cons(SYM_ARITHMETIC_ERROR, CL_NIL)),
        condition_hierarchy);

    /* arithmetic-error -> error */
    condition_hierarchy = cl_cons(
        cl_cons(SYM_ARITHMETIC_ERROR,
                cl_cons(SYM_ERROR_COND, CL_NIL)),
        condition_hierarchy);

    /* control-error -> error */
    condition_hierarchy = cl_cons(
        cl_cons(SYM_CONTROL_ERROR,
                cl_cons(SYM_ERROR_COND, CL_NIL)),
        condition_hierarchy);

    /* program-error -> error */
    condition_hierarchy = cl_cons(
        cl_cons(SYM_PROGRAM_ERROR,
                cl_cons(SYM_ERROR_COND, CL_NIL)),
        condition_hierarchy);

    /* undefined-function -> error */
    condition_hierarchy = cl_cons(
        cl_cons(SYM_UNDEFINED_FUNCTION_COND,
                cl_cons(SYM_ERROR_COND, CL_NIL)),
        condition_hierarchy);

    /* unbound-variable -> error */
    condition_hierarchy = cl_cons(
        cl_cons(SYM_UNBOUND_VARIABLE_COND,
                cl_cons(SYM_ERROR_COND, CL_NIL)),
        condition_hierarchy);

    /* type-error -> error */
    condition_hierarchy = cl_cons(
        cl_cons(SYM_TYPE_ERROR,
                cl_cons(SYM_ERROR_COND, CL_NIL)),
        condition_hierarchy);

    /* simple-error -> error, simple-condition */
    condition_hierarchy = cl_cons(
        cl_cons(SYM_SIMPLE_ERROR,
                cl_cons(SYM_ERROR_COND,
                        cl_cons(SYM_SIMPLE_CONDITION, CL_NIL))),
        condition_hierarchy);

    /* simple-warning -> warning, simple-condition */
    condition_hierarchy = cl_cons(
        cl_cons(SYM_SIMPLE_WARNING,
                cl_cons(SYM_WARNING,
                        cl_cons(SYM_SIMPLE_CONDITION, CL_NIL))),
        condition_hierarchy);

    /* error -> serious-condition */
    condition_hierarchy = cl_cons(
        cl_cons(SYM_ERROR_COND,
                cl_cons(SYM_SERIOUS_CONDITION, CL_NIL)),
        condition_hierarchy);

    /* serious-condition -> condition */
    condition_hierarchy = cl_cons(
        cl_cons(SYM_SERIOUS_CONDITION,
                cl_cons(SYM_CONDITION, CL_NIL)),
        condition_hierarchy);

    /* simple-condition -> condition */
    condition_hierarchy = cl_cons(
        cl_cons(SYM_SIMPLE_CONDITION,
                cl_cons(SYM_CONDITION, CL_NIL)),
        condition_hierarchy);

    /* warning -> condition */
    condition_hierarchy = cl_cons(
        cl_cons(SYM_WARNING,
                cl_cons(SYM_CONDITION, CL_NIL)),
        condition_hierarchy);

    /* condition -> (no parent) */
    condition_hierarchy = cl_cons(
        cl_cons(SYM_CONDITION, CL_NIL),
        condition_hierarchy);
}

/* Look up parent list for a type in the hierarchy alist.
 * Returns CDR of the matching entry (list of parents), or NIL. */
static CL_Obj find_parents(CL_Obj type_sym)
{
    CL_Obj list = condition_hierarchy;
    while (!CL_NULL_P(list)) {
        CL_Obj entry = cl_car(list);
        if (cl_car(entry) == type_sym)
            return cl_cdr(entry);
        list = cl_cdr(list);
    }
    return CL_NIL;
}

/* Check if cond_type is a subtype of (or equal to) handler_type.
 * Walks the hierarchy recursively. Public for use by handler-bind etc. */
int cl_condition_type_matches(CL_Obj cond_type, CL_Obj handler_type)
{
    CL_Obj parents;

    /* Identity check */
    if (cond_type == handler_type)
        return 1;

    /* Walk parent chain */
    parents = find_parents(cond_type);
    while (!CL_NULL_P(parents)) {
        if (cl_condition_type_matches(cl_car(parents), handler_type))
            return 1;
        parents = cl_cdr(parents);
    }

    return 0;
}

/* --- Slot lookup helper --- */

static CL_Obj slot_lookup(CL_Obj slots, CL_Obj key)
{
    while (!CL_NULL_P(slots)) {
        CL_Obj pair = cl_car(slots);
        if (cl_car(pair) == key)
            return cl_cdr(pair);
        slots = cl_cdr(slots);
    }
    return CL_NIL;
}

/* Check if a symbol is a known condition type in the hierarchy */
int cl_is_condition_type(CL_Obj type_sym)
{
    CL_Obj list = condition_hierarchy;
    while (!CL_NULL_P(list)) {
        CL_Obj entry = cl_car(list);
        if (cl_car(entry) == type_sym)
            return 1;
        list = cl_cdr(list);
    }
    return 0;
}

/* --- Builtins --- */

/* (make-condition type &key ...) */
static CL_Obj bi_make_condition(CL_Obj *args, int n)
{
    CL_Obj type_sym = args[0];
    CL_Obj slots = CL_NIL;
    CL_Obj report_string = CL_NIL;
    int i;

    if (!CL_SYMBOL_P(type_sym) && !CL_NULL_P(type_sym))
        cl_error(CL_ERR_TYPE, "MAKE-CONDITION: type must be a symbol");

    /* Parse keyword initargs */
    CL_GC_PROTECT(slots);
    CL_GC_PROTECT(report_string);

    for (i = 1; i + 1 < n; i += 2) {
        CL_Obj key = args[i];
        CL_Obj val = args[i + 1];
        CL_Obj pair = cl_cons(key, val);
        slots = cl_cons(pair, slots);

        /* Extract :format-control as report_string if it's a string */
        if (key == KW_FORMAT_CONTROL && CL_STRING_P(val))
            report_string = val;
    }

    {
        CL_Obj result = cl_make_condition(type_sym, slots, report_string);
        CL_GC_UNPROTECT(2);
        return result;
    }
}

/* (conditionp obj) */
static CL_Obj bi_conditionp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_CONDITION_P(args[0]) ? SYM_T : CL_NIL;
}

/* (condition-type-name condition) */
static CL_Obj bi_condition_type_name(CL_Obj *args, int n)
{
    CL_Condition *cond;
    CL_UNUSED(n);
    if (!CL_CONDITION_P(args[0]))
        cl_error(CL_ERR_TYPE, "CONDITION-TYPE-NAME: not a condition");
    cond = (CL_Condition *)CL_OBJ_TO_PTR(args[0]);
    return cond->type_name;
}

/* (simple-condition-format-control condition) */
static CL_Obj bi_simple_condition_format_control(CL_Obj *args, int n)
{
    CL_Condition *cond;
    CL_UNUSED(n);
    if (!CL_CONDITION_P(args[0]))
        cl_error(CL_ERR_TYPE, "SIMPLE-CONDITION-FORMAT-CONTROL: not a condition");
    cond = (CL_Condition *)CL_OBJ_TO_PTR(args[0]);
    return slot_lookup(cond->slots, KW_FORMAT_CONTROL);
}

/* (simple-condition-format-arguments condition) */
static CL_Obj bi_simple_condition_format_arguments(CL_Obj *args, int n)
{
    CL_Condition *cond;
    CL_UNUSED(n);
    if (!CL_CONDITION_P(args[0]))
        cl_error(CL_ERR_TYPE, "SIMPLE-CONDITION-FORMAT-ARGUMENTS: not a condition");
    cond = (CL_Condition *)CL_OBJ_TO_PTR(args[0]);
    return slot_lookup(cond->slots, KW_FORMAT_ARGUMENTS);
}

/* (type-error-datum condition) */
static CL_Obj bi_type_error_datum(CL_Obj *args, int n)
{
    CL_Condition *cond;
    CL_UNUSED(n);
    if (!CL_CONDITION_P(args[0]))
        cl_error(CL_ERR_TYPE, "TYPE-ERROR-DATUM: not a condition");
    cond = (CL_Condition *)CL_OBJ_TO_PTR(args[0]);
    return slot_lookup(cond->slots, KW_DATUM);
}

/* (type-error-expected-type condition) */
static CL_Obj bi_type_error_expected_type(CL_Obj *args, int n)
{
    CL_Condition *cond;
    CL_UNUSED(n);
    if (!CL_CONDITION_P(args[0]))
        cl_error(CL_ERR_TYPE, "TYPE-ERROR-EXPECTED-TYPE: not a condition");
    cond = (CL_Condition *)CL_OBJ_TO_PTR(args[0]);
    return slot_lookup(cond->slots, KW_EXPECTED_TYPE);
}

/* --- Signaling --- */

/* Create a condition object from a C error code and message string */
CL_Obj cl_create_condition_from_error(int code, const char *msg)
{
    CL_Obj type_sym;
    CL_Obj slots = CL_NIL;
    CL_Obj report = CL_NIL;

    switch (code) {
    case CL_ERR_TYPE:      type_sym = SYM_TYPE_ERROR; break;
    case CL_ERR_UNBOUND:   type_sym = SYM_UNBOUND_VARIABLE_COND; break;
    case CL_ERR_UNDEFINED: type_sym = SYM_UNDEFINED_FUNCTION_COND; break;
    case CL_ERR_DIVZERO:   type_sym = SYM_DIVISION_BY_ZERO; break;
    case CL_ERR_OVERFLOW:  type_sym = SYM_ARITHMETIC_ERROR; break;
    case CL_ERR_ARGS:      type_sym = SYM_PROGRAM_ERROR; break;
    default:               type_sym = SYM_SIMPLE_ERROR; break;
    }

    if (msg && msg[0]) {
        report = cl_make_string(msg, (uint32_t)strlen(msg));
        CL_GC_PROTECT(report);
        {
            CL_Obj pair = cl_cons(KW_FORMAT_CONTROL, report);
            slots = cl_cons(pair, CL_NIL);
        }
        CL_GC_UNPROTECT(1);
    }

    return cl_make_condition(type_sym, slots, report);
}

/* Walk handler stack top-down, calling matching handlers.
 * Handlers run in the signaler's dynamic context without unwinding.
 * Returns NIL if no handler transferred control. */
CL_Obj cl_signal_condition(CL_Obj condition)
{
    CL_Condition *cond;
    int i, saved_top;

    if (!CL_CONDITION_P(condition))
        return CL_NIL;

    cond = (CL_Condition *)CL_OBJ_TO_PTR(condition);

    for (i = cl_handler_top - 1; i >= 0; i--) {
        if (cl_condition_type_matches(cond->type_name,
                                      cl_handler_stack[i].type_name)) {
            /* Truncate handler stack to this handler's mark so that
             * the handler itself doesn't see itself in the stack
             * (prevents infinite recursion if handler signals same type) */
            saved_top = cl_handler_top;
            cl_handler_top = cl_handler_stack[i].handler_mark;
            cl_vm_apply(cl_handler_stack[i].handler, &condition, 1);
            cl_handler_top = saved_top;
            /* Handler returned normally — continue searching */
        }
    }

    return CL_NIL;
}

/* Helper: coerce signal/warn/error argument into a condition object.
 * If arg is already a condition, return it.
 * If arg is a symbol, make-condition with that type.
 * If arg is a string, make simple-condition with :format-control.
 * default_type is used when arg is a string (e.g. SIMPLE-ERROR, SIMPLE-WARNING). */
static CL_Obj coerce_to_condition(CL_Obj *args, int n, CL_Obj default_type)
{
    CL_Obj arg = args[0];

    if (CL_CONDITION_P(arg))
        return arg;

    if (CL_SYMBOL_P(arg)) {
        /* (signal 'type-error :datum x :expected-type y) */
        CL_Obj slots = CL_NIL;
        CL_Obj report = CL_NIL;
        int i;

        CL_GC_PROTECT(slots);
        CL_GC_PROTECT(report);

        for (i = 1; i + 1 < n; i += 2) {
            CL_Obj key = args[i];
            CL_Obj val = args[i + 1];
            CL_Obj pair = cl_cons(key, val);
            slots = cl_cons(pair, slots);
            if (key == KW_FORMAT_CONTROL && CL_STRING_P(val))
                report = val;
        }

        {
            CL_Obj result = cl_make_condition(arg, slots, report);
            CL_GC_UNPROTECT(2);
            return result;
        }
    }

    if (CL_STRING_P(arg)) {
        /* (signal "message") → simple-condition with :format-control */
        CL_Obj pair, slots;
        CL_GC_PROTECT(arg);
        pair = cl_cons(KW_FORMAT_CONTROL, arg);
        CL_GC_PROTECT(pair);
        slots = cl_cons(pair, CL_NIL);
        {
            CL_Obj result = cl_make_condition(default_type, slots, arg);
            CL_GC_UNPROTECT(2);
            return result;
        }
    }

    cl_error(CL_ERR_TYPE, "Expected condition, symbol, or string");
    return CL_NIL;
}

/* (signal condition-or-type &rest args) */
static CL_Obj bi_signal(CL_Obj *args, int n)
{
    CL_Obj cond = coerce_to_condition(args, n, SYM_SIMPLE_CONDITION);
    cl_signal_condition(cond);
    return CL_NIL;
}

/* (warn condition-or-type &rest args) */
static CL_Obj bi_warn(CL_Obj *args, int n)
{
    CL_Obj cond = coerce_to_condition(args, n, SYM_SIMPLE_WARNING);
    cl_signal_condition(cond);

    /* No handler transferred control — print warning and return NIL */
    {
        CL_Condition *c = (CL_Condition *)CL_OBJ_TO_PTR(cond);
        platform_write_string("WARNING: ");
        if (!CL_NULL_P(c->report_string)) {
            CL_String *s = (CL_String *)CL_OBJ_TO_PTR(c->report_string);
            platform_write_string(s->data);
        } else {
            char buf[128];
            cl_prin1_to_string(c->type_name, buf, sizeof(buf));
            platform_write_string(buf);
        }
        platform_write_string("\n");
    }
    return CL_NIL;
}

/* (error condition-or-type &rest args) — new version with condition system */
static CL_Obj bi_error(CL_Obj *args, int n)
{
    CL_Obj cond = coerce_to_condition(args, n, SYM_SIMPLE_ERROR);
    cl_signal_condition(cond);

    /* No handler transferred control — fall to C error handler */
    {
        CL_Condition *c = (CL_Condition *)CL_OBJ_TO_PTR(cond);
        if (!CL_NULL_P(c->report_string)) {
            CL_String *s = (CL_String *)CL_OBJ_TO_PTR(c->report_string);
            cl_error(CL_ERR_GENERAL, "%s", s->data);
        } else {
            char buf[128];
            cl_prin1_to_string(c->type_name, buf, sizeof(buf));
            cl_error(CL_ERR_GENERAL, "%s", buf);
        }
    }
    return CL_NIL;
}

/* --- Registration --- */

void cl_builtins_condition_init(void)
{
    /* Build condition type hierarchy */
    CL_GC_PROTECT(condition_hierarchy);
    build_hierarchy();
    CL_GC_UNPROTECT(1);

    /* Register builtins */
    defun("MAKE-CONDITION", bi_make_condition, 1, -1);
    defun("CONDITIONP", bi_conditionp, 1, 1);
    defun("CONDITION-TYPE-NAME", bi_condition_type_name, 1, 1);
    defun("SIMPLE-CONDITION-FORMAT-CONTROL", bi_simple_condition_format_control, 1, 1);
    defun("SIMPLE-CONDITION-FORMAT-ARGUMENTS", bi_simple_condition_format_arguments, 1, 1);
    defun("TYPE-ERROR-DATUM", bi_type_error_datum, 1, 1);
    defun("TYPE-ERROR-EXPECTED-TYPE", bi_type_error_expected_type, 1, 1);

    /* Signaling */
    defun("SIGNAL", bi_signal, 1, -1);
    defun("WARN", bi_warn, 1, -1);
    defun("ERROR", bi_error, 1, -1);
}
