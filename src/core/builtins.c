#include "builtins.h"
#include "bignum.h"
#include "float.h"
#include "ratio.h"
#include "stream.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "vm.h"
#include "compiler.h"
#include "string_utils.h"
#include "../platform/platform.h"
#include "../platform/platform_thread.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Shared: register a builtin in a specific package */
void cl_register_builtin(const char *name, CL_CFunc func,
                          int min, int max, CL_Obj package)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), package);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    s->value = fn;
}

/* Helper to register a builtin in CL */
static void defun(const char *name, CL_CFunc func, int min, int max)
{
    cl_register_builtin(name, func, min, max, cl_package_cl);
}

/* Coerce function designator: symbol -> its function binding.
 * A function designator is a function object or a symbol that names one.
 * A funcallable instance (STANDARD-GENERIC-FUNCTION struct) is unwrapped
 * to its discriminating-function slot here, so every higher-order
 * builtin (MAPCAR, REMOVE-IF-NOT, SORT, MAPHASH, ...) sees a callable
 * of one of the three flat types their dispatch already handles — no
 * per-callsite change needed to accept GFs as arguments. */
CL_Obj cl_coerce_funcdesig(CL_Obj obj, const char *context)
{
    if (cl_funcallable_instance_p(obj))
        obj = cl_unwrap_funcallable(obj);
    if (CL_FUNCTION_P(obj) || CL_BYTECODE_P(obj) || CL_CLOSURE_P(obj))
        return obj;
    if (CL_SYMBOL_P(obj)) {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(obj);
        CL_Obj fn = s->function;
        if (fn != CL_UNBOUND && !CL_NULL_P(fn)) {
            if (cl_funcallable_instance_p(fn))
                fn = cl_unwrap_funcallable(fn);
            return fn;
        }
        cl_error(CL_ERR_UNDEFINED, "Undefined function: %s", cl_symbol_name(obj));
    }
    cl_error(CL_ERR_TYPE, "%s: not a function", context);
    return CL_NIL;
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

static CL_Obj bi_second(CL_Obj *a, int n) { CL_UNUSED(n); return cl_car(cl_cdr(a[0])); }
static CL_Obj bi_third(CL_Obj *a, int n)  { CL_UNUSED(n); return cl_car(cl_cdr(cl_cdr(a[0]))); }
static CL_Obj bi_fourth(CL_Obj *a, int n) { CL_UNUSED(n); return cl_car(cl_cdr(cl_cdr(cl_cdr(a[0])))); }
static CL_Obj bi_fifth(CL_Obj *a, int n)  { CL_UNUSED(n); return cl_car(cl_cdr(cl_cdr(cl_cdr(cl_cdr(a[0]))))); }
static CL_Obj bi_sixth(CL_Obj *a, int n)  { CL_UNUSED(n); return cl_car(cl_cdr(cl_cdr(cl_cdr(cl_cdr(cl_cdr(a[0])))))); }
static CL_Obj bi_seventh(CL_Obj *a, int n){ CL_UNUSED(n); return cl_car(cl_cdr(cl_cdr(cl_cdr(cl_cdr(cl_cdr(cl_cdr(a[0]))))))); }
static CL_Obj bi_eighth(CL_Obj *a, int n) { CL_UNUSED(n); return cl_car(cl_cdr(cl_cdr(cl_cdr(cl_cdr(cl_cdr(cl_cdr(cl_cdr(a[0])))))))); }
static CL_Obj bi_ninth(CL_Obj *a, int n)  { CL_UNUSED(n); return cl_car(cl_cdr(cl_cdr(cl_cdr(cl_cdr(cl_cdr(cl_cdr(cl_cdr(cl_cdr(a[0]))))))))); }
static CL_Obj bi_tenth(CL_Obj *a, int n)  { CL_UNUSED(n); return cl_car(cl_cdr(cl_cdr(cl_cdr(cl_cdr(cl_cdr(cl_cdr(cl_cdr(cl_cdr(cl_cdr(a[0])))))))))); }

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

#ifdef CL_WIDE_STRINGS
    if (CL_WIDE_STRING_P(obj)) {
        CL_WideString *ws = (CL_WideString *)CL_OBJ_TO_PTR(obj);
        return CL_MAKE_FIXNUM(ws->length);
    }
#endif

    if (CL_VECTOR_P(obj)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
        return CL_MAKE_FIXNUM(cl_vector_active_length(v));
    }

    if (CL_BIT_VECTOR_P(obj)) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(obj);
        return CL_MAKE_FIXNUM(cl_bv_active_length(bv));
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
    CL_Obj list;
    int i;

    if (n == 0) return CL_NIL;

    CL_GC_PROTECT(result);
    CL_GC_PROTECT(tail);

    for (i = 0; i < n - 1; i++) {
        list = args[i];
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
        CL_GC_UNPROTECT(2);
        return args[n - 1];
    }
    ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = args[n - 1];
    CL_GC_UNPROTECT(2);
    return result;
}

