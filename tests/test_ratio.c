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
#include "core/ratio.h"
#include "core/bignum.h"
#include "core/float.h"
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

/* Helper: print to C buffer */
static const char *print_ratio(CL_Obj obj)
{
    static char buf[256];
    cl_prin1_to_string(obj, buf, sizeof(buf));
    return buf;
}

/* Helper: eval a string, return printed result */
static const char *eval_print(const char *str)
{
    static char buf[256];
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
        return buf;
    }
}

/* --- Raw allocation and predicates --- */

TEST(ratio_alloc_and_predicates)
{
    CL_Obj num = CL_MAKE_FIXNUM(1);
    CL_Obj den = CL_MAKE_FIXNUM(2);
    CL_Obj r = cl_make_ratio(num, den);

    ASSERT(CL_RATIO_P(r));
    ASSERT(CL_RATIONAL_P(r));
    ASSERT(CL_REALP(r));
    ASSERT(CL_NUMBER_P(r));
    ASSERT(!CL_FIXNUM_P(r));
    ASSERT(!CL_BIGNUM_P(r));
    ASSERT(!CL_INTEGER_P(r));
    ASSERT(!CL_FLOATP(r));
}

TEST(ratio_accessors)
{
    CL_Obj num = CL_MAKE_FIXNUM(3);
    CL_Obj den = CL_MAKE_FIXNUM(4);
    CL_Obj r = cl_make_ratio(num, den);

    ASSERT_EQ(cl_numerator(r), CL_MAKE_FIXNUM(3));
    ASSERT_EQ(cl_denominator(r), CL_MAKE_FIXNUM(4));
}

TEST(integer_accessors)
{
    CL_Obj n = CL_MAKE_FIXNUM(5);
    ASSERT_EQ(cl_numerator(n), CL_MAKE_FIXNUM(5));
    ASSERT_EQ(cl_denominator(n), CL_MAKE_FIXNUM(1));
}

TEST(integer_predicates)
{
    CL_Obj n = CL_MAKE_FIXNUM(7);
    ASSERT(!CL_RATIO_P(n));
    ASSERT(CL_RATIONAL_P(n));
    ASSERT(CL_INTEGER_P(n));
}

TEST(type_name)
{
    CL_Obj r = cl_make_ratio(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(2));
    ASSERT_STR_EQ(cl_type_name(r), "RATIO");
}

/* --- Normalization --- */

TEST(normalize_reduce)
{
    /* 6/4 -> 3/2 */
    CL_Obj r = cl_make_ratio_normalized(CL_MAKE_FIXNUM(6), CL_MAKE_FIXNUM(4));
    ASSERT(CL_RATIO_P(r));
    ASSERT_EQ(cl_numerator(r), CL_MAKE_FIXNUM(3));
    ASSERT_EQ(cl_denominator(r), CL_MAKE_FIXNUM(2));
}

TEST(normalize_to_integer)
{
    /* 6/3 -> 2 */
    CL_Obj r = cl_make_ratio_normalized(CL_MAKE_FIXNUM(6), CL_MAKE_FIXNUM(3));
    ASSERT(CL_FIXNUM_P(r));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(r), 2);
}

TEST(normalize_zero)
{
    /* 0/5 -> 0 */
    CL_Obj r = cl_make_ratio_normalized(CL_MAKE_FIXNUM(0), CL_MAKE_FIXNUM(5));
    ASSERT(CL_FIXNUM_P(r));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(r), 0);
}

TEST(normalize_negative_den)
{
    /* 3/-6 -> -1/2 */
    CL_Obj r = cl_make_ratio_normalized(CL_MAKE_FIXNUM(3), CL_MAKE_FIXNUM(-6));
    ASSERT(CL_RATIO_P(r));
    ASSERT_EQ(cl_numerator(r), CL_MAKE_FIXNUM(-1));
    ASSERT_EQ(cl_denominator(r), CL_MAKE_FIXNUM(2));
}

TEST(normalize_both_negative)
{
    /* -3/-6 -> 1/2 */
    CL_Obj r = cl_make_ratio_normalized(CL_MAKE_FIXNUM(-3), CL_MAKE_FIXNUM(-6));
    ASSERT(CL_RATIO_P(r));
    ASSERT_EQ(cl_numerator(r), CL_MAKE_FIXNUM(1));
    ASSERT_EQ(cl_denominator(r), CL_MAKE_FIXNUM(2));
}

