#include "printer.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "bignum.h"
#include "ratio.h"
#include "float.h"
#include "stream.h"
#include "vm.h"
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

/* Pretty-printing: column tracking and indentation stack */
static int32_t current_column = 0;

#define PP_INDENT_MAX 32
static int32_t pp_indent_stack[PP_INDENT_MAX];
static int32_t pp_indent_top = 0;

/* Forward declaration — out_char is defined after the circle detection section */
static void out_char(int ch);

static void pp_newline_indent(void)
{
    int32_t indent = (pp_indent_top > 0) ? pp_indent_stack[pp_indent_top - 1] : 0;
    int32_t i;
    out_char('\n');
    for (i = 0; i < indent; i++) out_char(' ');
}

/* ================================================================
 * *print-circle* support — two-pass printing with static hash table
 *
 * Pass 1: iterative DFS pre-walk counts visits per compound object.
 * Pass 2: objects seen >1 time get #n= (definition) / #n# (back-ref).
 * ================================================================ */

static int print_circle_p(void)
{
    CL_Symbol *s;
    if (CL_NULL_P(SYM_PRINT_CIRCLE)) return 0;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_CIRCLE);
    return !CL_NULL_P(s->value);
}

static int print_pretty_p(void)
{
    CL_Symbol *s;
    if (CL_NULL_P(SYM_PRINT_PRETTY)) return 0;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_PRETTY);
    return !CL_NULL_P(s->value);
}

/* Returns right margin (default 72 when NIL), or fixnum value */
static int32_t print_right_margin(void)
{
    CL_Symbol *s;
    if (CL_NULL_P(SYM_PRINT_RIGHT_MARGIN)) return 72;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_RIGHT_MARGIN);
    if (CL_NULL_P(s->value)) return 72;
    if (CL_FIXNUM_P(s->value)) return CL_FIXNUM_VAL(s->value);
    return 72;
}

/*
 * Open-addressing hash table for circle detection.
 * Keys are CL_Obj values (heap offsets); CL_NIL (0) = empty slot.
 * Values encode state:
 *   0 = seen once (not shared)
 *   1 = seen more than once (shared, no label yet)
 *   >=2 = assigned label (label = val - 2)
 *   <0 = already printed (negated label - 2)
 */
#define CIRCLE_HT_SIZE 256
static CL_Obj  circle_keys[CIRCLE_HT_SIZE];
static int32_t circle_vals[CIRCLE_HT_SIZE];
static int circle_count;
static int circle_next_label;
static int circle_active;

static void circle_clear(void)
{
    int i;
    for (i = 0; i < CIRCLE_HT_SIZE; i++) {
        circle_keys[i] = CL_NIL;
        circle_vals[i] = 0;
    }
    circle_count = 0;
    circle_next_label = 0;
    circle_active = 0;
}

static uint32_t circle_hash(CL_Obj obj)
{
    /* Simple hash for arena offsets: multiply and shift */
    uint32_t h = obj;
    h ^= h >> 16;
    h *= 0x45d9f3b;
    h ^= h >> 16;
    return h & (CIRCLE_HT_SIZE - 1);
}

/*
 * Find slot for obj. Returns index into circle_keys/circle_vals.
 * If obj is not in the table, returns the first empty slot.
 */
static int circle_find(CL_Obj obj)
{
    uint32_t idx = circle_hash(obj);
    int i;
    for (i = 0; i < CIRCLE_HT_SIZE; i++) {
        uint32_t slot = (idx + (uint32_t)i) & (CIRCLE_HT_SIZE - 1);
        if (circle_keys[slot] == obj) return (int)slot;
        if (circle_keys[slot] == CL_NIL) return (int)slot;
    }
    return (int)idx; /* table full — shouldn't happen */
}

