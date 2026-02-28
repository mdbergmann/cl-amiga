#include "reader.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "../platform/platform.h"
#include <string.h>
#include <ctype.h>

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

/* Reading from console vs string buffer */
static int use_stream = 0;
static CL_ReadStream *current_stream = NULL;
static int eof_seen = 0;

static int read_char(void)
{
    if (use_stream) {
        if (!current_stream || current_stream->pos >= current_stream->len)
            return -1;
        return (unsigned char)current_stream->buf[current_stream->pos++];
    }
    return platform_getchar();
}

static void unread_char(int ch)
{
    if (ch < 0) return;
    if (use_stream) {
        if (current_stream && current_stream->pos > 0)
            current_stream->pos--;
        return;
    }
    platform_ungetchar(ch);
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
        long val = 0;
        int neg = 0;
        i = 0;
        if (buf[0] == '-') { neg = 1; i = 1; }
        else if (buf[0] == '+') { i = 1; }
        for (; i < len; i++) {
            val = val * 10 + (buf[i] - '0');
        }
        if (neg) val = -val;
        if (val > CL_FIXNUM_MAX || val < CL_FIXNUM_MIN) {
            cl_error(CL_ERR_OVERFLOW, "Integer overflow: %s", buf);
        }
        return CL_MAKE_FIXNUM((int32_t)val);
    }

    /* Check for keyword */
    if (buf[0] == ':') {
        return cl_intern_keyword(buf + 1, (uint32_t)(len - 1));
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

/* Read a list */
static CL_Obj read_list(void)
{
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
    use_stream = 0;
    current_stream = NULL;
    eof_seen = 0;
    return read_expr();
}

CL_Obj cl_read_from_string(CL_ReadStream *stream)
{
    use_stream = 1;
    current_stream = stream;
    eof_seen = 0;
    return read_expr();
}

void cl_reader_init(void)
{
    /* Nothing needed yet */
}
