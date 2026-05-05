#include "reader.h"
#include "readtable.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "bignum.h"
#include "float.h"
#include "ratio.h"
#include "error.h"
#include "stream.h"
#include "string_utils.h"
#include "vm.h"
#include "compiler.h"
#include "../platform/platform.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

/* strcasecmp: available via <strings.h> on POSIX, provide fallback for AmigaOS */
#ifdef PLATFORM_AMIGA
static int cl_strcasecmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
#define strcasecmp cl_strcasecmp
#else
#include <strings.h>
#endif

/* Reader state now lives in CL_Thread.  Local macros redirect old names. */
#define reader_stream  (CT->rd_stream)
#define eof_seen       (CT->rd_eof)
#define read_suppress  (CT->rd_suppress)
#define reader_line    (CT->rd_line)
#define rd_uninterned  (CT->rd_uninterned)
/* cl_current_source_file and cl_current_file_id are macros from thread.h */

/* Source location tracking (shared, not per-thread) */
CL_SrcLoc cl_srcloc_table[CL_SRCLOC_SIZE];

/* Reader error helper — prepends source file and line to the message so
 * diagnostics point at the actual location, not always "(line 1)". */
static void cl_reader_error(int code, const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    const char *file;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    file = cl_current_source_file;
    if (file && file[0]) {
        /* Show only the basename to keep messages compact */
        const char *base = file;
        const char *p;
        for (p = file; *p; p++) {
            if (*p == '/' || *p == ':') base = p + 1;
        }
        cl_error(code, "%s:%d: %s", base, reader_line, msg);
    } else {
        cl_error(code, "line %d: %s", reader_line, msg);
    }
}

static int read_char(void)
{
    int ch = cl_stream_read_char(reader_stream);
    if (ch == '\n') reader_line++;
    return ch;
}

static void unread_char(int ch)
{
    if (ch < 0) return;
    cl_stream_unread_char(reader_stream, ch);
    if (ch == '\n') reader_line--;
}

static int current_line(void)
{
    return reader_line;
}

static void srcloc_record(CL_Obj cons_obj, int line)
{
    uint32_t idx = (cons_obj >> 2) % CL_SRCLOC_SIZE;
    cl_srcloc_table[idx].cons_obj = cons_obj;
    cl_srcloc_table[idx].line = (uint16_t)line;
    cl_srcloc_table[idx].file_id = cl_current_file_id;
}

/* Skip whitespace and comments */
static void skip_whitespace(void)
{
    int ch;
    for (;;) {
        ch = read_char();
        if (ch < 0) { eof_seen = 1; return; }
        if (ch == ';') {
            /* Line comment */
            do { ch = read_char(); } while (ch >= 0 && ch != '\n');
            if (ch < 0) { eof_seen = 1; return; }
            continue;
        }
        if (!isspace(ch)) {
            unread_char(ch);
            return;
        }
    }
}

static int is_delimiter(int ch)
{
    if (ch < 0) return 1;
    if (ch < CL_RT_CHARS) {
        CL_Readtable *rt = cl_readtable_current();
        uint8_t syn = rt->syntax[ch];
        return syn == CL_CHAR_WHITESPACE || syn == CL_CHAR_TERM_MACRO;
    }
    return 0;
}

/* Forward declarations */
static CL_Obj read_expr(void);
static CL_Obj read_list(void);

/* Sentinel value returned when #+/#- skips a form.
 * Low byte 0x06: not fixnum (bit 0 = 0), not char (not 0x0A), not NIL (not 0).
 * Never a valid arena offset. Only used internally by the reader. */
#define CL_READER_SKIP ((CL_Obj)0x06)

/* --- Feature conditional support (#+ / #-) --- */

/* Check if a keyword symbol is in the *features* list */
static int feature_member(CL_Obj keyword)
{
    CL_Obj list = cl_symbol_value(SYM_STAR_FEATURES);
    while (CL_CONS_P(list)) {
        if (cl_car(list) == keyword)
            return 1;
        list = cl_cdr(list);
    }
    return 0;
}

/* Keyword symbols for feature expression operators.
 * Initialized lazily on first use to avoid init-order issues. */
static CL_Obj kw_and = CL_NIL;
static CL_Obj kw_or  = CL_NIL;
static CL_Obj kw_not = CL_NIL;

static void ensure_feature_keywords(void)
{
    if (CL_NULL_P(kw_and)) {
        kw_and = cl_intern_keyword("AND", 3);
        kw_or  = cl_intern_keyword("OR", 2);
        kw_not = cl_intern_keyword("NOT", 3);
        /* Register cached symbols for GC compaction forwarding */
        cl_gc_register_root(&kw_and);
        cl_gc_register_root(&kw_or);
        cl_gc_register_root(&kw_not);
    }
}

/* Evaluate a feature expression (read as a form) against *features*.
 * Feature exprs: keyword atoms, (:and ...), (:or ...), (:not ...) */
static int eval_feature_expr(CL_Obj expr)
{
    if (CL_SYMBOL_P(expr)) {
        /* Should be a keyword — check membership */
        return feature_member(expr);
    }
    if (CL_CONS_P(expr)) {
        CL_Obj head = cl_car(expr);
        CL_Obj rest = cl_cdr(expr);
        ensure_feature_keywords();
        if (head == kw_and) {
            /* (:and f1 f2 ...) — all must be true */
            while (CL_CONS_P(rest)) {
                if (!eval_feature_expr(cl_car(rest)))
                    return 0;
                rest = cl_cdr(rest);
            }
            return 1;
        }
        if (head == kw_or) {
            /* (:or f1 f2 ...) — any must be true */
            while (CL_CONS_P(rest)) {
                if (eval_feature_expr(cl_car(rest)))
                    return 1;
                rest = cl_cdr(rest);
            }
            return 0;
        }
        if (head == kw_not) {
            /* (:not f) — invert */
            if (CL_CONS_P(rest))
                return !eval_feature_expr(cl_car(rest));
            return 1;
        }
    }
    return 0;
}

/* Skip (read and discard) a single form from the reader.
 * Increments read_suppress so that errors (unknown packages, etc.) are suppressed. */
static void skip_form(void)
{
    read_suppress++;
    read_expr();  /* Just read and discard */
    read_suppress--;
}

