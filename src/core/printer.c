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
#include "string_utils.h"
#include <stdio.h>
#include <string.h>
#include "ascii_ctype.h"

/* ================================================================
 * Dynamic printer variable readers
 *
 * These read *print-escape* etc. from their symbol values so that
 * LET / dynamic bindings are honored automatically.
 * Guard against early calls before cl_symbol_init() has run.
 * ================================================================ */

static int print_escape_p(void)
{
    if (CL_NULL_P(SYM_PRINT_ESCAPE)) return 1; /* before init */
    return !CL_NULL_P(cl_symbol_value(SYM_PRINT_ESCAPE));
}

static int print_readably_p(void)
{
    if (CL_NULL_P(SYM_PRINT_READABLY)) return 0;
    return !CL_NULL_P(cl_symbol_value(SYM_PRINT_READABLY));
}

/* Returns -1 for NIL (no limit), else fixnum value */
static int32_t print_level(void)
{
    CL_Obj val;
    if (CL_NULL_P(SYM_PRINT_LEVEL)) return -1;
    val = cl_symbol_value(SYM_PRINT_LEVEL);
    if (CL_NULL_P(val)) return -1;
    if (CL_FIXNUM_P(val)) return CL_FIXNUM_VAL(val);
    return -1;
}

/* Returns -1 for NIL (no limit), else fixnum value */
static int32_t print_length(void)
{
    CL_Obj val;
    if (CL_NULL_P(SYM_PRINT_LENGTH)) return -1;
    val = cl_symbol_value(SYM_PRINT_LENGTH);
    if (CL_NULL_P(val)) return -1;
    if (CL_FIXNUM_P(val)) return CL_FIXNUM_VAL(val);
    return -1;
}

/* Returns current *print-base* (2-36), default 10 */
static int32_t print_base(void)
{
    CL_Obj v;
    int32_t val;
    if (CL_NULL_P(SYM_PRINT_BASE)) return 10;
    v = cl_symbol_value(SYM_PRINT_BASE);
    if (!CL_FIXNUM_P(v)) return 10;
    val = CL_FIXNUM_VAL(v);
    if (val < 2 || val > 36) return 10;
    return val;
}

static int print_radix_p(void)
{
    if (CL_NULL_P(SYM_PRINT_RADIX)) return 0;
    return !CL_NULL_P(cl_symbol_value(SYM_PRINT_RADIX));
}

static int print_gensym_p(void)
{
    if (CL_NULL_P(SYM_PRINT_GENSYM)) return 1; /* before init */
    return !CL_NULL_P(cl_symbol_value(SYM_PRINT_GENSYM));
}

static int print_array_p(void)
{
    if (CL_NULL_P(SYM_PRINT_ARRAY)) return 1; /* before init */
    return !CL_NULL_P(cl_symbol_value(SYM_PRINT_ARRAY));
}

/* Returns 0=UPCASE, 1=DOWNCASE, 2=CAPITALIZE */
static int print_case(void)
{
    CL_Obj val;
    if (CL_NULL_P(SYM_PRINT_CASE)) return 0;
    val = cl_symbol_value(SYM_PRINT_CASE);
    if (val == KW_DOWNCASE) return 1;
    if (val == KW_CAPITALIZE) return 2;
    return 0; /* :UPCASE or unknown */
}

/* Printer state now lives in CL_Thread.  Local macros redirect old names. */
#define current_depth   (CT->pr_depth)
#define current_column  (CT->pr_column)
#define PP_INDENT_MAX   CL_PP_INDENT_MAX
#define pp_indent_stack (CT->pr_indent_stack)
#define pp_indent_top   (CT->pr_indent_top)
#define pr_inprog       (CT->pr_inprog)
#define pr_inprog_top   (CT->pr_inprog_top)

/* NLX snapshot/restore of the leak-prone printer flags — see printer.h.
 * pr_inprog_top / current_depth are the local macros above (the bare
 * CT->pr_inprog_top spelling would be re-expanded by its own macro). */
CL_PrinterState cl_printer_state_save(void)
{
    CL_PrinterState s;
    s.depth = current_depth;
    s.inprog_top = pr_inprog_top;
    s.dispatch_active = CT->pr_pprint_dispatch_active;
    s.circle_active = CT->pr_circle_active;
    return s;
}

void cl_printer_state_restore(CL_PrinterState s)
{
    current_depth = s.depth;
    pr_inprog_top = s.inprog_top;
    CT->pr_pprint_dispatch_active = s.dispatch_active;
    CT->pr_circle_active = s.circle_active;
}

void cl_printer_state_reset(void)
{
    current_depth = 0;
    pr_inprog_top = 0;
    CT->pr_pprint_dispatch_active = 0;
    CT->pr_circle_active = 0;
}

/* The *PRINT-* keyword-override builtins (WRITE / PPRINT / W-T-S / FORMAT's
 * ~D renderer) install THREAD-LOCAL dynamic binds via cl_dynbind_c and pop
 * them with cl_dynbind_restore_to — see tier-4 FS16.  The former
 * cl_print_controls_save/restore global-snapshot helpers are gone: mutating
 * the global cells raced peer threads' printers between set and restore. */

/* Returns 1 if obj is currently being printed (re-entrant on same object).
 * Used to short-circuit print-object-hook dispatch on circular structures. */
static int pr_inprog_contains(CL_Obj obj)
{
    int i;
    for (i = 0; i < pr_inprog_top; i++)
        if (pr_inprog[i] == obj) return 1;
    return 0;
}

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
    if (CL_NULL_P(SYM_PRINT_CIRCLE)) return 0;
    return !CL_NULL_P(cl_symbol_value(SYM_PRINT_CIRCLE));
}

static int print_pretty_p(void)
{
    if (CL_NULL_P(SYM_PRINT_PRETTY)) return 0;
    return !CL_NULL_P(cl_symbol_value(SYM_PRINT_PRETTY));
}

