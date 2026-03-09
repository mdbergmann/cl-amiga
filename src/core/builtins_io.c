#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "printer.h"
#include "compiler.h"
#include "reader.h"
#include "stream.h"
#include "vm.h"
#include "opcodes.h"
#include "float.h"
#include "../platform/platform.h"
#include <stdio.h>
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

/* Helper to register an extension builtin in the EXT package (exported) */
static void extfun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_ext);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    s->value = fn;
    cl_export_symbol(sym, cl_package_ext);
}

/* --- Stream resolution helper (matches builtins_stream.c pattern) --- */

static CL_Obj resolve_output_stream_io(CL_Obj *args, int n, int idx)
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

/* --- Keywords for WRITE --- */

static CL_Obj KW_WR_STREAM;
static CL_Obj KW_WR_ESCAPE;
static CL_Obj KW_WR_READABLY;
static CL_Obj KW_WR_BASE;
static CL_Obj KW_WR_RADIX;
static CL_Obj KW_WR_LEVEL;
static CL_Obj KW_WR_LENGTH;
static CL_Obj KW_WR_CASE;
static CL_Obj KW_WR_GENSYM;
static CL_Obj KW_WR_ARRAY;
static CL_Obj KW_WR_CIRCLE;
static CL_Obj KW_WR_PRETTY;
static CL_Obj KW_WR_RIGHT_MARGIN;
static CL_Obj KW_WR_PPRINT_DISPATCH;

/* --- I/O --- */

/*
 * (write object &key :stream :escape :readably :base :radix :level
 *        :length :case :gensym :array :circle :pretty)
 * Outputs object honoring all *print-* variable overrides.
 * Returns object.
 */
static CL_Obj bi_write(CL_Obj *args, int n)
{
    CL_Obj obj = args[0];
    CL_Obj stream;
    CL_Symbol *sym;
    int i;

    /* Symbol pointers for save/restore */
    CL_Symbol *se = NULL, *sr = NULL, *sb = NULL, *sx = NULL;
    CL_Symbol *sl = NULL, *sn = NULL, *sc = NULL, *sg = NULL;
    CL_Symbol *sa = NULL, *si = NULL, *sp = NULL, *sm = NULL;
    CL_Symbol *sd = NULL;
    CL_Obj prev_e, prev_r, prev_b, prev_x, prev_l, prev_n;
    CL_Obj prev_c, prev_g, prev_a, prev_i, prev_p, prev_m;
    CL_Obj prev_d;

    /* Default: *standard-output* */
    sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STANDARD_OUTPUT);
    stream = sym->value;

    /* Save current values */
    se = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_ESCAPE);    prev_e = se->value;
    sr = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_READABLY);  prev_r = sr->value;
    sb = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_BASE);      prev_b = sb->value;
    sx = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_RADIX);     prev_x = sx->value;
    sl = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_LEVEL);     prev_l = sl->value;
    sn = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_LENGTH);    prev_n = sn->value;
    sc = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_CASE);      prev_c = sc->value;
    sg = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_GENSYM);    prev_g = sg->value;
    sa = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_ARRAY);     prev_a = sa->value;
    si = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_CIRCLE);    prev_i = si->value;
    sp = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_PRETTY);    prev_p = sp->value;
    sm = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_RIGHT_MARGIN); prev_m = sm->value;
    sd = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_PPRINT_DISPATCH); prev_d = sd->value;

    /* Parse keyword arguments (start at index 1, pairs) */
    for (i = 1; i + 1 < n; i += 2) {
        CL_Obj kw = args[i];
        CL_Obj val = args[i + 1];
        if (kw == KW_WR_STREAM) {
            if (val == CL_T) {
                sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_TERMINAL_IO);
                stream = sym->value;
            } else if (!CL_NULL_P(val)) {
                stream = val;
            }
        } else if (kw == KW_WR_ESCAPE) {
            se->value = val;
        } else if (kw == KW_WR_READABLY) {
            sr->value = val;
        } else if (kw == KW_WR_BASE) {
            sb->value = val;
        } else if (kw == KW_WR_RADIX) {
            sx->value = val;
        } else if (kw == KW_WR_LEVEL) {
            sl->value = val;
        } else if (kw == KW_WR_LENGTH) {
            sn->value = val;
        } else if (kw == KW_WR_CASE) {
            sc->value = val;
        } else if (kw == KW_WR_GENSYM) {
            sg->value = val;
        } else if (kw == KW_WR_ARRAY) {
            sa->value = val;
        } else if (kw == KW_WR_CIRCLE) {
            si->value = val;
        } else if (kw == KW_WR_PRETTY) {
            sp->value = val;
        } else if (kw == KW_WR_RIGHT_MARGIN) {
            sm->value = val;
        } else if (kw == KW_WR_PPRINT_DISPATCH) {
            sd->value = val;
        }
    }

    cl_write_to_stream(obj, stream);

    /* Restore all values */
    se->value = prev_e;
    sr->value = prev_r;
    sb->value = prev_b;
    sx->value = prev_x;
    sl->value = prev_l;
    sn->value = prev_n;
    sc->value = prev_c;
    sg->value = prev_g;
    sa->value = prev_a;
    si->value = prev_i;
    sp->value = prev_p;
    sm->value = prev_m;
    sd->value = prev_d;

    return obj;
}

static CL_Obj bi_print(CL_Obj *args, int n)
{
    CL_Obj stream = resolve_output_stream_io(args, n, 1);
    cl_print_to_stream(args[0], stream);
    return args[0];
}

static CL_Obj bi_prin1(CL_Obj *args, int n)
{
    CL_Obj stream = resolve_output_stream_io(args, n, 1);
    cl_prin1_to_stream(args[0], stream);
    return args[0];
}

static CL_Obj bi_princ(CL_Obj *args, int n)
{
    CL_Obj stream = resolve_output_stream_io(args, n, 1);
    cl_princ_to_stream(args[0], stream);
    return args[0];
}

static CL_Obj bi_pprint(CL_Obj *args, int n)
{
    CL_Obj stream = resolve_output_stream_io(args, n, 1);
    CL_Symbol *se = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_ESCAPE);
    CL_Symbol *sp = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_PRETTY);
    CL_Obj prev_e = se->value;
    CL_Obj prev_p = sp->value;
    se->value = SYM_T;
    sp->value = SYM_T;
    cl_stream_write_char(stream, '\n');
    cl_write_to_stream(args[0], stream);
    se->value = prev_e;
    sp->value = prev_p;
    return CL_NIL;
}

