/*
 * builtins_format.c — Advanced FORMAT directive implementation
 *
 * Supports: ~A ~S ~W ~D ~B ~O ~X ~C ~% ~& ~| ~~ ~R ~T ~* ~^ ~? ~(~) ~[~;~] ~{~}
 * With prefix parameters, colon/at-sign modifiers, padding, commas, sign.
 */

#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "printer.h"
#include "stream.h"
#include "float.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Data Structures
 * ================================================================ */

#define FMT_MAX_PARAMS 4
#define FMT_PARAM_UNSET (-1)

typedef struct {
    int32_t params[FMT_MAX_PARAMS];
    int     param_given[FMT_MAX_PARAMS];
    int     n_params;
    int     colon;    /* : modifier */
    int     atsign;   /* @ modifier */
    char    directive; /* uppercase */
} FmtDirective;

typedef struct {
    CL_Obj       stream;
    const char  *fmt;       /* full format string */
    const char  *pos;       /* current scan position */
    CL_Obj      *args;
    int          nargs;
    int          ai;        /* current arg index */
    int          escape;    /* ~^ triggered */
} FmtCtx;

/* Forward declarations */
static void fmt_dispatch(FmtCtx *ctx, FmtDirective *d);
static void fmt_run(FmtCtx *ctx);

/* ================================================================
 * Parsing
 * ================================================================ */

/* Consume next format arg. Returns CL_NIL if exhausted. */
static CL_Obj fmt_next_arg(FmtCtx *ctx)
{
    if (ctx->ai < ctx->nargs)
        return ctx->args[ctx->ai++];
    return CL_NIL;
}

/* Peek at next arg without consuming */
static CL_Obj fmt_peek_arg(FmtCtx *ctx)
{
    if (ctx->ai < ctx->nargs)
        return ctx->args[ctx->ai];
    return CL_NIL;
}

/* Remaining arg count */
static int fmt_args_remaining(FmtCtx *ctx)
{
    return ctx->nargs - ctx->ai;
}

/*
 * Parse a format directive starting at ctx->pos (which points just past '~').
 * After return, ctx->pos points past the directive character.
 */
static void fmt_parse_directive(FmtCtx *ctx, FmtDirective *d)
{
    const char *p = ctx->pos;
    int pi = 0;
    int i;

    for (i = 0; i < FMT_MAX_PARAMS; i++) {
        d->params[i] = 0;
        d->param_given[i] = 0;
    }
    d->n_params = 0;
    d->colon = 0;
    d->atsign = 0;
    d->directive = 0;

    /* Parse prefix parameters: integers, 'char, V, # separated by commas */
    for (;;) {
        if (*p >= '0' && *p <= '9') {
            /* Decimal integer */
            int32_t val = 0;
            while (*p >= '0' && *p <= '9') {
                val = val * 10 + (*p - '0');
                p++;
            }
            if (pi < FMT_MAX_PARAMS) {
                d->params[pi] = val;
                d->param_given[pi] = 1;
            }
            pi++;
        } else if (*p == '+' || *p == '-') {
            /* Signed integer */
            int sign = (*p == '-') ? -1 : 1;
            int32_t val = 0;
            p++;
            while (*p >= '0' && *p <= '9') {
                val = val * 10 + (*p - '0');
                p++;
            }
            if (pi < FMT_MAX_PARAMS) {
                d->params[pi] = val * sign;
                d->param_given[pi] = 1;
            }
            pi++;
        } else if (*p == '\'') {
            /* Character literal */
            p++;
            if (*p) {
                if (pi < FMT_MAX_PARAMS) {
                    d->params[pi] = (int32_t)(unsigned char)*p;
                    d->param_given[pi] = 1;
                }
                pi++;
                p++;
            }
        } else if (*p == 'V' || *p == 'v') {
            /* V: consume next arg as parameter */
            CL_Obj arg = fmt_next_arg(ctx);
            if (pi < FMT_MAX_PARAMS) {
                if (CL_FIXNUM_P(arg)) {
                    d->params[pi] = CL_FIXNUM_VAL(arg);
                    d->param_given[pi] = 1;
                } else if (CL_CHAR_P(arg)) {
                    d->params[pi] = CL_CHAR_VAL(arg);
                    d->param_given[pi] = 1;
                }
                /* NIL arg = param not given */
            }
            pi++;
            p++;
        } else if (*p == '#') {
            /* #: remaining arg count */
            if (pi < FMT_MAX_PARAMS) {
                d->params[pi] = fmt_args_remaining(ctx);
                d->param_given[pi] = 1;
            }
            pi++;
            p++;
        } else if (*p == ',') {
            /* Empty parameter slot (or separator) */
            if (pi == 0 || (pi > 0 && *(p-1) == ',')) {
                /* Empty slot before comma or consecutive commas */
                pi++;
            }
            p++;
            continue;
        } else {
            break;
        }
        /* After a parameter, check for comma separator */
        if (*p == ',') {
            p++;
        } else {
            break;
        }
    }
    d->n_params = pi;

    /* Parse colon and at-sign modifiers (in any order) */
    while (*p == ':' || *p == '@') {
        if (*p == ':') d->colon = 1;
        if (*p == '@') d->atsign = 1;
        p++;
    }

    /* The directive character */
    if (*p) {
        d->directive = (*p >= 'a' && *p <= 'z') ? (*p - 32) : *p;
        p++;
    }

    ctx->pos = p;
}

/*
 * Find matching close directive, collecting separator positions.
 * Handles nesting of the same open/close pair.
 *
 * `p` points just past the opening directive (e.g., past '(' in ~().
 * `open` and `close` are uppercase directive chars (e.g., '(' and ')').
 * `seps` array receives positions of ~; separators at nesting level 0.
 * `n_seps` receives the count. `max_seps` is array capacity.
 *
 * Returns pointer to the character just past the close directive, or NULL.
 */
