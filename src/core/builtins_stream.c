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
#include "bignum.h"
#include "readtable.h"
#include "../platform/platform.h"
#include <string.h>
#include <stdlib.h>

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
static CL_Obj KW_DIRECTION = CL_NIL;
static CL_Obj KW_INPUT = CL_NIL;
static CL_Obj KW_OUTPUT = CL_NIL;
static CL_Obj KW_IO = CL_NIL;
static CL_Obj KW_PROBE = CL_NIL;
static CL_Obj KW_IF_EXISTS = CL_NIL;
static CL_Obj KW_IF_DOES_NOT_EXIST = CL_NIL;
static CL_Obj KW_SUPERSEDE = CL_NIL;
static CL_Obj KW_APPEND = CL_NIL;
static CL_Obj KW_ERROR_KW = CL_NIL;
static CL_Obj KW_CREATE = CL_NIL;
static CL_Obj KW_NEW_VERSION = CL_NIL;

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
    CL_Obj stream;
    CL_Stream *st;
    if (n > 0 && !CL_NULL_P(args[0]) && CL_STREAM_P(args[0])) {
        stream = args[0];
        st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
        if (st->stream_type == CL_STREAM_FILE)
            platform_file_flush((PlatformFile)st->handle_id);
        else if (st->stream_type == CL_STREAM_SOCKET)
            platform_socket_flush((PlatformSocket)st->handle_id);
    }
    return CL_NIL;
}

/* (force-output &optional stream) => NIL */
static CL_Obj bi_force_output(CL_Obj *args, int n)
{
    CL_Obj stream;
    CL_Stream *st;
    if (n > 0 && !CL_NULL_P(args[0]) && CL_STREAM_P(args[0])) {
        stream = args[0];
        st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
        if (st->stream_type == CL_STREAM_FILE)
            platform_file_flush((PlatformFile)st->handle_id);
        else if (st->stream_type == CL_STREAM_SOCKET)
            platform_socket_flush((PlatformSocket)st->handle_id);
    }
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

/* (make-synonym-stream symbol) */
static CL_Obj bi_make_synonym_stream(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_SYMBOL_P(args[0]))
        cl_error(CL_ERR_TYPE, "MAKE-SYNONYM-STREAM: argument must be a symbol");
    return cl_make_synonym_stream(args[0]);
}

