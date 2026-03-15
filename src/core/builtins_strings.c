#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "printer.h"
#include "vm.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Helper to register a builtin */
static void defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_cl);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    s->value = fn;
}

/* --- Character functions --- */

static CL_Obj bi_characterp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_CHAR_P(args[0]) ? SYM_T : CL_NIL;
}

static CL_Obj bi_char_eq(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]) || !CL_CHAR_P(args[1]))
        cl_error(CL_ERR_TYPE, "CHAR=: not a character");
    return (CL_CHAR_VAL(args[0]) == CL_CHAR_VAL(args[1])) ? SYM_T : CL_NIL;
}

static CL_Obj bi_char_ne(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]) || !CL_CHAR_P(args[1]))
        cl_error(CL_ERR_TYPE, "CHAR/=: not a character");
    return (CL_CHAR_VAL(args[0]) != CL_CHAR_VAL(args[1])) ? SYM_T : CL_NIL;
}

static CL_Obj bi_char_lt(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]) || !CL_CHAR_P(args[1]))
        cl_error(CL_ERR_TYPE, "CHAR<: not a character");
    return (CL_CHAR_VAL(args[0]) < CL_CHAR_VAL(args[1])) ? SYM_T : CL_NIL;
}

static CL_Obj bi_char_gt(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]) || !CL_CHAR_P(args[1]))
        cl_error(CL_ERR_TYPE, "CHAR>: not a character");
    return (CL_CHAR_VAL(args[0]) > CL_CHAR_VAL(args[1])) ? SYM_T : CL_NIL;
}

static CL_Obj bi_char_le(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]) || !CL_CHAR_P(args[1]))
        cl_error(CL_ERR_TYPE, "CHAR<=: not a character");
    return (CL_CHAR_VAL(args[0]) <= CL_CHAR_VAL(args[1])) ? SYM_T : CL_NIL;
}

static CL_Obj bi_char_ge(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]) || !CL_CHAR_P(args[1]))
        cl_error(CL_ERR_TYPE, "CHAR>=: not a character");
    return (CL_CHAR_VAL(args[0]) >= CL_CHAR_VAL(args[1])) ? SYM_T : CL_NIL;
}

static CL_Obj bi_char_code(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "CHAR-CODE: not a character");
    return CL_MAKE_FIXNUM(CL_CHAR_VAL(args[0]));
}

static CL_Obj bi_code_char(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_FIXNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "CODE-CHAR: not an integer");
    return CL_MAKE_CHAR(CL_FIXNUM_VAL(args[0]));
}

static CL_Obj bi_char_upcase(CL_Obj *args, int n)
{
    int c;
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "CHAR-UPCASE: not a character");
    c = CL_CHAR_VAL(args[0]);
    if (c >= 'a' && c <= 'z') c -= 32;
    return CL_MAKE_CHAR(c);
}

static CL_Obj bi_char_downcase(CL_Obj *args, int n)
{
    int c;
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "CHAR-DOWNCASE: not a character");
    c = CL_CHAR_VAL(args[0]);
    if (c >= 'A' && c <= 'Z') c += 32;
    return CL_MAKE_CHAR(c);
}

static CL_Obj bi_upper_case_p(CL_Obj *args, int n)
{
    int c;
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "UPPER-CASE-P: not a character");
    c = CL_CHAR_VAL(args[0]);
    return (c >= 'A' && c <= 'Z') ? SYM_T : CL_NIL;
}

static CL_Obj bi_lower_case_p(CL_Obj *args, int n)
{
    int c;
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "LOWER-CASE-P: not a character");
    c = CL_CHAR_VAL(args[0]);
    return (c >= 'a' && c <= 'z') ? SYM_T : CL_NIL;
}

static CL_Obj bi_alpha_char_p(CL_Obj *args, int n)
{
    int c;
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "ALPHA-CHAR-P: not a character");
    c = CL_CHAR_VAL(args[0]);
    return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) ? SYM_T : CL_NIL;
}

static CL_Obj bi_digit_char_p(CL_Obj *args, int n)
{
    int c;
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "DIGIT-CHAR-P: not a character");
    c = CL_CHAR_VAL(args[0]);
    return (c >= '0' && c <= '9') ? SYM_T : CL_NIL;
}

/* --- Symbol functions --- */

static CL_Obj bi_symbol_name(CL_Obj *args, int n)
{
    CL_Symbol *s;
    CL_UNUSED(n);
    if (CL_NULL_P(args[0]))
        return cl_make_string("NIL", 3);
    if (!CL_SYMBOL_P(args[0]))
        cl_error(CL_ERR_TYPE, "SYMBOL-NAME: not a symbol");
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    return s->name;
}

static CL_Obj bi_symbol_package(CL_Obj *args, int n)
{
    CL_Symbol *s;
    CL_UNUSED(n);
    if (CL_NULL_P(args[0]))
        return cl_package_cl;
    if (!CL_SYMBOL_P(args[0]))
        cl_error(CL_ERR_TYPE, "SYMBOL-PACKAGE: not a symbol");
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    return s->package;
}

static CL_Obj bi_make_symbol(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "MAKE-SYMBOL: not a string");
    return cl_make_symbol(args[0]);
}

static CL_Obj bi_keywordp(CL_Obj *args, int n)
{
    CL_Symbol *s;
    CL_UNUSED(n);
    if (CL_NULL_P(args[0])) return CL_NIL;
    if (!CL_SYMBOL_P(args[0])) return CL_NIL;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(args[0]);
    return (s->package == cl_package_keyword) ? SYM_T : CL_NIL;
}

/* --- String functions --- */

static const char *obj_to_cstr(CL_Obj obj, uint32_t *out_len)
{
    if (CL_STRING_P(obj)) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(obj);
        *out_len = s->length;
        return s->data;
    }
    if (CL_SYMBOL_P(obj) || CL_NULL_P(obj)) {
        const char *name = cl_symbol_name(obj);
        *out_len = (uint32_t)strlen(name);
        return name;
    }
    return NULL;
}