static CL_Obj bi_reverse(CL_Obj *args, int n)
{
    CL_Obj seq = args[0];
    CL_UNUSED(n);

    if (CL_NULL_P(seq)) return CL_NIL;

    if (CL_CONS_P(seq)) {
        CL_Obj result = CL_NIL;
        while (!CL_NULL_P(seq)) {
            result = cl_cons(cl_car(seq), result);
            seq = cl_cdr(seq);
        }
        return result;
    }
    if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        uint32_t alen = cl_vector_active_length(v);
        CL_Obj result = cl_make_vector(alen);
        CL_Vector *rv = (CL_Vector *)CL_OBJ_TO_PTR(result);
        uint32_t i;
        /* Re-fetch after potential GC */
        v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        for (i = 0; i < alen; i++)
            cl_vector_data(rv)[i] = cl_vector_data(v)[alen - 1 - i];
        return result;
    }
    if (CL_ANY_STRING_P(seq)) {
        uint32_t slen = cl_string_length(seq);
        CL_Obj result;
        uint32_t i;
        result = cl_string_copy(seq);
        for (i = 0; i < slen; i++)
            cl_string_set_char_at(result, i, cl_string_char_at(seq, slen - 1 - i));
        return result;
    }
    if (CL_BIT_VECTOR_P(seq)) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(seq);
        uint32_t blen = bv->length;
        CL_Obj result = cl_make_bit_vector(blen);
        CL_BitVector *rv = (CL_BitVector *)CL_OBJ_TO_PTR(result);
        uint32_t i;
        bv = (CL_BitVector *)CL_OBJ_TO_PTR(seq);
        for (i = 0; i < blen; i++)
            cl_bv_set_bit(rv, i, cl_bv_get_bit(bv, blen - 1 - i));
        return result;
    }
    cl_error(CL_ERR_TYPE, "REVERSE: not a sequence");
    return CL_NIL;
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

static CL_Obj bi_symbolp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return (CL_NULL_P(args[0]) || CL_SYMBOL_P(args[0])) ? SYM_T : CL_NIL;
}

static CL_Obj bi_stringp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return (CL_ANY_STRING_P(args[0]) || CL_STRING_VECTOR_P(args[0])) ? SYM_T : CL_NIL;
}