static const char *fmt_find_close(const char *p, char open, char close,
                                  const char **seps, int *n_seps, int max_seps)
{
    int depth = 1;
    *n_seps = 0;

    while (*p && depth > 0) {
        if (*p == '~') {
            const char *start = p;
            p++;
            /* Skip prefix params */
            while (*p && ((*p >= '0' && *p <= '9') || *p == ',' || *p == '\'' ||
                          *p == '+' || *p == '-' || *p == 'V' || *p == 'v' ||
                          *p == '#')) {
                if (*p == '\'') { p++; if (*p) p++; }
                else p++;
            }
            /* Skip modifiers */
            while (*p == ':' || *p == '@') p++;

            if (*p) {
                char dc = (*p >= 'a' && *p <= 'z') ? (*p - 32) : *p;
                if (dc == open) {
                    depth++;
                } else if (dc == close) {
                    depth--;
                    if (depth == 0) {
                        p++;
                        return p;
                    }
                } else if (dc == ';' && depth == 1) {
                    /* Record separator — `start` points to the '~' before ';' */
                    if (*n_seps < max_seps) {
                        seps[*n_seps] = start;
                        (*n_seps)++;
                    }
                }
                p++;
            }
        } else {
            p++;
        }
    }
    return NULL; /* no matching close found */
}

/* Helper: get param with default */
static int32_t fmt_param(FmtDirective *d, int idx, int32_t defval)
{
    if (idx < d->n_params && d->param_given[idx])
        return d->params[idx];
    return defval;
}

/* ================================================================
 * Integer formatting with padding/commas/sign
 * ================================================================ */

/* Render an integer to a stack buffer in the given base.
 * Returns pointer into buf (may not be buf[0]).
 * `len_out` receives the string length. */
static char *render_integer(CL_Obj obj, int32_t base, char *buf, int bufsz,
                            int *len_out)
{
    /* Use printer to render in given base */
    CL_Obj prev_b = cl_symbol_value(SYM_PRINT_BASE);
    CL_Obj prev_x = cl_symbol_value(SYM_PRINT_RADIX);
    int len;

    cl_set_symbol_value(SYM_PRINT_BASE, CL_MAKE_FIXNUM(base));
    cl_set_symbol_value(SYM_PRINT_RADIX, CL_NIL);
    len = cl_princ_to_string(obj, buf, bufsz);
    cl_set_symbol_value(SYM_PRINT_BASE, prev_b);
    cl_set_symbol_value(SYM_PRINT_RADIX, prev_x);
    *len_out = len;
    return buf;
}

/*
 * Format a padded integer: handles mincol, padchar, commachar, comma-interval,
 * colon (commas), atsign (sign).
 */
static void fmt_padded_integer(FmtCtx *ctx, FmtDirective *d, int32_t base)
{
    CL_Obj arg = fmt_next_arg(ctx);
    char raw[128];
    char with_commas[192];
    int raw_len;
    int32_t mincol   = fmt_param(d, 0, 0);
    int32_t padchar  = fmt_param(d, 1, (int32_t)' ');
    int32_t commachar = fmt_param(d, 2, (int32_t)',');
    int32_t comma_int = fmt_param(d, 3, 3);
    const char *digits;
    int dlen;
    int negative;
    int final_len;
    int pad;
    int i;

    /* If not an integer, just princ it */
    if (!CL_INTEGER_P(arg)) {
        cl_princ_to_stream(arg, ctx->stream);
        return;
    }

    render_integer(arg, base, raw, sizeof(raw), &raw_len);

    /* Separate sign from digits */
    negative = (raw[0] == '-');
    digits = negative ? raw + 1 : raw;
    dlen = negative ? raw_len - 1 : raw_len;

    /* Insert commas if :modifier */
    if (d->colon && comma_int > 0 && dlen > comma_int) {
        int n_commas = (dlen - 1) / comma_int;
        int out_len = dlen + n_commas;
        int si = dlen - 1;
        int di = out_len - 1;
        int count = 0;
        if (out_len >= (int)sizeof(with_commas))
            out_len = (int)sizeof(with_commas) - 1;
        with_commas[out_len] = '\0';
        for (; si >= 0 && di >= 0; si--, di--) {
            with_commas[di] = digits[si];
            count++;
            if (count == comma_int && si > 0 && di > 0) {
                di--;
                with_commas[di] = (char)commachar;
                count = 0;
            }
        }
        digits = with_commas + di + (di >= 0 ? 0 : 1);
        /* Recalculate — point to start */
        if (di < 0) di = 0;
        digits = with_commas;
        /* Actually let's just recalculate properly */
        {
            int wi = 0, ri = 0;
            for (ri = 0; ri < dlen; ri++) {
                if (ri > 0 && ((dlen - ri) % comma_int == 0))
                    with_commas[wi++] = (char)commachar;
                with_commas[wi++] = (negative ? raw[ri + 1] : raw[ri]);
            }
            with_commas[wi] = '\0';
            digits = with_commas;
            dlen = wi;
        }
    }

    /* Compute final length with sign */
    final_len = dlen;
    if (negative || d->atsign) final_len++;

    /* Pad */
    pad = (mincol > final_len) ? (int)(mincol - final_len) : 0;
    for (i = 0; i < pad; i++)
        cl_stream_write_char(ctx->stream, (int)padchar);

    /* Sign */
    if (negative)
        cl_stream_write_char(ctx->stream, '-');
    else if (d->atsign)
        cl_stream_write_char(ctx->stream, '+');

    /* Digits */
    cl_stream_write_string(ctx->stream, digits, (uint32_t)dlen);
}

/* ================================================================
 * Object formatting with padding (for ~A, ~S)
 * ================================================================ */