static CL_Obj bi_string_eq(CL_Obj *args, int n)
{
    uint32_t la, lb;
    const char *a, *b;
    CL_UNUSED(n);
    a = obj_to_cstr(args[0], &la);
    b = obj_to_cstr(args[1], &lb);
    if (!a || !b) cl_error(CL_ERR_TYPE, "STRING=: not a string designator");
    if (la != lb) return CL_NIL;
    return (memcmp(a, b, la) == 0) ? SYM_T : CL_NIL;
}

static CL_Obj bi_string_equal(CL_Obj *args, int n)
{
    uint32_t la, lb, i;
    const char *a, *b;
    CL_UNUSED(n);
    a = obj_to_cstr(args[0], &la);
    b = obj_to_cstr(args[1], &lb);
    if (!a || !b) cl_error(CL_ERR_TYPE, "STRING-EQUAL: not a string designator");
    if (la != lb) return CL_NIL;
    for (i = 0; i < la; i++) {
        int ca = a[i], cb = b[i];
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return CL_NIL;
    }
    return SYM_T;
}

static CL_Obj bi_string_lt(CL_Obj *args, int n)
{
    uint32_t la, lb, min;
    const char *a, *b;
    int cmp;
    CL_UNUSED(n);
    a = obj_to_cstr(args[0], &la);
    b = obj_to_cstr(args[1], &lb);
    if (!a || !b) cl_error(CL_ERR_TYPE, "STRING<: not a string designator");
    min = la < lb ? la : lb;
    cmp = memcmp(a, b, min);
    if (cmp < 0 || (cmp == 0 && la < lb)) return SYM_T;
    return CL_NIL;
}

static CL_Obj bi_string_gt(CL_Obj *args, int n)
{
    uint32_t la, lb, min;
    const char *a, *b;
    int cmp;
    CL_UNUSED(n);
    a = obj_to_cstr(args[0], &la);
    b = obj_to_cstr(args[1], &lb);
    if (!a || !b) cl_error(CL_ERR_TYPE, "STRING>: not a string designator");
    min = la < lb ? la : lb;
    cmp = memcmp(a, b, min);
    if (cmp > 0 || (cmp == 0 && la > lb)) return SYM_T;
    return CL_NIL;
}

static CL_Obj bi_string_le(CL_Obj *args, int n)
{
    uint32_t la, lb, min;
    const char *a, *b;
    int cmp;
    CL_UNUSED(n);
    a = obj_to_cstr(args[0], &la);
    b = obj_to_cstr(args[1], &lb);
    if (!a || !b) cl_error(CL_ERR_TYPE, "STRING<=: not a string designator");
    min = la < lb ? la : lb;
    cmp = memcmp(a, b, min);
    if (cmp < 0 || (cmp == 0 && la <= lb)) return SYM_T;
    return CL_NIL;
}

static CL_Obj bi_string_ge(CL_Obj *args, int n)
{
    uint32_t la, lb, min;
    const char *a, *b;
    int cmp;
    CL_UNUSED(n);
    a = obj_to_cstr(args[0], &la);
    b = obj_to_cstr(args[1], &lb);
    if (!a || !b) cl_error(CL_ERR_TYPE, "STRING>=: not a string designator");
    min = la < lb ? la : lb;
    cmp = memcmp(a, b, min);
    if (cmp > 0 || (cmp == 0 && la >= lb)) return SYM_T;
    return CL_NIL;
}

/* Coerce a string designator (string, symbol, or character) to CL_String*.
 * Per CL spec, string designators are accepted by string functions. */
static CL_String *coerce_to_cl_string(CL_Obj obj, const char *func_name)
{
    if (CL_STRING_P(obj))
        return (CL_String *)CL_OBJ_TO_PTR(obj);
    if (CL_SYMBOL_P(obj)) {
        CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(obj);
        return (CL_String *)CL_OBJ_TO_PTR(sym->name);
    }
    (void)func_name;
    cl_error(CL_ERR_TYPE, "not a string designator");
    return NULL;
}

static CL_Obj bi_string_upcase(CL_Obj *args, int n)
{
    CL_String *s;
    CL_Obj result;
    CL_String *rs;
    uint32_t i;
    CL_UNUSED(n);
    s = coerce_to_cl_string(args[0], "STRING-UPCASE");
    result = cl_make_string(s->data, s->length);
    rs = (CL_String *)CL_OBJ_TO_PTR(result);
    for (i = 0; i < rs->length; i++) {
        if (rs->data[i] >= 'a' && rs->data[i] <= 'z')
            rs->data[i] -= 32;
    }
    return result;
}

static CL_Obj bi_string_downcase(CL_Obj *args, int n)
{
    CL_String *s;
    CL_Obj result;
    CL_String *rs;
    uint32_t i;
    CL_UNUSED(n);
    s = coerce_to_cl_string(args[0], "STRING-DOWNCASE");
    result = cl_make_string(s->data, s->length);
    rs = (CL_String *)CL_OBJ_TO_PTR(result);
    for (i = 0; i < rs->length; i++) {
        if (rs->data[i] >= 'A' && rs->data[i] <= 'Z')
            rs->data[i] += 32;
    }
    return result;
}

static int trim_char_in_set(int ch, CL_String *set)
{
    uint32_t i;
    for (i = 0; i < set->length; i++)
        if (set->data[i] == ch) return 1;
    return 0;
}

