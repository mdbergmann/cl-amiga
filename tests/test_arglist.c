/* Tests for EXT:FUNCTION-ARGLIST and the underlying source-lambda-list
 * capture on CL_Bytecode (FASL format v9).
 *
 *   Path B: functions compiled from source return their exact written
 *           lambda-list (verbatim, including &optional/&key default forms).
 *   Path A: C builtins (and any bytecode lacking a captured list) get a
 *           lambda-list reconstructed from the arity descriptor, using
 *           uninterned #:ARGn placeholder names.
 *
 * FASL round-trip of the captured lambda-list is covered cross-session by
 * tests/test_fasl_compat.sh. */

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
    cl_mem_shutdown();
    platform_shutdown();
}

/* Evaluate EXPR; treat a non-NIL result as a passing predicate. */
static int truthy(const char *expr)
{
    return cl_eval_string(expr) != CL_NIL;
}

/* --- Path B: exact source lambda-list --- */

TEST(arglist_lambda_required)
{
    ASSERT(truthy("(equal (ext:function-arglist (lambda (a b) (+ a b))) '(a b))"));
}

TEST(arglist_lambda_empty)
{
    /* No parameters -> empty lambda-list (NIL). */
    ASSERT(truthy("(null (ext:function-arglist (lambda () 1)))"));
}

TEST(arglist_defun_full)
{
    cl_eval_string("(defun al-full (a b &optional (c 10) &rest more &key kw)"
                   "  (list a b c more kw))");
    ASSERT(truthy("(equal (ext:function-arglist #'al-full)"
                  "       '(a b &optional (c 10) &rest more &key kw))"));
}

TEST(arglist_optional_default_preserved)
{
    cl_eval_string("(defun al-opt (x &optional (y 42)) (list x y))");
    /* The &optional default form must survive verbatim. */
    ASSERT(truthy("(equal (third (ext:function-arglist #'al-opt)) '(y 42))"));
}

TEST(arglist_defun_keys)
{
    cl_eval_string("(defun al-keys (&key alpha beta) (list alpha beta))");
    ASSERT(truthy("(equal (ext:function-arglist #'al-keys) '(&key alpha beta))"));
}

/* --- Path A: reconstruction for C builtins --- */

TEST(arglist_builtin_fixed_arity)
{
    /* CAR takes exactly one argument: one placeholder, no markers. */
    ASSERT(truthy("(= (length (ext:function-arglist #'car)) 1)"));
    ASSERT(truthy("(symbolp (car (ext:function-arglist #'car)))"));
    /* Placeholder is uninterned (no home package). */
    ASSERT(truthy("(null (symbol-package (car (ext:function-arglist #'car))))"));
}

TEST(arglist_builtin_variadic)
{
    /* LIST is 0..* -> (&rest #:ARG0). */
    ASSERT(truthy("(eq (car (ext:function-arglist #'list)) '&rest)"));
    ASSERT(truthy("(= (length (ext:function-arglist #'list)) 2)"));
}

/* --- Non-functions --- */

TEST(arglist_not_available)
{
    ASSERT(truthy("(eq (ext:function-arglist 42) :not-available)"));
    ASSERT(truthy("(eq (ext:function-arglist \"str\") :not-available)"));
    ASSERT(truthy("(eq (ext:function-arglist '(1 2 3)) :not-available)"));
}

int main(void)
{
    test_init();
    setup();

    RUN(arglist_lambda_required);
    RUN(arglist_lambda_empty);
    RUN(arglist_defun_full);
    RUN(arglist_optional_default_preserved);
    RUN(arglist_defun_keys);
    RUN(arglist_builtin_fixed_arity);
    RUN(arglist_builtin_variadic);
    RUN(arglist_not_available);

    teardown();
    REPORT();
}
