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

/* --- Macros --- */

TEST(eval_defmacro_simple)
{
    /* Macro that always returns a constant form */
    eval_print("(defmacro always-42 () 42)");
    ASSERT_EQ_INT(eval_int("(always-42)"), 42);
}

TEST(eval_defmacro_with_args)
{
    /* Macro that builds (+ x 1) using list */
    eval_print("(defmacro inc (x) (list '+ x 1))");
    ASSERT_EQ_INT(eval_int("(inc 5)"), 6);
    ASSERT_EQ_INT(eval_int("(inc 99)"), 100);
}

TEST(eval_defmacro_when)
{
    /* when macro: (when test body...) => (if test (progn body...)) */
    eval_print("(defmacro my-when (test &rest body) (list 'if test (cons 'progn body)))");
    ASSERT_EQ_INT(eval_int("(my-when t 1 2 42)"), 42);
    ASSERT_STR_EQ(eval_print("(my-when nil 1 2 42)"), "NIL");
}

TEST(eval_defmacro_unless)
{
    /* unless macro: (unless test body...) => (if test nil (progn body...)) */
    eval_print("(defmacro my-unless (test &rest body) (list 'if test nil (cons 'progn body)))");
    ASSERT_EQ_INT(eval_int("(my-unless nil 1 2 99)"), 99);
    ASSERT_STR_EQ(eval_print("(my-unless t 1 2 99)"), "NIL");
}

TEST(eval_defmacro_identity)
{
    /* Macro receives unevaluated forms — verify by quoting the arg */
    eval_print("(defmacro quote-it (x) (list 'quote x))");
    ASSERT_STR_EQ(eval_print("(quote-it (+ 1 2))"), "(+ 1 2)");
    ASSERT_STR_EQ(eval_print("(quote-it hello)"), "HELLO");
}

/* --- Closures with captured upvalues --- */

TEST(eval_closure_capture_let)
{
    /* Lambda captures variable from let binding */
    ASSERT_EQ_INT(eval_int("(let ((x 10)) ((lambda (y) (+ x y)) 5))"), 15);
    ASSERT_EQ_INT(eval_int("(let ((a 100)) ((lambda () a)))"), 100);
}

TEST(eval_closure_make_adder)
{
    /* Classic make-adder: defun returns closure capturing n */
    eval_print("(defun make-adder (n) (lambda (x) (+ n x)))");
    ASSERT_EQ_INT(eval_int("((make-adder 10) 5)"), 15);
    ASSERT_EQ_INT(eval_int("((make-adder 100) 42)"), 142);
}

TEST(eval_closure_multiple_captures)
{
    /* Capture multiple variables */
    ASSERT_EQ_INT(eval_int("(let ((a 1) (b 2) (c 3)) ((lambda () (+ a b c))))"), 6);
    ASSERT_EQ_INT(eval_int("(let ((x 10) (y 20)) ((lambda (z) (+ x y z)) 30))"), 60);
}

TEST(eval_closure_nested)
{
    /* Nested lambda: inner captures from outer closure's upvalue */
    eval_print("(defun make-adder2 (n) (lambda (x) ((lambda (a b) (+ a b)) n x)))");
    ASSERT_EQ_INT(eval_int("((make-adder2 10) 5)"), 15);

    /* Double-nesting: outer captures from let, inner captures from outer */
    ASSERT_EQ_INT(eval_int("(let ((x 10)) ((lambda () ((lambda () (+ x 5))))))"), 15);
}

TEST(eval_closure_shared_scope)
{
    /* Two lambdas capture the same variable (value capture = independent copies) */
    eval_print("(defun make-pair (x) (list (lambda () x) (lambda () (+ x 1))))");
    ASSERT_EQ_INT(eval_int("(let ((p (make-pair 10))) ((car p)))"), 10);
    ASSERT_EQ_INT(eval_int("(let ((p (make-pair 10))) ((car (cdr p))))"), 11);
}

/* --- AND / OR / COND --- */