/* Defined in builtins_format.c */
extern void cl_format_to_stream(CL_Obj stream, CL_Obj *args, int n);

static CL_Obj bi_format(CL_Obj *args, int n)
{
    CL_Obj dest = (n >= 1) ? args[0] : CL_NIL;

    if (CL_NULL_P(dest)) {
        /* (format nil ...) => return string */
        CL_Obj sstream = cl_make_string_output_stream();
        CL_GC_PROTECT(sstream);
        cl_format_to_stream(sstream, args, n);
        {
            CL_Obj result = cl_get_output_stream_string(sstream);
            /* Free the temp outbuf slot to avoid exhausting the table */
            CL_Stream *tmp_st = (CL_Stream *)CL_OBJ_TO_PTR(sstream);
            cl_stream_free_outbuf(tmp_st->out_buf_handle);
            tmp_st->out_buf_handle = 0;
            CL_GC_UNPROTECT(1);
            return result;
        }
    } else {
        /* T => *standard-output*, stream => that stream */
        CL_Obj stream;
        if (dest == CL_T) {
            CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STANDARD_OUTPUT);
            stream = sym->value;
        } else if (CL_STREAM_P(dest)) {
            stream = dest;
        } else {
            /* Treat anything else as T for backward compat */
            CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STANDARD_OUTPUT);
            stream = sym->value;
        }
        cl_format_to_stream(stream, args, n);
        return CL_NIL;
    }
}

/* --- Load --- */

static CL_Obj bi_load(CL_Obj *args, int n)
{
    CL_String *path_str;
    char *buf;
    unsigned long size;
    CL_Obj stream, expr, bytecode;
    const char *prev_file;
    uint16_t prev_file_id;
    int prev_line;

    /* Per CL spec, LOAD binds *package* so in-package in loaded file
       doesn't affect the caller */
    CL_Obj saved_package = cl_current_package;
    CL_Obj load_pathname_obj, load_truename_obj;
    CL_Symbol *lp_sym, *lt_sym;
    CL_Obj saved_load_pathname, saved_load_truename;

    CL_UNUSED(n);

    /* Sync cl_current_package from *package* special variable,
       so Lisp-level let-bindings of *package* are respected by the reader */
    {
        CL_Symbol *pkg_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_PACKAGE);
        if (!CL_NULL_P(pkg_sym->value))
            cl_current_package = pkg_sym->value;
    }

    if (CL_PATHNAME_P(args[0])) {
        /* Convert pathname to namestring */
        char ns_buf[1024];
        extern const char *cl_coerce_to_namestring(CL_Obj arg, char *buf, uint32_t bufsz);
        cl_coerce_to_namestring(args[0], ns_buf, sizeof(ns_buf));
        args[0] = cl_make_string(ns_buf, (uint32_t)strlen(ns_buf));
    }
    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "LOAD: argument must be a string or pathname");

    path_str = (CL_String *)CL_OBJ_TO_PTR(args[0]);
    buf = platform_file_read(path_str->data, &size);
    if (!buf)
        cl_error(CL_ERR_GENERAL, "LOAD: cannot open file");

    /* Bind *load-pathname* and *load-truename* per CL spec */
    {
        extern CL_Obj cl_parse_namestring(const char *str, uint32_t len);
        load_pathname_obj = cl_parse_namestring(path_str->data,
                                                (uint32_t)strlen(path_str->data));
    }
    load_truename_obj = load_pathname_obj; /* truename = pathname for now */
    CL_GC_PROTECT(load_pathname_obj);
    CL_GC_PROTECT(load_truename_obj);
    lp_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_LOAD_PATHNAME);
    lt_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_LOAD_TRUENAME);
    saved_load_pathname = lp_sym->value;
    saved_load_truename = lt_sym->value;
    lp_sym->value = load_pathname_obj;
    lt_sym->value = load_truename_obj;

    /* Print loading message if *load-verbose* is true */
    {
        CL_Symbol *lv_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_LOAD_VERBOSE);
        if (!CL_NULL_P(lv_sym->value)) {
            platform_write_string("; Loading ");
            platform_write_string(path_str->data);
            platform_write_string("\n");
        }
    }

    /* Save and set source file context */
    prev_file = cl_current_source_file;
    prev_file_id = cl_current_file_id;
    prev_line = cl_reader_get_line();
    cl_current_source_file = path_str->data;
    cl_current_file_id++;
    cl_reader_reset_line();

    /* Use C-buffer stream — file content stays outside GC arena */
    stream = cl_make_cbuf_input_stream(buf, (uint32_t)size);
    CL_GC_PROTECT(stream);

    for (;;) {
        int err;

        expr = cl_read_from_stream(stream);
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

    CL_GC_UNPROTECT(1); /* stream */
    cl_stream_close(stream);
    platform_free(buf);

    /* Restore *load-pathname* and *load-truename* */
    lp_sym->value = saved_load_pathname;
    lt_sym->value = saved_load_truename;
    CL_GC_UNPROTECT(2); /* load_truename_obj, load_pathname_obj */

    /* Restore source file context */
    cl_current_source_file = prev_file;
    cl_current_file_id = prev_file_id;
    cl_reader_set_line(prev_line);

    /* Restore *package* — in-package in loaded file must not leak */
    cl_current_package = saved_package;
    {
        CL_Symbol *pkg_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_PACKAGE);
        pkg_sym->value = saved_package;
    }

    return SYM_T;
}

/* --- Read --- */

static CL_Obj bi_read(CL_Obj *args, int n)
{
    CL_Obj stream;
    int eof_error_p;
    CL_Obj eof_value;
    CL_Obj result;

    /* Resolve stream argument (nil->*standard-input*, t->*terminal-io*) */
    if (n < 1 || CL_NULL_P(args[0])) {
        CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STANDARD_INPUT);
        stream = sym->value;
    } else if (args[0] == CL_T) {
        CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_TERMINAL_IO);
        stream = sym->value;
    } else {
        stream = args[0];
    }

    eof_error_p = (n < 2 || !CL_NULL_P(args[1]));  /* default T */
    eof_value = (n >= 3) ? args[2] : CL_NIL;

    result = cl_read_from_stream(stream);
    if (cl_reader_eof()) {
        if (eof_error_p)
            cl_error(CL_ERR_GENERAL, "READ: end of file");
        return eof_value;
    }
    return result;
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

/* --- Disassemble --- */

typedef struct {
    const char *name;
    uint8_t arg_type;
} DisasmInfo;

