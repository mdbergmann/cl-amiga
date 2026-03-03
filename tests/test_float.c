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
#include "core/float.h"
#include "core/repl.h"
#include "platform/platform.h"

static void setup(void)
{
    platform_init();
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
        cl_vm.sp = 0;
        cl_vm.fp = 0;
        snprintf(buf, sizeof(buf), "ERROR:%d", err);
        return buf;
    }
}

/* ================================================================
 * Step 9: Float-specific functions
 * ================================================================ */

/* --- float (conversion) --- */

TEST(float_from_integer)
{
    ASSERT_STR_EQ(eval_print("(float 5)"), "5.0");
    ASSERT_STR_EQ(eval_print("(float 0)"), "0.0");
    ASSERT_STR_EQ(eval_print("(float -3)"), "-3.0");
}

TEST(float_from_float)
{
    ASSERT_STR_EQ(eval_print("(float 3.14)"), "3.14");
    ASSERT_STR_EQ(eval_print("(float 1.0d0)"), "1.0d0");
}

TEST(float_with_prototype)
{
    /* Integer → double via double prototype */
    ASSERT_STR_EQ(eval_print("(float 5 1.0d0)"), "5.0d0");
    /* Integer → single via single prototype */
    ASSERT_STR_EQ(eval_print("(float 5 1.0)"), "5.0");
    /* Double → single via prototype */
    ASSERT_STR_EQ(eval_print("(float 1.0d0 1.0)"), "1.0");
    /* Single → double via prototype */
    ASSERT_STR_EQ(eval_print("(float 1.0 1.0d0)"), "1.0d0");
}

/* --- float-digits --- */

TEST(float_digits_single)
{
    ASSERT_STR_EQ(eval_print("(float-digits 1.0)"), "24");
    ASSERT_STR_EQ(eval_print("(float-digits 0.0)"), "24");
    ASSERT_STR_EQ(eval_print("(float-digits -3.14)"), "24");
}

TEST(float_digits_double)
{
    ASSERT_STR_EQ(eval_print("(float-digits 1.0d0)"), "53");
    ASSERT_STR_EQ(eval_print("(float-digits 0.0d0)"), "53");
}

/* --- float-radix --- */

TEST(float_radix)
{
    ASSERT_STR_EQ(eval_print("(float-radix 1.0)"), "2");
    ASSERT_STR_EQ(eval_print("(float-radix 1.0d0)"), "2");
}

/* --- float-sign --- */

TEST(float_sign_one_arg)
{
    ASSERT_STR_EQ(eval_print("(float-sign 3.5)"), "1.0");
    ASSERT_STR_EQ(eval_print("(float-sign -3.5)"), "-1.0");
    ASSERT_STR_EQ(eval_print("(float-sign 0.0)"), "1.0");
}

TEST(float_sign_two_args)
{
    ASSERT_STR_EQ(eval_print("(float-sign -1.0 2.5)"), "-2.5");
    ASSERT_STR_EQ(eval_print("(float-sign 1.0 -2.5)"), "2.5");
    ASSERT_STR_EQ(eval_print("(float-sign -1.0 -2.5)"), "-2.5");
    ASSERT_STR_EQ(eval_print("(float-sign 1.0 2.5)"), "2.5");
}

TEST(float_sign_type_preservation)
{
    /* Type matches float-2 (or float-1 if no float-2) */
    ASSERT_STR_EQ(eval_print("(float-sign -1.0d0 2.0)"), "-2.0");
    ASSERT_STR_EQ(eval_print("(float-sign -1.0 2.0d0)"), "-2.0d0");
}

/* --- decode-float --- */

TEST(decode_float_basic)
{
    /* 1.5 = 0.75 * 2^1, sign=1.0 */
    ASSERT_STR_EQ(eval_print("(multiple-value-list (decode-float 1.5))"),
                  "(0.75 1 1.0)");
}

TEST(decode_float_negative)
{
    /* -1.5: significand is always positive, sign=-1.0 */
    ASSERT_STR_EQ(eval_print("(multiple-value-list (decode-float -1.5))"),
                  "(0.75 1 -1.0)");
}

TEST(decode_float_zero)
{
    ASSERT_STR_EQ(eval_print("(multiple-value-list (decode-float 0.0))"),
                  "(0.0 0 1.0)");
}

TEST(decode_float_power_of_two)
{
    /* 8.0 = 0.5 * 2^4, sign=1.0 */
    ASSERT_STR_EQ(eval_print("(multiple-value-list (decode-float 8.0))"),
                  "(0.5 4 1.0)");
}