TEST(eval_and)
{
    /* (and) => T */
    ASSERT_STR_EQ(eval_print("(and)"), "T");
    /* (and x) => x */
    ASSERT_EQ_INT(eval_int("(and 42)"), 42);
    /* Short-circuit: returns first nil */
    ASSERT_STR_EQ(eval_print("(and 1 nil 2)"), "NIL");
    /* All truthy: returns last value */
    ASSERT_EQ_INT(eval_int("(and 1 2 3)"), 3);
    /* First is nil */
    ASSERT_STR_EQ(eval_print("(and nil 1)"), "NIL");
}

TEST(eval_or)
{
    /* (or) => NIL */
    ASSERT_STR_EQ(eval_print("(or)"), "NIL");
    /* (or x) => x */
    ASSERT_EQ_INT(eval_int("(or 42)"), 42);
    /* Short-circuit: returns first truthy */
    ASSERT_EQ_INT(eval_int("(or nil 2 3)"), 2);
    /* All nil: returns nil */
    ASSERT_STR_EQ(eval_print("(or nil nil nil)"), "NIL");
    /* First is truthy */
    ASSERT_EQ_INT(eval_int("(or 1 2)"), 1);
}

TEST(eval_cond)
{
    /* Basic clause matching */
    ASSERT_EQ_INT(eval_int("(cond (t 42))"), 42);
    ASSERT_EQ_INT(eval_int("(cond (nil 1) (t 2))"), 2);
    /* No matching clause */
    ASSERT_STR_EQ(eval_print("(cond (nil 1))"), "NIL");
    /* Multiple body forms */
    ASSERT_EQ_INT(eval_int("(cond (t 1 2 3))"), 3);
    /* Condition with expression */
    ASSERT_EQ_INT(eval_int("(cond ((= 1 2) 10) ((= 1 1) 20))"), 20);
    /* Empty cond */
    ASSERT_STR_EQ(eval_print("(cond)"), "NIL");
}

TEST(eval_when_unless)
{
    /* when: true => body result */
    ASSERT_EQ_INT(eval_int("(when t 1 2 42)"), 42);
    /* when: nil => NIL */
    ASSERT_STR_EQ(eval_print("(when nil 1 2 42)"), "NIL");
    /* unless: nil => body result */
    ASSERT_EQ_INT(eval_int("(unless nil 1 2 99)"), 99);
    /* unless: true => NIL */
    ASSERT_STR_EQ(eval_print("(unless t 1 2 99)"), "NIL");
}

/* --- mapcar / funcall / apply with compiled functions --- */

TEST(eval_mapcar_lambda)
{
    /* mapcar with lambda */
    ASSERT_STR_EQ(eval_print("(mapcar (lambda (x) (* x x)) '(1 2 3))"), "(1 4 9)");
    /* mapcar with closure */
    eval_print("(defun make-multiplier (n) (lambda (x) (* n x)))");
    ASSERT_STR_EQ(eval_print("(mapcar (make-multiplier 3) '(1 2 3))"), "(3 6 9)");
}

TEST(eval_funcall_lambda)
{
    /* funcall with lambda */
    ASSERT_EQ_INT(eval_int("(funcall (lambda (a b) (+ a b)) 10 20)"), 30);
    /* funcall with closure */
    ASSERT_EQ_INT(eval_int("(funcall (make-adder 100) 5)"), 105);
}

TEST(eval_apply_closure)
{
    /* apply with lambda */
    ASSERT_EQ_INT(eval_int("(apply (lambda (a b) (+ a b)) '(3 4))"), 7);
    /* apply with closure */
    ASSERT_EQ_INT(eval_int("(apply (make-adder 50) '(7))"), 57);
}

/* --- Loop forms: dolist, dotimes, do --- */

