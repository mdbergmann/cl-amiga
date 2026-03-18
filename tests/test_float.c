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

/* ================================================================
 * Step 10: Rounding functions
 * ================================================================ */

/* --- truncate (float-aware + MV) --- */

TEST(truncate_float_1arg)
{
    ASSERT_STR_EQ(eval_print("(truncate 2.7)"), "2");
    ASSERT_STR_EQ(eval_print("(truncate -2.7)"), "-2");
    ASSERT_STR_EQ(eval_print("(truncate 2.0)"), "2");
}

TEST(truncate_float_2args)
{
    ASSERT_STR_EQ(eval_print("(truncate 10.0 3)"), "3");
    ASSERT_STR_EQ(eval_print("(truncate -10.0 3)"), "-3");
}

TEST(truncate_mv)
{
    ASSERT_STR_EQ(eval_print("(multiple-value-list (truncate 7 2))"), "(3 1)");
    ASSERT_STR_EQ(eval_print("(multiple-value-list (truncate -7 2))"), "(-3 -1)");
    ASSERT_STR_EQ(eval_print("(multiple-value-list (truncate 2.5))"), "(2 0.5)");
    ASSERT_STR_EQ(eval_print("(multiple-value-list (truncate -2.5))"), "(-2 -0.5)");
}

/* --- floor --- */

TEST(floor_integer)
{
    ASSERT_STR_EQ(eval_print("(floor 7 2)"), "3");
    ASSERT_STR_EQ(eval_print("(floor -7 2)"), "-4");
    ASSERT_STR_EQ(eval_print("(floor 7 -2)"), "-4");
    ASSERT_STR_EQ(eval_print("(floor -7 -2)"), "3");
}

TEST(floor_float_1arg)
{
    ASSERT_STR_EQ(eval_print("(floor 2.7)"), "2");
    ASSERT_STR_EQ(eval_print("(floor -2.7)"), "-3");
    ASSERT_STR_EQ(eval_print("(floor 2.0)"), "2");
}

TEST(floor_float_2args)
{
    ASSERT_STR_EQ(eval_print("(floor 10.0 3)"), "3");
    ASSERT_STR_EQ(eval_print("(floor -10.0 3)"), "-4");
}

TEST(floor_mv)
{
    ASSERT_STR_EQ(eval_print("(multiple-value-list (floor 7 2))"), "(3 1)");
    ASSERT_STR_EQ(eval_print("(multiple-value-list (floor -7 2))"), "(-4 1)");
    ASSERT_STR_EQ(eval_print("(multiple-value-list (floor 2.5))"), "(2 0.5)");
    ASSERT_STR_EQ(eval_print("(multiple-value-list (floor -2.5))"), "(-3 0.5)");
}

/* --- ceiling --- */

TEST(ceiling_integer)
{
    ASSERT_STR_EQ(eval_print("(ceiling 7 2)"), "4");
    ASSERT_STR_EQ(eval_print("(ceiling -7 2)"), "-3");
    ASSERT_STR_EQ(eval_print("(ceiling 7 -2)"), "-3");
    ASSERT_STR_EQ(eval_print("(ceiling -7 -2)"), "4");
}

TEST(ceiling_float_1arg)
{
    ASSERT_STR_EQ(eval_print("(ceiling 2.3)"), "3");
    ASSERT_STR_EQ(eval_print("(ceiling -2.3)"), "-2");
    ASSERT_STR_EQ(eval_print("(ceiling 2.0)"), "2");
}

TEST(ceiling_mv)
{
    ASSERT_STR_EQ(eval_print("(multiple-value-list (ceiling 7 2))"), "(4 -1)");
    ASSERT_STR_EQ(eval_print("(multiple-value-list (ceiling -7 2))"), "(-3 -1)");
    ASSERT_STR_EQ(eval_print("(multiple-value-list (ceiling 2.5))"), "(3 -0.5)");
    ASSERT_STR_EQ(eval_print("(multiple-value-list (ceiling -2.5))"), "(-2 -0.5)");
}

/* --- round --- */

