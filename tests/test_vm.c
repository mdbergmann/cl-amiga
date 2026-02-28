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

/* Helper: eval and get fixnum value */
static int eval_int(const char *str)
{
    CL_Obj result = cl_eval_string(str);
    return CL_FIXNUM_VAL(result);
}

/* --- Arithmetic --- */

TEST(eval_add)
{
    ASSERT_EQ_INT(eval_int("(+ 1 2)"), 3);
    ASSERT_EQ_INT(eval_int("(+ 1 2 3 4 5)"), 15);
    ASSERT_EQ_INT(eval_int("(+)"), 0);
}

TEST(eval_sub)
{
    ASSERT_EQ_INT(eval_int("(- 10 3)"), 7);
    ASSERT_EQ_INT(eval_int("(- 5)"), -5);
    ASSERT_EQ_INT(eval_int("(- 100 50 25)"), 25);
}

TEST(eval_mul)
{
    ASSERT_EQ_INT(eval_int("(* 3 4)"), 12);
    ASSERT_EQ_INT(eval_int("(* 2 3 4)"), 24);
    ASSERT_EQ_INT(eval_int("(*)"), 1);
}

TEST(eval_div)
{
    ASSERT_EQ_INT(eval_int("(/ 20 4)"), 5);
    ASSERT_EQ_INT(eval_int("(/ 100 2 5)"), 10);
}

TEST(eval_1plus_1minus)
{
    ASSERT_EQ_INT(eval_int("(1+ 41)"), 42);
    ASSERT_EQ_INT(eval_int("(1- 43)"), 42);
}

TEST(eval_mod)
{
    ASSERT_EQ_INT(eval_int("(mod 10 3)"), 1);
    ASSERT_EQ_INT(eval_int("(mod 9 3)"), 0);
}

/* --- Comparison --- */

TEST(eval_numeq)
{
    ASSERT_STR_EQ(eval_print("(= 5 5)"), "T");
    ASSERT_STR_EQ(eval_print("(= 5 6)"), "NIL");
}

TEST(eval_lt)
{
    ASSERT_STR_EQ(eval_print("(< 1 2)"), "T");
    ASSERT_STR_EQ(eval_print("(< 2 1)"), "NIL");
    ASSERT_STR_EQ(eval_print("(< 1 2 3)"), "T");
}

TEST(eval_gt)
{
    ASSERT_STR_EQ(eval_print("(> 3 2)"), "T");
    ASSERT_STR_EQ(eval_print("(> 3 2 1)"), "T");
    ASSERT_STR_EQ(eval_print("(> 1 2)"), "NIL");
}

TEST(eval_le_ge)
{
    ASSERT_STR_EQ(eval_print("(<= 1 1)"), "T");
    ASSERT_STR_EQ(eval_print("(<= 1 2)"), "T");
    ASSERT_STR_EQ(eval_print("(<= 2 1)"), "NIL");
    ASSERT_STR_EQ(eval_print("(>= 2 2)"), "T");
    ASSERT_STR_EQ(eval_print("(>= 2 1)"), "T");
}

/* --- Predicates --- */

TEST(eval_predicates)
{
    ASSERT_STR_EQ(eval_print("(null nil)"), "T");
    ASSERT_STR_EQ(eval_print("(null t)"), "NIL");
    ASSERT_STR_EQ(eval_print("(consp '(1))"), "T");
    ASSERT_STR_EQ(eval_print("(consp 42)"), "NIL");
    ASSERT_STR_EQ(eval_print("(atom 42)"), "T");
    ASSERT_STR_EQ(eval_print("(atom '(1))"), "NIL");
    ASSERT_STR_EQ(eval_print("(numberp 42)"), "T");
    ASSERT_STR_EQ(eval_print("(symbolp 'foo)"), "T");
    ASSERT_STR_EQ(eval_print("(stringp \"hi\")"), "T");
    ASSERT_STR_EQ(eval_print("(listp nil)"), "T");
    ASSERT_STR_EQ(eval_print("(listp '(1))"), "T");
}

TEST(eval_not)
{
    ASSERT_STR_EQ(eval_print("(not nil)"), "T");
    ASSERT_STR_EQ(eval_print("(not t)"), "NIL");
    ASSERT_STR_EQ(eval_print("(not 42)"), "NIL");
}