TEST(eval_dolist)
{
    /* Basic iteration — collect via side effect */
    eval_print("(setq *result* nil)");
    eval_print("(dolist (x '(1 2 3)) (setq *result* (cons x *result*)))");
    ASSERT_STR_EQ(eval_print("*result*"), "(3 2 1)");

    /* Empty list — body never executes */
    eval_print("(setq *count* 0)");
    eval_print("(dolist (x nil) (setq *count* (+ *count* 1)))");
    ASSERT_EQ_INT(eval_int("*count*"), 0);

    /* Result form */
    ASSERT_EQ_INT(eval_int("(dolist (x '(1 2 3) 42))"), 42);

    /* Default return is NIL */
    ASSERT_STR_EQ(eval_print("(dolist (x '(1 2 3)))"), "NIL");

    /* var is NIL during result-form */
    ASSERT_STR_EQ(eval_print("(dolist (x '(1 2 3) x))"), "NIL");
}

TEST(eval_dotimes)
{
    /* Count 0..n-1 */
    eval_print("(setq *sum* 0)");
    eval_print("(dotimes (i 5) (setq *sum* (+ *sum* i)))");
    ASSERT_EQ_INT(eval_int("*sum*"), 10);  /* 0+1+2+3+4 */

    /* Zero count — body never executes */
    eval_print("(setq *count* 0)");
    eval_print("(dotimes (i 0) (setq *count* (+ *count* 1)))");
    ASSERT_EQ_INT(eval_int("*count*"), 0);

    /* Result form */
    ASSERT_EQ_INT(eval_int("(dotimes (i 3 99))"), 99);

    /* Default return is NIL */
    ASSERT_STR_EQ(eval_print("(dotimes (i 3))"), "NIL");

    /* var accessible in result-form (equals count at end) */
    ASSERT_EQ_INT(eval_int("(dotimes (i 5 i))"), 5);
}

TEST(eval_do)
{
    /* Simple countdown */
    ASSERT_EQ_INT(eval_int(
        "(do ((i 10 (- i 1))) ((= i 0) 42))"), 42);

    /* Multiple vars with steps */
    ASSERT_EQ_INT(eval_int(
        "(do ((i 0 (+ i 1)) (sum 0 (+ sum i))) ((= i 5) sum))"), 10);

    /* Var without step — retains value */
    ASSERT_EQ_INT(eval_int(
        "(do ((x 100) (i 0 (+ i 1))) ((= i 3) x))"), 100);

    /* Body forms execute */
    eval_print("(setq *do-body* 0)");
    eval_print("(do ((i 0 (+ i 1))) ((= i 3)) (setq *do-body* (+ *do-body* 1)))");
    ASSERT_EQ_INT(eval_int("*do-body*"), 3);

    /* Multiple result forms (progn) */
    ASSERT_EQ_INT(eval_int(
        "(do ((i 0 (+ i 1))) ((= i 1) 10 20 30))"), 30);

    /* No result form — returns NIL */
    ASSERT_STR_EQ(eval_print(
        "(do ((i 0 (+ i 1))) ((= i 1)))"), "NIL");
}

/* --- Quasiquote --- */

TEST(eval_quasiquote_atom)
{
    ASSERT_EQ_INT(eval_int("`42"), 42);
    ASSERT_STR_EQ(eval_print("`foo"), "FOO");
}

TEST(eval_quasiquote_simple_list)
{
    ASSERT_STR_EQ(eval_print("`(1 2 3)"), "(1 2 3)");
    ASSERT_STR_EQ(eval_print("`(a b c)"), "(A B C)");
}

TEST(eval_quasiquote_unquote)
{
    ASSERT_STR_EQ(eval_print("`(a ,(+ 1 2) c)"), "(A 3 C)");
    ASSERT_STR_EQ(eval_print("(let ((x 10)) `(val ,x))"), "(VAL 10)");
}

TEST(eval_quasiquote_splicing)
{
    ASSERT_STR_EQ(eval_print("`(a ,@'(1 2 3) b)"), "(A 1 2 3 B)");
    ASSERT_STR_EQ(eval_print("`(,@'(1 2) ,@'(3 4))"), "(1 2 3 4)");
    ASSERT_STR_EQ(eval_print("`(a ,@'(1 2))"), "(A 1 2)");
}

TEST(eval_quasiquote_nested_list)
{
    ASSERT_STR_EQ(eval_print("`(a (b ,(+ 1 2)))"), "(A (B 3))");
}

