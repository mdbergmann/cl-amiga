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
#include "fasl.h"
#include "../platform/platform.h"
#include "../platform/platform_thread.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Forward declarations for helpers used in bi_load before definition */
static int make_fasl_cache_path(const char *input, char *output, uint32_t outsize);
static void path_directory(const char *path, char *dir, uint32_t dirsz);
static void mkdir_p(const char *path);

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
    if (idx >= n || CL_NULL_P(args[idx]))
        return cl_symbol_value(SYM_STANDARD_OUTPUT);
    s = args[idx];
    if (s == CL_T)
        return cl_symbol_value(SYM_TERMINAL_IO);
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
    stream = cl_symbol_value(SYM_STANDARD_OUTPUT);

    /* Save current values */
    se = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_ESCAPE);    prev_e = cl_symbol_value(SYM_PRINT_ESCAPE);
    sr = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_READABLY);  prev_r = cl_symbol_value(SYM_PRINT_READABLY);
    sb = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_BASE);      prev_b = cl_symbol_value(SYM_PRINT_BASE);
    sx = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_RADIX);     prev_x = cl_symbol_value(SYM_PRINT_RADIX);
    sl = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_LEVEL);     prev_l = cl_symbol_value(SYM_PRINT_LEVEL);
    sn = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_LENGTH);    prev_n = cl_symbol_value(SYM_PRINT_LENGTH);
    sc = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_CASE);      prev_c = cl_symbol_value(SYM_PRINT_CASE);
    sg = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_GENSYM);    prev_g = cl_symbol_value(SYM_PRINT_GENSYM);
    sa = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_ARRAY);     prev_a = cl_symbol_value(SYM_PRINT_ARRAY);
    si = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_CIRCLE);    prev_i = cl_symbol_value(SYM_PRINT_CIRCLE);
    sp = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_PRETTY);    prev_p = cl_symbol_value(SYM_PRINT_PRETTY);
    sm = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_RIGHT_MARGIN); prev_m = cl_symbol_value(SYM_PRINT_RIGHT_MARGIN);
    sd = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_PPRINT_DISPATCH); prev_d = cl_symbol_value(SYM_PRINT_PPRINT_DISPATCH);

    /* Parse keyword arguments (start at index 1, pairs) */
    for (i = 1; i + 1 < n; i += 2) {
        CL_Obj kw = args[i];
        CL_Obj val = args[i + 1];
        if (kw == KW_WR_STREAM) {
            if (val == CL_T) {
                stream = cl_symbol_value(SYM_TERMINAL_IO);
            } else if (!CL_NULL_P(val)) {
                stream = val;
            }
        } else if (kw == KW_WR_ESCAPE) {
            cl_set_symbol_value(SYM_PRINT_ESCAPE, val);
        } else if (kw == KW_WR_READABLY) {
            cl_set_symbol_value(SYM_PRINT_READABLY, val);
        } else if (kw == KW_WR_BASE) {
            cl_set_symbol_value(SYM_PRINT_BASE, val);
        } else if (kw == KW_WR_RADIX) {
            cl_set_symbol_value(SYM_PRINT_RADIX, val);
        } else if (kw == KW_WR_LEVEL) {
            cl_set_symbol_value(SYM_PRINT_LEVEL, val);
        } else if (kw == KW_WR_LENGTH) {
            cl_set_symbol_value(SYM_PRINT_LENGTH, val);
        } else if (kw == KW_WR_CASE) {
            cl_set_symbol_value(SYM_PRINT_CASE, val);
        } else if (kw == KW_WR_GENSYM) {
            cl_set_symbol_value(SYM_PRINT_GENSYM, val);
        } else if (kw == KW_WR_ARRAY) {
            cl_set_symbol_value(SYM_PRINT_ARRAY, val);
        } else if (kw == KW_WR_CIRCLE) {
            cl_set_symbol_value(SYM_PRINT_CIRCLE, val);
        } else if (kw == KW_WR_PRETTY) {
            cl_set_symbol_value(SYM_PRINT_PRETTY, val);
        } else if (kw == KW_WR_RIGHT_MARGIN) {
            cl_set_symbol_value(SYM_PRINT_RIGHT_MARGIN, val);
        } else if (kw == KW_WR_PPRINT_DISPATCH) {
            cl_set_symbol_value(SYM_PRINT_PPRINT_DISPATCH, val);
        }
    }

    cl_write_to_stream(obj, stream);

    /* Restore all values */
    cl_set_symbol_value(SYM_PRINT_ESCAPE, prev_e);
    cl_set_symbol_value(SYM_PRINT_READABLY, prev_r);
    cl_set_symbol_value(SYM_PRINT_BASE, prev_b);
    cl_set_symbol_value(SYM_PRINT_RADIX, prev_x);
    cl_set_symbol_value(SYM_PRINT_LEVEL, prev_l);
    cl_set_symbol_value(SYM_PRINT_LENGTH, prev_n);
    cl_set_symbol_value(SYM_PRINT_CASE, prev_c);
    cl_set_symbol_value(SYM_PRINT_GENSYM, prev_g);
    cl_set_symbol_value(SYM_PRINT_ARRAY, prev_a);
    cl_set_symbol_value(SYM_PRINT_CIRCLE, prev_i);
    cl_set_symbol_value(SYM_PRINT_PRETTY, prev_p);
    cl_set_symbol_value(SYM_PRINT_RIGHT_MARGIN, prev_m);
    cl_set_symbol_value(SYM_PRINT_PPRINT_DISPATCH, prev_d);

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
    CL_Obj prev_e = cl_symbol_value(SYM_PRINT_ESCAPE);
    CL_Obj prev_p = cl_symbol_value(SYM_PRINT_PRETTY);
    cl_set_symbol_value(SYM_PRINT_ESCAPE, SYM_T);
    cl_set_symbol_value(SYM_PRINT_PRETTY, SYM_T);
    cl_stream_write_char(stream, '\n');
    cl_write_to_stream(args[0], stream);
    cl_set_symbol_value(SYM_PRINT_ESCAPE, prev_e);
    cl_set_symbol_value(SYM_PRINT_PRETTY, prev_p);
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
            stream = cl_symbol_value(SYM_STANDARD_OUTPUT);
        } else if (CL_STREAM_P(dest)) {
            stream = dest;
        } else {
            /* Treat anything else as T for backward compat */
            stream = cl_symbol_value(SYM_STANDARD_OUTPUT);
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
        CL_Obj pkg_val = cl_symbol_value(SYM_STAR_PACKAGE);
        if (!CL_NULL_P(pkg_val))
            cl_current_package = pkg_val;
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
    /* Expand leading ~ to home directory */
    {
        CL_String *tmp_s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
        if (tmp_s->length > 0 && tmp_s->data[0] == '~') {
            char expand_buf[1024];
            const char *expanded = platform_expand_home(tmp_s->data,
                expand_buf, (int)sizeof(expand_buf));
            if (expanded != tmp_s->data) {
                args[0] = cl_make_string(expanded, (uint32_t)strlen(expanded));
            }
        }
    }

    path_str = (CL_String *)CL_OBJ_TO_PTR(args[0]);

    /* Check for cached FASL before reading source (skip .asd — they share
       base names with .lisp source files and must not be FASL-substituted) */
    {
        char cache_path[1024];
        size_t plen2 = strlen(path_str->data);
        int skip_cache = (plen2 >= 4 && strcmp(path_str->data + plen2 - 4, ".asd") == 0);
        if (!skip_cache &&
            make_fasl_cache_path(path_str->data, cache_path, sizeof(cache_path)) &&
            platform_file_exists(cache_path))
        {
            uint32_t src_mtime = platform_file_mtime(path_str->data);
            uint32_t fasl_mtime = platform_file_mtime(cache_path);
            if (fasl_mtime > 0 && fasl_mtime >= src_mtime) {
                /* Load cached FASL instead of source */
                buf = platform_file_read(cache_path, &size);
                if (buf) {
                    /* Verify it's actually a FASL */
                    if (size >= 4) {
                        uint32_t magic = ((uint32_t)(uint8_t)buf[0] << 24) |
                                         ((uint32_t)(uint8_t)buf[1] << 16) |
                                         ((uint32_t)(uint8_t)buf[2] << 8) |
                                         ((uint32_t)(uint8_t)buf[3]);
                        if (magic == CL_FASL_MAGIC) {
                            /* Try loading from FASL; fall back to source on error */
                            int fasl_err;
                            /* Bind load-pathname and load-truename */
                            {
                                extern CL_Obj cl_parse_namestring(const char *str, uint32_t len);
                                load_pathname_obj = cl_parse_namestring(path_str->data,
                                    (uint32_t)strlen(path_str->data));
                            }
                            {
                                char resolved[512];
                                if (platform_realpath(path_str->data, resolved, (int)sizeof(resolved))) {
                                    extern CL_Obj cl_parse_namestring(const char *str, uint32_t len);
                                    load_truename_obj = cl_parse_namestring(resolved,
                                        (uint32_t)strlen(resolved));
                                } else {
                                    load_truename_obj = load_pathname_obj;
                                }
                            }
                            CL_GC_PROTECT(load_pathname_obj);
                            CL_GC_PROTECT(load_truename_obj);
                            lp_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_LOAD_PATHNAME);
                            lt_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_LOAD_TRUENAME);
                            saved_load_pathname = cl_symbol_value(SYM_STAR_LOAD_PATHNAME);
                            saved_load_truename = cl_symbol_value(SYM_STAR_LOAD_TRUENAME);
                            cl_set_symbol_value(SYM_STAR_LOAD_PATHNAME, load_pathname_obj);
                            cl_set_symbol_value(SYM_STAR_LOAD_TRUENAME, load_truename_obj);

                            {
                                CL_Symbol *lv_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_LOAD_VERBOSE);
                                if (!CL_NULL_P(cl_symbol_value(SYM_STAR_LOAD_VERBOSE))) {
                                    platform_write_string("; Loading ");
                                    platform_write_string(cache_path);
                                    platform_write_string("\n");
                                }
                            }

                            {
                                int sf = cl_vm.fp, ss = cl_vm.sp, sn = cl_nlx_top;
                                int saved_gc_roots_fasl = gc_root_count;
                                fasl_err = CL_CATCH();
                                if (fasl_err == CL_ERR_NONE) {
                                    cl_fasl_load((const uint8_t *)buf, (uint32_t)size);
                                    platform_free(buf);
                                    CL_UNCATCH();

                                    cl_set_symbol_value(SYM_STAR_LOAD_PATHNAME, saved_load_pathname);
                                    cl_set_symbol_value(SYM_STAR_LOAD_TRUENAME, saved_load_truename);
                                    CL_GC_UNPROTECT(2);
                                    cl_current_package = saved_package;
                                    {
                                        CL_Symbol *pkg_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_PACKAGE);
                                        cl_set_symbol_value(SYM_STAR_PACKAGE, saved_package);
                                    }
                                    return SYM_T;
                                } else {
                                    /* Restore VM state leaked by aborted cl_vm_eval */
                                    cl_vm.fp = sf;
                                    cl_vm.sp = ss;
                                    cl_nlx_top = sn;
                                    gc_root_count = saved_gc_roots_fasl;  /* Restore leaked GC roots */
                                    /* FASL load failed — restore state, fall through to source */
                                    CL_UNCATCH();
                                platform_free(buf);
                                buf = NULL;
                                cl_set_symbol_value(SYM_STAR_LOAD_PATHNAME, saved_load_pathname);
                                cl_set_symbol_value(SYM_STAR_LOAD_TRUENAME, saved_load_truename);
                                CL_GC_UNPROTECT(2);
                                cl_current_package = saved_package;
                                {
                                    CL_Symbol *pkg_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_PACKAGE);
                                    cl_set_symbol_value(SYM_STAR_PACKAGE, saved_package);
                                }
                                /* Delete broken FASL so we don't retry it */
                                platform_file_delete(cache_path);
                            }
                            }
                        }
                    }
                    if (buf) platform_free(buf);
                }
            }
        }
    }

    buf = platform_file_read(path_str->data, &size);
    if (!buf)
        cl_error(CL_ERR_GENERAL, "LOAD: cannot open file");

    /* Bind *load-pathname* and *load-truename* per CL spec */
    {
        extern CL_Obj cl_parse_namestring(const char *str, uint32_t len);
        load_pathname_obj = cl_parse_namestring(path_str->data,
                                                (uint32_t)strlen(path_str->data));
    }
    /* Resolve truename to absolute path */
    {
        char resolved[512];
        if (platform_realpath(path_str->data, resolved, (int)sizeof(resolved))) {
            extern CL_Obj cl_parse_namestring(const char *str, uint32_t len);
            load_truename_obj = cl_parse_namestring(resolved,
                                                    (uint32_t)strlen(resolved));
        } else {
            load_truename_obj = load_pathname_obj;
        }
    }
    CL_GC_PROTECT(load_pathname_obj);
    CL_GC_PROTECT(load_truename_obj);
    lp_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_LOAD_PATHNAME);
    lt_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_LOAD_TRUENAME);
    saved_load_pathname = cl_symbol_value(SYM_STAR_LOAD_PATHNAME);
    saved_load_truename = cl_symbol_value(SYM_STAR_LOAD_TRUENAME);
    cl_set_symbol_value(SYM_STAR_LOAD_PATHNAME, load_pathname_obj);
    cl_set_symbol_value(SYM_STAR_LOAD_TRUENAME, load_truename_obj);

    /* Print loading message if *load-verbose* is true */
    {
        CL_Symbol *lv_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_LOAD_VERBOSE);
        if (!CL_NULL_P(cl_symbol_value(SYM_STAR_LOAD_VERBOSE))) {
            platform_write_string("; Loading ");
            platform_write_string(path_str->data);
            platform_write_string("\n");
        }
    }

    /* Check if this is a FASL file (by magic bytes or extension) */
    {
        int is_fasl = 0;

        /* Check magic bytes first (most reliable) */
        if (size >= 4) {
            uint32_t magic = ((uint32_t)(uint8_t)buf[0] << 24) |
                             ((uint32_t)(uint8_t)buf[1] << 16) |
                             ((uint32_t)(uint8_t)buf[2] << 8) |
                             ((uint32_t)(uint8_t)buf[3]);
            if (magic == CL_FASL_MAGIC)
                is_fasl = 1;
        }

        if (is_fasl) {
            /* FASL binary loading — no source parsing needed */
            cl_fasl_load((const uint8_t *)buf, (uint32_t)size);
            platform_free(buf);

            /* Restore *load-pathname*, *load-truename*, *package* */
            cl_set_symbol_value(SYM_STAR_LOAD_PATHNAME, saved_load_pathname);
            cl_set_symbol_value(SYM_STAR_LOAD_TRUENAME, saved_load_truename);
            CL_GC_UNPROTECT(2);
            cl_current_package = saved_package;
            {
                CL_Symbol *pkg_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_PACKAGE);
                cl_set_symbol_value(SYM_STAR_PACKAGE, saved_package);
            }
            return SYM_T;
        }
    }

    /* Source loading — also auto-cache to FASL for faster subsequent loads */

    /* Set up FASL auto-cache if cache path is available */
    {
        char auto_cache_path[1024];
        uint8_t *fasl_buf = NULL;
        uint8_t *unit_buf = NULL;
        uint32_t fasl_capacity = 64 * 1024;
        uint32_t unit_capacity = 32 * 1024;
        uint32_t n_units = 0;
        CL_FaslWriter fw;
        int do_cache = 0;

        if (make_fasl_cache_path(path_str->data, auto_cache_path, sizeof(auto_cache_path))) {
            /* Skip auto-caching for .asd files — they are ASDF system definitions
             * and would collide with .lisp files of the same name in the cache
             * (e.g. mt19937.asd and mt19937.lisp both map to mt19937.fasl) */
            size_t plen = strlen(path_str->data);
            int is_asd = (plen >= 4 && strcmp(path_str->data + plen - 4, ".asd") == 0);
            int is_fasl = (plen >= 5 && strcmp(path_str->data + plen - 5, ".fasl") == 0);
            if (!is_asd && !is_fasl) {
            fasl_buf = (uint8_t *)platform_alloc(fasl_capacity);
            unit_buf = (uint8_t *)platform_alloc(unit_capacity);
            if (fasl_buf && unit_buf) {
                do_cache = 1;
                cl_fasl_writer_init(&fw, fasl_buf, fasl_capacity);
                cl_fasl_write_header(&fw, 0); /* patch n_units later */
            } else {
                if (fasl_buf) platform_free(fasl_buf);
                if (unit_buf) platform_free(unit_buf);
                fasl_buf = NULL;
                unit_buf = NULL;
            }
            } /* !is_asd */
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
        int saved_fp, saved_sp, saved_nlx;

        expr = cl_read_from_stream(stream);
        if (cl_reader_eof()) break;

        /* Save VM state so we can restore on error (cl_error longjmps
           past cl_vm_eval's OP_HALT, leaving fp/sp/nlx leaked) */
        saved_fp = cl_vm.fp;
        saved_sp = cl_vm.sp;
        saved_nlx = cl_nlx_top;
        {
        int saved_gc_roots = gc_root_count;

        err = CL_CATCH();
        if (err == CL_ERR_NONE) {
            CL_GC_PROTECT(expr);
            bytecode = cl_compile(expr);
            CL_GC_UNPROTECT(1);
            if (!CL_NULL_P(bytecode)) {
                /* GC-protect bytecode across eval + serialization */
                CL_GC_PROTECT(bytecode);
                cl_vm_eval(bytecode);

                /* Safe point: run pending compaction between top-level forms */
                cl_gc_compact_if_pending();

                /* Serialize bytecode to FASL cache buffer */
                if (do_cache) {
                    CL_FaslWriter uw;
                    cl_fasl_writer_init(&uw, unit_buf, unit_capacity);
                    /* Share gensym dedup table across units */
                    memcpy(uw.gensym_objs, fw.gensym_objs, fw.gensym_count * sizeof(CL_Obj));
                    uw.gensym_count = fw.gensym_count;
                    cl_fasl_serialize_bytecode(&uw, bytecode);
                    while (uw.error == FASL_ERR_OVERFLOW) {
                        platform_free(unit_buf);
                        unit_capacity *= 2;
                        unit_buf = (uint8_t *)platform_alloc(unit_capacity);
                        if (!unit_buf) { do_cache = 0; break; }
                        cl_fasl_writer_init(&uw, unit_buf, unit_capacity);
                        memcpy(uw.gensym_objs, fw.gensym_objs, fw.gensym_count * sizeof(CL_Obj));
                        uw.gensym_count = fw.gensym_count;
                        cl_fasl_serialize_bytecode(&uw, bytecode);
                    }
                    /* Update file-level gensym table */
                    if (do_cache && uw.error == FASL_OK) {
                        memcpy(fw.gensym_objs, uw.gensym_objs, uw.gensym_count * sizeof(CL_Obj));
                        fw.gensym_count = uw.gensym_count;
                    }
                    if (do_cache && uw.error == FASL_OK) {
                        while (fw.pos + 4 + uw.pos > fasl_capacity) {
                            uint8_t *new_buf;
                            fasl_capacity *= 2;
                            new_buf = (uint8_t *)platform_alloc(fasl_capacity);
                            if (!new_buf) { do_cache = 0; break; }
                            memcpy(new_buf, fasl_buf, fw.pos);
                            platform_free(fasl_buf);
                            fasl_buf = new_buf;
                            fw.data = fasl_buf;
                            fw.capacity = fasl_capacity;
                        }
                        if (do_cache) {
                            cl_fasl_write_u32(&fw, uw.pos);
                            cl_fasl_write_bytes(&fw, unit_buf, uw.pos);
                            n_units++;
                        }
                    }
                }
                CL_GC_UNPROTECT(1); /* bytecode */
            }
            CL_UNCATCH();
        } else {
            /* Restore VM state leaked by aborted cl_vm_eval */
            cl_vm.fp = saved_fp;
            cl_vm.sp = saved_sp;
            cl_nlx_top = saved_nlx;
            gc_root_count = saved_gc_roots;  /* Restore leaked GC roots */
            cl_error_print();
            CL_UNCATCH();
            do_cache = 0; /* Don't cache files with errors */
        }
        }
    }

    CL_GC_UNPROTECT(1); /* stream */
    cl_stream_close(stream);
    platform_free(buf);

    /* Write FASL cache file if we collected units */
    if (do_cache && n_units > 0) {
        char dir[1024];
        /* Patch n_units in header */
        fasl_buf[8]  = (uint8_t)(n_units >> 24);
        fasl_buf[9]  = (uint8_t)(n_units >> 16);
        fasl_buf[10] = (uint8_t)(n_units >> 8);
        fasl_buf[11] = (uint8_t)(n_units);
        /* Ensure directory exists */
        path_directory(auto_cache_path, dir, sizeof(dir));
        if (dir[0]) mkdir_p(dir);
        /* Write file */
        {
            PlatformFile fh = platform_file_open(auto_cache_path, PLATFORM_FILE_WRITE);
            if (fh != PLATFORM_FILE_INVALID) {
                platform_file_write_buf(fh, (const char *)fasl_buf, fw.pos);
                platform_file_close(fh);
            }
        }
    }
    if (fasl_buf) platform_free(fasl_buf);
    if (unit_buf) platform_free(unit_buf);
    } /* end auto-cache scope */

    /* Restore *load-pathname* and *load-truename* */
    cl_set_symbol_value(SYM_STAR_LOAD_PATHNAME, saved_load_pathname);
    cl_set_symbol_value(SYM_STAR_LOAD_TRUENAME, saved_load_truename);
    CL_GC_UNPROTECT(2); /* load_truename_obj, load_pathname_obj */

    /* Restore source file context */
    cl_current_source_file = prev_file;
    cl_current_file_id = prev_file_id;
    cl_reader_set_line(prev_line);

    /* Restore *package* — in-package in loaded file must not leak */
    cl_current_package = saved_package;
    {
        CL_Symbol *pkg_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_PACKAGE);
        cl_set_symbol_value(SYM_STAR_PACKAGE, saved_package);
    }

    return SYM_T;
}