TEST(decode_float_double)
{
    ASSERT_STR_EQ(eval_print("(multiple-value-list (decode-float 1.5d0))"),
                  "(0.75d0 1 1.0d0)");
}

TEST(decode_float_reconstruct)
{
    /* Verify: significand * 2^exponent * sign = original */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (s e g) (decode-float 3.14)"
        "  (* (scale-float s e) g))"), "3.14");
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (s e g) (decode-float -42.0)"
        "  (* (scale-float s e) g))"), "-42.0");
}

/* --- integer-decode-float --- */

TEST(integer_decode_float_single)
{
    /* 1.5 single: significand=12582912, exponent=-23, sign=1 */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-list (integer-decode-float 1.5))"),
        "(12582912 -23 1)");
}

TEST(integer_decode_float_double)
{
    /* 1.5 double: significand=6755399441055744, exponent=-52, sign=1 */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-list (integer-decode-float 1.5d0))"),
        "(6755399441055744 -52 1)");
}

TEST(integer_decode_float_negative)
{
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-list (integer-decode-float -1.5))"),
        "(12582912 -23 -1)");
}

TEST(integer_decode_float_zero)
{
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-list (integer-decode-float 0.0))"),
        "(0 0 1)");
}

/* --- scale-float --- */

TEST(scale_float_basic)
{
    ASSERT_STR_EQ(eval_print("(scale-float 1.0 3)"), "8.0");
    ASSERT_STR_EQ(eval_print("(scale-float 0.5 10)"), "512.0");
}

TEST(scale_float_negative_exp)
{
    ASSERT_STR_EQ(eval_print("(scale-float 8.0 -3)"), "1.0");
    ASSERT_STR_EQ(eval_print("(scale-float 1024.0 -10)"), "1.0");
}

TEST(scale_float_double)
{
    ASSERT_STR_EQ(eval_print("(scale-float 1.0d0 3)"), "8.0d0");
}

TEST(scale_float_zero)
{
    ASSERT_STR_EQ(eval_print("(scale-float 0.0 100)"), "0.0");
}

/* --- Error cases --- */

TEST(float_error_not_number)
{
    ASSERT_STR_EQ(eval_print("(float \"hello\")"), "ERROR:2");
}

TEST(float_digits_error_not_float)
{
    ASSERT_STR_EQ(eval_print("(float-digits 5)"), "ERROR:2");
}

TEST(float_radix_error_not_float)
{
    ASSERT_STR_EQ(eval_print("(float-radix 5)"), "ERROR:2");
}

TEST(float_sign_error_not_float)
{
    ASSERT_STR_EQ(eval_print("(float-sign 5)"), "ERROR:2");
}

TEST(decode_float_error_not_float)
{
    ASSERT_STR_EQ(eval_print("(decode-float 5)"), "ERROR:2");
}

TEST(integer_decode_float_error_not_float)
{
    ASSERT_STR_EQ(eval_print("(integer-decode-float 5)"), "ERROR:2");
}

TEST(scale_float_error_not_float)
{
    ASSERT_STR_EQ(eval_print("(scale-float 5 3)"), "ERROR:2");
}

/* ================================================================ */

int main(void)
{
    setup();

    /* float conversion */
    RUN(float_from_integer);
    RUN(float_from_float);
    RUN(float_with_prototype);

    /* float-digits */
    RUN(float_digits_single);
    RUN(float_digits_double);

    /* float-radix */
    RUN(float_radix);

    /* float-sign */
    RUN(float_sign_one_arg);
    RUN(float_sign_two_args);
    RUN(float_sign_type_preservation);

    /* decode-float */
    RUN(decode_float_basic);
    RUN(decode_float_negative);
    RUN(decode_float_zero);
    RUN(decode_float_power_of_two);
    RUN(decode_float_double);
    RUN(decode_float_reconstruct);

    /* integer-decode-float */
    RUN(integer_decode_float_single);
    RUN(integer_decode_float_double);
    RUN(integer_decode_float_negative);
    RUN(integer_decode_float_zero);

    /* scale-float */
    RUN(scale_float_basic);
    RUN(scale_float_negative_exp);
    RUN(scale_float_double);
    RUN(scale_float_zero);

    /* error cases */
    RUN(float_error_not_number);
    RUN(float_digits_error_not_float);
    RUN(float_radix_error_not_float);
    RUN(float_sign_error_not_float);
    RUN(decode_float_error_not_float);
    RUN(integer_decode_float_error_not_float);
    RUN(scale_float_error_not_float);

    teardown();
    REPORT();
}
