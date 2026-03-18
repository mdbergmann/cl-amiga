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

/* Helper: eval a string, return printed result */
static const char *eval_print(const char *str)
{
    static char buf[512];
    int err;

    err = CL_CATCH();
    if (err == CL_ERR_NONE) {
        CL_Obj result = cl_eval_string(str);
        cl_prin1_to_string(result, buf, sizeof(buf));
        CL_UNCATCH();
        return buf;
    } else {
        CL_UNCATCH();
        snprintf(buf, sizeof(buf), "ERROR:%d", err);
        cl_vm.sp = 0;
        cl_vm.fp = 0;
        return buf;
    }
}

/* --- random-state-p --- */

TEST(random_state_p_true)
{
    ASSERT_STR_EQ(eval_print("(random-state-p (make-random-state))"), "T");
}

TEST(random_state_p_false)
{
    ASSERT_STR_EQ(eval_print("(random-state-p 42)"), "NIL");
    ASSERT_STR_EQ(eval_print("(random-state-p \"foo\")"), "NIL");
    ASSERT_STR_EQ(eval_print("(random-state-p nil)"), "NIL");
}

TEST(random_state_p_default)
{
    ASSERT_STR_EQ(eval_print("(random-state-p *random-state*)"), "T");
}

/* --- make-random-state --- */

TEST(make_random_state_nil)
{
    /* NIL arg copies *random-state* */
    ASSERT_STR_EQ(eval_print("(random-state-p (make-random-state nil))"), "T");
}

TEST(make_random_state_t)
{
    /* T arg creates fresh state */
    ASSERT_STR_EQ(eval_print("(random-state-p (make-random-state t))"), "T");
}

TEST(make_random_state_copy)
{
    /* Copy a random state */
    ASSERT_STR_EQ(eval_print("(random-state-p (make-random-state (make-random-state t)))"), "T");
}

TEST(make_random_state_no_args)
{
    /* No args = copy *random-state* */
    ASSERT_STR_EQ(eval_print("(random-state-p (make-random-state))"), "T");
}

/* --- random with fixnum --- */

TEST(random_fixnum_basic)
{
    /* (random 100) should produce a fixnum in [0, 100) */
    const char *res = eval_print("(let ((r (random 100))) (and (integerp r) (>= r 0) (< r 100)))");
    ASSERT_STR_EQ(res, "T");
}

TEST(random_one)
{
    /* (random 1) must always return 0 */
    ASSERT_STR_EQ(eval_print("(random 1)"), "0");
}

TEST(random_range_stays_in_bounds)
{
    /* Call random 50 times, verify all in range */
    const char *res = eval_print(
        "(let ((ok t))"
        "  (dotimes (i 50)"
        "    (let ((r (random 10)))"
        "      (when (or (< r 0) (>= r 10)) (setq ok nil))))"
        "  ok)");
    ASSERT_STR_EQ(res, "T");
}

TEST(random_large_fixnum)
{
    /* Large fixnum limit */
    const char *res = eval_print(
        "(let ((r (random 1000000))) (and (integerp r) (>= r 0) (< r 1000000)))");
    ASSERT_STR_EQ(res, "T");
}

/* --- random with float --- */

TEST(random_single_float)
{
    const char *res = eval_print(
        "(let ((r (random 1.0))) (and (floatp r) (>= r 0.0) (< r 1.0)))");
    ASSERT_STR_EQ(res, "T");
}

TEST(random_single_float_limit)
{
    const char *res = eval_print(
        "(let ((r (random 5.0))) (and (floatp r) (>= r 0.0) (< r 5.0)))");
    ASSERT_STR_EQ(res, "T");
}

TEST(random_double_float)
{
    const char *res = eval_print(
        "(let ((r (random 1.0d0))) (and (floatp r) (>= r 0.0d0) (< r 1.0d0)))");
    ASSERT_STR_EQ(res, "T");
}

/* --- determinism: same seed, same sequence --- */

TEST(random_determinism)
{
    const char *res = eval_print(
        "(let* ((s1 (make-random-state t))"
        "       (s2 (make-random-state s1))"
        "       (a1 (random 1000 s1))"
        "       (a2 (random 1000 s1))"
        "       (b1 (random 1000 s2))"
        "       (b2 (random 1000 s2)))"
        "  (and (= a1 b1) (= a2 b2)))");
    ASSERT_STR_EQ(res, "T");
}

/* --- typep / type-of --- */

TEST(random_state_typep)
{
    ASSERT_STR_EQ(eval_print("(typep (make-random-state) 'random-state)"), "T");
}

TEST(random_state_type_of)
{
    ASSERT_STR_EQ(eval_print("(type-of (make-random-state))"), "RANDOM-STATE");
}

/* --- printer --- */

TEST(random_state_print)
{
    ASSERT_STR_EQ(eval_print("(make-random-state)"), "#<RANDOM-STATE>");
}

/* --- error cases --- */

TEST(random_zero_error)
{
    const char *res = eval_print("(random 0)");
    ASSERT(strncmp(res, "ERROR:", 6) == 0);
}

TEST(random_negative_error)
{
    const char *res = eval_print("(random -1)");
    ASSERT(strncmp(res, "ERROR:", 6) == 0);
}

TEST(random_string_error)
{
    const char *res = eval_print("(random \"foo\")");
    ASSERT(strncmp(res, "ERROR:", 6) == 0);
}

/* --- random with explicit state --- */

TEST(random_with_state)
{
    const char *res = eval_print(
        "(let ((s (make-random-state t)))"
        "  (let ((r (random 100 s)))"
        "    (and (integerp r) (>= r 0) (< r 100))))");
    ASSERT_STR_EQ(res, "T");
}

/* --- Run all tests --- */

int main(void)
{
    setup();

    RUN(random_state_p_true);
    RUN(random_state_p_false);
    RUN(random_state_p_default);
    RUN(make_random_state_nil);
    RUN(make_random_state_t);
    RUN(make_random_state_copy);
    RUN(make_random_state_no_args);
    RUN(random_fixnum_basic);
    RUN(random_one);
    RUN(random_range_stays_in_bounds);
    RUN(random_large_fixnum);
    RUN(random_single_float);
    RUN(random_single_float_limit);
    RUN(random_double_float);
    RUN(random_determinism);
    RUN(random_state_typep);
    RUN(random_state_type_of);
    RUN(random_state_print);
    RUN(random_zero_error);
    RUN(random_negative_error);
    RUN(random_string_error);
    RUN(random_with_state);

    teardown();
    REPORT();
}
