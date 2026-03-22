#include "repl.h"
#include "reader.h"
#include "printer.h"
#include "compiler.h"
#include "vm.h"
#include "error.h"
#include "debugger.h"
#include "mem.h"
#include "symbol.h"
#include "package.h"
#include "stream.h"
#include "color.h"
#include "../platform/platform.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define REPL_BUF_SIZE 4096

/* Get current package name length */
static int pkg_name_len(void)
{
    if (CL_HEAP_P(cl_current_package)) {
        CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(cl_current_package);
        CL_String *name = (CL_String *)CL_OBJ_TO_PTR(pkg->name);
        return (int)name->length;
    }
    return 8; /* strlen("CL-AMIGA") */
}

/* Print REPL prompt showing current package name */
static void repl_prompt(void)
{
    cl_color_set(CL_COLOR_DIM_CYAN);
    if (CL_HEAP_P(cl_current_package)) {
        CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(cl_current_package);
        CL_String *name = (CL_String *)CL_OBJ_TO_PTR(pkg->name);
        platform_write_string(name->data);
    } else {
        platform_write_string("CL-AMIGA");
    }
    platform_write_string("> ");
    cl_color_reset();
}

/* Print continuation prompt (spaces matching package name width + "> ") */
static void repl_continuation_prompt(void)
{
    int i, len = pkg_name_len();
    cl_color_set(CL_COLOR_DIM_CYAN);
    for (i = 0; i < len; i++)
        platform_write_string(" ");
    platform_write_string("> ");
    cl_color_reset();
}

/* Load a file by path (C string). Used by --load and --script. */
void cl_load_file(const char *path)
{
    unsigned long size;
    char *buf = platform_file_read(path, &size);
    if (!buf) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Cannot open file: %s", path);
        cl_error(CL_ERR_GENERAL, msg);
        return;
    }

    {
        const char *prev_file = cl_current_source_file;
        uint16_t prev_file_id = cl_current_file_id;
        int prev_line = cl_reader_get_line();
        /* Per CL spec, LOAD binds *package* so in-package in loaded file
           doesn't affect the caller */
        CL_Obj saved_package = cl_current_package;
        CL_Obj stream, load_pathname_obj, load_truename_obj;
        CL_Obj saved_load_pathname, saved_load_truename;

        /* Bind *load-pathname* and *load-truename* per CL spec */
        {
            extern CL_Obj cl_parse_namestring(const char *str, uint32_t len);
            load_pathname_obj = cl_parse_namestring(path, (uint32_t)strlen(path));
        }
        load_truename_obj = load_pathname_obj;
        CL_GC_PROTECT(load_pathname_obj);
        CL_GC_PROTECT(load_truename_obj);
        saved_load_pathname = cl_symbol_value(SYM_STAR_LOAD_PATHNAME);
        saved_load_truename = cl_symbol_value(SYM_STAR_LOAD_TRUENAME);
        cl_set_symbol_value(SYM_STAR_LOAD_PATHNAME, load_pathname_obj);
        cl_set_symbol_value(SYM_STAR_LOAD_TRUENAME, load_truename_obj);

        cl_current_source_file = path;
        cl_current_file_id++;
        cl_reader_reset_line();

        /* Use C-buffer stream — file content stays outside GC arena */
        stream = cl_make_cbuf_input_stream(buf, (uint32_t)size);
        CL_GC_PROTECT(stream);

        for (;;) {
            CL_Obj expr, bytecode;
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
        cl_set_symbol_value(SYM_STAR_LOAD_PATHNAME, saved_load_pathname);
        cl_set_symbol_value(SYM_STAR_LOAD_TRUENAME, saved_load_truename);
        CL_GC_UNPROTECT(2); /* load_truename_obj, load_pathname_obj */

        cl_current_source_file = prev_file;
        cl_current_file_id = prev_file_id;
        cl_reader_set_line(prev_line);

        /* Restore *package* — in-package in loaded file must not leak */
        cl_current_package = saved_package;
        cl_set_symbol_value(SYM_STAR_PACKAGE, saved_package);
    }
}

