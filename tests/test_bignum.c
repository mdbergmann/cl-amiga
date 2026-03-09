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
#include "core/bignum.h"
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
 * Fixnum overflow → bignum promotion
 * ================================================================ */

TEST(add_overflow_positive)
{
    /* MOST-POSITIVE-FIXNUM + 1 = bignum */
    ASSERT_STR_EQ(eval_print("(+ 1073741823 1)"), "1073741824");
}

TEST(add_overflow_negative)
{
    ASSERT_STR_EQ(eval_print("(+ -1073741824 -1)"), "-1073741825");
}

TEST(sub_overflow)
{
    ASSERT_STR_EQ(eval_print("(- -1073741824 1)"), "-1073741825");
}

TEST(mul_overflow)
{
    ASSERT_STR_EQ(eval_print("(* 1000000 1000000)"), "1000000000000");
}

TEST(mul_large)
{
    ASSERT_STR_EQ(eval_print("(* 1000000000 1000000000)"), "1000000000000000000");
}

/* ================================================================
 * Bignum → fixnum demotion
 * ================================================================ */

TEST(demotion_add)
{
    /* (+ MOST-POSITIVE-FIXNUM+1 -1) should demote back to fixnum */
    ASSERT_STR_EQ(eval_print("(- (+ 1073741823 1) 1)"), "1073741823");
}

TEST(demotion_sub)
{
    ASSERT_STR_EQ(eval_print("(- 1073741824 1)"), "1073741823");
}

TEST(demotion_mul)
{
    /* Bignum * 1 = same bignum, but bignum / bignum = might be fixnum */
    ASSERT_STR_EQ(eval_print("(/ (* 100 200) 100)"), "200");
}

/* ================================================================
 * Reader parsing of large integers
 * ================================================================ */

TEST(read_large_positive)
{
    ASSERT_STR_EQ(eval_print("99999999999999999999"), "99999999999999999999");
}

TEST(read_large_negative)
{
    ASSERT_STR_EQ(eval_print("-99999999999999999999"), "-99999999999999999999");
}

TEST(read_just_over_fixnum)
{
    ASSERT_STR_EQ(eval_print("1073741824"), "1073741824");
}

TEST(read_just_under_neg_fixnum)
{
    ASSERT_STR_EQ(eval_print("-1073741825"), "-1073741825");
}

TEST(read_fixnum_boundary)
{
    /* This should still be a fixnum */
    ASSERT_STR_EQ(eval_print("1073741823"), "1073741823");
}

/* ================================================================
 * Large number arithmetic
 * ================================================================ */

TEST(factorial_10)
{
    ASSERT_STR_EQ(eval_print(
        "(defun fact (n) (if (<= n 1) 1 (* n (fact (1- n)))))"), "FACT");
    ASSERT_STR_EQ(eval_print("(fact 10)"), "3628800");
}

TEST(factorial_20)
{
    ASSERT_STR_EQ(eval_print("(fact 20)"), "2432902008176640000");
}

TEST(factorial_30)
{
    ASSERT_STR_EQ(eval_print("(fact 30)"), "265252859812191058636308480000000");
}

TEST(expt_2_32)
{
    ASSERT_STR_EQ(eval_print("(expt 2 32)"), "4294967296");
}

TEST(expt_2_64)
{
    ASSERT_STR_EQ(eval_print("(expt 2 64)"), "18446744073709551616");
}

TEST(expt_2_100)
{
    ASSERT_STR_EQ(eval_print("(expt 2 100)"),
                  "1267650600228229401496703205376");
}

TEST(expt_10_18)
{
    ASSERT_STR_EQ(eval_print("(expt 10 18)"), "1000000000000000000");
}

TEST(expt_neg_base)
{
    ASSERT_STR_EQ(eval_print("(expt -2 31)"), "-2147483648");
}

TEST(expt_zero)
{
    ASSERT_STR_EQ(eval_print("(expt 999 0)"), "1");
}

TEST(expt_one)
{
    ASSERT_STR_EQ(eval_print("(expt 42 1)"), "42");
}