/* Is this a compound heap object that can contain references? */
static int circle_compound_p(CL_Obj obj)
{
    int type;
    if (!CL_HEAP_P(obj)) return 0;
    type = (int)CL_HDR_TYPE(CL_OBJ_TO_PTR(obj));
    return type == TYPE_CONS || type == TYPE_VECTOR || type == TYPE_STRUCT;
}

/*
 * Iterative DFS pre-walk. Marks each compound object as seen-once (0)
 * or seen-more-than-once (1). Stops recursion on already-seen objects.
 */
static void circle_walk(CL_Obj root)
{
    CL_Obj stack[512];
    int sp = 0;
    int slot;

    if (!circle_compound_p(root)) return;
    stack[sp++] = root;

    while (sp > 0) {
        CL_Obj obj = stack[--sp];
        int type;

        if (!circle_compound_p(obj)) continue;

        slot = circle_find(obj);
        if (circle_keys[slot] == obj) {
            /* Already seen — mark as shared */
            if (circle_vals[slot] == 0)
                circle_vals[slot] = 1;
            continue; /* don't recurse again */
        }

        /* First time seeing this object */
        if (circle_count >= CIRCLE_HT_SIZE * 3 / 4) continue; /* table too full */
        circle_keys[slot] = obj;
        circle_vals[slot] = 0;
        circle_count++;

        type = (int)CL_HDR_TYPE(CL_OBJ_TO_PTR(obj));
        if (type == TYPE_CONS) {
            CL_Obj cdr_val = cl_cdr(obj);
            CL_Obj car_val = cl_car(obj);
            /* Push cdr first so car is processed first (stack is LIFO) */
            if (circle_compound_p(cdr_val) && sp < 511)
                stack[sp++] = cdr_val;
            if (circle_compound_p(car_val) && sp < 511)
                stack[sp++] = car_val;
        } else if (type == TYPE_VECTOR) {
            CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
            CL_Obj *elts = cl_vector_data(v);
            uint32_t len = cl_vector_active_length(v);
            uint32_t i;
            for (i = 0; i < len && sp < 511; i++) {
                if (circle_compound_p(elts[i]))
                    stack[sp++] = elts[i];
            }
        } else if (type == TYPE_STRUCT) {
            CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
            uint32_t i;
            for (i = 0; i < st->n_slots && sp < 511; i++) {
                if (circle_compound_p(st->slots[i]))
                    stack[sp++] = st->slots[i];
            }
        }
    }
}

/*
 * Assign labels 0,1,2,... to objects seen more than once.
 * Stored as label+2 in circle_vals to distinguish from seen-once(0)
 * and shared-no-label(1).
 */
static void circle_assign_labels(void)
{
    int i;
    circle_next_label = 0;
    for (i = 0; i < CIRCLE_HT_SIZE; i++) {
        if (circle_keys[i] != CL_NIL && circle_vals[i] == 1) {
            circle_vals[i] = circle_next_label + 2;
            circle_next_label++;
        }
    }
}

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
    if (ch == '\n') current_column = 0; else current_column++;
}