/* Try to load user init file */
static void load_user_init(void)
{
#ifdef PLATFORM_AMIGA
    static const char *paths[] = { "S:clamiga.lisp", NULL };
#else
    static char user_init_path[512];
    static const char *paths[2];
    const char *home = getenv("HOME");
    if (home) {
        snprintf(user_init_path, sizeof(user_init_path),
                 "%s/.clamigarc", home);
        paths[0] = user_init_path;
        paths[1] = NULL;
    } else {
        paths[0] = NULL;
    }
#endif
    {
        int i;
        for (i = 0; paths[i] != NULL; i++) {
            unsigned long size;
            char *buf = platform_file_read(paths[i], &size);
            if (buf) {
                CL_Obj cl_str, stream;
                const char *prev_file = cl_current_source_file;
                uint16_t prev_file_id = cl_current_file_id;
                int prev_line = cl_reader_get_line();
                cl_current_source_file = paths[i];
                cl_current_file_id++;
                cl_reader_reset_line();

                cl_str = cl_make_string(buf, (int)size);
                CL_GC_PROTECT(cl_str);
                stream = cl_make_string_input_stream(cl_str, 0, (uint32_t)size);
                CL_GC_PROTECT(stream);

                platform_free(buf);

                for (;;) {
                    CL_Obj expr, bytecode;
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

                CL_GC_UNPROTECT(2);
                cl_current_source_file = prev_file;
                cl_current_file_id = prev_file_id;
                cl_reader_set_line(prev_line);
                return;  /* Loaded, stop trying */
            }
        }
    }
    /* No user init found — silently continue */
}

/* Compute net parenthesis depth of a string, skipping:
 * - String literals ("..." with \" escape handling)
 * - Line comments (; to end of line)
 * - Character literals (#\( #\) #\Space etc.) */
/* Skip whitespace and comments in paren-depth scanner, return new index */
static int pd_skip_ws(const char *str, int i)
{
    while (str[i] != '\0') {
        if (str[i] == ' ' || str[i] == '\t' || str[i] == '\n' || str[i] == '\r') {
            i++;
        } else if (str[i] == ';') {
            while (str[i] != '\0' && str[i] != '\n') i++;
        } else {
            break;
        }
    }
    return i;
}

/* Skip a single token (symbol/number) in paren-depth scanner, return new index */
static int pd_skip_token(const char *str, int i)
{
    while (str[i] != '\0' && str[i] != ' ' && str[i] != '\t' &&
           str[i] != '\n' && str[i] != '\r' && str[i] != '(' &&
           str[i] != ')' && str[i] != '"' && str[i] != ';')
        i++;
    return i;
}

/* Skip a balanced form in paren-depth scanner (for feature expressions).
 * Returns new index, or -1 if form is incomplete. */
static int pd_skip_form(const char *str, int i)
{
    i = pd_skip_ws(str, i);
    if (str[i] == '\0') return -1;
    if (str[i] == '(') {
        int d = 1;
        i++;
        while (str[i] != '\0' && d > 0) {
            if (str[i] == '(') d++;
            else if (str[i] == ')') d--;
            else if (str[i] == '"') {
                i++;
                while (str[i] != '\0') {
                    if (str[i] == '\\' && str[i + 1] != '\0') { i += 2; continue; }
                    if (str[i] == '"') break;
                    i++;
                }
            } else if (str[i] == ';') {
                while (str[i] != '\0' && str[i] != '\n') i++;
                continue;
            }
            if (str[i] != '\0') i++;
        }
        return (d == 0) ? i : -1;
    }
    if (str[i] == '"') {
        i++;
        while (str[i] != '\0') {
            if (str[i] == '\\' && str[i + 1] != '\0') { i += 2; continue; }
            if (str[i] == '"') { i++; return i; }
            i++;
        }
        return -1;
    }
    /* Token */
    if (str[i] == '#' || str[i] == '\'') return -1;  /* complex — be conservative */
    return pd_skip_token(str, i);
}

int cl_paren_depth(const char *str)
{
    int depth = 0;
    int i = 0;
    int pending_forms = 0;  /* forms still needed by #+/#- */

    while (str[i] != '\0') {
        char c = str[i];

        /* String literal — skip to closing quote */
        if (c == '"') {
            i++;
            while (str[i] != '\0') {
                if (str[i] == '\\' && str[i + 1] != '\0') {
                    i += 2;  /* skip escaped char */
                    continue;
                }
                if (str[i] == '"') {
                    i++;
                    break;
                }
                i++;
            }
            if (pending_forms > 0 && depth == 0) pending_forms--;
            continue;
        }

        /* Line comment — skip to end of line */
        if (c == ';') {
            while (str[i] != '\0' && str[i] != '\n')
                i++;
            continue;
        }

        /* Character literal #\x — skip the character name */
        if (c == '#' && str[i + 1] == '\\') {
            i += 2;  /* skip #\ */
            /* Skip character name (e.g., Space, Newline, or single char) */
            if (str[i] != '\0') {
                /* Check for named character (alphabetic sequence) */
                if ((str[i] >= 'A' && str[i] <= 'Z') ||
                    (str[i] >= 'a' && str[i] <= 'z')) {
                    while (str[i] != '\0' &&
                           ((str[i] >= 'A' && str[i] <= 'Z') ||
                            (str[i] >= 'a' && str[i] <= 'z')))
                        i++;
                } else {
                    i++;  /* single non-alpha char like #\( */
                }
            }
            if (pending_forms > 0 && depth == 0) pending_forms--;
            continue;
        }

        /* #+ / #- feature conditionals: need feature-expr + one more form */
        if (c == '#' && (str[i + 1] == '+' || str[i + 1] == '-')) {
            int j;
            i += 2;  /* skip #+ or #- */
            /* Skip the feature expression */
            j = pd_skip_form(str, i);
            if (j < 0) {
                /* Incomplete feature expr — definitely need more input */
                return 1;
            }
            i = j;
            /* Now we need one more form (the conditioned form) */
            pending_forms++;
            continue;
        }

        if (c == '(') {
            depth++;
        } else if (c == ')') {
            depth--;
            if (pending_forms > 0 && depth == 0) pending_forms--;
        } else if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            /* Start of an atom/token — skip it */
            int start = i;
            i = pd_skip_token(str, i);
            if (i > start && pending_forms > 0 && depth == 0) pending_forms--;
            continue;
        }

        i++;
    }

    if (pending_forms > 0) return 1;  /* still waiting for conditioned form */
    return depth;
}