TEST(eval_quasiquote_dotted)
{
    ASSERT_STR_EQ(eval_print("`(a . ,(+ 1 2))"), "(A . 3)");
}

TEST(eval_quasiquote_in_macro)
{
    eval_print("(defmacro my-inc2 (x) `(+ ,x 1))");
    ASSERT_EQ_INT(eval_int("(my-inc2 5)"), 6);
    ASSERT_EQ_INT(eval_int("(my-inc2 99)"), 100);
}

TEST(eval_quasiquote_macro_splice)
{
    eval_print("(defmacro my-progn (&rest body) `(progn ,@body))");
    ASSERT_EQ_INT(eval_int("(my-progn 1 2 42)"), 42);
}

/* --- Gensym --- */

TEST(eval_gensym)
{
    CL_Obj g1, g2;

    /* gensym returns a symbol */
    ASSERT_STR_EQ(eval_print("(symbolp (gensym))"), "T");

    /* Two gensyms are distinct */
    g1 = cl_eval_string("(gensym)");
    g2 = cl_eval_string("(gensym)");
    ASSERT(g1 != g2);
}

/* --- Load --- */

TEST(eval_load)
{
    eval_print("(load \"tests/test_load.lisp\")");
    ASSERT_EQ_INT(eval_int("*load-test-var*"), 42);
    ASSERT_EQ_INT(eval_int("(load-test-fn 5)"), 105);
}

/* --- Boot file functions --- */

TEST(eval_boot_functions)
{
    ASSERT_EQ_INT(eval_int("(cadr '(1 2 3))"), 2);
    ASSERT_STR_EQ(eval_print("(caar '((a b) c))"), "A");
    ASSERT_STR_EQ(eval_print("(cdar '((a b) c))"), "(B)");
    ASSERT_STR_EQ(eval_print("(cddr '(1 2 3))"), "(3)");
    ASSERT_EQ_INT(eval_int("(caddr '(1 2 3))"), 3);
    ASSERT_EQ_INT(eval_int("(identity 42)"), 42);
    ASSERT_STR_EQ(eval_print("(endp nil)"), "T");
    ASSERT_STR_EQ(eval_print("(endp '(1))"), "NIL");
}

/* --- Phase 4: return / return-from --- */

TEST(eval_block_return)
{
    ASSERT_EQ_INT(eval_int("(block nil 1 (return 42) 3)"), 42);
    ASSERT_EQ_INT(eval_int("(block foo (return-from foo 42))"), 42);
    ASSERT_EQ_INT(eval_int("(block nil 1 2 3)"), 3);
    ASSERT_STR_EQ(eval_print("(block nil (return nil))"), "NIL");
}

TEST(eval_return_in_do)
{
    ASSERT_EQ_INT(eval_int(
        "(do ((i 0 (1+ i))) ((= i 10) 99) (if (= i 5) (return 42)))"), 42);
}

TEST(eval_return_in_dolist)
{
    ASSERT_EQ_INT(eval_int(
        "(dolist (x '(1 3 4 7)) (if (= 0 (mod x 2)) (return x)))"), 4);
}

TEST(eval_return_in_dotimes)
{
    ASSERT_EQ_INT(eval_int(
        "(dotimes (i 100) (if (= i 7) (return i)))"), 7);
}

/* --- Phase 4: prog1 / prog2 --- */

TEST(eval_prog1)
{
    ASSERT_EQ_INT(eval_int("(prog1 1 2 3)"), 1);
    ASSERT_EQ_INT(eval_int("(prog1 42)"), 42);
}

TEST(eval_prog2)
{
    ASSERT_EQ_INT(eval_int("(prog2 1 2 3)"), 2);
    ASSERT_EQ_INT(eval_int("(prog2 1 42)"), 42);
}

/* --- Phase 4: case / ecase --- */

TEST(eval_case)
{
    ASSERT_STR_EQ(eval_print("(case 2 (1 'a) (2 'b) (3 'c))"), "B");
    ASSERT_STR_EQ(eval_print("(case 5 ((1 2) 'low) ((3 4) 'high) (t 'other))"), "OTHER");
    ASSERT_STR_EQ(eval_print("(case 99 (1 'a))"), "NIL");
    ASSERT_STR_EQ(eval_print("(ecase 2 (1 'a) (2 'b))"), "B");
}