/* --- Basic arithmetic --- */

TEST(ratio_add)
{
    /* 1/2 + 1/3 = 5/6 */
    CL_Obj a = cl_make_ratio_normalized(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(2));
    CL_Obj b = cl_make_ratio_normalized(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(3));
    CL_Obj r = cl_ratio_add(a, b);
    ASSERT(CL_RATIO_P(r));
    ASSERT_EQ(cl_numerator(r), CL_MAKE_FIXNUM(5));
    ASSERT_EQ(cl_denominator(r), CL_MAKE_FIXNUM(6));
}

TEST(ratio_add_to_integer)
{
    /* 1/2 + 1/2 = 1 */
    CL_Obj a = cl_make_ratio_normalized(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(2));
    CL_Obj r = cl_ratio_add(a, a);
    ASSERT(CL_FIXNUM_P(r));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(r), 1);
}

TEST(ratio_sub)
{
    /* 5/6 - 1/3 = 1/2 */
    CL_Obj a = cl_make_ratio_normalized(CL_MAKE_FIXNUM(5), CL_MAKE_FIXNUM(6));
    CL_Obj b = cl_make_ratio_normalized(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(3));
    CL_Obj r = cl_ratio_sub(a, b);
    ASSERT(CL_RATIO_P(r));
    ASSERT_EQ(cl_numerator(r), CL_MAKE_FIXNUM(1));
    ASSERT_EQ(cl_denominator(r), CL_MAKE_FIXNUM(2));
}

TEST(ratio_mul)
{
    /* 2/3 * 3/4 = 1/2 */
    CL_Obj a = cl_make_ratio_normalized(CL_MAKE_FIXNUM(2), CL_MAKE_FIXNUM(3));
    CL_Obj b = cl_make_ratio_normalized(CL_MAKE_FIXNUM(3), CL_MAKE_FIXNUM(4));
    CL_Obj r = cl_ratio_mul(a, b);
    ASSERT(CL_RATIO_P(r));
    ASSERT_EQ(cl_numerator(r), CL_MAKE_FIXNUM(1));
    ASSERT_EQ(cl_denominator(r), CL_MAKE_FIXNUM(2));
}

TEST(ratio_div)
{
    /* (1/2) / (3/4) = 2/3 */
    CL_Obj a = cl_make_ratio_normalized(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(2));
    CL_Obj b = cl_make_ratio_normalized(CL_MAKE_FIXNUM(3), CL_MAKE_FIXNUM(4));
    CL_Obj r = cl_ratio_div(a, b);
    ASSERT(CL_RATIO_P(r));
    ASSERT_EQ(cl_numerator(r), CL_MAKE_FIXNUM(2));
    ASSERT_EQ(cl_denominator(r), CL_MAKE_FIXNUM(3));
}

TEST(ratio_negate)
{
    CL_Obj a = cl_make_ratio_normalized(CL_MAKE_FIXNUM(3), CL_MAKE_FIXNUM(4));
    CL_Obj r = cl_ratio_negate(a);
    ASSERT(CL_RATIO_P(r));
    ASSERT_EQ(cl_numerator(r), CL_MAKE_FIXNUM(-3));
    ASSERT_EQ(cl_denominator(r), CL_MAKE_FIXNUM(4));
}

TEST(ratio_abs)
{
    CL_Obj a = cl_make_ratio_normalized(CL_MAKE_FIXNUM(-3), CL_MAKE_FIXNUM(4));
    CL_Obj r = cl_ratio_abs(a);
    ASSERT(CL_RATIO_P(r));
    ASSERT_EQ(cl_numerator(r), CL_MAKE_FIXNUM(3));
    ASSERT_EQ(cl_denominator(r), CL_MAKE_FIXNUM(4));
}

/* --- Comparison --- */

TEST(ratio_compare)
{
    CL_Obj a = cl_make_ratio_normalized(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(2));
    CL_Obj b = cl_make_ratio_normalized(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(3));
    ASSERT(cl_ratio_compare(a, b) > 0);  /* 1/2 > 1/3 */
    ASSERT(cl_ratio_compare(b, a) < 0);  /* 1/3 < 1/2 */
    ASSERT(cl_ratio_compare(a, a) == 0); /* 1/2 = 1/2 */
}