/* --- Helper: recursive mkdir (like mkdir -p) --- */

static void mkdir_p(const char *path)
{
    char buf[1024];
    int i, len;
    len = (int)strlen(path);
    if (len >= (int)sizeof(buf)) return;
    memcpy(buf, path, (size_t)len + 1);
    for (i = 1; i < len; i++) {
#ifdef PLATFORM_AMIGA
        if (buf[i] == '/') {
            /* Don't mkdir before the colon (volume name) */
            int has_colon = 0;
            int j;
            for (j = 0; j < i; j++) {
                if (buf[j] == ':') { has_colon = 1; break; }
            }
            if (!has_colon) continue;
            buf[i] = '\0';
            platform_mkdir(buf);
            buf[i] = '/';
        }
#else
        if (buf[i] == '/') {
            buf[i] = '\0';
            platform_mkdir(buf);
            buf[i] = '/';
        }
#endif
    }
    platform_mkdir(buf);
}

/* --- Helper: construct FASL cache path from source path --- */

static int make_fasl_cache_path(const char *input, char *output, uint32_t outsize)
{
    char resolved[1024];
    char root[512];
    const char *src;
    const char *dot;
    size_t root_len, src_len, base_len, total;

    /* Resolve to absolute path — try realpath first, fall back to
       resolving directory + filename for non-existent files */
    if (!platform_realpath(input, resolved, (int)sizeof(resolved))) {
        /* File doesn't exist yet — try resolving directory */
        const char *last_sep = strrchr(input, '/');
#ifdef PLATFORM_AMIGA
        const char *colon = strrchr(input, ':');
        if (colon && (!last_sep || colon > last_sep))
            last_sep = colon;
#endif
        if (last_sep) {
            char dir_buf[1024];
            size_t dir_len = (size_t)(last_sep - input + 1);
            if (dir_len >= sizeof(dir_buf)) return 0;
            memcpy(dir_buf, input, dir_len);
            dir_buf[dir_len] = '\0';
            if (platform_realpath(dir_buf, resolved, (int)sizeof(resolved))) {
                size_t rlen = strlen(resolved);
                const char *fname = last_sep + 1;
                size_t flen = strlen(fname);
                if (rlen + 1 + flen >= sizeof(resolved)) return 0;
                if (rlen > 0 && resolved[rlen - 1] != '/') {
                    resolved[rlen] = '/';
                    rlen++;
                }
                memcpy(resolved + rlen, fname, flen + 1);
            } else {
                return 0;
            }
        } else if (input[0] == '/') {
            /* Already absolute but doesn't exist */
            if (strlen(input) >= sizeof(resolved)) return 0;
            strcpy(resolved, input);
        } else {
            /* Relative path, resolve from cwd */
            char cwd[512];
            size_t clen, ilen;
            if (!platform_getcwd(cwd, (int)sizeof(cwd))) return 0;
            clen = strlen(cwd);
            ilen = strlen(input);
            if (clen + 1 + ilen >= sizeof(resolved)) return 0;
            memcpy(resolved, cwd, clen);
            resolved[clen] = '/';
            memcpy(resolved + clen + 1, input, ilen + 1);
        }
    }

    /* Build cache root */
#ifdef PLATFORM_AMIGA
    snprintf(root, sizeof(root), "S:cl-amiga/faslcache/%s/", CL_VERSION_STRING);
    /* Transform "DH0:path" -> "DH0/path" by replacing ':' with '/' */
    {
        char *colon = strchr(resolved, ':');
        if (colon) *colon = '/';
    }
    src = resolved;
#else
    {
        const char *home = platform_getenv("HOME", root, sizeof(root));
        if (!home) return 0;
        snprintf(root, sizeof(root), "%s/.cache/common-lisp/cl-amiga-%s/",
                 home, CL_VERSION_STRING);
    }
    /* Strip leading '/' */
    src = resolved;
    if (src[0] == '/') src++;
#endif

    root_len = strlen(root);
    src_len = strlen(src);

    /* Find last dot for extension replacement */
    dot = strrchr(src, '.');
    if (dot && dot > strrchr(src, '/')) {
        base_len = (size_t)(dot - src);
    } else {
        base_len = src_len;
    }

    total = root_len + base_len + 5; /* ".fasl" */
    if (total + 1 > outsize) return 0;

    memcpy(output, root, root_len);
    memcpy(output + root_len, src, base_len);
    memcpy(output + root_len + base_len, ".fasl", 5);
    output[root_len + base_len + 5] = '\0';
    return 1;
}