/* ================================================================
 * Comparison operators with bignums
 * ================================================================ */

TEST(compare_bignum_equal)
{
    ASSERT_STR_EQ(eval_print("(= (expt 2 100) (expt 2 100))"), "T");
}

TEST(compare_bignum_lt)
{
    ASSERT_STR_EQ(eval_print("(< (expt 2 100) (expt 2 101))"), "T");
}

TEST(compare_bignum_gt)
{
    ASSERT_STR_EQ(eval_print("(> (expt 2 101) (expt 2 100))"), "T");
}

TEST(compare_bignum_fixnum)
{
    ASSERT_STR_EQ(eval_print("(> (expt 2 100) 42)"), "T");
    ASSERT_STR_EQ(eval_print("(< 42 (expt 2 100))"), "T");
}

TEST(compare_negative_bignums)
{
    ASSERT_STR_EQ(eval_print("(< (- (expt 2 100)) (- (expt 2 50)))"), "T");
}

/* ================================================================
 * Mixed fixnum/bignum arithmetic
 * ================================================================ */

TEST(mixed_add)
{
    ASSERT_STR_EQ(eval_print("(+ (expt 2 100) 1)"),
                  "1267650600228229401496703205377");
}

TEST(mixed_sub)
{
    ASSERT_STR_EQ(eval_print("(- (expt 2 100) 1)"),
                  "1267650600228229401496703205375");
}

TEST(mixed_mul)
{
    ASSERT_STR_EQ(eval_print("(* (expt 2 100) 2)"),
                  "2535301200456458802993406410752");
}

/* ================================================================
 * Division and modulo
 * ================================================================ */

TEST(div_bignum)
{
    ASSERT_STR_EQ(eval_print("(/ (expt 2 100) (expt 2 50))"),
                  "1125899906842624");
}

TEST(div_bignum_by_fixnum)
{
    ASSERT_STR_EQ(eval_print("(/ 1000000000000 1000)"), "1000000000");
}

TEST(mod_bignum)
{
    /* 2^100 mod 7 = 2, so (2^100 + 3) mod 7 = 5 */
    ASSERT_STR_EQ(eval_print("(mod (+ (expt 2 100) 3) 7)"), "5");
}

TEST(mod_positive)
{
    ASSERT_STR_EQ(eval_print("(mod 10 3)"), "1");
}

TEST(mod_negative_dividend)
{
    /* CL mod: result has sign of divisor */
    ASSERT_STR_EQ(eval_print("(mod -10 3)"), "2");
}

TEST(rem_positive)
{
    ASSERT_STR_EQ(eval_print("(rem 10 3)"), "1");
}

TEST(rem_negative_dividend)
{
    /* REM: truncation remainder, sign follows dividend */
    ASSERT_STR_EQ(eval_print("(rem -10 3)"), "-1");
}

/* ================================================================
 * Negation and absolute value
 * ================================================================ */

TEST(negate_bignum)
{
    ASSERT_STR_EQ(eval_print("(- (expt 2 100))"),
                  "-1267650600228229401496703205376");
}

TEST(abs_negative_bignum)
{
    ASSERT_STR_EQ(eval_print("(abs (- (expt 2 100)))"),
                  "1267650600228229401496703205376");
}

TEST(negate_most_negative_fixnum)
{
    /* Negating MOST-NEGATIVE-FIXNUM should produce a bignum */
    ASSERT_STR_EQ(eval_print("(- -1073741824)"), "1073741824");
}

/* ================================================================
 * GCD and LCM
 * ================================================================ */

TEST(gcd_basic)
{
    ASSERT_STR_EQ(eval_print("(gcd 12 8)"), "4");
}

TEST(gcd_bignum)
{
    ASSERT_STR_EQ(eval_print("(gcd (expt 2 100) (expt 2 50))"),
                  "1125899906842624");
}

TEST(gcd_coprime)
{
    ASSERT_STR_EQ(eval_print("(gcd 7 13)"), "1");
}

TEST(gcd_zero)
{
    ASSERT_STR_EQ(eval_print("(gcd 0 42)"), "42");
    ASSERT_STR_EQ(eval_print("(gcd 42 0)"), "42");
}