/* (synonym-stream-symbol stream) */
static CL_Obj bi_synonym_stream_symbol(CL_Obj *args, int n)
{
    CL_Stream *st;
    CL_UNUSED(n);
    if (!CL_STREAM_P(args[0]))
        cl_error(CL_ERR_TYPE, "SYNONYM-STREAM-SYMBOL: argument is not a stream");
    st = (CL_Stream *)CL_OBJ_TO_PTR(args[0]);
    if (st->stream_type != CL_STREAM_SYNONYM)
        cl_error(CL_ERR_TYPE, "SYNONYM-STREAM-SYMBOL: not a synonym stream");
    return st->string_buf;
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

/* --- File streams (Step 8) --- */

/* Helper: find keyword value in args list (keyword arg pairs start at idx) */
static CL_Obj find_keyword_arg(CL_Obj *args, int n, int start, CL_Obj keyword)
{
    int i;
    for (i = start; i + 1 < n; i += 2) {
        if (args[i] == keyword)
            return args[i + 1];
    }
    return CL_UNBOUND;  /* Not found */
}

/* Coerce pathname designator to a CL string object, modifying *arg in place.
   Returns the C string data pointer, or NULL if not a valid designator. */
static const char *coerce_to_filename(CL_Obj *arg)
{
    if (CL_PATHNAME_P(*arg)) {
        char ns_buf[1024];
        extern const char *cl_coerce_to_namestring(CL_Obj, char *, uint32_t);
        cl_coerce_to_namestring(*arg, ns_buf, sizeof(ns_buf));
        *arg = cl_make_string(ns_buf, (uint32_t)strlen(ns_buf));
    }
    if (!CL_STRING_P(*arg)) return NULL;
    return ((CL_String *)CL_OBJ_TO_PTR(*arg))->data;
}

/* (open filename &key :direction :if-exists :if-does-not-exist) */
static CL_Obj bi_open(CL_Obj *args, int n)
{
    CL_String *path_str;
    CL_Obj direction_val, if_exists_val, if_dne_val;
    CL_Obj direction, if_exists, if_dne;
    int platform_mode;
    uint32_t stream_dir;
    PlatformFile fh;
    CL_Obj stream;
    CL_Stream *st;

    if (!coerce_to_filename(&args[0]))
        cl_error(CL_ERR_TYPE, "OPEN: filename must be a string or pathname");
    path_str = (CL_String *)CL_OBJ_TO_PTR(args[0]);

    /* Parse keyword arguments */
    direction_val = find_keyword_arg(args, n, 1, KW_DIRECTION);
    if_exists_val = find_keyword_arg(args, n, 1, KW_IF_EXISTS);
    if_dne_val    = find_keyword_arg(args, n, 1, KW_IF_DOES_NOT_EXIST);

    /* Default direction is :input */
    direction = (direction_val != CL_UNBOUND) ? direction_val : KW_INPUT;

    /* Determine defaults based on direction */
    if (direction == KW_INPUT) {
        if_exists = (if_exists_val != CL_UNBOUND) ? if_exists_val : CL_NIL; /* don't care */
        if_dne    = (if_dne_val != CL_UNBOUND)    ? if_dne_val    : KW_ERROR_KW;
        stream_dir = CL_STREAM_INPUT;
        platform_mode = PLATFORM_FILE_READ;
    } else if (direction == KW_OUTPUT) {
        if_exists = (if_exists_val != CL_UNBOUND) ? if_exists_val : KW_NEW_VERSION;
        if_dne    = (if_dne_val != CL_UNBOUND)    ? if_dne_val    : KW_CREATE;
        stream_dir = CL_STREAM_OUTPUT;

        if (if_exists == KW_APPEND) {
            platform_mode = PLATFORM_FILE_APPEND;
        } else {
            /* :supersede, :new-version, or default → truncate/create */
            platform_mode = PLATFORM_FILE_WRITE;
        }
    } else if (direction == KW_IO) {
        if_exists = (if_exists_val != CL_UNBOUND) ? if_exists_val : KW_NEW_VERSION;
        if_dne    = (if_dne_val != CL_UNBOUND)    ? if_dne_val    : KW_CREATE;
        stream_dir = CL_STREAM_IO;
        platform_mode = PLATFORM_FILE_APPEND; /* read+write, no truncate */
    } else if (direction == KW_PROBE) {
        /* :probe — check existence, optionally create, return NIL */
        if_dne = (if_dne_val != CL_UNBOUND) ? if_dne_val : CL_NIL;
        if (if_dne == KW_CREATE) {
            /* Create the file if it doesn't exist */
            PlatformFile test = platform_file_open(path_str->data, PLATFORM_FILE_READ);
            if (test != PLATFORM_FILE_INVALID) {
                platform_file_close(test);
            } else {
                test = platform_file_open(path_str->data, PLATFORM_FILE_WRITE);
                if (test != PLATFORM_FILE_INVALID)
                    platform_file_close(test);
            }
        } else if (if_dne == KW_ERROR_KW) {
            PlatformFile test = platform_file_open(path_str->data, PLATFORM_FILE_READ);
            if (test == PLATFORM_FILE_INVALID)
                cl_error(CL_ERR_GENERAL, "OPEN: file does not exist \"%s\"",
                         path_str->data);
            platform_file_close(test);
        }
        return CL_NIL;
    } else {
        cl_error(CL_ERR_GENERAL, "OPEN: invalid :direction");
        return CL_NIL;
    }

    /* Handle :if-exists :error — check if file already exists */
    if (direction != KW_INPUT && if_exists == KW_ERROR_KW) {
        PlatformFile test = platform_file_open(path_str->data, PLATFORM_FILE_READ);
        if (test != PLATFORM_FILE_INVALID) {
            platform_file_close(test);
            cl_error(CL_ERR_GENERAL, "OPEN: file already exists");
        }
    }

    /* Open the file */
    fh = platform_file_open(path_str->data, platform_mode);
    if (fh == PLATFORM_FILE_INVALID) {
        /* :if-does-not-exist nil — return NIL silently */
        if (CL_NULL_P(if_dne))
            return CL_NIL;
        cl_error(CL_ERR_GENERAL, "OPEN: cannot open file \"%s\"",
                 path_str->data);
        return CL_NIL;
    }

    /* Create stream object */
    stream = cl_make_stream(stream_dir, CL_STREAM_FILE);
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    st->handle_id = (uint32_t)fh;

    return stream;
}

/* (file-position stream &optional position) => position or T/NIL */
static CL_Obj bi_file_position(CL_Obj *args, int n)
{
    CL_Stream *st;
    if (!CL_STREAM_P(args[0]))
        cl_error(CL_ERR_TYPE, "FILE-POSITION: not a stream");
    st = (CL_Stream *)CL_OBJ_TO_PTR(args[0]);
    if (st->stream_type == CL_STREAM_STRING) {
        if (n > 1 && !CL_NULL_P(args[1])) {
            /* Set position */
            if (CL_FIXNUM_P(args[1]))
                st->position = (uint32_t)CL_FIXNUM_VAL(args[1]);
            return CL_T;
        }
        return CL_MAKE_FIXNUM((int32_t)st->position);
    }
    if (st->stream_type != CL_STREAM_FILE)
        return CL_NIL;
    if (n > 1 && !CL_NULL_P(args[1])) {
        /* Set position */
        long pos = 0;
        if (CL_FIXNUM_P(args[1]))
            pos = (long)CL_FIXNUM_VAL(args[1]);
        return platform_file_set_position((PlatformFile)st->handle_id, pos) == 0
            ? CL_T : CL_NIL;
    }
    /* Get position */
    {
        long pos = platform_file_position((PlatformFile)st->handle_id);
        if (pos < 0) return CL_NIL;
        return CL_MAKE_FIXNUM((int32_t)pos);
    }
}

/* (file-length stream) => integer or NIL */
static CL_Obj bi_file_length(CL_Obj *args, int n)
{
    CL_Stream *st;
    long cur, end;
    (void)n;
    if (!CL_STREAM_P(args[0]))
        cl_error(CL_ERR_TYPE, "FILE-LENGTH: not a stream");
    st = (CL_Stream *)CL_OBJ_TO_PTR(args[0]);
    if (st->stream_type != CL_STREAM_FILE)
        return CL_NIL;
    cur = platform_file_position((PlatformFile)st->handle_id);
    if (cur < 0) return CL_NIL;
    end = platform_file_length((PlatformFile)st->handle_id);
    platform_file_set_position((PlatformFile)st->handle_id, cur);
    if (end < 0) return CL_NIL;
    return CL_MAKE_FIXNUM((int32_t)end);
}

/* --- Time functions (Step 10) --- */

/* (get-universal-time) => integer */
static CL_Obj bi_get_universal_time(CL_Obj *args, int n)
{
    uint32_t ut;
    CL_UNUSED(args); CL_UNUSED(n);
    ut = platform_universal_time();
    /* Universal time is large (>3.9 billion) — needs bignum on 32-bit */
    return cl_bignum_from_uint32(ut);
}

/* --- File system functions (Step 10) --- */

/* (probe-file pathname) => pathname or NIL */
static CL_Obj bi_probe_file(CL_Obj *args, int n)
{
    const char *path;
    CL_UNUSED(n);
    path = coerce_to_filename(&args[0]);
    if (!path)
        cl_error(CL_ERR_TYPE, "PROBE-FILE: argument must be a pathname designator");
    if (!platform_file_exists(path)) return CL_NIL;
    {
        extern CL_Obj cl_parse_namestring(const char *str, uint32_t len);
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
        return cl_parse_namestring(s->data, s->length);
    }
}

/* (delete-file pathname) => T */
static CL_Obj bi_delete_file(CL_Obj *args, int n)
{
    const char *path;
    CL_UNUSED(n);
    path = coerce_to_filename(&args[0]);
    if (!path)
        cl_error(CL_ERR_TYPE, "DELETE-FILE: argument must be a pathname designator");
    if (platform_file_delete(path) != 0)
        cl_error(CL_ERR_GENERAL, "DELETE-FILE: cannot delete file");
    return CL_T;
}

/* (rename-file filespec new-name) => new-name, old-truename, new-truename (3 values) */
static CL_Obj bi_rename_file(CL_Obj *args, int n)
{
    const char *old_path, *new_path;
    CL_UNUSED(n);
    old_path = coerce_to_filename(&args[0]);
    if (!old_path)
        cl_error(CL_ERR_TYPE, "RENAME-FILE: first argument must be a pathname designator");
    new_path = coerce_to_filename(&args[1]);
    if (!new_path)
        cl_error(CL_ERR_TYPE, "RENAME-FILE: second argument must be a pathname designator");
    if (platform_file_rename(old_path, new_path) != 0)
        cl_error(CL_ERR_GENERAL, "RENAME-FILE: cannot rename file");
    /* Return 3 values: new-name, old-truename, new-truename */
    cl_mv_values[0] = args[1];
    cl_mv_values[1] = args[0];
    cl_mv_values[2] = args[1];
    cl_mv_count = 3;
    return args[1];
}

/* (file-write-date pathname) => universal-time or NIL */
static CL_Obj bi_file_write_date(CL_Obj *args, int n)
{
    const char *path;
    uint32_t mtime;
    CL_UNUSED(n);
    path = coerce_to_filename(&args[0]);
    if (!path)
        cl_error(CL_ERR_TYPE, "FILE-WRITE-DATE: argument must be a pathname designator");
    mtime = platform_file_mtime(path);
    if (mtime == 0) return CL_NIL;
    return cl_bignum_from_uint32(mtime);
}

/* (directory pathname &key) => list of pathnames */
static CL_Obj bi_directory(CL_Obj *args, int n)
{
    const char *pattern;
    char **entries;
    int count, i;
    CL_Obj result = CL_NIL;
    CL_UNUSED(n);

    pattern = coerce_to_filename(&args[0]);
    if (!pattern)
        cl_error(CL_ERR_TYPE, "DIRECTORY: argument must be a pathname designator");

    entries = platform_directory(pattern, &count);
    if (!entries) return CL_NIL;

    CL_GC_PROTECT(result);
    {
        CL_Obj tail = CL_NIL;
        CL_GC_PROTECT(tail);
        for (i = 0; i < count; i++) {
            extern CL_Obj cl_parse_namestring(const char *str, uint32_t len);
            CL_Obj pn = cl_parse_namestring(entries[i], (uint32_t)strlen(entries[i]));
            CL_Obj cell = cl_cons(pn, CL_NIL);
            if (CL_NULL_P(result)) {
                result = cell;
                tail = cell;
            } else {
                ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
                tail = cell;
            }
            free(entries[i]);
        }
        CL_GC_UNPROTECT(1); /* tail */
    }
    free(entries);
    CL_GC_UNPROTECT(1); /* result */
    return result;
}

/* (%mkdir pathname) => T or NIL — internal for ensure-directories-exist */
static CL_Obj bi_mkdir(CL_Obj *args, int n)
{
    CL_String *str;
    CL_UNUSED(n);
    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "%MKDIR: argument must be a string");
    str = (CL_String *)CL_OBJ_TO_PTR(args[0]);
    return (platform_mkdir(str->data) == 0) ? CL_T : CL_NIL;
}

