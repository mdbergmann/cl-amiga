#include "test.h"
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
#include "core/repl.h"
#include "platform/platform.h"
#include <stdio.h>
#include <string.h>

#define LOAD_VERBOSE_TMP "/tmp/cl_test_load_verbose.lisp"

static void setup(void)
{
    platform_init();
    cl_thread_init();
    cl_error_init();
    cl_mem_init(CL_DEFAULT_HEAP_SIZE);
    cl_package_init();
    cl_symbol_init();
    cl_reader_init();
    cl_printer_init();
    cl_compiler_init();
    cl_vm_init(0, 0);
    cl_builtins_init();
    cl_stream_init();
    cl_repl_init();
}

static void teardown(void)
{
    cl_stream_shutdown();
    cl_mem_shutdown();
    platform_shutdown();
}

static const char *eval_str(const char *expr)
{
    static char buf[4096];
    int err;

    CL_CATCH(err);
    if (err == CL_ERR_NONE) {
        CL_Obj result = cl_eval_string(expr);
        cl_prin1_to_string(result, buf, sizeof(buf));
        CL_UNCATCH();
        return buf;
    } else {
        CL_UNCATCH();
        snprintf(buf, sizeof(buf), "ERROR:%d", err);
        return buf;
    }
}

/* Write a tiny Lisp file for use by LOAD tests */
static int write_tmp_lisp(void)
{
    FILE *f = fopen(LOAD_VERBOSE_TMP, "w");
    if (!f) return 0;
    fprintf(f, "(defvar *load-verbose-test-ran* t)\n");
    fclose(f);
    return 1;
}

/* --- Tests --- */

TEST(load_verbose_writes_to_standard_output)
{
    /* *load-verbose* output must go to *standard-output* so it can be
     * captured by with-output-to-string (e.g. SLY REPL redirection). */
    ASSERT(write_tmp_lisp());
    const char *out = eval_str(
        "(with-output-to-string (*standard-output*)"
        "  (let ((*load-verbose* t))"
        "    (load \"" LOAD_VERBOSE_TMP "\")))");
    ASSERT(strstr(out, "; Loading") != NULL);
}

TEST(load_verbose_contains_path)
{
    /* The '; Loading' message must include the file path */
    ASSERT(write_tmp_lisp());
    const char *out = eval_str(
        "(with-output-to-string (*standard-output*)"
        "  (let ((*load-verbose* t))"
        "    (load \"" LOAD_VERBOSE_TMP "\")))");
    ASSERT(strstr(out, "cl_test_load_verbose") != NULL);
}

TEST(load_verbose_nil_suppresses)
{
    /* When *load-verbose* is NIL, no '; Loading' line appears */
    ASSERT(write_tmp_lisp());
    const char *out = eval_str(
        "(with-output-to-string (*standard-output*)"
        "  (let ((*load-verbose* nil))"
        "    (load \"" LOAD_VERBOSE_TMP "\")))");
    ASSERT(strstr(out, "; Loading") == NULL);
}

int main(void)
{
    test_init();
    setup();

    RUN(load_verbose_writes_to_standard_output);
    RUN(load_verbose_contains_path);
    RUN(load_verbose_nil_suppresses);

    teardown();
    remove(LOAD_VERBOSE_TMP);
    REPORT();
}