static DisasmInfo disasm_opcode_info(uint8_t op)
{
    DisasmInfo info;
    info.name = NULL;
    info.arg_type = OP_ARG_NONE;

    switch (op) {
    case OP_CONST:      info.name = "CONST";      info.arg_type = OP_ARG_U16; break;
    case OP_LOAD:       info.name = "LOAD";       info.arg_type = OP_ARG_U8;  break;
    case OP_STORE:      info.name = "STORE";      info.arg_type = OP_ARG_U8;  break;
    case OP_GLOAD:      info.name = "GLOAD";      info.arg_type = OP_ARG_U16; break;
    case OP_GSTORE:     info.name = "GSTORE";     info.arg_type = OP_ARG_U16; break;
    case OP_UPVAL:      info.name = "UPVAL";      info.arg_type = OP_ARG_U8;  break;
    case OP_POP:        info.name = "POP";        break;
    case OP_DUP:        info.name = "DUP";        break;
    case OP_CONS:       info.name = "CONS";       break;
    case OP_CAR:        info.name = "CAR";        break;
    case OP_CDR:        info.name = "CDR";        break;
    case OP_ADD:        info.name = "ADD";        break;
    case OP_SUB:        info.name = "SUB";        break;
    case OP_MUL:        info.name = "MUL";        break;
    case OP_DIV:        info.name = "DIV";        break;
    case OP_EQ:         info.name = "EQ";         break;
    case OP_LT:         info.name = "LT";         break;
    case OP_GT:         info.name = "GT";         break;
    case OP_LE:         info.name = "LE";         break;
    case OP_GE:         info.name = "GE";         break;
    case OP_NUMEQ:      info.name = "NUMEQ";     break;
    case OP_NOT:        info.name = "NOT";        break;
    case OP_JMP:        info.name = "JMP";        info.arg_type = OP_ARG_I16; break;
    case OP_JNIL:       info.name = "JNIL";       info.arg_type = OP_ARG_I16; break;
    case OP_JTRUE:      info.name = "JTRUE";      info.arg_type = OP_ARG_I16; break;
    case OP_CALL:       info.name = "CALL";       info.arg_type = OP_ARG_U8;  break;
    case OP_TAILCALL:   info.name = "TAILCALL";   info.arg_type = OP_ARG_U8;  break;
    case OP_RET:        info.name = "RET";        break;
    case OP_CLOSURE:    info.name = "CLOSURE";    info.arg_type = OP_ARG_U16; break;
    case OP_APPLY:      info.name = "APPLY";      break;
    case OP_LIST:       info.name = "LIST";       info.arg_type = OP_ARG_U8;  break;
    case OP_NIL:        info.name = "NIL";        break;
    case OP_T:          info.name = "T";          break;
    case OP_FLOAD:      info.name = "FLOAD";      info.arg_type = OP_ARG_U16; break;
    case OP_FSTORE:     info.name = "FSTORE";     info.arg_type = OP_ARG_U16; break;
    case OP_MAKE_CELL:  info.name = "MAKE_CELL";  break;
    case OP_CELL_REF:   info.name = "CELL_REF";   break;
    case OP_CELL_SET_LOCAL: info.name = "CELL_SET_LOCAL"; info.arg_type = OP_ARG_U8; break;
    case OP_CELL_SET_UPVAL: info.name = "CELL_SET_UPVAL"; info.arg_type = OP_ARG_U8; break;
    case OP_DEFMACRO:   info.name = "DEFMACRO";   info.arg_type = OP_ARG_U16; break;
    case OP_DEFTYPE:    info.name = "DEFTYPE";    info.arg_type = OP_ARG_U16; break;
    case OP_HANDLER_PUSH: info.name = "HANDLER_PUSH"; info.arg_type = OP_ARG_U16; break;
    case OP_HANDLER_POP:  info.name = "HANDLER_POP";  info.arg_type = OP_ARG_U8;  break;
    case OP_RESTART_PUSH: info.name = "RESTART_PUSH"; info.arg_type = OP_ARG_U16; break;
    case OP_RESTART_POP:  info.name = "RESTART_POP";  info.arg_type = OP_ARG_U8;  break;
    case OP_ASSERT_TYPE:  info.name = "ASSERT_TYPE";  info.arg_type = OP_ARG_U16; break;
    case OP_BLOCK_PUSH:   info.name = "BLOCK_PUSH";  info.arg_type = OP_ARG_U16; break;
    case OP_BLOCK_POP:    info.name = "BLOCK_POP";   break;
    case OP_BLOCK_RETURN: info.name = "BLOCK_RETURN"; info.arg_type = OP_ARG_U16; break;
    case OP_ARGC:       info.name = "ARGC";       break;
    case OP_CATCH:      info.name = "CATCH";      info.arg_type = OP_ARG_I16; break;
    case OP_UNCATCH:    info.name = "UNCATCH";    break;
    case OP_UWPROT:     info.name = "UWPROT";     info.arg_type = OP_ARG_I16; break;
    case OP_UWPOP:      info.name = "UWPOP";      break;
    case OP_UWRETHROW:  info.name = "UWRETHROW";  break;
    case OP_MV_LOAD:    info.name = "MV_LOAD";    info.arg_type = OP_ARG_U8;  break;
    case OP_MV_TO_LIST: info.name = "MV_TO_LIST"; break;
    case OP_NTH_VALUE:  info.name = "NTH_VALUE";  break;
    case OP_DYNBIND:    info.name = "DYNBIND";    info.arg_type = OP_ARG_U16; break;
    case OP_DYNUNBIND:  info.name = "DYNUNBIND";  info.arg_type = OP_ARG_U8;  break;
    case OP_RPLACA:     info.name = "RPLACA";     break;
    case OP_RPLACD:     info.name = "RPLACD";     break;
    case OP_ASET:       info.name = "ASET";       break;
    case OP_HALT:       info.name = "HALT";       break;
    default: break;
    }
    return info;
}

