/* Tests for EXT:FUNCTION-SOURCE-LOCATION — the native primitive behind the
 * Sly/SLYNK find-definitions (M-.) backend.
 *
 * The location data lives on CL_Bytecode.source_file / .source_line, captured
 * at compile time from cl_current_source_file (set by LOAD).  The primitive
 * resolves a closure/bytecode to that data and returns (FILE LINE), or
 * :NOT-AVAILABLE when there is no code object or no recorded file (e.g. forms
 * compiled at the REPL, where source_file is NULL).
 *
 * To exercise the (file line) path without an actual file we set
 * cl_current_source_file directly — the same per-thread variable LOAD binds —
 * before compiling, mirroring the runtime path. */

#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "core/reader.h"
#include "core/printer.h"
#include "core/compiler.h"
#include "core/thread.h"
#include "core/vm.h"
#include "core/builtins.h"
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
    cl_repl_init();
}

static void teardown(void)
{
    cl_current_source_file = NULL;
    cl_mem_shutdown();
    platform_shutdown();
}

static int truthy(const char *expr)
{
    return cl_eval_string(expr) != CL_NIL;
}

/* Compile a form as if it came from FILE, so the resulting bytecode carries
 * source_file/source_line.  Restores the previous file afterward. */
static void eval_in_file(const char *file, const char *form)
{
    const char *prev = cl_current_source_file;
    cl_current_source_file = cl_intern_source_file(file);
    cl_eval_string(form);
    cl_current_source_file = prev;
}

/* --- :NOT-AVAILABLE paths --- */

TEST(srcloc_repl_function_not_available)
{
    /* Defined at the REPL (no source file) -> no location. */
    cl_eval_string("(defun sl-repl (x) x)");
    ASSERT(truthy("(eq (ext:function-source-location #'sl-repl) :not-available)"));
}

TEST(srcloc_non_function_not_available)
{
    ASSERT(truthy("(eq (ext:function-source-location 42) :not-available)"));
    ASSERT(truthy("(eq (ext:function-source-location \"str\") :not-available)"));
    ASSERT(truthy("(eq (ext:function-source-location '(1 2 3)) :not-available)"));
    ASSERT(truthy("(eq (ext:function-source-location nil) :not-available)"));
}

TEST(srcloc_builtin_not_available)
{
    /* C builtins have no bytecode, hence no recorded source. */
    ASSERT(truthy("(eq (ext:function-source-location #'car) :not-available)"));
    ASSERT(truthy("(eq (ext:function-source-location #'cons) :not-available)"));
}

/* --- (FILE LINE) path --- */

TEST(srcloc_file_function)
{
    eval_in_file("sl-defun.lisp", "(defun sl-filefn (a b) (+ a b))");
    /* Result is (file line): a 2-element list, file a string, line an int. */
    ASSERT(truthy("(consp (ext:function-source-location #'sl-filefn))"));
    ASSERT(truthy("(= (length (ext:function-source-location #'sl-filefn)) 2)"));
    ASSERT(truthy("(stringp (first (ext:function-source-location #'sl-filefn)))"));
    ASSERT(truthy("(integerp (second (ext:function-source-location #'sl-filefn)))"));
    /* The recorded namestring is the one we compiled under. */
    ASSERT(truthy("(string= (first (ext:function-source-location #'sl-filefn))"
                  "         \"sl-defun.lisp\")"));
    /* Line is non-negative. */
    ASSERT(truthy("(>= (second (ext:function-source-location #'sl-filefn)) 0)"));
}

TEST(srcloc_file_lambda_object)
{
    /* A bare closure object (not via a symbol) resolves the same way. */
    eval_in_file("sl-lambda.lisp", "(defparameter *sl-clo* (lambda (x) (* x x)))");
    ASSERT(truthy("(consp (ext:function-source-location *sl-clo*))"));
    ASSERT(truthy("(string= (first (ext:function-source-location *sl-clo*))"
                  "         \"sl-lambda.lisp\")"));
}

TEST(srcloc_macro_function)
{
    /* Macros are functions too; their expander carries the location. */
    eval_in_file("sl-macro.lisp", "(defmacro sl-mac (x) `(list ,x))");
    ASSERT(truthy("(consp (ext:function-source-location (macro-function 'sl-mac)))"));
    ASSERT(truthy("(string= (first (ext:function-source-location"
                  "                  (macro-function 'sl-mac)))"
                  "         \"sl-macro.lisp\")"));
}

int main(void)
{
    test_init();
    setup();

    RUN(srcloc_repl_function_not_available);
    RUN(srcloc_non_function_not_available);
    RUN(srcloc_builtin_not_available);
    RUN(srcloc_file_function);
    RUN(srcloc_file_lambda_object);
    RUN(srcloc_macro_function);

    teardown();
    REPORT();
}
