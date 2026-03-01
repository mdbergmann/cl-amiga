#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "printer.h"
#include "compiler.h"
#include "reader.h"
#include "vm.h"
#include "../platform/platform.h"
#include <stdio.h>
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

/* --- I/O --- */

static CL_Obj bi_print(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    cl_print(args[0]);
    return args[0];
}

static CL_Obj bi_prin1(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    cl_prin1(args[0]);
    return args[0];
}

static CL_Obj bi_princ(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    cl_princ(args[0]);
    return args[0];
}

static CL_Obj bi_terpri(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    platform_write_string("\n");
    return CL_NIL;
}

static CL_Obj bi_format(CL_Obj *args, int n)
{
    /* Minimal: (format t "string") just prints */
    CL_UNUSED(n);
    if (n >= 2 && CL_STRING_P(args[1])) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(args[1]);
        /* Simple ~A and ~% substitution */
        const char *p = s->data;
        int ai = 2; /* Next argument index */
        while (*p) {
            if (*p == '~' && *(p+1)) {
                p++;
                if (*p == 'A' || *p == 'a') {
                    if (ai < n) cl_princ(args[ai++]);
                } else if (*p == 'S' || *p == 's') {
                    if (ai < n) cl_prin1(args[ai++]);
                } else if (*p == '%') {
                    platform_write_string("\n");
                } else if (*p == '~') {
                    platform_write_string("~");
                }
                p++;
            } else {
                char c[2] = { *p, '\0' };
                platform_write_string(c);
                p++;
            }
        }
    }
    return CL_NIL;
}

/* --- Load --- */

static CL_Obj bi_load(CL_Obj *args, int n)
{
    CL_String *path_str;
    char *buf;
    unsigned long size;
    CL_ReadStream stream;
    CL_Obj expr, bytecode;

    CL_UNUSED(n);
    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "LOAD: argument must be a string");

    path_str = (CL_String *)CL_OBJ_TO_PTR(args[0]);
    buf = platform_file_read(path_str->data, &size);
    if (!buf)
        cl_error(CL_ERR_GENERAL, "LOAD: cannot open file");

    stream.buf = buf;
    stream.pos = 0;
    stream.len = (int)size;

    for (;;) {
        int err;

        expr = cl_read_from_string(&stream);
        if (cl_reader_eof()) break;

        err = CL_CATCH();
        if (err == CL_ERR_NONE) {
            CL_GC_PROTECT(expr);
            bytecode = cl_compile(expr);
            CL_GC_UNPROTECT(1);
            if (!CL_NULL_P(bytecode))
                cl_vm_eval(bytecode);
            CL_UNCATCH();
        } else {
            cl_error_print();
            CL_UNCATCH();
        }
    }

    platform_free(buf);
    return SYM_T;
}

/* --- Eval / Macroexpand --- */

static CL_Obj bi_eval(CL_Obj *args, int n)
{
    CL_Obj bytecode;
    CL_UNUSED(n);
    CL_GC_PROTECT(args[0]);
    bytecode = cl_compile(args[0]);
    CL_GC_UNPROTECT(1);
    if (CL_NULL_P(bytecode)) return CL_NIL;
    return cl_vm_eval(bytecode);
}

static CL_Obj bi_macroexpand_1(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return cl_macroexpand_1(args[0]);
}

static CL_Obj bi_macroexpand(CL_Obj *args, int n)
{
    CL_Obj form = args[0];
    CL_UNUSED(n);
    for (;;) {
        CL_Obj expanded = cl_macroexpand_1(form);
        if (expanded == form) return form;
        form = expanded;
    }
}

/* --- Throw --- */