static CL_Obj bi_string_trim(CL_Obj *args, int n)
{
    CL_String *set, *s;
    uint32_t start, end;
    CL_UNUSED(n);
    set = coerce_to_cl_string(args[0], "STRING-TRIM");
    s = coerce_to_cl_string(args[1], "STRING-TRIM");
    start = 0;
    end = s->length;
    while (start < end && trim_char_in_set(s->data[start], set)) start++;
    while (end > start && trim_char_in_set(s->data[end - 1], set)) end--;
    return cl_make_string(s->data + start, end - start);
}

static CL_Obj bi_string_left_trim(CL_Obj *args, int n)
{
    CL_String *set, *s;
    uint32_t start;
    CL_UNUSED(n);
    set = coerce_to_cl_string(args[0], "STRING-LEFT-TRIM");
    s = coerce_to_cl_string(args[1], "STRING-LEFT-TRIM");
    start = 0;
    while (start < s->length && trim_char_in_set(s->data[start], set)) start++;
    return cl_make_string(s->data + start, s->length - start);
}

static CL_Obj bi_string_right_trim(CL_Obj *args, int n)
{
    CL_String *set, *s;
    uint32_t end;
    CL_UNUSED(n);
    set = coerce_to_cl_string(args[0], "STRING-RIGHT-TRIM");
    s = coerce_to_cl_string(args[1], "STRING-RIGHT-TRIM");
    end = s->length;
    while (end > 0 && trim_char_in_set(s->data[end - 1], set)) end--;
    return cl_make_string(s->data, end);
}

static CL_Obj bi_subseq(CL_Obj *args, int n)
{
    int32_t start, end;
    if (!CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE, "SUBSEQ: start must be an integer");
    start = CL_FIXNUM_VAL(args[1]);

    if (CL_STRING_P(args[0])) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
        end = (n > 2 && !CL_NULL_P(args[2]) && CL_FIXNUM_P(args[2]))
            ? CL_FIXNUM_VAL(args[2]) : (int32_t)s->length;
        if (start < 0) start = 0;
        if (end > (int32_t)s->length) end = (int32_t)s->length;
        if (start > end) start = end;
        return cl_make_string(s->data + start, (uint32_t)(end - start));
    }
    /* Vector subseq */
    if (CL_VECTOR_P(args[0])) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
        uint32_t len = cl_vector_active_length(v);
        CL_Obj *data;
        CL_Obj result;
        int32_t i, rlen;
        end = (n > 2 && !CL_NULL_P(args[2]) && CL_FIXNUM_P(args[2]))
            ? CL_FIXNUM_VAL(args[2]) : (int32_t)len;
        if (start < 0) start = 0;
        if (end > (int32_t)len) end = (int32_t)len;
        if (start > end) start = end;
        rlen = end - start;
        CL_GC_PROTECT(args[0]);
        result = cl_make_vector((uint32_t)rlen);
        v = (CL_Vector *)CL_OBJ_TO_PTR(args[0]);
        data = cl_vector_data(v);
        {
            CL_Vector *rv = (CL_Vector *)CL_OBJ_TO_PTR(result);
            CL_Obj *rdata = cl_vector_data(rv);
            for (i = 0; i < rlen; i++)
                rdata[i] = data[start + i];
        }
        CL_GC_UNPROTECT(1);
        return result;
    }
    if (CL_BIT_VECTOR_P(args[0])) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(args[0]);
        uint32_t len = cl_bv_active_length(bv);
        CL_Obj result;
        int32_t i, rlen;
        end = (n > 2 && !CL_NULL_P(args[2]) && CL_FIXNUM_P(args[2]))
            ? CL_FIXNUM_VAL(args[2]) : (int32_t)len;
        if (start < 0) start = 0;
        if (end > (int32_t)len) end = (int32_t)len;
        if (start > end) start = end;
        rlen = end - start;
        CL_GC_PROTECT(args[0]);
        result = cl_make_bit_vector((uint32_t)rlen);
        bv = (CL_BitVector *)CL_OBJ_TO_PTR(args[0]);
        {
            CL_BitVector *rv = (CL_BitVector *)CL_OBJ_TO_PTR(result);
            for (i = 0; i < rlen; i++)
                cl_bv_set_bit(rv, (uint32_t)i, cl_bv_get_bit(bv, (uint32_t)(start + i)));
        }
        CL_GC_UNPROTECT(1);
        return result;
    }
    /* List subseq */
    {
        CL_Obj list = args[0], result = CL_NIL, tail = CL_NIL;
        int32_t i = 0;
        CL_GC_PROTECT(result);
        CL_GC_PROTECT(tail);
        end = (n > 2 && !CL_NULL_P(args[2]) && CL_FIXNUM_P(args[2]))
            ? CL_FIXNUM_VAL(args[2]) : 0x7FFFFFFF;
        while (!CL_NULL_P(list) && i < end) {
            if (i >= start) {
                CL_Obj cell = cl_cons(cl_car(list), CL_NIL);
                if (CL_NULL_P(result)) result = cell;
                else ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
                tail = cell;
            }
            list = cl_cdr(list);
            i++;
        }
        CL_GC_UNPROTECT(2);
        return result;
    }
}

/* Helper: count total elements across sequences args[1..n-1] */
static uint32_t concat_total_length(CL_Obj *args, int n)
{
    uint32_t total = 0;
    int i;
    for (i = 1; i < n; i++) {
        if (CL_STRING_P(args[i])) {
            CL_String *s = (CL_String *)CL_OBJ_TO_PTR(args[i]);
            total += s->length;
        } else if (CL_VECTOR_P(args[i])) {
            CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(args[i]);
            total += cl_vector_active_length(v);
        } else if (CL_NULL_P(args[i])) {
            /* empty */
        } else if (CL_CONS_P(args[i])) {
            CL_Obj p = args[i];
            while (CL_CONS_P(p)) { total++; p = cl_cdr(p); }
        } else if (CL_BIT_VECTOR_P(args[i])) {
            CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(args[i]);
            total += bv->length;
        }
    }
    return total;
}