static void disasm_bytecode(CL_Bytecode *bc)
{
    uint8_t *code = bc->code;
    uint32_t len = bc->code_len;
    uint32_t ip = 0;
    char line[256];
    char annot[128];

    /* Header */
    platform_write_string("Disassembly of ");
    if (!CL_NULL_P(bc->name)) {
        cl_prin1_to_string(bc->name, annot, sizeof(annot));
        platform_write_string(annot);
    } else {
        platform_write_string("#<anonymous>");
    }
    platform_write_string(":\n");

    /* Metadata */
    {
        int req = bc->arity & 0x7FFF;
        int has_rest = (bc->arity >> 15) & 1;
        snprintf(line, sizeof(line), "  %d required, %d optional, %d key%s%s\n",
                req, (int)bc->n_optional, (int)bc->n_keys,
                has_rest ? ", &rest" : "",
                (bc->flags & 2) ? ", &allow-other-keys" : "");
        platform_write_string(line);
        snprintf(line, sizeof(line), "  %lu locals, %lu upvalues\n",
                (unsigned long)bc->n_locals, (unsigned long)bc->n_upvalues);
        platform_write_string(line);
        snprintf(line, sizeof(line), "  %lu bytes, %lu constants\n\n",
                (unsigned long)len, (unsigned long)bc->n_constants);
        platform_write_string(line);
    }

    /* Instructions */
    while (ip < len) {
        uint32_t start_ip = ip;
        uint8_t op = code[ip++];
        DisasmInfo info = disasm_opcode_info(op);

        if (!info.name) {
            snprintf(line, sizeof(line), "  %04lu: ???          0x%02X\n",
                    (unsigned long)start_ip, (unsigned int)op);
            platform_write_string(line);
            continue;
        }

        switch (info.arg_type) {
        case OP_ARG_NONE:
            snprintf(line, sizeof(line), "  %04lu: %s\n",
                    (unsigned long)start_ip, info.name);
            platform_write_string(line);
            break;

        case OP_ARG_U8: {
            uint8_t val = code[ip++];
            snprintf(line, sizeof(line), "  %04lu: %-12s %u\n",
                    (unsigned long)start_ip, info.name, (unsigned int)val);
            platform_write_string(line);
            break;
        }

        case OP_ARG_U16: {
            uint16_t val = (uint16_t)((code[ip] << 8) | code[ip + 1]);
            ip += 2;
            annot[0] = '\0';
            if (val < bc->n_constants &&
                (op == OP_CONST || op == OP_GLOAD || op == OP_GSTORE ||
                 op == OP_FLOAD || op == OP_DEFMACRO || op == OP_DEFTYPE || op == OP_DYNBIND ||
                 op == OP_CLOSURE || op == OP_HANDLER_PUSH || op == OP_ASSERT_TYPE ||
                 op == OP_BLOCK_PUSH || op == OP_BLOCK_RETURN)) {
                cl_prin1_to_string(bc->constants[val], annot, sizeof(annot));
            }
            if (annot[0]) {
                snprintf(line, sizeof(line), "  %04lu: %-12s %-4u ; %s\n",
                        (unsigned long)start_ip, info.name,
                        (unsigned int)val, annot);
            } else {
                snprintf(line, sizeof(line), "  %04lu: %-12s %u\n",
                        (unsigned long)start_ip, info.name, (unsigned int)val);
            }
            platform_write_string(line);

            /* OP_BLOCK_PUSH: also has i16 offset after u16 const_idx */
            if (op == OP_BLOCK_PUSH) {
                int16_t boff = (int16_t)((code[ip] << 8) | code[ip + 1]);
                ip += 2;
                snprintf(line, sizeof(line),
                        "          offset %+d -> %04lu\n",
                        (int)boff, (unsigned long)((int32_t)ip + (int32_t)boff));
                platform_write_string(line);
            }

            /* OP_CLOSURE: read and print capture descriptors */
            if (op == OP_CLOSURE && val < bc->n_constants) {
                CL_Obj tmpl = bc->constants[val];
                if (CL_BYTECODE_P(tmpl)) {
                    CL_Bytecode *tmpl_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(tmpl);
                    int ci;
                    for (ci = 0; ci < tmpl_bc->n_upvalues; ci++) {
                        uint8_t is_local = code[ip++];
                        uint8_t cap_idx = code[ip++];
                        snprintf(line, sizeof(line),
                                "          capture %d: %s slot %u\n",
                                ci, is_local ? "local" : "upval",
                                (unsigned int)cap_idx);
                        platform_write_string(line);
                    }
                }
            }
            break;
        }

        case OP_ARG_I16: {
            int16_t val = (int16_t)((code[ip] << 8) | code[ip + 1]);
            ip += 2;
            snprintf(line, sizeof(line), "  %04lu: %-12s %+d    ; -> %04lu\n",
                    (unsigned long)start_ip, info.name, (int)val,
                    (unsigned long)((int32_t)ip + (int32_t)val));
            platform_write_string(line);
            break;
        }
        }
    }

    /* Constants pool */
    if (bc->n_constants > 0) {
        uint16_t ci;
        platform_write_string("\nConstants:\n");
        for (ci = 0; ci < bc->n_constants; ci++) {
            cl_prin1_to_string(bc->constants[ci], annot, sizeof(annot));
            snprintf(line, sizeof(line), "  %u: %s\n",
                    (unsigned int)ci, annot);
            platform_write_string(line);
        }
    }
    platform_write_string("\n");
}

static CL_Obj bi_disassemble(CL_Obj *args, int n)
{
    CL_Obj arg = args[0];
    CL_Bytecode *bc = NULL;
    CL_UNUSED(n);

    /* Accept symbol — resolve to function binding (same fallback as OP_FLOAD) */
    if (CL_SYMBOL_P(arg)) {
        CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(arg);
        if (sym->function != CL_UNBOUND && !CL_NULL_P(sym->function))
            arg = sym->function;
        else if (sym->value != CL_UNBOUND && !CL_NULL_P(sym->value))
            arg = sym->value;
        else
            cl_error(CL_ERR_UNDEFINED, "DISASSEMBLE: no function binding");
    }

    if (CL_BYTECODE_P(arg)) {
        bc = (CL_Bytecode *)CL_OBJ_TO_PTR(arg);
    } else if (CL_CLOSURE_P(arg)) {
        CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(arg);
        if (CL_BYTECODE_P(cl->bytecode))
            bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode);
    } else if (CL_FUNCTION_P(arg)) {
        CL_Function *fn = (CL_Function *)CL_OBJ_TO_PTR(arg);
        char nbuf[128];
        platform_write_string("Built-in function: ");
        cl_prin1_to_string(fn->name, nbuf, sizeof(nbuf));
        platform_write_string(nbuf);
        platform_write_string("\n  (no bytecode to disassemble)\n");
        return CL_NIL;
    }

    if (!bc)
        cl_error(CL_ERR_TYPE, "DISASSEMBLE: argument must be a function or symbol");

    disasm_bytecode(bc);
    return CL_NIL;
}