/* (file-namestring pathname) => string */
static CL_Obj bi_file_namestring(CL_Obj *args, int n)
{
    CL_String *str;
    const char *data;
    uint32_t len, i, last_sep;
    CL_UNUSED(n);
    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "FILE-NAMESTRING: argument must be a string");
    str = (CL_String *)CL_OBJ_TO_PTR(args[0]);
    data = str->data;
    len = str->length;
    last_sep = 0;
    for (i = 0; i < len; i++) {
        if (data[i] == '/' || data[i] == ':')
            last_sep = i + 1;
    }
    return cl_make_string(data + last_sep, len - last_sep);
}

/* (directory-namestring pathname) => string */
static CL_Obj bi_directory_namestring(CL_Obj *args, int n)
{
    CL_String *str;
    const char *data;
    uint32_t len, i, last_sep;
    CL_UNUSED(n);
    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "DIRECTORY-NAMESTRING: argument must be a string");
    str = (CL_String *)CL_OBJ_TO_PTR(args[0]);
    data = str->data;
    len = str->length;
    last_sep = 0;
    for (i = 0; i < len; i++) {
        if (data[i] == '/' || data[i] == ':')
            last_sep = i + 1;
    }
    return cl_make_string(data, last_sep);
}

/* --- Readtable helpers --- */