static void fmt_padded_obj(FmtCtx *ctx, FmtDirective *d, int escape)
{
    CL_Obj arg = fmt_next_arg(ctx);
    int32_t mincol = fmt_param(d, 0, 0);
    int32_t colinc = fmt_param(d, 1, 1);
    int32_t minpad = fmt_param(d, 2, 0);
    int32_t padchar = fmt_param(d, 3, (int32_t)' ');
    char buf[512];
    int len;
    int total_pad;
    int i;

    if (escape)
        len = cl_prin1_to_string(arg, buf, sizeof(buf));
    else
        len = cl_princ_to_string(arg, buf, sizeof(buf));

    /* Calculate padding needed */
    total_pad = minpad;
    if (mincol > len + total_pad) {
        int extra = (int)(mincol - len - total_pad);
        if (colinc > 1)
            extra = ((extra + colinc - 1) / colinc) * colinc;
        total_pad += extra;
    }

    if (d->atsign) {
        /* @A/@S: pad on left (right-justify) */
        for (i = 0; i < total_pad; i++)
            cl_stream_write_char(ctx->stream, (int)padchar);
        cl_stream_write_string(ctx->stream, buf, (uint32_t)len);
    } else {
        /* ~A/~S: pad on right (left-justify) */
        cl_stream_write_string(ctx->stream, buf, (uint32_t)len);
        for (i = 0; i < total_pad; i++)
            cl_stream_write_char(ctx->stream, (int)padchar);
    }
}

/* ================================================================
 * ~R — Radix printing (cardinal, ordinal, Roman)
 * ================================================================ */

static const char *cardinal_ones[] = {
    "", "one", "two", "three", "four", "five", "six", "seven",
    "eight", "nine", "ten", "eleven", "twelve", "thirteen",
    "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"
};

static const char *cardinal_tens[] = {
    "", "", "twenty", "thirty", "forty", "fifty",
    "sixty", "seventy", "eighty", "ninety"
};

static const char *ordinal_ones[] = {
    "", "first", "second", "third", "fourth", "fifth", "sixth", "seventh",
    "eighth", "ninth", "tenth", "eleventh", "twelfth", "thirteenth",
    "fourteenth", "fifteenth", "sixteenth", "seventeenth", "eighteenth",
    "nineteenth"
};

static const char *ordinal_tens[] = {
    "", "", "twentieth", "thirtieth", "fortieth", "fiftieth",
    "sixtieth", "seventieth", "eightieth", "ninetieth"
};

/* Write cardinal English for 1-999 to stream.
 * Returns number of chars written (for tracking). */
static void fmt_cardinal_chunk(FmtCtx *ctx, int32_t n)
{
    if (n >= 100) {
        cl_stream_write_string(ctx->stream, cardinal_ones[n / 100],
                               (uint32_t)strlen(cardinal_ones[n / 100]));
        cl_stream_write_string(ctx->stream, " hundred", 8);
        n %= 100;
        if (n > 0) cl_stream_write_char(ctx->stream, ' ');
    }
    if (n >= 20) {
        cl_stream_write_string(ctx->stream, cardinal_tens[n / 10],
                               (uint32_t)strlen(cardinal_tens[n / 10]));
        n %= 10;
        if (n > 0) {
            cl_stream_write_char(ctx->stream, '-');
            cl_stream_write_string(ctx->stream, cardinal_ones[n],
                                   (uint32_t)strlen(cardinal_ones[n]));
        }
    } else if (n > 0) {
        cl_stream_write_string(ctx->stream, cardinal_ones[n],
                               (uint32_t)strlen(cardinal_ones[n]));
    }
}

static void fmt_cardinal(FmtCtx *ctx, int32_t n)
{
    int started = 0;

    if (n < 0) {
        cl_stream_write_string(ctx->stream, "negative ", 9);
        n = -n;
    }
    if (n == 0) {
        cl_stream_write_string(ctx->stream, "zero", 4);
        return;
    }

    if (n >= 1000000000) {
        fmt_cardinal_chunk(ctx, n / 1000000000);
        cl_stream_write_string(ctx->stream, " billion", 8);
        n %= 1000000000;
        started = 1;
    }
    if (n >= 1000000) {
        if (started) cl_stream_write_char(ctx->stream, ' ');
        fmt_cardinal_chunk(ctx, n / 1000000);
        cl_stream_write_string(ctx->stream, " million", 8);
        n %= 1000000;
        started = 1;
    }
    if (n >= 1000) {
        if (started) cl_stream_write_char(ctx->stream, ' ');
        fmt_cardinal_chunk(ctx, n / 1000);
        cl_stream_write_string(ctx->stream, " thousand", 9);
        n %= 1000;
        started = 1;
    }
    if (n > 0) {
        if (started) cl_stream_write_char(ctx->stream, ' ');
        fmt_cardinal_chunk(ctx, n);
    }
}

