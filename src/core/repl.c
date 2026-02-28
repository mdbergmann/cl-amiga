#include "repl.h"
#include "reader.h"
#include "printer.h"
#include "compiler.h"
#include "vm.h"
#include "error.h"
#include "mem.h"
#include "../platform/platform.h"
#include <string.h>

CL_Obj cl_eval_string(const char *str)
{
    CL_ReadStream stream;
    CL_Obj expr, bytecode, result;

    stream.buf = str;
    stream.pos = 0;
    stream.len = (int)strlen(str);

    expr = cl_read_from_string(&stream);
    if (CL_NULL_P(expr)) return CL_NIL;

    CL_GC_PROTECT(expr);
    bytecode = cl_compile(expr);
    CL_GC_UNPROTECT(1);

    if (CL_NULL_P(bytecode)) return CL_NIL;

    result = cl_vm_eval(bytecode);
    return result;
}

void cl_repl(void)
{
    char line[1024];

    platform_write_string("CL-AMIGA> ");

    while (platform_read_line(line, sizeof(line))) {
        int err;

        /* Skip empty lines */
        if (line[0] == '\0') {
            platform_write_string("CL-AMIGA> ");
            continue;
        }

        /* Quit command */
        if (strcmp(line, "(quit)") == 0 || strcmp(line, "(QUIT)") == 0 ||
            strcmp(line, "(exit)") == 0 || strcmp(line, "(EXIT)") == 0) {
            break;
        }

        err = CL_CATCH();
        if (err == CL_ERR_NONE) {
            CL_Obj result = cl_eval_string(line);
            cl_prin1(result);
            platform_write_string("\n");
            CL_UNCATCH();
        } else {
            cl_error_print();
            CL_UNCATCH();
        }

        platform_write_string("CL-AMIGA> ");
    }

    platform_write_string("\nBye.\n");
}

void cl_repl_batch(void)
{
    char line[1024];

    while (platform_read_line(line, sizeof(line))) {
        int err;
        int i;

        /* Skip empty lines */
        if (line[0] == '\0') continue;

        /* Skip comment lines and non-Lisp lines */
        i = 0;
        while (line[i] == ' ' || line[i] == '\t') i++;
        if (line[i] == ';') continue;
        /* Skip lines starting with -- (CLI args leaked to stdin on AmigaOS) */
        if (line[i] == '-' && line[i + 1] == '-') continue;

        /* Quit command */
        if (strcmp(line, "(quit)") == 0 || strcmp(line, "(QUIT)") == 0 ||
            strcmp(line, "(exit)") == 0 || strcmp(line, "(EXIT)") == 0) {
            break;
        }

        err = CL_CATCH();
        if (err == CL_ERR_NONE) {
            cl_eval_string(line);
            CL_UNCATCH();
        } else {
            cl_error_print();
            CL_UNCATCH();
        }
    }
}

void cl_repl_init(void)
{
    cl_eval_string("(defmacro when (test &rest body) (list 'if test (cons 'progn body)))");
    cl_eval_string("(defmacro unless (test &rest body) (list 'if test nil (cons 'progn body)))");
}