/* Resolve readtable argument: fixnum index or NIL (= current) */
static int resolve_readtable_idx(CL_Obj *args, int n, int arg_idx)
{
    if (arg_idx < n && !CL_NULL_P(args[arg_idx])) {
        if (CL_FIXNUM_P(args[arg_idx])) {
            int idx = CL_FIXNUM_VAL(args[arg_idx]);
            if (idx >= 0 && idx < CL_RT_POOL_SIZE)
                return idx;
        }
        cl_error(CL_ERR_TYPE, "Not a valid readtable");
    }
    /* Default: current readtable */
    {
        CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_READTABLE);
        CL_Obj val = sym->value;
        if (CL_FIXNUM_P(val)) {
            int idx = CL_FIXNUM_VAL(val);
            if (idx >= 0 && idx < CL_RT_POOL_SIZE)
                return idx;
        }
    }
    return 1; /* fallback */
}

/* (readtablep obj) => T or NIL */
static CL_Obj bi_readtablep(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (CL_FIXNUM_P(args[0])) {
        int idx = CL_FIXNUM_VAL(args[0]);
        if (idx >= 0 && idx < CL_RT_POOL_SIZE &&
            (cl_readtable_alloc_mask & (1u << idx)))
            return CL_T;
    }
    return CL_NIL;
}