/* Helper: iterate over sequence seq, calling cb(elem, ctx) for each element */
typedef void (*concat_cb)(CL_Obj elem, void *ctx);

static void concat_iterate(CL_Obj seq, concat_cb cb, void *ctx)
{
    if (CL_STRING_P(seq)) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(seq);
        uint32_t j;
        for (j = 0; j < s->length; j++)
            cb(CL_MAKE_CHAR((unsigned char)s->data[j]), ctx);
    } else if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        uint32_t len = cl_vector_active_length(v);
        uint32_t j;
        for (j = 0; j < len; j++)
            cb(cl_vector_data(v)[j], ctx);
    } else if (CL_NULL_P(seq)) {
        /* empty */
    } else if (CL_CONS_P(seq)) {
        CL_Obj p = seq;
        while (CL_CONS_P(p)) {
            cb(cl_car(p), ctx);
            p = cl_cdr(p);
        }
    } else if (CL_BIT_VECTOR_P(seq)) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(seq);
        uint32_t j;
        for (j = 0; j < bv->length; j++)
            cb(CL_MAKE_FIXNUM(cl_bv_get_bit(bv, j)), ctx);
    }
}

/* Callback context for string result */
typedef struct { char *buf; uint32_t pos; uint32_t cap; } concat_str_ctx;
static void concat_str_cb(CL_Obj elem, void *ctx_)
{
    concat_str_ctx *ctx = (concat_str_ctx *)ctx_;
    if (CL_CHAR_P(elem)) {
        if (ctx->pos < ctx->cap)
            ctx->buf[ctx->pos++] = (char)CL_CHAR_VAL(elem);
    } else {
        cl_error(CL_ERR_TYPE, "CONCATENATE: element is not a character for string result");
    }
}

/* Callback context for vector result */
typedef struct { CL_Obj vec; uint32_t pos; } concat_vec_ctx;
static void concat_vec_cb(CL_Obj elem, void *ctx_)
{
    concat_vec_ctx *ctx = (concat_vec_ctx *)ctx_;
    CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(ctx->vec);
    if (ctx->pos < cl_vector_active_length(v))
        cl_vector_data(v)[ctx->pos++] = elem;
}

/* Callback context for list result */
typedef struct { CL_Obj head; CL_Obj tail; } concat_list_ctx;
static void concat_list_cb(CL_Obj elem, void *ctx_)
{
    concat_list_ctx *ctx = (concat_list_ctx *)ctx_;
    CL_Obj cell = cl_cons(elem, CL_NIL);
    if (CL_NULL_P(ctx->head)) {
        ctx->head = cell;
        ctx->tail = cell;
    } else {
        ((CL_Cons *)CL_OBJ_TO_PTR(ctx->tail))->cdr = cell;
        ctx->tail = cell;
    }
}

static CL_Obj bi_concatenate(CL_Obj *args, int n)
{
    CL_Obj result_type = args[0];
    const char *tname;
    int i;

    if (CL_NULL_P(result_type))
        cl_error(CL_ERR_TYPE, "CONCATENATE: result type must not be NIL");
    if (!CL_SYMBOL_P(result_type))
        cl_error(CL_ERR_TYPE, "CONCATENATE: result type must be a type specifier");
    tname = cl_symbol_name(result_type);

    /* String result types */
    if (strcmp(tname, "STRING") == 0 || strcmp(tname, "SIMPLE-STRING") == 0 ||
        strcmp(tname, "BASE-STRING") == 0 || strcmp(tname, "SIMPLE-BASE-STRING") == 0) {
        char buf[4096];
        concat_str_ctx ctx = { buf, 0, sizeof(buf) };
        for (i = 1; i < n; i++)
            concat_iterate(args[i], concat_str_cb, &ctx);
        return cl_make_string(buf, ctx.pos);
    }

    /* Vector result types */
    if (strcmp(tname, "VECTOR") == 0 || strcmp(tname, "SIMPLE-VECTOR") == 0) {
        uint32_t total = concat_total_length(args, n);
        concat_vec_ctx ctx;
        ctx.vec = cl_make_vector(total);
        ctx.pos = 0;
        CL_GC_PROTECT(ctx.vec);
        for (i = 1; i < n; i++)
            concat_iterate(args[i], concat_vec_cb, &ctx);
        CL_GC_UNPROTECT(1);
        return ctx.vec;
    }

    /* List result type */
    if (strcmp(tname, "LIST") == 0) {
        concat_list_ctx ctx = { CL_NIL, CL_NIL };
        CL_GC_PROTECT(ctx.head);
        CL_GC_PROTECT(ctx.tail);
        for (i = 1; i < n; i++)
            concat_iterate(args[i], concat_list_cb, &ctx);
        CL_GC_UNPROTECT(2);
        return ctx.head;
    }

    cl_error(CL_ERR_TYPE, "CONCATENATE: unsupported result type %s", tname);
    return CL_NIL;
}

static CL_Obj bi_char_accessor(CL_Obj *args, int n)
{
    CL_String *s;
    int32_t idx;
    CL_UNUSED(n);
    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "CHAR: not a string");
    if (!CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE, "CHAR: index must be an integer");
    s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
    idx = CL_FIXNUM_VAL(args[1]);
    if (idx < 0 || (uint32_t)idx >= s->length)
        cl_error(CL_ERR_ARGS, "CHAR: index %d out of range", (int)idx);
    return CL_MAKE_CHAR((unsigned char)s->data[idx]);
}

static CL_Obj bi_string_coerce(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (CL_STRING_P(args[0])) return args[0];
    if (CL_NULL_P(args[0])) return cl_make_string("NIL", 3);
    if (CL_SYMBOL_P(args[0])) {
        const char *name = cl_symbol_name(args[0]);
        return cl_make_string(name, (uint32_t)strlen(name));
    }
    if (CL_CHAR_P(args[0])) {
        char c = (char)CL_CHAR_VAL(args[0]);
        return cl_make_string(&c, 1);
    }
    cl_error(CL_ERR_TYPE, "STRING: cannot coerce to string");
    return CL_NIL;
}

