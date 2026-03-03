/*
 * builtins_stream.c — Stream-related CL builtins
 *
 * Step 1: streamp, input-stream-p, output-stream-p, interactive-stream-p
 * Step 3: read-char, write-char, peek-char, unread-char, read-line,
 *         write-string, write-line, terpri, fresh-line, finish-output,
 *         force-output, clear-output, open-stream-p, close,
 *         make-string-input-stream, make-string-output-stream,
 *         get-output-stream-string
 */

#include "builtins.h"
#include "stream.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "vm.h"
#include "../platform/platform.h"
#include <string.h>

/* Helper to register a builtin */
static void defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_cl);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    s->value = fn;
}

/* --- Pre-interned keyword symbols --- */

static CL_Obj KW_START = CL_NIL;
static CL_Obj KW_END = CL_NIL;
static CL_Obj KW_ABORT_KW = CL_NIL;

/* --- Stream argument resolution helpers --- */

static CL_Obj resolve_input_stream(CL_Obj *args, int n, int idx)
{
    CL_Obj s;
    CL_Symbol *sym;
    if (idx >= n || CL_NULL_P(args[idx])) {
        sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STANDARD_INPUT);
        return sym->value;
    }
    s = args[idx];
    if (s == CL_T) {
        sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_TERMINAL_IO);
        return sym->value;
    }
    if (!CL_STREAM_P(s))
        cl_error(CL_ERR_TYPE, "argument is not a stream");
    return s;
}

static CL_Obj resolve_output_stream(CL_Obj *args, int n, int idx)
{
    CL_Obj s;
    CL_Symbol *sym;
    if (idx >= n || CL_NULL_P(args[idx])) {
        sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STANDARD_OUTPUT);
        return sym->value;
    }
    s = args[idx];
    if (s == CL_T) {
        sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_TERMINAL_IO);
        return sym->value;
    }
    if (!CL_STREAM_P(s))
        cl_error(CL_ERR_TYPE, "argument is not a stream");
    return s;
}

/* --- Step 1 builtins --- */

/* (streamp obj) => T or NIL */
static CL_Obj bi_streamp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_STREAM_P(args[0]) ? CL_T : CL_NIL;
}

/* (input-stream-p stream) => T or NIL */
static CL_Obj bi_input_stream_p(CL_Obj *args, int n)
{
    CL_Stream *st;
    CL_UNUSED(n);
    if (!CL_STREAM_P(args[0]))
        cl_error(CL_ERR_TYPE, "INPUT-STREAM-P: argument is not a stream");
    st = (CL_Stream *)CL_OBJ_TO_PTR(args[0]);
    return (st->direction == CL_STREAM_INPUT || st->direction == CL_STREAM_IO)
           ? CL_T : CL_NIL;
}

/* (output-stream-p stream) => T or NIL */
static CL_Obj bi_output_stream_p(CL_Obj *args, int n)
{
    CL_Stream *st;
    CL_UNUSED(n);
    if (!CL_STREAM_P(args[0]))
        cl_error(CL_ERR_TYPE, "OUTPUT-STREAM-P: argument is not a stream");
    st = (CL_Stream *)CL_OBJ_TO_PTR(args[0]);
    return (st->direction == CL_STREAM_OUTPUT || st->direction == CL_STREAM_IO)
           ? CL_T : CL_NIL;
}

/* (%make-test-stream direction type) => stream */
static CL_Obj bi_make_test_stream(CL_Obj *args, int n)
{
    uint32_t dir, stype;
    CL_UNUSED(n);
    if (!CL_FIXNUM_P(args[0]) || !CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE, "%MAKE-TEST-STREAM: arguments must be fixnums");
    dir = (uint32_t)CL_FIXNUM_VAL(args[0]);
    stype = (uint32_t)CL_FIXNUM_VAL(args[1]);
    return cl_make_stream(dir, stype);
}

