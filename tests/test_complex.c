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

/* Helper: eval and return result */
static CL_Obj eval(const char *str)
{
    return cl_eval_string(str);
}

/* Helper: eval and return printed result */
static const char *eval_print(const char *str)
{
    static char buf[256];
    int err;
    CL_CATCH(err);
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

/* --- Tests --- */

TEST(complex_reader)
{
    CL_Obj c = eval("#C(5 16)");
    ASSERT(CL_COMPLEX_P(c));
    CL_Complex *cx = (CL_Complex *)CL_OBJ_TO_PTR(c);
    ASSERT_EQ(CL_FIXNUM_VAL(cx->realpart), 5);
    ASSERT_EQ(CL_FIXNUM_VAL(cx->imagpart), 16);
}

TEST(complex_reader_negative)
{
    CL_Obj c = eval("#C(-4 15)");
    ASSERT(CL_COMPLEX_P(c));
    CL_Complex *cx = (CL_Complex *)CL_OBJ_TO_PTR(c);
    ASSERT_EQ(CL_FIXNUM_VAL(cx->realpart), -4);
    ASSERT_EQ(CL_FIXNUM_VAL(cx->imagpart), 15);
}

TEST(complex_reader_float)
{
    CL_Obj c = eval("#C(1.5 2.5)");
    ASSERT(CL_COMPLEX_P(c));
    CL_Complex *cx = (CL_Complex *)CL_OBJ_TO_PTR(c);
    ASSERT(CL_SINGLE_FLOAT_P(cx->realpart));
    ASSERT(CL_SINGLE_FLOAT_P(cx->imagpart));
}

TEST(complex_printer)
{
    ASSERT_STR_EQ(eval_print("#C(5 16)"), "#C(5 16)");
}

TEST(complex_printer_negative)
{
    ASSERT_STR_EQ(eval_print("#C(-3 7)"), "#C(-3 7)");
}

TEST(complex_eql)
{
    ASSERT_STR_EQ(eval_print("(eql #C(5 16) #C(5 16))"), "T");
}

TEST(complex_eql_different)
{
    ASSERT_STR_EQ(eval_print("(eql #C(5 16) #C(5 17))"), "NIL");
}

TEST(complex_equal)
{
    ASSERT_STR_EQ(eval_print("(equal #C(3 4) #C(3 4))"), "T");
}

TEST(complex_complexp)
{
    ASSERT_STR_EQ(eval_print("(complexp #C(5 16))"), "T");
}

TEST(complex_complexp_not)
{
    ASSERT_STR_EQ(eval_print("(complexp 42)"), "NIL");
}

TEST(complex_realpart)
{
    ASSERT_STR_EQ(eval_print("(realpart #C(5 16))"), "5");
}

TEST(complex_imagpart)
{
    ASSERT_STR_EQ(eval_print("(imagpart #C(5 16))"), "16");
}

TEST(complex_realpart_real)
{
    ASSERT_STR_EQ(eval_print("(realpart 42)"), "42");
}

TEST(complex_imagpart_real)
{
    ASSERT_STR_EQ(eval_print("(imagpart 42)"), "0");
}

TEST(complex_constructor)
{
    ASSERT_STR_EQ(eval_print("(complex 3 4)"), "#C(3 4)");
}

TEST(complex_constructor_zero_imag)
{
    /* (complex 5 0) returns just 5 per CL spec */
    ASSERT_STR_EQ(eval_print("(complex 5 0)"), "5");
}

TEST(complex_constructor_one_arg)
{
    ASSERT_STR_EQ(eval_print("(complex 7)"), "7");
}

TEST(complex_type_of)
{
    ASSERT_STR_EQ(eval_print("(type-of #C(1 2))"), "COMPLEX");
}

TEST(complex_typep)
{
    ASSERT_STR_EQ(eval_print("(typep #C(1 2) 'complex)"), "T");
}

TEST(complex_typep_number)
{
    ASSERT_STR_EQ(eval_print("(typep #C(1 2) 'number)"), "T");
}

TEST(complex_numberp)
{
    ASSERT_STR_EQ(eval_print("(numberp #C(1 2))"), "T");
}

TEST(complex_self_evaluating)
{
    CL_Obj c = eval("#C(5 16)");
    ASSERT(CL_COMPLEX_P(c));
}

TEST(complex_conjugate)
{
    ASSERT_STR_EQ(eval_print("(conjugate #C(3 4))"), "#C(3 -4)");
}

TEST(complex_conjugate_real)
{
    ASSERT_STR_EQ(eval_print("(conjugate 5)"), "5");
}

TEST(complex_hash_table)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((ht (make-hash-table :test 'eql))) "
        "  (setf (gethash #C(5 16) ht) 42) "
        "  (gethash #C(5 16) ht))"), "42");
}

TEST(complex_in_list)
{
    ASSERT_STR_EQ(eval_print("(car (list #C(1 2) #C(3 4)))"), "#C(1 2)");
}

TEST(complex_not_realp)
{
    /* Complex numbers are NOT real */
    ASSERT_STR_EQ(eval_print("(typep #C(1 2) 'real)"), "NIL");
}

/* --- Arithmetic on complex (regressions for numbers-chapter *.3 hang) --- */

TEST(complex_arith_add)
{
    ASSERT_STR_EQ(eval_print("(+ #C(1 2) #C(3 4))"), "#C(4 6)");
    ASSERT_STR_EQ(eval_print("(+ 1 #C(2 3))"), "#C(3 3)");
    ASSERT_STR_EQ(eval_print("(+ #C(2 3) 1)"), "#C(3 3)");
    /* Imag cancels to integer zero → canonicalize to real */
    ASSERT_STR_EQ(eval_print("(+ #C(1 2) #C(3 -2))"), "4");
}

TEST(complex_arith_sub)
{
    ASSERT_STR_EQ(eval_print("(- #C(5 7) #C(2 3))"), "#C(3 4)");
    ASSERT_STR_EQ(eval_print("(- 5 #C(2 3))"), "#C(3 -3)");
    ASSERT_STR_EQ(eval_print("(- #C(2 3) 2)"), "#C(0 3)");
    /* Identity: (- x x) = 0 even for complex */
    ASSERT_STR_EQ(eval_print("(- #C(1 2) #C(1 2))"), "0");
}

TEST(complex_arith_mul)
{
    /* (* x 1) must round-trip per CLHS — was the *.3 ANSI hang trigger */
    ASSERT_STR_EQ(eval_print("(* #C(1 2) 1)"), "#C(1 2)");
    ASSERT_STR_EQ(eval_print("(* 1 #C(1 2))"), "#C(1 2)");
    /* (a+bi)(c+di) */
    ASSERT_STR_EQ(eval_print("(* #C(1 2) #C(3 4))"), "#C(-5 10)");
    /* i² = -1 */
    ASSERT_STR_EQ(eval_print("(* #C(0 1) #C(0 1))"), "-1");
    /* (* x 0) collapses imag → 0 */
    ASSERT_STR_EQ(eval_print("(* #C(1 2) 0)"), "0");
}

TEST(complex_arith_div)
{
    /* Real / complex */
    ASSERT_STR_EQ(eval_print("(/ 2 #C(1 1))"), "#C(1 -1)");
    /* Complex / real */
    ASSERT_STR_EQ(eval_print("(/ #C(2 4) 2)"), "#C(1 2)");
    /* Complex / complex (rational) */
    ASSERT_STR_EQ(eval_print("(/ #C(3 4) #C(1 2))"), "#C(11/5 -2/5)");
    /* Identity: x / x = 1 (canonicalizes to real) */
    ASSERT_STR_EQ(eval_print("(/ #C(2 3) #C(2 3))"), "1");
}

TEST(complex_arith_negate)
{
    ASSERT_STR_EQ(eval_print("(- #C(1 2))"), "#C(-1 -2)");
    /* Single-arg `-` on real-valued complex via canonical: imag is 0 */
    ASSERT_STR_EQ(eval_print("(- (complex 5 0))"), "-5");
}

TEST(complex_arith_zerop)
{
    ASSERT_STR_EQ(eval_print("(zerop #C(0 0))"), "T");
    ASSERT_STR_EQ(eval_print("(zerop #C(0.0 0.0))"), "T");
    ASSERT_STR_EQ(eval_print("(zerop #C(1 0))"), "NIL");
    ASSERT_STR_EQ(eval_print("(zerop #C(0 1))"), "NIL");
}

TEST(complex_abs)
{
    /* Crashed before: cl_arith_abs treated complex header as bignum.
     * Pythagorean triple → exact integer. */
    ASSERT_STR_EQ(eval_print("(abs #C(3 4))"), "5");
    ASSERT_STR_EQ(eval_print("(abs #C(5 12))"), "13");
    /* Non-perfect-square sum → double. */
    ASSERT_STR_EQ(eval_print("(abs #C(1 2))"), "2.23606797749979d0");
    /* Float operands stay in their float kind. */
    ASSERT_STR_EQ(eval_print("(abs #C(3.0 4.0))"), "5.0");
}

TEST(complex_float_contagion)
{
    /* Mixing float with rational complex must promote both parts to float
     * per CLHS 12.1.5.3.1.  (+ 1.0 #C(1 2)) → real=2.0, imag was 2 (fixnum)
     * after the recursive add — the canonicalizer must coerce it to 2.0. */
    ASSERT_STR_EQ(eval_print("(+ 1.0 #C(1 2))"), "#C(2.0 2.0)");
    ASSERT_STR_EQ(eval_print("(* #C(1 2) 1.0d0)"), "#C(1.0d0 2.0d0)");
}

/* --- Main --- */

int main(void)
{
    setup();

    RUN(complex_reader);
    RUN(complex_reader_negative);
    RUN(complex_reader_float);
    RUN(complex_printer);
    RUN(complex_printer_negative);
    RUN(complex_eql);
    RUN(complex_eql_different);
    RUN(complex_equal);
    RUN(complex_complexp);
    RUN(complex_complexp_not);
    RUN(complex_realpart);
    RUN(complex_imagpart);
    RUN(complex_realpart_real);
    RUN(complex_imagpart_real);
    RUN(complex_constructor);
    RUN(complex_constructor_zero_imag);
    RUN(complex_constructor_one_arg);
    RUN(complex_type_of);
    RUN(complex_typep);
    RUN(complex_typep_number);
    RUN(complex_numberp);
    RUN(complex_self_evaluating);
    RUN(complex_conjugate);
    RUN(complex_conjugate_real);
    RUN(complex_hash_table);
    RUN(complex_in_list);
    RUN(complex_not_realp);

    RUN(complex_arith_add);
    RUN(complex_arith_sub);
    RUN(complex_arith_mul);
    RUN(complex_arith_div);
    RUN(complex_arith_negate);
    RUN(complex_arith_zerop);
    RUN(complex_abs);
    RUN(complex_float_contagion);

    REPORT();
    teardown();
    return 0;
}