TEST(eval_typecase)
{
    ASSERT_STR_EQ(eval_print("(typecase 42 (integer 'num) (string 'str))"), "NUM");
    ASSERT_STR_EQ(eval_print("(typecase \"hi\" (integer 'num) (string 'str))"), "STR");
    ASSERT_STR_EQ(eval_print("(typecase 42 (string 'str) (t 'other))"), "OTHER");
}

/* --- Phase 4: flet / labels --- */

TEST(eval_flet)
{
    ASSERT_EQ_INT(eval_int("(flet ((f (x) (+ x 1))) (f 3))"), 4);
    /* flet with closure capture from enclosing let */
    ASSERT_EQ_INT(eval_int("(let ((x 10)) (flet ((f (y) (+ x y))) (f 5)))"), 15);
}

TEST(eval_labels)
{
    /* Use unique names to avoid collision with earlier defun tests */
    ASSERT_EQ_INT(eval_int(
        "(labels ((lab-fact (n) (if (<= n 1) 1 (* n (lab-fact (- n 1)))))) (lab-fact 5))"), 120);
    /* Mutual recursion */
    ASSERT_STR_EQ(eval_print(
        "(labels ((lab-even (n) (if (= n 0) t (lab-odd (- n 1)))) "
        "(lab-odd (n) (if (= n 0) nil (lab-even (- n 1))))) "
        "(lab-even 4))"), "T");
}

/* --- Phase 4: &optional / &key --- */

TEST(eval_optional)
{
    eval_print("(defun opt-test (a &optional b) (list a b))");
    ASSERT_STR_EQ(eval_print("(opt-test 1)"), "(1 NIL)");
    ASSERT_STR_EQ(eval_print("(opt-test 1 2)"), "(1 2)");
}

TEST(eval_optional_default)
{
    eval_print("(defun opt-def (a &optional (b 10)) (+ a b))");
    ASSERT_EQ_INT(eval_int("(opt-def 1)"), 11);
    ASSERT_EQ_INT(eval_int("(opt-def 1 2)"), 3);
}

TEST(eval_key)
{
    eval_print("(defun key-test (&key x y) (list x y))");
    ASSERT_STR_EQ(eval_print("(key-test :x 1 :y 2)"), "(1 2)");
    ASSERT_STR_EQ(eval_print("(key-test)"), "(NIL NIL)");
    ASSERT_STR_EQ(eval_print("(key-test :y 5)"), "(NIL 5)");
}

TEST(eval_key_default)
{
    eval_print("(defun key-def (&key (x 0) (y 10)) (+ x y))");
    ASSERT_EQ_INT(eval_int("(key-def :x 5)"), 15);
    ASSERT_EQ_INT(eval_int("(key-def)"), 10);
}

TEST(eval_optional_lambda)
{
    ASSERT_EQ_INT(eval_int("((lambda (a &optional (b 5)) (+ a b)) 10)"), 15);
    ASSERT_EQ_INT(eval_int("((lambda (a &optional (b 5)) (+ a b)) 10 20)"), 30);
}

/* --- Phase 4: &allow-other-keys --- */

TEST(eval_key_unknown_error)
{
    /* Unknown keyword should signal an error */
    const char *r = eval_print("(key-test :x 1 :z 99)");
    ASSERT(strstr(r, "ERROR") != NULL);
}

TEST(eval_key_allow_other_keys)
{
    /* &allow-other-keys in lambda list suppresses error */
    eval_print("(defun aok-test (&key x &allow-other-keys) x)");
    ASSERT_EQ_INT(eval_int("(aok-test :x 5 :z 99)"), 5);
    ASSERT_EQ_INT(eval_int("(aok-test :z 99)"), 0);  /* x defaults to NIL=0 as fixnum... */
    ASSERT_STR_EQ(eval_print("(aok-test :z 99)"), "NIL");
}