static void fmt_ordinal(FmtCtx *ctx, int32_t n)
{
    int32_t last_two;

    if (n < 0) {
        cl_stream_write_string(ctx->stream, "negative ", 9);
        n = -n;
    }
    if (n == 0) {
        cl_stream_write_string(ctx->stream, "zeroth", 6);
        return;
    }

    last_two = n % 100;

    /* Print all but the last part as cardinal */
    if (n >= 100) {
        int32_t prefix = n - last_two;
        if (last_two == 0) {
            /* The whole number ends in a "hundred"/"thousand" etc. */
            /* We need to figure out the ordinal suffix */
            if (n % 1000000000 == 0) {
                fmt_cardinal(ctx, n / 1000000000);
                cl_stream_write_string(ctx->stream, " billionth", 10);
                return;
            } else if (n % 1000000 == 0) {
                fmt_cardinal(ctx, n / 1000000);
                cl_stream_write_string(ctx->stream, " millionth", 10);
                return;
            } else if (n % 1000 == 0) {
                fmt_cardinal(ctx, n / 1000);
                cl_stream_write_string(ctx->stream, " thousandth", 11);
                return;
            } else if (n % 100 == 0) {
                int32_t above = n - (n % 1000);
                if (above > 0) {
                    fmt_cardinal(ctx, above);
                    cl_stream_write_char(ctx->stream, ' ');
                }
                fmt_cardinal_chunk(ctx, (n % 1000) / 100);
                cl_stream_write_string(ctx->stream, " hundredth", 10);
                return;
            }
        }
        /* Print prefix as cardinal, then ordinal suffix for last_two */
        fmt_cardinal(ctx, prefix);
        cl_stream_write_char(ctx->stream, ' ');
    }

    /* Ordinal for the last part (1-99) */
    if (last_two >= 20) {
        int ones = last_two % 10;
        if (ones == 0) {
            cl_stream_write_string(ctx->stream, ordinal_tens[last_two / 10],
                                   (uint32_t)strlen(ordinal_tens[last_two / 10]));
        } else {
            cl_stream_write_string(ctx->stream, cardinal_tens[last_two / 10],
                                   (uint32_t)strlen(cardinal_tens[last_two / 10]));
            cl_stream_write_char(ctx->stream, '-');
            cl_stream_write_string(ctx->stream, ordinal_ones[ones],
                                   (uint32_t)strlen(ordinal_ones[ones]));
        }
    } else if (last_two > 0) {
        cl_stream_write_string(ctx->stream, ordinal_ones[last_two],
                               (uint32_t)strlen(ordinal_ones[last_two]));
    }
}

static void fmt_roman(FmtCtx *ctx, int32_t n, int old_style)
{
    /* Standard subtractive pairs */
    static const int    vals[]  = {1000,900,500,400,100,90,50,40,10,9,5,4,1};
    static const char  *syms[]  = {"M","CM","D","CD","C","XC","L","XL","X","IX","V","IV","I"};
    /* Old-style: additive only */
    static const int    old_vals[] = {1000,500,100,50,10,5,1};
    static const char  *old_syms[] = {"M","D","C","L","X","V","I"};
    const int *vp;
    const char **sp;
    int count;
    int i;

    if (n <= 0 || n > 3999) {
        /* Fall back to decimal for out-of-range */
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%ld", (long)n);
        cl_stream_write_string(ctx->stream, buf, (uint32_t)len);
        return;
    }

    if (old_style) {
        vp = old_vals; sp = old_syms; count = 7;
    } else {
        vp = vals; sp = syms; count = 13;
    }

    for (i = 0; i < count; i++) {
        while (n >= vp[i]) {
            cl_stream_write_string(ctx->stream, sp[i], (uint32_t)strlen(sp[i]));
            n -= vp[i];
        }
    }
}

static void fmt_radix(FmtCtx *ctx, FmtDirective *d)
{
    if (d->n_params >= 1 && d->param_given[0]) {
        /* ~radix,mincol,padchar,commachar,commaintR — shift params past radix */
        FmtDirective shifted = *d;
        int i;
        int32_t radix = d->params[0];
        for (i = 0; i < 3; i++) {
            shifted.params[i] = d->params[i + 1];
            shifted.param_given[i] = d->param_given[i + 1];
        }
        shifted.params[3] = 3;
        shifted.param_given[3] = 0;
        shifted.n_params = d->n_params > 1 ? d->n_params - 1 : 0;
        fmt_padded_integer(ctx, &shifted, radix);
    } else {
        CL_Obj arg = fmt_next_arg(ctx);
        int32_t n;
        if (!CL_INTEGER_P(arg)) {
            cl_princ_to_stream(arg, ctx->stream);
            return;
        }
        if (CL_FIXNUM_P(arg))
            n = CL_FIXNUM_VAL(arg);
        else {
            /* Bignum — render decimal and parse (simple approach) */
            char buf[64];
            cl_princ_to_string(arg, buf, sizeof(buf));
            n = (int32_t)strtol(buf, NULL, 10);
        }

        if (d->colon && d->atsign) {
            /* ~:@R — old Roman */
            fmt_roman(ctx, n, 1);
        } else if (d->atsign) {
            /* ~@R — Roman */
            fmt_roman(ctx, n, 0);
        } else if (d->colon) {
            /* ~:R — ordinal */
            fmt_ordinal(ctx, n);
        } else {
            /* ~R — cardinal */
            fmt_cardinal(ctx, n);
        }
    }
}

/* ================================================================
 * ~T — Tabulate
 * ================================================================ */

static void fmt_tabulate(FmtCtx *ctx, FmtDirective *d)
{
    int32_t colnum = fmt_param(d, 0, 1);
    int32_t colinc = fmt_param(d, 1, 1);
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(ctx->stream);
    int32_t curpos = (int32_t)st->charpos;
    int spaces;
    int i;

    if (d->atsign) {
        /* ~@T: relative — output colnum spaces minimum, then pad to colinc */
        spaces = (int)colnum;
        if (colinc > 0) {
            int newpos = curpos + spaces;
            int rem = newpos % colinc;
            if (rem != 0)
                spaces += (int)(colinc - rem);
        }
    } else {
        /* ~T: absolute — output spaces to reach colnum */
        if (curpos < colnum) {
            spaces = (int)(colnum - curpos);
        } else if (colinc > 0) {
            /* Already at or past colnum — advance to next colnum + k*colinc */
            int past = curpos - colnum;
            int rem = past % colinc;
            spaces = (rem == 0) ? colinc : (int)(colinc - rem);
        } else {
            spaces = 0;
        }
    }

    for (i = 0; i < spaces; i++)
        cl_stream_write_char(ctx->stream, ' ');
}

/* ================================================================
 * ~* — Go-To Argument
 * ================================================================ */

