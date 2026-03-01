#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "vm.h"
#include "../platform/platform.h"
#include <string.h>

/* Helper to register a builtin */
static void defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_cl);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    /* Also set value slot so (function +) and #'+ work */
    s->value = fn;
}

/* --- List operations --- */

static CL_Obj bi_cons(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return cl_cons(args[0], args[1]);
}

static CL_Obj bi_car(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return cl_car(args[0]);
}

static CL_Obj bi_cdr(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return cl_cdr(args[0]);
}

static CL_Obj bi_list(CL_Obj *args, int n)
{
    CL_Obj result = CL_NIL;
    int i;
    for (i = n - 1; i >= 0; i--)
        result = cl_cons(args[i], result);
    return result;
}

static CL_Obj bi_length(CL_Obj *args, int n)
{
    CL_Obj obj = args[0];
    int len = 0;
    CL_UNUSED(n);

    if (CL_STRING_P(obj)) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(obj);
        return CL_MAKE_FIXNUM(s->length);
    }

    if (CL_VECTOR_P(obj)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
        return CL_MAKE_FIXNUM(v->length);
    }

    while (!CL_NULL_P(obj)) {
        len++;
        obj = cl_cdr(obj);
    }
    return CL_MAKE_FIXNUM(len);
}

static CL_Obj bi_append(CL_Obj *args, int n)
{
    CL_Obj result = CL_NIL;
    CL_Obj tail = CL_NIL;
    int i;

    if (n == 0) return CL_NIL;

    for (i = 0; i < n - 1; i++) {
        CL_Obj list = args[i];
        while (!CL_NULL_P(list)) {
            CL_Obj cell = cl_cons(cl_car(list), CL_NIL);
            if (CL_NULL_P(result)) {
                result = cell;
            } else {
                ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
            }
            tail = cell;
            list = cl_cdr(list);
        }
    }

    /* Last arg shared (not copied) */
    if (CL_NULL_P(result)) {
        return args[n - 1];
    }
    ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = args[n - 1];
    return result;
}

static CL_Obj bi_reverse(CL_Obj *args, int n)
{
    CL_Obj list = args[0];
    CL_Obj result = CL_NIL;
    CL_UNUSED(n);

    while (!CL_NULL_P(list)) {
        result = cl_cons(cl_car(list), result);
        list = cl_cdr(list);
    }
    return result;
}

static CL_Obj bi_nth(CL_Obj *args, int n)
{
    int idx;
    CL_Obj list;
    CL_UNUSED(n);

    if (!CL_FIXNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "NTH: index must be a number");
    idx = CL_FIXNUM_VAL(args[0]);
    list = args[1];
    while (idx > 0 && !CL_NULL_P(list)) {
        list = cl_cdr(list);
        idx--;
    }
    return cl_car(list);
}

/* --- Predicates --- */