/* Read a number in the given radix (2, 8, 16, or arbitrary via #nR).
 * Handles optional sign, fixnum/bignum dispatch. */
static CL_Obj read_radix_number(int radix)
{
    char buf[256];
    int len = 0;
    int ch;
    int neg = 0;
    int i;

    /* Read token */
    while (len < 255) {
        ch = read_char();
        if (is_delimiter(ch)) { unread_char(ch); break; }
        buf[len++] = (char)toupper(ch);
    }
    buf[len] = '\0';

    if (read_suppress) return CL_NIL;
    if (len == 0) {
        cl_reader_error(CL_ERR_PARSE, "No digits after #radix prefix");
        return CL_NIL;
    }

    /* Handle sign */
    i = 0;
    if (buf[0] == '+') i = 1;
    else if (buf[0] == '-') { neg = 1; i = 1; }

    if (i >= len) {
        cl_reader_error(CL_ERR_PARSE, "No digits after sign in #radix number");
        return CL_NIL;
    }

    /* Try fixnum range first using unsigned long accumulator */
    {
        unsigned long val = 0;
        int overflow = 0;
        int j;
        for (j = i; j < len; j++) {
            int dv;
            char c = buf[j];
            if (c >= '0' && c <= '9') dv = c - '0';
            else if (c >= 'A' && c <= 'Z') dv = c - 'A' + 10;
            else {
                cl_reader_error(CL_ERR_PARSE, "Invalid digit '%c' for radix %d", buf[j], radix);
                return CL_NIL;
            }
            if (dv >= radix) {
                cl_reader_error(CL_ERR_PARSE, "Invalid digit '%c' for radix %d", buf[j], radix);
                return CL_NIL;
            }
            if (!overflow) {
                unsigned long nv = val * (unsigned long)radix + (unsigned long)dv;
                if (nv / (unsigned long)radix != val) overflow = 1;
                else val = nv;
            }
        }
        if (!overflow) {
            /* Guard against unsigned→signed overflow on 32-bit:
               if val > LONG_MAX, (long)val wraps to negative.
               Check against fixnum range using unsigned comparisons. */
            if (!neg && val <= (unsigned long)CL_FIXNUM_MAX)
                return CL_MAKE_FIXNUM((int32_t)val);
            if (neg && val <= ((unsigned long)CL_FIXNUM_MAX + 1u))
                return CL_MAKE_FIXNUM(-(int32_t)val);
        }
    }

    /* Bignum: Horner's method with cl_arith_mul/cl_arith_add */
    {
        CL_Obj result = CL_MAKE_FIXNUM(0);
        CL_Obj radix_obj = CL_MAKE_FIXNUM(radix);
        int j;
        CL_GC_PROTECT(result);
        for (j = i; j < len; j++) {
            int dv;
            char c = buf[j];
            if (c >= '0' && c <= '9') dv = c - '0';
            else dv = c - 'A' + 10;
            result = cl_arith_mul(result, radix_obj);
            result = cl_arith_add(result, CL_MAKE_FIXNUM(dv));
        }
        if (neg) result = cl_arith_negate(result);
        CL_GC_UNPROTECT(1);
        return result;
    }
}

/*
 * Try to parse buf as a float literal.
 * Returns the float object, or CL_NIL if not a valid float token.
 *
 * Recognized forms (input already uppercased by reader):
 *   1.0  .5  1.            — decimal point
 *   1E3  1.5E-2            — E exponent (default = single-float)
 *   1.0F0  1.5F-2          — F exponent (single-float)
 *   1.0S0  1.5S-2          — S exponent (short = single-float)
 *   1.0D0  1.5D-2          — D exponent (double-float)
 *   1.0L0  1.5L-2          — L exponent (long = double-float)
 */
static CL_Obj try_parse_float(const char *buf, int len)
{
    int i, has_dot = 0, has_exp = 0, has_digit = 0;
    int exp_pos = -1, is_double = 0;
    char parse_buf[256];
    double val;
    char *endp;

    i = 0;
    if (len > 0 && (buf[0] == '+' || buf[0] == '-')) i = 1;

    for (; i < len; i++) {
        if (isdigit((unsigned char)buf[i])) {
            has_digit = 1;
        } else if (buf[i] == '.' && !has_dot && !has_exp) {
            has_dot = 1;
        } else if ((buf[i] == 'E' || buf[i] == 'F' || buf[i] == 'D' ||
                    buf[i] == 'S' || buf[i] == 'L') && !has_exp && has_digit) {
            has_exp = 1;
            exp_pos = i;
            if (buf[i] == 'D' || buf[i] == 'L')
                is_double = 1;
        } else if ((buf[i] == '+' || buf[i] == '-') && has_exp && i == exp_pos + 1) {
            /* Sign after exponent marker — valid */
        } else {
            return CL_NIL;  /* Invalid character for float */
        }
    }

    /* Must have at least one digit and either a dot or exponent marker */
    if (!has_digit || (!has_dot && !has_exp))
        return CL_NIL;

    /* Build parse buffer: replace CL exponent marker with 'E' for strtod */
    if (len >= 256) return CL_NIL;
    memcpy(parse_buf, buf, (uint32_t)len);
    parse_buf[len] = '\0';
    if (exp_pos >= 0 && parse_buf[exp_pos] != 'E')
        parse_buf[exp_pos] = 'E';

    val = strtod(parse_buf, &endp);
    if (endp != parse_buf + len)
        return CL_NIL;  /* strtod didn't consume entire token */

    if (is_double)
        return cl_make_double_float(val);
    return cl_make_single_float((float)val);
}

/* Read an atom (number, symbol, keyword).
 *
 * If prefix is non-NULL, the first prefix_len characters are seeded into the
 * token buffer (already-consumed characters from upstream callers).  This is
 * used by read_list when it has read a leading `.` plus a non-delimiter
 * lookahead character but cannot push both back through the stream's
 * single-slot unread buffer. */
