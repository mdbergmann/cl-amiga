/* Regression tests for the LOOP macro's stepping semantics.
 *
 * Primary bug (surfaced by cl-spark's FIB tests): variables introduced by a
 * single FOR ... AND ... AND ... clause must be stepped in PARALLEL
 * (CLHS 6.1.2.1.4).  The previous expansion stepped them sequentially, so
 *
 *   (loop for f1 = 0 then f2 and f2 = 1 then (+ f1 f2) repeat n
 *         finally (return f1))
 *
 * computed powers of two (0 1 2 4 8 ...) instead of Fibonacci, because the
 * step of f2 saw the freshly-stepped f1.  These tests pin the parallel
 * behaviour while keeping ordinary sequential FOR clauses unchanged.
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
#include "core/repl.h"
#include "platform/platform.h"

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

static const char *eval_print(const char *str)
{
    static char buf[1024];
    int err;

    CL_CATCH(err);
    if (err == CL_ERR_NONE) {
        CL_Obj result = cl_eval_string(str);
        cl_prin1_to_string(result, buf, sizeof(buf));
        CL_UNCATCH();
        return buf;
    } else {
        CL_UNCATCH();
        cl_vm.sp = 0;
        cl_vm.fp = 0;
        snprintf(buf, sizeof(buf), "ERROR:%d", err);
        return buf;
    }
}

/* ================================================================
 * Parallel stepping of AND-connected FOR clauses
 * ================================================================ */

TEST(for_and_then_parallel_fib)
{
    /* The cl-spark FIB definition: must yield Fibonacci, not powers of 2. */
    ASSERT_STR_EQ(eval_print(
        "(loop for i from 0 to 8 collect"
        " (loop for f1 = 0 then f2 and f2 = 1 then (+ f1 f2)"
        "       repeat i finally (return f1)))"),
                  "(0 1 1 2 3 5 8 13 21)");
}

TEST(for_and_swap)
{
    /* Classic parallel-step swap: a and b exchange each iteration. */
    ASSERT_STR_EQ(eval_print(
        "(loop for a = 1 then b and b = 2 then a"
        "      repeat 3 finally (return (list a b)))"),
                  "(2 1)");
}

TEST(for_and_numeric_parallel)
{
    /* Two numeric steppers connected by AND advance from their own old
     * values, not each other's stepped values. */
    ASSERT_STR_EQ(eval_print(
        "(loop for i from 0 and j from 10"
        "      repeat 4 finally (return (list i j)))"),
                  "(4 14)");
}

TEST(for_then_first_iteration_uses_init)
{
    /* FOR var = init THEN step: the first body iteration sees INIT. */
    ASSERT_STR_EQ(eval_print(
        "(loop for x = 7 then (* x 2) repeat 4 collect x)"),
                  "(7 14 28 56)");
}

TEST(sequential_for_still_sequential)
{
    /* Separate FOR clauses (not joined by AND) step sequentially: j sees the
     * already-stepped i. */
    ASSERT_STR_EQ(eval_print(
        "(loop for i from 1 to 3 for j = (* i 10) collect j)"),
                  "(10 20 30)");
}

TEST(for_then_repeat_zero)
{
    /* REPEAT 0 runs the body zero times; FINALLY sees the initial values. */
    ASSERT_STR_EQ(eval_print(
        "(loop for f1 = 0 then f2 and f2 = 1 then (+ f1 f2)"
        "      repeat 0 finally (return f1))"),
                  "0");
}

TEST(sequential_then_chain)
{
    /* Separate (non-AND) FOR ... = ... THEN clauses must step in SOURCE
     * ORDER, each seeing the sibling's value from the correct point.  This
     * is cl-ppcre's create-matcher-aux idiom: curr takes the previous
     * iteration's `next`, and `next` is recomputed from curr.  Regression:
     * the THEN stepper must keep its position relative to the no-THEN
     * clause's preamble assignment (it must run before it here). */
    ASSERT_STR_EQ(eval_print(
        "(loop for e in '(a b c)"
        "      for curr = 'nf then next"
        "      for next = (list e curr)"
        "      finally (return next))"),
                  "(C (B (A NF)))");
}

TEST(then_step_uses_stepped_sibling)
{
    /* Sequential FOR clauses step in source order: i is stepped first, then
     * prev = i reads the freshly-stepped i (so prev tracks the current i,
     * not the previous one).  This pins the THEN stepper's position in the
     * preamble relative to the earlier clause's stepping. */
    ASSERT_STR_EQ(eval_print(
        "(loop for i from 1 to 4"
        "      for prev = 0 then i"
        "      collect (list i prev))"),
                  "((1 0) (2 2) (3 3) (4 4))");
}

TEST(for_and_mixed_then_from_parallel)
{
    /* CLHS 6.1.2.1.4: mixed AND group (= THEN + FROM) must step in parallel.
     * a's step (+ a j) must use the pre-step j, not j already incremented.
     * Correct: (0 1 3);  wrong (sequential): (0 2 5). */
    ASSERT_STR_EQ(eval_print(
        "(loop for a = 0 then (+ a j) and j from 1"
        "      repeat 3 collect a)"),
                  "(0 1 3)");
}

TEST(for_and_mixed_from_then_parallel)
{
    /* Same semantics with the clauses in reverse declaration order. */
    ASSERT_STR_EQ(eval_print(
        "(loop for j from 1 and a = 0 then (+ a j)"
        "      repeat 3 collect a)"),
                  "(0 1 3)");
}

int main(void)
{
    setup();

    RUN(for_and_then_parallel_fib);
    RUN(for_and_swap);
    RUN(for_and_numeric_parallel);
    RUN(for_then_first_iteration_uses_init);
    RUN(sequential_for_still_sequential);
    RUN(for_then_repeat_zero);
    RUN(sequential_then_chain);
    RUN(then_step_uses_stepped_sibling);
    RUN(for_and_mixed_then_from_parallel);
    RUN(for_and_mixed_from_then_parallel);

    teardown();
    REPORT();
}