static void fmt_goto(FmtCtx *ctx, FmtDirective *d)
{
    if (d->atsign) {
        /* ~n@* — go to absolute format arg position n (default 0).
         * Format args start at ctx->args[2] (past dest + fmt string),
         * so absolute arg n maps to ai = n + 2. */
        int32_t n = fmt_param(d, 0, 0);
        int target = (int)n + 2;
        if (target >= 2 && target <= ctx->nargs)
            ctx->ai = target;
    } else if (d->colon) {
        /* ~n:* — back up n args (default 1) */
        int32_t n = fmt_param(d, 0, 1);
        ctx->ai -= (int)n;
        if (ctx->ai < 0) ctx->ai = 0;
    } else {
        /* ~n* — skip forward n args (default 1) */
        int32_t n = fmt_param(d, 0, 1);
        ctx->ai += (int)n;
        if (ctx->ai > ctx->nargs) ctx->ai = ctx->nargs;
    }
}

/* ================================================================
 * ~(~) — Case Conversion
 * ================================================================ */

static void fmt_case_convert(FmtCtx *ctx, FmtDirective *d)
{
    const char *body_start = ctx->pos;
    const char *seps[1];
    int n_seps = 0;
    const char *body_end;
    CL_Obj sstream;
    CL_Obj result;
    CL_String *rs;
    char *data;
    uint32_t len;
    uint32_t i;
    FmtCtx sub;

    body_end = fmt_find_close(body_start, '(', ')', seps, &n_seps, 0);
    if (!body_end) {
        cl_error(CL_ERR_GENERAL, "FORMAT: unmatched ~(");
        return;
    }

    /* Format body into a temp string stream */
    sstream = cl_make_string_output_stream();
    CL_GC_PROTECT(sstream);

    sub = *ctx;
    sub.stream = sstream;
    sub.pos = body_start;
    /* Run until we hit the close paren position */
    {
        /* We need to run the format on the body substring.
         * body_end points past ')', and body_start is just past '('.
         * The body ends at body_end - 2 (at the '~' before ')').
         * But since fmt_run runs until NUL or until the pos reaches a point,
         * we use a substring approach with a temp NUL. Instead, let's just
         * run format on the body text. */
        int body_len = (int)(body_end - body_start);
        char *body_copy;
        /* The body_end points past ')'. The body includes everything from
         * body_start up to the '~' before ')'. We need to find that. */
        /* Actually, body_end is returned by find_close as pointing past ')'.
         * The close '~)' is at body_end - 2 (the '~') and body_end - 1 (')').
         * So the body text is from body_start to body_end - 2. */
        body_len = (int)(body_end - 2 - body_start);
        if (body_len < 0) body_len = 0;
        body_copy = (char *)platform_alloc((uint32_t)body_len + 1);
        memcpy(body_copy, body_start, (uint32_t)body_len);
        body_copy[body_len] = '\0';

        sub.fmt = body_copy;
        sub.pos = body_copy;
        fmt_run(&sub);
        platform_free(body_copy);
    }

    /* Get the result string and free the temp stream's outbuf */
    result = cl_get_output_stream_string(sstream);
    {
        CL_Stream *tmp_st = (CL_Stream *)CL_OBJ_TO_PTR(sstream);
        cl_stream_free_outbuf(tmp_st->out_buf_handle);
        tmp_st->out_buf_handle = 0;
    }
    CL_GC_UNPROTECT(1);
    rs = (CL_String *)CL_OBJ_TO_PTR(result);
    data = rs->data;
    len = rs->length;

    if (d->colon && d->atsign) {
        /* ~:@( — uppercase all */
        for (i = 0; i < len; i++) {
            if (data[i] >= 'a' && data[i] <= 'z')
                data[i] -= 32;
        }
    } else if (d->atsign) {
        /* ~@( — capitalize first word only */
        int first = 1;
        for (i = 0; i < len; i++) {
            if (data[i] >= 'A' && data[i] <= 'Z') {
                if (!first) data[i] += 32;
                first = 0;
            } else if (data[i] >= 'a' && data[i] <= 'z') {
                if (first) { data[i] -= 32; first = 0; }
            } else if (data[i] == ' ' || data[i] == '\t' || data[i] == '\n') {
                /* Don't reset first — only capitalize very first alpha */
            }
        }
    } else if (d->colon) {
        /* ~:( — capitalize each word */
        int word_start = 1;
        for (i = 0; i < len; i++) {
            if (data[i] >= 'A' && data[i] <= 'Z') {
                if (!word_start) data[i] += 32;
                word_start = 0;
            } else if (data[i] >= 'a' && data[i] <= 'z') {
                if (word_start) data[i] -= 32;
                word_start = 0;
            } else {
                word_start = 1;
            }
        }
    } else {
        /* ~( — lowercase all */
        for (i = 0; i < len; i++) {
            if (data[i] >= 'A' && data[i] <= 'Z')
                data[i] += 32;
        }
    }

    /* Write to output stream */
    cl_stream_write_string(ctx->stream, data, len);

    /* Update parent arg index */
    ctx->ai = sub.ai;
    ctx->pos = body_end;
}

/* ================================================================
 * ~[~;~] — Conditional Selection
 * ================================================================ */

#define FMT_MAX_SEPS 64

