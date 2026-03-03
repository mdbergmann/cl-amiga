#include "printer.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "bignum.h"
#include "float.h"
#include "stream.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ================================================================
 * Dynamic printer variable readers
 *
 * These read *print-escape* etc. from their symbol values so that
 * LET / dynamic bindings are honored automatically.
 * Guard against early calls before cl_symbol_init() has run.
 * ================================================================ */

static int print_escape_p(void)
{
    CL_Symbol *s;
    if (CL_NULL_P(SYM_PRINT_ESCAPE)) return 1; /* before init */
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_ESCAPE);
    return !CL_NULL_P(s->value);
}

static int print_readably_p(void)
{
    CL_Symbol *s;
    if (CL_NULL_P(SYM_PRINT_READABLY)) return 0;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_READABLY);
    return !CL_NULL_P(s->value);
}

/* Returns -1 for NIL (no limit), else fixnum value */
static int32_t print_level(void)
{
    CL_Symbol *s;
    if (CL_NULL_P(SYM_PRINT_LEVEL)) return -1;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_LEVEL);
    if (CL_NULL_P(s->value)) return -1;
    if (CL_FIXNUM_P(s->value)) return CL_FIXNUM_VAL(s->value);
    return -1;
}

/* Returns -1 for NIL (no limit), else fixnum value */
static int32_t print_length(void)
{
    CL_Symbol *s;
    if (CL_NULL_P(SYM_PRINT_LENGTH)) return -1;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_LENGTH);
    if (CL_NULL_P(s->value)) return -1;
    if (CL_FIXNUM_P(s->value)) return CL_FIXNUM_VAL(s->value);
    return -1;
}

/* Returns current *print-base* (2-36), default 10 */
static int32_t print_base(void)
{
    CL_Symbol *s;
    int32_t val;
    if (CL_NULL_P(SYM_PRINT_BASE)) return 10;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_BASE);
    if (!CL_FIXNUM_P(s->value)) return 10;
    val = CL_FIXNUM_VAL(s->value);
    if (val < 2 || val > 36) return 10;
    return val;
}

static int print_radix_p(void)
{
    CL_Symbol *s;
    if (CL_NULL_P(SYM_PRINT_RADIX)) return 0;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_RADIX);
    return !CL_NULL_P(s->value);
}

static int print_gensym_p(void)
{
    CL_Symbol *s;
    if (CL_NULL_P(SYM_PRINT_GENSYM)) return 1; /* before init */
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_GENSYM);
    return !CL_NULL_P(s->value);
}

static int print_array_p(void)
{
    CL_Symbol *s;
    if (CL_NULL_P(SYM_PRINT_ARRAY)) return 1; /* before init */
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_ARRAY);
    return !CL_NULL_P(s->value);
}

/* Returns 0=UPCASE, 1=DOWNCASE, 2=CAPITALIZE */
static int print_case(void)
{
    CL_Symbol *s;
    CL_Obj val;
    if (CL_NULL_P(SYM_PRINT_CASE)) return 0;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_CASE);
    val = s->value;
    if (val == KW_DOWNCASE) return 1;
    if (val == KW_CAPITALIZE) return 2;
    return 0; /* :UPCASE or unknown */
}

/* Current nesting depth for *print-level* tracking */
static int32_t current_depth = 0;

/* ================================================================
 * Output target state (stream or C buffer)
 * ================================================================ */

/* Stream-based output target (CL_NIL when using buffer mode) */
static CL_Obj printer_stream = CL_NIL;

/* Buffer output (for cl_prin1_to_string / cl_princ_to_string) */
static int to_buffer = 0;
static char *out_buf = NULL;
static int out_pos = 0;
static int out_size = 0;

static void out_char(int ch)
{
    if (printer_stream != CL_NIL) {
        cl_stream_write_char(printer_stream, ch);
    } else if (to_buffer) {
        if (out_pos < out_size - 1)
            out_buf[out_pos++] = (char)ch;
    } else {
        char c[2] = { (char)ch, '\0' };
        platform_write_string(c);
    }
}