/* --- Timing --- */

static CL_Obj bi_get_internal_time(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    return CL_MAKE_FIXNUM((int32_t)(platform_time_ms() & 0x7FFFFFFF));
}

static CL_Obj bi_get_bytes_consed(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    return CL_MAKE_FIXNUM((int32_t)(cl_heap.total_consed & 0x7FFFFFFF));
}

static CL_Obj bi_get_gc_count(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    return CL_MAKE_FIXNUM((int32_t)(cl_heap.gc_count & 0x7FFFFFFF));
}

static CL_Obj bi_time_report(CL_Obj *args, int n)
{
    uint32_t start_time, end_time, elapsed;
    uint32_t start_consed, end_consed, bytes_consed;
    uint32_t start_gc, end_gc, gc_cycles;
    char buf[256];
    CL_UNUSED(n);

    start_time = (uint32_t)CL_FIXNUM_VAL(args[0]);
    start_consed = (uint32_t)CL_FIXNUM_VAL(args[1]);
    start_gc = (uint32_t)CL_FIXNUM_VAL(args[2]);

    end_time = platform_time_ms() & 0x7FFFFFFF;
    end_consed = cl_heap.total_consed & 0x7FFFFFFF;
    end_gc = cl_heap.gc_count & 0x7FFFFFFF;

    elapsed = (end_time >= start_time)
        ? (end_time - start_time)
        : ((0x7FFFFFFF - start_time) + end_time + 1);
    bytes_consed = (end_consed >= start_consed)
        ? (end_consed - start_consed)
        : ((0x7FFFFFFF - start_consed) + end_consed + 1);
    gc_cycles = (end_gc >= start_gc)
        ? (end_gc - start_gc)
        : ((0x7FFFFFFF - start_gc) + end_gc + 1);

    snprintf(buf, sizeof(buf),
             "Evaluation took %lu ms; %lu bytes consed; %lu GC cycles; %lu/%lu heap bytes used.\n",
             (unsigned long)elapsed,
             (unsigned long)bytes_consed,
             (unsigned long)gc_cycles,
             (unsigned long)cl_heap.total_allocated,
             (unsigned long)cl_heap.arena_size);
    platform_write_string(buf);
    return CL_NIL;
}

static CL_Obj bi_get_internal_real_time(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    return CL_MAKE_FIXNUM((int32_t)(platform_time_ms() & 0x7FFFFFFF));
}

static CL_Obj bi_sleep(CL_Obj *args, int n)
{
    uint32_t ms;
    CL_UNUSED(n);
    if (CL_FIXNUM_P(args[0])) {
        int32_t sec = CL_FIXNUM_VAL(args[0]);
        if (sec < 0)
            cl_error(CL_ERR_TYPE, "SLEEP: argument must be non-negative");
        ms = (uint32_t)sec * 1000;
    } else if (CL_HEAP_P(args[0]) &&
               (CL_HDR_TYPE(CL_OBJ_TO_PTR(args[0])) == TYPE_SINGLE_FLOAT ||
                CL_HDR_TYPE(CL_OBJ_TO_PTR(args[0])) == TYPE_DOUBLE_FLOAT)) {
        double val = cl_to_double(args[0]);
        if (val < 0.0)
            cl_error(CL_ERR_TYPE, "SLEEP: argument must be non-negative");
        ms = (uint32_t)(val * 1000.0);
    } else {
        cl_error(CL_ERR_TYPE, "SLEEP: argument must be a non-negative real number");
        return CL_NIL;
    }
    platform_sleep_ms(ms);
    return CL_NIL;
}

/* (compile name &optional definition) */
static CL_Obj bi_compile(CL_Obj *args, int n)
{
    CL_Obj name = args[0];

    if (n > 1 && !CL_NULL_P(args[1])) {
        /* (compile nil '(lambda (x) x)) — compile lambda form, return function */
        CL_Obj bc = cl_compile(args[1]);
        CL_Obj fn = cl_vm_eval(bc);
        /* Return 3 values per CL spec: function, warnings-p, failure-p */
        cl_mv_count = 3;
        cl_mv_values[0] = fn;
        cl_mv_values[1] = CL_NIL;
        cl_mv_values[2] = CL_NIL;
        return fn;
    }

    if (!CL_NULL_P(name) && CL_SYMBOL_P(name)) {
        /* (compile 'foo) — return existing function binding */
        CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(name);
        CL_Obj fn = sym->function;
        cl_mv_count = 3;
        cl_mv_values[0] = fn;
        cl_mv_values[1] = CL_NIL;
        cl_mv_values[2] = CL_NIL;
        return fn;
    }

    return CL_NIL;
}

/* --- Pretty-printing builtins --- */

static CL_Obj KW_LINEAR;
static CL_Obj KW_FILL;
static CL_Obj KW_MISER;
static CL_Obj KW_MANDATORY;
static CL_Obj KW_BLOCK;
static CL_Obj KW_CURRENT;

/*
 * (pprint-newline kind &optional stream)
 * kind: :linear :fill :miser :mandatory
 */
static CL_Obj bi_pprint_newline(CL_Obj *args, int n)
{
    CL_Obj kind = args[0];
    CL_UNUSED(n);

    if (kind == KW_MANDATORY) {
        cl_pp_newline_indent();
    } else if (kind == KW_FILL || kind == KW_LINEAR) {
        /* Greedy: break when past right margin */
        if (cl_pp_get_column() >= cl_pp_get_right_margin())
            cl_pp_newline_indent();
    } else if (kind == KW_MISER) {
        /* Break when close to right margin */
        if (cl_pp_get_column() >= cl_pp_get_right_margin() - 4)
            cl_pp_newline_indent();
    } else {
        cl_error(CL_ERR_TYPE, "PPRINT-NEWLINE: invalid kind");
    }
    return CL_NIL;
}

/*
 * (pprint-indent relative-to n &optional stream)
 * relative-to: :block or :current
 */
static CL_Obj bi_pprint_indent(CL_Obj *args, int n)
{
    CL_Obj rel = args[0];
    int32_t delta;
    CL_UNUSED(n);

    if (!CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE, "PPRINT-INDENT: n must be an integer");
    delta = CL_FIXNUM_VAL(args[1]);

    if (rel == KW_CURRENT) {
        cl_pp_set_indent(cl_pp_get_column() + delta);
    } else {
        /* :block — would need block start column; use current indent as approximation */
        cl_pp_set_indent(delta);
    }
    return CL_NIL;
}

