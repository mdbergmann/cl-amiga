#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "../platform/platform.h"
#include <string.h>

/* Well-known symbols */
CL_Obj SYM_T = CL_NIL;
CL_Obj SYM_NIL = CL_NIL;
CL_Obj SYM_QUOTE = CL_NIL;
CL_Obj SYM_IF = CL_NIL;
CL_Obj SYM_PROGN = CL_NIL;
CL_Obj SYM_LAMBDA = CL_NIL;
CL_Obj SYM_LET = CL_NIL;
CL_Obj SYM_LETSTAR = CL_NIL;
CL_Obj SYM_SETQ = CL_NIL;
CL_Obj SYM_DEFUN = CL_NIL;
CL_Obj SYM_DEFMACRO = CL_NIL;
CL_Obj SYM_FUNCTION = CL_NIL;
CL_Obj SYM_BLOCK = CL_NIL;
CL_Obj SYM_RETURN_FROM = CL_NIL;
CL_Obj SYM_QUASIQUOTE = CL_NIL;
CL_Obj SYM_UNQUOTE = CL_NIL;
CL_Obj SYM_UNQUOTE_SPLICING = CL_NIL;
CL_Obj SYM_AMP_REST = CL_NIL;
CL_Obj SYM_AMP_OPTIONAL = CL_NIL;
CL_Obj SYM_AMP_BODY = CL_NIL;
CL_Obj SYM_AND = CL_NIL;
CL_Obj SYM_OR = CL_NIL;
CL_Obj SYM_COND = CL_NIL;
CL_Obj SYM_DO = CL_NIL;
CL_Obj SYM_DOLIST = CL_NIL;
CL_Obj SYM_DOTIMES = CL_NIL;
CL_Obj SYM_RETURN = CL_NIL;
CL_Obj SYM_CASE = CL_NIL;
CL_Obj SYM_ECASE = CL_NIL;
CL_Obj SYM_TYPECASE = CL_NIL;
CL_Obj SYM_ETYPECASE = CL_NIL;
CL_Obj SYM_OTHERWISE = CL_NIL;
CL_Obj SYM_FLET = CL_NIL;
CL_Obj SYM_LABELS = CL_NIL;
CL_Obj SYM_AMP_KEY = CL_NIL;
CL_Obj SYM_AMP_ALLOW_OTHER_KEYS = CL_NIL;
CL_Obj KW_ALLOW_OTHER_KEYS = CL_NIL;
CL_Obj SYM_TAGBODY = CL_NIL;
CL_Obj SYM_GO = CL_NIL;
CL_Obj SYM_CATCH = CL_NIL;
CL_Obj SYM_THROW = CL_NIL;
CL_Obj SYM_UNWIND_PROTECT = CL_NIL;
CL_Obj SYM_MULTIPLE_VALUE_BIND = CL_NIL;
CL_Obj SYM_MULTIPLE_VALUE_LIST = CL_NIL;
CL_Obj SYM_MULTIPLE_VALUE_PROG1 = CL_NIL;
CL_Obj SYM_NTH_VALUE = CL_NIL;
CL_Obj SYM_DEFVAR = CL_NIL;
CL_Obj SYM_DEFPARAMETER = CL_NIL;
CL_Obj SYM_SETF = CL_NIL;
CL_Obj SYM_EVAL_WHEN = CL_NIL;
CL_Obj SYM_DESTRUCTURING_BIND = CL_NIL;
CL_Obj SYM_DEFSETF = CL_NIL;
CL_Obj SYM_DEFTYPE = CL_NIL;
CL_Obj SYM_DECLARE = CL_NIL;
CL_Obj SYM_DECLAIM = CL_NIL;
CL_Obj SYM_LOCALLY = CL_NIL;
CL_Obj SYM_TRACE = CL_NIL;
CL_Obj SYM_UNTRACE = CL_NIL;
CL_Obj SYM_TIME = CL_NIL;

/* Declaration specifier symbols */
CL_Obj SYM_SPECIAL_DECL = CL_NIL;
CL_Obj SYM_TYPE_DECL = CL_NIL;
CL_Obj SYM_FTYPE_DECL = CL_NIL;
CL_Obj SYM_INLINE_DECL = CL_NIL;
CL_Obj SYM_NOTINLINE_DECL = CL_NIL;
CL_Obj SYM_OPTIMIZE_DECL = CL_NIL;
CL_Obj SYM_IGNORE_DECL = CL_NIL;
CL_Obj SYM_IGNORABLE_DECL = CL_NIL;
CL_Obj SYM_DYNAMIC_EXTENT_DECL = CL_NIL;

