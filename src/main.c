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
#include "core/stream.h"
#include "core/debugger.h"
#include "core/repl.h"
#include "core/color.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void print_usage(void)
{
    platform_write_string(
        "Usage: clamiga [options]\n"
        "Options:\n"
        "  --heap <size>    Heap arena size (default: 4M)\n"
        "  --stack <size>   VM value stack size (default: 64K)\n"
        "  --frames <n>     Max call frame depth (default: 256)\n"
        "  --batch          Batch mode (no prompts, read from stdin)\n"
        "  --color          Force color output\n"
        "  --no-color       Disable color output\n"
        "  --help           Show this help message\n"
        "\n"
        "Sizes accept K, M, G suffixes (e.g. 8M, 512K, 1G).\n"
    );
}

/* Parse a size string with optional K/M/G suffix. Returns 0 on error. */
static uint32_t parse_size(const char *str)
{
    uint32_t val = 0;
    const char *p = str;

    if (!str || !*str) return 0;

    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }

    if (p == str) return 0; /* no digits */

    switch (*p) {
    case 'K': case 'k': val *= 1024; p++; break;
    case 'M': case 'm': val *= 1024 * 1024; p++; break;
    case 'G': case 'g': val *= 1024 * 1024 * 1024; p++; break;
    case '\0': break;
    default: return 0; /* bad suffix */
    }

    if (*p != '\0') return 0; /* trailing garbage */

    return val;
}

int main(int argc, char *argv[])
{
    int batch = 0;
    int color_set = 0;
    int i;
    uint32_t heap_size = 0;
    uint32_t stack_entries = 0;
    int frame_count = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--batch") == 0) {
            batch = 1;
        } else if (strcmp(argv[i], "--color") == 0) {
            cl_repl_color = 1;
            color_set = 1;
        } else if (strcmp(argv[i], "--no-color") == 0) {
            cl_repl_color = 0;
            color_set = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage();
            exit(0);
        } else if (strcmp(argv[i], "--heap") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --heap requires a size argument\n");
                print_usage();
                exit(1);
            }
            heap_size = parse_size(argv[++i]);
            if (heap_size == 0) {
                fprintf(stderr, "Error: invalid heap size '%s'\n", argv[i]);
                print_usage();
                exit(1);
            }
        } else if (strcmp(argv[i], "--stack") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --stack requires a size argument\n");
                print_usage();
                exit(1);
            }
            {
                uint32_t stack_bytes = parse_size(argv[++i]);
                if (stack_bytes == 0) {
                    fprintf(stderr, "Error: invalid stack size '%s'\n", argv[i]);
                    print_usage();
                    exit(1);
                }
                stack_entries = stack_bytes / 4; /* each entry is uint32_t */
            }
        } else if (strcmp(argv[i], "--frames") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --frames requires a number\n");
                print_usage();
                exit(1);
            }
            {
                uint32_t n = parse_size(argv[++i]);
                if (n == 0) {
                    fprintf(stderr, "Error: invalid frame count '%s'\n", argv[i]);
                    print_usage();
                    exit(1);
                }
                frame_count = (int)n;
            }
        } else if (strncmp(argv[i], "--", 2) == 0) {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            print_usage();
            exit(1);
        }
    }

    /* Default: color on for interactive, off for batch */
    if (!color_set)
        cl_repl_color = !batch;

    platform_init();

    /* Initialize subsystems in dependency order */
    cl_error_init();
    cl_mem_init(heap_size ? heap_size : CL_DEFAULT_HEAP_SIZE);
    cl_package_init();
    cl_symbol_init();
    cl_reader_init();
    cl_printer_init();
    cl_compiler_init();
    cl_vm_init(stack_entries, frame_count);
    cl_stream_init();
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

    cl_stream_shutdown();
    cl_vm_shutdown();
    cl_mem_shutdown();
    platform_shutdown();

    return 0;
}