/* Internal builtins for pprint-logical-block */
static CL_Obj bi_pp_push_block(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    cl_pp_push_block(cl_pp_get_column());
    return CL_NIL;
}

static CL_Obj bi_pp_pop_block(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    cl_pp_pop_block();
    return CL_NIL;
}

/* --- Pprint-dispatch table --- */

/* Table is an alist: ((type-spec priority . function) ...) stored in *print-pprint-dispatch* */

/*
 * (set-pprint-dispatch type-spec function &optional priority table)
 * If function is nil, removes the entry.
 */
static CL_Obj bi_set_pprint_dispatch(CL_Obj *args, int n)
{
    CL_Obj type_spec = args[0];
    CL_Obj function = args[1];
    int32_t priority = 0;
    CL_Symbol *sym;
    CL_Obj table, prev, cur, entry, new_entry;

    if (n >= 3 && CL_FIXNUM_P(args[2]))
        priority = CL_FIXNUM_VAL(args[2]);

    /* Get the dispatch table (or use the 4th arg if provided) */
    sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_PPRINT_DISPATCH);
    if (n >= 4 && !CL_NULL_P(args[3]))
        table = args[3];
    else
        table = sym->value;

    /* If function is nil, remove matching entry */
    if (CL_NULL_P(function)) {
        prev = CL_NIL;
        cur = table;
        while (!CL_NULL_P(cur)) {
            entry = cl_car(cur);
            if (cl_car(entry) == type_spec) {
                if (CL_NULL_P(prev))
                    table = cl_cdr(cur);
                else {
                    /* Can't rplacd easily from C, rebuild without this entry */
                    CL_Obj result = CL_NIL;
                    CL_Obj scan = table;
                    CL_GC_PROTECT(result);
                    while (!CL_NULL_P(scan)) {
                        if (scan != cur)
                            result = cl_cons(cl_car(scan), result);
                        scan = cl_cdr(scan);
                    }
                    CL_GC_UNPROTECT(1);
                    table = result;
                }
                break;
            }
            prev = cur;
            cur = cl_cdr(cur);
        }
        sym->value = table;
        return CL_NIL;
    }

    /* Remove existing entry for this type-spec first */
    {
        CL_Obj result = CL_NIL;
        CL_GC_PROTECT(result);
        cur = table;
        while (!CL_NULL_P(cur)) {
            entry = cl_car(cur);
            if (cl_car(entry) != type_spec)
                result = cl_cons(cl_car(cur), result);
            cur = cl_cdr(cur);
        }
        CL_GC_UNPROTECT(1);
        table = result;
    }

    /* Build new entry: (type-spec priority . function) */
    new_entry = cl_cons(CL_MAKE_FIXNUM(priority), function);
    CL_GC_PROTECT(new_entry);
    new_entry = cl_cons(type_spec, new_entry);
    CL_GC_UNPROTECT(1);

    /* Cons onto front of table */
    CL_GC_PROTECT(new_entry);
    table = cl_cons(new_entry, table);
    CL_GC_UNPROTECT(1);

    sym->value = table;
    return CL_NIL;
}

/*
 * (pprint-dispatch object &optional table)
 * Returns 2 values: function, found-p
 */
static CL_Obj bi_pprint_dispatch(CL_Obj *args, int n)
{
    CL_Obj object = args[0];
    CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_PPRINT_DISPATCH);
    CL_Obj table = (n >= 2 && !CL_NULL_P(args[1])) ? args[1] : sym->value;
    CL_Obj best_fn = CL_NIL;
    int32_t best_priority = -999999;
    CL_Obj cur, entry;

    cur = table;
    while (!CL_NULL_P(cur)) {
        CL_Obj type_spec, prio_fn;
        int32_t prio;
        CL_Obj fn;
        CL_Obj typep_args[2];
        CL_Obj typep_result;

        entry = cl_car(cur);
        type_spec = cl_car(entry);
        prio_fn = cl_cdr(entry);
        prio = CL_FIXNUM_P(cl_car(prio_fn)) ? CL_FIXNUM_VAL(cl_car(prio_fn)) : 0;
        fn = cl_cdr(prio_fn);

        /* Check if object matches type-spec using typep */
        typep_args[0] = object;
        typep_args[1] = type_spec;
        typep_result = cl_typep(object, type_spec) ? SYM_T : CL_NIL;

        if (!CL_NULL_P(typep_result) && prio > best_priority) {
            best_priority = prio;
            best_fn = fn;
        }

        cur = cl_cdr(cur);
    }

    cl_mv_count = 2;
    cl_mv_values[0] = best_fn;
    cl_mv_values[1] = CL_NULL_P(best_fn) ? CL_NIL : SYM_T;
    return best_fn;
}

/*
 * (copy-pprint-dispatch &optional table)
 * Shallow copy the alist.
 */
static CL_Obj bi_copy_pprint_dispatch(CL_Obj *args, int n)
{
    CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_PPRINT_DISPATCH);
    CL_Obj table = (n >= 1 && !CL_NULL_P(args[0])) ? args[0] : sym->value;
    CL_Obj result = CL_NIL;
    CL_Obj cur;

    /* Rebuild list (reverses order, but that's fine for alists) */
    cur = table;
    while (!CL_NULL_P(cur)) {
        CL_GC_PROTECT(result);
        result = cl_cons(cl_car(cur), result);
        CL_GC_UNPROTECT(1);
        cur = cl_cdr(cur);
    }
    return result;
}

/* --- Modules (provide / require) --- */

/* Get module name as a C string from a string or symbol argument */
static const char *module_name_cstr(CL_Obj arg, uint32_t *len_out)
{
    if (CL_STRING_P(arg)) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(arg);
        *len_out = s->length;
        return s->data;
    }
    if (CL_SYMBOL_P(arg)) {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(arg);
        CL_String *name = (CL_String *)CL_OBJ_TO_PTR(s->name);
        *len_out = name->length;
        return name->data;
    }
    cl_error(CL_ERR_TYPE, "module name must be a string or symbol");
    *len_out = 0;
    return NULL;
}

/* Check if module name is in *modules* list (string= comparison) */
static int module_provided_p(const char *name, uint32_t len)
{
    CL_Symbol *mod_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_MODULES);
    CL_Obj list = mod_sym->value;
    while (!CL_NULL_P(list)) {
        CL_Obj entry = cl_car(list);
        if (CL_STRING_P(entry)) {
            CL_String *s = (CL_String *)CL_OBJ_TO_PTR(entry);
            if (s->length == len && memcmp(s->data, name, len) == 0)
                return 1;
        }
        list = cl_cdr(list);
    }
    return 0;
}