/* Optimize quality symbols */
CL_Obj SYM_SPEED = CL_NIL;
CL_Obj SYM_SAFETY = CL_NIL;
CL_Obj SYM_DEBUG = CL_NIL;
CL_Obj SYM_SPACE = CL_NIL;

/* Condition type name symbols */
CL_Obj SYM_CONDITION = CL_NIL;
CL_Obj SYM_WARNING = CL_NIL;
CL_Obj SYM_SERIOUS_CONDITION = CL_NIL;
CL_Obj SYM_ERROR_COND = CL_NIL;
CL_Obj SYM_SIMPLE_CONDITION = CL_NIL;
CL_Obj SYM_SIMPLE_ERROR = CL_NIL;
CL_Obj SYM_SIMPLE_WARNING = CL_NIL;
CL_Obj SYM_TYPE_ERROR = CL_NIL;
CL_Obj SYM_UNBOUND_VARIABLE_COND = CL_NIL;
CL_Obj SYM_UNDEFINED_FUNCTION_COND = CL_NIL;
CL_Obj SYM_PROGRAM_ERROR = CL_NIL;
CL_Obj SYM_CONTROL_ERROR = CL_NIL;
CL_Obj SYM_ARITHMETIC_ERROR = CL_NIL;
CL_Obj SYM_DIVISION_BY_ZERO = CL_NIL;

/* Signaling symbols */
CL_Obj SYM_SIGNAL = CL_NIL;
CL_Obj SYM_WARN = CL_NIL;

/* Condition keyword symbols */
CL_Obj KW_FORMAT_CONTROL = CL_NIL;
CL_Obj KW_FORMAT_ARGUMENTS = CL_NIL;
CL_Obj KW_DATUM = CL_NIL;
CL_Obj KW_EXPECTED_TYPE = CL_NIL;

/* FNV-1a hash */
uint32_t cl_hash_string(const char *str, uint32_t len)
{
    uint32_t hash = 2166136261u;
    uint32_t i;
    for (i = 0; i < len; i++) {
        hash ^= (uint8_t)str[i];
        hash *= 16777619u;
    }
    return hash;
}

CL_Obj cl_intern_in(const char *name, uint32_t len, CL_Obj package)
{
    CL_Obj existing;
    CL_Obj name_str, sym;
    CL_Symbol *s;

    /* Check if already interned */
    existing = cl_package_find_symbol(name, len, package);
    if (!CL_NULL_P(existing))
        return existing;

    /* Create new symbol */
    name_str = cl_make_string(name, len);
    CL_GC_PROTECT(name_str);

    sym = cl_make_symbol(name_str);
    CL_GC_UNPROTECT(1);

    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->hash = cl_hash_string(name, len);

    cl_package_add_symbol(package, sym);
    return sym;
}

CL_Obj cl_intern(const char *name, uint32_t len)
{
    return cl_intern_in(name, len, cl_current_package);
}

CL_Obj cl_find_symbol(const char *name, uint32_t len, CL_Obj package)
{
    return cl_package_find_symbol(name, len, package);
}

CL_Obj cl_intern_keyword(const char *name, uint32_t len)
{
    CL_Obj sym = cl_intern_in(name, len, cl_package_keyword);
    /* Keywords are self-evaluating: their value is themselves */
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->value = sym;
    return sym;
}

int cl_symbol_specialp(CL_Obj sym)
{
    CL_Symbol *s;
    if (CL_NULL_P(sym)) return 0;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    return (s->flags & CL_SYM_SPECIAL) != 0;
}

const char *cl_symbol_name(CL_Obj sym)
{
    CL_Symbol *s;
    CL_String *name;
    if (CL_NULL_P(sym)) return "NIL";
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    name = (CL_String *)CL_OBJ_TO_PTR(s->name);
    return name->data;
}

