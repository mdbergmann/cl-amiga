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
#include "core/repl.h"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

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
    cl_repl_init();

    platform_write_string("CL-Amiga v0.1\n");
    platform_write_string("Type (quit) to exit.\n\n");

    cl_repl();

    cl_mem_shutdown();
    platform_shutdown();

    return 0;
}