/* (get-macro-character char &optional readtable) => fn, non-terminating-p */
static CL_Obj bi_get_macro_character(CL_Obj *args, int n)
{
    int ch, rt_idx;
    CL_Readtable *rt;

    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "GET-MACRO-CHARACTER: first argument must be a character");
    ch = CL_CHAR_VAL(args[0]);
    rt_idx = resolve_readtable_idx(args, n, 1);
    rt = cl_readtable_get(rt_idx);

    if (ch >= 0 && ch < CL_RT_CHARS &&
        (rt->syntax[ch] == CL_CHAR_TERM_MACRO ||
         rt->syntax[ch] == CL_CHAR_NONTERM_MACRO)) {
        /* Return 2 values: function (or NIL for built-in), non-terminating-p */
        cl_mv_count = 2;
        cl_mv_values[0] = rt->macro_fn[ch];
        cl_mv_values[1] = (rt->syntax[ch] == CL_CHAR_NONTERM_MACRO) ? CL_T : CL_NIL;
        return rt->macro_fn[ch];
    }
    cl_mv_count = 2;
    cl_mv_values[0] = CL_NIL;
    cl_mv_values[1] = CL_NIL;
    return CL_NIL;
}

/* (set-macro-character char fn &optional non-terminating-p readtable) => T */
static CL_Obj bi_set_macro_character(CL_Obj *args, int n)
{
    int ch, rt_idx;
    CL_Readtable *rt;
    int non_term = 0;

    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "SET-MACRO-CHARACTER: first argument must be a character");
    ch = CL_CHAR_VAL(args[0]);
    if (ch < 0 || ch >= CL_RT_CHARS)
        cl_error(CL_ERR_TYPE, "SET-MACRO-CHARACTER: character out of range");

    /* args[1] = function */
    if (n > 2 && !CL_NULL_P(args[2]))
        non_term = 1;
    rt_idx = resolve_readtable_idx(args, n, 3);
    rt = cl_readtable_get(rt_idx);

    rt->syntax[ch] = non_term ? CL_CHAR_NONTERM_MACRO : CL_CHAR_TERM_MACRO;
    rt->macro_fn[ch] = args[1];
    return CL_T;
}

