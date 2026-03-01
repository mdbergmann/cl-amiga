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

static CL_Obj bi_string_upcase(CL_Obj *args, int n)
{
    CL_String *s;
    CL_Obj result;
    CL_String *rs;
    uint32_t i;
    CL_UNUSED(n);
    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "STRING-UPCASE: not a string");
    s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
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
    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "STRING-DOWNCASE: not a string");
    s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
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
    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "STRING-TRIM: char-bag must be a string");
    if (!CL_STRING_P(args[1]))
        cl_error(CL_ERR_TYPE, "STRING-TRIM: not a string");
    set = (CL_String *)CL_OBJ_TO_PTR(args[0]);
    s = (CL_String *)CL_OBJ_TO_PTR(args[1]);
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
    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "STRING-LEFT-TRIM: char-bag must be a string");
    if (!CL_STRING_P(args[1]))
        cl_error(CL_ERR_TYPE, "STRING-LEFT-TRIM: not a string");
    set = (CL_String *)CL_OBJ_TO_PTR(args[0]);
    s = (CL_String *)CL_OBJ_TO_PTR(args[1]);
    start = 0;
    while (start < s->length && trim_char_in_set(s->data[start], set)) start++;
    return cl_make_string(s->data + start, s->length - start);
}

static CL_Obj bi_string_right_trim(CL_Obj *args, int n)
{
    CL_String *set, *s;
    uint32_t end;
    CL_UNUSED(n);
    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "STRING-RIGHT-TRIM: char-bag must be a string");
    if (!CL_STRING_P(args[1]))
        cl_error(CL_ERR_TYPE, "STRING-RIGHT-TRIM: not a string");
    set = (CL_String *)CL_OBJ_TO_PTR(args[0]);
    s = (CL_String *)CL_OBJ_TO_PTR(args[1]);
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
    /* List subseq */
    {
        CL_Obj list = args[0], result = CL_NIL, tail = CL_NIL;
        int32_t i = 0;
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
        return result;
    }
}

static CL_Obj bi_concatenate(CL_Obj *args, int n)
{
    char buf[4096];
    int pos = 0;
    int i;
    CL_UNUSED(n);
    for (i = 1; i < n; i++) {
        if (CL_STRING_P(args[i])) {
            CL_String *s = (CL_String *)CL_OBJ_TO_PTR(args[i]);
            if (pos + (int)s->length < (int)sizeof(buf)) {
                memcpy(buf + pos, s->data, s->length);
                pos += (int)s->length;
            }
        } else if (CL_CHAR_P(args[i])) {
            if (pos < (int)sizeof(buf) - 1)
                buf[pos++] = (char)CL_CHAR_VAL(args[i]);
        }
    }
    return cl_make_string(buf, (uint32_t)pos);
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

static CL_Obj bi_write_to_string(CL_Obj *args, int n)
{
    char buf[1024];
    int len;
    CL_UNUSED(n);
    len = cl_prin1_to_string(args[0], buf, sizeof(buf));
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
    defun("FBOUNDP", bi_fboundp, 1, 1);
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
    defun("WRITE-TO-STRING", bi_write_to_string, 1, 1);
    defun("PRIN1-TO-STRING", bi_prin1_to_string_fn, 1, 1);
    defun("PRINC-TO-STRING", bi_princ_to_string_fn, 1, 1);
}