CL_Obj cl_eval_string(const char *str)
{
    CL_ReadStream stream;
    CL_Obj expr, bytecode, result;

    stream.buf = str;
    stream.pos = 0;
    stream.len = (int)strlen(str);
    stream.line = 1;

    expr = cl_read_from_string(&stream);
    if (CL_NULL_P(expr)) return CL_NIL;

    CL_GC_PROTECT(expr);
    bytecode = cl_compile(expr);
    CL_GC_UNPROTECT(1);

    if (CL_NULL_P(bytecode)) return CL_NIL;

    result = cl_vm_eval(bytecode);
    return result;
}

/* Update REPL history variables after a successful eval.
 * form = the expression that was read, result = the value it produced. */
void cl_repl_update_history(CL_Obj form, CL_Obj result)
{
    /* Shift ***: *** <- **, ** <- *, * <- result */
    cl_set_symbol_value(SYM_STARSTARSTAR, cl_symbol_value(SYM_STARSTAR));
    cl_set_symbol_value(SYM_STARSTAR, cl_symbol_value(SYM_STAR));
    cl_set_symbol_value(SYM_STAR, result);

    /* Shift +++: +++ <- ++, ++ <- +, + <- form */
    cl_set_symbol_value(SYM_PLUSPLUSPLUS, cl_symbol_value(SYM_PLUSPLUS));
    cl_set_symbol_value(SYM_PLUSPLUS, cl_symbol_value(SYM_PLUS));
    cl_set_symbol_value(SYM_PLUS, form);
}