TEST(gcd_no_args)
{
    ASSERT_STR_EQ(eval_print("(gcd)"), "0");
}

TEST(lcm_basic)
{
    ASSERT_STR_EQ(eval_print("(lcm 4 6)"), "12");
}

TEST(lcm_no_args)
{
    ASSERT_STR_EQ(eval_print("(lcm)"), "1");
}

/* ================================================================
 * Bitwise operations
 * ================================================================ */

TEST(logand_basic)
{
    ASSERT_STR_EQ(eval_print("(logand 15 9)"), "9");
}

TEST(logior_basic)
{
    ASSERT_STR_EQ(eval_print("(logior 15 9)"), "15");
}

TEST(logxor_basic)
{
    ASSERT_STR_EQ(eval_print("(logxor 15 9)"), "6");
}

TEST(lognot_basic)
{
    ASSERT_STR_EQ(eval_print("(lognot 0)"), "-1");
    ASSERT_STR_EQ(eval_print("(lognot -1)"), "0");
    ASSERT_STR_EQ(eval_print("(lognot 7)"), "-8");
}

TEST(ash_left)
{
    ASSERT_STR_EQ(eval_print("(ash 1 10)"), "1024");
}

TEST(ash_right)
{
    ASSERT_STR_EQ(eval_print("(ash 1024 -10)"), "1");
}

TEST(ash_large)
{
    ASSERT_STR_EQ(eval_print("(ash 1 100)"),
                  "1267650600228229401496703205376");
}

TEST(logand_no_args)
{
    ASSERT_STR_EQ(eval_print("(logand)"), "-1");
}

/* Regression: two's complement negation used ~(uint16_t) which promotes to
   int and flips all 32 bits, causing incorrect carry propagation */
TEST(logand_negative_one_identity)
{
    ASSERT_STR_EQ(eval_print("(logand -1 65536)"), "65536");
    ASSERT_STR_EQ(eval_print("(logand -1 610777)"), "610777");
    ASSERT_STR_EQ(eval_print("(logand #xFFFFFFFF 610777)"), "610777");
    ASSERT_STR_EQ(eval_print("(logand -1 #x12345678)"), "305419896");
}

TEST(logior_no_args)
{
    ASSERT_STR_EQ(eval_print("(logior)"), "0");
}

/* ================================================================
 * Integer-length, isqrt
 * ================================================================ */

TEST(integer_length_basic)
{
    ASSERT_STR_EQ(eval_print("(integer-length 0)"), "0");
    ASSERT_STR_EQ(eval_print("(integer-length 1)"), "1");
    ASSERT_STR_EQ(eval_print("(integer-length 7)"), "3");
    ASSERT_STR_EQ(eval_print("(integer-length 255)"), "8");
}

TEST(integer_length_negative)
{
    ASSERT_STR_EQ(eval_print("(integer-length -1)"), "0");
    ASSERT_STR_EQ(eval_print("(integer-length -8)"), "3");
}

TEST(isqrt_basic)
{
    ASSERT_STR_EQ(eval_print("(isqrt 0)"), "0");
    ASSERT_STR_EQ(eval_print("(isqrt 1)"), "1");
    ASSERT_STR_EQ(eval_print("(isqrt 4)"), "2");
    ASSERT_STR_EQ(eval_print("(isqrt 9)"), "3");
    ASSERT_STR_EQ(eval_print("(isqrt 10)"), "3");
}

TEST(isqrt_large)
{
    ASSERT_STR_EQ(eval_print("(isqrt 1000000)"), "1000");
}

/* ================================================================
 * Evenp, oddp
 * ================================================================ */

TEST(evenp_fixnum)
{
    ASSERT_STR_EQ(eval_print("(evenp 0)"), "T");
    ASSERT_STR_EQ(eval_print("(evenp 2)"), "T");
    ASSERT_STR_EQ(eval_print("(evenp 3)"), "NIL");
}