static void out_str(const char *s)
{
    if (printer_stream != CL_NIL) {
        cl_stream_write_string(printer_stream, s, (uint32_t)strlen(s));
    } else {
        while (*s) out_char(*s++);
    }
}

/* Base-aware integer output honoring *print-base* and *print-radix* */
static void out_integer(int32_t val, int32_t base, int radix)
{
    static const char digit_chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char buf[40]; /* enough for 32-bit binary + prefix + sign */
    int pos = 0;
    uint32_t uval;
    int negative = 0;

    /* Radix prefix */
    if (radix) {
        switch (base) {
        case 2:  out_str("#b"); break;
        case 8:  out_str("#o"); break;
        case 16: out_str("#x"); break;
        default:
            if (base != 10) {
                char rbuf[8];
                sprintf(rbuf, "#%d", (int)base);
                out_str(rbuf);
                out_char('r');
            }
            break;
        }
    }

    /* Handle sign */
    if (val < 0) {
        negative = 1;
        /* Handle INT32_MIN carefully */
        if (val == (int32_t)0x80000000) {
            uval = (uint32_t)0x80000000;
        } else {
            uval = (uint32_t)(-val);
        }
    } else {
        uval = (uint32_t)val;
    }

    /* Convert to digits in reverse */
    if (uval == 0) {
        buf[pos++] = '0';
    } else {
        while (uval > 0) {
            buf[pos++] = digit_chars[uval % (uint32_t)base];
            uval /= (uint32_t)base;
        }
    }

    /* Output sign */
    if (negative) out_char('-');

    /* Output digits in correct order */
    while (pos > 0) out_char(buf[--pos]);

    /* Radix suffix: trailing dot for base 10 */
    if (radix && base == 10) out_char('.');
}

static void print_obj(CL_Obj obj);

/*
 * Output a symbol name honoring *print-case*.
 * Per CL spec, *print-case* only affects all-uppercase names
 * (which is standard for interned CL symbols).
 * Names with lowercase or non-alpha chars are printed as-is.
 */
static void out_symbol_name(const char *name)
{
    int pcase = print_case();
    const char *p;
    int all_upper = 1;

    /* If :UPCASE (default), output as-is */
    if (pcase == 0) {
        out_str(name);
        return;
    }

    /* Check if name is all uppercase letters + non-alpha chars */
    for (p = name; *p; p++) {
        if (*p >= 'a' && *p <= 'z') {
            all_upper = 0;
            break;
        }
    }

    /* If name has lowercase chars, print as-is (mixed case, not transformed) */
    if (!all_upper) {
        out_str(name);
        return;
    }

    if (pcase == 1) {
        /* :DOWNCASE — all uppercase -> lowercase */
        for (p = name; *p; p++) {
            if (*p >= 'A' && *p <= 'Z')
                out_char(*p + ('a' - 'A'));
            else
                out_char(*p);
        }
    } else {
        /* :CAPITALIZE — first letter of each word uppercase, rest lowercase */
        int word_start = 1;
        for (p = name; *p; p++) {
            if (*p >= 'A' && *p <= 'Z') {
                if (word_start)
                    out_char(*p); /* keep uppercase */
                else
                    out_char(*p + ('a' - 'A')); /* lowercase */
                word_start = 0;
            } else {
                out_char(*p);
                /* Non-alphanumeric chars are word separators */
                word_start = !(*p >= '0' && *p <= '9');
            }
        }
    }
}

/* Check if sprintf result looks like a plain integer (all digits, no decimal/exponent) */
static int needs_decimal(const char *buf)
{
    const char *p = buf;
    if (*p == '-' || *p == '+') p++;
    while (*p) {
        if (!isdigit((unsigned char)*p)) return 0;
        p++;
    }
    return 1;
}

static void print_single_float(float value)
{
    char buf[32];
    sprintf(buf, "%g", (double)value);
    if (needs_decimal(buf))
        strcat(buf, ".0");
    out_str(buf);
}

static void print_double_float(double value)
{
    char buf[48];
    char *e;
    sprintf(buf, "%.15g", value);
    /* Replace 'e' with 'd' for double-float exponent marker */
    e = strchr(buf, 'e');
    if (!e) e = strchr(buf, 'E');
    if (e) {
        *e = 'd';
    } else if (needs_decimal(buf)) {
        strcat(buf, ".0d0");
    } else {
        strcat(buf, "d0");
    }
    out_str(buf);
}