static CL_Obj bi_parse_integer(CL_Obj *args, int n)
{
    CL_String *s;
    int32_t start = 0, end, radix = 10;
    int32_t result = 0, sign = 1;
    int32_t i;
    int found = 0;

    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "PARSE-INTEGER: not a string");
    s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
    end = (int32_t)s->length;

    {
        int k;
        for (k = 1; k + 1 < n; k += 2) {
            if (CL_SYMBOL_P(args[k])) {
                const char *kn = cl_symbol_name(args[k]);
                if (strcmp(kn, "START") == 0 && CL_FIXNUM_P(args[k+1]))
                    start = CL_FIXNUM_VAL(args[k+1]);
                else if (strcmp(kn, "END") == 0 && CL_FIXNUM_P(args[k+1]))
                    end = CL_FIXNUM_VAL(args[k+1]);
                else if (strcmp(kn, "RADIX") == 0 && CL_FIXNUM_P(args[k+1]))
                    radix = CL_FIXNUM_VAL(args[k+1]);
            }
        }
    }

    while (start < end && (s->data[start] == ' ' || s->data[start] == '\t'))
        start++;

    if (start < end && (s->data[start] == '+' || s->data[start] == '-')) {
        if (s->data[start] == '-') sign = -1;
        start++;
    }

    for (i = start; i < end; i++) {
        int digit;
        char c = s->data[i];
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'A' && c <= 'Z') digit = c - 'A' + 10;
        else if (c >= 'a' && c <= 'z') digit = c - 'a' + 10;
        else break;
        if (digit >= radix) break;
        result = result * radix + digit;
        found = 1;
    }

    if (!found)
        cl_error(CL_ERR_GENERAL, "PARSE-INTEGER: no digits found");

    cl_mv_values[0] = CL_MAKE_FIXNUM(sign * result);
    cl_mv_values[1] = CL_MAKE_FIXNUM(i);
    cl_mv_count = 2;
    return CL_MAKE_FIXNUM(sign * result);
}

/* Keywords for write-to-string (same as write) */
static CL_Obj KW_WTS_ESCAPE;
static CL_Obj KW_WTS_READABLY;
static CL_Obj KW_WTS_BASE;
static CL_Obj KW_WTS_RADIX;
static CL_Obj KW_WTS_LEVEL;
static CL_Obj KW_WTS_LENGTH;
static CL_Obj KW_WTS_CASE;
static CL_Obj KW_WTS_GENSYM;
static CL_Obj KW_WTS_ARRAY;
static CL_Obj KW_WTS_CIRCLE;
static CL_Obj KW_WTS_PRETTY;
static CL_Obj KW_WTS_RIGHT_MARGIN;

static CL_Obj bi_write_to_string(CL_Obj *args, int n)
{
    char buf[1024];
    int len, i;
    CL_Symbol *se = NULL, *sr = NULL, *sb = NULL, *sx = NULL;
    CL_Symbol *sl = NULL, *sn = NULL, *sc = NULL, *sg = NULL;
    CL_Symbol *sa = NULL, *si = NULL, *sp = NULL, *sm = NULL;
    CL_Obj prev_e, prev_r, prev_b, prev_x, prev_l, prev_n;
    CL_Obj prev_c, prev_g, prev_a, prev_i, prev_p, prev_m;
    int has_keywords = (n > 1);

    if (has_keywords) {
        se = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_ESCAPE);    prev_e = se->value;
        sr = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_READABLY);  prev_r = sr->value;
        sb = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_BASE);      prev_b = sb->value;
        sx = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_RADIX);     prev_x = sx->value;
        sl = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_LEVEL);     prev_l = sl->value;
        sn = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_LENGTH);    prev_n = sn->value;
        sc = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_CASE);      prev_c = sc->value;
        sg = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_GENSYM);    prev_g = sg->value;
        sa = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_ARRAY);     prev_a = sa->value;
        si = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_CIRCLE);    prev_i = si->value;
        sp = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_PRETTY);    prev_p = sp->value;
        sm = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_RIGHT_MARGIN); prev_m = sm->value;

        for (i = 1; i + 1 < n; i += 2) {
            CL_Obj kw = args[i];
            CL_Obj val = args[i + 1];
            if (kw == KW_WTS_ESCAPE)        se->value = val;
            else if (kw == KW_WTS_READABLY) sr->value = val;
            else if (kw == KW_WTS_BASE)     sb->value = val;
            else if (kw == KW_WTS_RADIX)    sx->value = val;
            else if (kw == KW_WTS_LEVEL)    sl->value = val;
            else if (kw == KW_WTS_LENGTH)   sn->value = val;
            else if (kw == KW_WTS_CASE)     sc->value = val;
            else if (kw == KW_WTS_GENSYM)   sg->value = val;
            else if (kw == KW_WTS_ARRAY)    sa->value = val;
            else if (kw == KW_WTS_CIRCLE)   si->value = val;
            else if (kw == KW_WTS_PRETTY)   sp->value = val;
            else if (kw == KW_WTS_RIGHT_MARGIN) sm->value = val;
        }
    }

    len = cl_prin1_to_string(args[0], buf, sizeof(buf));

    if (has_keywords) {
        se->value = prev_e;
        sr->value = prev_r;
        sb->value = prev_b;
        sx->value = prev_x;
        sl->value = prev_l;
        sn->value = prev_n;
        sc->value = prev_c;
        sg->value = prev_g;
        sa->value = prev_a;
        si->value = prev_i;
        sp->value = prev_p;
        sm->value = prev_m;
    }

    return cl_make_string(buf, (uint32_t)len);
}