TEST(eval_key_caller_allow)
{
    /* Caller passes :allow-other-keys t to suppress error */
    ASSERT_STR_EQ(eval_print("(key-test :x 1 :z 99 :allow-other-keys t)"), "(1 NIL)");
}

/* --- Phase 4: eval builtin --- */

TEST(eval_eval_builtin)
{
    ASSERT_EQ_INT(eval_int("(eval '(+ 1 2))"), 3);
    ASSERT_EQ_INT(eval_int("(eval '(* 6 7))"), 42);
    ASSERT_STR_EQ(eval_print("(eval ''foo)"), "FOO");
}

/* --- Phase 4: macroexpand-1 / macroexpand --- */

TEST(eval_macroexpand_1)
{
    eval_print("(defmacro me-test (x) `(+ ,x 1))");
    /* macroexpand-1 on a macro call returns expanded form */
    ASSERT_STR_EQ(eval_print("(macroexpand-1 '(me-test 5))"), "(+ 5 1)");
    /* macroexpand-1 on a non-macro form returns it unchanged */
    ASSERT_STR_EQ(eval_print("(macroexpand-1 '(+ 1 2))"), "(+ 1 2)");
}

TEST(eval_macroexpand)
{
    /* macroexpand expands until no more macros at top level */
    eval_print("(defmacro me-wrap (x) `(me-test ,x))");
    ASSERT_STR_EQ(eval_print("(macroexpand '(me-wrap 5))"), "(+ 5 1)");
    /* Non-macro form returned unchanged */
    ASSERT_STR_EQ(eval_print("(macroexpand '(+ 1 2))"), "(+ 1 2)");
}

/* --- Phase 4 Tier 2: tagbody/go --- */

TEST(eval_tagbody_basic)
{
    /* tagbody with sequential tags and statements returns NIL */
    ASSERT_STR_EQ(eval_print("(let ((x 0)) (tagbody (setq x 1) (setq x (+ x 1))) x)"), "2");
}

TEST(eval_tagbody_forward_go)
{
    /* go jumps forward, skipping intermediate code */
    ASSERT_EQ_INT(eval_int("(let ((x 0)) (tagbody (go end) (setq x 99) end) x)"), 0);
}

TEST(eval_tagbody_backward_go)
{
    /* go jumps backward to implement a loop */
    ASSERT_EQ_INT(eval_int(
        "(let ((x 0)) (tagbody loop (setq x (+ x 1)) (if (< x 5) (go loop))) x)"), 5);
}

TEST(eval_tagbody_nested)
{
    /* nested tagbody */
    ASSERT_EQ_INT(eval_int(
        "(let ((x 0)) (tagbody outer (setq x (+ x 1)) (tagbody (setq x (+ x 10)) (go done)) done) x)"), 11);
}

TEST(eval_tagbody_fixnum_tags)
{
    /* integer tags are valid */
    ASSERT_EQ_INT(eval_int(
        "(let ((x 0)) (tagbody (go 2) 1 (setq x 99) 2 (setq x 42)) x)"), 42);
}

TEST(eval_tagbody_go_from_if)
{
    /* go from inside if */
    ASSERT_EQ_INT(eval_int(
        "(let ((x 0)) (tagbody start (setq x (+ x 1)) (if (< x 3) (go start))) x)"), 3);
}

/* --- Phase 4 Tier 2: catch/throw --- */

TEST(eval_catch_basic)
{
    /* Basic catch/throw */
    ASSERT_EQ_INT(eval_int("(catch 'done (throw 'done 42))"), 42);
}

TEST(eval_catch_normal)
{
    /* Normal return (no throw) */
    ASSERT_EQ_INT(eval_int("(catch 'done (+ 1 2))"), 3);
}

TEST(eval_catch_throw_across_call)
{
    /* throw across function calls */
    eval_print("(defun throw-helper () (throw 'bail 99))");
    ASSERT_EQ_INT(eval_int("(catch 'bail (+ 1 (throw-helper)))"), 99);
}

TEST(eval_catch_nested)
{
    /* nested catches — inner catches first */
    ASSERT_EQ_INT(eval_int(
        "(catch 'outer (+ 10 (catch 'inner (throw 'inner 5))))"), 15);
}

