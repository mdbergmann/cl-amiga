/*
 * builtins_stream.c — Stream-related CL builtins
 *
 * Step 1: streamp, %make-test-stream (internal test helper)
 * Later steps will add read-char, write-char, open, close, etc.
 */

#include "builtins.h"
#include "stream.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
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

/* (%make-test-stream direction type) => stream
 * Internal helper for testing. direction: 1=input, 2=output, 3=io
 * type: 0=console, 1=file, 2=string */
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

void cl_builtins_stream_init(void)
{
    defun("STREAMP", bi_streamp, 1, 1);
    defun("INPUT-STREAM-P", bi_input_stream_p, 1, 1);
    defun("OUTPUT-STREAM-P", bi_output_stream_p, 1, 1);
    defun("%MAKE-TEST-STREAM", bi_make_test_stream, 2, 2);
}