static CL_Obj bi_throw(CL_Obj *args, int n)
{
    CL_Obj tag = args[0];
    CL_Obj value = (n > 1) ? args[1] : CL_NIL;
    int i;

    /* Scan NLX stack for matching catch */
    for (i = cl_nlx_top - 1; i >= 0; i--) {
        if (cl_nlx_stack[i].type == CL_NLX_CATCH &&
            cl_nlx_stack[i].tag == tag) {
            int j;
            /* Check for interposing UWPROT frames */
            for (j = cl_nlx_top - 1; j > i; j--) {
                if (cl_nlx_stack[j].type == CL_NLX_UWPROT) {
                    /* Set pending throw, longjmp to UWPROT */
                    cl_pending_throw = 1;
                    cl_pending_tag = tag;
                    cl_pending_value = value;
                    cl_nlx_top = j;
                    longjmp(cl_nlx_stack[j].buf, 1);
                }
            }
            /* No interposing UWPROT — go directly to catch */
            cl_nlx_stack[i].result = value;
            cl_nlx_top = i;
            longjmp(cl_nlx_stack[i].buf, 1);
        }
    }

    cl_error(CL_ERR_GENERAL, "No catch for tag");
    return CL_NIL;
}

/* --- Multiple Values --- */

static CL_Obj bi_values(CL_Obj *args, int n)
{
    int i;
    int count = n < CL_MAX_MV ? n : CL_MAX_MV;
    for (i = 0; i < count; i++)
        cl_mv_values[i] = args[i];
    cl_mv_count = count;
    return n > 0 ? args[0] : CL_NIL;
}

static CL_Obj bi_values_list(CL_Obj *args, int n)
{
    CL_Obj list = args[0];
    int count = 0;
    CL_UNUSED(n);
    while (!CL_NULL_P(list) && count < CL_MAX_MV) {
        cl_mv_values[count++] = cl_car(list);
        list = cl_cdr(list);
    }
    cl_mv_count = count;
    return count > 0 ? cl_mv_values[0] : CL_NIL;
}

/* --- Error --- */

static CL_Obj bi_error(CL_Obj *args, int n)
{
    if (n > 0 && CL_STRING_P(args[0])) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
        cl_error(CL_ERR_GENERAL, "%s", s->data);
    } else {
        cl_error(CL_ERR_GENERAL, "Error signaled");
    }
    return CL_NIL;
}

/* --- Gensym --- */

static uint32_t gensym_counter = 0;

static CL_Obj bi_gensym(CL_Obj *args, int n)
{
    char buf[64];
    const char *prefix = "G";
    CL_Obj name_str, sym;
    int len;

    if (n > 0 && CL_STRING_P(args[0])) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
        prefix = s->data;
    }

    /* Manual int-to-string for vbcc compatibility */
    len = snprintf(buf, sizeof(buf), "%s%lu", prefix, (unsigned long)gensym_counter++);
    name_str = cl_make_string(buf, (uint32_t)len);
    CL_GC_PROTECT(name_str);
    sym = cl_make_symbol(name_str);  /* Uninterned — not in any package */
    CL_GC_UNPROTECT(1);
    return sym;
}

/* --- Registration --- */

void cl_builtins_io_init(void)
{
    /* I/O */
    defun("PRINT", bi_print, 1, 1);
    defun("PRIN1", bi_prin1, 1, 1);
    defun("PRINC", bi_princ, 1, 1);
    defun("TERPRI", bi_terpri, 0, 0);
    defun("FORMAT", bi_format, 1, -1);

    /* Load / Eval */
    defun("LOAD", bi_load, 1, 1);
    defun("EVAL", bi_eval, 1, 1);
    defun("MACROEXPAND-1", bi_macroexpand_1, 1, 1);
    defun("MACROEXPAND", bi_macroexpand, 1, 1);

    /* Error / Throw */
    defun("ERROR", bi_error, 1, -1);
    defun("THROW", bi_throw, 1, 2);

    /* Multiple values */
    defun("VALUES", bi_values, 0, -1);
    defun("VALUES-LIST", bi_values_list, 1, 1);

    /* Gensym */
    defun("GENSYM", bi_gensym, 0, 1);
}