static CL_Obj bi_prin1_to_string_fn(CL_Obj *args, int n)
{
    char buf[1024];
    int len;
    CL_UNUSED(n);
    len = cl_prin1_to_string(args[0], buf, sizeof(buf));
    return cl_make_string(buf, (uint32_t)len);
}

static CL_Obj bi_princ_to_string_fn(CL_Obj *args, int n)
{
    char buf[1024];
    int len;
    CL_UNUSED(n);
    len = cl_princ_to_string(args[0], buf, sizeof(buf));
    return cl_make_string(buf, (uint32_t)len);
}

/* --- Phase 8 Step 2: Additional string/char operations --- */

static CL_Obj bi_string_capitalize(CL_Obj *args, int n)
{
    CL_String *s;
    CL_Obj result;
    CL_String *rs;
    uint32_t i;
    int in_word = 0;
    CL_UNUSED(n);
    s = coerce_to_cl_string(args[0], "STRING-CAPITALIZE");
    result = cl_make_string(s->data, s->length);
    rs = (CL_String *)CL_OBJ_TO_PTR(result);
    for (i = 0; i < rs->length; i++) {
        char c = rs->data[i];
        int is_alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        int is_alnum = is_alpha || (c >= '0' && c <= '9');
        if (!in_word && is_alpha) {
            /* Capitalize first char of word */
            if (c >= 'a' && c <= 'z') rs->data[i] = c - 32;
            in_word = 1;
        } else if (in_word && is_alnum) {
            /* Downcase rest of word */
            if (c >= 'A' && c <= 'Z') rs->data[i] = c + 32;
        } else {
            in_word = is_alnum;
        }
    }
    return result;
}

static CL_Obj bi_nstring_upcase(CL_Obj *args, int n)
{
    CL_String *s;
    uint32_t i;
    CL_UNUSED(n);
    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "NSTRING-UPCASE: not a string");
    s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
    for (i = 0; i < s->length; i++) {
        if (s->data[i] >= 'a' && s->data[i] <= 'z')
            s->data[i] -= 32;
    }
    return args[0];
}

static CL_Obj bi_nstring_downcase(CL_Obj *args, int n)
{
    CL_String *s;
    uint32_t i;
    CL_UNUSED(n);
    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "NSTRING-DOWNCASE: not a string");
    s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
    for (i = 0; i < s->length; i++) {
        if (s->data[i] >= 'A' && s->data[i] <= 'Z')
            s->data[i] += 32;
    }
    return args[0];
}

static CL_Obj bi_nstring_capitalize(CL_Obj *args, int n)
{
    CL_String *s;
    uint32_t i;
    int in_word = 0;
    CL_UNUSED(n);
    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "NSTRING-CAPITALIZE: not a string");
    s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
    for (i = 0; i < s->length; i++) {
        char c = s->data[i];
        int is_alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        int is_alnum = is_alpha || (c >= '0' && c <= '9');
        if (!in_word && is_alpha) {
            if (c >= 'a' && c <= 'z') s->data[i] = c - 32;
            in_word = 1;
        } else if (in_word && is_alnum) {
            if (c >= 'A' && c <= 'Z') s->data[i] = c + 32;
        } else {
            in_word = is_alnum;
        }
    }
    return args[0];
}

/* char-name table */
static const struct { int code; const char *name; } char_names[] = {
    {'\0', "Null"},
    {' ',  "Space"},
    {'\n', "Newline"},
    {'\t', "Tab"},
    {'\r', "Return"},
    {'\b', "Backspace"},
    {127,  "Rubout"},
    {'\f', "Page"},
    {'\n', "Linefeed"},
    {0, NULL}
};

static CL_Obj bi_char_name(CL_Obj *args, int n)
{
    int c, i;
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "CHAR-NAME: not a character");
    c = CL_CHAR_VAL(args[0]);
    for (i = 0; char_names[i].name; i++) {
        if (char_names[i].code == c)
            return cl_make_string(char_names[i].name,
                                  (uint32_t)strlen(char_names[i].name));
    }
    return CL_NIL;
}

#ifdef PLATFORM_AMIGA
/* vbcc has no <strings.h> — provide case-insensitive compare */
static int cl_local_strcasecmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
#define STRCASECMP cl_local_strcasecmp
#else
#include <strings.h>
#define STRCASECMP strcasecmp
#endif

static CL_Obj bi_name_char(CL_Obj *args, int n)
{
    const char *name;
    uint32_t len;
    int i;
    CL_UNUSED(n);
    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "NAME-CHAR: not a string");
    {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
        name = s->data;
        len = s->length;
    }
    for (i = 0; char_names[i].name; i++) {
        if (strlen(char_names[i].name) == len &&
            STRCASECMP(char_names[i].name, name) == 0)
            return CL_MAKE_CHAR(char_names[i].code);
    }
    return CL_NIL;
}

static CL_Obj bi_graphic_char_p(CL_Obj *args, int n)
{
    int c;
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "GRAPHIC-CHAR-P: not a character");
    c = CL_CHAR_VAL(args[0]);
    return (c >= 32 && c < 127) ? SYM_T : CL_NIL;
}

static CL_Obj bi_standard_char_p(CL_Obj *args, int n)
{
    int c;
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "STANDARD-CHAR-P: not a character");
    c = CL_CHAR_VAL(args[0]);
    /* Standard chars: graphic chars + space + newline */
    return ((c >= 32 && c < 127) || c == '\n') ? SYM_T : CL_NIL;
}

static CL_Obj bi_alphanumericp(CL_Obj *args, int n)
{
    int c;
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "ALPHANUMERICP: not a character");
    c = CL_CHAR_VAL(args[0]);
    return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9')) ? SYM_T : CL_NIL;
}