TEST(eval_eq_eql_equal)
{
    ASSERT_STR_EQ(eval_print("(eq 'a 'a)"), "T");
    ASSERT_STR_EQ(eval_print("(eql 42 42)"), "T");
    ASSERT_STR_EQ(eval_print("(equal '(1 2) '(1 2))"), "T");
    ASSERT_STR_EQ(eval_print("(equal '(1 2) '(1 3))"), "NIL");
}

/* --- List operations --- */

TEST(eval_cons_car_cdr)
{
    ASSERT_STR_EQ(eval_print("(cons 1 2)"), "(1 . 2)");
    ASSERT_STR_EQ(eval_print("(car '(a b c))"), "A");
    ASSERT_STR_EQ(eval_print("(cdr '(a b c))"), "(B C)");
}

TEST(eval_list)
{
    ASSERT_STR_EQ(eval_print("(list 1 2 3)"), "(1 2 3)");
    ASSERT_STR_EQ(eval_print("(list)"), "NIL");
}

TEST(eval_length)
{
    ASSERT_EQ_INT(eval_int("(length '(1 2 3))"), 3);
    ASSERT_EQ_INT(eval_int("(length nil)"), 0);
    ASSERT_EQ_INT(eval_int("(length \"hello\")"), 5);
}

TEST(eval_append)
{
    ASSERT_STR_EQ(eval_print("(append '(1 2) '(3 4))"), "(1 2 3 4)");
    ASSERT_STR_EQ(eval_print("(append nil '(1))"), "(1)");
}

TEST(eval_reverse)
{
    ASSERT_STR_EQ(eval_print("(reverse '(1 2 3))"), "(3 2 1)");
    ASSERT_STR_EQ(eval_print("(reverse nil)"), "NIL");
}

TEST(eval_nth)
{
    ASSERT_STR_EQ(eval_print("(nth 0 '(a b c))"), "A");
    ASSERT_STR_EQ(eval_print("(nth 1 '(a b c))"), "B");
    ASSERT_STR_EQ(eval_print("(nth 2 '(a b c))"), "C");
}

TEST(eval_misc_math)
{
    ASSERT_STR_EQ(eval_print("(zerop 0)"), "T");
    ASSERT_STR_EQ(eval_print("(zerop 1)"), "NIL");
    ASSERT_STR_EQ(eval_print("(plusp 5)"), "T");
    ASSERT_STR_EQ(eval_print("(minusp -3)"), "T");
    ASSERT_EQ_INT(eval_int("(abs -5)"), 5);
    ASSERT_EQ_INT(eval_int("(max 1 5 3)"), 5);
    ASSERT_EQ_INT(eval_int("(min 1 5 3)"), 1);
}

/* --- Special forms --- */

TEST(eval_quote)
{
    ASSERT_STR_EQ(eval_print("'foo"), "FOO");
    ASSERT_STR_EQ(eval_print("'(1 2 3)"), "(1 2 3)");
    ASSERT_STR_EQ(eval_print("(quote hello)"), "HELLO");
}

TEST(eval_if)
{
    ASSERT_STR_EQ(eval_print("(if t 'yes 'no)"), "YES");
    ASSERT_STR_EQ(eval_print("(if nil 'yes 'no)"), "NO");
    ASSERT_STR_EQ(eval_print("(if nil 'yes)"), "NIL");
    ASSERT_STR_EQ(eval_print("(if 42 'yes 'no)"), "YES");
}

TEST(eval_progn)
{
    ASSERT_EQ_INT(eval_int("(progn 1 2 3)"), 3);
    ASSERT_STR_EQ(eval_print("(progn)"), "NIL");
}

TEST(eval_setq)
{
    eval_print("(setq test-var 99)");
    ASSERT_EQ_INT(eval_int("test-var"), 99);
}

TEST(eval_let)
{
    ASSERT_EQ_INT(eval_int("(let ((x 10) (y 20)) (+ x y))"), 30);
    ASSERT_EQ_INT(eval_int("(let ((a 1) (b 2) (c 3)) (+ a b c))"), 6);
}