/* --- Helper: get directory portion of path --- */

static void path_directory(const char *path, char *dir, uint32_t dirsize)
{
    const char *last_sep = strrchr(path, '/');
#ifdef PLATFORM_AMIGA
    const char *colon = strrchr(path, ':');
    if (colon && (!last_sep || colon > last_sep))
        last_sep = colon;
#endif
    if (last_sep) {
        size_t len = (size_t)(last_sep - path + 1);
        if (len >= dirsize) len = dirsize - 1;
        memcpy(dir, path, len);
        dir[len] = '\0';
    } else {
        dir[0] = '\0';
    }
}

/* --- Helper: replace extension with .fasl --- */

static void make_fasl_path(const char *input, char *output, uint32_t outsize)
{
    const char *dot;
    size_t base_len;

    /* Find last dot */
    dot = strrchr(input, '.');
    if (dot) {
        base_len = (size_t)(dot - input);
    } else {
        base_len = strlen(input);
    }

    if (base_len + 5 >= outsize) {
        /* Truncate if needed */
        base_len = outsize - 6;
    }
    memcpy(output, input, base_len);
    memcpy(output + base_len, ".fasl", 5);
    output[base_len + 5] = '\0';
}

/* --- compile-file ---
 *
 * (compile-file input-file &key output-file verbose)
 * Reads source, compiles+evals each top-level form, serializes bytecode to FASL.
 * Returns (values output-truename nil nil) per CL spec.
 */