static CL_Obj bi_digit_char(CL_Obj *args, int n)
{
    int32_t weight, radix = 10;
    CL_UNUSED(n);
    if (!CL_FIXNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "DIGIT-CHAR: not an integer");
    weight = CL_FIXNUM_VAL(args[0]);
    if (n >= 2 && CL_FIXNUM_P(args[1]))
        radix = CL_FIXNUM_VAL(args[1]);
    if (weight < 0 || weight >= radix)
        return CL_NIL;
    if (weight < 10)
        return CL_MAKE_CHAR('0' + weight);
    return CL_MAKE_CHAR('A' + weight - 10);
}

static CL_Obj bi_both_case_p(CL_Obj *args, int n)
{
    int c;
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "BOTH-CASE-P: not a character");
    c = CL_CHAR_VAL(args[0]);
    return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
        ? SYM_T : CL_NIL;
}

static CL_Obj bi_char_equal(CL_Obj *args, int n)
{
    int a, b;
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]) || !CL_CHAR_P(args[1]))
        cl_error(CL_ERR_TYPE, "CHAR-EQUAL: not a character");
    a = CL_CHAR_VAL(args[0]);
    b = CL_CHAR_VAL(args[1]);
    if (a >= 'a' && a <= 'z') a -= 32;
    if (b >= 'a' && b <= 'z') b -= 32;
    return (a == b) ? SYM_T : CL_NIL;
}

static CL_Obj bi_char_not_equal(CL_Obj *args, int n)
{
    int a, b;
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]) || !CL_CHAR_P(args[1]))
        cl_error(CL_ERR_TYPE, "CHAR-NOT-EQUAL: not a character");
    a = CL_CHAR_VAL(args[0]);
    b = CL_CHAR_VAL(args[1]);
    if (a >= 'a' && a <= 'z') a -= 32;
    if (b >= 'a' && b <= 'z') b -= 32;
    return (a != b) ? SYM_T : CL_NIL;
}

static CL_Obj bi_char_lessp(CL_Obj *args, int n)
{
    int a, b;
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]) || !CL_CHAR_P(args[1]))
        cl_error(CL_ERR_TYPE, "CHAR-LESSP: not a character");
    a = CL_CHAR_VAL(args[0]);
    b = CL_CHAR_VAL(args[1]);
    if (a >= 'a' && a <= 'z') a -= 32;
    if (b >= 'a' && b <= 'z') b -= 32;
    return (a < b) ? SYM_T : CL_NIL;
}

static CL_Obj bi_char_greaterp(CL_Obj *args, int n)
{
    int a, b;
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]) || !CL_CHAR_P(args[1]))
        cl_error(CL_ERR_TYPE, "CHAR-GREATERP: not a character");
    a = CL_CHAR_VAL(args[0]);
    b = CL_CHAR_VAL(args[1]);
    if (a >= 'a' && a <= 'z') a -= 32;
    if (b >= 'a' && b <= 'z') b -= 32;
    return (a > b) ? SYM_T : CL_NIL;
}

static CL_Obj bi_char_not_greaterp(CL_Obj *args, int n)
{
    int a, b;
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]) || !CL_CHAR_P(args[1]))
        cl_error(CL_ERR_TYPE, "CHAR-NOT-GREATERP: not a character");
    a = CL_CHAR_VAL(args[0]);
    b = CL_CHAR_VAL(args[1]);
    if (a >= 'a' && a <= 'z') a -= 32;
    if (b >= 'a' && b <= 'z') b -= 32;
    return (a <= b) ? SYM_T : CL_NIL;
}

static CL_Obj bi_char_not_lessp(CL_Obj *args, int n)
{
    int a, b;
    CL_UNUSED(n);
    if (!CL_CHAR_P(args[0]) || !CL_CHAR_P(args[1]))
        cl_error(CL_ERR_TYPE, "CHAR-NOT-LESSP: not a character");
    a = CL_CHAR_VAL(args[0]);
    b = CL_CHAR_VAL(args[1]);
    if (a >= 'a' && a <= 'z') a -= 32;
    if (b >= 'a' && b <= 'z') b -= 32;
    return (a >= b) ? SYM_T : CL_NIL;
}

/* --- MAKE-STRING --- */

static CL_Obj KW_INITIAL_ELEMENT;
static CL_Obj KW_ELEMENT_TYPE;

static CL_Obj bi_make_string_fn(CL_Obj *args, int n)
{
    uint32_t size;
    char fill_char = ' ';  /* default fill */
    int i;
    CL_Obj result;
    CL_String *s;

    if (!CL_FIXNUM_P(args[0]) || CL_FIXNUM_VAL(args[0]) < 0)
        cl_error(CL_ERR_TYPE, "MAKE-STRING: size must be a non-negative integer");
    size = (uint32_t)CL_FIXNUM_VAL(args[0]);

    /* Parse keyword arguments */
    for (i = 1; i + 1 < n; i += 2) {
        if (args[i] == KW_INITIAL_ELEMENT) {
            if (!CL_CHAR_P(args[i + 1]))
                cl_error(CL_ERR_TYPE, "MAKE-STRING: :initial-element must be a character");
            fill_char = (char)CL_CHAR_VAL(args[i + 1]);
        } else if (args[i] == KW_ELEMENT_TYPE) {
            /* Ignore :element-type — we only support base-char */
        }
    }

    result = cl_make_string(NULL, size);
    s = (CL_String *)CL_OBJ_TO_PTR(result);
    {
        uint32_t j;
        for (j = 0; j < size; j++)
            s->data[j] = fill_char;
    }
    return result;
}

/* --- Registration --- */