/* (interactive-stream-p stream) => T or NIL */
static CL_Obj bi_interactive_stream_p(CL_Obj *args, int n)
{
    CL_Stream *st;
    CL_UNUSED(n);
    if (!CL_STREAM_P(args[0]))
        cl_error(CL_ERR_TYPE, "INTERACTIVE-STREAM-P: argument is not a stream");
    st = (CL_Stream *)CL_OBJ_TO_PTR(args[0]);
    return (st->stream_type == CL_STREAM_CONSOLE) ? CL_T : CL_NIL;
}

/* --- Step 3 builtins: Stream I/O --- */

/* (read-char &optional stream eof-error-p eof-value recursive-p) */
static CL_Obj bi_read_char(CL_Obj *args, int n)
{
    CL_Obj stream = resolve_input_stream(args, n, 0);
    int eof_error_p = (n < 2 || !CL_NULL_P(args[1]));
    CL_Obj eof_value = (n >= 3) ? args[2] : CL_NIL;
    int ch;

    ch = cl_stream_read_char(stream);
    if (ch == -1) {
        if (eof_error_p)
            cl_error(CL_ERR_GENERAL, "READ-CHAR: end of file");
        return eof_value;
    }
    return CL_MAKE_CHAR(ch);
}

/* (write-char character &optional stream) => character */
static CL_Obj bi_write_char(CL_Obj *args, int n)
{
    CL_Obj stream;
    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "WRITE-CHAR: argument is not a character");
    stream = resolve_output_stream(args, n, 1);
    cl_stream_write_char(stream, CL_CHAR_VAL(args[0]));
    return args[0];
}

/* (peek-char &optional peek-type stream eof-error-p eof-value recursive-p) */
static CL_Obj bi_peek_char(CL_Obj *args, int n)
{
    CL_Obj peek_type = (n >= 1) ? args[0] : CL_NIL;
    CL_Obj stream = resolve_input_stream(args, n, 1);
    int eof_error_p = (n < 3 || !CL_NULL_P(args[2]));
    CL_Obj eof_value = (n >= 4) ? args[3] : CL_NIL;
    int ch;

    if (CL_NULL_P(peek_type)) {
        /* NIL: just peek at next char */
        ch = cl_stream_peek_char(stream);
    } else if (peek_type == CL_T) {
        /* T: skip whitespace, peek at first non-whitespace */
        for (;;) {
            ch = cl_stream_read_char(stream);
            if (ch == -1) break;
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
                cl_stream_unread_char(stream, ch);
                break;
            }
        }
    } else if (CL_CHAR_P(peek_type)) {
        /* Character: skip until that char, peek at it */
        int target = CL_CHAR_VAL(peek_type);
        for (;;) {
            ch = cl_stream_read_char(stream);
            if (ch == -1) break;
            if (ch == target) {
                cl_stream_unread_char(stream, ch);
                break;
            }
        }
    } else {
        cl_error(CL_ERR_TYPE, "PEEK-CHAR: invalid peek-type");
        return CL_NIL;
    }

    if (ch == -1) {
        if (eof_error_p)
            cl_error(CL_ERR_GENERAL, "PEEK-CHAR: end of file");
        return eof_value;
    }
    return CL_MAKE_CHAR(ch);
}

/* (unread-char character &optional stream) => NIL */
static CL_Obj bi_unread_char(CL_Obj *args, int n)
{
    CL_Obj stream;
    CL_Stream *st;
    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "UNREAD-CHAR: argument is not a character");
    stream = resolve_input_stream(args, n, 1);
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    if (st->unread_char != -1)
        cl_error(CL_ERR_GENERAL, "UNREAD-CHAR: already unreading a character");
    cl_stream_unread_char(stream, CL_CHAR_VAL(args[0]));
    return CL_NIL;
}

/* (read-line &optional stream eof-error-p eof-value recursive-p)
 * => string, missing-newline-p (2 values) */