TEST(round_integer)
{
    ASSERT_STR_EQ(eval_print("(round 7 2)"), "4");   /* 3.5 → 4 (even) */
    ASSERT_STR_EQ(eval_print("(round 5 2)"), "2");   /* 2.5 → 2 (even) */
    ASSERT_STR_EQ(eval_print("(round 8 3)"), "3");   /* 2.67 → 3 */
    ASSERT_STR_EQ(eval_print("(round 7 3)"), "2");   /* 2.33 → 2 */
}

TEST(round_float_1arg)
{
    ASSERT_STR_EQ(eval_print("(round 2.5)"), "2");   /* banker's: even */
    ASSERT_STR_EQ(eval_print("(round 3.5)"), "4");   /* banker's: even */
    ASSERT_STR_EQ(eval_print("(round 2.3)"), "2");
    ASSERT_STR_EQ(eval_print("(round 2.7)"), "3");
    ASSERT_STR_EQ(eval_print("(round -2.5)"), "-2");
    ASSERT_STR_EQ(eval_print("(round -3.5)"), "-4");
}

TEST(round_mv)
{
    ASSERT_STR_EQ(eval_print("(multiple-value-list (round 2.5))"), "(2 0.5)");
    ASSERT_STR_EQ(eval_print("(multiple-value-list (round 3.5))"), "(4 -0.5)");
}

/* --- f-variants --- */

TEST(ffloor_basic)
{
    ASSERT_STR_EQ(eval_print("(ffloor 2.7)"), "2.0");
    ASSERT_STR_EQ(eval_print("(ffloor -2.7)"), "-3.0");
    ASSERT_STR_EQ(eval_print("(ffloor 7 2)"), "3.0");
}

TEST(fceiling_basic)
{
    ASSERT_STR_EQ(eval_print("(fceiling 2.3)"), "3.0");
    ASSERT_STR_EQ(eval_print("(fceiling -2.3)"), "-2.0");
}

TEST(ftruncate_basic)
{
    ASSERT_STR_EQ(eval_print("(ftruncate 2.7)"), "2.0");
    ASSERT_STR_EQ(eval_print("(ftruncate -2.7)"), "-2.0");
}

TEST(fround_basic)
{
    ASSERT_STR_EQ(eval_print("(fround 2.5)"), "2.0");
    ASSERT_STR_EQ(eval_print("(fround 3.5)"), "4.0");
}

TEST(ffloor_double)
{
    ASSERT_STR_EQ(eval_print("(ffloor 2.7d0)"), "2.0d0");
}

TEST(ffloor_mv)
{
    ASSERT_STR_EQ(eval_print("(multiple-value-list (ffloor 2.5))"), "(2.0 0.5)");
    ASSERT_STR_EQ(eval_print("(multiple-value-list (ffloor 7 2))"), "(3.0 1)");
}

/* --- mod/rem with floats --- */

TEST(mod_float)
{
    ASSERT_STR_EQ(eval_print("(mod 10.0 3)"), "1.0");
    ASSERT_STR_EQ(eval_print("(mod -10.0 3)"), "2.0");
    ASSERT_STR_EQ(eval_print("(mod 10.0 -3)"), "-2.0");
}

TEST(rem_float)
{
    ASSERT_STR_EQ(eval_print("(rem 10.0 3)"), "1.0");
    ASSERT_STR_EQ(eval_print("(rem -10.0 3)"), "-1.0");
    ASSERT_STR_EQ(eval_print("(rem 10.0 -3)"), "1.0");
}

/* ================================================================
 * Step 11: Math functions (sqrt, exp, log, expt)
 * ================================================================ */

/* --- sqrt --- */

TEST(sqrt_perfect_square)
{
    ASSERT_STR_EQ(eval_print("(sqrt 4.0)"), "2.0");
    ASSERT_STR_EQ(eval_print("(sqrt 9.0)"), "3.0");
    ASSERT_STR_EQ(eval_print("(sqrt 0.0)"), "0.0");
    ASSERT_STR_EQ(eval_print("(sqrt 1.0)"), "1.0");
}

TEST(sqrt_non_perfect)
{
    /* sqrt(2) ≈ 1.41421 */
    ASSERT_STR_EQ(eval_print("(< (abs (- (sqrt 2.0) 1.41421)) 0.001)"), "T");
}