static void fmt_conditional(FmtCtx *ctx, FmtDirective *d)
{
    const char *body_start = ctx->pos;
    const char *seps[FMT_MAX_SEPS];
    int n_seps = 0;
    const char *body_end;
    /* Collect clause boundaries: seps[i] point to '~' before ';' */
    const char *clause_starts[FMT_MAX_SEPS + 2];
    const char *clause_ends[FMT_MAX_SEPS + 2];
    int n_clauses;
    int has_default = 0;
    int selected = -1;
    int i;

    body_end = fmt_find_close(body_start, '[', ']', seps, &n_seps, FMT_MAX_SEPS);
    if (!body_end) {
        cl_error(CL_ERR_GENERAL, "FORMAT: unmatched ~[");
        return;
    }

    /* Build clause boundaries */
    n_clauses = n_seps + 1;
    clause_starts[0] = body_start;
    for (i = 0; i < n_seps; i++) {
        /* seps[i] points to '~' before ';'. The ';' may have ':' modifier. */
        const char *sp = seps[i] + 1; /* past '~' */
        /* Skip params/modifiers to find ';' */
        while (*sp == ':' || *sp == '@') sp++;
        /* sp now at ';' */
        clause_ends[i] = seps[i];
        clause_starts[i + 1] = sp + 1;

        /* Check if this separator has colon modifier (~:;) = default clause */
        {
            const char *tp = seps[i] + 1;
            while (*tp == ':' || *tp == '@') {
                if (*tp == ':') has_default = 1;
                tp++;
            }
        }
    }
    /* Last clause ends at the close bracket */
    clause_ends[n_seps] = body_end - 2; /* point to '~' before ']' */

    if (d->colon) {
        /* ~:[false~;true~] — boolean: nil=0, non-nil=1 */
        CL_Obj arg = fmt_next_arg(ctx);
        selected = CL_NULL_P(arg) ? 0 : 1;
    } else if (d->atsign) {
        /* ~@[clause~] — if arg non-nil, format clause with arg available */
        CL_Obj arg = fmt_peek_arg(ctx);
        if (!CL_NULL_P(arg)) {
            selected = 0; /* format the one clause */
        } else {
            ctx->ai++; /* consume nil */
            selected = -1; /* skip */
        }
    } else {
        /* ~[c0~;c1~;...~] — select by integer */
        CL_Obj arg = fmt_next_arg(ctx);
        int32_t idx = 0;
        if (CL_FIXNUM_P(arg))
            idx = CL_FIXNUM_VAL(arg);
        if (idx >= 0 && idx < n_clauses) {
            /* Check if this is the default clause */
            selected = (int)idx;
        } else if (has_default) {
            selected = n_clauses - 1; /* default is last */
        } else {
            selected = -1;
        }
    }

    if (selected >= 0 && selected < n_clauses) {
        /* Format the selected clause */
        const char *cs = clause_starts[selected];
        const char *ce = clause_ends[selected];
        int clen = (int)(ce - cs);
        if (clen > 0) {
            char *clause_copy = (char *)platform_alloc((uint32_t)clen + 1);
            FmtCtx sub;
            memcpy(clause_copy, cs, (uint32_t)clen);
            clause_copy[clen] = '\0';
            sub = *ctx;
            sub.fmt = clause_copy;
            sub.pos = clause_copy;
            fmt_run(&sub);
            ctx->ai = sub.ai;
            platform_free(clause_copy);
        }
    }

    ctx->pos = body_end;
}

/* ================================================================
 * ~{~} — Iteration and ~^
 * ================================================================ */

#define FMT_MAX_ITERATIONS 1000

static void fmt_iteration(FmtCtx *ctx, FmtDirective *d)
{
    const char *body_start = ctx->pos;
    const char *seps_unused[1];
    int n_seps = 0;
    const char *body_end;
    int body_len;
    char *body_copy;
    int32_t max_iter = fmt_param(d, 0, FMT_MAX_ITERATIONS);
    int iter_count = 0;

    body_end = fmt_find_close(body_start, '{', '}', seps_unused, &n_seps, 0);
    if (!body_end) {
        cl_error(CL_ERR_GENERAL, "FORMAT: unmatched ~{");
        return;
    }

    body_len = (int)(body_end - 2 - body_start);
    if (body_len < 0) body_len = 0;
    body_copy = (char *)platform_alloc((uint32_t)body_len + 1);
    memcpy(body_copy, body_start, (uint32_t)body_len);
    body_copy[body_len] = '\0';

    if (d->colon && d->atsign) {
        /* ~:@{body~} — remaining args are sublists */
        /* ~^ in sub-context only terminates current step, not whole iteration */
        while (ctx->ai < ctx->nargs && iter_count < max_iter) {
            CL_Obj sublist = fmt_next_arg(ctx);
            /* Build args array from sublist */
            CL_Obj tmp;
            int sub_n = 0;
            CL_Obj sub_args[64];
            FmtCtx sub;

            tmp = sublist;
            while (!CL_NULL_P(tmp) && sub_n < 64) {
                sub_args[sub_n++] = cl_car(tmp);
                tmp = cl_cdr(tmp);
            }

            sub.stream = ctx->stream;
            sub.fmt = body_copy;
            sub.pos = body_copy;
            sub.args = sub_args;
            sub.nargs = sub_n;
            sub.ai = 0;
            sub.escape = 0;
            fmt_run(&sub);
            /* ~^ only terminates current step for ~:@{ */
            iter_count++;
        }
    } else if (d->atsign) {
        /* ~@{body~} — remaining parent args used directly */
        while (ctx->ai < ctx->nargs && iter_count < max_iter) {
            FmtCtx sub;
            sub.stream = ctx->stream;
            sub.fmt = body_copy;
            sub.pos = body_copy;
            sub.args = ctx->args;
            sub.nargs = ctx->nargs;
            sub.ai = ctx->ai;
            sub.escape = 0;
            fmt_run(&sub);
            ctx->ai = sub.ai;
            if (sub.escape) break;
            iter_count++;
        }
    } else if (d->colon) {
        /* ~:{body~} — consume list of sublists */
        /* ~^ in sub-context only terminates current step, not whole iteration */
        CL_Obj list = fmt_next_arg(ctx);
        while (!CL_NULL_P(list) && iter_count < max_iter) {
            CL_Obj sublist = cl_car(list);
            CL_Obj tmp;
            int sub_n = 0;
            CL_Obj sub_args[64];
            FmtCtx sub;

            tmp = sublist;
            while (!CL_NULL_P(tmp) && sub_n < 64) {
                sub_args[sub_n++] = cl_car(tmp);
                tmp = cl_cdr(tmp);
            }

            sub.stream = ctx->stream;
            sub.fmt = body_copy;
            sub.pos = body_copy;
            sub.args = sub_args;
            sub.nargs = sub_n;
            sub.ai = 0;
            sub.escape = 0;
            fmt_run(&sub);
            /* ~^ only terminates current step for ~:{ */

            list = cl_cdr(list);
            iter_count++;
        }
    } else {
        /* ~{body~} — consume list, elements become args.
         * CLHS 22.3.7.4: NIL → no iterations.  Non-list non-NIL is allowed
         * (SBCL behavior): treated as a non-empty source with no extractable
         * args, so iteration runs until V cap (or ~^ / FMT_MAX_ITERATIONS).
         * Termination otherwise: iteration consumed args AND exhausted them. */
        CL_Obj list = fmt_next_arg(ctx);
        int list_n = 0;
        CL_Obj list_args[256];
        int list_ai = 0;

        if (CL_NULL_P(list)) {
            platform_free(body_copy);
            ctx->pos = body_end;
            return;
        }

        if (CL_CONS_P(list)) {
            CL_Obj tmp = list;
            while (CL_CONS_P(tmp) && list_n < 256) {
                list_args[list_n++] = cl_car(tmp);
                tmp = cl_cdr(tmp);
            }
        }

        while (iter_count < max_iter) {
            FmtCtx sub;
            int prev_ai;
            sub.stream = ctx->stream;
            sub.fmt = body_copy;
            sub.pos = body_copy;
            sub.args = list_args;
            sub.nargs = list_n;
            sub.ai = list_ai;
            sub.escape = 0;
            prev_ai = sub.ai;
            fmt_run(&sub);
            list_ai = sub.ai;
            if (sub.escape) break;
            iter_count++;
            if (sub.ai > prev_ai && sub.ai >= list_n) break;
        }
    }

    platform_free(body_copy);
    ctx->pos = body_end;
}