static CL_Obj read_atom_with_prefix(const char *prefix, int prefix_len)
{
    char buf[256];
    int len = 0;
    int ch;
    int is_number = 1;
    int has_digit = 0;
    int has_escape = 0;  /* Set if | or \ escaping was used */
    int i;
    CL_Readtable *rt = cl_readtable_current();

    if (prefix && prefix_len > 0) {
        int p;
        if (prefix_len > 255) prefix_len = 255;
        for (p = 0; p < prefix_len; p++)
            buf[len++] = (char)toupper((unsigned char)prefix[p]);
    }

    while (len < 255) {
        ch = read_char();
        if (ch < 0) break;  /* EOF */

        /* Multiple escape: |...| — read literally until closing | */
        if (ch < CL_RT_CHARS && rt->syntax[ch] == CL_CHAR_MULTI_ESCAPE) {
            has_escape = 1;
            for (;;) {
                ch = read_char();
                if (ch < 0) {
                    cl_reader_error(CL_ERR_PARSE, "Unterminated | in symbol name");
                    return CL_NIL;
                }
                if (ch < CL_RT_CHARS && rt->syntax[ch] == CL_CHAR_MULTI_ESCAPE)
                    break;  /* closing | */
                /* Single escape inside multiple escape */
                if (ch < CL_RT_CHARS && rt->syntax[ch] == CL_CHAR_ESCAPE) {
                    ch = read_char();
                    if (ch < 0) break;
                }
                if (len < 255) buf[len++] = (char)ch;  /* NO case conversion */
            }
            continue;
        }

        /* Single escape: \ — read next char literally */
        if (ch < CL_RT_CHARS && rt->syntax[ch] == CL_CHAR_ESCAPE) {
            has_escape = 1;
            ch = read_char();
            if (ch < 0) break;
            if (len < 255) buf[len++] = (char)ch;  /* NO case conversion */
            continue;
        }

        if (is_delimiter(ch)) {
            unread_char(ch);
            break;
        }
        buf[len++] = (char)toupper(ch);
    }
    buf[len] = '\0';

    if (len == 0 && !has_escape) {
        cl_reader_error(CL_ERR_PARSE, "Unexpected end of input");
        return CL_NIL;
    }

    /* Escaped tokens (|...|, \x) are always symbols — skip numeric parsing
     * but still honor the keyword (':foo) and pkg:sym prefixes, otherwise
     * forms like ':|abcdefg|' get interned as a CL-USER symbol named
     * ":abcdefg" instead of a keyword |abcdefg|. */
    if (has_escape) goto check_keyword;

    /* Check for number: optional sign followed by digits */
    i = 0;
    if (buf[0] == '+' || buf[0] == '-') i = 1;
    for (; i < len; i++) {
        if (isdigit((unsigned char)buf[i])) {
            has_digit = 1;
        } else {
            is_number = 0;
            break;
        }
    }
    if (is_number && has_digit) {
        int neg = 0;
        int start;
        int digit_count;
        i = 0;
        if (buf[0] == '-') { neg = 1; i = 1; }
        else if (buf[0] == '+') { i = 1; }
        start = i;
        digit_count = len - start;

        /* If too many digits for fixnum (> 10 digits), go straight to bignum */
        if (digit_count <= 9) {
            long val = 0;
            for (i = start; i < len; i++) {
                val = val * 10 + (buf[i] - '0');
            }
            if (neg) val = -val;
            if (val >= CL_FIXNUM_MIN && val <= CL_FIXNUM_MAX)
                return CL_MAKE_FIXNUM((int32_t)val);
        }
        /* Overflow or large number: create bignum */
        return cl_bignum_from_string(buf + start, digit_count, neg);
    }

    /* Try float literal (decimal point or exponent marker) */
    {
        CL_Obj float_obj = try_parse_float(buf, len);
        if (!CL_NULL_P(float_obj))
            return float_obj;
    }

    /* Check for ratio literal: [sign]digits/digits (e.g. 1/2, -3/4) */
    {
        int slash_pos = -1;
        int valid_ratio = 1;
        int ri;
        int rstart = 0;

        if (buf[0] == '+' || buf[0] == '-') rstart = 1;
        /* Find exactly one slash with digits on both sides */
        for (ri = rstart; ri < len; ri++) {
            if (buf[ri] == '/') {
                if (slash_pos >= 0) { valid_ratio = 0; break; }
                slash_pos = ri;
            } else if (!isdigit((unsigned char)buf[ri])) {
                valid_ratio = 0;
                break;
            }
        }
        if (valid_ratio && slash_pos > rstart && slash_pos < len - 1) {
            /* Parse numerator */
            int num_neg = (buf[0] == '-');
            int num_start = rstart;
            int num_digits = slash_pos - num_start;
            CL_Obj num_obj, den_obj;

            if (num_digits <= 9) {
                long nv = 0;
                for (ri = num_start; ri < slash_pos; ri++)
                    nv = nv * 10 + (buf[ri] - '0');
                if (num_neg) nv = -nv;
                if (nv >= CL_FIXNUM_MIN && nv <= CL_FIXNUM_MAX)
                    num_obj = CL_MAKE_FIXNUM((int32_t)nv);
                else
                    num_obj = cl_bignum_from_string(buf + num_start, num_digits, num_neg);
            } else {
                num_obj = cl_bignum_from_string(buf + num_start, num_digits, num_neg);
            }

            /* Parse denominator (always positive) */
            {
                int den_start = slash_pos + 1;
                int den_digits = len - den_start;

                CL_GC_PROTECT(num_obj);

                if (den_digits <= 9) {
                    long dv = 0;
                    for (ri = den_start; ri < len; ri++)
                        dv = dv * 10 + (buf[ri] - '0');
                    if (dv >= CL_FIXNUM_MIN && dv <= CL_FIXNUM_MAX)
                        den_obj = CL_MAKE_FIXNUM((int32_t)dv);
                    else
                        den_obj = cl_bignum_from_string(buf + den_start, den_digits, 0);
                } else {
                    den_obj = cl_bignum_from_string(buf + den_start, den_digits, 0);
                }

                CL_GC_UNPROTECT(1);
            }

            return cl_make_ratio_normalized(num_obj, den_obj);
        }
    }

check_keyword:
    /* Check for keyword */
    if (buf[0] == ':') {
        return cl_intern_keyword(buf + 1, (uint32_t)(len - 1));
    }

    /* Check for package-qualified symbol (pkg:sym or pkg::sym) */
    {
        int colon_pos = -1;
        int double_colon = 0;
        for (i = 0; i < len; i++) {
            if (buf[i] == ':') {
                colon_pos = i;
                if (i + 1 < len && buf[i + 1] == ':') {
                    double_colon = 1;
                }
                break;
            }
        }
        if (colon_pos > 0) {
            char pkg_name[256];
            char sym_name[256];
            CL_Obj package;
            int sym_start = double_colon ? colon_pos + 2 : colon_pos + 1;
            int sym_len = len - sym_start;

            if (sym_len <= 0) {
                if (read_suppress) return CL_NIL;
                cl_reader_error(CL_ERR_PARSE, "Missing symbol name after package qualifier");
            }

            memcpy(pkg_name, buf, (uint32_t)colon_pos);
            pkg_name[colon_pos] = '\0';
            memcpy(sym_name, buf + sym_start, (uint32_t)sym_len);
            sym_name[sym_len] = '\0';

            package = cl_find_package(pkg_name, (uint32_t)colon_pos);
            if (CL_NULL_P(package)) {
                if (read_suppress) return CL_NIL;
                cl_reader_error(CL_ERR_PARSE, "Package %s not found", pkg_name);
            }

            if (double_colon) {
                /* pkg::sym — intern as internal symbol */
                return cl_intern_in(sym_name, (uint32_t)sym_len, package);
            } else {
                /* pkg:sym — look up external symbol only */
                CL_Obj sym = cl_package_find_external(sym_name, (uint32_t)sym_len, package);
                if (CL_NULL_P(sym)) {
                    if (read_suppress) return CL_NIL;
                    cl_reader_error(CL_ERR_PARSE, "Symbol %s not exported from %s",
                             sym_name, pkg_name);
                }
                return sym;
            }
        }
    }

    /* Check for NIL */
    if (len == 3 && buf[0] == 'N' && buf[1] == 'I' && buf[2] == 'L') {
        return CL_NIL;
    }

intern_symbol:
    /* Regular symbol (also reached via goto for escaped tokens) */
    return cl_intern(buf, (uint32_t)len);
}