TEST(eval_catch_nested_outer)
{
    /* nested catches — throw to outer */
    ASSERT_EQ_INT(eval_int(
        "(catch 'outer (catch 'inner (throw 'outer 42)))"), 42);
}

TEST(eval_catch_no_value)
{
    /* throw with no value defaults to NIL */
    ASSERT_STR_EQ(eval_print("(catch 'done (throw 'done))"), "NIL");
}

TEST(eval_throw_unmatched)
{
    /* Unmatched throw signals error */
    ASSERT_STR_EQ(eval_print("(catch 'a (throw 'b 1))"), "ERROR:1");
}

/* --- Phase 4 Tier 2: unwind-protect --- */

TEST(eval_uwp_normal)
{
    /* Normal exit runs cleanup, returns protected form value */
    ASSERT_STR_EQ(eval_print(
        "(let ((log nil)) (let ((r (unwind-protect 42 (setq log t)))) (list r log)))"),
        "(42 T)");
}

TEST(eval_uwp_throw_cleanup)
{
    /* throw through UWP runs cleanup */
    ASSERT_STR_EQ(eval_print(
        "(let ((cleanup nil)) (catch 'done (unwind-protect (throw 'done 1) (setq cleanup t))) cleanup)"),
        "T");
}

TEST(eval_uwp_throw_value)
{
    /* throw through UWP delivers correct value */
    ASSERT_EQ_INT(eval_int(
        "(catch 'done (unwind-protect (throw 'done 42) (+ 1 2)))"), 42);
}

TEST(eval_uwp_nested)
{
    /* nested UWP: both cleanups run */
    ASSERT_STR_EQ(eval_print(
        "(let ((log nil)) (catch 'done (unwind-protect (unwind-protect (throw 'done 1) (setq log (cons 'inner log))) (setq log (cons 'outer log)))) log)"),
        "(OUTER INNER)");
}

TEST(eval_uwp_error_cleanup)
{
    /* error through UWP runs cleanup (checked via global side-effect,
       since error propagates past catch and aborts the expression) */
    eval_print("(setq *uwp-flag* nil)");
    eval_print("(unwind-protect (error \"boom\") (setq *uwp-flag* t))");
    ASSERT_STR_EQ(eval_print("*uwp-flag*"), "T");
}

/* --- Phase 4 Tier 2: Multiple Values --- */

TEST(eval_values_basic)
{
    /* (values) returns NIL */
    ASSERT_STR_EQ(eval_print("(values)"), "NIL");
    /* (values 1) returns 1 */
    ASSERT_EQ_INT(eval_int("(values 1)"), 1);
    /* (values 1 2 3) returns primary = 1 */
    ASSERT_EQ_INT(eval_int("(values 1 2 3)"), 1);
}

TEST(eval_multiple_value_bind)
{
    /* Basic MVB */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (a b c) (values 1 2 3) (list a b c))"), "(1 2 3)");
    /* More vars than values — extras are NIL */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (a b c) (values 1) (list a b c))"), "(1 NIL NIL)");
    /* Fewer vars than values — extras ignored */
    ASSERT_EQ_INT(eval_int(
        "(multiple-value-bind (a) (values 10 20 30) a)"), 10);
}

TEST(eval_multiple_value_list)
{
    ASSERT_STR_EQ(eval_print("(multiple-value-list (values 1 2 3))"), "(1 2 3)");
    ASSERT_STR_EQ(eval_print("(multiple-value-list (values))"), "NIL");
    ASSERT_STR_EQ(eval_print("(multiple-value-list (+ 1 2))"), "(3)");
}

TEST(eval_multiple_value_prog1)
{
    /* Preserves all values of first form */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-list (multiple-value-prog1 (values 1 2 3) (+ 4 5)))"), "(1 2 3)");
}