void cl_symbol_init(void)
{
    /* Intern well-known symbols in CL package */
    SYM_T             = cl_intern_in("T", 1, cl_package_cl);
    SYM_NIL           = cl_intern_in("NIL", 3, cl_package_cl);
    SYM_QUOTE         = cl_intern_in("QUOTE", 5, cl_package_cl);
    SYM_IF            = cl_intern_in("IF", 2, cl_package_cl);
    SYM_PROGN         = cl_intern_in("PROGN", 5, cl_package_cl);
    SYM_LAMBDA        = cl_intern_in("LAMBDA", 6, cl_package_cl);
    SYM_LET           = cl_intern_in("LET", 3, cl_package_cl);
    SYM_LETSTAR       = cl_intern_in("LET*", 4, cl_package_cl);
    SYM_SETQ          = cl_intern_in("SETQ", 4, cl_package_cl);
    SYM_DEFUN         = cl_intern_in("DEFUN", 5, cl_package_cl);
    SYM_DEFMACRO      = cl_intern_in("DEFMACRO", 8, cl_package_cl);
    SYM_FUNCTION      = cl_intern_in("FUNCTION", 8, cl_package_cl);
    SYM_BLOCK         = cl_intern_in("BLOCK", 5, cl_package_cl);
    SYM_RETURN_FROM   = cl_intern_in("RETURN-FROM", 11, cl_package_cl);
    SYM_QUASIQUOTE    = cl_intern_in("QUASIQUOTE", 10, cl_package_cl);
    SYM_UNQUOTE       = cl_intern_in("UNQUOTE", 7, cl_package_cl);
    SYM_UNQUOTE_SPLICING = cl_intern_in("UNQUOTE-SPLICING", 16, cl_package_cl);
    SYM_AMP_REST      = cl_intern_in("&REST", 5, cl_package_cl);
    SYM_AMP_OPTIONAL  = cl_intern_in("&OPTIONAL", 9, cl_package_cl);
    SYM_AMP_BODY      = cl_intern_in("&BODY", 5, cl_package_cl);
    SYM_AND           = cl_intern_in("AND", 3, cl_package_cl);
    SYM_OR            = cl_intern_in("OR", 2, cl_package_cl);
    SYM_COND          = cl_intern_in("COND", 4, cl_package_cl);
    SYM_DO            = cl_intern_in("DO", 2, cl_package_cl);
    SYM_DOLIST        = cl_intern_in("DOLIST", 6, cl_package_cl);
    SYM_DOTIMES       = cl_intern_in("DOTIMES", 7, cl_package_cl);
    SYM_RETURN        = cl_intern_in("RETURN", 6, cl_package_cl);
    SYM_CASE          = cl_intern_in("CASE", 4, cl_package_cl);
    SYM_ECASE         = cl_intern_in("ECASE", 5, cl_package_cl);
    SYM_TYPECASE      = cl_intern_in("TYPECASE", 8, cl_package_cl);
    SYM_ETYPECASE     = cl_intern_in("ETYPECASE", 9, cl_package_cl);
    SYM_OTHERWISE     = cl_intern_in("OTHERWISE", 9, cl_package_cl);
    SYM_FLET          = cl_intern_in("FLET", 4, cl_package_cl);
    SYM_LABELS        = cl_intern_in("LABELS", 6, cl_package_cl);
    SYM_AMP_KEY       = cl_intern_in("&KEY", 4, cl_package_cl);
    SYM_AMP_ALLOW_OTHER_KEYS = cl_intern_in("&ALLOW-OTHER-KEYS", 17, cl_package_cl);
    KW_ALLOW_OTHER_KEYS = cl_intern_keyword("ALLOW-OTHER-KEYS", 16);
    SYM_TAGBODY        = cl_intern_in("TAGBODY", 7, cl_package_cl);
    SYM_GO             = cl_intern_in("GO", 2, cl_package_cl);
    SYM_CATCH          = cl_intern_in("CATCH", 5, cl_package_cl);
    SYM_THROW          = cl_intern_in("THROW", 5, cl_package_cl);
    SYM_UNWIND_PROTECT = cl_intern_in("UNWIND-PROTECT", 14, cl_package_cl);
    SYM_MULTIPLE_VALUE_BIND  = cl_intern_in("MULTIPLE-VALUE-BIND", 19, cl_package_cl);
    SYM_MULTIPLE_VALUE_LIST  = cl_intern_in("MULTIPLE-VALUE-LIST", 19, cl_package_cl);
    SYM_MULTIPLE_VALUE_PROG1 = cl_intern_in("MULTIPLE-VALUE-PROG1", 20, cl_package_cl);
    SYM_NTH_VALUE            = cl_intern_in("NTH-VALUE", 9, cl_package_cl);
    SYM_DEFVAR               = cl_intern_in("DEFVAR", 6, cl_package_cl);
    SYM_DEFPARAMETER         = cl_intern_in("DEFPARAMETER", 12, cl_package_cl);
    SYM_SETF                 = cl_intern_in("SETF", 4, cl_package_cl);
    SYM_EVAL_WHEN            = cl_intern_in("EVAL-WHEN", 9, cl_package_cl);
    SYM_DESTRUCTURING_BIND   = cl_intern_in("DESTRUCTURING-BIND", 18, cl_package_cl);
    SYM_DEFSETF              = cl_intern_in("DEFSETF", 7, cl_package_cl);
    SYM_DEFTYPE              = cl_intern_in("DEFTYPE", 7, cl_package_cl);
    SYM_DECLARE              = cl_intern_in("DECLARE", 7, cl_package_cl);
    SYM_DECLAIM              = cl_intern_in("DECLAIM", 7, cl_package_cl);
    SYM_LOCALLY              = cl_intern_in("LOCALLY", 7, cl_package_cl);
    SYM_TRACE                = cl_intern_in("TRACE", 5, cl_package_cl);
    SYM_UNTRACE              = cl_intern_in("UNTRACE", 7, cl_package_cl);
    SYM_TIME                 = cl_intern_in("TIME", 4, cl_package_cl);

    /* Declaration specifier symbols */
    SYM_SPECIAL_DECL         = cl_intern_in("SPECIAL", 7, cl_package_cl);
    SYM_TYPE_DECL            = cl_intern_in("TYPE", 4, cl_package_cl);
    SYM_FTYPE_DECL           = cl_intern_in("FTYPE", 5, cl_package_cl);
    SYM_INLINE_DECL          = cl_intern_in("INLINE", 6, cl_package_cl);
    SYM_NOTINLINE_DECL       = cl_intern_in("NOTINLINE", 9, cl_package_cl);
    SYM_OPTIMIZE_DECL        = cl_intern_in("OPTIMIZE", 8, cl_package_cl);
    SYM_IGNORE_DECL          = cl_intern_in("IGNORE", 6, cl_package_cl);
    SYM_IGNORABLE_DECL       = cl_intern_in("IGNORABLE", 9, cl_package_cl);
    SYM_DYNAMIC_EXTENT_DECL  = cl_intern_in("DYNAMIC-EXTENT", 14, cl_package_cl);

    /* Optimize quality symbols */
    SYM_SPEED                = cl_intern_in("SPEED", 5, cl_package_cl);
    SYM_SAFETY               = cl_intern_in("SAFETY", 6, cl_package_cl);
    SYM_DEBUG                = cl_intern_in("DEBUG", 5, cl_package_cl);
    SYM_SPACE                = cl_intern_in("SPACE", 5, cl_package_cl);

    /* Condition type name symbols */
    SYM_CONDITION                = cl_intern_in("CONDITION", 9, cl_package_cl);
    SYM_WARNING                  = cl_intern_in("WARNING", 7, cl_package_cl);
    SYM_SERIOUS_CONDITION        = cl_intern_in("SERIOUS-CONDITION", 17, cl_package_cl);
    SYM_ERROR_COND               = cl_intern_in("ERROR", 5, cl_package_cl);
    SYM_SIMPLE_CONDITION         = cl_intern_in("SIMPLE-CONDITION", 16, cl_package_cl);
    SYM_SIMPLE_ERROR             = cl_intern_in("SIMPLE-ERROR", 12, cl_package_cl);
    SYM_SIMPLE_WARNING           = cl_intern_in("SIMPLE-WARNING", 14, cl_package_cl);
    SYM_TYPE_ERROR               = cl_intern_in("TYPE-ERROR", 10, cl_package_cl);
    SYM_UNBOUND_VARIABLE_COND    = cl_intern_in("UNBOUND-VARIABLE", 16, cl_package_cl);
    SYM_UNDEFINED_FUNCTION_COND  = cl_intern_in("UNDEFINED-FUNCTION", 18, cl_package_cl);
    SYM_PROGRAM_ERROR            = cl_intern_in("PROGRAM-ERROR", 13, cl_package_cl);
    SYM_CONTROL_ERROR            = cl_intern_in("CONTROL-ERROR", 13, cl_package_cl);
    SYM_ARITHMETIC_ERROR         = cl_intern_in("ARITHMETIC-ERROR", 16, cl_package_cl);
    SYM_DIVISION_BY_ZERO         = cl_intern_in("DIVISION-BY-ZERO", 16, cl_package_cl);

    /* Signaling symbols */
    SYM_SIGNAL                   = cl_intern_in("SIGNAL", 6, cl_package_cl);
    SYM_WARN                     = cl_intern_in("WARN", 4, cl_package_cl);

    /* Condition keyword symbols */
    KW_FORMAT_CONTROL            = cl_intern_keyword("FORMAT-CONTROL", 14);
    KW_FORMAT_ARGUMENTS          = cl_intern_keyword("FORMAT-ARGUMENTS", 16);
    KW_DATUM                     = cl_intern_keyword("DATUM", 5);
    KW_EXPECTED_TYPE             = cl_intern_keyword("EXPECTED-TYPE", 13);

    /* T is self-evaluating */
    {
        CL_Symbol *t = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_T);
        t->value = SYM_T;
    }

    /* NIL's value is NIL */
    {
        CL_Symbol *n = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_NIL);
        n->value = CL_NIL;
    }

    /* Set the global CL_T for use by type predicates */
    CL_T = SYM_T;
}