void cl_repl(void)
{
    char accum[REPL_BUF_SIZE];
    char line[1024];
    int accum_len = 0;
    int depth = 0;

    cl_debugger_enabled = 1;
    repl_prompt();

    while (platform_read_line(line, sizeof(line))) {
        int line_len = (int)strlen(line);

        /* When accumulator is empty, apply line-level skip rules */
        if (accum_len == 0) {
            /* Empty line: skip */
            if (line[0] == '\0') {
                repl_prompt();
                continue;
            }
            /* Skip lines starting with -- (CLI args leaked to stdin on AmigaOS) */
            if (line[0] == '-' && line[1] == '-') {
                repl_prompt();
                continue;
            }
        }

        /* Append line to accumulation buffer (with newline separator) */
        if (accum_len > 0 && accum_len < REPL_BUF_SIZE - 1) {
            accum[accum_len++] = '\n';
        }
        if (accum_len + line_len < REPL_BUF_SIZE - 1) {
            memcpy(accum + accum_len, line, line_len);
            accum_len += line_len;
        }
        accum[accum_len] = '\0';

        /* Check paren depth */
        depth = cl_paren_depth(accum);

        if (depth > 0) {
            /* Incomplete expression — show continuation prompt */
            repl_continuation_prompt();
            continue;
        }

        /* Complete expression — evaluate */

        /* Quit command */
        if (strcmp(accum, "(quit)") == 0 || strcmp(accum, "(QUIT)") == 0 ||
            strcmp(accum, "(exit)") == 0 || strcmp(accum, "(EXIT)") == 0) {
            break;
        }

        {
            int err;
            err = CL_CATCH();
            if (err == CL_ERR_NONE) {
                /* Inline read/compile/eval for history variable support */
                CL_ReadStream stream;
                CL_Obj expr, bytecode, result;

                stream.buf = accum;
                stream.pos = 0;
                stream.len = accum_len;
                stream.line = 1;

                expr = cl_read_from_string(&stream);
                if (!CL_NULL_P(expr) && !cl_reader_eof()) {
                    /* Set - to current form */
                    cl_set_symbol_value(SYM_MINUS, expr);

                    CL_GC_PROTECT(expr);
                    bytecode = cl_compile(expr);
                    CL_GC_UNPROTECT(1);

                    if (!CL_NULL_P(bytecode)) {
                        result = cl_vm_eval(bytecode);

                        /* Update history: shift *, **, ***, +, ++, +++ */
                        cl_repl_update_history(expr, result);

                        cl_color_set(CL_COLOR_DIM_GREEN);
                        cl_prin1(result);
                        cl_color_reset();
                        platform_write_string("\n");
                    }
                }
                CL_UNCATCH();
            } else if (err == CL_ERR_EXIT) {
                CL_UNCATCH();
                break;
            } else {
                cl_error_print();
                /* Reset VM state after error (prevent stale frames) */
                cl_vm.sp = 0;
                cl_vm.fp = 0;
                CL_UNCATCH();
            }
        }

        /* Reset accumulation buffer */
        accum_len = 0;
        depth = 0;
        repl_prompt();
    }

    platform_write_string("\nBye.\n");
}