__attribute__((no_stack_protector))
static CL_Obj bi_compile_file(CL_Obj *args, int n)
{
    char in_path[1024], out_path[1024];
    char *src_buf;
    unsigned long src_size;
    CL_Obj stream, expr, bytecode;
    const char *prev_file;
    uint16_t prev_file_id;
    int prev_line;
    CL_Obj saved_package = cl_current_package;
    CL_Symbol *cfp_sym, *cft_sym;
    CL_Obj saved_cfp, saved_cft;
    CL_Obj output_pathname;
    int verbose = 0;
    int i;

    /* Dynamic serialization buffer */
    uint8_t *fasl_buf = NULL;
    uint32_t fasl_capacity = 64 * 1024;  /* Start with 64KB */
    CL_FaslWriter *w = NULL;   /* heap-allocated — too large for stack (~4KB) */

    /* Temp buffer for each unit */
    uint8_t *unit_buf = NULL;
    uint32_t unit_capacity = 32 * 1024;  /* 32KB per unit */
    CL_FaslWriter *uw = NULL;  /* heap-allocated — too large for stack (~4KB) */

    /* Collected bytecodes — we need to serialize after eval */
    /* We use a two-pass approach: first serialize each unit to unit_buf,
       write length + data to fasl_buf. We don't know n_units upfront,
       so we reserve the header and patch it at the end. */
    uint32_t n_units = 0;

    extern const char *cl_coerce_to_namestring(CL_Obj arg, char *buf, uint32_t bufsz);
    extern CL_Obj cl_parse_namestring(const char *str, uint32_t len);

    /* Sync *package* */
    {
        CL_Obj pkg_val2 = cl_symbol_value(SYM_STAR_PACKAGE);
        if (!CL_NULL_P(pkg_val2))
            cl_current_package = pkg_val2;
    }

    /* Resolve input path */
    if (CL_PATHNAME_P(args[0])) {
        cl_coerce_to_namestring(args[0], in_path, sizeof(in_path));
    } else if (CL_STRING_P(args[0])) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
        const char *expanded = platform_expand_home(s->data, in_path, (int)sizeof(in_path));
        if (expanded != in_path) {
            /* No expansion — copy raw string */
            if (s->length < sizeof(in_path)) {
                memcpy(in_path, s->data, s->length);
                in_path[s->length] = '\0';
            } else {
                cl_error(CL_ERR_GENERAL, "COMPILE-FILE: path too long");
                return CL_NIL;
            }
        }
        /* else: expanded path is already in in_path */
    } else {
        cl_error(CL_ERR_TYPE, "COMPILE-FILE: argument must be a string or pathname");
        return CL_NIL;
    }

    /* Default output: cache path, fallback to next-to-source */
    if (!make_fasl_cache_path(in_path, out_path, sizeof(out_path)))
        make_fasl_path(in_path, out_path, sizeof(out_path));
    /* Parse keyword args: :output-file, :verbose */
    for (i = 1; i + 1 < n; i += 2) {
        if (args[i] == cl_intern_keyword("OUTPUT-FILE", 11)) {
            if (CL_PATHNAME_P(args[i + 1])) {
                cl_coerce_to_namestring(args[i + 1], out_path, sizeof(out_path));
            } else if (CL_STRING_P(args[i + 1])) {
                CL_String *s = (CL_String *)CL_OBJ_TO_PTR(args[i + 1]);
                if (s->length < sizeof(out_path)) {
                    memcpy(out_path, s->data, s->length);
                    out_path[s->length] = '\0';
                }
            }
        } else if (args[i] == cl_intern_keyword("VERBOSE", 7)) {
            verbose = !CL_NULL_P(args[i + 1]);
        }
        /* Ignore unknown keywords (allow-other-keys behavior) */
    }

    /* Check *compile-verbose* if :verbose not explicitly given */
    if (!verbose) {
        if (!CL_NULL_P(cl_symbol_value(SYM_STAR_COMPILE_VERBOSE)))
            verbose = 1;
    }

    /* Read source file */
    src_buf = platform_file_read(in_path, &src_size);
    if (!src_buf) {
        cl_error(CL_ERR_GENERAL, "COMPILE-FILE: cannot open file");
        return CL_NIL;
    }

    if (verbose) {
        platform_write_string("; Compiling ");
        platform_write_string(in_path);
        platform_write_string("\n");
    }

    /* Bind *compile-file-pathname* and *compile-file-truename* */
    {
        CL_Obj cfp_path = cl_parse_namestring(in_path, (uint32_t)strlen(in_path));
        CL_GC_PROTECT(cfp_path);
        CL_Obj cft_path;
        char resolved[512];
        if (platform_realpath(in_path, resolved, (int)sizeof(resolved)))
            cft_path = cl_parse_namestring(resolved, (uint32_t)strlen(resolved));
        else
            cft_path = cfp_path;
        CL_GC_PROTECT(cft_path);

        cfp_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_COMPILE_FILE_PATHNAME);
        cft_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_COMPILE_FILE_TRUENAME);
        saved_cfp = cl_symbol_value(SYM_STAR_COMPILE_FILE_PATHNAME);
        saved_cft = cl_symbol_value(SYM_STAR_COMPILE_FILE_TRUENAME);
        cl_set_symbol_value(SYM_STAR_COMPILE_FILE_PATHNAME, cfp_path);
        cl_set_symbol_value(SYM_STAR_COMPILE_FILE_TRUENAME, cft_path);
        CL_GC_UNPROTECT(2);
    }

    /* Save source file context */
    prev_file = cl_current_source_file;
    prev_file_id = cl_current_file_id;
    prev_line = cl_reader_get_line();
    cl_current_source_file = in_path;
    cl_current_file_id++;
    cl_reader_reset_line();

    /* Allocate FASL and unit buffers + FASL writers (heap-allocated to
     * keep bi_compile_file's stack frame small — CL_FaslWriter is ~4KB
     * due to gensym_objs[1024], and two on the stack would interact
     * badly with setjmp/longjmp error recovery) */
    fasl_buf = (uint8_t *)platform_alloc(fasl_capacity);
    unit_buf = (uint8_t *)platform_alloc(unit_capacity);
    w = (CL_FaslWriter *)platform_alloc(sizeof(CL_FaslWriter));
    uw = (CL_FaslWriter *)platform_alloc(sizeof(CL_FaslWriter));
    if (!fasl_buf || !unit_buf || !w || !uw) {
        if (fasl_buf) platform_free(fasl_buf);
        if (unit_buf) platform_free(unit_buf);
        if (w) platform_free(w);
        if (uw) platform_free(uw);
        platform_free(src_buf);
        cl_error(CL_ERR_GENERAL, "COMPILE-FILE: out of memory");
        return CL_NIL;
    }

    /* Write header placeholder (n_units=0, patched later) */
    cl_fasl_writer_init(w, fasl_buf, fasl_capacity);
    cl_fasl_write_header(w, 0);

    /* Create source stream */
    stream = cl_make_cbuf_input_stream(src_buf, (uint32_t)src_size);
    CL_GC_PROTECT(stream);

    /* Read-compile-eval-serialize loop */
