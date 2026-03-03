#include "platform/platform.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "core/reader.h"
#include "core/printer.h"
#include "core/compiler.h"
#include "core/vm.h"
#include "core/builtins.h"
#include "core/debugger.h"
#include "core/repl.h"
#include "core/color.h"
#include <string.h>

int main(int argc, char *argv[])
{
    int batch = 0;
    int color_set = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--batch") == 0) {
            batch = 1;
        } else if (strcmp(argv[i], "--color") == 0) {
            cl_repl_color = 1;
            color_set = 1;
        } else if (strcmp(argv[i], "--no-color") == 0) {
            cl_repl_color = 0;
            color_set = 1;
        }
    }

    /* Default: color on for interactive, off for batch */
    if (!color_set)
        cl_repl_color = !batch;

    platform_init();

    /* Initialize subsystems in dependency order */
    cl_error_init();
    cl_mem_init(CL_DEFAULT_HEAP_SIZE);
    cl_package_init();
    cl_symbol_init();
    cl_reader_init();
    cl_printer_init();
    cl_compiler_init();
    cl_vm_init();
    cl_builtins_init();
    cl_debugger_init();
    cl_repl_init();

    if (batch) {
        cl_repl_batch();
    } else {
        cl_color_set(CL_COLOR_BOLD_CYAN);
        platform_write_string("CL-Amiga v0.1");
        cl_color_reset();
        platform_write_string("\nType (quit) to exit.\n\n");
        cl_repl();
    }

    cl_mem_shutdown();
    platform_shutdown();

    return 0;
}