TEST(ratio_zerop_plusp_minusp)
{
    CL_Obj pos = cl_make_ratio_normalized(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(2));
    CL_Obj neg = cl_make_ratio_normalized(CL_MAKE_FIXNUM(-1), CL_MAKE_FIXNUM(2));

    ASSERT(!cl_ratio_zerop(pos));
    ASSERT(cl_ratio_plusp(pos));
    ASSERT(!cl_ratio_minusp(pos));

    ASSERT(!cl_ratio_zerop(neg));
    ASSERT(!cl_ratio_plusp(neg));
    ASSERT(cl_ratio_minusp(neg));
}

/* --- Equality and hashing --- */

TEST(ratio_equal)
{
    CL_Obj a = cl_make_ratio_normalized(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(2));
    CL_Obj b = cl_make_ratio_normalized(CL_MAKE_FIXNUM(2), CL_MAKE_FIXNUM(4));
    /* Both normalize to 1/2 */
    ASSERT(cl_ratio_equal(a, b));
}

TEST(ratio_hash_consistent)
{
    CL_Obj a = cl_make_ratio_normalized(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(2));
    CL_Obj b = cl_make_ratio_normalized(CL_MAKE_FIXNUM(2), CL_MAKE_FIXNUM(4));
    ASSERT_EQ(cl_ratio_hash(a), cl_ratio_hash(b));
}

/* --- Conversion --- */

TEST(ratio_to_double)
{
    CL_Obj r = cl_make_ratio_normalized(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(2));
    double d = cl_ratio_to_double(r);
    ASSERT(d > 0.499 && d < 0.501);
}

/* --- Integer-ratio mixed arithmetic --- */

TEST(ratio_add_integer)
{
    /* 1/2 + 1 = 3/2 */
    CL_Obj a = cl_make_ratio_normalized(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(2));
    CL_Obj b = CL_MAKE_FIXNUM(1);
    CL_Obj r = cl_ratio_add(a, b);
    ASSERT(CL_RATIO_P(r));
    ASSERT_EQ(cl_numerator(r), CL_MAKE_FIXNUM(3));
    ASSERT_EQ(cl_denominator(r), CL_MAKE_FIXNUM(2));
}

TEST(ratio_mul_integer)
{
    /* 1/3 * 6 = 2 */
    CL_Obj a = cl_make_ratio_normalized(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(3));
    CL_Obj b = CL_MAKE_FIXNUM(6);
    CL_Obj r = cl_ratio_mul(a, b);
    ASSERT(CL_FIXNUM_P(r));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(r), 2);
}

/* --- GC survival --- */

TEST(ratio_gc_survival)
{
    CL_Obj r = cl_make_ratio(CL_MAKE_FIXNUM(7), CL_MAKE_FIXNUM(11));
    CL_GC_PROTECT(r);
    cl_gc();
    ASSERT(CL_RATIO_P(r));
    ASSERT_EQ(cl_numerator(r), CL_MAKE_FIXNUM(7));
    ASSERT_EQ(cl_denominator(r), CL_MAKE_FIXNUM(11));
    CL_GC_UNPROTECT(1);
}

/* --- Printer --- */

TEST(print_ratio_simple)
{
    CL_Obj r = cl_make_ratio_normalized(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(2));
    ASSERT_STR_EQ(print_ratio(r), "1/2");
}

TEST(print_ratio_negative)
{
    CL_Obj r = cl_make_ratio_normalized(CL_MAKE_FIXNUM(-3), CL_MAKE_FIXNUM(4));
    ASSERT_STR_EQ(print_ratio(r), "-3/4");
}

TEST(print_ratio_large)
{
    CL_Obj r = cl_make_ratio_normalized(CL_MAKE_FIXNUM(355), CL_MAKE_FIXNUM(113));
    ASSERT_STR_EQ(print_ratio(r), "355/113");
}

/* --- Eval-based tests (division produces ratios) --- */

TEST(eval_div_ratio)
{
    ASSERT_STR_EQ(eval_print("(/ 1 2)"), "1/2");
}