/* (make-dispatch-macro-character char &optional non-terminating-p readtable) => T */
static CL_Obj bi_make_dispatch_macro_character(CL_Obj *args, int n)
{
    int ch, rt_idx;
    CL_Readtable *rt;
    int non_term = 0;

    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "MAKE-DISPATCH-MACRO-CHARACTER: first argument must be a character");
    ch = CL_CHAR_VAL(args[0]);
    if (ch < 0 || ch >= CL_RT_CHARS)
        cl_error(CL_ERR_TYPE, "MAKE-DISPATCH-MACRO-CHARACTER: character out of range");

    if (n > 1 && !CL_NULL_P(args[1]))
        non_term = 1;
    rt_idx = resolve_readtable_idx(args, n, 2);
    rt = cl_readtable_get(rt_idx);

    rt->syntax[ch] = non_term ? CL_CHAR_NONTERM_MACRO : CL_CHAR_TERM_MACRO;
    rt->macro_fn[ch] = CL_NIL; /* built-in dispatch handling */
    return CL_T;
}

/* (set-dispatch-macro-character disp-char sub-char fn &optional readtable) => T */
static CL_Obj bi_set_dispatch_macro_character(CL_Obj *args, int n)
{
    int sub_ch, rt_idx;
    CL_Readtable *rt;

    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "SET-DISPATCH-MACRO-CHARACTER: disp-char must be a character");
    if (!CL_CHAR_P(args[1]))
        cl_error(CL_ERR_TYPE, "SET-DISPATCH-MACRO-CHARACTER: sub-char must be a character");
    sub_ch = CL_CHAR_VAL(args[1]);
    if (sub_ch < 0 || sub_ch >= CL_RT_CHARS)
        cl_error(CL_ERR_TYPE, "SET-DISPATCH-MACRO-CHARACTER: sub-char out of range");

    rt_idx = resolve_readtable_idx(args, n, 3);
    rt = cl_readtable_get(rt_idx);

    rt->dispatch_fn[sub_ch] = args[2]; /* fn */
    return CL_T;
}

/* (get-dispatch-macro-character disp-char sub-char &optional readtable) => fn */
static CL_Obj bi_get_dispatch_macro_character(CL_Obj *args, int n)
{
    int sub_ch, rt_idx;
    CL_Readtable *rt;

    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "GET-DISPATCH-MACRO-CHARACTER: disp-char must be a character");
    if (!CL_CHAR_P(args[1]))
        cl_error(CL_ERR_TYPE, "GET-DISPATCH-MACRO-CHARACTER: sub-char must be a character");
    sub_ch = CL_CHAR_VAL(args[1]);
    if (sub_ch < 0 || sub_ch >= CL_RT_CHARS)
        return CL_NIL;

    rt_idx = resolve_readtable_idx(args, n, 2);
    rt = cl_readtable_get(rt_idx);
    return rt->dispatch_fn[sub_ch];
}

/* (copy-readtable &optional from-readtable to-readtable) => readtable */
static CL_Obj bi_copy_readtable(CL_Obj *args, int n)
{
    int from_idx, to_idx, result;

    /* from: default = current readtable */
    from_idx = resolve_readtable_idx(args, n, 0);

    /* to: NIL = allocate new, or fixnum index */
    if (n > 1 && !CL_NULL_P(args[1])) {
        if (!CL_FIXNUM_P(args[1]))
            cl_error(CL_ERR_TYPE, "COPY-READTABLE: to argument must be a readtable or NIL");
        to_idx = CL_FIXNUM_VAL(args[1]);
    } else {
        to_idx = -1; /* allocate new */
    }

    result = cl_readtable_copy(from_idx, to_idx);
    if (result < 0)
        cl_error(CL_ERR_TYPE, "COPY-READTABLE: no free readtable slots");
    return CL_MAKE_FIXNUM(result);
}