static void print_list(CL_Obj obj)
{
    int32_t max_depth = print_level();
    int32_t max_len = print_length();
    int32_t count = 0;

    if (max_depth >= 0 && current_depth >= max_depth) {
        out_char('#');
        return;
    }
    current_depth++;

    out_char('(');
    if (max_len >= 0 && max_len == 0) {
        out_str("...");
    } else {
        print_obj(cl_car(obj));
        count++;
        obj = cl_cdr(obj);

        while (CL_CONS_P(obj)) {
            if (max_len >= 0 && count >= max_len) {
                out_str(" ...");
                obj = CL_NIL; /* skip dotted pair check */
                break;
            }
            out_char(' ');
            print_obj(cl_car(obj));
            count++;
            obj = cl_cdr(obj);
        }

        if (!CL_NULL_P(obj)) {
            out_str(" . ");
            print_obj(obj);
        }
    }

    out_char(')');
    current_depth--;
}

static void print_string(CL_Obj obj)
{
    CL_String *s = (CL_String *)CL_OBJ_TO_PTR(obj);
    if (print_escape_p() || print_readably_p()) {
        uint32_t i;
        out_char('"');
        for (i = 0; i < s->length; i++) {
            char ch = s->data[i];
            if (ch == '"' || ch == '\\') out_char('\\');
            if (ch == '\n') { out_char('\\'); out_char('n'); continue; }
            if (ch == '\t') { out_char('\\'); out_char('t'); continue; }
            out_char(ch);
        }
        out_char('"');
    } else {
        out_str(s->data);
    }
}

/* Recursive helper for multi-dim array printing.
 * Prints one dimension slice, advancing *row_major as elements are printed. */
static void print_array_slice(CL_Obj *elts, uint32_t *dims, uint8_t rank,
                              uint8_t dim, uint32_t *row_major, int32_t max_len)
{
    uint32_t i;
    out_char('(');
    for (i = 0; i < dims[dim]; i++) {
        if (max_len >= 0 && (int32_t)i >= max_len) {
            /* Skip remaining elements/slices */
            uint32_t skip = dims[dim] - i;
            uint8_t d;
            uint32_t slice_size = 1;
            for (d = dim + 1; d < rank; d++)
                slice_size *= dims[d];
            *row_major += skip * slice_size;
            out_str("...");
            break;
        }
        if (i > 0) out_char(' ');
        if (dim == rank - 1) {
            print_obj(elts[*row_major]);
            (*row_major)++;
        } else {
            print_array_slice(elts, dims, rank, dim + 1, row_major, max_len);
        }
    }
    out_char(')');
}