TEST(sqrt_integer_arg)
{
    /* Integer args produce single-float result */
    ASSERT_STR_EQ(eval_print("(sqrt 4)"), "2.0");
    ASSERT_STR_EQ(eval_print("(sqrt 0)"), "0.0");
}

TEST(sqrt_double)
{
    ASSERT_STR_EQ(eval_print("(sqrt 4.0d0)"), "2.0d0");
    ASSERT_STR_EQ(eval_print("(sqrt 0.0d0)"), "0.0d0");
}

TEST(sqrt_error_negative)
{
    ASSERT_STR_EQ(eval_print("(sqrt -1.0)"), "ERROR:2");
    ASSERT_STR_EQ(eval_print("(sqrt -4)"), "ERROR:2");
}

/* --- exp --- */

TEST(exp_zero)
{
    ASSERT_STR_EQ(eval_print("(exp 0.0)"), "1.0");
    ASSERT_STR_EQ(eval_print("(exp 0)"), "1.0");
}

TEST(exp_one)
{
    /* e^1 ≈ 2.71828 */
    ASSERT_STR_EQ(eval_print("(< (abs (- (exp 1.0) 2.71828)) 0.001)"), "T");
}

TEST(exp_negative)
{
    /* e^-1 ≈ 0.36788 */
    ASSERT_STR_EQ(eval_print("(< (abs (- (exp -1.0) 0.36788)) 0.001)"), "T");
}

TEST(exp_double)
{
    ASSERT_STR_EQ(eval_print("(exp 0.0d0)"), "1.0d0");
}

/* --- log --- */

TEST(log_one)
{
    ASSERT_STR_EQ(eval_print("(log 1.0)"), "0.0");
}

TEST(log_e)
{
    /* ln(e) ≈ 1.0 */
    ASSERT_STR_EQ(eval_print("(< (abs (- (log 2.71828) 1.0)) 0.001)"), "T");
}

TEST(log_integer_arg)
{
    /* ln(1) = 0.0 */
    ASSERT_STR_EQ(eval_print("(log 1)"), "0.0");
}

TEST(log_with_base)
{
    /* log base 10 of 100 = 2 */
    ASSERT_STR_EQ(eval_print("(< (abs (- (log 100.0 10.0) 2.0)) 0.001)"), "T");
    /* log base 2 of 8 = 3 */
    ASSERT_STR_EQ(eval_print("(< (abs (- (log 8.0 2.0) 3.0)) 0.001)"), "T");
}

TEST(log_double)
{
    ASSERT_STR_EQ(eval_print("(log 1.0d0)"), "0.0d0");
}

TEST(log_error_nonpositive)
{
    ASSERT_STR_EQ(eval_print("(log 0)"), "ERROR:2");
    ASSERT_STR_EQ(eval_print("(log -1.0)"), "ERROR:2");
}

TEST(log_error_bad_base)
{
    ASSERT_STR_EQ(eval_print("(log 10.0 1.0)"), "ERROR:7");  /* base=1 → divzero */
    ASSERT_STR_EQ(eval_print("(log 10.0 0)"), "ERROR:2");    /* base≤0 → type */
}

/* --- expt (float-aware) --- */

TEST(expt_integer_positive)
{
    /* Integer base + non-negative integer exponent → exact integer */
    ASSERT_STR_EQ(eval_print("(expt 2 10)"), "1024");
    ASSERT_STR_EQ(eval_print("(expt 3 0)"), "1");
    ASSERT_STR_EQ(eval_print("(expt -2 3)"), "-8");
    ASSERT_STR_EQ(eval_print("(expt 5 1)"), "5");
}

TEST(expt_integer_negative_exp)
{
    /* CL spec: rational base + integer exponent → exact rational */
    ASSERT_STR_EQ(eval_print("(expt 2 -1)"), "1/2");
    ASSERT_STR_EQ(eval_print("(expt 2 -3)"), "1/8");
}

TEST(expt_float_args)
{
    /* Float args → float result */
    ASSERT_STR_EQ(eval_print("(expt 2.0 3.0)"), "8.0");
    ASSERT_STR_EQ(eval_print("(< (abs (- (expt 2.0 0.5) 1.41421)) 0.001)"), "T");
}