/* --- Registration --- */

void cl_builtins_stream_init(void)
{
    /* Pre-intern keyword symbols */
    KW_START   = cl_intern_keyword("START", 5);
    KW_END     = cl_intern_keyword("END", 3);
    KW_ABORT_KW = cl_intern_keyword("ABORT", 5);
    KW_DIRECTION = cl_intern_keyword("DIRECTION", 9);
    KW_INPUT   = cl_intern_keyword("INPUT", 5);
    KW_OUTPUT  = cl_intern_keyword("OUTPUT", 6);
    KW_IO      = cl_intern_keyword("IO", 2);
    KW_PROBE   = cl_intern_keyword("PROBE", 5);
    KW_IF_EXISTS = cl_intern_keyword("IF-EXISTS", 9);
    KW_IF_DOES_NOT_EXIST = cl_intern_keyword("IF-DOES-NOT-EXIST", 17);
    KW_SUPERSEDE = cl_intern_keyword("SUPERSEDE", 9);
    KW_APPEND  = cl_intern_keyword("APPEND", 6);
    KW_ERROR_KW = cl_intern_keyword("ERROR", 5);
    KW_CREATE  = cl_intern_keyword("CREATE", 6);
    KW_NEW_VERSION = cl_intern_keyword("NEW-VERSION", 11);

    /* Step 1 */
    defun("STREAMP", bi_streamp, 1, 1);
    defun("INPUT-STREAM-P", bi_input_stream_p, 1, 1);
    defun("OUTPUT-STREAM-P", bi_output_stream_p, 1, 1);
    defun("INTERACTIVE-STREAM-P", bi_interactive_stream_p, 1, 1);
    cl_register_builtin("%MAKE-TEST-STREAM", bi_make_test_stream, 2, 2, cl_package_clamiga);

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
    defun("MAKE-SYNONYM-STREAM", bi_make_synonym_stream, 1, 1);
    defun("SYNONYM-STREAM-SYMBOL", bi_synonym_stream_symbol, 1, 1);

    /* Step 8: File streams */
    defun("OPEN", bi_open, 1, -1);
    defun("FILE-POSITION", bi_file_position, 1, 2);
    defun("FILE-LENGTH", bi_file_length, 1, 1);

    /* Step 10: Time and file system */
    defun("GET-UNIVERSAL-TIME", bi_get_universal_time, 0, 0);
    defun("PROBE-FILE", bi_probe_file, 1, 1);
    defun("DELETE-FILE", bi_delete_file, 1, 1);
    defun("RENAME-FILE", bi_rename_file, 2, 2);
    defun("FILE-WRITE-DATE", bi_file_write_date, 1, 1);
    cl_register_builtin("%MKDIR", bi_mkdir, 1, 1, cl_package_clamiga);
    defun("DIRECTORY", bi_directory, 1, -1);
    defun("FILE-NAMESTRING", bi_file_namestring, 1, 1);
    defun("DIRECTORY-NAMESTRING", bi_directory_namestring, 1, 1);

    /* Step 12: Readtable */
    defun("READTABLEP", bi_readtablep, 1, 1);
    defun("GET-MACRO-CHARACTER", bi_get_macro_character, 1, 2);
    defun("SET-MACRO-CHARACTER", bi_set_macro_character, 2, 4);
    defun("MAKE-DISPATCH-MACRO-CHARACTER", bi_make_dispatch_macro_character, 1, 3);
    defun("SET-DISPATCH-MACRO-CHARACTER", bi_set_dispatch_macro_character, 3, 4);
    defun("GET-DISPATCH-MACRO-CHARACTER", bi_get_dispatch_macro_character, 2, 3);
    defun("COPY-READTABLE", bi_copy_readtable, 0, 2);
}