static void out_str(const char *s)
{
    if (printer_stream != CL_NIL) {
        const char *p;
        for (p = s; *p; p++) {
            if (*p == '\n') current_column = 0; else current_column++;
        }
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

/* Output #n= or #n# label */
static void out_circle_label(int label, int definition)
{
    char buf[16];
    sprintf(buf, "#%d%c", label, definition ? '=' : '#');
    out_str(buf);
}

/*
 * Check circle table for obj. Returns:
 *   0 = not shared, print normally
 *   1 = first time printing shared obj (emitted #n=, continue printing)
 *  -1 = already printed (emitted #n#, caller should return immediately)
 */
static int circle_check(CL_Obj obj)
{
    int slot;
    int32_t val;

    if (!circle_active) return 0;
    if (!circle_compound_p(obj)) return 0;

    slot = circle_find(obj);
    if (circle_keys[slot] != obj) return 0; /* not in table */

    val = circle_vals[slot];
    if (val == 0) return 0; /* seen once, not shared */

    if (val < 0) {
        /* Already printed — emit back-reference */
        out_circle_label((-val) - 2, 0);
        return -1;
    }

    if (val >= 2) {
        /* First print of shared object — emit definition label */
        int label = val - 2;
        out_circle_label(label, 1);
        circle_vals[slot] = -(val); /* mark as printed */
        return 1;
    }

    /* val == 1: shared but no label assigned (shouldn't happen after assign) */
    return 0;
}

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
    int pretty = print_pretty_p();
    int32_t margin = 0;

    if (max_depth >= 0 && current_depth >= max_depth) {
        out_char('#');
        return;
    }
    current_depth++;

    out_char('(');

    if (pretty) {
        margin = print_right_margin();
        if (pp_indent_top < PP_INDENT_MAX)
            pp_indent_stack[pp_indent_top++] = current_column;
    }

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
            /* CDR circle check: if this cdr cons is shared/circular,
             * print it as ". <obj>" so print_obj handles #n= / #n# */
            if (circle_active && circle_compound_p(obj)) {
                int slot = circle_find(obj);
                if (circle_keys[slot] == obj && circle_vals[slot] != 0) {
                    out_str(" . ");
                    print_obj(obj);
                    obj = CL_NIL; /* suppress trailing dotted pair check */
                    break;
                }
            }
            if (pretty && current_column >= margin) {
                pp_newline_indent();
            } else {
                out_char(' ');
            }
            print_obj(cl_car(obj));
            count++;
            obj = cl_cdr(obj);
        }

        if (!CL_NULL_P(obj)) {
            out_str(" . ");
            print_obj(obj);
        }
    }

    if (pretty && pp_indent_top > 0)
        pp_indent_top--;

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

/*
 * Check *print-pprint-dispatch* for a custom printer function.
 * Returns 1 if handled (dispatched), 0 if not.
 */
static int pprint_dispatch_active = 0; /* recursion guard */

static int try_pprint_dispatch(CL_Obj obj)
{
    CL_Symbol *sd;
    CL_Obj table, cur, best_fn;
    int32_t best_priority;

    if (pprint_dispatch_active) return 0; /* prevent recursion */
    if (CL_NULL_P(SYM_PRINT_PPRINT_DISPATCH)) return 0;
    sd = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_PPRINT_DISPATCH);
    table = sd->value;
    if (CL_NULL_P(table)) return 0;
    if (!print_pretty_p()) return 0;

    /* Linear scan for best matching entry */
    best_fn = CL_NIL;
    best_priority = -999999;
    cur = table;
    while (!CL_NULL_P(cur)) {
        CL_Obj entry = cl_car(cur);
        CL_Obj type_spec = cl_car(entry);
        CL_Obj prio_fn = cl_cdr(entry);
        int32_t prio = CL_FIXNUM_P(cl_car(prio_fn)) ? CL_FIXNUM_VAL(cl_car(prio_fn)) : 0;
        CL_Obj fn = cl_cdr(prio_fn);

        if (cl_typep(obj, type_spec) && prio > best_priority) {
            best_priority = prio;
            best_fn = fn;
        }
        cur = cl_cdr(cur);
    }

    if (CL_NULL_P(best_fn)) return 0;

    /* Call the dispatch function: (fn stream obj) */
    {
        CL_Obj stream = printer_stream;
        CL_Obj call_args[2];
        call_args[0] = stream;
        call_args[1] = obj;
        pprint_dispatch_active = 1;
        cl_vm_apply(best_fn, call_args, 2);
        pprint_dispatch_active = 0;
    }
    return 1;
}