static CL_Obj bi_read_line(CL_Obj *args, int n)
{
    CL_Obj stream = resolve_input_stream(args, n, 0);
    int eof_error_p = (n < 2 || !CL_NULL_P(args[1]));
    CL_Obj eof_value = (n >= 3) ? args[2] : CL_NIL;
    char stack_buf[256];
    char *buf = stack_buf;
    uint32_t cap = 256;
    uint32_t len = 0;
    int ch;
    int got_any = 0;
    int missing_newline = 1;
    CL_Obj result;

    for (;;) {
        ch = cl_stream_read_char(stream);
        if (ch == -1) break;
        got_any = 1;
        if (ch == '\n') {
            missing_newline = 0;
            break;
        }
        if (len + 1 >= cap) {
            uint32_t new_cap = cap * 2;
            char *new_buf = (char *)platform_alloc(new_cap);
            if (!new_buf) break;
            memcpy(new_buf, buf, len);
            if (buf != stack_buf) platform_free(buf);
            buf = new_buf;
            cap = new_cap;
        }
        buf[len++] = (char)ch;
    }

    if (!got_any && ch == -1) {
        if (buf != stack_buf) platform_free(buf);
        if (eof_error_p)
            cl_error(CL_ERR_GENERAL, "READ-LINE: end of file");
        cl_mv_values[0] = eof_value;
        cl_mv_values[1] = CL_T;
        cl_mv_count = 2;
        return eof_value;
    }

    result = cl_make_string(buf, len);
    if (buf != stack_buf) platform_free(buf);

    cl_mv_values[0] = result;
    cl_mv_values[1] = missing_newline ? CL_T : CL_NIL;
    cl_mv_count = 2;
    return result;
}

/* (write-string string &optional stream &key :start :end) => string */
static CL_Obj bi_write_string(CL_Obj *args, int n)
{
    CL_Obj stream;
    CL_String *str;
    uint32_t start = 0, end;
    int i;

    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "WRITE-STRING: argument is not a string");
    str = (CL_String *)CL_OBJ_TO_PTR(args[0]);
    end = str->length;

    stream = resolve_output_stream(args, n, 1);

    /* Parse keyword args starting from index 2 */
    for (i = 2; i + 1 < n; i += 2) {
        CL_Obj kw = args[i];
        CL_Obj val = args[i + 1];
        if (kw == KW_START && CL_FIXNUM_P(val))
            start = (uint32_t)CL_FIXNUM_VAL(val);
        else if (kw == KW_END && !CL_NULL_P(val) && CL_FIXNUM_P(val))
            end = (uint32_t)CL_FIXNUM_VAL(val);
    }

    if (start > str->length) start = str->length;
    if (end > str->length) end = str->length;
    if (start < end)
        cl_stream_write_string(stream, str->data + start, end - start);

    return args[0];
}

/* (write-line string &optional stream &key :start :end) => string */
static CL_Obj bi_write_line(CL_Obj *args, int n)
{
    bi_write_string(args, n);
    {
        CL_Obj stream = resolve_output_stream(args, n, 1);
        cl_stream_write_char(stream, '\n');
    }
    return args[0];
}

/* (terpri &optional stream) => NIL */
static CL_Obj bi_terpri(CL_Obj *args, int n)
{
    CL_Obj stream = resolve_output_stream(args, n, 0);
    cl_stream_write_char(stream, '\n');
    return CL_NIL;
}

/* (fresh-line &optional stream) => T if newline written, NIL if at BOL */
static CL_Obj bi_fresh_line(CL_Obj *args, int n)
{
    CL_Obj stream = resolve_output_stream(args, n, 0);
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    if (st->charpos != 0) {
        cl_stream_write_char(stream, '\n');
        return CL_T;
    }
    return CL_NIL;
}

/* (finish-output &optional stream) => NIL */
static CL_Obj bi_finish_output(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    return CL_NIL;
}

/* (force-output &optional stream) => NIL */
static CL_Obj bi_force_output(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    return CL_NIL;
}

/* (clear-output &optional stream) => NIL */
static CL_Obj bi_clear_output(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    return CL_NIL;
}

/* (open-stream-p stream) => T or NIL */
static CL_Obj bi_open_stream_p(CL_Obj *args, int n)
{
    CL_Stream *st;
    CL_UNUSED(n);
    if (!CL_STREAM_P(args[0]))
        cl_error(CL_ERR_TYPE, "OPEN-STREAM-P: argument is not a stream");
    st = (CL_Stream *)CL_OBJ_TO_PTR(args[0]);
    return (st->flags & CL_STREAM_FLAG_OPEN) ? CL_T : CL_NIL;
}