TEST(oddp_fixnum)
{
    ASSERT_STR_EQ(eval_print("(oddp 1)"), "T");
    ASSERT_STR_EQ(eval_print("(oddp 2)"), "NIL");
}

TEST(evenp_bignum)
{
    ASSERT_STR_EQ(eval_print("(evenp (expt 2 100))"), "T");
    ASSERT_STR_EQ(eval_print("(oddp (+ (expt 2 100) 1))"), "T");
}

/* ================================================================
 * eql/equal with bignums
 * ================================================================ */

TEST(eql_bignums)
{
    ASSERT_STR_EQ(eval_print("(eql (expt 2 100) (expt 2 100))"), "T");
    ASSERT_STR_EQ(eval_print("(eql (expt 2 100) (expt 2 99))"), "NIL");
}

TEST(equal_bignums)
{
    ASSERT_STR_EQ(eval_print("(equal (expt 2 100) (expt 2 100))"), "T");
}

/* ================================================================
 * Type predicates
 * ================================================================ */

TEST(typep_fixnum)
{
    ASSERT_STR_EQ(eval_print("(typep 42 'fixnum)"), "T");
    ASSERT_STR_EQ(eval_print("(typep 42 'integer)"), "T");
    ASSERT_STR_EQ(eval_print("(typep 42 'number)"), "T");
    ASSERT_STR_EQ(eval_print("(typep 42 'bignum)"), "NIL");
}

TEST(typep_bignum)
{
    ASSERT_STR_EQ(eval_print("(typep (expt 2 100) 'bignum)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (expt 2 100) 'integer)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (expt 2 100) 'number)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (expt 2 100) 'fixnum)"), "NIL");
}

TEST(type_of_bignum)
{
    ASSERT_STR_EQ(eval_print("(type-of (expt 2 100))"), "BIGNUM");
    ASSERT_STR_EQ(eval_print("(type-of 42)"), "FIXNUM");
}

TEST(numberp_integerp)
{
    ASSERT_STR_EQ(eval_print("(numberp 42)"), "T");
    ASSERT_STR_EQ(eval_print("(numberp (expt 2 100))"), "T");
    ASSERT_STR_EQ(eval_print("(integerp 42)"), "T");
    ASSERT_STR_EQ(eval_print("(integerp (expt 2 100))"), "T");
    ASSERT_STR_EQ(eval_print("(numberp \"hello\")"), "NIL");
}

/* ================================================================
 * MOST-POSITIVE-FIXNUM / MOST-NEGATIVE-FIXNUM
 * ================================================================ */

TEST(fixnum_constants)
{
    ASSERT_STR_EQ(eval_print("most-positive-fixnum"), "1073741823");
    ASSERT_STR_EQ(eval_print("most-negative-fixnum"), "-1073741824");
}

TEST(fixnum_boundary_add)
{
    ASSERT_STR_EQ(eval_print("(+ most-positive-fixnum 1)"), "1073741824");
}

TEST(fixnum_boundary_sub)
{
    ASSERT_STR_EQ(eval_print("(- most-negative-fixnum 1)"), "-1073741825");
}

/* ================================================================
 * Subtypep
 * ================================================================ */

TEST(subtypep_bignum)
{
    ASSERT_STR_EQ(eval_print("(subtypep 'bignum 'integer)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'fixnum 'integer)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'bignum 'number)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'integer 'number)"), "T");
}

/* ================================================================
 * Predicates: zerop, plusp, minusp on bignums
 * ================================================================ */

TEST(predicates_bignum)
{
    ASSERT_STR_EQ(eval_print("(zerop (expt 2 100))"), "NIL");
    ASSERT_STR_EQ(eval_print("(plusp (expt 2 100))"), "T");
    ASSERT_STR_EQ(eval_print("(minusp (expt 2 100))"), "NIL");
    ASSERT_STR_EQ(eval_print("(minusp (- (expt 2 100)))"), "T");
}

/* ================================================================
 * Max/min with bignums
 * ================================================================ */

TEST(max_min_bignum)
{
    ASSERT_STR_EQ(eval_print("(max (expt 2 100) 42)"),
                  "1267650600228229401496703205376");
    ASSERT_STR_EQ(eval_print("(min (expt 2 100) 42)"), "42");
}