/*
 * (provide module-name)
 * Add module-name to *modules* if not already present.
 */
static CL_Obj bi_provide(CL_Obj *args, int n)
{
    uint32_t len;
    const char *name;
    CL_Symbol *mod_sym;
    CL_Obj name_str;

    CL_UNUSED(n);
    name = module_name_cstr(args[0], &len);

    if (module_provided_p(name, len))
        return SYM_T;

    /* Push string onto *modules* */
    mod_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_MODULES);
    name_str = cl_make_string(name, len);
    CL_GC_PROTECT(name_str);
    mod_sym->value = cl_cons(name_str, mod_sym->value);
    CL_GC_UNPROTECT(1);
    return SYM_T;
}

/*
 * (require module-name &optional pathnames)
 * Load module if not already provided.
 */
static CL_Obj bi_require(CL_Obj *args, int n)
{
    uint32_t len;
    const char *name;

    name = module_name_cstr(args[0], &len);

    /* Already provided? */
    if (module_provided_p(name, len))
        return CL_NIL;

    if (n >= 2 && !CL_NULL_P(args[1])) {
        /* Pathnames provided */
        CL_Obj pathnames = args[1];
        if (CL_STRING_P(pathnames) || CL_PATHNAME_P(pathnames)) {
            /* Single pathname */
            CL_Obj load_args[1];
            load_args[0] = pathnames;
            bi_load(load_args, 1);
        } else if (CL_CONS_P(pathnames)) {
            /* List of pathnames */
            while (!CL_NULL_P(pathnames)) {
                CL_Obj load_args[1];
                load_args[0] = cl_car(pathnames);
                bi_load(load_args, 1);
                pathnames = cl_cdr(pathnames);
            }
        } else {
            cl_error(CL_ERR_TYPE, "REQUIRE: pathnames must be a string, pathname, or list");
        }
    } else {
        /* Implementation-defined search: try lib/<module-name>.lisp */
        char path[256];
        CL_Obj load_args[1];
        CL_Obj path_obj;
        char *buf;
        unsigned long size;

        snprintf(path, sizeof(path), "lib/%.*s.lisp", (int)len, name);

        /* Check if file exists */
        buf = platform_file_read(path, &size);
        if (!buf) {
#ifdef PLATFORM_AMIGA
            snprintf(path, sizeof(path), "PROGDIR:lib/%.*s.lisp", (int)len, name);
            buf = platform_file_read(path, &size);
            if (!buf)
                cl_error(CL_ERR_GENERAL, "REQUIRE: cannot find module file");
#else
            cl_error(CL_ERR_GENERAL, "REQUIRE: cannot find module file");
#endif
        }
        platform_free(buf);

        path_obj = cl_make_string(path, (uint32_t)strlen(path));
        CL_GC_PROTECT(path_obj);
        load_args[0] = path_obj;
        bi_load(load_args, 1);
        CL_GC_UNPROTECT(1);
    }

    return SYM_T;
}

/* --- Environment Info --- */

static CL_Obj bi_lisp_implementation_type(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    return cl_make_string("CL-Amiga", 8);
}

static CL_Obj bi_lisp_implementation_version(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    return cl_make_string("0.1.0", 5);
}

static CL_Obj bi_software_type(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
#ifdef PLATFORM_AMIGA
    return cl_make_string("AmigaOS", 7);
#elif defined(__APPLE__)
    return cl_make_string("Darwin", 6);
#else
    return cl_make_string("Linux", 5);
#endif
}

static CL_Obj bi_software_version(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    return CL_NIL;
}

static CL_Obj bi_machine_type(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
#ifdef PLATFORM_AMIGA
    return cl_make_string("m68k", 4);
#elif defined(__aarch64__)
    return cl_make_string("aarch64", 7);
#elif defined(__x86_64__)
    return cl_make_string("x86-64", 6);
#else
    return cl_make_string("unknown", 7);
#endif
}

static CL_Obj bi_machine_version(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    return CL_NIL;
}

static CL_Obj bi_machine_instance(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    return CL_NIL;
}

static CL_Obj bi_getenv(CL_Obj *args, int n)
{
    CL_Obj name_obj = args[0];
    const char *result;
    CL_UNUSED(n);
    if (!CL_STRING_P(name_obj))
        cl_error(CL_ERR_TYPE, "EXT:GETENV: argument must be a string");
    {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(name_obj);
        char buf[256];
        result = platform_getenv(s->data, buf, sizeof(buf));
        if (!result) return CL_NIL;
        return cl_make_string(result, (uint32_t)strlen(result));
    }
}

static CL_Obj bi_getcwd(CL_Obj *args, int n)
{
    char buf[1024];
    int len;
    CL_UNUSED(args); CL_UNUSED(n);
    len = platform_getcwd(buf, sizeof(buf));
    if (len == 0)
        cl_error(CL_ERR_GENERAL, "EXT:GETCWD: could not determine current directory");
    return cl_make_string(buf, (uint32_t)len);
}

static CL_Obj bi_system_command(CL_Obj *args, int n)
{
    CL_Obj cmd_obj = args[0];
    CL_String *s;
    int exit_code;
    CL_UNUSED(n);
    if (!CL_STRING_P(cmd_obj))
        cl_error(CL_ERR_TYPE, "EXT:SYSTEM-COMMAND: argument must be a string");
    s = (CL_String *)CL_OBJ_TO_PTR(cmd_obj);
    exit_code = platform_system(s->data);
    return CL_MAKE_FIXNUM(exit_code);
}

/* (ext:open-tcp-stream host port) => stream or NIL
 * Like LispWorks comm:open-tcp-stream / CLISP socket:socket-connect.
 * Returns a bidirectional binary stream connected to host:port. */
static CL_Obj bi_open_tcp_stream(CL_Obj *args, int n)
{
    CL_Obj host_obj = args[0];
    CL_Obj port_obj = args[1];
    CL_String *host_str;
    int port;
    CL_Obj stream;
    CL_UNUSED(n);
    if (!CL_STRING_P(host_obj))
        cl_error(CL_ERR_TYPE, "EXT:OPEN-TCP-STREAM: host must be a string");
    if (!CL_FIXNUM_P(port_obj))
        cl_error(CL_ERR_TYPE, "EXT:OPEN-TCP-STREAM: port must be an integer");
    host_str = (CL_String *)CL_OBJ_TO_PTR(host_obj);
    port = CL_FIXNUM_VAL(port_obj);
    if (port < 1 || port > 65535)
        cl_error(CL_ERR_GENERAL, "EXT:OPEN-TCP-STREAM: port must be 1-65535");
    stream = cl_make_socket_stream(host_str->data, port);
    if (CL_NULL_P(stream))
        cl_error(CL_ERR_GENERAL, "EXT:OPEN-TCP-STREAM: failed to connect to %s:%d",
                 host_str->data, port);
    return stream;
}