TEST(eval_div_reduces)
{
    ASSERT_STR_EQ(eval_print("(/ 6 4)"), "3/2");
}

TEST(eval_div_to_integer)
{
    ASSERT_STR_EQ(eval_print("(/ 6 3)"), "2");
}

TEST(eval_ratio_add)
{
    ASSERT_STR_EQ(eval_print("(+ (/ 1 2) (/ 1 3))"), "5/6");
}

TEST(eval_ratio_float_contagion)
{
    ASSERT_STR_EQ(eval_print("(+ (/ 1 2) 0.5)"), "1.0");
}

TEST(eval_ratio_compare)
{
    ASSERT_STR_EQ(eval_print("(< (/ 1 3) (/ 1 2))"), "T");
    ASSERT_STR_EQ(eval_print("(> (/ 1 3) (/ 1 2))"), "NIL");
    ASSERT_STR_EQ(eval_print("(= (/ 2 4) (/ 1 2))"), "T");
}

TEST(eval_rationalp)
{
    ASSERT_STR_EQ(eval_print("(rationalp (/ 1 2))"), "T");
    ASSERT_STR_EQ(eval_print("(rationalp 5)"), "T");
    ASSERT_STR_EQ(eval_print("(rationalp 1.0)"), "NIL");
}

/* --- Reader tests --- */

/* Helper: read from string */
static CL_Obj reads(const char *str)
{
    CL_ReadStream stream;
    stream.buf = str;
    stream.pos = 0;
    stream.len = (int)strlen(str);
    stream.line = 1;
    return cl_read_from_string(&stream);
}

TEST(read_ratio_simple)
{
    CL_Obj r = reads("1/2");
    ASSERT(CL_RATIO_P(r));
    ASSERT_EQ(cl_numerator(r), CL_MAKE_FIXNUM(1));
    ASSERT_EQ(cl_denominator(r), CL_MAKE_FIXNUM(2));
}

TEST(read_ratio_negative)
{
    CL_Obj r = reads("-3/4");
    ASSERT(CL_RATIO_P(r));
    ASSERT_EQ(cl_numerator(r), CL_MAKE_FIXNUM(-3));
    ASSERT_EQ(cl_denominator(r), CL_MAKE_FIXNUM(4));
}

TEST(read_ratio_positive_sign)
{
    CL_Obj r = reads("+5/7");
    ASSERT(CL_RATIO_P(r));
    ASSERT_EQ(cl_numerator(r), CL_MAKE_FIXNUM(5));
    ASSERT_EQ(cl_denominator(r), CL_MAKE_FIXNUM(7));
}

TEST(read_ratio_normalizes)
{
    /* 6/4 normalizes to 3/2 */
    CL_Obj r = reads("6/4");
    ASSERT(CL_RATIO_P(r));
    ASSERT_EQ(cl_numerator(r), CL_MAKE_FIXNUM(3));
    ASSERT_EQ(cl_denominator(r), CL_MAKE_FIXNUM(2));
}

TEST(read_ratio_demotes_to_integer)
{
    /* 6/3 = 2 */
    CL_Obj r = reads("6/3");
    ASSERT(CL_FIXNUM_P(r));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(r), 2);
}

TEST(read_ratio_roundtrip)
{
    ASSERT_STR_EQ(print_ratio(reads("1/2")), "1/2");
    ASSERT_STR_EQ(print_ratio(reads("-3/4")), "-3/4");
    ASSERT_STR_EQ(print_ratio(reads("355/113")), "355/113");
}

TEST(read_ratio_in_expr)
{
    /* Reader literal in expression */
    ASSERT_STR_EQ(eval_print("(+ 1/2 1/3)"), "5/6");
    ASSERT_STR_EQ(eval_print("(* 1/2 1/2)"), "1/4");
    ASSERT_STR_EQ(eval_print("(< 1/3 1/2)"), "T");
}

TEST(read_ratio_zero_num)
{
    /* 0/5 = 0 */
    CL_Obj r = reads("0/5");
    ASSERT(CL_FIXNUM_P(r));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(r), 0);
}

TEST(read_ratio_large)
{
    /* Large numerator/denominator (bignum territory) */
    CL_Obj r = reads("1000000000000/3");
    ASSERT(CL_RATIO_P(r));
    ASSERT(CL_BIGNUM_P(cl_numerator(r)));
    ASSERT_EQ(cl_denominator(r), CL_MAKE_FIXNUM(3));
}