#ifdef DEBUG_COMPILER
    int cf_form_count = 0;
#endif
    for (;;) {
        int err;
        int saved_fp, saved_sp, saved_nlx;
        int saved_handler_top, saved_restart_top;

        /* Save VM state so we can restore on error (cl_error longjmps
           past cl_vm_eval's OP_HALT, leaving fp/sp/nlx leaked) */
        saved_fp = cl_vm.fp;
        saved_sp = cl_vm.sp;
        saved_nlx = cl_nlx_top;
        /* Hide outer Lisp handlers so compile-file errors don't escape
         * to handler-case frames installed by the caller (e.g. ASDF).
         * Use floor instead of zeroing top — GC must still mark the
         * full handler/restart stacks to avoid collecting referenced objects. */
        saved_handler_top = cl_handler_floor;
        saved_restart_top = cl_restart_floor;
        cl_handler_floor = cl_handler_top;
        cl_restart_floor = cl_restart_top;
        {
        int saved_gc_roots_cf = gc_root_count;

        err = CL_CATCH();
        if (err == CL_ERR_NONE) {
            expr = cl_read_from_stream(stream);
            if (cl_reader_eof()) {
                CL_UNCATCH();
                cl_handler_floor = saved_handler_top;
                cl_restart_floor = saved_restart_top;
                break;
            }

#ifdef DEBUG_COMPILER
            cf_form_count++;
            fprintf(stderr, "[compile-file %s] form %d read\n",
                    in_path, cf_form_count);
            fflush(stderr);
#endif
            CL_GC_PROTECT(expr);
            bytecode = cl_compile(expr);
            CL_GC_UNPROTECT(1);
            if (!CL_NULL_P(bytecode)) {
#ifdef DEBUG_COMPILER
                fprintf(stderr, "[compile-file %s] form %d compiled, evaluating\n",
                        in_path, cf_form_count);
                fflush(stderr);
#endif
                /* GC-protect bytecode: it must survive eval for FASL serialization */
                CL_GC_PROTECT(bytecode);

                /* Eval the form (macros, defvar, etc. must take effect) */
                cl_vm_eval(bytecode);

                /* Safe point: run pending compaction between top-level forms */
                cl_gc_compact_if_pending();

                CL_GC_UNPROTECT(1); /* bytecode */

                /* Serialize this unit */
                cl_fasl_writer_init(uw, unit_buf, unit_capacity);
                memcpy(uw->gensym_objs, w->gensym_objs, w->gensym_count * sizeof(CL_Obj));
                uw->gensym_count = w->gensym_count;
                cl_fasl_serialize_bytecode(uw, bytecode);

                /* If unit_buf was too small, grow and retry */
                while (uw->error == FASL_ERR_OVERFLOW) {
                    platform_free(unit_buf);
                    unit_capacity *= 2;
                    unit_buf = (uint8_t *)platform_alloc(unit_capacity);
                    if (!unit_buf) break;
                    cl_fasl_writer_init(uw, unit_buf, unit_capacity);
                    memcpy(uw->gensym_objs, w->gensym_objs, w->gensym_count * sizeof(CL_Obj));
                    uw->gensym_count = w->gensym_count;
                    cl_fasl_serialize_bytecode(uw, bytecode);
                }

                if (unit_buf && uw->error == FASL_OK) {
                    /* Update file-level gensym table */
                    memcpy(w->gensym_objs, uw->gensym_objs, uw->gensym_count * sizeof(CL_Obj));
                    w->gensym_count = uw->gensym_count;
                    /* Grow fasl_buf if needed */
                    while (w->pos + 4 + uw->pos > fasl_capacity) {
                        uint8_t *new_buf;
                        fasl_capacity *= 2;
                        new_buf = (uint8_t *)platform_alloc(fasl_capacity);
                        if (!new_buf) break;
                        memcpy(new_buf, fasl_buf, w->pos);
                        platform_free(fasl_buf);
                        fasl_buf = new_buf;
                        w->data = fasl_buf;
                        w->capacity = fasl_capacity;
                    }

                    cl_fasl_write_u32(w, uw->pos);
                    cl_fasl_write_bytes(w, unit_buf, uw->pos);
                    n_units++;
                }
            }
            CL_UNCATCH();
        } else {
            /* Restore VM state leaked by aborted cl_vm_eval */
            cl_vm.fp = saved_fp;
            cl_vm.sp = saved_sp;
            cl_nlx_top = saved_nlx;
            gc_root_count = saved_gc_roots_cf;  /* Restore leaked GC roots */
            cl_error_print();
            CL_UNCATCH();
        }
        }
        /* Restore outer Lisp handler/restart floor */
        cl_handler_floor = saved_handler_top;
        cl_restart_floor = saved_restart_top;
    }

    CL_GC_UNPROTECT(1); /* stream */
    cl_stream_close(stream);
    platform_free(src_buf);

