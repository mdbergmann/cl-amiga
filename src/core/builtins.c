#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "printer.h"
#include "vm.h"
#include "compiler.h"
#include "reader.h"
#include "../platform/platform.h"
#include <stdio.h>
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
    return CL_NIL;
}

static CL_Obj bi_not(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_NULL_P(args[0]) ? SYM_T : CL_NIL;
}

/* --- I/O --- */

static CL_Obj bi_print(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    cl_print(args[0]);
    return args[0];
}

static CL_Obj bi_prin1(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    cl_prin1(args[0]);
    return args[0];
}

static CL_Obj bi_princ(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    cl_princ(args[0]);
    return args[0];
}

static CL_Obj bi_terpri(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    platform_write_string("\n");
    return CL_NIL;
}

static CL_Obj bi_format(CL_Obj *args, int n)
{
    /* Minimal: (format t "string") just prints */
    CL_UNUSED(n);
    if (n >= 2 && CL_STRING_P(args[1])) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(args[1]);
        /* Simple ~A and ~% substitution */
        const char *p = s->data;
        int ai = 2; /* Next argument index */
        while (*p) {
            if (*p == '~' && *(p+1)) {
                p++;
                if (*p == 'A' || *p == 'a') {
                    if (ai < n) cl_princ(args[ai++]);
                } else if (*p == 'S' || *p == 's') {
                    if (ai < n) cl_prin1(args[ai++]);
                } else if (*p == '%') {
                    platform_write_string("\n");
                } else if (*p == '~') {
                    platform_write_string("~");
                }
                p++;
            } else {
                char c[2] = { *p, '\0' };
                platform_write_string(c);
                p++;
            }
        }
    }
    return CL_NIL;
}

/* --- Higher-order --- */

static CL_Obj bi_mapcar(CL_Obj *args, int n)
{
    CL_Obj func = args[0];
    CL_Obj list = args[1];
    CL_Obj result = CL_NIL, tail = CL_NIL;
    CL_UNUSED(n);

    CL_GC_PROTECT(func);
    CL_GC_PROTECT(list);
    CL_GC_PROTECT(result);
    CL_GC_PROTECT(tail);

    while (!CL_NULL_P(list)) {
        CL_Obj elem = cl_car(list);
        CL_Obj val;

        /* Call function with one argument */
        if (CL_FUNCTION_P(func)) {
            CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(func);
            val = f->func(&elem, 1);
        } else if (CL_BYTECODE_P(func) || CL_CLOSURE_P(func)) {
            val = cl_vm_apply(func, &elem, 1);
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

        list = cl_cdr(list);
    }

    CL_GC_UNPROTECT(4);
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

/* --- Load --- */

static CL_Obj bi_load(CL_Obj *args, int n)
{
    CL_String *path_str;
    char *buf;
    unsigned long size;
    CL_ReadStream stream;
    CL_Obj expr, bytecode;

    CL_UNUSED(n);
    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "LOAD: argument must be a string");

    path_str = (CL_String *)CL_OBJ_TO_PTR(args[0]);
    buf = platform_file_read(path_str->data, &size);
    if (!buf)
        cl_error(CL_ERR_GENERAL, "LOAD: cannot open file");

    stream.buf = buf;
    stream.pos = 0;
    stream.len = (int)size;

    for (;;) {
        int err;

        expr = cl_read_from_string(&stream);
        if (cl_reader_eof()) break;

        err = CL_CATCH();
        if (err == CL_ERR_NONE) {
            CL_GC_PROTECT(expr);
            bytecode = cl_compile(expr);
            CL_GC_UNPROTECT(1);
            if (!CL_NULL_P(bytecode))
                cl_vm_eval(bytecode);
            CL_UNCATCH();
        } else {
            cl_error_print();
            CL_UNCATCH();
        }
    }

    platform_free(buf);
    return SYM_T;
}

/* --- Error --- */

static CL_Obj bi_error(CL_Obj *args, int n)
{
    if (n > 0 && CL_STRING_P(args[0])) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
        cl_error(CL_ERR_GENERAL, "%s", s->data);
    } else {
        cl_error(CL_ERR_GENERAL, "Error signaled");
    }
    return CL_NIL;
}

/* --- Gensym --- */

static uint32_t gensym_counter = 0;

static CL_Obj bi_gensym(CL_Obj *args, int n)
{
    char buf[64];
    const char *prefix = "G";
    CL_Obj name_str, sym;
    int len;

    if (n > 0 && CL_STRING_P(args[0])) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
        prefix = s->data;
    }

    /* Manual int-to-string for vbcc compatibility */
    len = snprintf(buf, sizeof(buf), "%s%lu", prefix, (unsigned long)gensym_counter++);
    name_str = cl_make_string(buf, (uint32_t)len);
    CL_GC_PROTECT(name_str);
    sym = cl_make_symbol(name_str);  /* Uninterned — not in any package */
    CL_GC_UNPROTECT(1);
    return sym;
}

/* --- Registration --- */

void cl_builtins_init(void)
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
    defun("ZEROP", bi_zerop, 1, 1);
    defun("PLUSP", bi_plusp, 1, 1);
    defun("MINUSP", bi_minusp, 1, 1);
    defun("ABS", bi_abs, 1, 1);
    defun("MAX", bi_max, 1, -1);
    defun("MIN", bi_min, 1, -1);

    /* I/O */
    defun("PRINT", bi_print, 1, 1);
    defun("PRIN1", bi_prin1, 1, 1);
    defun("PRINC", bi_princ, 1, 1);
    defun("TERPRI", bi_terpri, 0, 0);
    defun("FORMAT", bi_format, 1, -1);

    /* Higher-order */
    defun("MAPCAR", bi_mapcar, 2, 2);
    defun("APPLY", bi_apply, 2, -1);
    defun("FUNCALL", bi_funcall, 1, -1);

    /* Misc */
    defun("TYPE-OF", bi_type_of, 1, 1);
    defun("GENSYM", bi_gensym, 0, 1);
    defun("LOAD", bi_load, 1, 1);
    defun("ERROR", bi_error, 1, -1);
}