TEST(eval_letstar)
{
    ASSERT_EQ_INT(eval_int("(let* ((x 10) (y (* x 2))) y)"), 20);
    ASSERT_EQ_INT(eval_int("(let* ((a 1) (b (+ a 1)) (c (+ b 1))) c)"), 3);
}

/* --- Functions --- */

TEST(eval_defun_basic)
{
    eval_print("(defun double (x) (* x 2))");
    ASSERT_EQ_INT(eval_int("(double 21)"), 42);
}

TEST(eval_defun_recursive)
{
    eval_print("(defun fact (n) (if (<= n 1) 1 (* n (fact (1- n)))))");
    ASSERT_EQ_INT(eval_int("(fact 0)"), 1);
    ASSERT_EQ_INT(eval_int("(fact 1)"), 1);
    ASSERT_EQ_INT(eval_int("(fact 5)"), 120);
    ASSERT_EQ_INT(eval_int("(fact 10)"), 3628800);
}

TEST(eval_defun_fibonacci)
{
    eval_print("(defun fib (n) (if (<= n 1) n (+ (fib (- n 1)) (fib (- n 2)))))");
    ASSERT_EQ_INT(eval_int("(fib 0)"), 0);
    ASSERT_EQ_INT(eval_int("(fib 1)"), 1);
    ASSERT_EQ_INT(eval_int("(fib 10)"), 55);
}

TEST(eval_defun_multiarg)
{
    eval_print("(defun add3 (a b c) (+ a b c))");
    ASSERT_EQ_INT(eval_int("(add3 1 2 3)"), 6);
}

TEST(eval_lambda)
{
    ASSERT_EQ_INT(eval_int("((lambda (x) (* x x)) 5)"), 25);
}

TEST(eval_function_ref)
{
    ASSERT_STR_EQ(eval_print("(mapcar #'1+ '(1 2 3))"), "(2 3 4)");
}

/* --- Error handling --- */

TEST(eval_error_unbound)
{
    const char *r = eval_print("nonexistent-var-xyz");
    ASSERT(strstr(r, "ERROR") != NULL);
}

TEST(eval_error_divzero)
{
    const char *r = eval_print("(/ 1 0)");
    ASSERT(strstr(r, "ERROR") != NULL);
}

TEST(eval_error_type)
{
    const char *r = eval_print("(+ 1 'a)");
    ASSERT(strstr(r, "ERROR") != NULL);
}

/* --- Self-evaluating --- */

TEST(eval_self_evaluating)
{
    ASSERT_EQ_INT(eval_int("42"), 42);
    ASSERT_STR_EQ(eval_print("t"), "T");
    ASSERT_STR_EQ(eval_print("nil"), "NIL");
    ASSERT_STR_EQ(eval_print("\"hello\""), "\"hello\"");
}

TEST(eval_keyword)
{
    ASSERT_STR_EQ(eval_print(":test"), ":TEST");
}

int main(void)
{
    test_init();
    setup();

    RUN(eval_add);
    RUN(eval_sub);
    RUN(eval_mul);
    RUN(eval_div);
    RUN(eval_1plus_1minus);
    RUN(eval_mod);
    RUN(eval_numeq);
    RUN(eval_lt);
    RUN(eval_gt);
    RUN(eval_le_ge);
    RUN(eval_predicates);
    RUN(eval_not);
    RUN(eval_eq_eql_equal);
    RUN(eval_cons_car_cdr);
    RUN(eval_list);
    RUN(eval_length);
    RUN(eval_append);
    RUN(eval_reverse);
    RUN(eval_nth);
    RUN(eval_misc_math);
    RUN(eval_quote);
    RUN(eval_if);
    RUN(eval_progn);
    RUN(eval_setq);
    RUN(eval_let);
    RUN(eval_letstar);
    RUN(eval_defun_basic);
    RUN(eval_defun_recursive);
    RUN(eval_defun_fibonacci);
    RUN(eval_defun_multiarg);
    RUN(eval_lambda);
    RUN(eval_function_ref);
    RUN(eval_error_unbound);
    RUN(eval_error_divzero);
    RUN(eval_error_type);
    RUN(eval_self_evaluating);
    RUN(eval_keyword);

    teardown();
    REPORT();
}