/* ================================================================
 * ~? — Recursive Format
 * ================================================================ */

static void fmt_recursive(FmtCtx *ctx, FmtDirective *d)
{
    CL_Obj fmt_str_obj = fmt_next_arg(ctx);
    CL_String *fmt_str;
    FmtCtx sub;

    if (!CL_STRING_P(fmt_str_obj)) {
        cl_error(CL_ERR_TYPE, "FORMAT ~?: argument must be a string");
        return;
    }
    fmt_str = (CL_String *)CL_OBJ_TO_PTR(fmt_str_obj);

    sub.stream = ctx->stream;
    sub.fmt = fmt_str->data;
    sub.pos = fmt_str->data;
    sub.escape = 0;

    if (d->atsign) {
        /* ~@? — use remaining parent args */
        sub.args = ctx->args;
        sub.nargs = ctx->nargs;
        sub.ai = ctx->ai;
        fmt_run(&sub);
        ctx->ai = sub.ai;
    } else {
        /* ~? — consume format string + list of args */
        CL_Obj arg_list = fmt_next_arg(ctx);
        CL_Obj tmp;
        int sub_n = 0;
        CL_Obj sub_args[64];

        tmp = arg_list;
        while (!CL_NULL_P(tmp) && sub_n < 64) {
            sub_args[sub_n++] = cl_car(tmp);
            tmp = cl_cdr(tmp);
        }

        sub.args = sub_args;
        sub.nargs = sub_n;
        sub.ai = 0;
        fmt_run(&sub);
    }
}

/* ================================================================
 * Dispatch table
 * ================================================================ */