static void print_obj(CL_Obj obj)
{
    if (CL_NULL_P(obj)) {
        out_symbol_name("NIL");
        return;
    }

    if (CL_FIXNUM_P(obj)) {
        out_integer(CL_FIXNUM_VAL(obj), print_base(), print_radix_p());
        return;
    }

    if (CL_CHAR_P(obj)) {
        int ch = CL_CHAR_VAL(obj);
        if (print_escape_p() || print_readably_p()) {
            out_str("#\\");
            switch (ch) {
            case ' ':  out_str("Space"); break;
            case '\n': out_str("Newline"); break;
            case '\t': out_str("Tab"); break;
            case '\r': out_str("Return"); break;
            case '\b': out_str("Backspace"); break;
            case '\f': out_str("Page"); break;
            case 0x7F: out_str("Rubout"); break;
            default:   out_char(ch); break;
            }
        } else {
            out_char(ch);
        }
        return;
    }

    if (!CL_HEAP_P(obj)) {
        out_str("#<unknown>");
        return;
    }

    if (CL_BIGNUM_P(obj)) {
        int32_t base = print_base();
        int radix = print_radix_p();
        if (radix) {
            switch (base) {
            case 2:  out_str("#b"); break;
            case 8:  out_str("#o"); break;
            case 16: out_str("#x"); break;
            default:
                if (base != 10) {
                    char rbuf[8];
                    sprintf(rbuf, "#%d", (int)base);
                    out_str(rbuf);
                    out_char('r');
                }
                break;
            }
        }
        cl_bignum_print_base(obj, base, out_str);
        if (radix && base == 10) out_char('.');
        return;
    }

    if (CL_SINGLE_FLOAT_P(obj)) {
        print_single_float(((CL_SingleFloat *)CL_OBJ_TO_PTR(obj))->value);
        return;
    }

    if (CL_DOUBLE_FLOAT_P(obj)) {
        print_double_float(((CL_DoubleFloat *)CL_OBJ_TO_PTR(obj))->value);
        return;
    }

    switch (CL_HDR_TYPE(CL_OBJ_TO_PTR(obj))) {
    case TYPE_CONS:
        print_list(obj);
        break;

    case TYPE_SYMBOL: {
        CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(obj);
        if (CL_NULL_P(sym->package)) {
            /* Uninterned symbol — #: prefix only if *print-gensym* */
            if (print_gensym_p())
                out_str("#:");
            out_symbol_name(cl_symbol_name(obj));
        } else if (sym->package == cl_package_keyword) {
            /* Keyword */
            out_char(':');
            out_symbol_name(cl_symbol_name(obj));
        } else if (sym->package == cl_current_package ||
                   cl_package_find_symbol(cl_symbol_name(obj),
                       ((CL_String *)CL_OBJ_TO_PTR(sym->name))->length,
                       cl_current_package) == obj) {
            /* Accessible in current package — no prefix */
            out_symbol_name(cl_symbol_name(obj));
        } else {
            /* Symbol from another package */
            CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(sym->package);
            CL_String *pkg_name = (CL_String *)CL_OBJ_TO_PTR(pkg->name);
            out_symbol_name(pkg_name->data);
            if (sym->flags & CL_SYM_EXPORTED) {
                out_char(':');
            } else {
                out_str("::");
            }
            out_symbol_name(cl_symbol_name(obj));
        }
        break;
    }

    case TYPE_STRING:
        print_string(obj);
        break;

    case TYPE_FUNCTION: {
        CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(obj);
        out_str("#<FUNCTION ");
        if (!CL_NULL_P(f->name))
            out_str(cl_symbol_name(f->name));
        else
            out_str("anonymous");
        out_char('>');
        break;
    }

    case TYPE_CLOSURE: {
        CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(obj);
        CL_Bytecode *bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode);
        out_str("#<CLOSURE ");
        if (!CL_NULL_P(bc->name))
            out_str(cl_symbol_name(bc->name));
        else
            out_str("anonymous");
        out_char('>');
        break;
    }

    case TYPE_BYTECODE: {
        CL_Bytecode *bc = (CL_Bytecode *)CL_OBJ_TO_PTR(obj);
        out_str("#<COMPILED-FUNCTION ");
        if (!CL_NULL_P(bc->name))
            out_str(cl_symbol_name(bc->name));
        else
            out_str("anonymous");
        out_char('>');
        break;
    }

    case TYPE_VECTOR: {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
        int32_t max_depth = print_level();
        int32_t max_len = print_length();

        if (!print_array_p()) {
            if (v->rank > 1)
                out_str("#<ARRAY>");
            else
                out_str("#<VECTOR>");
            break;
        }

        if (max_depth >= 0 && current_depth >= max_depth) {
            out_char('#');
            break;
        }

        if (v->rank > 1) {
            /* Multi-dimensional: #nA(...) with nested lists */
            char rank_buf[12];
            uint32_t dims[16];
            uint8_t rank = v->rank;
            uint8_t d;
            uint32_t rm = 0;
            snprintf(rank_buf, sizeof(rank_buf), "#%dA", (int)rank);
            out_str(rank_buf);
            for (d = 0; d < rank; d++)
                dims[d] = (uint32_t)CL_FIXNUM_VAL(v->data[d]);
            current_depth++;
            print_array_slice(cl_vector_data(v), dims, rank, 0, &rm, max_len);
            current_depth--;
        } else {
            /* 1D vector: #(...) */
            uint32_t i;
            uint32_t vec_len = cl_vector_active_length(v);
            CL_Obj *elts = cl_vector_data(v);
            current_depth++;
            out_str("#(");
            for (i = 0; i < vec_len; i++) {
                if (max_len >= 0 && (int32_t)i >= max_len) {
                    out_str("...");
                    break;
                }
                if (i > 0) out_char(' ');
                print_obj(elts[i]);
            }
            out_char(')');
            current_depth--;
        }
        break;
    }

    case TYPE_PACKAGE: {
        CL_Package *p = (CL_Package *)CL_OBJ_TO_PTR(obj);
        CL_String *name = (CL_String *)CL_OBJ_TO_PTR(p->name);
        out_str("#<PACKAGE ");
        out_str(name->data);
        out_char('>');
        break;
    }

    case TYPE_HASHTABLE: {
        CL_Hashtable *ht = (CL_Hashtable *)CL_OBJ_TO_PTR(obj);
        char buf[16];
        out_str("#<HASH-TABLE :TEST ");
        switch (ht->test) {
        case CL_HT_TEST_EQ:    out_str("EQ"); break;
        case CL_HT_TEST_EQL:   out_str("EQL"); break;
        case CL_HT_TEST_EQUAL: out_str("EQUAL"); break;
        default:                out_str("?"); break;
        }
        out_str(" :COUNT ");
        sprintf(buf, "%d", (int)ht->count);
        out_str(buf);
        out_char('>');
        break;
    }

    case TYPE_STRUCT: {
        CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
        uint32_t i;
        int32_t max_depth = print_level();
        extern CL_Obj cl_struct_slot_names(CL_Obj type_name);
        CL_Obj slot_names = cl_struct_slot_names(st->type_desc);

        if (max_depth >= 0 && current_depth >= max_depth) {
            out_char('#');
            break;
        }
        current_depth++;
        out_str("#S(");
        if (!CL_NULL_P(st->type_desc))
            out_str(cl_symbol_name(st->type_desc));
        for (i = 0; i < st->n_slots; i++) {
            out_char(' ');
            if (!CL_NULL_P(slot_names)) {
                out_char(':');
                out_str(cl_symbol_name(cl_car(slot_names)));
                slot_names = cl_cdr(slot_names);
            }
            out_char(' ');
            print_obj(st->slots[i]);
        }
        out_char(')');
        current_depth--;
        break;
    }

    case TYPE_CONDITION: {
        CL_Condition *cond = (CL_Condition *)CL_OBJ_TO_PTR(obj);
        out_str("#<CONDITION ");
        if (!CL_NULL_P(cond->type_name))
            out_str(cl_symbol_name(cond->type_name));
        else
            out_str("CONDITION");
        if (!CL_NULL_P(cond->report_string)) {
            CL_String *rs = (CL_String *)CL_OBJ_TO_PTR(cond->report_string);
            out_str(": \"");
            out_str(rs->data);
            out_char('"');
        }
        out_char('>');
        break;
    }

    case TYPE_STREAM: {
        CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(obj);
        out_str("#<");
        switch (st->stream_type) {
        case CL_STREAM_CONSOLE: out_str("CONSOLE-"); break;
        case CL_STREAM_FILE:    out_str("FILE-"); break;
        case CL_STREAM_STRING:  out_str("STRING-"); break;
        default:                out_str(""); break;
        }
        switch (st->direction) {
        case CL_STREAM_INPUT:  out_str("INPUT-STREAM"); break;
        case CL_STREAM_OUTPUT: out_str("OUTPUT-STREAM"); break;
        case CL_STREAM_IO:     out_str("IO-STREAM"); break;
        default:               out_str("STREAM"); break;
        }
        if (!(st->flags & CL_STREAM_FLAG_OPEN))
            out_str(" (closed)");
        out_char('>');
        break;
    }

    default:
        out_str("#<unknown>");
        break;
    }
}