static CL_Obj bi_simple_string_p(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    /* Simple strings are TYPE_STRING or TYPE_WIDE_STRING (no fill-pointer/displaced) */
    return CL_ANY_STRING_P(args[0]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_functionp(CL_Obj *args, int n)
{
    CL_Obj obj = args[0];
    CL_UNUSED(n);
    return (CL_FUNCTION_P(obj) || CL_CLOSURE_P(obj) ||
            CL_BYTECODE_P(obj) || cl_funcallable_instance_p(obj))
        ? SYM_T : CL_NIL;
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
    if (CL_BIGNUM_P(args[0]) && CL_BIGNUM_P(args[1]))
        return cl_bignum_equal(args[0], args[1]) ? SYM_T : CL_NIL;
    /* Floats: same type and same value */
    if (CL_SINGLE_FLOAT_P(args[0]) && CL_SINGLE_FLOAT_P(args[1]))
        return ((CL_SingleFloat *)CL_OBJ_TO_PTR(args[0]))->value ==
               ((CL_SingleFloat *)CL_OBJ_TO_PTR(args[1]))->value ? SYM_T : CL_NIL;
    if (CL_DOUBLE_FLOAT_P(args[0]) && CL_DOUBLE_FLOAT_P(args[1]))
        return ((CL_DoubleFloat *)CL_OBJ_TO_PTR(args[0]))->value ==
               ((CL_DoubleFloat *)CL_OBJ_TO_PTR(args[1]))->value ? SYM_T : CL_NIL;
    if (CL_RATIO_P(args[0]) && CL_RATIO_P(args[1]))
        return cl_ratio_equal(args[0], args[1]) ? SYM_T : CL_NIL;
    if (CL_COMPLEX_P(args[0]) && CL_COMPLEX_P(args[1])) {
        CL_Complex *ca = (CL_Complex *)CL_OBJ_TO_PTR(args[0]);
        CL_Complex *cb = (CL_Complex *)CL_OBJ_TO_PTR(args[1]);
        CL_Obj ra[2], ia[2];
        ra[0] = ca->realpart; ra[1] = cb->realpart;
        ia[0] = ca->imagpart; ia[1] = cb->imagpart;
        return (!CL_NULL_P(bi_eql(ra, 2)) && !CL_NULL_P(bi_eql(ia, 2))) ? SYM_T : CL_NIL;
    }
    return (args[0] == args[1]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_equal(CL_Obj *args, int n)
{
    CL_Obj a = args[0], b = args[1];
    CL_UNUSED(n);

    if (a == b) return SYM_T;
    if (CL_BIGNUM_P(a) && CL_BIGNUM_P(b))
        return cl_bignum_equal(a, b) ? SYM_T : CL_NIL;
    if (CL_RATIO_P(a) && CL_RATIO_P(b))
        return cl_ratio_equal(a, b) ? SYM_T : CL_NIL;
    if (CL_COMPLEX_P(a) && CL_COMPLEX_P(b)) {
        CL_Obj pair[2];
        pair[0] = a; pair[1] = b;
        return bi_eql(pair, 2);
    }
    /* Floats: equal is same as eql (same type, same value) */
    if (CL_SINGLE_FLOAT_P(a) && CL_SINGLE_FLOAT_P(b))
        return ((CL_SingleFloat *)CL_OBJ_TO_PTR(a))->value ==
               ((CL_SingleFloat *)CL_OBJ_TO_PTR(b))->value ? SYM_T : CL_NIL;
    if (CL_DOUBLE_FLOAT_P(a) && CL_DOUBLE_FLOAT_P(b))
        return ((CL_DoubleFloat *)CL_OBJ_TO_PTR(a))->value ==
               ((CL_DoubleFloat *)CL_OBJ_TO_PTR(b))->value ? SYM_T : CL_NIL;
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
#ifdef CL_WIDE_STRINGS
    /* Mixed or wide-wide string comparison */
    if (CL_ANY_STRING_P(a) && CL_ANY_STRING_P(b) &&
        !(CL_STRING_P(a) && CL_STRING_P(b))) {
        uint32_t la = cl_string_length(a), lb = cl_string_length(b);
        uint32_t i;
        if (la != lb) return CL_NIL;
        for (i = 0; i < la; i++) {
            if (cl_string_char_at(a, i) != cl_string_char_at(b, i))
                return CL_NIL;
        }
        return SYM_T;
    }
#endif
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
    if (CL_BIT_VECTOR_P(a) && CL_BIT_VECTOR_P(b)) {
        CL_BitVector *ba = (CL_BitVector *)CL_OBJ_TO_PTR(a);
        CL_BitVector *bb = (CL_BitVector *)CL_OBJ_TO_PTR(b);
        uint32_t nwords;
        if (ba->length != bb->length) return CL_NIL;
        nwords = CL_BV_WORDS(ba->length);
        if (nwords == 0) return SYM_T;
        return memcmp(ba->data, bb->data, nwords * sizeof(uint32_t)) == 0 ? SYM_T : CL_NIL;
    }
    if (CL_PATHNAME_P(a) && CL_PATHNAME_P(b)) {
        extern int cl_pathname_equal(CL_Obj a, CL_Obj b);
        return cl_pathname_equal(a, b) ? SYM_T : CL_NIL;
    }
    return CL_NIL;
}

/* EQUALP: case-insensitive, numeric =, recursive on arrays/structs */
static CL_Obj bi_equalp(CL_Obj *args, int n)
{
    CL_Obj a = args[0], b = args[1];
    CL_UNUSED(n);

    if (a == b) return SYM_T;
    /* Characters: case-insensitive */
    if (CL_CHAR_P(a) && CL_CHAR_P(b)) {
        int ca = CL_CHAR_VAL(a), cb = CL_CHAR_VAL(b);
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        return ca == cb ? SYM_T : CL_NIL;
    }
    /* Numbers: use numeric = (cross-type) */
    if (CL_NUMBER_P(a) && CL_NUMBER_P(b))
        return cl_arith_compare(a, b) == 0 ? SYM_T : CL_NIL;
    /* Conses: recursive */
    if (CL_CONS_P(a) && CL_CONS_P(b)) {
        CL_Obj pair[2];
        pair[0] = cl_car(a); pair[1] = cl_car(b);
        if (CL_NULL_P(bi_equalp(pair, 2))) return CL_NIL;
        pair[0] = cl_cdr(a); pair[1] = cl_cdr(b);
        return bi_equalp(pair, 2);
    }
    /* Strings: case-insensitive */
    if (CL_ANY_STRING_P(a) && CL_ANY_STRING_P(b)) {
        uint32_t la = cl_string_length(a), lb = cl_string_length(b);
        uint32_t i;
        if (la != lb) return CL_NIL;
        for (i = 0; i < la; i++) {
            int ca = cl_string_char_at(a, i), cb = cl_string_char_at(b, i);
            if (ca >= 'a' && ca <= 'z') ca -= 32;
            if (cb >= 'a' && cb <= 'z') cb -= 32;
            if (ca != cb) return CL_NIL;
        }
        return SYM_T;
    }
    /* Vectors/arrays: element-wise equalp */
    if (CL_VECTOR_P(a) && CL_VECTOR_P(b)) {
        CL_Vector *va = (CL_Vector *)CL_OBJ_TO_PTR(a);
        CL_Vector *vb = (CL_Vector *)CL_OBJ_TO_PTR(b);
        uint32_t i;
        if (va->length != vb->length) return CL_NIL;
        for (i = 0; i < va->length; i++) {
            CL_Obj pair[2];
            pair[0] = va->data[i]; pair[1] = vb->data[i];
            if (CL_NULL_P(bi_equalp(pair, 2))) return CL_NIL;
        }
        return SYM_T;
    }
    /* Bit vectors */
    if (CL_BIT_VECTOR_P(a) && CL_BIT_VECTOR_P(b)) {
        CL_BitVector *ba = (CL_BitVector *)CL_OBJ_TO_PTR(a);
        CL_BitVector *bb_bv = (CL_BitVector *)CL_OBJ_TO_PTR(b);
        uint32_t nwords;
        if (ba->length != bb_bv->length) return CL_NIL;
        nwords = CL_BV_WORDS(ba->length);
        if (nwords == 0) return SYM_T;
        return memcmp(ba->data, bb_bv->data, nwords * sizeof(uint32_t)) == 0 ? SYM_T : CL_NIL;
    }
    /* Structs: same type, slot-wise equalp */
    if (CL_STRUCT_P(a) && CL_STRUCT_P(b)) {
        CL_Struct *sa = (CL_Struct *)CL_OBJ_TO_PTR(a);
        CL_Struct *sb = (CL_Struct *)CL_OBJ_TO_PTR(b);
        uint32_t i;
        if (sa->type_desc != sb->type_desc) return CL_NIL;
        if (sa->n_slots != sb->n_slots) return CL_NIL;
        for (i = 0; i < sa->n_slots; i++) {
            CL_Obj pair[2];
            pair[0] = sa->slots[i]; pair[1] = sb->slots[i];
            if (CL_NULL_P(bi_equalp(pair, 2))) return CL_NIL;
        }
        return SYM_T;
    }
    /* Hash tables: same count, same test, all keys present with equalp values */
    if (CL_HASHTABLE_P(a) && CL_HASHTABLE_P(b)) {
        /* Simplified: just check same object or use equal */
        return bi_equal(args, n);
    }
    /* Pathnames: same as equal */
    if (CL_PATHNAME_P(a) && CL_PATHNAME_P(b)) {
        extern int cl_pathname_equal(CL_Obj a, CL_Obj b);
        return cl_pathname_equal(a, b) ? SYM_T : CL_NIL;
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
    CL_Obj func = cl_coerce_funcdesig(args[0], "MAPCAR");
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
            if (nlists < f->min_args)
                cl_error(CL_ERR_ARGS,
                         "MAPCAR: too few arguments to function (got %d, min %d)",
                         nlists, f->min_args);
            if (f->max_args >= 0 && nlists > f->max_args)
                cl_error(CL_ERR_ARGS,
                         "MAPCAR: too many arguments to function (got %d, max %d)",
                         nlists, f->max_args);
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

    /* Resolve symbol to its function binding (same as funcall) */
    if (CL_SYMBOL_P(func)) {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(func);
        func = s->function;
        if (CL_NULL_P(func) || func == CL_UNBOUND)
            cl_error(CL_ERR_TYPE, "APPLY: symbol has no function binding");
    }
    func = cl_unwrap_funcallable(func);

    if (CL_FUNCTION_P(func)) {
        CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(func);
        if (!f->func) {
            cl_error(CL_ERR_TYPE, "APPLY: NULL function pointer in %s",
                     CL_NULL_P(f->name) ? "?" : cl_symbol_name(f->name));
        }
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
    func = cl_unwrap_funcallable(func);
    if (CL_FUNCTION_P(func)) {
        CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(func);
        if (!f->func) {
            cl_error(CL_ERR_TYPE, "FUNCALL: NULL function pointer in %s",
                     CL_NULL_P(f->name) ? "?" : cl_symbol_name(f->name));
        }
        return f->func(args + 1, n - 1);
    }
    if (CL_BYTECODE_P(func) || CL_CLOSURE_P(func)) {
        return cl_vm_apply(func, args + 1, n - 1);
    }
    cl_error(CL_ERR_TYPE, "FUNCALL: not a function");
    return CL_NIL;
}

/* --- Declarations --- */

static CL_Obj bi_proclaim(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    cl_process_declaration_specifier(args[0]);
    return CL_NIL;
}

/* --- Trace --- */

static CL_Obj trace_list = CL_NIL;

static CL_Obj bi_trace_function(CL_Obj *args, int n)
{
    CL_Symbol *s;
    CL_UNUSED(n);
    if (!CL_SYMBOL_P(args[0]))
        cl_error(CL_ERR_TYPE, "TRACE: not a symbol");
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    if (!(s->flags & CL_SYM_TRACED)) {
        s->flags |= CL_SYM_TRACED;
        cl_trace_count++;
        cl_tables_wrlock();
        trace_list = cl_cons(args[0], trace_list);
        cl_tables_rwunlock();
    }
    return args[0];
}

static CL_Obj bi_untrace_function(CL_Obj *args, int n)
{
    CL_Symbol *s;
    CL_UNUSED(n);
    if (!CL_SYMBOL_P(args[0]))
        cl_error(CL_ERR_TYPE, "UNTRACE: not a symbol");
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    if (s->flags & CL_SYM_TRACED) {
        s->flags &= ~CL_SYM_TRACED;
        cl_trace_count--;
        cl_tables_wrlock();
        {
            CL_Obj prev = CL_NIL, curr = trace_list;
            while (!CL_NULL_P(curr)) {
                if (cl_car(curr) == args[0]) {
                    if (CL_NULL_P(prev))
                        trace_list = cl_cdr(curr);
                    else
                        ((CL_Cons *)CL_OBJ_TO_PTR(prev))->cdr = cl_cdr(curr);
                    break;
                }
                prev = curr;
                curr = cl_cdr(curr);
            }
        }
        cl_tables_rwunlock();
    }
    return args[0];
}

static CL_Obj bi_traced_functions(CL_Obj *args, int n)
{
    CL_Obj result;
    CL_UNUSED(args);
    CL_UNUSED(n);
    cl_tables_rdlock();
    result = trace_list;
    cl_tables_rwunlock();
    return result;
}

static CL_Obj bi_untrace_all(CL_Obj *args, int n)
{
    CL_UNUSED(args);
    CL_UNUSED(n);
    cl_tables_wrlock();
    {
        CL_Obj list = trace_list;
        while (!CL_NULL_P(list)) {
            CL_Obj sym = cl_car(list);
            if (CL_SYMBOL_P(sym)) {
                ((CL_Symbol *)CL_OBJ_TO_PTR(sym))->flags &= ~CL_SYM_TRACED;
            }
            list = cl_cdr(list);
        }
        trace_list = CL_NIL;
    }
    cl_tables_rwunlock();
    cl_trace_count = 0;
    cl_trace_depth = 0;
    return CL_NIL;
}

/* --- Property lists --- */

static CL_Obj bi_symbol_plist(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_SYMBOL_P(args[0]))
        cl_error(CL_ERR_TYPE, "SYMBOL-PLIST: not a symbol");
    return ((CL_Symbol *)CL_OBJ_TO_PTR(args[0]))->plist;
}

static CL_Obj bi_get(CL_Obj *args, int n)
{
    CL_Symbol *s;
    CL_Obj plist, indicator, def;
    if (!CL_SYMBOL_P(args[0]))
        cl_error(CL_ERR_TYPE, "GET: not a symbol");
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    indicator = args[1];
    def = (n > 2) ? args[2] : CL_NIL;
    plist = s->plist;
    while (!CL_NULL_P(plist) && !CL_NULL_P(cl_cdr(plist))) {
        if (cl_car(plist) == indicator)
            return cl_car(cl_cdr(plist));
        plist = cl_cdr(cl_cdr(plist));
    }
    return def;
}

static CL_Obj bi_setf_get(CL_Obj *args, int n)
{
    /* (%setf-get symbol indicator value) */
    CL_Symbol *s;
    CL_Obj plist, indicator, value;
    CL_UNUSED(n);
    if (!CL_SYMBOL_P(args[0]))
        cl_error(CL_ERR_TYPE, "GET: not a symbol");
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    indicator = args[1];
    value = args[2];
    plist = s->plist;
    /* Search for existing indicator */
    while (!CL_NULL_P(plist) && !CL_NULL_P(cl_cdr(plist))) {
        if (cl_car(plist) == indicator) {
            /* Update value in place */
            ((CL_Cons *)CL_OBJ_TO_PTR(cl_cdr(plist)))->car = value;
            return value;
        }
        plist = cl_cdr(cl_cdr(plist));
    }
    /* Not found — prepend indicator+value */
    {
        CL_Obj new_plist;
        CL_GC_PROTECT(value);
        new_plist = cl_cons(indicator, cl_cons(value, s->plist));
        CL_GC_UNPROTECT(1);
        /* Re-fetch symbol pointer after potential GC */
        s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
        s->plist = new_plist;
    }
    return value;
}

static CL_Obj bi_remprop(CL_Obj *args, int n)
{
    CL_Symbol *s;
    CL_Obj indicator, plist, prev_val;
    CL_UNUSED(n);
    if (!CL_SYMBOL_P(args[0]))
        cl_error(CL_ERR_TYPE, "REMPROP: not a symbol");
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    indicator = args[1];
    plist = s->plist;
    prev_val = CL_NIL;
    while (!CL_NULL_P(plist) && !CL_NULL_P(cl_cdr(plist))) {
        if (cl_car(plist) == indicator) {
            CL_Obj rest = cl_cdr(cl_cdr(plist));
            if (CL_NULL_P(prev_val))
                s->plist = rest;
            else
                ((CL_Cons *)CL_OBJ_TO_PTR(prev_val))->cdr = rest;
            return SYM_T;
        }
        prev_val = cl_cdr(plist);  /* value cell */
        plist = cl_cdr(cl_cdr(plist));
    }
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
void cl_builtins_type_init(void);
void cl_builtins_condition_init(void);
void cl_builtins_package_init(void);
void cl_builtins_struct_init(void);
void cl_builtins_stream_init(void);
void cl_builtins_array_init(void);
void cl_float_math_init(void);
void cl_builtins_random_init(void);
void cl_builtins_bitvector_init(void);
void cl_builtins_pathname_init(void);
void cl_builtins_describe_init(void);
void cl_builtins_inspect_init(void);
void cl_builtins_thread_init(void);
void cl_builtins_ffi_init(void);
void cl_builtins_amiga_init(void);

static CL_Obj bi_quit(CL_Obj *args, int n)
{
    int code = 0;
    if (n > 0 && CL_FIXNUM_P(args[0]))
        code = CL_FIXNUM_VAL(args[0]);
    cl_exit_code = code;
    cl_error(CL_ERR_EXIT, "");
    return CL_NIL; /* unreachable */
}

void cl_builtins_init(void)
{
    /* List ops */
    defun("CONS", bi_cons, 2, 2);
    defun("CAR", bi_car, 1, 1);
    defun("CDR", bi_cdr, 1, 1);
    defun("FIRST", bi_car, 1, 1);
    defun("SECOND", bi_second, 1, 1);
    defun("THIRD", bi_third, 1, 1);
    defun("FOURTH", bi_fourth, 1, 1);
    defun("FIFTH", bi_fifth, 1, 1);
    defun("SIXTH", bi_sixth, 1, 1);
    defun("SEVENTH", bi_seventh, 1, 1);
    defun("EIGHTH", bi_eighth, 1, 1);
    defun("NINTH", bi_ninth, 1, 1);
    defun("TENTH", bi_tenth, 1, 1);
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
    defun("SYMBOLP", bi_symbolp, 1, 1);
    defun("STRINGP", bi_stringp, 1, 1);
    defun("SIMPLE-STRING-P", bi_simple_string_p, 1, 1);
    defun("FUNCTIONP", bi_functionp, 1, 1);
    defun("EQ", bi_eq, 2, 2);
    defun("EQL", bi_eql, 2, 2);
    defun("EQUAL", bi_equal, 2, 2);
    defun("EQUALP", bi_equalp, 2, 2);
    defun("NOT", bi_not, 1, 1);

    /* Higher-order */
    defun("MAPCAR", bi_mapcar, 2, -1);
    defun("APPLY", bi_apply, 2, -1);
    defun("FUNCALL", bi_funcall, 1, -1);

    /* Declarations */
    defun("PROCLAIM", bi_proclaim, 1, 1);

    /* Property lists */
    defun("SYMBOL-PLIST", bi_symbol_plist, 1, 1);
    defun("GET", bi_get, 2, 3);
    cl_register_builtin("%SETF-GET", bi_setf_get, 3, 3, cl_package_clamiga);
    defun("REMPROP", bi_remprop, 2, 2);

    /* Trace (internal helpers, called by compiler special forms) */
    cl_register_builtin("%TRACE-FUNCTION", bi_trace_function, 1, 1, cl_package_clamiga);
    cl_register_builtin("%UNTRACE-FUNCTION", bi_untrace_function, 1, 1, cl_package_clamiga);
    cl_register_builtin("%TRACED-FUNCTIONS", bi_traced_functions, 0, 0, cl_package_clamiga);
    cl_register_builtin("%UNTRACE-ALL", bi_untrace_all, 0, 0, cl_package_clamiga);

    /* Sub-module builtins */
    cl_builtins_arith_init();
    cl_builtins_io_init();
    cl_builtins_mutation_init();
    cl_builtins_strings_init();
    cl_builtins_lists_init();
    cl_builtins_hashtable_init();
    cl_builtins_sequence_init();
    cl_builtins_sequence2_init();
    cl_builtins_type_init();
    cl_builtins_condition_init();
    cl_builtins_package_init();
    cl_builtins_struct_init();
    cl_builtins_stream_init();
    cl_builtins_array_init();
    cl_float_math_init();
    cl_builtins_random_init();
    cl_builtins_bitvector_init();
    cl_builtins_pathname_init();
    cl_builtins_describe_init();
    cl_builtins_inspect_init();
    cl_builtins_thread_init();
    cl_builtins_ffi_init();
    cl_builtins_amiga_init();

    /* All CL symbols now interned — mark them exported */
    cl_package_export_all_cl_symbols();

    /* Process control — in CL-USER, not CL (quit/exit are non-standard) */
    {
        CL_Obj sym;
        CL_Obj fn;
        CL_Symbol *s;

        sym = cl_intern_in("QUIT", 4, cl_package_cl_user);
        fn = cl_make_function(bi_quit, sym, 0, 1);
        s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
        s->function = fn;
        s->value = fn;

        sym = cl_intern_in("EXIT", 4, cl_package_cl_user);
        fn = cl_make_function(bi_quit, sym, 0, 1);
        s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
        s->function = fn;
        s->value = fn;
    }

    /* Register cached symbols for GC compaction forwarding */
    cl_gc_register_root(&trace_list);
}
