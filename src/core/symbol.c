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