TEST(eval_nth_value)
{
    ASSERT_EQ_INT(eval_int("(nth-value 0 (values 10 20 30))"), 10);
    ASSERT_EQ_INT(eval_int("(nth-value 1 (values 10 20 30))"), 20);
    ASSERT_EQ_INT(eval_int("(nth-value 2 (values 10 20 30))"), 30);
    /* Out of range returns NIL */
    ASSERT_STR_EQ(eval_print("(nth-value 5 (values 1 2 3))"), "NIL");
}

TEST(eval_values_list)
{
    /* values-list creates multiple values from a list */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-list (values-list '(1 2 3)))"), "(1 2 3)");
    ASSERT_EQ_INT(eval_int("(values-list '(42))"), 42);
}

TEST(eval_mv_propagation)
{
    /* MV propagates through progn (last form's values) */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-list (progn 1 (values 2 3 4)))"), "(2 3 4)");
    /* MV propagates through if */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-list (if t (values 1 2)))"), "(1 2)");
    /* MV propagates through let body */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-list (let ((x 1)) (values x 2 3)))"), "(1 2 3)");
    /* Non-MV form yields single value */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-list 42)"), "(42)");
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
    RUN(eval_defmacro_simple);
    RUN(eval_defmacro_with_args);
    RUN(eval_defmacro_when);
    RUN(eval_defmacro_unless);
    RUN(eval_defmacro_identity);
    RUN(eval_closure_capture_let);
    RUN(eval_closure_make_adder);
    RUN(eval_closure_multiple_captures);
    RUN(eval_closure_nested);
    RUN(eval_closure_shared_scope);
    RUN(eval_and);
    RUN(eval_or);
    RUN(eval_cond);
    RUN(eval_when_unless);
    RUN(eval_mapcar_lambda);
    RUN(eval_funcall_lambda);
    RUN(eval_apply_closure);
    RUN(eval_dolist);
    RUN(eval_dotimes);
    RUN(eval_do);
    RUN(eval_quasiquote_atom);
    RUN(eval_quasiquote_simple_list);
    RUN(eval_quasiquote_unquote);
    RUN(eval_quasiquote_splicing);
    RUN(eval_quasiquote_nested_list);
    RUN(eval_quasiquote_dotted);
    RUN(eval_quasiquote_in_macro);
    RUN(eval_quasiquote_macro_splice);
    RUN(eval_gensym);
    RUN(eval_load);
    RUN(eval_boot_functions);
    RUN(eval_block_return);
    RUN(eval_return_in_do);
    RUN(eval_return_in_dolist);
    RUN(eval_return_in_dotimes);
    RUN(eval_prog1);
    RUN(eval_prog2);
    RUN(eval_case);
    RUN(eval_typecase);
    RUN(eval_flet);
    RUN(eval_labels);
    RUN(eval_optional);
    RUN(eval_optional_default);
    RUN(eval_key);
    RUN(eval_key_default);
    RUN(eval_optional_lambda);
    RUN(eval_key_unknown_error);
    RUN(eval_key_allow_other_keys);
    RUN(eval_key_caller_allow);
    RUN(eval_eval_builtin);
    RUN(eval_macroexpand_1);
    RUN(eval_macroexpand);
    RUN(eval_tagbody_basic);
    RUN(eval_tagbody_forward_go);
    RUN(eval_tagbody_backward_go);
    RUN(eval_tagbody_nested);
    RUN(eval_tagbody_fixnum_tags);
    RUN(eval_tagbody_go_from_if);
    RUN(eval_catch_basic);
    RUN(eval_catch_normal);
    RUN(eval_catch_throw_across_call);
    RUN(eval_catch_nested);
    RUN(eval_catch_nested_outer);
    RUN(eval_catch_no_value);
    RUN(eval_throw_unmatched);
    RUN(eval_uwp_normal);
    RUN(eval_uwp_throw_cleanup);
    RUN(eval_uwp_throw_value);
    RUN(eval_uwp_nested);
    RUN(eval_uwp_error_cleanup);
    RUN(eval_values_basic);
    RUN(eval_multiple_value_bind);
    RUN(eval_multiple_value_list);
    RUN(eval_multiple_value_prog1);
    RUN(eval_nth_value);
    RUN(eval_values_list);
    RUN(eval_mv_propagation);

    teardown();
    REPORT();
}
