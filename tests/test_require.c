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
#include <stdlib.h>
#include <sys/stat.h>

/* Regression tests for REQUIRE/PROVIDE (builtins_io.c):
 *   - module-name accepts a character string designator (CLHS glossary)
 *   - the host $CLAMIGA_HOME fallback lets clamiga find its lib/ when it is
 *     launched from a directory whose cwd-relative "lib/" does not contain the
 *     module — so an editor/Sly session can keep its working directory on the
 *     file buffer instead of pinning it to clamiga's source root.
 */

#define HOME_DIR "/tmp/cl_test_require_home"
#define HOME_LIB HOME_DIR "/lib"
#define HOME_MOD HOME_LIB "/cltestrequirehome.lisp"

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

/* --- Character string designator --- */

TEST(provide_accepts_character)
{
    /* A character denotes the singleton string containing it. */
    ASSERT_STR_EQ("T", eval_str("(provide #\\Z)"));
    ASSERT_STR_EQ("\"Z\"",
        eval_str("(find \"Z\" *modules* :test #'string=)"));
}

TEST(require_accepts_character_already_provided)
{
    eval_str("(provide #\\Q)");
    /* Already provided -> NIL, and crucially NOT a TYPE-ERROR on the char. */
    ASSERT_STR_EQ("NIL", eval_str("(require #\\Q)"));
}

TEST(provide_rejects_non_designator)
{
    /* A number is not a string designator. */
    ASSERT_STR_EQ("ERROR:2", eval_str("(provide 42)"));
}

/* --- $CLAMIGA_HOME fallback --- */

static int write_home_module(void)
{
    FILE *f;
    mkdir(HOME_DIR, 0777);
    mkdir(HOME_LIB, 0777);
    f = fopen(HOME_MOD, "w");
    if (!f) return 0;
    fprintf(f, "(defvar *cl-test-require-home-ran* 42)\n");
    fprintf(f, "(provide \"cltestrequirehome\")\n");
    fclose(f);
    return 1;
}

TEST(require_resolves_via_clamiga_home)
{
    /* The module does not exist under cwd-relative lib/, so the only way
       REQUIRE can find it is the $CLAMIGA_HOME/lib/ fallback. */
    ASSERT(write_home_module());
    setenv("CLAMIGA_HOME", HOME_DIR, 1);

    eval_str("(require \"cltestrequirehome\")");
    ASSERT_STR_EQ("42", eval_str("*cl-test-require-home-ran*"));

    unsetenv("CLAMIGA_HOME");
    remove(HOME_MOD);
    rmdir(HOME_LIB);
    rmdir(HOME_DIR);
}

TEST(require_clamiga_home_tolerates_trailing_slash)
{
    ASSERT(write_home_module());
    setenv("CLAMIGA_HOME", HOME_DIR "/", 1);

    /* Fresh symbol so the previous test's binding can't mask a failure. */
    eval_str("(makunbound '*cl-test-require-home-ran*)");
    eval_str("(setf *modules* "
             "  (remove \"cltestrequirehome\" *modules* :test #'string=))");
    eval_str("(require \"cltestrequirehome\")");
    ASSERT_STR_EQ("42", eval_str("*cl-test-require-home-ran*"));

    unsetenv("CLAMIGA_HOME");
    remove(HOME_MOD);
    rmdir(HOME_LIB);
    rmdir(HOME_DIR);
}

int main(void)
{
    test_init();
    setup();

    RUN(provide_accepts_character);
    RUN(require_accepts_character_already_provided);
    RUN(provide_rejects_non_designator);
    RUN(require_resolves_via_clamiga_home);
    RUN(require_clamiga_home_tolerates_trailing_slash);

    teardown();
    REPORT();
}