/* --- Step 6: eql/equal, typep, coerce, rounding, hash tables --- */

TEST(eval_eql_ratio)
{
    ASSERT_STR_EQ(eval_print("(eql 1/2 1/2)"), "T");
    ASSERT_STR_EQ(eval_print("(eql 1/2 2/4)"), "T");
    ASSERT_STR_EQ(eval_print("(eql 1/2 1/3)"), "NIL");
    /* eql does not cross types */
    ASSERT_STR_EQ(eval_print("(eql 1/2 0.5)"), "NIL");
}

TEST(eval_equal_ratio)
{
    ASSERT_STR_EQ(eval_print("(equal 1/2 1/2)"), "T");
    ASSERT_STR_EQ(eval_print("(equal 1/2 2/4)"), "T");
    ASSERT_STR_EQ(eval_print("(equal 1/2 1/3)"), "NIL");
}

TEST(eval_typep_ratio)
{
    ASSERT_STR_EQ(eval_print("(typep 1/2 'ratio)"), "T");
    ASSERT_STR_EQ(eval_print("(typep 1/2 'rational)"), "T");
    ASSERT_STR_EQ(eval_print("(typep 1/2 'real)"), "T");
    ASSERT_STR_EQ(eval_print("(typep 1/2 'number)"), "T");
    ASSERT_STR_EQ(eval_print("(typep 1/2 'integer)"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep 1/2 'float)"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep 5 'ratio)"), "NIL");
}

TEST(eval_type_of_ratio)
{
    ASSERT_STR_EQ(eval_print("(type-of 1/2)"), "RATIO");
}

TEST(eval_subtypep_ratio)
{
    ASSERT_STR_EQ(eval_print("(subtypep 'ratio 'rational)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'ratio 'real)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'ratio 'number)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'ratio 'integer)"), "NIL");
}

TEST(eval_coerce_ratio_to_float)
{
    ASSERT_STR_EQ(eval_print("(coerce 1/2 'single-float)"), "0.5");
    ASSERT_STR_EQ(eval_print("(coerce 1/4 'double-float)"), "0.25d0");
    ASSERT_STR_EQ(eval_print("(coerce 1/2 'float)"), "0.5");
}

TEST(eval_floor_ratio)
{
    ASSERT_STR_EQ(eval_print("(floor 7/2)"), "3");
    ASSERT_STR_EQ(eval_print("(floor -7/2)"), "-4");
}

TEST(eval_ceiling_ratio)
{
    ASSERT_STR_EQ(eval_print("(ceiling 7/2)"), "4");
    ASSERT_STR_EQ(eval_print("(ceiling -7/2)"), "-3");
}

TEST(eval_truncate_ratio)
{
    ASSERT_STR_EQ(eval_print("(truncate 7/2)"), "3");
    ASSERT_STR_EQ(eval_print("(truncate -7/2)"), "-3");
}

TEST(eval_round_ratio)
{
    ASSERT_STR_EQ(eval_print("(round 7/2)"), "4");
    ASSERT_STR_EQ(eval_print("(round 3/2)"), "2");
}

TEST(eval_ratio_hashtable)
{
    /* Ratio keys in hash tables with eql test */
    ASSERT_STR_EQ(eval_print(
        "(let ((ht (make-hash-table))) "
        "  (setf (gethash 1/2 ht) 'half) "
        "  (gethash 1/2 ht))"), "HALF");
    /* 2/4 normalizes to 1/2, should find same entry */
    ASSERT_STR_EQ(eval_print(
        "(let ((ht (make-hash-table))) "
        "  (setf (gethash 1/2 ht) 'half) "
        "  (gethash 2/4 ht))"), "HALF");
}

/* --- Step 7: builtins --- */

TEST(eval_numerator)
{
    ASSERT_STR_EQ(eval_print("(numerator 3/4)"), "3");
    ASSERT_STR_EQ(eval_print("(numerator -3/4)"), "-3");
    ASSERT_STR_EQ(eval_print("(numerator 5)"), "5");
    ASSERT_STR_EQ(eval_print("(numerator 0)"), "0");
}

TEST(eval_denominator)
{
    ASSERT_STR_EQ(eval_print("(denominator 3/4)"), "4");
    ASSERT_STR_EQ(eval_print("(denominator -3/4)"), "4");
    ASSERT_STR_EQ(eval_print("(denominator 5)"), "1");
}

TEST(eval_rational)
{
    /* Integers pass through */
    ASSERT_STR_EQ(eval_print("(rational 5)"), "5");
    /* Ratios pass through */
    ASSERT_STR_EQ(eval_print("(rational 1/2)"), "1/2");
    /* Float → exact rational */
    ASSERT_STR_EQ(eval_print("(rational 0.5)"), "1/2");
    ASSERT_STR_EQ(eval_print("(rational 0.25)"), "1/4");
    ASSERT_STR_EQ(eval_print("(rational 0.0)"), "0");
    ASSERT_STR_EQ(eval_print("(rational -0.5)"), "-1/2");
}

TEST(eval_rationalize)
{
    ASSERT_STR_EQ(eval_print("(rationalize 0.5)"), "1/2");
    ASSERT_STR_EQ(eval_print("(rationalize 1/3)"), "1/3");
}

/* --- Step 8: edge cases --- */

TEST(eval_expt_ratio)
{
    ASSERT_STR_EQ(eval_print("(expt 1/2 3)"), "1/8");
    ASSERT_STR_EQ(eval_print("(expt 2/3 2)"), "4/9");
    ASSERT_STR_EQ(eval_print("(expt 1/2 0)"), "1");
    ASSERT_STR_EQ(eval_print("(expt 2/3 -1)"), "3/2");
    ASSERT_STR_EQ(eval_print("(expt -1/2 3)"), "-1/8");
}

TEST(eval_1plus_1minus_ratio)
{
    ASSERT_STR_EQ(eval_print("(1+ 1/2)"), "3/2");
    ASSERT_STR_EQ(eval_print("(1- 1/2)"), "-1/2");
    ASSERT_STR_EQ(eval_print("(1+ -1/2)"), "1/2");
}

TEST(eval_mod_rem_ratio)
{
    /* mod/rem on rationals: defined via floor/truncate */
    ASSERT_STR_EQ(eval_print("(mod 5/2 1)"), "1/2");
    ASSERT_STR_EQ(eval_print("(rem 5/2 1)"), "1/2");
}

TEST(eval_numeq_cross_type)
{
    /* = compares across types numerically */
    ASSERT_STR_EQ(eval_print("(= 1/2 0.5)"), "T");
    ASSERT_STR_EQ(eval_print("(= 2/1 2)"), "T");
    ASSERT_STR_EQ(eval_print("(= 0/1 0)"), "T");
    ASSERT_STR_EQ(eval_print("(= 1/3 0.5)"), "NIL");
}

TEST(eval_compare_cross_type)
{
    ASSERT_STR_EQ(eval_print("(< 1/3 1)"), "T");
    ASSERT_STR_EQ(eval_print("(> 3/2 1)"), "T");
    ASSERT_STR_EQ(eval_print("(<= 1/2 0.5)"), "T");
    ASSERT_STR_EQ(eval_print("(>= 1/2 1/3)"), "T");
}

TEST(eval_min_max_ratio)
{
    ASSERT_STR_EQ(eval_print("(min 1/2 1/3 1/4)"), "1/4");
    ASSERT_STR_EQ(eval_print("(max 1/2 1/3 1/4)"), "1/2");
    ASSERT_STR_EQ(eval_print("(min 1/2 1)"), "1/2");
    ASSERT_STR_EQ(eval_print("(max 1/2 1)"), "1");
}

TEST(eval_abs_ratio)
{
    ASSERT_STR_EQ(eval_print("(abs -3/4)"), "3/4");
    ASSERT_STR_EQ(eval_print("(abs 3/4)"), "3/4");
}

TEST(eval_zerop_plusp_minusp_ratio)
{
    ASSERT_STR_EQ(eval_print("(zerop 0/1)"), "T");
    ASSERT_STR_EQ(eval_print("(plusp 1/2)"), "T");
    ASSERT_STR_EQ(eval_print("(minusp -1/2)"), "T");
    ASSERT_STR_EQ(eval_print("(plusp -1/2)"), "NIL");
}

TEST(eval_div_reciprocal)
{
    /* (/ x) = reciprocal */
    ASSERT_STR_EQ(eval_print("(/ 3)"), "1/3");
    ASSERT_STR_EQ(eval_print("(/ 1/3)"), "3");
    ASSERT_STR_EQ(eval_print("(/ 2/3)"), "3/2");
}

TEST(eval_div_chained)
{
    /* (/ a b c) = a / b / c */
    ASSERT_STR_EQ(eval_print("(/ 1 2 3)"), "1/6");
    ASSERT_STR_EQ(eval_print("(/ 12 2 3)"), "2");
}

TEST(eval_ratio_bignum_mixed)
{
    /* Ratio with bignum component */
    ASSERT_STR_EQ(eval_print("(+ 1/2 1000000000000)"), "2000000000001/2");
    ASSERT_STR_EQ(eval_print("(* 1/3 999999999999)"), "333333333333");
}

int main(void)
{
    test_init();
    setup();

    /* Allocation and predicates */
    RUN(ratio_alloc_and_predicates);
    RUN(ratio_accessors);
    RUN(integer_accessors);
    RUN(integer_predicates);
    RUN(type_name);

    /* Normalization */
    RUN(normalize_reduce);
    RUN(normalize_to_integer);
    RUN(normalize_zero);
    RUN(normalize_negative_den);
    RUN(normalize_both_negative);

    /* Arithmetic */
    RUN(ratio_add);
    RUN(ratio_add_to_integer);
    RUN(ratio_sub);
    RUN(ratio_mul);
    RUN(ratio_div);
    RUN(ratio_negate);
    RUN(ratio_abs);

    /* Comparison */
    RUN(ratio_compare);
    RUN(ratio_zerop_plusp_minusp);

    /* Equality and hashing */
    RUN(ratio_equal);
    RUN(ratio_hash_consistent);

    /* Conversion */
    RUN(ratio_to_double);

    /* Mixed arithmetic */
    RUN(ratio_add_integer);
    RUN(ratio_mul_integer);

    /* GC */
    RUN(ratio_gc_survival);

    /* Printer */
    RUN(print_ratio_simple);
    RUN(print_ratio_negative);
    RUN(print_ratio_large);

    /* Eval-based */
    RUN(eval_div_ratio);
    RUN(eval_div_reduces);
    RUN(eval_div_to_integer);
    RUN(eval_ratio_add);
    RUN(eval_ratio_float_contagion);
    RUN(eval_ratio_compare);
    RUN(eval_rationalp);

    /* Step 6: eql/equal, typep, coerce, rounding, hash tables */
    RUN(eval_eql_ratio);
    RUN(eval_equal_ratio);
    RUN(eval_typep_ratio);
    RUN(eval_type_of_ratio);
    RUN(eval_subtypep_ratio);
    RUN(eval_coerce_ratio_to_float);
    RUN(eval_floor_ratio);
    RUN(eval_ceiling_ratio);
    RUN(eval_truncate_ratio);
    RUN(eval_round_ratio);
    RUN(eval_ratio_hashtable);

    /* Step 7: builtins */
    RUN(eval_numerator);
    RUN(eval_denominator);
    RUN(eval_rational);
    RUN(eval_rationalize);

    /* Reader */
    RUN(read_ratio_simple);
    RUN(read_ratio_negative);
    RUN(read_ratio_positive_sign);
    RUN(read_ratio_normalizes);
    RUN(read_ratio_demotes_to_integer);
    RUN(read_ratio_roundtrip);
    RUN(read_ratio_in_expr);
    RUN(read_ratio_zero_num);
    RUN(read_ratio_large);

    /* Step 8: edge cases */
    RUN(eval_expt_ratio);
    RUN(eval_1plus_1minus_ratio);
    RUN(eval_mod_rem_ratio);
    RUN(eval_numeq_cross_type);
    RUN(eval_compare_cross_type);
    RUN(eval_min_max_ratio);
    RUN(eval_abs_ratio);
    RUN(eval_zerop_plusp_minusp_ratio);
    RUN(eval_div_reciprocal);
    RUN(eval_div_chained);
    RUN(eval_ratio_bignum_mixed);

    teardown();
    REPORT();
}
