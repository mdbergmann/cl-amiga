#include "reader.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "bignum.h"
#include "float.h"
#include "error.h"
#include "stream.h"
#include "../platform/platform.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

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

/* Current stream being read from */
static CL_Obj reader_stream = 0;
static int eof_seen = 0;

/* Source location tracking */
CL_SrcLoc cl_srcloc_table[CL_SRCLOC_SIZE];
const char *cl_current_source_file = NULL;
uint16_t cl_current_file_id = 0;
static int reader_line = 1;  /* Line counter for reads */

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
    return ch < 0 || isspace(ch) || ch == '(' || ch == ')' ||
           ch == '"' || ch == ';' || ch == '\'' || ch == '`' || ch == ',';
}

/* Forward declaration */
static CL_Obj read_expr(void);

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

/* Read an atom (number, symbol, keyword) */
static CL_Obj read_atom(void)
{
    char buf[256];
    int len = 0;
    int ch;
    int is_number = 1;
    int has_digit = 0;
    int i;

    while (len < 255) {
        ch = read_char();
        if (is_delimiter(ch)) {
            unread_char(ch);
            break;
        }
        buf[len++] = (char)toupper(ch);
    }
    buf[len] = '\0';

    if (len == 0) {
        cl_error(CL_ERR_PARSE, "Unexpected end of input");
        return CL_NIL;
    }

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

            if (sym_len <= 0)
                cl_error(CL_ERR_PARSE, "Missing symbol name after package qualifier");

            memcpy(pkg_name, buf, (uint32_t)colon_pos);
            pkg_name[colon_pos] = '\0';
            memcpy(sym_name, buf + sym_start, (uint32_t)sym_len);
            sym_name[sym_len] = '\0';

            package = cl_find_package(pkg_name, (uint32_t)colon_pos);
            if (CL_NULL_P(package))
                cl_error(CL_ERR_PARSE, "Package %s not found", pkg_name);

            if (double_colon) {
                /* pkg::sym — intern as internal symbol */
                return cl_intern_in(sym_name, (uint32_t)sym_len, package);
            } else {
                /* pkg:sym — look up external symbol only */
                CL_Obj sym = cl_package_find_external(sym_name, (uint32_t)sym_len, package);
                if (CL_NULL_P(sym))
                    cl_error(CL_ERR_PARSE, "Symbol %s not exported from %s",
                             sym_name, pkg_name);
                return sym;
            }
        }
    }

    /* Check for NIL */
    if (len == 3 && buf[0] == 'N' && buf[1] == 'I' && buf[2] == 'L') {
        return CL_NIL;
    }

    /* Regular symbol */
    return cl_intern(buf, (uint32_t)len);
}

/* Read a string literal */
static CL_Obj read_string(void)
{
    char buf[1024];
    int len = 0;
    int ch;

    for (;;) {
        ch = read_char();
        if (ch < 0) {
            cl_error(CL_ERR_PARSE, "Unterminated string");
            return CL_NIL;
        }
        if (ch == '"') break;
        if (ch == '\\') {
            ch = read_char();
            if (ch < 0) {
                cl_error(CL_ERR_PARSE, "Unterminated string escape");
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
        if (len < 1023) buf[len++] = (char)ch;
    }
    buf[len] = '\0';
    return cl_make_string(buf, (uint32_t)len);
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
            cl_error(CL_ERR_PARSE, "Unterminated list");
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
                        cl_error(CL_ERR_PARSE, "Expected ')' after dotted pair");
                    }
                    CL_GC_UNPROTECT(2);
                    return head;
                }
                /* Not a dot — put back both chars and read as atom */
                unread_char(next);
                unread_char(ch);
            } else {
                unread_char(ch);
            }
        }

        elem = read_expr();

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
        if (ch == '@') {
            obj = read_expr();
            return cl_cons(SYM_UNQUOTE_SPLICING, cl_cons(obj, CL_NIL));
        }
        unread_char(ch);
        obj = read_expr();
        return cl_cons(SYM_UNQUOTE, cl_cons(obj, CL_NIL));

    case '#':
        ch = read_char();
        if (ch == '\'') {
            /* #'foo => (FUNCTION foo) */
            obj = read_expr();
            return cl_cons(SYM_FUNCTION, cl_cons(obj, CL_NIL));
        }
        if (ch == '\\') {
            /* #\x => character literal */
            ch = read_char();
            if (ch < 0) {
                cl_error(CL_ERR_PARSE, "Unexpected EOF in character literal");
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
                /* Named characters */
                if (strcasecmp(name, "Space") == 0) return CL_MAKE_CHAR(' ');
                if (strcasecmp(name, "Newline") == 0) return CL_MAKE_CHAR('\n');
                if (strcasecmp(name, "Tab") == 0) return CL_MAKE_CHAR('\t');
                if (strcasecmp(name, "Return") == 0) return CL_MAKE_CHAR('\r');
                cl_error(CL_ERR_PARSE, "Unknown character name: %s", name);
                return CL_NIL;
            }
            return CL_MAKE_CHAR(ch);
        }
        if (ch == ':') {
            /* #:sym — uninterned symbol */
            char sym_buf[256];
            int sym_len = 0;
            int ch2;
            CL_Obj name_str;
            while (sym_len < 255) {
                ch2 = read_char();
                if (is_delimiter(ch2)) { unread_char(ch2); break; }
                sym_buf[sym_len++] = (char)toupper(ch2);
            }
            sym_buf[sym_len] = '\0';
            if (sym_len == 0)
                cl_error(CL_ERR_PARSE, "Missing symbol name after #:");
            name_str = cl_make_string(sym_buf, (uint32_t)sym_len);
            return cl_make_uninterned_symbol(name_str);
        }
        cl_error(CL_ERR_PARSE, "Unknown dispatch macro: #%c", ch);
        return CL_NIL;

    case '"':
        return read_string();

    case ')':
        cl_error(CL_ERR_PARSE, "Unexpected ')'");
        return CL_NIL;

    default:
        unread_char(ch);
        return read_atom();
    }
}

/* Public API */

CL_Obj cl_read(void)
{
    reader_stream = cl_stdin_stream;
    reader_line = 1;
    eof_seen = 0;
    return read_expr();
}

CL_Obj cl_read_from_stream(CL_Obj stream)
{
    reader_stream = stream;
    eof_seen = 0;
    return read_expr();
}

CL_Obj cl_read_from_string(CL_ReadStream *stream)
{
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

    result = read_expr();

    /* Sync position back */
    st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    stream->pos = (int)st->position;
    stream->line = reader_line;
    return result;
}

int cl_reader_eof(void)
{
    return eof_seen;
}

void cl_reader_init(void)
{
    memset(cl_srcloc_table, 0, sizeof(cl_srcloc_table));
    reader_line = 1;
    cl_current_source_file = NULL;
    cl_current_file_id = 0;
}