/* Read a string literal */
static CL_Obj read_string(void)
{
    char buf[4096]; /* UTF-8 encoded buffer */
    int len = 0;
    int ch;

    for (;;) {
        ch = read_char();
        if (ch < 0) {
            cl_reader_error(CL_ERR_PARSE, "Unterminated string");
            return CL_NIL;
        }
        if (ch == '"') break;
        if (ch == '\\') {
            ch = read_char();
            if (ch < 0) {
                cl_reader_error(CL_ERR_PARSE, "Unterminated string escape");
                return CL_NIL;
            }
            switch (ch) {
            case 'n': ch = '\n'; break;
            case 't': ch = '\t'; break;
            case '\\': ch = '\\'; break;
            case '"': ch = '"'; break;
            default: break;
            }
        }
#ifdef CL_WIDE_STRINGS
        if (ch > 0x7F) {
            char tmp[4];
            int nb = cl_utf8_encode(ch, tmp);
            int j;
            for (j = 0; j < nb && len < 4095; j++)
                buf[len++] = tmp[j];
        } else
#endif
        {
            if (len < 4095) buf[len++] = (char)ch;
        }
    }
    buf[len] = '\0';
#ifdef CL_WIDE_STRINGS
    return cl_utf8_to_cl_string(buf, (uint32_t)len);
#else
    return cl_make_string(buf, (uint32_t)len);
#endif
}

int cl_srcloc_lookup(CL_Obj cons_obj)
{
    uint32_t idx = (cons_obj >> 2) % CL_SRCLOC_SIZE;
    if (cl_srcloc_table[idx].cons_obj == cons_obj)
        return (int)cl_srcloc_table[idx].line;
    return 0;
}

/* Read a list */
static CL_Obj read_list(void)
{
    int start_line = current_line();
    CL_Obj head = CL_NIL;
    CL_Obj tail = CL_NIL;
    CL_Obj elem;

    CL_GC_PROTECT(head);
    CL_GC_PROTECT(tail);

    for (;;) {
        skip_whitespace();
        if (eof_seen) {
            CL_GC_UNPROTECT(2);
            cl_reader_error(CL_ERR_PARSE, "Unterminated list");
            return CL_NIL;
        }

        {
            int ch = read_char();
            if (ch == ')') {
                CL_GC_UNPROTECT(2);
                return head;
            }

            /* Dotted pair */
            if (ch == '.') {
                int next = read_char();
                if (is_delimiter(next)) {
                    unread_char(next);
                    /* Read cdr of dotted pair */
                    elem = read_expr();
                    if (!CL_NULL_P(tail)) {
                        ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = elem;
                    }
                    skip_whitespace();
                    ch = read_char();
                    if (ch != ')') {
                        CL_GC_UNPROTECT(2);
                        cl_reader_error(CL_ERR_PARSE, "Expected ')' after dotted pair");
                    }
                    CL_GC_UNPROTECT(2);
                    return head;
                }
                /* Not a dotted-pair `.` — the token starts with `.` followed
                 * by a non-delimiter character (e.g. `.5`, `.foo`).  The
                 * stream only has a single-slot unread buffer, so we cannot
                 * push back both `ch` and `next`.  Push back only `next` and
                 * seed the atom reader with the consumed `.`. */
                unread_char(next);
                elem = read_atom_with_prefix(".", 1);
                goto have_elem;
            } else {
                unread_char(ch);
            }
        }

        elem = read_expr();
    have_elem:
        if (elem == CL_READER_SKIP) continue;  /* #+ / #- skipped form */

        {
            CL_Obj cell = cl_cons(elem, CL_NIL);
            if (CL_NULL_P(head)) {
                head = cell;
                srcloc_record(head, start_line);
            } else {
                ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
            }
            tail = cell;
        }
    }
}