TEST(expt_mixed_args)
{
    /* Integer base + float exponent → float */
    ASSERT_STR_EQ(eval_print("(expt 4 0.5)"), "2.0");
    /* Float base + integer exponent → float */
    ASSERT_STR_EQ(eval_print("(expt 2.0 3)"), "8.0");
}

TEST(expt_double)
{
    ASSERT_STR_EQ(eval_print("(expt 2.0d0 3)"), "8.0d0");
    ASSERT_STR_EQ(eval_print("(expt 2 3.0d0)"), "8.0d0");
}

TEST(expt_zero_base)
{
    ASSERT_STR_EQ(eval_print("(expt 0 0)"), "1");
    ASSERT_STR_EQ(eval_print("(expt 0 5)"), "0");
    ASSERT_STR_EQ(eval_print("(expt 0.0 5.0)"), "0.0");
}

TEST(expt_error_cases)
{
    /* 0^negative → error */
    ASSERT_STR_EQ(eval_print("(expt 0 -1)"), "ERROR:7");
    /* negative base + non-integer exponent → error */
    ASSERT_STR_EQ(eval_print("(expt -2.0 0.5)"), "ERROR:2");
}

/* ================================================================
 * Step 12: Trigonometric functions (sin, cos, tan, asin, acos, atan)
 * ================================================================ */

/* --- sin --- */

TEST(sin_zero)
{
    ASSERT_STR_EQ(eval_print("(sin 0.0)"), "0.0");
    ASSERT_STR_EQ(eval_print("(sin 0)"), "0.0");
}

TEST(sin_known_value)
{
    /* sin(1.0) ≈ 0.84147 */
    ASSERT_STR_EQ(eval_print("(< (abs (- (sin 1.0) 0.84147)) 0.001)"), "T");
}

TEST(sin_double)
{
    ASSERT_STR_EQ(eval_print("(sin 0.0d0)"), "0.0d0");
}

/* --- cos --- */

TEST(cos_zero)
{
    ASSERT_STR_EQ(eval_print("(cos 0.0)"), "1.0");
    ASSERT_STR_EQ(eval_print("(cos 0)"), "1.0");
}

TEST(cos_known_value)
{
    /* cos(1.0) ≈ 0.5403 */
    ASSERT_STR_EQ(eval_print("(< (abs (- (cos 1.0) 0.5403)) 0.001)"), "T");
}

TEST(cos_double)
{
    ASSERT_STR_EQ(eval_print("(cos 0.0d0)"), "1.0d0");
}

/* --- tan --- */

TEST(tan_zero)
{
    ASSERT_STR_EQ(eval_print("(tan 0.0)"), "0.0");
    ASSERT_STR_EQ(eval_print("(tan 0)"), "0.0");
}

TEST(tan_known_value)
{
    /* tan(1.0) ≈ 1.55740 */
    ASSERT_STR_EQ(eval_print("(< (abs (- (tan 1.0) 1.55740)) 0.001)"), "T");
}

TEST(tan_double)
{
    ASSERT_STR_EQ(eval_print("(tan 0.0d0)"), "0.0d0");
}

/* --- asin --- */

TEST(asin_zero)
{
    ASSERT_STR_EQ(eval_print("(asin 0.0)"), "0.0");
    ASSERT_STR_EQ(eval_print("(asin 0)"), "0.0");
}

TEST(asin_one)
{
    /* asin(1.0) ≈ π/2 ≈ 1.5707 */
    ASSERT_STR_EQ(eval_print("(< (abs (- (asin 1.0) 1.5707)) 0.001)"), "T");
}

TEST(asin_double)
{
    ASSERT_STR_EQ(eval_print("(asin 0.0d0)"), "0.0d0");
}

TEST(asin_error_domain)
{
    ASSERT_STR_EQ(eval_print("(asin 2.0)"), "ERROR:2");
    ASSERT_STR_EQ(eval_print("(asin -2.0)"), "ERROR:2");
}

/* --- acos --- */

TEST(acos_one)
{
    ASSERT_STR_EQ(eval_print("(acos 1.0)"), "0.0");
}

