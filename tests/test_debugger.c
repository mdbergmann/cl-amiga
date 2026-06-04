/*
 * test_debugger.c — Tests that cl_invoke_debugger routes output through
 * *debug-io* as required by the HyperSpec (debugger interacts on *debug-io*).
 *
 * Amiga note: the interactive debugger is not exercised from the Amiga Lisp
 * test suite (run-tests.lisp) because driving it non-interactively would
 * require piping commands through the emulator, which the harness does not
 * currently support.  The stream-routing behavior is covered here (host only).
 */

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
#include "core/debugger.h"
#include "core/repl.h"
#include "platform/platform.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>

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
    cl_debugger_init();
    cl_repl_init();
}

static void teardown(void)
{
    cl_stream_shutdown();
    cl_mem_shutdown();
    platform_shutdown();
}

static CL_Obj eval(const char *expr)
{
    return cl_eval_string(expr);
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

/*
 * Test: with the debugger enabled but stdin NOT an interactive terminal,
 * cl_invoke_debugger must NOT block reading stdin.  Instead it takes the
 * non-interactive fallback: it reports the condition through *error-output*
 * (a Lisp stream SLY can capture — never raw fd 1) and returns so the error
 * unwinds.  *debug-io* (the interactive channel) must stay untouched, proving
 * the interactive mini-REPL loop was skipped rather than entered.
 *
 * stdin is redirected to /dev/null (a non-tty) to model a SLY launcher /
 * background worker; platform_stdin_is_interactive() returns 0 for it.
 */
TEST(debugger_noninteractive_reports_to_error_output)
{
    CL_Obj cond = CL_NIL;
    int saved_fd;
    const char *err_out;
    const char *dbg_out;

    /* Bind both channels to fresh string-output-streams so we can tell which
     * one (if any) the debugger wrote to. */
    eval("(setq *error-output* (make-string-output-stream))");
    eval("(setq *debug-io* (make-string-output-stream))");

    /* GC-protect the condition across potential allocating calls */
    CL_GC_PROTECT(cond);
    cond = cl_make_condition(SYM_SIMPLE_ERROR, CL_NIL, CL_NIL);

    /* Redirect stdin to /dev/null: a non-interactive (non-tty) handle, so the
     * fallback fires.  (Were the gate broken, platform_read_line would return
     * EOF here rather than hang — keeping the test itself non-blocking.) */
    saved_fd = dup(fileno(stdin));
    freopen("/dev/null", "r", stdin);

    cl_debugger_enabled = 1;
    {
        int err;
        CL_CATCH(err);
        if (err == CL_ERR_NONE) {
            cl_invoke_debugger(cond);
            CL_UNCATCH();
        } else {
            CL_UNCATCH();
        }
    }
    cl_debugger_enabled = 0;

    /* Restore stdin */
    dup2(saved_fd, fileno(stdin));
    close(saved_fd);

    CL_GC_UNPROTECT(1);

    /* The condition header went to *error-output*... */
    err_out = eval_str(
        "(prog1 (get-output-stream-string *error-output*)"
        "       (setq *error-output* (make-synonym-stream '*terminal-io*)))");
    ASSERT(strstr(err_out, "Debugger entered:") != NULL);

    /* ...and *debug-io* stayed empty (interactive loop was never entered). */
    dbg_out = eval_str(
        "(prog1 (get-output-stream-string *debug-io*)"
        "       (setq *debug-io* (make-synonym-stream '*terminal-io*)))");
    ASSERT(strstr(dbg_out, "Debugger entered:") == NULL);
}

/*
 * Test: with cl_debugger_enabled=0, cl_invoke_debugger returns without
 * writing any output (the display functions are never called).
 */
TEST(debugger_disabled_produces_no_output)
{
    CL_Obj cond = CL_NIL;
    const char *out;

    eval("(setq *debug-io* (make-string-output-stream))");

    CL_GC_PROTECT(cond);
    cond = cl_make_condition(SYM_SIMPLE_ERROR, CL_NIL, CL_NIL);

    /* cl_debugger_enabled is already 0 after setup */
    {
        int err;
        CL_CATCH(err);
        if (err == CL_ERR_NONE) {
            cl_invoke_debugger(cond);
            CL_UNCATCH();
        } else {
            CL_UNCATCH();
        }
    }

    CL_GC_UNPROTECT(1);

    out = eval_str(
        "(prog1 (get-output-stream-string *debug-io*)"
        "       (setq *debug-io* (make-synonym-stream '*terminal-io*)))");
    /* With debugger disabled, nothing should have been written */
    ASSERT(strstr(out, "Debugger entered:") == NULL);
    ASSERT(strcmp(out, "\"\"") == 0);
}

int main(void)
{
    test_init();
    setup();

    RUN(debugger_noninteractive_reports_to_error_output);
    RUN(debugger_disabled_produces_no_output);

    teardown();
    REPORT();
}