static CL_Obj bi_null(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_NULL_P(args[0]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_consp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_CONS_P(args[0]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_atom(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_CONS_P(args[0]) ? CL_NIL : SYM_T;
}

static CL_Obj bi_listp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return (CL_NULL_P(args[0]) || CL_CONS_P(args[0])) ? SYM_T : CL_NIL;
}

static CL_Obj bi_numberp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_FIXNUM_P(args[0]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_symbolp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return (CL_NULL_P(args[0]) || CL_SYMBOL_P(args[0])) ? SYM_T : CL_NIL;
}

static CL_Obj bi_stringp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_STRING_P(args[0]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_functionp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return (CL_FUNCTION_P(args[0]) || CL_CLOSURE_P(args[0]) ||
            CL_BYTECODE_P(args[0])) ? SYM_T : CL_NIL;
}

static CL_Obj bi_eq(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return (args[0] == args[1]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_eql(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    /* For fixnums and characters, value equality; otherwise identity */
    if (CL_FIXNUM_P(args[0]) && CL_FIXNUM_P(args[1]))
        return (args[0] == args[1]) ? SYM_T : CL_NIL;
    if (CL_CHAR_P(args[0]) && CL_CHAR_P(args[1]))
        return (args[0] == args[1]) ? SYM_T : CL_NIL;
    return (args[0] == args[1]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_equal(CL_Obj *args, int n)
{
    CL_Obj a = args[0], b = args[1];
    CL_UNUSED(n);

    if (a == b) return SYM_T;
    if (CL_CONS_P(a) && CL_CONS_P(b)) {
        CL_Obj aa[2], bb[2];
        aa[0] = cl_car(a); aa[1] = cl_car(b);
        if (CL_NULL_P(bi_equal(aa, 2))) return CL_NIL;
        bb[0] = cl_cdr(a); bb[1] = cl_cdr(b);
        return bi_equal(bb, 2);
    }
    if (CL_STRING_P(a) && CL_STRING_P(b)) {
        CL_String *sa = (CL_String *)CL_OBJ_TO_PTR(a);
        CL_String *sb = (CL_String *)CL_OBJ_TO_PTR(b);
        if (sa->length == sb->length &&
            memcmp(sa->data, sb->data, sa->length) == 0)
            return SYM_T;
    }
    if (CL_VECTOR_P(a) && CL_VECTOR_P(b)) {
        CL_Vector *va = (CL_Vector *)CL_OBJ_TO_PTR(a);
        CL_Vector *vb = (CL_Vector *)CL_OBJ_TO_PTR(b);
        uint32_t i;
        if (va->length != vb->length) return CL_NIL;
        for (i = 0; i < va->length; i++) {
            CL_Obj pair[2];
            pair[0] = va->data[i]; pair[1] = vb->data[i];
            if (CL_NULL_P(bi_equal(pair, 2))) return CL_NIL;
        }
        return SYM_T;
    }
    return CL_NIL;
}

static CL_Obj bi_not(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_NULL_P(args[0]) ? SYM_T : CL_NIL;
}

/* --- Higher-order --- */

static CL_Obj bi_mapcar(CL_Obj *args, int n)
{
    CL_Obj func = args[0];
    CL_Obj result = CL_NIL, tail = CL_NIL;
    int nlists = n - 1;
    CL_Obj lists[16]; /* Max 16 list arguments */
    CL_Obj call_args[16];
    int i;

    if (nlists > 16) nlists = 16;
    for (i = 0; i < nlists; i++)
        lists[i] = args[i + 1];

    CL_GC_PROTECT(func);
    CL_GC_PROTECT(result);
    CL_GC_PROTECT(tail);

    for (;;) {
        CL_Obj val;

        /* Check if any list is exhausted */
        for (i = 0; i < nlists; i++) {
            if (CL_NULL_P(lists[i])) goto done;
        }

        /* Collect car of each list */
        for (i = 0; i < nlists; i++) {
            call_args[i] = cl_car(lists[i]);
            lists[i] = cl_cdr(lists[i]);
        }

        /* Call function */
        if (CL_FUNCTION_P(func)) {
            CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(func);
            val = f->func(call_args, nlists);
        } else if (CL_BYTECODE_P(func) || CL_CLOSURE_P(func)) {
            val = cl_vm_apply(func, call_args, nlists);
        } else {
            cl_error(CL_ERR_TYPE, "MAPCAR: not a function");
            val = CL_NIL;
        }

        {
            CL_Obj cell = cl_cons(val, CL_NIL);
            if (CL_NULL_P(result)) {
                result = cell;
            } else {
                ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
            }
            tail = cell;
        }
    }

done:
    CL_GC_UNPROTECT(3);
    return result;
}

static CL_Obj bi_apply(CL_Obj *args, int n)
{
    CL_Obj func = args[0];
    CL_Obj arglist;
    CL_Obj flat_args[64];
    int nflat = 0;

    /* (apply func arg1 arg2 ... arglist) */
    if (n == 2) {
        arglist = args[1];
    } else {
        int i;
        /* Spread initial args, last arg is the list */
        for (i = 1; i < n - 1; i++) {
            if (nflat < 64) flat_args[nflat++] = args[i];
        }
        arglist = args[n - 1];
    }

    /* Flatten remaining arglist */
    while (!CL_NULL_P(arglist)) {
        if (nflat < 64) flat_args[nflat++] = cl_car(arglist);
        arglist = cl_cdr(arglist);
    }

    if (CL_FUNCTION_P(func)) {
        CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(func);
        return f->func(flat_args, nflat);
    }
    if (CL_BYTECODE_P(func) || CL_CLOSURE_P(func)) {
        return cl_vm_apply(func, flat_args, nflat);
    }

    cl_error(CL_ERR_TYPE, "APPLY: not a function");
    return CL_NIL;
}

static CL_Obj bi_funcall(CL_Obj *args, int n)
{
    CL_Obj func = args[0];
    /* CL spec: funcall accepts a symbol, resolving to its function binding */
    if (CL_SYMBOL_P(func)) {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(func);
        func = s->function;
        if (CL_NULL_P(func) || func == CL_UNBOUND)
            cl_error(CL_ERR_TYPE, "FUNCALL: symbol has no function binding");
    }
    if (CL_FUNCTION_P(func)) {
        CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(func);
        return f->func(args + 1, n - 1);
    }
    if (CL_BYTECODE_P(func) || CL_CLOSURE_P(func)) {
        return cl_vm_apply(func, args + 1, n - 1);
    }
    cl_error(CL_ERR_TYPE, "FUNCALL: not a function");
    return CL_NIL;
}

/* --- Misc --- */

static CL_Obj bi_type_of(CL_Obj *args, int n)
{
    const char *name;
    CL_UNUSED(n);
    name = cl_type_name(args[0]);
    return cl_intern(name, (uint32_t)strlen(name));
}

static CL_Obj bi_typep(CL_Obj *args, int n)
{
    CL_Obj obj = args[0];
    CL_Obj type_spec = args[1];
    const char *tname;
    CL_UNUSED(n);

    if (!CL_SYMBOL_P(type_spec) && !CL_NULL_P(type_spec))
        cl_error(CL_ERR_TYPE, "TYPEP: type specifier must be a symbol");

    tname = cl_symbol_name(type_spec);

    if (strcmp(tname, "T") == 0)              return SYM_T;
    if (strcmp(tname, "NIL") == 0)            return CL_NIL;
    if (strcmp(tname, "NULL") == 0)           return CL_NULL_P(obj) ? SYM_T : CL_NIL;
    if (strcmp(tname, "SYMBOL") == 0)         return (CL_NULL_P(obj) || CL_SYMBOL_P(obj)) ? SYM_T : CL_NIL;
    if (strcmp(tname, "KEYWORD") == 0) {
        if (!CL_NULL_P(obj) && CL_SYMBOL_P(obj)) {
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(obj);
            return (s->package == cl_package_keyword) ? SYM_T : CL_NIL;
        }
        return CL_NIL;
    }
    if (strcmp(tname, "CONS") == 0)           return CL_CONS_P(obj) ? SYM_T : CL_NIL;
    if (strcmp(tname, "LIST") == 0)           return (CL_NULL_P(obj) || CL_CONS_P(obj)) ? SYM_T : CL_NIL;
    if (strcmp(tname, "ATOM") == 0)           return (!CL_CONS_P(obj)) ? SYM_T : CL_NIL;
    if (strcmp(tname, "INTEGER") == 0 || strcmp(tname, "FIXNUM") == 0 || strcmp(tname, "NUMBER") == 0)
        return CL_FIXNUM_P(obj) ? SYM_T : CL_NIL;
    if (strcmp(tname, "CHARACTER") == 0)      return CL_CHAR_P(obj) ? SYM_T : CL_NIL;
    if (strcmp(tname, "STRING") == 0)         return CL_STRING_P(obj) ? SYM_T : CL_NIL;
    if (strcmp(tname, "VECTOR") == 0 || strcmp(tname, "SIMPLE-VECTOR") == 0 || strcmp(tname, "ARRAY") == 0 || strcmp(tname, "SIMPLE-ARRAY") == 0)
        return (CL_STRING_P(obj) || CL_VECTOR_P(obj)) ? SYM_T : CL_NIL;
    if (strcmp(tname, "SEQUENCE") == 0)
        return (CL_NULL_P(obj) || CL_CONS_P(obj) || CL_STRING_P(obj) || CL_VECTOR_P(obj)) ? SYM_T : CL_NIL;
    if (strcmp(tname, "FUNCTION") == 0)
        return (CL_FUNCTION_P(obj) || CL_CLOSURE_P(obj) || CL_BYTECODE_P(obj)) ? SYM_T : CL_NIL;
    if (strcmp(tname, "COMPILED-FUNCTION") == 0)
        return (CL_CLOSURE_P(obj) || CL_BYTECODE_P(obj)) ? SYM_T : CL_NIL;
    if (strcmp(tname, "HASH-TABLE") == 0)     return CL_HASHTABLE_P(obj) ? SYM_T : CL_NIL;
    if (strcmp(tname, "PACKAGE") == 0)        return CL_PACKAGE_P(obj) ? SYM_T : CL_NIL;

    cl_error(CL_ERR_TYPE, "TYPEP: unknown type specifier");
    return CL_NIL;
}

static CL_Obj bi_coerce(CL_Obj *args, int n)
{
    CL_Obj obj = args[0];
    CL_Obj result_type = args[1];
    const char *tname;
    CL_UNUSED(n);

    if (!CL_SYMBOL_P(result_type) && !CL_NULL_P(result_type))
        cl_error(CL_ERR_TYPE, "COERCE: result type must be a symbol");

    tname = cl_symbol_name(result_type);

    /* (coerce x 't) — identity */
    if (strcmp(tname, "T") == 0)
        return obj;

    /* (coerce x 'character) */
    if (strcmp(tname, "CHARACTER") == 0) {
        if (CL_CHAR_P(obj)) return obj;
        if (CL_FIXNUM_P(obj)) return CL_MAKE_CHAR((uint32_t)CL_FIXNUM_VAL(obj));
        cl_error(CL_ERR_TYPE, "COERCE: cannot coerce to CHARACTER");
        return CL_NIL;
    }

    /* (coerce x 'integer) / 'fixnum / 'number */
    if (strcmp(tname, "INTEGER") == 0 || strcmp(tname, "FIXNUM") == 0 || strcmp(tname, "NUMBER") == 0) {
        if (CL_FIXNUM_P(obj)) return obj;
        if (CL_CHAR_P(obj)) return CL_MAKE_FIXNUM(CL_CHAR_VAL(obj));
        cl_error(CL_ERR_TYPE, "COERCE: cannot coerce to INTEGER");
        return CL_NIL;
    }

    /* (coerce x 'string) */
    if (strcmp(tname, "STRING") == 0) {
        if (CL_STRING_P(obj)) return obj;
        if (CL_NULL_P(obj)) return cl_make_string("NIL", 3);
        if (CL_SYMBOL_P(obj)) {
            const char *sname = cl_symbol_name(obj);
            return cl_make_string(sname, (uint32_t)strlen(sname));
        }
        if (CL_CHAR_P(obj)) {
            char c = (char)CL_CHAR_VAL(obj);
            return cl_make_string(&c, 1);
        }
        cl_error(CL_ERR_TYPE, "COERCE: cannot coerce to STRING");
        return CL_NIL;
    }

    /* (coerce x 'list) */
    if (strcmp(tname, "LIST") == 0) {
        if (CL_NULL_P(obj) || CL_CONS_P(obj)) return obj;
        if (CL_VECTOR_P(obj)) {
            CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
            CL_Obj result = CL_NIL;
            uint32_t i = v->length;
            while (i > 0) {
                i--;
                result = cl_cons(v->data[i], result);
            }
            return result;
        }
        if (CL_STRING_P(obj)) {
            CL_String *s = (CL_String *)CL_OBJ_TO_PTR(obj);
            CL_Obj result = CL_NIL;
            uint32_t i = s->length;
            while (i > 0) {
                i--;
                result = cl_cons(CL_MAKE_CHAR((unsigned char)s->data[i]), result);
            }
            return result;
        }
        cl_error(CL_ERR_TYPE, "COERCE: cannot coerce to LIST");
        return CL_NIL;
    }

    /* (coerce x 'vector) */
    if (strcmp(tname, "VECTOR") == 0) {
        if (CL_VECTOR_P(obj)) return obj;
        if (CL_NULL_P(obj) || CL_CONS_P(obj)) {
            /* Count list length */
            CL_Obj p = obj;
            uint32_t len = 0;
            uint32_t i;
            CL_Obj vec;
            CL_Vector *v;
            while (!CL_NULL_P(p)) {
                len++;
                p = cl_cdr(p);
            }
            vec = cl_make_vector(len);
            v = (CL_Vector *)CL_OBJ_TO_PTR(vec);
            p = obj;
            for (i = 0; i < len; i++) {
                v->data[i] = cl_car(p);
                p = cl_cdr(p);
            }
            return vec;
        }
        cl_error(CL_ERR_TYPE, "COERCE: cannot coerce to VECTOR");
        return CL_NIL;
    }

    cl_error(CL_ERR_TYPE, "COERCE: unknown result type");
    return CL_NIL;
}

/* --- Registration --- */

/* Sub-module init functions */
void cl_builtins_arith_init(void);
void cl_builtins_io_init(void);
void cl_builtins_mutation_init(void);
void cl_builtins_strings_init(void);
void cl_builtins_lists_init(void);
void cl_builtins_hashtable_init(void);
void cl_builtins_sequence_init(void);
void cl_builtins_sequence2_init(void);

void cl_builtins_init(void)
{
    /* List ops */
    defun("CONS", bi_cons, 2, 2);
    defun("CAR", bi_car, 1, 1);
    defun("CDR", bi_cdr, 1, 1);
    defun("FIRST", bi_car, 1, 1);
    defun("REST", bi_cdr, 1, 1);
    defun("LIST", bi_list, 0, -1);
    defun("LENGTH", bi_length, 1, 1);
    defun("APPEND", bi_append, 0, -1);
    defun("REVERSE", bi_reverse, 1, 1);
    defun("NTH", bi_nth, 2, 2);

    /* Predicates */
    defun("NULL", bi_null, 1, 1);
    defun("CONSP", bi_consp, 1, 1);
    defun("ATOM", bi_atom, 1, 1);
    defun("LISTP", bi_listp, 1, 1);
    defun("NUMBERP", bi_numberp, 1, 1);
    defun("INTEGERP", bi_numberp, 1, 1);
    defun("SYMBOLP", bi_symbolp, 1, 1);
    defun("STRINGP", bi_stringp, 1, 1);
    defun("FUNCTIONP", bi_functionp, 1, 1);
    defun("EQ", bi_eq, 2, 2);
    defun("EQL", bi_eql, 2, 2);
    defun("EQUAL", bi_equal, 2, 2);
    defun("NOT", bi_not, 1, 1);

    /* Higher-order */
    defun("MAPCAR", bi_mapcar, 2, -1);
    defun("APPLY", bi_apply, 2, -1);
    defun("FUNCALL", bi_funcall, 1, -1);

    /* Misc */
    defun("TYPE-OF", bi_type_of, 1, 1);
    defun("TYPEP", bi_typep, 2, 2);
    defun("COERCE", bi_coerce, 2, 2);

    /* Sub-module builtins */
    cl_builtins_arith_init();
    cl_builtins_io_init();
    cl_builtins_mutation_init();
    cl_builtins_strings_init();
    cl_builtins_lists_init();
    cl_builtins_hashtable_init();
    cl_builtins_sequence_init();
    cl_builtins_sequence2_init();
}