#ifdef DEBUG_COMPILER
    fprintf(stderr, "[compile-file %s] all forms done, n_units=%u, writing FASL\n",
            in_path, n_units);
    fflush(stderr);
#endif

    /* Patch n_units in the header (bytes 8-11, big-endian) */
    fasl_buf[8]  = (uint8_t)(n_units >> 24);
    fasl_buf[9]  = (uint8_t)(n_units >> 16);
    fasl_buf[10] = (uint8_t)(n_units >> 8);
    fasl_buf[11] = (uint8_t)(n_units);

    /* Ensure output directory exists */
    {
        char dir[1024];
        path_directory(out_path, dir, sizeof(dir));
        if (dir[0]) mkdir_p(dir);
    }

    /* Write FASL file to disk */
    {
        PlatformFile fh = platform_file_open(out_path, PLATFORM_FILE_WRITE);
        if (fh == PLATFORM_FILE_INVALID) {
            platform_free(fasl_buf);
            platform_free(unit_buf);
            platform_free(w);
            platform_free(uw);
            cl_error(CL_ERR_GENERAL, "COMPILE-FILE: cannot create output file: %s", out_path);
            return CL_NIL;
        }
        platform_file_write_buf(fh, (const char *)fasl_buf, w->pos);
        platform_file_close(fh);
    }

    platform_free(fasl_buf);
    platform_free(unit_buf);
    platform_free(w);
    platform_free(uw);

    /* Restore *compile-file-pathname* and *compile-file-truename* */
    cl_set_symbol_value(SYM_STAR_COMPILE_FILE_PATHNAME, saved_cfp);
    cl_set_symbol_value(SYM_STAR_COMPILE_FILE_TRUENAME, saved_cft);

    /* Restore source file context */
    cl_current_source_file = prev_file;
    cl_current_file_id = prev_file_id;
    cl_reader_set_line(prev_line);

    /* Restore *package* */
    cl_current_package = saved_package;
    {
        CL_Symbol *pkg_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_PACKAGE);
        cl_set_symbol_value(SYM_STAR_PACKAGE, saved_package);
    }

    /* Return (values output-truename nil nil) per CL spec */
#ifdef DEBUG_COMPILER
    fprintf(stderr, "[compile-file %s] FASL written, returning to caller\n", in_path);
    fflush(stderr);
#endif
    output_pathname = cl_parse_namestring(out_path, (uint32_t)strlen(out_path));
    /* Set multiple values: truename, warnings-p, failure-p */
    cl_mv_values[0] = output_pathname;
    cl_mv_values[1] = CL_NIL;  /* warnings-p */
    cl_mv_values[2] = CL_NIL;  /* failure-p */
    cl_mv_count = 3;
    return output_pathname;
}