/* (close stream &key :abort) => T */
static CL_Obj bi_close(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_STREAM_P(args[0]))
        cl_error(CL_ERR_TYPE, "CLOSE: argument is not a stream");
    cl_stream_close(args[0]);
    return CL_T;
}

/* (make-string-input-stream string &optional start end) */
static CL_Obj bi_make_string_input_stream(CL_Obj *args, int n)
{
    CL_String *str;
    uint32_t start = 0, end;

    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "MAKE-STRING-INPUT-STREAM: argument is not a string");
    str = (CL_String *)CL_OBJ_TO_PTR(args[0]);
    end = str->length;

    if (n >= 2 && CL_FIXNUM_P(args[1]))
        start = (uint32_t)CL_FIXNUM_VAL(args[1]);
    if (n >= 3 && !CL_NULL_P(args[2]) && CL_FIXNUM_P(args[2]))
        end = (uint32_t)CL_FIXNUM_VAL(args[2]);

    if (start > str->length) start = str->length;
    if (end > str->length) end = str->length;

    return cl_make_string_input_stream(args[0], start, end);
}

/* (make-string-output-stream &key :element-type) */
static CL_Obj bi_make_string_output_stream(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    return cl_make_string_output_stream();
}

/* (get-output-stream-string stream) */
static CL_Obj bi_get_output_stream_string(CL_Obj *args, int n)
{
    CL_Stream *st;
    CL_UNUSED(n);
    if (!CL_STREAM_P(args[0]))
        cl_error(CL_ERR_TYPE, "GET-OUTPUT-STREAM-STRING: argument is not a stream");
    st = (CL_Stream *)CL_OBJ_TO_PTR(args[0]);
    if (st->stream_type != CL_STREAM_STRING || !(st->direction & CL_STREAM_OUTPUT))
        cl_error(CL_ERR_TYPE, "GET-OUTPUT-STREAM-STRING: not a string output stream");
    return cl_get_output_stream_string(args[0]);
}

/* --- Registration --- */

void cl_builtins_stream_init(void)
{
    /* Pre-intern keyword symbols */
    KW_START   = cl_intern_keyword("START", 5);
    KW_END     = cl_intern_keyword("END", 3);
    KW_ABORT_KW = cl_intern_keyword("ABORT", 5);

    /* Step 1 */
    defun("STREAMP", bi_streamp, 1, 1);
    defun("INPUT-STREAM-P", bi_input_stream_p, 1, 1);
    defun("OUTPUT-STREAM-P", bi_output_stream_p, 1, 1);
    defun("INTERACTIVE-STREAM-P", bi_interactive_stream_p, 1, 1);
    defun("%MAKE-TEST-STREAM", bi_make_test_stream, 2, 2);

    /* Step 3: Character I/O */
    defun("READ-CHAR", bi_read_char, 0, 4);
    defun("WRITE-CHAR", bi_write_char, 1, 2);
    defun("PEEK-CHAR", bi_peek_char, 0, 5);
    defun("UNREAD-CHAR", bi_unread_char, 1, 2);
    defun("READ-LINE", bi_read_line, 0, 4);
    defun("WRITE-STRING", bi_write_string, 1, -1);
    defun("WRITE-LINE", bi_write_line, 1, -1);
    defun("TERPRI", bi_terpri, 0, 1);
    defun("FRESH-LINE", bi_fresh_line, 0, 1);
    defun("FINISH-OUTPUT", bi_finish_output, 0, 1);
    defun("FORCE-OUTPUT", bi_force_output, 0, 1);
    defun("CLEAR-OUTPUT", bi_clear_output, 0, 1);
    defun("OPEN-STREAM-P", bi_open_stream_p, 1, 1);
    defun("CLOSE", bi_close, 1, -1);
    defun("MAKE-STRING-INPUT-STREAM", bi_make_string_input_stream, 1, 3);
    defun("MAKE-STRING-OUTPUT-STREAM", bi_make_string_output_stream, 0, -1);
    defun("GET-OUTPUT-STREAM-STRING", bi_get_output_stream_string, 1, 1);
}
