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
#include "color.h"
#include "../platform/platform.h"
#include <string.h>

#define REPL_BUF_SIZE 4096

/* Try to load boot.lisp from known locations */
static void load_boot_file(void)
{
    /* Paths to try, in order */
    static const char *paths[] = {
        "lib/boot.lisp",
#ifdef PLATFORM_AMIGA
        "PROGDIR:lib/boot.lisp",
#endif
        NULL
    };
    int i;

    for (i = 0; paths[i] != NULL; i++) {
        unsigned long size;
        char *buf = platform_file_read(paths[i], &size);
        if (buf) {
            CL_ReadStream stream;
            const char *prev_file = cl_current_source_file;
            uint16_t prev_file_id = cl_current_file_id;
            cl_current_source_file = paths[i];
            cl_current_file_id++;

            stream.buf = buf;
            stream.pos = 0;
            stream.len = (int)size;
            stream.line = 1;

            for (;;) {
                CL_Obj expr, bytecode;
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
                    CL_UNCATCH();
                }
            }

            cl_current_source_file = prev_file;
            cl_current_file_id = prev_file_id;
            platform_free(buf);
            return;  /* Loaded successfully, stop trying */
        }
    }
    /* If no boot file found, silently continue */
}

/* Compute net parenthesis depth of a string, skipping:
 * - String literals ("..." with \" escape handling)
 * - Line comments (; to end of line)
 * - Character literals (#\( #\) #\Space etc.) */
int cl_paren_depth(const char *str)
{
    int depth = 0;
    int i = 0;

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
            continue;
        }

        if (c == '(') depth++;
        else if (c == ')') depth--;

        i++;
    }

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
    CL_Symbol *s;

    /* Shift ***: *** <- **, ** <- *, * <- result */
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STARSTARSTAR);
    s->value = ((CL_Symbol *)CL_OBJ_TO_PTR(SYM_STARSTAR))->value;

    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STARSTAR);
    s->value = ((CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR))->value;

    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR);
    s->value = result;

    /* Shift +++: +++ <- ++, ++ <- +, + <- form */
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PLUSPLUSPLUS);
    s->value = ((CL_Symbol *)CL_OBJ_TO_PTR(SYM_PLUSPLUS))->value;

    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PLUSPLUS);
    s->value = ((CL_Symbol *)CL_OBJ_TO_PTR(SYM_PLUS))->value;

    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PLUS);
    s->value = form;
}

void cl_repl(void)
{
    char accum[REPL_BUF_SIZE];
    char line[1024];
    int accum_len = 0;
    int depth = 0;

    cl_debugger_enabled = 1;
    cl_color_set(CL_COLOR_BOLD_CYAN);
    platform_write_string("CL-AMIGA> ");
    cl_color_reset();

    while (platform_read_line(line, sizeof(line))) {
        int line_len = (int)strlen(line);

        /* Empty line with no accumulated input: skip */
        if (line[0] == '\0' && accum_len == 0) {
            cl_color_set(CL_COLOR_BOLD_CYAN);
            platform_write_string("CL-AMIGA> ");
            cl_color_reset();
            continue;
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
            cl_color_set(CL_COLOR_BOLD_CYAN);
            platform_write_string("       > ");
            cl_color_reset();
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
                    CL_Symbol *s;

                    /* Set - to current form */
                    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_MINUS);
                    s->value = expr;

                    CL_GC_PROTECT(expr);
                    bytecode = cl_compile(expr);
                    CL_GC_UNPROTECT(1);

                    if (!CL_NULL_P(bytecode)) {
                        result = cl_vm_eval(bytecode);

                        /* Update history: shift *, **, ***, +, ++, +++ */
                        cl_repl_update_history(expr, result);

                        cl_color_set(CL_COLOR_GREEN);
                        cl_prin1(result);
                        cl_color_reset();
                        platform_write_string("\n");
                    }
                }
                CL_UNCATCH();
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
        cl_color_set(CL_COLOR_BOLD_CYAN);
        platform_write_string("CL-AMIGA> ");
        cl_color_reset();
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
                cl_eval_string(accum);
                CL_UNCATCH();
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
    }
}

static void init_history_symbol(CL_Obj sym)
{
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->flags |= CL_SYM_SPECIAL;
    s->value = CL_NIL;
}

void cl_repl_init(void)
{
    cl_eval_string("(defmacro when (test &rest body) (list 'if test (cons 'progn body)))");
    cl_eval_string("(defmacro unless (test &rest body) (list 'if test nil (cons 'progn body)))");
    load_boot_file();

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