/* ================================================================
 * Bignum in VM opcodes
 * ================================================================ */

TEST(vm_add_overflow)
{
    /* Compiled code path (VM opcodes) */
    ASSERT_STR_EQ(eval_print(
        "(let ((x 1073741823)) (+ x 1))"), "1073741824");
}

TEST(vm_sub_overflow)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((x -1073741824)) (- x 1))"), "-1073741825");
}

TEST(vm_mul_overflow)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((x 100000)) (* x x))"), "10000000000");
}

TEST(vm_numeq_bignum)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((x (expt 2 100))) (= x x))"), "T");
}

TEST(vm_lt_bignum)
{
    ASSERT_STR_EQ(eval_print(
        "(< 42 (expt 2 100))"), "T");
}

/* ================================================================
 * Hash table with bignum keys
 * ================================================================ */

TEST(hash_table_bignum_key)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((ht (make-hash-table :test 'eql)))"
        "  (setf (gethash (expt 2 100) ht) 'found)"
        "  (gethash (expt 2 100) ht))"), "FOUND");
}

/* ================================================================
 * Negative exponent
 * ================================================================ */

TEST(expt_negative_exp)
{
    /* CL spec: rational base + integer exponent → exact rational */
    ASSERT_STR_EQ(eval_print("(expt 2 -1)"), "1/2");
    ASSERT_STR_EQ(eval_print("(expt 1 -5)"), "1");
    ASSERT_STR_EQ(eval_print("(expt -1 -3)"), "-1");
    ASSERT_STR_EQ(eval_print("(expt -1 -4)"), "1");
}

/* ================================================================
 * Multi-arg arithmetic
 * ================================================================ */

TEST(multi_arg_add)
{
    ASSERT_STR_EQ(eval_print("(+ 1073741823 1 1)"), "1073741825");
}

TEST(multi_arg_mul)
{
    ASSERT_STR_EQ(eval_print("(* 1000000 1000 1000)"), "1000000000000");
}

/* ================================================================
 * C-level API tests
 * ================================================================ */

TEST(c_bignum_from_int32)
{
    CL_Obj obj = cl_bignum_from_int32(CL_FIXNUM_MAX + 1);
    ASSERT(CL_BIGNUM_P(obj));
    ASSERT(cl_arith_compare(obj, CL_MAKE_FIXNUM(CL_FIXNUM_MAX)) > 0);
}