/* --- Registration --- */

void cl_builtins_io_init(void)
{
    /* Intern keywords for WRITE */
    KW_WR_STREAM   = cl_intern_keyword("STREAM", 6);
    KW_WR_ESCAPE   = cl_intern_keyword("ESCAPE", 6);
    KW_WR_READABLY = cl_intern_keyword("READABLY", 8);
    KW_WR_BASE     = cl_intern_keyword("BASE", 4);
    KW_WR_RADIX    = cl_intern_keyword("RADIX", 5);
    KW_WR_LEVEL    = cl_intern_keyword("LEVEL", 5);
    KW_WR_LENGTH   = cl_intern_keyword("LENGTH", 6);
    KW_WR_CASE     = cl_intern_keyword("CASE", 4);
    KW_WR_GENSYM   = cl_intern_keyword("GENSYM", 6);
    KW_WR_ARRAY    = cl_intern_keyword("ARRAY", 5);
    KW_WR_CIRCLE   = cl_intern_keyword("CIRCLE", 6);
    KW_WR_PRETTY   = cl_intern_keyword("PRETTY", 6);
    KW_WR_RIGHT_MARGIN = cl_intern_keyword("RIGHT-MARGIN", 12);
    KW_WR_PPRINT_DISPATCH = cl_intern_keyword("PPRINT-DISPATCH", 15);

    /* I/O */
    defun("WRITE", bi_write, 1, -1);
    defun("PRINT", bi_print, 1, 2);
    defun("PRIN1", bi_prin1, 1, 2);
    defun("PRINC", bi_princ, 1, 2);
    defun("PPRINT", bi_pprint, 1, 2);
    defun("FORMAT", bi_format, 1, -1);

    /* Read / Load / Eval */
    defun("READ", bi_read, 0, -1);
    defun("LOAD", bi_load, 1, 1);
    defun("EVAL", bi_eval, 1, 1);
    defun("MACROEXPAND-1", bi_macroexpand_1, 1, 1);
    defun("MACROEXPAND", bi_macroexpand, 1, 1);

    /* Throw (ERROR is now in builtins_condition.c) */
    defun("THROW", bi_throw, 1, 2);

    /* Multiple values */
    defun("VALUES", bi_values, 0, -1);
    defun("VALUES-LIST", bi_values_list, 1, 1);

    /* Gensym */
    defun("GENSYM", bi_gensym, 0, 1);

    /* Debugging */
    defun("DISASSEMBLE", bi_disassemble, 1, 1);

    /* Timing (internal helpers for TIME special form) */
    cl_register_builtin("%GET-INTERNAL-TIME", bi_get_internal_time, 0, 0, cl_package_clamiga);
    cl_register_builtin("%GET-BYTES-CONSED", bi_get_bytes_consed, 0, 0, cl_package_clamiga);
    cl_register_builtin("%GET-GC-COUNT", bi_get_gc_count, 0, 0, cl_package_clamiga);
    cl_register_builtin("%TIME-REPORT", bi_time_report, 3, 3, cl_package_clamiga);
    defun("GET-INTERNAL-REAL-TIME", bi_get_internal_real_time, 0, 0);
    {
        /* INTERNAL-TIME-UNITS-PER-SECOND — milliseconds */
        CL_Obj sym = cl_intern_in("INTERNAL-TIME-UNITS-PER-SECOND", 30, cl_package_cl);
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
        s->value = CL_MAKE_FIXNUM(1000);
    }

    /* Sleep */
    defun("SLEEP", bi_sleep, 1, 1);

    /* Compile */
    defun("COMPILE", bi_compile, 1, 2);

    /* Modules */
    defun("PROVIDE", bi_provide, 1, 1);
    defun("REQUIRE", bi_require, 1, 2);

    /* Environment */
    defun("LISP-IMPLEMENTATION-TYPE", bi_lisp_implementation_type, 0, 0);
    defun("LISP-IMPLEMENTATION-VERSION", bi_lisp_implementation_version, 0, 0);
    defun("SOFTWARE-TYPE", bi_software_type, 0, 0);
    defun("SOFTWARE-VERSION", bi_software_version, 0, 0);
    defun("MACHINE-TYPE", bi_machine_type, 0, 0);
    defun("MACHINE-VERSION", bi_machine_version, 0, 0);
    defun("MACHINE-INSTANCE", bi_machine_instance, 0, 0);
    /* Extension functions (EXT package) */
    extfun("GETENV", bi_getenv, 1, 1);
    extfun("GETCWD", bi_getcwd, 0, 0);
    extfun("SYSTEM-COMMAND", bi_system_command, 1, 1);
    extfun("OPEN-TCP-STREAM", bi_open_tcp_stream, 2, 2);

    /* Pretty-printing keywords */
    KW_LINEAR    = cl_intern_keyword("LINEAR", 6);
    KW_FILL      = cl_intern_keyword("FILL", 4);
    KW_MISER     = cl_intern_keyword("MISER", 5);
    KW_MANDATORY = cl_intern_keyword("MANDATORY", 9);
    KW_BLOCK     = cl_intern_keyword("BLOCK", 5);
    KW_CURRENT   = cl_intern_keyword("CURRENT", 7);

    /* Pretty-printing builtins */
    defun("PPRINT-NEWLINE", bi_pprint_newline, 1, 2);
    defun("PPRINT-INDENT", bi_pprint_indent, 2, 3);

    /* Internal builtins for pprint-logical-block */
    cl_register_builtin("%PP-PUSH-BLOCK", bi_pp_push_block, 0, 0, cl_package_clamiga);
    cl_register_builtin("%PP-POP-BLOCK", bi_pp_pop_block, 0, 0, cl_package_clamiga);

    /* Pprint-dispatch table */
    defun("SET-PPRINT-DISPATCH", bi_set_pprint_dispatch, 2, 4);
    defun("PPRINT-DISPATCH", bi_pprint_dispatch, 1, 2);
    defun("COPY-PPRINT-DISPATCH", bi_copy_pprint_dispatch, 0, 1);
}