/* Read a single expression */
static CL_Obj read_expr(void)
{
    int ch;
    CL_Obj obj;

    skip_whitespace();
    if (eof_seen) return CL_NIL;

    ch = read_char();
    if (ch < 0) { eof_seen = 1; return CL_NIL; }

    /* Check readtable for user-defined macro function */
    if (ch >= 0 && ch < CL_RT_CHARS) {
        CL_Readtable *rt = cl_readtable_current();
        uint8_t syn = rt->syntax[ch];
        if ((syn == CL_CHAR_TERM_MACRO || syn == CL_CHAR_NONTERM_MACRO) &&
            !CL_NULL_P(rt->macro_fn[ch])) {
            /* User-defined macro: call (fn stream char) */
            CL_Obj args[2];
            args[0] = reader_stream;
            args[1] = CL_MAKE_CHAR(ch);
            return cl_vm_apply(rt->macro_fn[ch], args, 2);
        }
    }

    /* Built-in macro dispatch */
    switch (ch) {
    case '(':
        return read_list();

    case '\'': /* quote */
        obj = read_expr();
        return cl_cons(SYM_QUOTE, cl_cons(obj, CL_NIL));

    case '`': /* quasiquote */
        obj = read_expr();
        return cl_cons(SYM_QUASIQUOTE, cl_cons(obj, CL_NIL));

    case ',': /* unquote / unquote-splicing */
        ch = read_char();
        /* ,. is the destructive-splice variant of ,@ — ANSI CL permits
         * implementations to expand it identically to ,@ (the
         * difference is only whether APPEND or NCONC is used in the
         * expansion, and iterate relies on the form being accepted at
         * read time).  Treat them the same here. */
        if (ch == '@' || ch == '.') {
            obj = read_expr();
            return cl_cons(SYM_UNQUOTE_SPLICING, cl_cons(obj, CL_NIL));
        }
        unread_char(ch);
        obj = read_expr();
        return cl_cons(SYM_UNQUOTE, cl_cons(obj, CL_NIL));

    case '#': {
        int sub_ch = read_char();

        /* Check readtable for user-defined dispatch sub-character */
        if (sub_ch >= 0 && sub_ch < CL_RT_CHARS) {
            CL_Readtable *rt = cl_readtable_current();
            if (!CL_NULL_P(rt->dispatch_fn[sub_ch])) {
                /* User dispatch macro: call (fn stream sub-char nil) */
                CL_Obj args[3];
                args[0] = reader_stream;
                args[1] = CL_MAKE_CHAR(sub_ch);
                args[2] = CL_NIL; /* no numeric arg */
                return cl_vm_apply(rt->dispatch_fn[sub_ch], args, 3);
            }
        }

        /* Built-in dispatch macros */
        ch = sub_ch;
        if (ch == '|') {
            /* #|...|# block comment (nestable per CL spec) */
            int depth = 1;
            int prev = -1;
            while (depth > 0) {
                int c2 = read_char();
                if (c2 < 0) {
                    cl_reader_error(CL_ERR_PARSE, "Unterminated block comment #|...|#");
                    return CL_NIL;
                }
                if (prev == '#' && c2 == '|') {
                    depth++;
                    prev = -1;  /* reset to avoid #|| matching twice */
                    continue;
                }
                if (prev == '|' && c2 == '#') {
                    depth--;
                    prev = -1;
                    continue;
                }
                prev = c2;
            }
            return CL_READER_SKIP;
        }
        if (ch == '\'') {
            /* #'foo => (FUNCTION foo) */
            obj = read_expr();
            return cl_cons(SYM_FUNCTION, cl_cons(obj, CL_NIL));
        }
        if (ch == '\\') {
            /* #\x => character literal */
            ch = read_char();
            if (ch < 0) {
                cl_reader_error(CL_ERR_PARSE, "Unexpected EOF in character literal");
                return CL_NIL;
            }
            /* Check for named characters */
            if (isalpha(ch)) {
                char name[32];
                int nlen = 0;
                name[nlen++] = (char)ch;
                while (nlen < 31) {
                    ch = read_char();
                    if (is_delimiter(ch)) { unread_char(ch); break; }
                    name[nlen++] = (char)ch;
                }
                name[nlen] = '\0';
                if (nlen == 1) return CL_MAKE_CHAR(name[0]);
                /* Named characters — CL standard */
                if (strcasecmp(name, "Null") == 0) return CL_MAKE_CHAR('\0');
                if (strcasecmp(name, "Nul") == 0) return CL_MAKE_CHAR('\0');
                if (strcasecmp(name, "Space") == 0) return CL_MAKE_CHAR(' ');
                if (strcasecmp(name, "Newline") == 0) return CL_MAKE_CHAR('\n');
                if (strcasecmp(name, "Linefeed") == 0) return CL_MAKE_CHAR('\n');
                if (strcasecmp(name, "Tab") == 0) return CL_MAKE_CHAR('\t');
                if (strcasecmp(name, "Return") == 0) return CL_MAKE_CHAR('\r');
                if (strcasecmp(name, "Backspace") == 0) return CL_MAKE_CHAR('\b');
                if (strcasecmp(name, "Page") == 0) return CL_MAKE_CHAR('\f');
                if (strcasecmp(name, "Rubout") == 0) return CL_MAKE_CHAR(0x7F);
                /* Extended character names (common across implementations) */
                if (strcasecmp(name, "Escape") == 0 ||
                    strcasecmp(name, "Esc") == 0) return CL_MAKE_CHAR(0x1B);
                if (strcasecmp(name, "Vt") == 0) return CL_MAKE_CHAR(0x0B);
                if (strcasecmp(name, "Bell") == 0 ||
                    strcasecmp(name, "Bel") == 0) return CL_MAKE_CHAR(0x07);
                if (strcasecmp(name, "Delete") == 0 ||
                    strcasecmp(name, "Del") == 0) return CL_MAKE_CHAR(0x7F);
                if (strcasecmp(name, "Soh") == 0) return CL_MAKE_CHAR(0x01);
                if (strcasecmp(name, "Stx") == 0) return CL_MAKE_CHAR(0x02);
                if (strcasecmp(name, "Etx") == 0) return CL_MAKE_CHAR(0x03);
                if (strcasecmp(name, "Eot") == 0) return CL_MAKE_CHAR(0x04);
                if (strcasecmp(name, "Enq") == 0) return CL_MAKE_CHAR(0x05);
                if (strcasecmp(name, "Ack") == 0) return CL_MAKE_CHAR(0x06);
                if (strcasecmp(name, "So") == 0) return CL_MAKE_CHAR(0x0E);
                if (strcasecmp(name, "Si") == 0) return CL_MAKE_CHAR(0x0F);
                if (strcasecmp(name, "Dle") == 0) return CL_MAKE_CHAR(0x10);
                if (strcasecmp(name, "Dc1") == 0) return CL_MAKE_CHAR(0x11);
                if (strcasecmp(name, "Dc2") == 0) return CL_MAKE_CHAR(0x12);
                if (strcasecmp(name, "Dc3") == 0) return CL_MAKE_CHAR(0x13);
                if (strcasecmp(name, "Dc4") == 0) return CL_MAKE_CHAR(0x14);
                if (strcasecmp(name, "Nak") == 0) return CL_MAKE_CHAR(0x15);
                if (strcasecmp(name, "Syn") == 0) return CL_MAKE_CHAR(0x16);
                if (strcasecmp(name, "Etb") == 0) return CL_MAKE_CHAR(0x17);
                if (strcasecmp(name, "Can") == 0) return CL_MAKE_CHAR(0x18);
                if (strcasecmp(name, "Em") == 0) return CL_MAKE_CHAR(0x19);
                if (strcasecmp(name, "Sub") == 0) return CL_MAKE_CHAR(0x1A);
                if (strcasecmp(name, "Fs") == 0) return CL_MAKE_CHAR(0x1C);
                if (strcasecmp(name, "Gs") == 0) return CL_MAKE_CHAR(0x1D);
                if (strcasecmp(name, "Rs") == 0) return CL_MAKE_CHAR(0x1E);
                if (strcasecmp(name, "Us") == 0) return CL_MAKE_CHAR(0x1F);
                /* Unicode-named whitespace characters */
                if (strcasecmp(name, "No-Break_Space") == 0 ||
                    strcasecmp(name, "No-break_space") == 0 ||
                    strcasecmp(name, "NO-BREAK_SPACE") == 0) return CL_MAKE_CHAR(0xA0);
                if (strcasecmp(name, "Ideographic_Space") == 0 ||
                    strcasecmp(name, "Ideographic_space") == 0 ||
                    strcasecmp(name, "IDEOGRAPHIC_SPACE") == 0) return CL_MAKE_CHAR(0x3000);
                /* Hex codepoint: #\U+XXXX or #\u+XXXX */
                if ((name[0] == 'U' || name[0] == 'u') && name[1] == '+') {
                    char *endp;
                    unsigned long cp = strtoul(name + 2, &endp, 16);
                    if (*endp == '\0' && cp <= 0x10FFFF)
                        return CL_MAKE_CHAR((int)cp);
                }
                if (read_suppress) return CL_NIL;
                cl_reader_error(CL_ERR_PARSE, "Unknown character name: %s", name);
                return CL_NIL;
            }
            return CL_MAKE_CHAR(ch);
        }
        if (ch == ':') {
            /* #:sym — uninterned symbol.  Per CLHS 2.4.8.5, within a
             * single call to READ, all #:foo with the same name must
             * denote the *same* uninterned symbol.  We track this via
             * CT->rd_uninterned (alist of (name-string . symbol)),
             * saved/restored around each cl_read_from_stream call. */
            char sym_buf[256];
            int sym_len = 0;
            int ch2;
            CL_Obj name_str, new_sym, cell;
            while (sym_len < 255) {
                ch2 = read_char();
                if (is_delimiter(ch2)) { unread_char(ch2); break; }
                sym_buf[sym_len++] = (char)toupper(ch2);
            }
            sym_buf[sym_len] = '\0';
            if (sym_len == 0) {
                if (read_suppress) return CL_NIL;
                cl_reader_error(CL_ERR_PARSE, "Missing symbol name after #:");
            }
            /* Look up by name in the per-read alist. */
            {
                CL_Obj p = rd_uninterned;
                while (!CL_NULL_P(p)) {
                    CL_Obj pair = cl_car(p);
                    CL_Obj nm   = cl_car(pair);
                    if (CL_ANY_STRING_P(nm) &&
                        cl_string_length(nm) == (uint32_t)sym_len) {
                        uint32_t i;
                        int match = 1;
                        for (i = 0; i < (uint32_t)sym_len; i++) {
                            if ((char)cl_string_char_at(nm, i) != sym_buf[i]) {
                                match = 0; break;
                            }
                        }
                        if (match) return cl_cdr(pair);
                    }
                    p = cl_cdr(p);
                }
            }
            name_str = cl_make_string(sym_buf, (uint32_t)sym_len);
            CL_GC_PROTECT(name_str);
            new_sym = cl_make_uninterned_symbol(name_str);
            CL_GC_PROTECT(new_sym);
            cell = cl_cons(name_str, new_sym);
            CL_GC_PROTECT(cell);
            rd_uninterned = cl_cons(cell, rd_uninterned);
            CL_GC_UNPROTECT(3);
            return new_sym;
        }
        if (ch == '+' || ch == '-') {
            /* #+ / #- feature conditionals.
             * Per CL spec, feature expr is read with *package* = KEYWORD */
            CL_Obj saved_pkg = cl_current_package;
            CL_Obj feat_expr;
            int present;
            cl_current_package = cl_package_keyword;
            feat_expr = read_expr();
            cl_current_package = saved_pkg;
            present = eval_feature_expr(feat_expr);
            if (ch == '-') present = !present;
            if (present) {
                return read_expr();
            } else {
                skip_form();
                return CL_READER_SKIP;
            }
        }
        if (ch == '*') {
            /* #*0110 => simple bit vector */
            char bits[4096];
            int blen = 0;
            uint32_t bi;
            CL_Obj bvobj;
            CL_BitVector *bv;
            for (;;) {
                int c2 = read_char();
                if (c2 == '0' || c2 == '1') {
                    if (blen < 4095) bits[blen++] = (char)c2;
                } else {
                    if (c2 >= 0) unread_char(c2);
                    break;
                }
            }
            bvobj = cl_make_bit_vector((uint32_t)blen);
            bv = (CL_BitVector *)CL_OBJ_TO_PTR(bvobj);
            for (bi = 0; bi < (uint32_t)blen; bi++) {
                if (bits[bi] == '1')
                    bv->data[bi / 32] |= (1u << (bi % 32));
            }
            return bvobj;
        }
        if (ch == 'p' || ch == 'P') {
            /* #P"..." => pathname literal */
            CL_Obj path_str;
            extern CL_Obj cl_parse_namestring(const char *str, uint32_t len);
            skip_whitespace();
            ch = read_char();
            if (ch != '"') {
                if (read_suppress) { unread_char(ch); return CL_NIL; }
                cl_reader_error(CL_ERR_PARSE, "#P must be followed by a string");
                return CL_NIL;
            }
            path_str = read_string();
            {
                CL_String *s = (CL_String *)CL_OBJ_TO_PTR(path_str);
                return cl_parse_namestring(s->data, s->length);
            }
        }
        if (ch == 'c' || ch == 'C') {
            /* #C(real imag) => complex number */
            CL_Obj parts;
            if (read_suppress) {
                skip_whitespace();
                ch = read_char();
                if (ch == '(') { read_list(); }
                return CL_NIL;
            }
            skip_whitespace();
            ch = read_char();
            if (ch != '(') {
                cl_reader_error(CL_ERR_PARSE, "#C must be followed by (real imag)");
                return CL_NIL;
            }
            parts = read_list();
            if (CL_CONS_P(parts) && CL_CONS_P(cl_cdr(parts)) &&
                CL_NULL_P(cl_cdr(cl_cdr(parts)))) {
                CL_Obj real = cl_car(parts);
                CL_Obj imag = cl_car(cl_cdr(parts));
                if (!CL_REALP(real) || !CL_REALP(imag))
                    cl_reader_error(CL_ERR_PARSE, "#C components must be real numbers");
                return cl_make_complex(real, imag);
            }
            cl_reader_error(CL_ERR_PARSE, "#C requires exactly two elements: (real imag)");
            return CL_NIL;
        }
        if (ch == '(') {
            /* #(e1 e2 ...) => simple vector */
            CL_Obj elems = CL_NIL;
            CL_Obj tail = CL_NIL;
            uint32_t count = 0;
            uint32_t i;
            CL_Obj vec;
            CL_Vector *v;
            CL_Obj elem;

            CL_GC_PROTECT(elems);
            CL_GC_PROTECT(tail);

            for (;;) {
                skip_whitespace();
                if (eof_seen) {
                    CL_GC_UNPROTECT(2);
                    cl_reader_error(CL_ERR_PARSE, "Unterminated #( vector literal");
                    return CL_NIL;
                }
                {
                    int c2 = read_char();
                    if (c2 == ')') break;
                    unread_char(c2);
                }
                elem = read_expr();
                if (elem == CL_READER_SKIP) continue;
                {
                    CL_Obj cell = cl_cons(elem, CL_NIL);
                    if (CL_NULL_P(elems)) {
                        elems = cell;
                    } else {
                        ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
                    }
                    tail = cell;
                    count++;
                }
            }

            vec = cl_make_vector(count);
            CL_GC_UNPROTECT(2);
            v = (CL_Vector *)CL_OBJ_TO_PTR(vec);
            for (i = 0; i < count; i++) {
                cl_vector_data(v)[i] = cl_car(elems);
                elems = cl_cdr(elems);
            }
            return vec;
        }
        if (ch == '.') {
            /* #. read-time eval */
            CL_Obj form = read_expr();
            CL_Obj bytecode, result;
            CL_Symbol *re_sym;

            if (read_suppress) return CL_NIL;

            /* Check *read-eval* */
            (void)re_sym;
            if (CL_NULL_P(cl_symbol_value(SYM_STAR_READ_EVAL)))
                cl_reader_error(CL_ERR_GENERAL, "#. disabled: *READ-EVAL* is NIL");

            CL_GC_PROTECT(form);
            bytecode = cl_compile(form);
            if (!CL_NULL_P(bytecode))
                result = cl_vm_eval(bytecode);
            else
                result = CL_NIL;
            CL_GC_UNPROTECT(1);
            return result;
        }
        if (ch == 'X' || ch == 'x') {
            return read_radix_number(16);
        }
        if (ch == 'B' || ch == 'b') {
            return read_radix_number(2);
        }
        if (ch == 'O' || ch == 'o') {
            return read_radix_number(8);
        }
        /* #nR — arbitrary radix, #nA — multi-dimensional array */
        if (ch >= '0' && ch <= '9') {
            int num_val = ch - '0';
            while (1) {
                ch = read_char();
                if (ch == 'R' || ch == 'r') break;
                if (ch == 'A' || ch == 'a') {
                    /* #nA(...) — n-dimensional array */
                    int rank = num_val;
                    CL_Obj contents = read_expr();
                    uint32_t dims[8];
                    uint32_t total = 1;
                    int d;
                    CL_Obj arr;
                    CL_Vector *vp;
                    CL_Obj flat_elems = CL_NIL;
                    CL_Obj flat_tail = CL_NIL;

                    if (read_suppress) return CL_NIL;
                    if (rank < 0 || rank > 8) {
                        cl_reader_error(CL_ERR_PARSE, "#%dA: rank must be 0-8", rank);
                        return CL_NIL;
                    }

                    CL_GC_PROTECT(contents);
                    CL_GC_PROTECT(flat_elems);
                    CL_GC_PROTECT(flat_tail);

                    if (rank == 0) {
                        /* #0A datum — 0-dimensional array containing datum */
                        arr = cl_make_array(1, 0, NULL, 0, CL_NO_FILL_POINTER);
                        vp = (CL_Vector *)CL_OBJ_TO_PTR(arr);
                        cl_vector_data(vp)[0] = contents;
                        CL_GC_UNPROTECT(3);
                        return arr;
                    }

                    /* Derive dimensions from nested list structure */
                    {
                        CL_Obj probe = contents;
                        for (d = 0; d < rank; d++) {
                            if (CL_CONS_P(probe)) {
                                uint32_t len = 0;
                                CL_Obj p = probe;
                                while (CL_CONS_P(p)) { len++; p = cl_cdr(p); }
                                dims[d] = len;
                                probe = cl_car(probe);
                            } else {
                                dims[d] = 0;
                            }
                        }
                    }
                    for (d = 0; d < rank; d++) total *= dims[d];

                    /* Recursively flatten nested lists into flat_elems */
                    {
                        /* Use a simple iterative approach with a work stack */
                        /* For simplicity, we flatten recursively for depth = rank */
                        CL_Obj work[8]; /* One per dimension level */
                        int level = 0;
                        uint32_t fi = 0;
                        work[0] = contents;
                        while (fi < total) {
                            if (level == rank - 1) {
                                /* Innermost: collect elements from list */
                                CL_Obj lst = work[level];
                                while (CL_CONS_P(lst) && fi < total) {
                                    CL_Obj cell = cl_cons(cl_car(lst), CL_NIL);
                                    if (CL_NULL_P(flat_elems)) {
                                        flat_elems = cell;
                                    } else {
                                        ((CL_Cons *)CL_OBJ_TO_PTR(flat_tail))->cdr = cell;
                                    }
                                    flat_tail = cell;
                                    fi++;
                                    lst = cl_cdr(lst);
                                }
                                /* Go back up */
                                level--;
                                if (level < 0) break;
                                work[level] = cl_cdr(work[level]);
                            } else {
                                /* Descend into sublists */
                                if (CL_CONS_P(work[level])) {
                                    work[level + 1] = cl_car(work[level]);
                                    level++;
                                } else {
                                    level--;
                                    if (level < 0) break;
                                    work[level] = cl_cdr(work[level]);
                                }
                            }
                        }
                    }

                    arr = cl_make_array(total, (uint8_t)rank, dims, 0,
                                        CL_NO_FILL_POINTER);
                    CL_GC_UNPROTECT(3);
                    vp = (CL_Vector *)CL_OBJ_TO_PTR(arr);
                    {
                        uint32_t i;
                        CL_Obj *data = cl_vector_data(vp);
                        CL_Obj p = flat_elems;
                        for (i = 0; i < total; i++) {
                            if (CL_CONS_P(p)) {
                                data[i] = cl_car(p);
                                p = cl_cdr(p);
                            } else {
                                data[i] = CL_NIL;
                            }
                        }
                    }
                    return arr;
                }
                if (ch >= '0' && ch <= '9') {
                    num_val = num_val * 10 + (ch - '0');
                } else {
                    cl_reader_error(CL_ERR_PARSE, "Invalid radix prefix #%d%c", num_val, ch);
                    return CL_NIL;
                }
            }
            if (num_val < 2 || num_val > 36) {
                cl_reader_error(CL_ERR_PARSE, "Radix %d out of range (2-36)", num_val);
                return CL_NIL;
            }
            return read_radix_number(num_val);
        }
        if (read_suppress) return CL_NIL;
        cl_reader_error(CL_ERR_PARSE, "Unknown dispatch macro: #%c", ch);
        return CL_NIL;
    }

    case '"':
        return read_string();

    case ')':
        cl_reader_error(CL_ERR_PARSE, "Unexpected ')'");
        return CL_NIL;

    default:
        unread_char(ch);
        return read_atom_with_prefix(NULL, 0);
    }
}