TEST(acos_zero)
{
    /* acos(0.0) ≈ π/2 ≈ 1.5707 */
    ASSERT_STR_EQ(eval_print("(< (abs (- (acos 0.0) 1.5707)) 0.001)"), "T");
}

TEST(acos_integer_arg)
{
    ASSERT_STR_EQ(eval_print("(acos 1)"), "0.0");
}

TEST(acos_double)
{
    ASSERT_STR_EQ(eval_print("(acos 1.0d0)"), "0.0d0");
}

TEST(acos_error_domain)
{
    ASSERT_STR_EQ(eval_print("(acos 2.0)"), "ERROR:2");
    ASSERT_STR_EQ(eval_print("(acos -2.0)"), "ERROR:2");
}

/* --- atan --- */

TEST(atan_zero)
{
    ASSERT_STR_EQ(eval_print("(atan 0.0)"), "0.0");
    ASSERT_STR_EQ(eval_print("(atan 0)"), "0.0");
}

TEST(atan_one)
{
    /* atan(1.0) ≈ π/4 ≈ 0.7853 */
    ASSERT_STR_EQ(eval_print("(< (abs (- (atan 1.0) 0.7853)) 0.001)"), "T");
}

TEST(atan_two_args)
{
    /* atan2(1.0, 1.0) ≈ π/4 ≈ 0.7853 */
    ASSERT_STR_EQ(eval_print("(< (abs (- (atan 1.0 1.0) 0.7853)) 0.001)"), "T");
}

TEST(atan_double)
{
    ASSERT_STR_EQ(eval_print("(atan 0.0d0)"), "0.0d0");
}

TEST(atan_integer_arg)
{
    ASSERT_STR_EQ(eval_print("(atan 1)"), eval_print("(atan 1.0)"));
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

    /* Step 10: rounding */
    RUN(truncate_float_1arg);
    RUN(truncate_float_2args);
    RUN(truncate_mv);
    RUN(floor_integer);
    RUN(floor_float_1arg);
    RUN(floor_float_2args);
    RUN(floor_mv);
    RUN(ceiling_integer);
    RUN(ceiling_float_1arg);
    RUN(ceiling_mv);
    RUN(round_integer);
    RUN(round_float_1arg);
    RUN(round_mv);
    RUN(ffloor_basic);
    RUN(fceiling_basic);
    RUN(ftruncate_basic);
    RUN(fround_basic);
    RUN(ffloor_double);
    RUN(ffloor_mv);
    RUN(mod_float);
    RUN(rem_float);

    /* Step 11: math functions */
    RUN(sqrt_perfect_square);
    RUN(sqrt_non_perfect);
    RUN(sqrt_integer_arg);
    RUN(sqrt_double);
    RUN(sqrt_error_negative);
    RUN(exp_zero);
    RUN(exp_one);
    RUN(exp_negative);
    RUN(exp_double);
    RUN(log_one);
    RUN(log_e);
    RUN(log_integer_arg);
    RUN(log_with_base);
    RUN(log_double);
    RUN(log_error_nonpositive);
    RUN(log_error_bad_base);
    RUN(expt_integer_positive);
    RUN(expt_integer_negative_exp);
    RUN(expt_float_args);
    RUN(expt_mixed_args);
    RUN(expt_double);
    RUN(expt_zero_base);
    RUN(expt_error_cases);

    /* Step 12: trigonometric functions */
    RUN(sin_zero);
    RUN(sin_known_value);
    RUN(sin_double);
    RUN(cos_zero);
    RUN(cos_known_value);
    RUN(cos_double);
    RUN(tan_zero);
    RUN(tan_known_value);
    RUN(tan_double);
    RUN(asin_zero);
    RUN(asin_one);
    RUN(asin_double);
    RUN(asin_error_domain);
    RUN(acos_one);
    RUN(acos_zero);
    RUN(acos_integer_arg);
    RUN(acos_double);
    RUN(acos_error_domain);
    RUN(atan_zero);
    RUN(atan_one);
    RUN(atan_two_args);
    RUN(atan_double);
    RUN(atan_integer_arg);

    teardown();
    REPORT();
}