void cl_builtins_strings_init(void)
{
    /* Character functions */
    defun("CHARACTERP", bi_characterp, 1, 1);
    defun("CHAR=", bi_char_eq, 2, 2);
    defun("CHAR/=", bi_char_ne, 2, 2);
    defun("CHAR<", bi_char_lt, 2, 2);
    defun("CHAR>", bi_char_gt, 2, 2);
    defun("CHAR<=", bi_char_le, 2, 2);
    defun("CHAR>=", bi_char_ge, 2, 2);
    defun("CHAR-CODE", bi_char_code, 1, 1);
    defun("CODE-CHAR", bi_code_char, 1, 1);
    defun("CHAR-UPCASE", bi_char_upcase, 1, 1);
    defun("CHAR-DOWNCASE", bi_char_downcase, 1, 1);
    defun("UPPER-CASE-P", bi_upper_case_p, 1, 1);
    defun("LOWER-CASE-P", bi_lower_case_p, 1, 1);
    defun("ALPHA-CHAR-P", bi_alpha_char_p, 1, 1);
    defun("DIGIT-CHAR-P", bi_digit_char_p, 1, 1);

    /* Symbol functions */
    defun("SYMBOL-NAME", bi_symbol_name, 1, 1);
    defun("SYMBOL-PACKAGE", bi_symbol_package, 1, 1);
    defun("MAKE-SYMBOL", bi_make_symbol, 1, 1);
    defun("KEYWORDP", bi_keywordp, 1, 1);

    /* String functions */
    defun("STRING=", bi_string_eq, 2, 2);
    defun("STRING-EQUAL", bi_string_equal, 2, 2);
    defun("STRING<", bi_string_lt, 2, 2);
    defun("STRING>", bi_string_gt, 2, 2);
    defun("STRING<=", bi_string_le, 2, 2);
    defun("STRING>=", bi_string_ge, 2, 2);
    defun("STRING-UPCASE", bi_string_upcase, 1, 1);
    defun("STRING-DOWNCASE", bi_string_downcase, 1, 1);
    defun("STRING-TRIM", bi_string_trim, 2, 2);
    defun("STRING-LEFT-TRIM", bi_string_left_trim, 2, 2);
    defun("STRING-RIGHT-TRIM", bi_string_right_trim, 2, 2);
    defun("SUBSEQ", bi_subseq, 2, 3);
    defun("CONCATENATE", bi_concatenate, 1, -1);
    defun("CHAR", bi_char_accessor, 2, 2);
    defun("SCHAR", bi_char_accessor, 2, 2);
    defun("STRING", bi_string_coerce, 1, 1);
    defun("PARSE-INTEGER", bi_parse_integer, 1, -1);
    defun("WRITE-TO-STRING", bi_write_to_string, 1, -1);

    /* Initialize write-to-string keywords */
    KW_WTS_ESCAPE   = cl_intern_in("ESCAPE",   6, cl_package_keyword);
    KW_WTS_READABLY = cl_intern_in("READABLY", 8, cl_package_keyword);
    KW_WTS_BASE     = cl_intern_in("BASE",     4, cl_package_keyword);
    KW_WTS_RADIX    = cl_intern_in("RADIX",    5, cl_package_keyword);
    KW_WTS_LEVEL    = cl_intern_in("LEVEL",    5, cl_package_keyword);
    KW_WTS_LENGTH   = cl_intern_in("LENGTH",   6, cl_package_keyword);
    KW_WTS_CASE     = cl_intern_in("CASE",     4, cl_package_keyword);
    KW_WTS_GENSYM   = cl_intern_in("GENSYM",   6, cl_package_keyword);
    KW_WTS_ARRAY    = cl_intern_in("ARRAY",    5, cl_package_keyword);
    KW_WTS_CIRCLE   = cl_intern_in("CIRCLE",   6, cl_package_keyword);
    KW_WTS_PRETTY   = cl_intern_in("PRETTY",   6, cl_package_keyword);
    KW_WTS_RIGHT_MARGIN = cl_intern_in("RIGHT-MARGIN", 12, cl_package_keyword);
    defun("PRIN1-TO-STRING", bi_prin1_to_string_fn, 1, 1);
    defun("PRINC-TO-STRING", bi_princ_to_string_fn, 1, 1);

    /* Phase 8 Step 2 */
    defun("STRING-CAPITALIZE", bi_string_capitalize, 1, 1);
    defun("NSTRING-UPCASE", bi_nstring_upcase, 1, 1);
    defun("NSTRING-DOWNCASE", bi_nstring_downcase, 1, 1);
    defun("NSTRING-CAPITALIZE", bi_nstring_capitalize, 1, 1);
    defun("CHAR-NAME", bi_char_name, 1, 1);
    defun("NAME-CHAR", bi_name_char, 1, 1);
    defun("GRAPHIC-CHAR-P", bi_graphic_char_p, 1, 1);
    defun("STANDARD-CHAR-P", bi_standard_char_p, 1, 1);
    defun("ALPHANUMERICP", bi_alphanumericp, 1, 1);
    defun("DIGIT-CHAR", bi_digit_char, 1, 2);
    defun("BOTH-CASE-P", bi_both_case_p, 1, 1);
    defun("CHAR-EQUAL", bi_char_equal, 2, 2);
    defun("CHAR-NOT-EQUAL", bi_char_not_equal, 2, 2);
    defun("CHAR-LESSP", bi_char_lessp, 2, 2);
    defun("CHAR-GREATERP", bi_char_greaterp, 2, 2);
    defun("CHAR-NOT-GREATERP", bi_char_not_greaterp, 2, 2);
    defun("CHAR-NOT-LESSP", bi_char_not_lessp, 2, 2);
    defun("MAKE-STRING", bi_make_string_fn, 1, -1);

    KW_INITIAL_ELEMENT = cl_intern_keyword("INITIAL-ELEMENT", 15);
    KW_ELEMENT_TYPE    = cl_intern_keyword("ELEMENT-TYPE", 12);
}