/* ================================================================
 * Public API — stream-based
 * ================================================================ */

void cl_write_to_stream(CL_Obj obj, CL_Obj stream)
{
    CL_Obj prev = printer_stream;
    int32_t prev_depth = current_depth;
    current_depth = 0;
    to_buffer = 0;
    printer_stream = stream;
    print_obj(obj);
    printer_stream = prev;
    current_depth = prev_depth;
}

void cl_prin1_to_stream(CL_Obj obj, CL_Obj stream)
{
    CL_Symbol *se;
    CL_Obj prev_e;
    if (CL_NULL_P(SYM_PRINT_ESCAPE)) {
        /* Before init — fall through directly */
        CL_Obj prev = printer_stream;
        to_buffer = 0;
        printer_stream = stream;
        print_obj(obj);
        printer_stream = prev;
        return;
    }
    se = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_ESCAPE);
    prev_e = se->value;
    se->value = CL_T;
    cl_write_to_stream(obj, stream);
    se->value = prev_e;
}

void cl_princ_to_stream(CL_Obj obj, CL_Obj stream)
{
    CL_Symbol *se, *sr;
    CL_Obj prev_e, prev_r;
    if (CL_NULL_P(SYM_PRINT_ESCAPE)) {
        CL_Obj prev = printer_stream;
        to_buffer = 0;
        printer_stream = stream;
        print_obj(obj);
        printer_stream = prev;
        return;
    }
    se = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_ESCAPE);
    sr = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_READABLY);
    prev_e = se->value;
    prev_r = sr->value;
    se->value = CL_NIL;
    sr->value = CL_NIL;
    cl_write_to_stream(obj, stream);
    se->value = prev_e;
    sr->value = prev_r;
}