/* Returns right margin (default 72 when NIL), or fixnum value */
static int32_t print_right_margin(void)
{
    CL_Obj val;
    if (CL_NULL_P(SYM_PRINT_RIGHT_MARGIN)) return 72;
    val = cl_symbol_value(SYM_PRINT_RIGHT_MARGIN);
    if (CL_NULL_P(val)) return 72;
    if (CL_FIXNUM_P(val)) return CL_FIXNUM_VAL(val);
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
#define CIRCLE_HT_SIZE CL_CIRCLE_HT_SIZE
#define circle_keys       (CT->pr_circle_keys)
#define circle_vals       (CT->pr_circle_vals)
#define circle_count      (CT->pr_circle_count)
#define circle_next_label (CT->pr_circle_next_label)
#define circle_active     (CT->pr_circle_active)

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

#define printer_stream (CT->pr_stream)
#define to_buffer      (CT->pr_to_buffer)
#define out_buf        (CT->pr_out_buf)
#define out_pos        (CT->pr_out_pos)
#define out_size       (CT->pr_out_size)

static void out_char(int ch)
{
    if (printer_stream != CL_NIL) {
        /* Stream layer (cl_stream_write_char) handles UTF-8 encoding of
         * non-ASCII codepoints per destination (string/file/socket/console). */
        cl_stream_write_char(printer_stream, ch);
    } else if (to_buffer) {
#ifdef CL_WIDE_STRINGS
        if (ch > 0x7F) {
            /* UTF-8 encode into the C buffer so a non-ASCII char survives as
             * its real codepoint (not a truncated byte) — mirrors the string
             * stream path. */
            char utf8[4];
            int nb = cl_utf8_encode(ch, utf8), j;
            for (j = 0; j < nb && out_pos < out_size - 1; j++)
                out_buf[out_pos++] = utf8[j];
        } else
#endif
        if (out_pos < out_size - 1)
            out_buf[out_pos++] = (char)ch;
    } else {
#ifdef CL_WIDE_STRINGS
        if (ch > 0x7F) {
            char utf8[5];
            int nb = cl_utf8_encode(ch, utf8);
            if (nb > 0) { utf8[nb] = '\0'; platform_write_string(utf8); }
        } else
#endif
        {
            char c[2] = { (char)ch, '\0' };
            platform_write_string(c);
        }
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

/* ---- Arena-safe printer output (tier-4 audit, P-out_str) ----
 *
 * out_str(s->data) with an ARENA pointer is unsafe under MT: the stream
 * write inside can block (contended per-stream iolock, socket back-
 * pressure) in a GC safe region, a peer thread's compaction relocates the
 * string, and the write keeps reading the pre-move address — silently
 * emitting (and worse, the platform layer reading) freed arena memory.
 * These helpers take the CL_Obj instead: it is rooted (so the compactor
 * forwards the local), and the bytes are copied chunk-by-chunk into a C
 * buffer with the data pointer re-derived from the forwarded root per
 * chunk — the cl_stream_write_lisp_string pattern, incl. the m68k
 * `volatile` workaround.  Column tracking runs inside out_str/out_char on
 * the C copy.  The out path never allocates, so single-threaded behavior
 * is unchanged. */
static void out_str_lisp(CL_Obj strobj)
{
    char chunk[257];
    uint32_t start = 0, end;
    if (!CL_HEAP_P(strobj) ||
        CL_HDR_TYPE(CL_OBJ_TO_PTR(strobj)) != TYPE_STRING)
        return;
    end = ((CL_String *)CL_OBJ_TO_PTR(strobj))->length;
    CL_GC_PROTECT(strobj);
    while (start < end) {
        uint32_t nb = end - start;
        const char *volatile src =
            ((CL_String *)CL_OBJ_TO_PTR(strobj))->data;
        if (nb > (uint32_t)sizeof(chunk) - 1)
            nb = (uint32_t)sizeof(chunk) - 1;
        memcpy(chunk, src + start, nb);
        chunk[nb] = '\0';
        out_str(chunk);
        start += nb;
    }
    CL_GC_UNPROTECT(1);
}

#ifdef CL_WIDE_STRINGS
/* Wide-string variant: per code point through out_char (which UTF-8
 * encodes per destination), re-deriving the data pointer from the rooted
 * object every iteration — out_char itself can block and let a peer
 * compact between characters. */
static void out_wide_str_lisp(CL_Obj wobj)
{
    uint32_t i, len;
    if (!CL_HEAP_P(wobj) ||
        CL_HDR_TYPE(CL_OBJ_TO_PTR(wobj)) != TYPE_WIDE_STRING)
        return;
    len = ((CL_WideString *)CL_OBJ_TO_PTR(wobj))->length;
    CL_GC_PROTECT(wobj);
    for (i = 0; i < len; i++) {
        CL_WideString *tw = (CL_WideString *)CL_OBJ_TO_PTR(wobj);
        out_char((int)tw->data[i]);
    }
    CL_GC_UNPROTECT(1);
}
#endif

/* Base-aware integer output honoring *print-base* and *print-radix* */
/* Emit the read-syntax radix prefix for BASE: #b (2), #o (8), #x (16),
 * #Nr (any other non-decimal base).  Base 10 gets no prefix.  Shared by the
 * fixnum / bignum / ratio printers (each still emits its own trailing '.' for
 * decimal integers, since ratios omit it). */
static void out_radix_prefix(int32_t base)
{
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

static void out_integer(int32_t val, int32_t base, int radix)
{
    static const char digit_chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char buf[40]; /* enough for 32-bit binary + prefix + sign */
    int pos = 0;
    uint32_t uval;
    int negative = 0;

    if (radix)
        out_radix_prefix(base);

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

/* Arena-safe variant of out_symbol_name (see out_str_lisp): takes the
 * NAME STRING CL_Obj, copies it whole into C memory BEFORE any output
 * call can block, then runs the *print-case* logic on the copy.  A whole
 * copy (not chunks) because the case logic scans the full name up front
 * (needs-bars check) and tracks word state across characters. */
static void out_symbol_name_lisp(CL_Obj name_str)
{
    char sbuf[512];
    uint32_t len;
    if (!CL_HEAP_P(name_str) ||
        CL_HDR_TYPE(CL_OBJ_TO_PTR(name_str)) != TYPE_STRING)
        return;
    len = ((CL_String *)CL_OBJ_TO_PTR(name_str))->length;
    if (len < sizeof(sbuf)) {
        memcpy(sbuf, ((CL_String *)CL_OBJ_TO_PTR(name_str))->data, len);
        sbuf[len] = '\0';
        out_symbol_name(sbuf);
    } else {
        char *hbuf = (char *)platform_alloc(len + 1);
        if (!hbuf) return;
        memcpy(hbuf, ((CL_String *)CL_OBJ_TO_PTR(name_str))->data, len);
        hbuf[len] = '\0';
        out_symbol_name(hbuf);
        platform_free(hbuf);
    }
}

/* Check if sprintf result looks like a plain integer (all digits, no decimal/exponent) */
static int needs_decimal(const char *buf)
{
    const char *p = buf;
    if (*p == '-' || *p == '+') p++;
    while (*p) {
        if (!cl_ascii_isdigit((unsigned char)*p)) return 0;
        p++;
    }
    return 1;
}

/* Returns 1 when *read-default-float-format* designates a double (or long) float. */
static int default_float_is_double(void)
{
    CL_Obj val;
    const char *name;
    if (CL_NULL_P(SYM_READ_DEFAULT_FLOAT_FORMAT)) return 0; /* before init */
    val = cl_symbol_value(SYM_READ_DEFAULT_FLOAT_FORMAT);
    if (!CL_HEAP_P(val)) return 0;
    if (CL_HDR_TYPE(CL_OBJ_TO_PTR(val)) != TYPE_SYMBOL) return 0;
    name = cl_symbol_name(val);
    return (strcmp(name, "DOUBLE-FLOAT") == 0 || strcmp(name, "LONG-FLOAT") == 0);
}

/* Print a single (is_double=0) or double (is_double=1) float in CL read
 * syntax.  A type marker (f0/d0 or the exponent char f/d) is emitted only when
 * the value's type differs from *read-default-float-format* — a single needs
 * one iff the default is double, a double iff the default is single — so a
 * round-trip re-reads the same type.  Folds the two mirror-image printers. */
static void print_float(double value, int is_double)
{
    char buf[48];
    char *e;
    int default_double = default_float_is_double();
    int need_marker = is_double ? !default_double : default_double;
    char marker_exp = is_double ? 'd' : 'f';
    const char *marker_suffix = is_double ? "d0" : "f0";
    cl_float_shortest_g(buf, (int)sizeof(buf), value, is_double);
    e = strchr(buf, 'e');
    if (!e) e = strchr(buf, 'E');
    if (e) {
        *e = need_marker ? marker_exp : 'e';
    } else if (needs_decimal(buf)) {
        strcat(buf, ".0");
        if (need_marker) strcat(buf, marker_suffix);
    } else if (need_marker) {
        strcat(buf, marker_suffix);
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
        /* `obj` is the list-walk cursor.  It starts as the (rooted) argument
         * but is immediately advanced by cl_cdr, after which it lives ONLY in
         * this C local.  print_obj below can allocate (a Lisp print-object
         * method via cl_vm_apply, bignum digits) and thus reach a GC safepoint,
         * where a peer thread's stop-the-world compaction relocates the list
         * and would leave `obj` a stale offset.  Protect the cursor so the
         * compactor forwards it. */
        CL_GC_PROTECT(obj);
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
        CL_GC_UNPROTECT(1);  /* balance the cursor protect (else-branch only) */
    }

    if (pretty && pp_indent_top > 0)
        pp_indent_top--;

    out_char(')');
    current_depth--;
}

static void print_string(CL_Obj obj)
{
    uint32_t i, len = ((CL_String *)CL_OBJ_TO_PTR(obj))->length;
    /* Base strings (TYPE_STRING) hold latin-1 bytes: a byte 0x80..0xFF is a
     * codepoint in [128,255], not a UTF-8 fragment. Read each byte UNSIGNED so
     * high bytes take out_char's encode path (cl_stream_write_char UTF-8-encodes
     * codepoints > 0x7F for non-latin-1 streams). Reading it as a signed `char`
     * would sign-extend 0xFC to -4, dodge the `ch > 0x7F` test, and emit a lone
     * raw byte that get-output-stream-string then mis-decodes to U+FFFD.
     *
     * GC SAFETY (MT): out_char can block in a stream write, letting a peer
     * compact — root obj and re-derive the data pointer per character
     * instead of holding one CL_String* across the loop (see out_str_lisp). */
    CL_GC_PROTECT(obj);
    if (print_escape_p() || print_readably_p()) {
        out_char('"');
        for (i = 0; i < len; i++) {
            int ch = (unsigned char)
                ((CL_String *)CL_OBJ_TO_PTR(obj))->data[i];
            if (ch == '"' || ch == '\\') out_char('\\');
            if (ch == '\n') { out_char('\\'); out_char('n'); continue; }
            if (ch == '\t') { out_char('\\'); out_char('t'); continue; }
            out_char(ch);
        }
        out_char('"');
    } else {
        /* princ: per-char (not out_str) so high bytes are encoded, not dumped raw */
        for (i = 0; i < len; i++)
            out_char((unsigned char)
                     ((CL_String *)CL_OBJ_TO_PTR(obj))->data[i]);
    }
    CL_GC_UNPROTECT(1);
}

/* Recursive helper for multi-dim array printing.
 * Prints one dimension slice, advancing *row_major as elements are printed.
 * GC SAFETY: takes the vector as a CL_Obj, not a raw data pointer — element
 * prints can compact (struct elements allocate via cl_struct_slot_names,
 * hooks, pprint dispatch), so each level roots its own copy and the data
 * pointer is re-derived per element. */
static void print_array_slice(CL_Obj vec, uint32_t *dims, uint8_t rank,
                              uint8_t dim, uint32_t *row_major, int32_t max_len)
{
    uint32_t i;
    CL_GC_PROTECT(vec);
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
            CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(vec);
            print_obj(cl_vector_data(v)[*row_major]);
            (*row_major)++;
        } else {
            print_array_slice(vec, dims, rank, dim + 1, row_major, max_len);
        }
    }
    out_char(')');
    CL_GC_UNPROTECT(1);
}

/*
 * Check *print-pprint-dispatch* for a custom printer function.
 * Returns 1 if handled (dispatched), 0 if not.
 */
#define pprint_dispatch_active (CT->pr_pprint_dispatch_active)

static int try_pprint_dispatch(CL_Obj *obj_p)
{
    CL_Obj table, cur, best_fn;
    int32_t best_priority;

    if (pprint_dispatch_active) return 0; /* prevent recursion */
    if (CL_NULL_P(SYM_PRINT_PPRINT_DISPATCH)) return 0;
    table = cl_symbol_value(SYM_PRINT_PPRINT_DISPATCH);
    if (CL_NULL_P(table)) return 0;
    if (!print_pretty_p()) return 0;

    /* Linear scan for best matching entry.
     * GC SAFETY: cl_typep can allocate (deftype expansion / SATISFIES run
     * user code via cl_vm_apply) — root the caller's obj slot, the cursor
     * and the best-match fn so they forward across each test; entry fields
     * are re-derived from the rooted cursor after a match.
     * The recursion guard is held across the SCAN too, not just the apply:
     * a cl_typep that signals mid-scan gets its condition printed by the
     * error path, and if that print re-entered this scan the same signal
     * would recurse without bound (condition → scan → signal → condition
     * ...) until the GC root stack overflows.  The guard is restored on
     * longjmp via the error/NLX-frame printer snapshot. */
    best_fn = CL_NIL;
    best_priority = -999999;
    cur = table;
    cl_gc_push_root(obj_p);
    CL_GC_PROTECT(cur);
    CL_GC_PROTECT(best_fn);
    pprint_dispatch_active = 1;
    while (!CL_NULL_P(cur)) {
        CL_Obj entry = cl_car(cur);
        CL_Obj type_spec = cl_car(entry);
        CL_Obj prio_fn = cl_cdr(entry);
        int32_t prio = CL_FIXNUM_P(cl_car(prio_fn)) ? CL_FIXNUM_VAL(cl_car(prio_fn)) : 0;

        if (cl_typep(*obj_p, type_spec) && prio > best_priority) {
            best_priority = prio;
            best_fn = cl_cdr(cl_cdr(cl_car(cur)));
        }
        cur = cl_cdr(cur);
    }

    if (CL_NULL_P(best_fn)) {
        pprint_dispatch_active = 0;
        CL_GC_UNPROTECT(3);
        return 0;
    }

    /* Call the dispatch function: (fn stream obj).  obj_p/cur/best_fn stay
     * rooted from the scan above so best_fn survives any alloc below. */
    if (!to_buffer) {
        CL_Obj call_args[2];
        call_args[0] = printer_stream;
        call_args[1] = *obj_p;
        cl_vm_apply(best_fn, call_args, 2);
        pprint_dispatch_active = 0;
        CL_GC_UNPROTECT(3);
    } else {
        /* Buffer mode (write-to-string / princ-to-string): printer_stream is
         * NIL here — a user fn handed NIL would treat it as format's
         * "return a string" destination and the output would be silently
         * dropped.  Capture through a real string stream and splice (same
         * pattern as the TYPE_RESTART report-function path). */
        CL_Obj call_args[2];
        CL_Obj sstream, text;
        sstream = cl_make_string_output_stream();
        CL_GC_PROTECT(sstream);
        call_args[0] = sstream;
        call_args[1] = *obj_p;
        cl_vm_apply(best_fn, call_args, 2);
        pprint_dispatch_active = 0;
        text = cl_finish_string_output_stream(sstream);
        CL_GC_UNPROTECT(4);
        if (CL_STRING_P(text)) {
            out_str_lisp(text);
        }
#ifdef CL_WIDE_STRINGS
        else if (CL_HEAP_P(text) &&
                 CL_HDR_TYPE(CL_OBJ_TO_PTR(text)) == TYPE_WIDE_STRING) {
            out_wide_str_lisp(text);
        }
#endif
    }
    return 1;
}

/* Hard cap on recursive printing depth — independent of *print-level*.
 * Default *print-level* is NIL (unlimited), but custom print-object methods
 * that call into our printer via the *print-object-hook* path each enter
 * a fresh write_to_buffer_internal context; circular references between
 * objects (e.g. sento's actor-cell ↔ message-box ↔ queue ↔ message-item's
 * handler-fun-args containing the actor-cell) recurse without bound and
 * either stack-overflow or corrupt the print buffer.  This cap fires
 * regardless of *print-level* and short-circuits to "..." once the
 * cumulative recursion exceeds CL_PRINT_HARD_CAP. */
#define CL_PRINT_HARD_CAP 64

static void print_obj(CL_Obj obj)
{
    if (current_depth >= CL_PRINT_HARD_CAP) {
        out_str("...");
        return;
    }
    /* Circle check: emit #n= or #n# for shared/circular objects */
    {
        int cc = circle_check(obj);
        if (cc < 0) return; /* already printed — #n# emitted */
        /* cc == 1: #n= emitted, fall through to print contents */
    }

    /* Check pprint dispatch table.  Takes &obj so a compacting cl_typep
     * inside the scan forwards our local — the switch below would otherwise
     * deref a stale offset. */
    if (try_pprint_dispatch(&obj)) return;

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
            case '\0': out_str("Null"); break;
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
        if (radix)
            out_radix_prefix(base);
        cl_bignum_print_base(obj, base, out_str);
        if (radix && base == 10) out_char('.');
        return;
    }

    if (CL_RATIO_P(obj)) {
        CL_Ratio *r = (CL_Ratio *)CL_OBJ_TO_PTR(obj);
        int32_t base = print_base();
        int radix = print_radix_p();
        /* GC SAFETY: read both components up front and protect the second —
         * don't reach back through the raw r pointer after emitting the
         * numerator (output can reach allocating paths). */
        CL_Obj num = r->numerator;
        CL_Obj den = r->denominator;
        CL_GC_PROTECT(den);
        /* Radix prefix (same as bignum/fixnum but NO trailing dot) */
        if (radix)
            out_radix_prefix(base);
        /* Print numerator */
        if (CL_FIXNUM_P(num))
            out_integer(CL_FIXNUM_VAL(num), base, 0);
        else
            cl_bignum_print_base(num, base, out_str);
        out_char('/');
        /* Print denominator */
        if (CL_FIXNUM_P(den))
            out_integer(CL_FIXNUM_VAL(den), base, 0);
        else
            cl_bignum_print_base(den, base, out_str);
        CL_GC_UNPROTECT(1);
        return;
    }

    if (CL_COMPLEX_P(obj)) {
        CL_Complex *cx = (CL_Complex *)CL_OBJ_TO_PTR(obj);
        /* GC SAFETY: print_obj(realpart) can compact (pprint dispatch,
         * hooks) — read both parts up front and protect the second instead
         * of re-reading through the stale cx afterwards. */
        CL_Obj re = cx->realpart;
        CL_Obj im = cx->imagpart;
        CL_GC_PROTECT(im);
        out_str("#C(");
        print_obj(re);
        out_char(' ');
        print_obj(im);
        out_char(')');
        CL_GC_UNPROTECT(1);
        return;
    }

    if (CL_SINGLE_FLOAT_P(obj)) {
        print_float((double)((CL_SingleFloat *)CL_OBJ_TO_PTR(obj))->value, 0);
        return;
    }

    if (CL_DOUBLE_FLOAT_P(obj)) {
        print_float(((CL_DoubleFloat *)CL_OBJ_TO_PTR(obj))->value, 1);
        return;
    }

    switch (CL_HDR_TYPE(CL_OBJ_TO_PTR(obj))) {
    case TYPE_CONS:
        print_list(obj);
        break;

    case TYPE_SYMBOL: {
        CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(obj);
        /* Per CLHS 22.1.3.3.1: when *print-escape* and *print-readably*
         * are both NIL (PRINC / ~A), a symbol is written using just its
         * name characters — no #:, no leading colon for keywords, no
         * package qualifier.  Otherwise we print a readable form.
         *
         * GC SAFETY (MT): every out_* below can block and let a peer
         * compact, so obj is rooted for the whole case and the name /
         * package-name strings go through the *_lisp writers with the
         * pointers re-derived through the forwarded root per use. */
        int escape = print_escape_p() || print_readably_p();
        CL_GC_PROTECT(obj);
        if (!escape) {
            out_symbol_name_lisp(sym->name);
        } else if (CL_NULL_P(sym->package)) {
            /* Uninterned symbol — #: prefix only if *print-gensym* */
            if (print_gensym_p())
                out_str("#:");
            out_symbol_name_lisp(((CL_Symbol *)CL_OBJ_TO_PTR(obj))->name);
        } else if (sym->package == cl_package_keyword) {
            out_char(':');
            out_symbol_name_lisp(((CL_Symbol *)CL_OBJ_TO_PTR(obj))->name);
        } else if (sym->package == cl_current_package ||
                   cl_package_find_symbol(cl_symbol_name(obj),
                       ((CL_String *)CL_OBJ_TO_PTR(sym->name))->length,
                       cl_current_package) == obj) {
            /* Accessible in current package — no prefix */
            out_symbol_name_lisp(((CL_Symbol *)CL_OBJ_TO_PTR(obj))->name);
        } else {
            /* Symbol from another package — single colon if its home
             * package exports it, double colon otherwise.  The package
             * and its name are re-derived through the rooted obj after
             * each potentially-blocking write. */
            CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(sym->package);
            out_symbol_name_lisp(pkg->name);
            /* sym is stale after the blocking write above — re-derive
             * the home package through the rooted obj. */
            if (cl_symbol_external_p(obj,
                    ((CL_Symbol *)CL_OBJ_TO_PTR(obj))->package)) {
                out_char(':');
            } else {
                out_str("::");
            }
            out_symbol_name_lisp(((CL_Symbol *)CL_OBJ_TO_PTR(obj))->name);
        }
        CL_GC_UNPROTECT(1);
        break;
    }

    case TYPE_STRING:
        print_string(obj);
        break;

#ifdef CL_WIDE_STRINGS
    case TYPE_WIDE_STRING: {
        uint32_t wlen = ((CL_WideString *)CL_OBJ_TO_PTR(obj))->length;
        /* GC SAFETY (MT): out_char can block — root obj and re-derive the
         * data pointer per code point (see out_wide_str_lisp). */
        CL_GC_PROTECT(obj);
        if (print_escape_p() || print_readably_p()) {
            uint32_t i;
            out_char('"');
            for (i = 0; i < wlen; i++) {
                uint32_t ch =
                    ((CL_WideString *)CL_OBJ_TO_PTR(obj))->data[i];
                if (ch == '"' || ch == '\\') out_char('\\');
                if (ch == '\n') { out_char('\\'); out_char('n'); continue; }
                if (ch == '\t') { out_char('\\'); out_char('t'); continue; }
                /* Non-ASCII graphic chars are written verbatim (the output
                 * layer UTF-8-encodes per destination stream), NOT as \uXXXX:
                 * CL has no such string escape, so the old form failed to
                 * re-read to an EQUAL string and broke downstream HTML escaping
                 * of e.g. Latin-1 parameter values (cl-who &#xFC;). */
                out_char((int)ch);
            }
            out_char('"');
        } else {
            /* princ: emit every code point through out_char, which
             * UTF-8-encodes non-ASCII chars for the destination (string
             * stream / file / console).  Substituting '?' here truncated
             * wide chars (e.g. the scale glyphs ˫ ˧, U+02EB/U+02E7) to '?'
             * whenever a wide string was princ'd — including via
             * WITH-OUTPUT-TO-STRING / FORMAT ~A of a string argument. */
            uint32_t i;
            for (i = 0; i < wlen; i++)
                out_char((int)((CL_WideString *)CL_OBJ_TO_PTR(obj))->data[i]);
        }
        CL_GC_UNPROTECT(1);
        break;
    }
#endif

    /* GC SAFETY (MT) for the three function printers: the leading write
     * can block and let a peer compact — root obj and re-derive the name
     * through the forwarded local afterwards (out_str_lisp of the name
     * string keeps the historical no-*print-case* semantics). */
    case TYPE_FUNCTION: {
        CL_GC_PROTECT(obj);
        out_str("#<FUNCTION ");
        {
            CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(obj);
            if (!CL_NULL_P(f->name))
                out_str_lisp(((CL_Symbol *)CL_OBJ_TO_PTR(f->name))->name);
            else
                out_str("anonymous");
        }
        out_char('>');
        CL_GC_UNPROTECT(1);
        break;
    }

    case TYPE_CLOSURE: {
        CL_GC_PROTECT(obj);
        out_str("#<CLOSURE ");
        {
            CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(obj);
            CL_Bytecode *bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode);
            if (!CL_NULL_P(bc->name))
                out_str_lisp(((CL_Symbol *)CL_OBJ_TO_PTR(bc->name))->name);
            else
                out_str("anonymous");
        }
        out_char('>');
        CL_GC_UNPROTECT(1);
        break;
    }

    case TYPE_BYTECODE: {
        CL_GC_PROTECT(obj);
        out_str("#<COMPILED-FUNCTION ");
        {
            CL_Bytecode *bc = (CL_Bytecode *)CL_OBJ_TO_PTR(obj);
            if (!CL_NULL_P(bc->name))
                out_str_lisp(((CL_Symbol *)CL_OBJ_TO_PTR(bc->name))->name);
            else
                out_str("anonymous");
        }
        out_char('>');
        CL_GC_UNPROTECT(1);
        break;
    }

    case TYPE_VECTOR: {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
        int32_t max_depth = print_level();
        int32_t max_len = print_length();

        /* String vector: print like a string */
        if (v->flags & CL_VEC_FLAG_STRING) {
            uint32_t slen = cl_vector_active_length(v);
            CL_Obj *elts = cl_vector_data(v);
            uint32_t i;
            if (print_escape_p()) out_char('"');
            for (i = 0; i < slen; i++) {
                char ch = CL_CHAR_P(elts[i]) ? (char)CL_CHAR_VAL(elts[i]) : '?';
                if (print_escape_p() && (ch == '"' || ch == '\\'))
                    out_char('\\');
                out_char(ch);
            }
            if (print_escape_p()) out_char('"');
            break;
        }

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
            print_array_slice(obj, dims, rank, 0, &rm, max_len);
            current_depth--;
        } else {
            /* 1D vector: #(...) */
            uint32_t i;
            uint32_t vec_len = cl_vector_active_length(v);
            int pretty = print_pretty_p();
            int32_t margin = pretty ? print_right_margin() : 0;
            /* GC SAFETY: element prints can compact (struct elements always
             * allocate via cl_struct_slot_names; hooks, pprint dispatch) —
             * protect obj and re-derive the data pointer per element instead
             * of holding a raw elts pointer across print_obj. */
            CL_GC_PROTECT(obj);
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
                v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
                print_obj(cl_vector_data(v)[i]);
            }
            if (pretty && pp_indent_top > 0)
                pp_indent_top--;
            out_char(')');
            current_depth--;
            CL_GC_UNPROTECT(1);
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
        /* GC SAFETY (MT): the first write can block — root obj so the
         * name read afterwards goes through the forwarded local. */
        CL_GC_PROTECT(obj);
        out_str("#<PACKAGE ");
        out_str_lisp(((CL_Package *)CL_OBJ_TO_PTR(obj))->name);
        out_char('>');
        CL_GC_UNPROTECT(1);
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
         * to fall through to default printing.  Bump current_depth
         * across the apply so the hard recursion cap sees the nesting
         * (the hook re-enters the printer via format ~A → cl_princ_to_string
         * → write_to_buffer_internal → print_obj).  Skip the hook entirely
         * if obj is already on the in-progress stack — terminates Lisp-side
         * circular print-object recursion. */
        if (!CL_NULL_P(SYM_PRINT_OBJECT_HOOK) && !pr_inprog_contains(obj)) {
            CL_Obj hook_val = cl_symbol_value(SYM_PRINT_OBJECT_HOOK);
            if (!CL_NULL_P(hook_val)) {
                CL_Obj hook_args[1];
                CL_Obj result = CL_NIL;
                hook_args[0] = obj;
                if (pr_inprog_top < CL_PR_INPROG_MAX) {
                    pr_inprog[pr_inprog_top++] = obj;
                    current_depth++;
                    result = cl_vm_apply(hook_val, hook_args, 1);
                    current_depth--;
                    pr_inprog_top--;
                    /* GC SAFETY: the hook can compact — obj and the raw st
                     * pointer are stale.  pr_inprog[] is GC-updated, so
                     * recover the forwarded offset from the slot just popped
                     * and refresh both (same as the TYPE_CONDITION case). */
                    obj = pr_inprog[pr_inprog_top];
                    st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
                }
                if (!CL_NULL_P(result) && CL_HEAP_P(result) &&
                    CL_HDR_TYPE(CL_OBJ_TO_PTR(result)) == TYPE_STRING) {
                    out_str_lisp(result);
                    break; /* hook handled it */
                }
            }
        } else if (!CL_NULL_P(SYM_PRINT_OBJECT_HOOK) && pr_inprog_contains(obj)) {
            /* Re-entrant on the same object — emit a marker and stop. */
            out_str("#<...>");
            break;
        }

        /* GC SAFETY: cl_struct_slot_names ALLOCATES (a cons per slot) and
         * the recursive print_obj on a slot can compact (nested hook
         * dispatch, pprint) — protect obj BEFORE the slot-names call and
         * re-derive st after it, then keep obj + the slot_names cursor
         * rooted for the whole slot loop, re-deriving st each iteration. */
        CL_GC_PROTECT(obj);
        slot_names = cl_struct_slot_names(st->type_desc);
        CL_GC_PROTECT(slot_names);
        st = (CL_Struct *)CL_OBJ_TO_PTR(obj);

        if (max_depth >= 0 && current_depth >= max_depth) {
            out_char('#');
            CL_GC_UNPROTECT(2);
            break;
        }
        current_depth++;
        out_str("#S(");
        if (pretty && pp_indent_top < PP_INDENT_MAX)
            pp_indent_stack[pp_indent_top++] = current_column;
        /* st is stale after the blocking write above (MT peer compaction)
         * — re-derive through the rooted obj; type/slot names go through
         * out_str_lisp (arena-safe, historical no-*print-case* form). */
        st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
        if (!CL_NULL_P(st->type_desc))
            out_str_lisp(((CL_Symbol *)CL_OBJ_TO_PTR(st->type_desc))->name);
        {
            uint32_t nslots = st->n_slots;
            for (i = 0; i < nslots; i++) {
                if (pretty && current_column >= margin)
                    pp_newline_indent();
                else
                    out_char(' ');
                if (!CL_NULL_P(slot_names)) {
                    out_char(':');
                    out_str_lisp(((CL_Symbol *)CL_OBJ_TO_PTR(
                        cl_car(slot_names)))->name);
                    slot_names = cl_cdr(slot_names);
                }
                out_char(' ');
                st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
                print_obj(st->slots[i]);
            }
        }
        CL_GC_UNPROTECT(2);
        if (pretty && pp_indent_top > 0)
            pp_indent_top--;
        out_char(')');
        current_depth--;
        break;
    }

    case TYPE_CONDITION: {
        CL_Condition *cond = (CL_Condition *)CL_OBJ_TO_PTR(obj);
        /* Per CLHS 9.1.3 / 22.3.1.3 (~A): when *print-escape* and
         * *print-readably* are both NIL (PRINC / ~A), a condition is printed
         * by invoking its report function — the human-readable message
         * alone, no #<CONDITION ...> wrapper.
         *
         * If define-condition installed a print-object method (e.g. via
         * `:report (lambda (c s) (format s …))`) for this class, dispatch
         * through *print-object-hook* to get the user's output.  Falls
         * back to cond->report_string and the #<CONDITION …> default. */
        if (!print_escape_p() && !print_readably_p()) {
            if (!CL_NULL_P(SYM_PRINT_OBJECT_HOOK) && !pr_inprog_contains(obj)) {
                CL_Obj hook_val = cl_symbol_value(SYM_PRINT_OBJECT_HOOK);
                if (!CL_NULL_P(hook_val)) {
                    CL_Obj hook_args[1];
                    CL_Obj result = CL_NIL;
                    hook_args[0] = obj;
                    if (pr_inprog_top < CL_PR_INPROG_MAX) {
                        pr_inprog[pr_inprog_top++] = obj;
                        current_depth++;
                        result = cl_vm_apply(hook_val, hook_args, 1);
                        current_depth--;
                        pr_inprog_top--;
                        /* cl_vm_apply may have triggered a compacting GC that
                         * relocated the condition: `cond` is a raw arena pointer
                         * captured before the call and is now stale, and `obj`'s
                         * offset is stale too.  pr_inprog[] is GC-updated (see
                         * gc_update_thread_roots), so recover the forwarded
                         * offset from the slot we just popped and refresh both —
                         * otherwise cond->report_string below reads freed/moved
                         * memory and the message is lost ("#<CONDITION ...>"
                         * instead of the report under GC stress). */
                        obj = pr_inprog[pr_inprog_top];
                        cond = (CL_Condition *)CL_OBJ_TO_PTR(obj);
                    }
                    if (!CL_NULL_P(result) && CL_HEAP_P(result) &&
                        CL_HDR_TYPE(CL_OBJ_TO_PTR(result)) == TYPE_STRING) {
                        CL_String *rs = (CL_String *)CL_OBJ_TO_PTR(result);
                        if (rs->length > 0) {
                            out_str_lisp(result);
                            break;
                        }
                    }
                }
            }
            if (!CL_NULL_P(cond->report_string)) {
                out_str_lisp(cond->report_string);
                break;
            }
        }
        /* GC SAFETY (MT): each write below can block and let a peer
         * compact — root obj and re-derive cond after every write.
         * out_str_lisp of the symbol's NAME string keeps the historical
         * out_str(cl_symbol_name(...)) semantics (no *print-case*). */
        CL_GC_PROTECT(obj);
        out_str("#<CONDITION ");
        cond = (CL_Condition *)CL_OBJ_TO_PTR(obj);
        if (!CL_NULL_P(cond->type_name))
            out_str_lisp(
                ((CL_Symbol *)CL_OBJ_TO_PTR(cond->type_name))->name);
        else
            out_str("CONDITION");
        cond = (CL_Condition *)CL_OBJ_TO_PTR(obj);
        if (!CL_NULL_P(cond->report_string)) {
            out_str(": \"");
            out_str_lisp(((CL_Condition *)CL_OBJ_TO_PTR(obj))->report_string);
            out_char('"');
        }
        out_char('>');
        CL_GC_UNPROTECT(1);
        break;
    }

    case TYPE_RESTART: {
        CL_Restart *r = (CL_Restart *)CL_OBJ_TO_PTR(obj);
        /* PRINC / ~A (escape and readably both NIL): print the restart's
         * report — its :report string, or the result of calling its report
         * function on the stream (CLHS 9.1.4.2.2). */
        if (!print_escape_p() && !print_readably_p() &&
            !CL_NULL_P(r->report)) {
            if (CL_STRING_P(r->report)) {
                out_str_lisp(r->report);
                break;
            }
            /* Report function: call it with a fresh string output stream,
             * then splice the captured text. */
            if (!pr_inprog_contains(obj) && pr_inprog_top < CL_PR_INPROG_MAX) {
                CL_Obj sstream, text, rargs[1], report_fn;
                report_fn = r->report;
                CL_GC_PROTECT(report_fn);
                sstream = cl_make_string_output_stream();
                CL_GC_PROTECT(sstream);
                rargs[0] = sstream;
                pr_inprog[pr_inprog_top++] = obj;
                current_depth++;
                cl_vm_apply(report_fn, rargs, 1);
                current_depth--;
                pr_inprog_top--;
                /* GC SAFETY: the apply can compact — obj and the raw r
                 * pointer are stale.  pr_inprog[] is GC-updated, so recover
                 * the forwarded offset from the slot just popped (same as
                 * the STRUCT/CONDITION hook paths), keep it rooted across
                 * the allocating get-output-stream-string, and re-derive r
                 * for the #<RESTART NAME> fallback below. */
                obj = pr_inprog[pr_inprog_top];
                CL_GC_PROTECT(obj);
                text = cl_finish_string_output_stream(sstream);
                CL_GC_UNPROTECT(3);
                r = (CL_Restart *)CL_OBJ_TO_PTR(obj);
                if (CL_STRING_P(text)) {
                    out_str_lisp(text);
                    break;
                }
#ifdef CL_WIDE_STRINGS
                /* A report that emitted non-ASCII comes back as a wide
                 * string — print it per code point instead of degrading to
                 * the #<RESTART ...> fallback (out_char UTF-8-encodes). */
                if (CL_HEAP_P(text) &&
                    CL_HDR_TYPE(CL_OBJ_TO_PTR(text)) == TYPE_WIDE_STRING) {
                    out_wide_str_lisp(text);
                    break;
                }
#endif
            }
        }
        /* Escaped form (or no report): #<RESTART NAME>.  GC SAFETY (MT):
         * the write can block — root obj, re-derive r after it. */
        CL_GC_PROTECT(obj);
        out_str("#<RESTART");
        r = (CL_Restart *)CL_OBJ_TO_PTR(obj);
        if (!CL_NULL_P(r->name)) {
            out_char(' ');
            out_str_lisp(((CL_Symbol *)CL_OBJ_TO_PTR(
                ((CL_Restart *)CL_OBJ_TO_PTR(obj))->name))->name);
        }
        out_char('>');
        CL_GC_UNPROTECT(1);
        break;
    }

    case TYPE_PATHNAME: {
        CL_Pathname *pn = (CL_Pathname *)CL_OBJ_TO_PTR(obj);
        char ns_buf[1024];
        extern uint32_t cl_pathname_to_namestring(CL_Pathname *pn, char *buf, uint32_t bufsz);
        cl_pathname_to_namestring(pn, ns_buf, sizeof(ns_buf));
        /* PRIN1/print-readably use the #P"..." reader syntax; PRINC (~A,
         * *print-escape* nil) prints the bare namestring.  local-time's
         * REREAD-TIMEZONE-REPOSITORY relies on (princ-to-string pathname)
         * returning just the namestring to compute substring offsets, so
         * emitting #P"..." here corrupted every zone's hash key. */
        if (print_escape_p() || print_readably_p()) {
            out_str("#P\"");
            out_str(ns_buf);
            out_char('"');
        } else {
            out_str(ns_buf);
        }
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
        if (st->stream_type == CL_STREAM_TWO_WAY) {
            out_str("#<TWO-WAY-STREAM");
            if (!(st->flags & CL_STREAM_FLAG_OPEN))
                out_str(" (closed)");
            out_char('>');
            break;
        }
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

    case TYPE_THREAD: {
        CL_ThreadObj *to = (CL_ThreadObj *)CL_OBJ_TO_PTR(obj);
        out_str("#<THREAD");
        if (!CL_NULL_P(to->name)) {
            out_char(' ');
            print_obj(to->name);
        }
        out_char('>');
        break;
    }

    case TYPE_LOCK: {
        CL_Lock *lk = (CL_Lock *)CL_OBJ_TO_PTR(obj);
        out_str("#<LOCK");
        if (!CL_NULL_P(lk->name)) {
            out_char(' ');
            print_obj(lk->name);
        }
        out_char('>');
        break;
    }

    case TYPE_CONDVAR: {
        CL_CondVar *cv = (CL_CondVar *)CL_OBJ_TO_PTR(obj);
        out_str("#<CONDITION-VARIABLE");
        if (!CL_NULL_P(cv->name)) {
            out_char(' ');
            print_obj(cv->name);
        }
        out_char('>');
        break;
    }

    case TYPE_FOREIGN_POINTER: {
        CL_ForeignPtr *fp = (CL_ForeignPtr *)CL_OBJ_TO_PTR(obj);
        char buf[48];
        snprintf(buf, sizeof(buf), "#<FOREIGN-POINTER #x%08X [%u]>",
                 (unsigned)fp->address, (unsigned)fp->size);
        out_str(buf);
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
    int prev_to_buffer = to_buffer;
    /* GC SAFETY: print_obj can compact (hooks, pprint dispatch, struct
     * slot names).  prev is a heap stream offset held in a C local across
     * the whole print — protect it so the restore doesn't write a stale
     * offset back into CT->pr_stream (a nested print via hook → format nil
     * would then emit through a garbage stream). */
    CL_GC_PROTECT(prev);
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
    to_buffer = prev_to_buffer;
    current_depth = prev_depth;
    current_column = prev_column;
    pp_indent_top = prev_indent_top;
    CL_GC_UNPROTECT(1);
}

void cl_prin1_to_stream(CL_Obj obj, CL_Obj stream)
{
    if (CL_NULL_P(SYM_PRINT_ESCAPE)) {
        /* Before init — fall through directly */
        CL_Obj prev = printer_stream;
        CL_GC_PROTECT(prev);
        to_buffer = 0;
        printer_stream = stream;
        print_obj(obj);
        printer_stream = prev;
        CL_GC_UNPROTECT(1);
        return;
    }
    /* Thread-local dynamic bind (tier-4 FS16): mutating the global
     * *PRINT-ESCAPE* cell raced peer threads' printers between set and
     * restore.  The dyn stack is GC-marked, so the saved value needs no
     * manual root (supersedes the tier-4 IO2 restore-class protects).
     * Same in the variants below. */
    {
        int dyn_mark = cl_dyn_top;
        cl_dynbind_c(SYM_PRINT_ESCAPE, CL_T);
        cl_write_to_stream(obj, stream);
        cl_dynbind_restore_to(dyn_mark);
    }
}

void cl_princ_to_stream(CL_Obj obj, CL_Obj stream)
{
    if (CL_NULL_P(SYM_PRINT_ESCAPE)) {
        CL_Obj prev = printer_stream;
        CL_GC_PROTECT(prev);
        to_buffer = 0;
        printer_stream = stream;
        print_obj(obj);
        printer_stream = prev;
        CL_GC_UNPROTECT(1);
        return;
    }
    {
        int dyn_mark = cl_dyn_top;
        cl_dynbind_c(SYM_PRINT_ESCAPE, CL_NIL);
        cl_dynbind_c(SYM_PRINT_READABLY, CL_NIL);
        cl_write_to_stream(obj, stream);
        cl_dynbind_restore_to(dyn_mark);
    }
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
    cl_prin1_to_stream(obj, cl_symbol_value(SYM_STANDARD_OUTPUT));
}

void cl_princ(CL_Obj obj)
{
    cl_princ_to_stream(obj, cl_symbol_value(SYM_STANDARD_OUTPUT));
}

void cl_print(CL_Obj obj)
{
    cl_print_to_stream(obj, cl_symbol_value(SYM_STANDARD_OUTPUT));
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
    /* Save the entire buffer-mode context: re-entry from a print hook
     * (e.g. *print-object-hook* dispatches to a user method that calls
     * format, which itself uses cl_princ_to_string for ~A) would clobber
     * out_buf/out_pos/out_size/to_buffer otherwise — losing whatever the
     * outer caller had accumulated and writing into a now-stale buffer
     * after the inner returns. */
    int prev_to_buffer = to_buffer;
    char *prev_out_buf = out_buf;
    int prev_out_pos = out_pos;
    int prev_out_size = out_size;
    int result_pos;

    /* GC SAFETY: prev is a heap stream offset saved in a C local across
     * print_obj — protect it (see cl_write_to_stream). */
    CL_GC_PROTECT(prev);

    /* Preserve current_depth across nested write_to_buffer_internal
     * calls so the hard recursion cap in print_obj sees the cumulative
     * depth.  current_depth is restored by save/restore below — this
     * just means a nested call picks up where the outer left off. */
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
    result_pos = out_pos;

    to_buffer = prev_to_buffer;
    out_buf = prev_out_buf;
    out_pos = prev_out_pos;
    out_size = prev_out_size;
    printer_stream = prev;
    current_depth = prev_depth;
    current_column = prev_column;
    pp_indent_top = prev_indent_top;
    CL_GC_UNPROTECT(1);
    return result_pos;
}

int cl_prin1_to_string(CL_Obj obj, char *buf, int bufsize)
{
    int result;
    if (CL_NULL_P(SYM_PRINT_ESCAPE)) {
        /* Before init */
        return write_to_buffer_internal(obj, buf, bufsize);
    }
    /* Thread-local dynamic bind — see cl_prin1_to_stream. */
    {
        int dyn_mark = cl_dyn_top;
        cl_dynbind_c(SYM_PRINT_ESCAPE, CL_T);
        result = write_to_buffer_internal(obj, buf, bufsize);
        cl_dynbind_restore_to(dyn_mark);
    }
    return result;
}

int cl_princ_to_string(CL_Obj obj, char *buf, int bufsize)
{
    int result;
    if (CL_NULL_P(SYM_PRINT_ESCAPE)) {
        return write_to_buffer_internal(obj, buf, bufsize);
    }
    {
        int dyn_mark = cl_dyn_top;
        cl_dynbind_c(SYM_PRINT_ESCAPE, CL_NIL);
        cl_dynbind_c(SYM_PRINT_READABLY, CL_NIL);
        result = write_to_buffer_internal(obj, buf, bufsize);
        cl_dynbind_restore_to(dyn_mark);
    }
    return result;
}

/* ================================================================
 * Pretty-printing public API
 * ================================================================ */

/* Block start column stack (parallel to pp_indent_stack) */
#define pp_block_start (CT->pr_block_start)

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