static void print_obj(CL_Obj obj)
{
    /* Circle check: emit #n= or #n# for shared/circular objects */
    {
        int cc = circle_check(obj);
        if (cc < 0) return; /* already printed — #n# emitted */
        /* cc == 1: #n= emitted, fall through to print contents */
    }

    /* Check pprint dispatch table */
    if (try_pprint_dispatch(obj)) return;

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

    if (CL_RATIO_P(obj)) {
        CL_Ratio *r = (CL_Ratio *)CL_OBJ_TO_PTR(obj);
        int32_t base = print_base();
        int radix = print_radix_p();
        /* Radix prefix (same as bignum/fixnum but NO trailing dot) */
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
        /* Print numerator */
        if (CL_FIXNUM_P(r->numerator))
            out_integer(CL_FIXNUM_VAL(r->numerator), base, 0);
        else
            cl_bignum_print_base(r->numerator, base, out_str);
        out_char('/');
        /* Print denominator */
        if (CL_FIXNUM_P(r->denominator))
            out_integer(CL_FIXNUM_VAL(r->denominator), base, 0);
        else
            cl_bignum_print_base(r->denominator, base, out_str);
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
            int pretty = print_pretty_p();
            int32_t margin = pretty ? print_right_margin() : 0;
            current_depth++;
            out_str("#(");
            if (pretty && pp_indent_top < PP_INDENT_MAX)
                pp_indent_stack[pp_indent_top++] = current_column;
            for (i = 0; i < vec_len; i++) {
                if (max_len >= 0 && (int32_t)i >= max_len) {
                    out_str("...");
                    break;
                }
                if (i > 0) {
                    if (pretty && current_column >= margin)
                        pp_newline_indent();
                    else
                        out_char(' ');
                }
                print_obj(elts[i]);
            }
            if (pretty && pp_indent_top > 0)
                pp_indent_top--;
            out_char(')');
            current_depth--;
        }
        break;
    }

    case TYPE_BIT_VECTOR: {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(obj);
        uint32_t bvi, bvlen;
        if (!print_array_p()) {
            out_str("#<BIT-VECTOR>");
            break;
        }
        bvlen = cl_bv_active_length(bv);
        out_str("#*");
        for (bvi = 0; bvi < bvlen; bvi++)
            out_char(cl_bv_get_bit(bv, bvi) ? '1' : '0');
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
        case CL_HT_TEST_EQ:     out_str("EQ"); break;
        case CL_HT_TEST_EQL:    out_str("EQL"); break;
        case CL_HT_TEST_EQUAL:  out_str("EQUAL"); break;
        case CL_HT_TEST_EQUALP: out_str("EQUALP"); break;
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
        int pretty = print_pretty_p();
        int32_t margin = pretty ? print_right_margin() : 0;
        extern CL_Obj cl_struct_slot_names(CL_Obj type_name);
        CL_Obj slot_names;

        /* *print-object-hook*: if set, call it for custom struct printing.
         * Hook takes (object) and returns a string to output, or NIL
         * to fall through to default printing. */
        if (!CL_NULL_P(SYM_PRINT_OBJECT_HOOK)) {
            CL_Symbol *hook_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_OBJECT_HOOK);
            if (!CL_NULL_P(hook_sym->value)) {
                CL_Obj hook_args[1];
                CL_Obj result;
                hook_args[0] = obj;
                result = cl_vm_apply(hook_sym->value, hook_args, 1);
                if (!CL_NULL_P(result) && CL_HEAP_P(result) &&
                    CL_HDR_TYPE(CL_OBJ_TO_PTR(result)) == TYPE_STRING) {
                    CL_String *rs = (CL_String *)CL_OBJ_TO_PTR(result);
                    out_str(rs->data);
                    break; /* hook handled it */
                }
            }
        }

        slot_names = cl_struct_slot_names(st->type_desc);

        if (max_depth >= 0 && current_depth >= max_depth) {
            out_char('#');
            break;
        }
        current_depth++;
        out_str("#S(");
        if (pretty && pp_indent_top < PP_INDENT_MAX)
            pp_indent_stack[pp_indent_top++] = current_column;
        if (!CL_NULL_P(st->type_desc))
            out_str(cl_symbol_name(st->type_desc));
        for (i = 0; i < st->n_slots; i++) {
            if (pretty && current_column >= margin)
                pp_newline_indent();
            else
                out_char(' ');
            if (!CL_NULL_P(slot_names)) {
                out_char(':');
                out_str(cl_symbol_name(cl_car(slot_names)));
                slot_names = cl_cdr(slot_names);
            }
            out_char(' ');
            print_obj(st->slots[i]);
        }
        if (pretty && pp_indent_top > 0)
            pp_indent_top--;
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

    case TYPE_PATHNAME: {
        CL_Pathname *pn = (CL_Pathname *)CL_OBJ_TO_PTR(obj);
        char ns_buf[1024];
        extern uint32_t cl_pathname_to_namestring(CL_Pathname *pn, char *buf, uint32_t bufsz);
        cl_pathname_to_namestring(pn, ns_buf, sizeof(ns_buf));
        out_str("#P\"");
        out_str(ns_buf);
        out_char('"');
        break;
    }

    case TYPE_CELL: {
        CL_Cell *cell = (CL_Cell *)CL_OBJ_TO_PTR(obj);
        out_str("#<CELL ");
        print_obj(cell->value);
        out_char('>');
        break;
    }

    case TYPE_RANDOM_STATE:
        out_str("#<RANDOM-STATE>");
        break;

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
    int32_t prev_column = current_column;
    int32_t prev_indent_top = pp_indent_top;
    int prev_circle_active = circle_active;
    current_depth = 0;
    current_column = 0;
    pp_indent_top = 0;
    to_buffer = 0;
    printer_stream = stream;

    /* Activate *print-circle* if enabled and not already active (re-entrant safe) */
    if (print_circle_p() && !circle_active) {
        circle_clear();
        circle_walk(obj);
        circle_assign_labels();
        circle_active = 1;
    }

    print_obj(obj);

    if (!prev_circle_active && circle_active) {
        circle_active = 0;
    }

    printer_stream = prev;
    current_depth = prev_depth;
    current_column = prev_column;
    pp_indent_top = prev_indent_top;
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
    int32_t prev_column = current_column;
    int32_t prev_indent_top = pp_indent_top;
    int prev_circle_active = circle_active;
    current_depth = 0;
    current_column = 0;
    pp_indent_top = 0;
    to_buffer = 1;
    printer_stream = CL_NIL;
    out_buf = buf;
    out_pos = 0;
    out_size = bufsize;

    /* Activate *print-circle* if enabled and not already active */
    if (print_circle_p() && !circle_active) {
        circle_clear();
        circle_walk(obj);
        circle_assign_labels();
        circle_active = 1;
    }

    print_obj(obj);

    if (!prev_circle_active && circle_active) {
        circle_active = 0;
    }

    if (out_pos < out_size) out_buf[out_pos] = '\0';
    to_buffer = 0;
    printer_stream = prev;
    current_depth = prev_depth;
    current_column = prev_column;
    pp_indent_top = prev_indent_top;
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

/* ================================================================
 * Pretty-printing public API
 * ================================================================ */

/* Block start column stack (parallel to pp_indent_stack) */
static int32_t pp_block_start[PP_INDENT_MAX];

int32_t cl_pp_get_column(void)
{
    return current_column;
}

int32_t cl_pp_get_right_margin(void)
{
    return print_right_margin();
}

void cl_pp_newline_indent(void)
{
    pp_newline_indent();
}

void cl_pp_set_indent(int32_t n)
{
    if (pp_indent_top > 0)
        pp_indent_stack[pp_indent_top - 1] = n;
}

void cl_pp_push_block(int32_t start_col)
{
    if (pp_indent_top < PP_INDENT_MAX) {
        pp_block_start[pp_indent_top] = start_col;
        pp_indent_stack[pp_indent_top] = start_col;
        pp_indent_top++;
    }
}

void cl_pp_pop_block(void)
{
    if (pp_indent_top > 0)
        pp_indent_top--;
}

void cl_printer_init(void)
{
    /* Nothing needed yet */
}