TEST(c_bignum_normalize_to_fixnum)
{
    CL_Obj obj = cl_make_bignum(1, 0);
    CL_Bignum *bn = (CL_Bignum *)CL_OBJ_TO_PTR(obj);
    bn->limbs[0] = 42;
    obj = cl_bignum_normalize(obj);
    ASSERT(CL_FIXNUM_P(obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(obj), 42);
}

TEST(c_bignum_equal)
{
    CL_Obj a, b;
    a = cl_arith_mul(CL_MAKE_FIXNUM(1000000), CL_MAKE_FIXNUM(1000000));
    b = cl_arith_mul(CL_MAKE_FIXNUM(1000000), CL_MAKE_FIXNUM(1000000));
    ASSERT(cl_bignum_equal(a, b));
}

TEST(c_arith_zerop)
{
    ASSERT(cl_arith_zerop(CL_MAKE_FIXNUM(0)));
    ASSERT(!cl_arith_zerop(CL_MAKE_FIXNUM(1)));
    ASSERT(!cl_arith_zerop(cl_arith_mul(CL_MAKE_FIXNUM(1000000), CL_MAKE_FIXNUM(1000000))));
}

TEST(c_arith_compare)
{
    CL_Obj big = cl_arith_mul(CL_MAKE_FIXNUM(1000000), CL_MAKE_FIXNUM(1000000));
    ASSERT_EQ_INT(cl_arith_compare(big, CL_MAKE_FIXNUM(42)), 1);
    ASSERT_EQ_INT(cl_arith_compare(CL_MAKE_FIXNUM(42), big), -1);
    ASSERT_EQ_INT(cl_arith_compare(big, big), 0);
}

/* ================================================================
 * Bignum logand masking to 32 bits (CDB hash pattern)
 * ================================================================ */

TEST(logand_mask_32bit)
{
    /* logand with #xFFFFFFFF should truncate to 32 bits */
    ASSERT_STR_EQ(eval_print("(logand #xFFFFFFFF 6382565244)"), "2087597948");
    ASSERT_STR_EQ(eval_print("(logand #xFFFFFFFF 6950613443893)"), "1356358965");
    ASSERT_STR_EQ(eval_print("(logand #xFFFFFFFF 4294967296)"), "0");
    ASSERT_STR_EQ(eval_print("(logand #xFFFFFFFF 4294967295)"), "4294967295");
    ASSERT_STR_EQ(eval_print("(logand #xFFFFFFFF 8589934591)"), "4294967295");
}

TEST(logand_bignum_with_bignum)
{
    /* Both args bignums */
    ASSERT_STR_EQ(eval_print("(logand #xFFFFFFFF #xFFFFFFFF)"), "4294967295");
    ASSERT_STR_EQ(eval_print("(logand #x1FFFFFFFF #xFFFFFFFF)"), "4294967295");
    ASSERT_STR_EQ(eval_print("(logand #xFFFF0000FFFF #x0000FFFFFFFF)"), "65535");
}

TEST(logxor_bignum)
{
    ASSERT_STR_EQ(eval_print("(logxor 2087597948 101)"), "2087597849");
    ASSERT_STR_EQ(eval_print("(logxor #xFFFFFFFF 0)"), "4294967295");
    ASSERT_STR_EQ(eval_print("(logxor #xFFFFFFFF #xFFFFFFFF)"), "0");
}

TEST(ash_bignum_left)
{
    /* Shift that produces bignum, then mask */
    ASSERT_STR_EQ(eval_print("(ash 193411068 5)"), "6189154176");
    ASSERT_STR_EQ(eval_print("(logand #xFFFFFFFF (ash 193411068 5))"), "1894186880");
}

TEST(cdb_hash_fiveam)
{
    /* Full CDB hash of "fiveam" — the exact pattern that fails */
    ASSERT_STR_EQ(eval_print(
        "(let ((h 5381))"
        "  (dolist (c '(102 105 118 101 97 109) h)"
        "    (setf h (logand #xFFFFFFFF (+ h (ash h 5))))"
        "    (setf h (logxor h c))))"),
        "1356358965");
}

TEST(cdb_hash_intermediate_steps)
{
    /* Step-by-step CDB hash to find where it diverges */
    /* After 'f' (102): (logand #xFFFFFFFF (+ 5381 (ash 5381 5))) = 177573, xor 102 = 177603 */
    ASSERT_STR_EQ(eval_print("(logxor (logand #xFFFFFFFF (+ 5381 (ash 5381 5))) 102)"), "177603");
    /* After 'i' (105): */
    ASSERT_STR_EQ(eval_print("(logxor (logand #xFFFFFFFF (+ 177603 (ash 177603 5))) 105)"), "5860938");
    /* After 'v' (118): */
    ASSERT_STR_EQ(eval_print("(logxor (logand #xFFFFFFFF (+ 5860938 (ash 5860938 5))) 118)"), "193411068");
    /* After 'e' (101): h + ash h 5 = 193411068 + 6189154176 = 6382565244 */
    ASSERT_STR_EQ(eval_print("(+ 193411068 (ash 193411068 5))"), "6382565244");
    ASSERT_STR_EQ(eval_print("(logand #xFFFFFFFF 6382565244)"), "2087597948");
    ASSERT_STR_EQ(eval_print("(logxor 2087597948 101)"), "2087597849");
}

/* ================================================================
 * main
 * ================================================================ */

int main(void)
{
    test_init();
    setup();

    /* Fixnum overflow → bignum promotion */
    RUN(add_overflow_positive);
    RUN(add_overflow_negative);
    RUN(sub_overflow);
    RUN(mul_overflow);
    RUN(mul_large);

    /* Bignum → fixnum demotion */
    RUN(demotion_add);
    RUN(demotion_sub);
    RUN(demotion_mul);

    /* Reader parsing */
    RUN(read_large_positive);
    RUN(read_large_negative);
    RUN(read_just_over_fixnum);
    RUN(read_just_under_neg_fixnum);
    RUN(read_fixnum_boundary);

    /* Large number arithmetic */
    RUN(factorial_10);
    RUN(factorial_20);
    RUN(factorial_30);
    RUN(expt_2_32);
    RUN(expt_2_64);
    RUN(expt_2_100);
    RUN(expt_10_18);
    RUN(expt_neg_base);
    RUN(expt_zero);
    RUN(expt_one);

    /* Comparison */
    RUN(compare_bignum_equal);
    RUN(compare_bignum_lt);
    RUN(compare_bignum_gt);
    RUN(compare_bignum_fixnum);
    RUN(compare_negative_bignums);

    /* Mixed arithmetic */
    RUN(mixed_add);
    RUN(mixed_sub);
    RUN(mixed_mul);

    /* Division and modulo */
    RUN(div_bignum);
    RUN(div_bignum_by_fixnum);
    RUN(mod_bignum);
    RUN(mod_positive);
    RUN(mod_negative_dividend);
    RUN(rem_positive);
    RUN(rem_negative_dividend);

    /* Negation and abs */
    RUN(negate_bignum);
    RUN(abs_negative_bignum);
    RUN(negate_most_negative_fixnum);

    /* GCD and LCM */
    RUN(gcd_basic);
    RUN(gcd_bignum);
    RUN(gcd_coprime);
    RUN(gcd_zero);
    RUN(gcd_no_args);
    RUN(lcm_basic);
    RUN(lcm_no_args);

    /* Bitwise */
    RUN(logand_basic);
    RUN(logior_basic);
    RUN(logxor_basic);
    RUN(lognot_basic);
    RUN(ash_left);
    RUN(ash_right);
    RUN(ash_large);
    RUN(logand_no_args);
    RUN(logand_negative_one_identity);
    RUN(logior_no_args);

    /* Integer-length, isqrt */
    RUN(integer_length_basic);
    RUN(integer_length_negative);
    RUN(isqrt_basic);
    RUN(isqrt_large);

    /* Evenp, oddp */
    RUN(evenp_fixnum);
    RUN(oddp_fixnum);
    RUN(evenp_bignum);

    /* eql/equal */
    RUN(eql_bignums);
    RUN(equal_bignums);

    /* Type predicates */
    RUN(typep_fixnum);
    RUN(typep_bignum);
    RUN(type_of_bignum);
    RUN(numberp_integerp);

    /* Constants */
    RUN(fixnum_constants);
    RUN(fixnum_boundary_add);
    RUN(fixnum_boundary_sub);

    /* Subtypep */
    RUN(subtypep_bignum);

    /* Predicates */
    RUN(predicates_bignum);

    /* Max/min */
    RUN(max_min_bignum);

    /* VM opcodes */
    RUN(vm_add_overflow);
    RUN(vm_sub_overflow);
    RUN(vm_mul_overflow);
    RUN(vm_numeq_bignum);
    RUN(vm_lt_bignum);

    /* Hash table */
    RUN(hash_table_bignum_key);

    /* Negative exponent */
    RUN(expt_negative_exp);

    /* Multi-arg */
    RUN(multi_arg_add);
    RUN(multi_arg_mul);

    /* Bignum logand masking / CDB hash pattern */
    RUN(logand_mask_32bit);
    RUN(logand_bignum_with_bignum);
    RUN(logxor_bignum);
    RUN(ash_bignum_left);
    RUN(cdb_hash_fiveam);
    RUN(cdb_hash_intermediate_steps);

    /* C-level API */
    RUN(c_bignum_from_int32);
    RUN(c_bignum_normalize_to_fixnum);
    RUN(c_bignum_equal);
    RUN(c_arith_zerop);
    RUN(c_arith_compare);

    teardown();
    REPORT();
}