static void fmt_dispatch(FmtCtx *ctx, FmtDirective *d)
{
    switch (d->directive) {
    case 'A':
        fmt_padded_obj(ctx, d, 0);
        break;
    case 'S':
        fmt_padded_obj(ctx, d, 1);
        break;
    case 'W':
        if (ctx->ai < ctx->nargs)
            cl_write_to_stream(ctx->args[ctx->ai++], ctx->stream);
        break;
    case 'D':
        fmt_padded_integer(ctx, d, 10);
        break;
    case 'B':
        fmt_padded_integer(ctx, d, 2);
        break;
    case 'O':
        fmt_padded_integer(ctx, d, 8);
        break;
    case 'X':
        fmt_padded_integer(ctx, d, 16);
        break;
    case 'R':
        fmt_radix(ctx, d);
        break;
    case 'C':
        /* ~C: character */
        if (ctx->ai < ctx->nargs)
            cl_princ_to_stream(ctx->args[ctx->ai++], ctx->stream);
        break;
    case '%': {
        int32_t count = fmt_param(d, 0, 1);
        int i;
        for (i = 0; i < count; i++)
            cl_stream_write_char(ctx->stream, '\n');
        break;
    }
    case '&': {
        /* ~&: fresh-line. ~n& = one fresh-line then n-1 newlines */
        int32_t count = fmt_param(d, 0, 1);
        CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(ctx->stream);
        int i;
        if (count > 0) {
            if (st->charpos != 0)
                cl_stream_write_char(ctx->stream, '\n');
            for (i = 1; i < count; i++)
                cl_stream_write_char(ctx->stream, '\n');
        }
        break;
    }
    case '|': {
        int32_t count = fmt_param(d, 0, 1);
        int i;
        for (i = 0; i < count; i++)
            cl_stream_write_char(ctx->stream, '\f');
        break;
    }
    case '~': {
        int32_t count = fmt_param(d, 0, 1);
        int i;
        for (i = 0; i < count; i++)
            cl_stream_write_char(ctx->stream, '~');
        break;
    }
    case 'P': {
        /* ~P: plural — "s" if arg /= 1, else ""
           ~:P: backup then plural — backs up one arg, then plural
           ~@P: "y"/"ies" — "y" if arg == 1, else "ies" */
        CL_Obj arg;
        int is_one;
        if (d->colon && ctx->ai > 0)
            ctx->ai--;  /* back up one arg */
        arg = (ctx->ai < ctx->nargs) ? ctx->args[ctx->ai++] : CL_NIL;
        is_one = (CL_FIXNUM_P(arg) && CL_FIXNUM_VAL(arg) == 1);
        if (d->atsign) {
            if (is_one)
                cl_stream_write_char(ctx->stream, 'y');
            else {
                cl_stream_write_char(ctx->stream, 'i');
                cl_stream_write_char(ctx->stream, 'e');
                cl_stream_write_char(ctx->stream, 's');
            }
        } else {
            if (!is_one)
                cl_stream_write_char(ctx->stream, 's');
        }
        break;
    }
    case 'F':
    case '$': {
        /* ~F: fixed-format float — ~w,d,k,ovf,padcharF
           ~$: monetary — ~d,n,w,padchar$ (d=2 dec digits, n=1 min int digits)
           CLHS 22.3.3.1: non-number arg falls back to ~A behavior using
           the same width parameter. */
        CL_Obj arg = fmt_next_arg(ctx);
        int32_t mincol = (d->directive == '$') ? fmt_param(d, 2, 0)
                                               : fmt_param(d, 0, 0);
        int32_t digs = (d->directive == '$') ? fmt_param(d, 0, 2)
                                             : fmt_param(d, 1, -1);
        char buf[64];
        int len;
        int is_num = CL_FIXNUM_P(arg) ||
                     (CL_HEAP_P(arg) &&
                      (CL_HDR_TYPE(CL_OBJ_TO_PTR(arg)) == TYPE_SINGLE_FLOAT ||
                       CL_HDR_TYPE(CL_OBJ_TO_PTR(arg)) == TYPE_DOUBLE_FLOAT ||
                       CL_HDR_TYPE(CL_OBJ_TO_PTR(arg)) == TYPE_RATIO ||
                       CL_HDR_TYPE(CL_OBJ_TO_PTR(arg)) == TYPE_BIGNUM));
        if (is_num) {
            double val = cl_to_double(arg);
            if (digs < 0)
                len = snprintf(buf, sizeof(buf), "%g", val);
            else
                len = snprintf(buf, sizeof(buf), "%.*f", (int)digs, val);
            if (len < 0) len = 0;
            if (len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1;
        } else {
            len = cl_princ_to_string(arg, buf, sizeof(buf));
        }
        if (d->atsign && mincol > len) {
            int i;
            for (i = 0; i < (int)(mincol - len); i++)
                cl_stream_write_char(ctx->stream, ' ');
        }
        {
            int i;
            for (i = 0; i < len; i++)
                cl_stream_write_char(ctx->stream, buf[i]);
        }
        if (!d->atsign && mincol > len) {
            int i;
            for (i = 0; i < (int)(mincol - len); i++)
                cl_stream_write_char(ctx->stream, ' ');
        }
        break;
    }
    case 'E': {
        /* ~E: exponential format — simplified */
        CL_Obj arg = fmt_next_arg(ctx);
        double val = cl_to_double(arg);
        char buf[64];
        snprintf(buf, sizeof(buf), "%E", val);
        {
            const char *p = buf;
            while (*p) cl_stream_write_char(ctx->stream, *p++);
        }
        break;
    }
    case 'T':
        fmt_tabulate(ctx, d);
        break;
    case '*':
        fmt_goto(ctx, d);
        break;
    case '(':
        fmt_case_convert(ctx, d);
        break;
    case '[':
        fmt_conditional(ctx, d);
        break;
    case '{':
        fmt_iteration(ctx, d);
        break;
    case '^':
        /* ~^ — escape from iteration if no args remain */
        if (fmt_args_remaining(ctx) == 0)
            ctx->escape = 1;
        break;
    case '?':
        fmt_recursive(ctx, d);
        break;
    case '\n':
        /* ~<newline> — ignore newline and any following whitespace */
        if (!d->colon) {
            while (*ctx->pos == ' ' || *ctx->pos == '\t')
                ctx->pos++;
        }
        break;
    default:
        /* Unknown directive — output literally */
        cl_stream_write_char(ctx->stream, '~');
        cl_stream_write_char(ctx->stream, d->directive);
        break;
    }
}

/* ================================================================
 * Main format loop
 * ================================================================ */

static void fmt_run(FmtCtx *ctx)
{
    while (*ctx->pos && !ctx->escape) {
        if (*ctx->pos == '~') {
            FmtDirective d;
            ctx->pos++; /* skip '~' */
            if (!*ctx->pos) break;
            fmt_parse_directive(ctx, &d);
            fmt_dispatch(ctx, &d);
        } else {
            cl_stream_write_char(ctx->stream, *ctx->pos);
            ctx->pos++;
        }
    }
}

/* ================================================================
 * Public entry point — called from builtins_io.c
 * ================================================================ */

void cl_format_to_stream(CL_Obj stream, CL_Obj *args, int n)
{
    CL_String *s;
    FmtCtx ctx;

    if (n < 2 || !CL_STRING_P(args[1]))
        return;

    s = (CL_String *)CL_OBJ_TO_PTR(args[1]);

    ctx.stream = stream;
    ctx.fmt = s->data;
    ctx.pos = s->data;
    ctx.args = args;
    ctx.nargs = n;
    ctx.ai = 2;  /* args start after destination and format string */
    ctx.escape = 0;

    fmt_run(&ctx);
}
