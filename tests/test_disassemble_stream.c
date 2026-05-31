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
#include <string.h>

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

/* --- Tests --- */

TEST(disassemble_builtin_to_stdout)
{
    /* (disassemble 'car) captures output and contains expected strings */
    const char *out = eval_str(
        "(with-output-to-string (*standard-output*) (disassemble 'car))");
    ASSERT(strstr(out, "Built-in function:") != NULL);
    ASSERT(strstr(out, "CAR") != NULL);
}

TEST(disassemble_defun_to_stdout)
{
    /* Bytecode disassembly of a user-defined function goes to *standard-output* */
    const char *out = eval_str(
        "(progn"
        "  (defun disasm-stream-test-foo (x) (+ x 1))"
        "  (with-output-to-string (*standard-output*)"
        "    (disassemble 'disasm-stream-test-foo)))");
    ASSERT(strstr(out, "Disassembly of") != NULL);
    ASSERT(strstr(out, "FOO") != NULL);
    /* Must contain at least one opcode mnemonic */
    ASSERT(strstr(out, "OP_") != NULL || strstr(out, "RET") != NULL
           || strstr(out, "ADD") != NULL || strstr(out, "LOAD") != NULL);
}

TEST(disassemble_lambda_to_stdout)
{
    /* Anonymous lambda disassembly goes to *standard-output* */
    const char *out = eval_str(
        "(with-output-to-string (*standard-output*)"
        "  (disassemble (lambda (x) (* x 2))))");
    ASSERT(strstr(out, "Disassembly of") != NULL);
}

TEST(disassemble_returns_nil)
{
    /* DISASSEMBLE always returns NIL */
    const char *result = eval_str("(disassemble 'car)");
    ASSERT(strstr(result, "NIL") != NULL);
}

TEST(disassemble_defun_returns_nil)
{
    const char *result = eval_str(
        "(progn (defun disasm-nil-test (x) x) (disassemble 'disasm-nil-test))");
    ASSERT(strstr(result, "NIL") != NULL);
}

int main(void)
{
    test_init();
    setup();

    RUN(disassemble_builtin_to_stdout);
    RUN(disassemble_defun_to_stdout);
    RUN(disassemble_lambda_to_stdout);
    RUN(disassemble_returns_nil);
    RUN(disassemble_defun_returns_nil);

    teardown();
    REPORT();
}