void cl_repl_batch(void)
{
    char accum[REPL_BUF_SIZE];
    char line[1024];
    int accum_len = 0;
    int depth = 0;

    while (platform_read_line(line, sizeof(line))) {
        int line_len = (int)strlen(line);
        int i;

        /* When accumulator is empty, apply line-level skip rules */
        if (accum_len == 0) {
            /* Skip empty lines */
            if (line[0] == '\0') continue;

            /* Skip comment lines and non-Lisp lines */
            i = 0;
            while (line[i] == ' ' || line[i] == '\t') i++;
            if (line[i] == ';') continue;
            /* Skip lines starting with -- (CLI args leaked to stdin on AmigaOS) */
            if (line[i] == '-' && line[i + 1] == '-') continue;
        }

        /* Append line to accumulation buffer (with newline separator) */
        if (accum_len > 0 && accum_len < REPL_BUF_SIZE - 1) {
            accum[accum_len++] = '\n';
        }
        if (accum_len + line_len < REPL_BUF_SIZE - 1) {
            memcpy(accum + accum_len, line, line_len);
            accum_len += line_len;
        }
        accum[accum_len] = '\0';

        /* Check paren depth */
        depth = cl_paren_depth(accum);

        if (depth > 0) {
            /* Incomplete — read another line */
            continue;
        }

        /* Complete expression(s) — read and evaluate all forms in buffer */

        /* Quit command */
        if (strcmp(accum, "(quit)") == 0 || strcmp(accum, "(QUIT)") == 0 ||
            strcmp(accum, "(exit)") == 0 || strcmp(accum, "(EXIT)") == 0) {
            break;
        }

        {
            CL_ReadStream stream;
            int quit = 0;
            stream.buf = accum;
            stream.pos = 0;
            stream.len = accum_len;
            stream.line = 1;

            while (stream.pos < stream.len && !quit) {
                int err;
                err = CL_CATCH();
                if (err == CL_ERR_NONE) {
                    CL_Obj expr, bytecode, result;

                    expr = cl_read_from_string(&stream);
                    if (CL_NULL_P(expr) || cl_reader_eof()) {
                        CL_UNCATCH();
                        break;
                    }

                    CL_GC_PROTECT(expr);
                    bytecode = cl_compile(expr);
                    CL_GC_UNPROTECT(1);

                    if (!CL_NULL_P(bytecode)) {
                        result = cl_vm_eval(bytecode);
                        cl_prin1(result);
                        platform_write_string("\n");
                    }
                    CL_UNCATCH();
                } else if (err == CL_ERR_EXIT) {
                    CL_UNCATCH();
                    quit = 1;
                } else {
                    cl_error_print();
                    cl_vm.sp = 0;
                    cl_vm.fp = 0;
                    CL_UNCATCH();
                    break; /* skip rest of buffer on error */
                }
            }
            if (quit) break;
        }

        /* Reset accumulation buffer */
        accum_len = 0;
        depth = 0;
    }
}

static void init_history_symbol(CL_Obj sym)
{
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->flags |= CL_SYM_SPECIAL;
    s->value = CL_NIL;
}