/* Public API */

void cl_reader_reset_line(void)
{
    reader_line = 1;
}

int cl_reader_get_line(void)
{
    return reader_line;
}

void cl_reader_set_line(int line)
{
    reader_line = line;
}

CL_Obj cl_read(void)
{
    CL_Obj result;
    CL_Obj saved_uninterned = rd_uninterned;
    reader_stream = cl_stdin_stream;
    reader_line = 1;
    eof_seen = 0;
    rd_uninterned = CL_NIL;
    do { result = read_expr(); } while (result == CL_READER_SKIP && !eof_seen);
    CT->rd_last_eof = eof_seen;
    rd_uninterned = saved_uninterned;
    return result;
}

CL_Obj cl_read_from_stream(CL_Obj stream)
{
    /* Save/restore reader state so nested reads (e.g. read-from-string
     * called from inside a dispatch-macro function that is itself running
     * during a parent read) don't leak state back to the parent reader.
     * The innermost EOF is preserved in rd_last_eof for cl_reader_eof().
     *
     * Line numbers persist *per stream*: the stream's `line` field stores
     * the cumulative line position so repeated top-level reads on the same
     * stream (e.g. LOAD) report real source lines, not always-1. */
    CL_Obj saved_stream      = reader_stream;
    int    saved_eof         = eof_seen;
    int    saved_line        = reader_line;
    CL_Obj saved_uninterned  = rd_uninterned;
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    CL_Obj result;

    reader_stream = stream;
    eof_seen = 0;
    reader_line = st->line ? (int)st->line : 1;
    rd_uninterned = CL_NIL;
    do { result = read_expr(); } while (result == CL_READER_SKIP && !eof_seen);

    st->line = (uint32_t)reader_line;
    CT->rd_last_eof = eof_seen;
    reader_stream = saved_stream;
    reader_line   = saved_line;
    eof_seen      = saved_eof;
    rd_uninterned = saved_uninterned;
    return result;
}