void cl_print_to_stream(CL_Obj obj, CL_Obj stream)
{
    cl_stream_write_char(stream, '\n');
    cl_prin1_to_stream(obj, stream);
    cl_stream_write_char(stream, ' ');
}

/* Convenience wrappers — print to *standard-output* */

void cl_prin1(CL_Obj obj)
{
    CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STANDARD_OUTPUT);
    cl_prin1_to_stream(obj, sym->value);
}

void cl_princ(CL_Obj obj)
{
    CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STANDARD_OUTPUT);
    cl_princ_to_stream(obj, sym->value);
}

void cl_print(CL_Obj obj)
{
    CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STANDARD_OUTPUT);
    cl_print_to_stream(obj, sym->value);
}

/* ================================================================
 * C-internal: print to a C buffer (for vm.c, debugger.c, etc.)
 * ================================================================ */

static int write_to_buffer_internal(CL_Obj obj, char *buf, int bufsize)
{
    CL_Obj prev = printer_stream;
    int32_t prev_depth = current_depth;
    current_depth = 0;
    to_buffer = 1;
    printer_stream = CL_NIL;
    out_buf = buf;
    out_pos = 0;
    out_size = bufsize;
    print_obj(obj);
    if (out_pos < out_size) out_buf[out_pos] = '\0';
    to_buffer = 0;
    printer_stream = prev;
    current_depth = prev_depth;
    return out_pos;
}

int cl_prin1_to_string(CL_Obj obj, char *buf, int bufsize)
{
    CL_Symbol *se;
    CL_Obj prev_e;
    int result;
    if (CL_NULL_P(SYM_PRINT_ESCAPE)) {
        /* Before init */
        return write_to_buffer_internal(obj, buf, bufsize);
    }
    se = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_ESCAPE);
    prev_e = se->value;
    se->value = CL_T;
    result = write_to_buffer_internal(obj, buf, bufsize);
    se->value = prev_e;
    return result;
}

int cl_princ_to_string(CL_Obj obj, char *buf, int bufsize)
{
    CL_Symbol *se, *sr;
    CL_Obj prev_e, prev_r;
    int result;
    if (CL_NULL_P(SYM_PRINT_ESCAPE)) {
        return write_to_buffer_internal(obj, buf, bufsize);
    }
    se = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_ESCAPE);
    sr = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_READABLY);
    prev_e = se->value;
    prev_r = sr->value;
    se->value = CL_NIL;
    sr->value = CL_NIL;
    result = write_to_buffer_internal(obj, buf, bufsize);
    se->value = prev_e;
    sr->value = prev_r;
    return result;
}

void cl_printer_init(void)
{
    /* Nothing needed yet */
}