void cl_repl_init_no_userinit(int no_userinit)
{
    /* Load standard library in CL package so macros/functions are
       accessible from any package that :use's CL */
    CL_Obj saved_pkg = cl_current_package;
    cl_current_package = cl_package_cl;
    {
        CL_Symbol *pkg_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_PACKAGE);
        pkg_sym->value = cl_package_cl;
    }

    cl_eval_string("(defmacro when (test &rest body) (list 'if test (cons 'progn body)))");
    cl_eval_string("(defmacro unless (test &rest body) (list 'if test nil (cons 'progn body)))");

    /* Export all CL symbols defined so far (from symbol_init + builtins_init).
       This must happen BEFORE loading boot.lisp, which runs in the CL package
       and may intern stray symbols (e.g. local variable names in macro bodies)
       that should NOT be exported. */
    cl_package_export_all_cl_symbols();

    /* Suppress *load-verbose* during internal boot/clos loading */
    {
        CL_Symbol *lv = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_LOAD_VERBOSE);
        lv->value = CL_NIL;
    }

    /* Load boot — try pre-compiled FASL first, fall back to source.
       Skip stale FASLs (source newer than FASL) and recompile after
       loading from source so the next startup is fast. */
    {
        static const char *fasl_src_pairs[][2] = {
            { "lib/boot.fasl", "lib/boot.lisp" },
#ifdef PLATFORM_AMIGA
            { "PROGDIR:lib/boot.fasl", "PROGDIR:lib/boot.lisp" },
#endif
        };
        int boot_loaded = 0;
        int bi;
        int npairs = (int)(sizeof(fasl_src_pairs) / sizeof(fasl_src_pairs[0]));

        for (bi = 0; bi < npairs && !boot_loaded; bi++) {
            const char *fasl_path = fasl_src_pairs[bi][0];
            const char *src_path  = fasl_src_pairs[bi][1];
            int fasl_fresh = 0;

            /* Try FASL if it exists and is not stale */
            if (platform_file_exists(fasl_path)) {
                uint32_t fasl_mt = platform_file_mtime(fasl_path);
                uint32_t src_mt  = platform_file_mtime(src_path);
                if (fasl_mt > 0 && fasl_mt >= src_mt) {
                    int err = CL_CATCH();
                    if (err == CL_ERR_NONE) {
                        char cmd[256];
                        snprintf(cmd, sizeof(cmd), "(load \"%s\")", fasl_path);
                        cl_eval_string(cmd);
                        boot_loaded = 1;
                        fasl_fresh = 1;
                    }
                    CL_UNCATCH();
                }
            }

            /* Fall back to source */
            if (!boot_loaded && platform_file_exists(src_path)) {
                int err = CL_CATCH();
                if (err == CL_ERR_NONE) {
                    char cmd[256];
                    snprintf(cmd, sizeof(cmd), "(load \"%s\")", src_path);
                    cl_eval_string(cmd);
                    boot_loaded = 1;
                }
                CL_UNCATCH();
            }

            /* Recompile FASL if we loaded from source */
            if (boot_loaded && !fasl_fresh) {
                int err = CL_CATCH();
                if (err == CL_ERR_NONE) {
                    char cmd[512];
                    snprintf(cmd, sizeof(cmd),
                        "(compile-file \"%s\" :output-file \"%s\")",
                        src_path, fasl_path);
                    cl_eval_string(cmd);
                }
                CL_UNCATCH();
            }
        }
        (void)boot_loaded;
    }

    /* Load CLOS so defclass/defgeneric/defmethod are available */
    cl_eval_string("(require \"clos\")");

    /* Re-enable *load-verbose* for user code */
    {
        CL_Symbol *lv = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_LOAD_VERBOSE);
        lv->value = SYM_T;
    }

    /* Selectively export only NEW CL symbols that have real bindings
       (function, value, macro, type, struct, or CLOS class).
       Stray symbols interned by boot.lisp/clos.lisp macro bodies are skipped. */
    cl_package_export_defined_cl_symbols();
    cl_current_package = saved_pkg;
    {
        CL_Symbol *pkg_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_PACKAGE);
        pkg_sym->value = saved_pkg;
    }

    if (!no_userinit)
        load_user_init();

    /* Look up *, +, - (already interned by builtins as arithmetic functions) */
    SYM_STAR  = cl_intern_in("*", 1, cl_package_cl);
    SYM_PLUS  = cl_intern_in("+", 1, cl_package_cl);
    SYM_MINUS = cl_intern_in("-", 1, cl_package_cl);

    /* Mark all 7 history symbols as special with initial value NIL */
    init_history_symbol(SYM_STAR);
    init_history_symbol(SYM_STARSTAR);
    init_history_symbol(SYM_STARSTARSTAR);
    init_history_symbol(SYM_PLUS);
    init_history_symbol(SYM_PLUSPLUS);
    init_history_symbol(SYM_PLUSPLUSPLUS);
    init_history_symbol(SYM_MINUS);
}

void cl_repl_init(void)
{
    cl_repl_init_no_userinit(0);
}

void cl_repl_init_minimal(void)
{
    /* Minimal init: set up when/unless macros and export CL symbols,
     * but skip boot.lisp, CLOS, and user init.
     * For unit tests that need cl_compile/cl_eval_string. */
    CL_Obj saved_pkg = cl_current_package;
    cl_current_package = cl_package_cl;
    {
        CL_Symbol *pkg_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_PACKAGE);
        pkg_sym->value = cl_package_cl;
    }

    cl_eval_string("(defmacro when (test &rest body) (list 'if test (cons 'progn body)))");
    cl_eval_string("(defmacro unless (test &rest body) (list 'if test nil (cons 'progn body)))");

    cl_package_export_all_cl_symbols();

    cl_current_package = saved_pkg;
    {
        CL_Symbol *pkg_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_PACKAGE);
        pkg_sym->value = saved_pkg;
    }
}