CL_Obj cl_read_from_string(CL_ReadStream *stream)
{
    CL_Obj saved_stream      = reader_stream;
    int    saved_eof         = eof_seen;
    int    saved_line        = reader_line;
    CL_Obj saved_uninterned  = rd_uninterned;
    CL_Obj str, s;
    CL_Stream *st;
    CL_Obj result;

    str = cl_make_string(stream->buf, (uint32_t)stream->len);
    CL_GC_PROTECT(str);
    s = cl_make_string_input_stream(str, (uint32_t)stream->pos, (uint32_t)stream->len);
    CL_GC_UNPROTECT(1);

    reader_stream = s;
    reader_line = stream->line ? stream->line : 1;
    eof_seen = 0;
    rd_uninterned = CL_NIL;

    do { result = read_expr(); } while (result == CL_READER_SKIP && !eof_seen);

    /* Sync position back */
    st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    stream->pos = (int)st->position;
    stream->line = reader_line;

    CT->rd_last_eof = eof_seen;
    reader_stream = saved_stream;
    reader_line   = saved_line;
    eof_seen      = saved_eof;
    rd_uninterned = saved_uninterned;
    return result;
}

int cl_reader_eof(void)
{
    /* Reflect the most recently completed top-level/embedded read.
     * When a nested reader invocation runs (e.g. read-from-string
     * inside a dispatch-macro), the saved eof_seen is restored on
     * return — rd_last_eof captures the innermost read's EOF state
     * so bi_read can still detect end-of-file on its own call. */
    return CT->rd_last_eof;
}

void cl_reader_init(void)
{
    memset(cl_srcloc_table, 0, sizeof(cl_srcloc_table));
    reader_line = 1;
    cl_current_source_file = NULL;
    cl_current_file_id = 0;
    cl_readtable_init();
}