/* --- compile-file-pathname ---
 *
 * (compile-file-pathname input-file &key output-file)
 * Returns the pathname of the FASL that compile-file would produce.
 */

static CL_Obj bi_compile_file_pathname(CL_Obj *args, int n)
{
    char in_path[1024], out_path[1024];
    int i;
    extern const char *cl_coerce_to_namestring(CL_Obj arg, char *buf, uint32_t bufsz);
    extern CL_Obj cl_parse_namestring(const char *str, uint32_t len);

    /* Resolve input path */
    if (CL_PATHNAME_P(args[0])) {
        cl_coerce_to_namestring(args[0], in_path, sizeof(in_path));
    } else if (CL_STRING_P(args[0])) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
        const char *expanded = platform_expand_home(s->data, in_path, (int)sizeof(in_path));
        if (expanded != in_path) {
            if (s->length < sizeof(in_path)) {
                memcpy(in_path, s->data, s->length);
                in_path[s->length] = '\0';
            } else {
                cl_error(CL_ERR_GENERAL, "COMPILE-FILE-PATHNAME: path too long");
                return CL_NIL;
            }
        }
    } else {
        cl_error(CL_ERR_TYPE, "COMPILE-FILE-PATHNAME: argument must be a string or pathname");
        return CL_NIL;
    }

    /* Default: cache path, fallback to next-to-source */
    if (!make_fasl_cache_path(in_path, out_path, sizeof(out_path)))
        make_fasl_path(in_path, out_path, sizeof(out_path));

    /* Check for :output-file keyword override */
    for (i = 1; i + 1 < n; i += 2) {
        if (args[i] == cl_intern_keyword("OUTPUT-FILE", 11)) {
            if (CL_PATHNAME_P(args[i + 1])) {
                cl_coerce_to_namestring(args[i + 1], out_path, sizeof(out_path));
            } else if (CL_STRING_P(args[i + 1])) {
                CL_String *s = (CL_String *)CL_OBJ_TO_PTR(args[i + 1]);
                if (s->length < sizeof(out_path)) {
                    memcpy(out_path, s->data, s->length);
                    out_path[s->length] = '\0';
                }
            }
            break;
        }
    }

    return cl_parse_namestring(out_path, (uint32_t)strlen(out_path));
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
        stream = cl_symbol_value(SYM_STANDARD_INPUT);
    } else if (args[0] == CL_T) {
        stream = cl_symbol_value(SYM_TERMINAL_IO);
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

/* (read-delimited-list char &optional stream recursive-p) */
static CL_Obj bi_read_delimited_list(CL_Obj *args, int n)
{
    CL_Obj stream;
    int delim_char;
    int ch;
    CL_Obj result = CL_NIL, tail = CL_NIL, obj, cell;

    if (!CL_CHAR_P(args[0]))
        cl_error(CL_ERR_TYPE, "READ-DELIMITED-LIST: first argument must be a character");
    delim_char = CL_CHAR_VAL(args[0]);

    /* Resolve stream */
    if (n < 2 || CL_NULL_P(args[1])) {
        stream = cl_symbol_value(SYM_STANDARD_INPUT);
    } else if (args[1] == CL_T) {
        stream = cl_symbol_value(SYM_TERMINAL_IO);
    } else {
        stream = args[1];
    }

    CL_GC_PROTECT(result);
    CL_GC_PROTECT(tail);

    for (;;) {
        /* Skip whitespace */
        for (;;) {
            ch = cl_stream_read_char(stream);
            if (ch == -1)
                cl_error(CL_ERR_GENERAL, "READ-DELIMITED-LIST: unexpected end of file");
            if (ch == delim_char) {
                CL_GC_UNPROTECT(2);
                return result;
            }
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r')
                break;
        }
        /* Unread the non-whitespace char and read an object */
        cl_stream_unread_char(stream, ch);
        obj = cl_read_from_stream(stream);
        if (cl_reader_eof()) {
            CL_GC_UNPROTECT(2);
            cl_error(CL_ERR_GENERAL, "READ-DELIMITED-LIST: unexpected end of file");
            return CL_NIL;
        }
        /* Append to list */
        cell = cl_cons(obj, CL_NIL);
        if (CL_NULL_P(result)) {
            result = cell;
        } else {
            ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
        }
        tail = cell;
    }
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

/* --- macro-function / (setf macro-function) --- */

static CL_Obj bi_macro_function(CL_Obj *args, int n)
{
    /* (macro-function symbol &optional env) */
    CL_UNUSED(n);
    if (!CL_SYMBOL_P(args[0]))
        cl_error(CL_ERR_TYPE, "MACRO-FUNCTION: not a symbol");
    return cl_get_macro(args[0]);
}

static CL_Obj bi_set_macro_function(CL_Obj *args, int n)
{
    /* (setf (macro-function symbol) function) — args: new-value, symbol */
    CL_UNUSED(n);
    if (!CL_SYMBOL_P(args[1]))
        cl_error(CL_ERR_TYPE, "(SETF MACRO-FUNCTION): not a symbol");
    cl_register_macro(args[1], args[0]);
    return args[0];
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
            /* Check for interposing UWPROT frames (skip stale ones) */
            for (j = cl_nlx_top - 1; j > i; j--) {
                if (cl_nlx_stack[j].type == CL_NLX_UWPROT) {
                    /* Skip stale NLX frames (frame was reused by tail call) */
                    CL_Frame *tf = &cl_vm.frames[cl_nlx_stack[j].vm_fp - 1];
                    if (tf->code != cl_nlx_stack[j].code) continue;
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
            /* Preserve multiple values across NLX */
            cl_nlx_stack[i].mv_count = cl_mv_count;
            { int mi; for (mi = 0; mi < cl_mv_count && mi < CL_MAX_MV; mi++)
                cl_nlx_stack[i].mv_values[mi] = cl_mv_values[mi]; }
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

static volatile uint32_t gensym_counter = 0;

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
    {
        uint32_t cnt = platform_atomic_inc(&gensym_counter) - 1;
        len = snprintf(buf, sizeof(buf), "%s%lu", prefix, (unsigned long)cnt);
    }
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
    case OP_JMP:        info.name = "JMP";        info.arg_type = OP_ARG_I32; break;
    case OP_JNIL:       info.name = "JNIL";       info.arg_type = OP_ARG_I32; break;
    case OP_JTRUE:      info.name = "JTRUE";      info.arg_type = OP_ARG_I32; break;
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
    case OP_CATCH:      info.name = "CATCH";      info.arg_type = OP_ARG_I32; break;
    case OP_UNCATCH:    info.name = "UNCATCH";    break;
    case OP_UWPROT:     info.name = "UWPROT";     info.arg_type = OP_ARG_I32; break;
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
    case OP_MV_RESET:   info.name = "MV_RESET";   break;
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

            /* OP_BLOCK_PUSH: also has i32 offset after u16 const_idx */
            if (op == OP_BLOCK_PUSH) {
                int32_t boff = (int32_t)(((uint32_t)code[ip] << 24) |
                                         ((uint32_t)code[ip + 1] << 16) |
                                         ((uint32_t)code[ip + 2] << 8) |
                                         (uint32_t)code[ip + 3]);
                ip += 4;
                snprintf(line, sizeof(line),
                        "          offset %+d -> %04lu\n",
                        (int)boff, (unsigned long)((int32_t)ip + boff));
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

        case OP_ARG_I32: {
            int32_t val = (int32_t)(((uint32_t)code[ip] << 24) |
                                    ((uint32_t)code[ip + 1] << 16) |
                                    ((uint32_t)code[ip + 2] << 8) |
                                    (uint32_t)code[ip + 3]);
            ip += 4;
            snprintf(line, sizeof(line), "  %04lu: %-12s %+d    ; -> %04lu\n",
                    (unsigned long)start_ip, info.name, (int)val,
                    (unsigned long)((int32_t)ip + val));
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
        else {
            CL_Obj sv = cl_symbol_value(arg);
            if (sv != CL_UNBOUND && !CL_NULL_P(sv))
                arg = sv;
            else
                cl_error(CL_ERR_UNDEFINED, "DISASSEMBLE: no function binding");
        }
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

/* ext:gc — trigger garbage collection + pending compaction */
static CL_Obj bi_ext_gc(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    cl_gc();
    cl_gc_compact_if_pending();
    return CL_NIL;
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
        table = cl_symbol_value(SYM_PRINT_PPRINT_DISPATCH);

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
        cl_set_symbol_value(SYM_PRINT_PPRINT_DISPATCH, table);
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

    cl_set_symbol_value(SYM_PRINT_PPRINT_DISPATCH, table);
    return CL_NIL;
}

/*
 * (pprint-dispatch object &optional table)
 * Returns 2 values: function, found-p
 */
static CL_Obj bi_pprint_dispatch(CL_Obj *args, int n)
{
    CL_Obj object = args[0];
    CL_Obj table = (n >= 2 && !CL_NULL_P(args[1])) ? args[1] : cl_symbol_value(SYM_PRINT_PPRINT_DISPATCH);
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
    CL_Obj table = (n >= 1 && !CL_NULL_P(args[0])) ? args[0] : cl_symbol_value(SYM_PRINT_PPRINT_DISPATCH);
    CL_Obj result = CL_NIL;
    CL_Obj cur;

    /* Rebuild list (reverses order, but that's fine for alists) */
    cur = table;
    CL_GC_PROTECT(result);
    while (!CL_NULL_P(cur)) {
        result = cl_cons(cl_car(cur), result);
        cur = cl_cdr(cur);
    }
    CL_GC_UNPROTECT(1);
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
    CL_Obj list = cl_symbol_value(SYM_STAR_MODULES);
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
    name_str = cl_make_string(name, len);
    CL_GC_PROTECT(name_str);
    cl_set_symbol_value(SYM_STAR_MODULES, cl_cons(name_str, cl_symbol_value(SYM_STAR_MODULES)));
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
        /* Implementation-defined search: try .fasl first, then .lisp */
        char path[256];
        CL_Obj load_args[1];
        CL_Obj path_obj;
        int found = 0;

        /* Try .fasl and .lisp; prefer .fasl only if newer than .lisp */
        {
            char fasl_path[256], lisp_path[256];
            int have_fasl = 0, have_lisp = 0;

            snprintf(fasl_path, sizeof(fasl_path), "lib/%.*s.fasl", (int)len, name);
            snprintf(lisp_path, sizeof(lisp_path), "lib/%.*s.lisp", (int)len, name);
            have_fasl = platform_file_exists(fasl_path);
            have_lisp = platform_file_exists(lisp_path);

#ifdef PLATFORM_AMIGA
            if (!have_fasl && !have_lisp) {
                snprintf(fasl_path, sizeof(fasl_path), "PROGDIR:lib/%.*s.fasl", (int)len, name);
                snprintf(lisp_path, sizeof(lisp_path), "PROGDIR:lib/%.*s.lisp", (int)len, name);
                have_fasl = platform_file_exists(fasl_path);
                have_lisp = platform_file_exists(lisp_path);
            }
#endif

            if (have_fasl && have_lisp) {
                /* Both exist: prefer FASL only if at least as new as source */
                uint32_t fasl_mt = platform_file_mtime(fasl_path);
                uint32_t lisp_mt = platform_file_mtime(lisp_path);
                if (fasl_mt > 0 && fasl_mt >= lisp_mt) {
                    snprintf(path, sizeof(path), "%s", fasl_path);
                    found = 1;
                } else {
                    snprintf(path, sizeof(path), "%s", lisp_path);
                    found = 1;
                }
            } else if (have_fasl) {
                snprintf(path, sizeof(path), "%s", fasl_path);
                found = 1;
            } else if (have_lisp) {
                snprintf(path, sizeof(path), "%s", lisp_path);
                found = 1;
            }
        }

        if (!found)
            cl_error(CL_ERR_GENERAL, "REQUIRE: cannot find module file");

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
    defun("READ-DELIMITED-LIST", bi_read_delimited_list, 1, 3);
    defun("LOAD", bi_load, 1, -1);  /* accepts keyword args: :verbose, :print */
    defun("EVAL", bi_eval, 1, 1);
    defun("MACROEXPAND-1", bi_macroexpand_1, 1, 1);
    defun("MACROEXPAND", bi_macroexpand, 1, 1);
    defun("MACRO-FUNCTION", bi_macro_function, 1, 2);
    cl_register_builtin("%SETF-MACRO-FUNCTION", bi_set_macro_function, 2, 2, cl_package_cl);

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
    defun("COMPILE-FILE", bi_compile_file, 1, -1);
    defun("COMPILE-FILE-PATHNAME", bi_compile_file_pathname, 1, -1);

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
    extfun("GC", bi_ext_gc, 0, 0);
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

    /* Register cached symbols for GC compaction forwarding */
    cl_gc_register_root(&KW_WR_STREAM);
    cl_gc_register_root(&KW_WR_ESCAPE);
    cl_gc_register_root(&KW_WR_READABLY);
    cl_gc_register_root(&KW_WR_BASE);
    cl_gc_register_root(&KW_WR_RADIX);
    cl_gc_register_root(&KW_WR_LEVEL);
    cl_gc_register_root(&KW_WR_LENGTH);
    cl_gc_register_root(&KW_WR_CASE);
    cl_gc_register_root(&KW_WR_GENSYM);
    cl_gc_register_root(&KW_WR_ARRAY);
    cl_gc_register_root(&KW_WR_CIRCLE);
    cl_gc_register_root(&KW_WR_PRETTY);
    cl_gc_register_root(&KW_WR_RIGHT_MARGIN);
    cl_gc_register_root(&KW_WR_PPRINT_DISPATCH);
    cl_gc_register_root(&KW_LINEAR);
    cl_gc_register_root(&KW_FILL);
    cl_gc_register_root(&KW_MISER);
    cl_gc_register_root(&KW_MANDATORY);
    cl_gc_register_root(&KW_BLOCK);
    cl_gc_register_root(&KW_CURRENT);
}
