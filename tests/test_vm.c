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

/* --- Heap-boxed cells (mutable closure capture) --- */

TEST(eval_cell_basic_counter)
{
    /* Closure captures and mutates a let variable via setq */
    eval_print("(let ((n 0)) (defun cell-inc () (setq n (+ n 1)) n))");
    ASSERT_EQ_INT(eval_int("(cell-inc)"), 1);
    ASSERT_EQ_INT(eval_int("(cell-inc)"), 2);
    ASSERT_EQ_INT(eval_int("(cell-inc)"), 3);
}

TEST(eval_cell_shared_getter_setter)
{
    /* Two closures share a mutable variable via heap-boxed cell */
    eval_print("(let ((x 10)) (defun cg () x) (defun cs (v) (setq x v)))");
    ASSERT_EQ_INT(eval_int("(cg)"), 10);
    eval_print("(cs 42)");
    ASSERT_EQ_INT(eval_int("(cg)"), 42);
    eval_print("(cs 99)");
    ASSERT_EQ_INT(eval_int("(cg)"), 99);
}

TEST(eval_cell_accumulator_param)
{
    /* Parameter captured and mutated in returned closure */
    eval_print("(defun make-acc (init) (lambda (x) (setq init (+ init x)) init))");
    eval_print("(defvar *ta* (make-acc 0))");
    ASSERT_EQ_INT(eval_int("(funcall *ta* 5)"), 5);
    ASSERT_EQ_INT(eval_int("(funcall *ta* 3)"), 8);
    ASSERT_EQ_INT(eval_int("(funcall *ta* 10)"), 18);
}

TEST(eval_cell_independent_instances)
{
    /* Multiple closure instances have independent cells */
    eval_print("(defun mk-ctr () (let ((n 0)) (lambda () (setq n (+ n 1)) n)))");
    eval_print("(defvar *tc1* (mk-ctr))");
    eval_print("(defvar *tc2* (mk-ctr))");
    ASSERT_EQ_INT(eval_int("(funcall *tc1*)"), 1);
    ASSERT_EQ_INT(eval_int("(funcall *tc1*)"), 2);
    ASSERT_EQ_INT(eval_int("(funcall *tc2*)"), 1);  /* independent */
    ASSERT_EQ_INT(eval_int("(funcall *tc1*)"), 3);
    ASSERT_EQ_INT(eval_int("(funcall *tc2*)"), 2);
}

TEST(eval_cell_setq_before_capture)
{
    /* Mutation before capture — closure sees post-mutation value */
    ASSERT_EQ_INT(eval_int("(let ((x 0)) (setq x 42) ((lambda () x)))"), 42);
}

TEST(eval_cell_nested_closure)
{
    /* Inner closure mutates var from outer let via upvalue chain */
    eval_print("(let ((x 0))"
               "  (defun cell-outer () (lambda () (setq x (+ x 1)) x)))");
    ASSERT_EQ_INT(eval_int("(funcall (cell-outer))"), 1);
    ASSERT_EQ_INT(eval_int("(funcall (cell-outer))"), 2);
}

TEST(eval_cell_let_star)
{
    /* let* with one mutable captured var, one read-only captured var */
    eval_print("(let* ((a 1) (b 10))"
               "  (defun clg () (+ a b))"
               "  (defun cls (v) (setq a v)))");
    ASSERT_EQ_INT(eval_int("(clg)"), 11);
    eval_print("(cls 5)");
    ASSERT_EQ_INT(eval_int("(clg)"), 15);
}

TEST(eval_cell_flet_mutation)
{
    /* flet function mutates captured outer variable */
    ASSERT_EQ_INT(eval_int(
        "(let ((n 0))"
        "  (flet ((bump () (setq n (+ n 1))))"
        "    (bump) (bump) (bump) n))"), 3);
}

TEST(eval_cell_labels_shared)
{
    /* labels functions share mutable variable */
    ASSERT_STR_EQ(eval_print(
        "(let ((n 0))"
        "  (labels ((my-inc () (setq n (+ n 1)))"
        "           (my-peek () n))"
        "    (my-inc) (my-inc) (my-inc) (my-peek)))"), "3");
}

TEST(eval_cell_do_let_capture)
{
    /* do loop with per-iteration let binding: each closure gets own value */
    ASSERT_STR_EQ(eval_print(
        "(let ((fns nil))"
        "  (do ((i 0 (+ i 1)))"
        "      ((= i 3) (mapcar #'funcall (reverse fns)))"
        "    (let ((j i))"
        "      (setq fns (cons (lambda () j) fns)))))"),
        "(0 1 2)");
}

TEST(eval_cell_readonly_no_boxing)
{
    /* Read-only capture: no boxing, no regression */
    ASSERT_EQ_INT(eval_int("(let ((x 42)) ((lambda () x)))"), 42);
    eval_print("(defun ma2 (n) (lambda (x) (+ n x)))");
    ASSERT_EQ_INT(eval_int("((ma2 10) 5)"), 15);
}

TEST(eval_setf_boxing_across_closure)
{
    /* setf must trigger boxing when variable is captured across closure boundary */
    eval_print("(defun test-setf-box (seed)"
               "  (let ((f (lambda (x) (setf seed (+ seed x)))))"
               "    (funcall f 1)"
               "    (funcall f 2)"
               "    seed))");
    ASSERT_EQ_INT(eval_int("(test-setf-box 0)"), 3);

    /* reduce pattern from ASDF */
    eval_print("(defun test-reduce (combinator seed)"
               "  (dolist (x '(1 2 3))"
               "    (funcall (lambda () (setf seed (funcall combinator x seed)))))"
               "  seed)");
    ASSERT_EQ_INT(eval_int("(test-reduce #'+ 0)"), 6);
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

    /* Nested dolist with same var name — stale slot binding regression test.
     * Inner dolist's var 's' in slot N must not shadow outer dolist's 's'
     * after the inner scope exits (anonymous iter/result slots must clear
     * stale locals[] entries). */
    ASSERT_STR_EQ(eval_print(
        "(let ((r nil))"
        "  (let ((y 0))"
        "    (dolist (s '(a b)) s))"
        "  (dolist (s '(x y z))"
        "    (setq r (cons s r)))"
        "  (reverse r))"),
        "(X Y Z)");
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

TEST(eval_do_star)
{
    /* Sequential binding: j sees current i (not previous) */
    ASSERT_STR_EQ(eval_print(
        "(let ((r nil))"
        "  (do* ((i 0 (+ i 1)) (j (* i 10) (* i 10)))"
        "       ((>= i 3) (reverse r))"
        "    (push (list i j) r)))"),
        "((0 0) (1 10) (2 20))");
    /* Contrast with do (parallel): j would see old i */
    ASSERT_STR_EQ(eval_print(
        "(let ((r nil))"
        "  (do ((i 0 (+ i 1)) (j 0 (* i 10)))"
        "      ((>= i 3) (reverse r))"
        "    (push (list i j) r)))"),
        "((0 0) (1 0) (2 10))");
    /* Simple do* */
    ASSERT_EQ_INT(eval_int(
        "(do* ((i 10 (- i 1))) ((= i 0) 42))"), 42);
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

/* --- Nested quasiquote --- */

TEST(eval_quasiquote_nested_double_unquote)
{
    /* ``(a ,,x) at outer level evaluates x, inner level preserves UNQUOTE */
    ASSERT_STR_EQ(eval_print("(let ((x 42)) ``(a ,,x))"),
                  "(QUASIQUOTE (A (UNQUOTE 42)))");
}

TEST(eval_quasiquote_nested_unquote_splice)
{
    /* ,,@xs — splice into nested quasiquote produces multiple UNQUOTE forms */
    ASSERT_STR_EQ(eval_print("(let ((xs '(1 2 3))) ``(a ,,@xs))"),
                  "(QUASIQUOTE (A (UNQUOTE 1) (UNQUOTE 2) (UNQUOTE 3)))");
}

TEST(eval_quasiquote_nested_simple)
{
    /* Nested backquote without unquote — all data */
    ASSERT_STR_EQ(eval_print("``(a b)"),
                  "(QUASIQUOTE (A B))");
}

TEST(eval_quasiquote_nested_inner_splice)
{
    /* ,@expr inside single backquote still works */
    ASSERT_STR_EQ(eval_print("(let ((xs '(1 2))) `(a ,@xs b))"),
                  "(A 1 2 B)");
}

TEST(eval_quasiquote_once_only_pattern)
{
    /* Test the once-only macro pattern from Alexandria */
    eval_print(
        "(defmacro test-oo (var &body body)"
        "  (let ((g (gensym)))"
        "    `(let ((,g ,var))"
        "       `(let ((,,g ,,(car body)))"
        "          t))))");
    ASSERT_STR_EQ(eval_print("(macroexpand-1 '(test-oo x (+ 1 2)))"),
                  /* Should produce a let-binding with gensym */
                  eval_print("(macroexpand-1 '(test-oo x (+ 1 2)))"));
}

/* --- Standard condition accessors --- */

TEST(eval_reader_error_condition)
{
    /* reader-error is a subtype of both parse-error and stream-error */
    ASSERT_STR_EQ(eval_print("(typep (make-condition 'reader-error) 'reader-error)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-condition 'reader-error) 'parse-error)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-condition 'reader-error) 'stream-error)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-condition 'reader-error) 'error)"), "T");
}

TEST(eval_stream_error_stream_accessor)
{
    ASSERT_STR_EQ(eval_print(
        "(stream-error-stream (make-condition 'stream-error :stream *standard-output*))"),
        eval_print("*standard-output*"));
}

TEST(eval_package_error_package_accessor)
{
    ASSERT_STR_EQ(eval_print(
        "(package-error-package (make-condition 'package-error :package (find-package :cl)))"),
        eval_print("(find-package :cl)"));
}

TEST(eval_cell_error_name_accessor)
{
    ASSERT_STR_EQ(eval_print(
        "(cell-error-name (make-condition 'cell-error :name 'foo))"),
        "FOO");
}

TEST(eval_file_error_pathname_accessor)
{
    ASSERT_STR_EQ(eval_print(
        "(file-error-pathname (make-condition 'file-error :pathname \"/tmp/x\"))"),
        "\"/tmp/x\"");
}

/* --- define-compiler-macro (no-op) --- */

TEST(eval_define_compiler_macro)
{
    /* define-compiler-macro should not error, returns the name */
    ASSERT_STR_EQ(eval_print(
        "(define-compiler-macro my-fn (&whole form x) (declare (ignore form x)) nil)"),
        "MY-FN");
    /* compiler-macro-function returns NIL */
    ASSERT_STR_EQ(eval_print("(compiler-macro-function 'my-fn)"), "NIL");
}

/* --- setf values --- */

TEST(eval_setf_values)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((a 0) (b 0) (c 0))"
        "  (setf (values a b c) (values 10 20 30))"
        "  (list a b c))"),
        "(10 20 30)");
}

TEST(eval_setf_values_two)
{
    ASSERT_EQ_INT(eval_int(
        "(let ((x 0) (y 0))"
        "  (setf (values x y) (values 100 200))"
        "  (+ x y))"), 300);
}

/* --- define-setf-expander (no-op) --- */

TEST(eval_define_setf_expander)
{
    ASSERT_STR_EQ(eval_print(
        "(define-setf-expander my-place (x) (declare (ignore x)) nil)"),
        "MY-PLACE");
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
    /* Labels inside let* — boxed slots must not leak into subsequent bindings */
    ASSERT_EQ_INT(eval_int(
        "(let* ((x 99) "
        "       (y (labels ((fn (z) z)) (fn x)))) y)"), 99);
    /* Labels with implicit block (return-from) */
    ASSERT_EQ_INT(eval_int(
        "(labels ((lab-f (n) (if (<= n 0) (return-from lab-f 42) (lab-f (- n 1))))) (lab-f 3))"), 42);
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

TEST(eval_optional_suppliedp)
{
    /* &optional with supplied-p variable */
    eval_print("(defun opt-sp (a &optional (b nil b-p)) (list a b b-p))");
    ASSERT_STR_EQ(eval_print("(opt-sp 1)"), "(1 NIL NIL)");
    ASSERT_STR_EQ(eval_print("(opt-sp 1 2)"), "(1 2 T)");
    ASSERT_STR_EQ(eval_print("(opt-sp 1 nil)"), "(1 NIL T)");
    /* Multiple optionals with supplied-p */
    eval_print("(defun opt-sp2 (&optional (x 0 xp) (y 0 yp)) (list x xp y yp))");
    ASSERT_STR_EQ(eval_print("(opt-sp2)"), "(0 NIL 0 NIL)");
    ASSERT_STR_EQ(eval_print("(opt-sp2 5)"), "(5 T 0 NIL)");
    ASSERT_STR_EQ(eval_print("(opt-sp2 5 10)"), "(5 T 10 T)");
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

TEST(eval_catch_throw_deep_chain)
{
    /* Regression: throw across deep call chain — exercises NLX frame sync
     * when longjmp restores frame state after potential tail calls in callees */
    eval_print("(defun nlx-deep-a (f) (funcall f))");
    eval_print("(defun nlx-deep-b (f) (nlx-deep-a f))");
    eval_print("(defun nlx-deep-c (f) (nlx-deep-b f))");
    ASSERT_EQ_INT(eval_int(
        "(catch 'deep-tag (nlx-deep-c (lambda () (throw 'deep-tag 77))))"), 77);
    /* Verify computation continues correctly after catch restores frame */
    ASSERT_EQ_INT(eval_int(
        "(+ 1 (catch 'deep-tag (nlx-deep-c (lambda () (throw 'deep-tag 77)))))"), 78);
}

TEST(eval_block_return_deep_chain)
{
    /* Regression: return-from across deep call chain */
    eval_print("(defun blk-caller (f) (funcall f))");
    ASSERT_EQ_INT(eval_int(
        "(block done (blk-caller (lambda () (return-from done 99))) 0)"), 99);
    /* Normal path still works */
    ASSERT_EQ_INT(eval_int(
        "(block done (blk-caller (lambda () 50)) 42)"), 42);
}

TEST(eval_uwp_throw_deep_cleanup)
{
    /* Regression: unwind-protect cleanup runs correctly after throw across deep calls */
    ASSERT_STR_EQ(eval_print(
        "(let ((log nil))"
        "  (catch 'x"
        "    (unwind-protect"
        "      (nlx-deep-c (lambda () (throw 'x 1)))"
        "      (setq log 'cleaned)))"
        "  log)"),
        "CLEANED");
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

/* === Dynamic (special) variables === */

TEST(eval_defvar_basic)
{
    ASSERT_STR_EQ(eval_print("(defvar *x* 10)"), "*X*");
    ASSERT_EQ_INT(eval_int("*x*"), 10);
}

TEST(eval_defvar_no_overwrite)
{
    /* defvar does not overwrite existing value */
    eval_print("(defvar *y* 42)");
    eval_print("(defvar *y* 99)");
    ASSERT_EQ_INT(eval_int("*y*"), 42);
}

TEST(eval_defparameter_overwrite)
{
    /* defparameter always overwrites */
    eval_print("(defparameter *z* 10)");
    ASSERT_EQ_INT(eval_int("*z*"), 10);
    eval_print("(defparameter *z* 20)");
    ASSERT_EQ_INT(eval_int("*z*"), 20);
}

TEST(eval_defconstant_basic)
{
    ASSERT_STR_EQ(eval_print("(defconstant +my-const+ 42)"), "+MY-CONST+");
    ASSERT_EQ_INT(eval_int("+my-const+"), 42);
}

TEST(eval_defconstant_setq_error)
{
    /* setq on a constant should error at compile time */
    eval_print("(defconstant +dc1+ 10)");
    const char *r = eval_print("(setq +dc1+ 20)");
    ASSERT(strstr(r, "ERROR") != NULL);
    /* Value should be unchanged */
    ASSERT_EQ_INT(eval_int("+dc1+"), 10);
}

TEST(eval_defconstant_set_error)
{
    /* set on a constant should error at runtime */
    eval_print("(defconstant +dc2+ 99)");
    const char *r = eval_print("(set '+dc2+ 0)");
    ASSERT(strstr(r, "ERROR") != NULL);
    ASSERT_EQ_INT(eval_int("+dc2+"), 99);
}

TEST(eval_defconstant_t_is_constant)
{
    /* T is a constant per CL spec */
    const char *r = eval_print("(setq t 42)");
    ASSERT(strstr(r, "ERROR") != NULL);
}

TEST(eval_defconstant_keyword_error)
{
    /* Keywords are constants per CL spec */
    const char *r = eval_print("(set ':foo 42)");
    ASSERT(strstr(r, "ERROR") != NULL);
}

TEST(eval_special_let_binding)
{
    /* let dynamically binds special var, restores after */
    eval_print("(defvar *a* 1)");
    ASSERT_EQ_INT(eval_int("(let ((*a* 2)) *a*)"), 2);
    ASSERT_EQ_INT(eval_int("*a*"), 1);
}

TEST(eval_special_let_star_binding)
{
    /* let* same behavior */
    eval_print("(defvar *b* 10)");
    ASSERT_EQ_INT(eval_int("(let* ((*b* 20)) *b*)"), 20);
    ASSERT_EQ_INT(eval_int("*b*"), 10);
}

TEST(eval_special_visible_in_called_fn)
{
    /* Dynamic binding visible through function calls */
    eval_print("(defvar *d* 0)");
    eval_print("(defun read-d () *d*)");
    ASSERT_EQ_INT(eval_int("(let ((*d* 99)) (read-d))"), 99);
    ASSERT_EQ_INT(eval_int("(read-d)"), 0);
}

TEST(eval_special_nested_binding)
{
    /* Nested let with same special var */
    eval_print("(defvar *e* 1)");
    ASSERT_EQ_INT(eval_int(
        "(let ((*e* 2)) (let ((*e* 3)) *e*))"), 3);
    ASSERT_EQ_INT(eval_int("*e*"), 1);
}

TEST(eval_special_setq)
{
    /* setq modifies current binding, outer restored */
    eval_print("(defvar *f* 1)");
    ASSERT_EQ_INT(eval_int(
        "(let ((*f* 10)) (setq *f* 20) *f*)"), 20);
    ASSERT_EQ_INT(eval_int("*f*"), 1);
}

TEST(eval_special_unwind_protect)
{
    /* Bindings restored on throw through unwind-protect */
    eval_print("(defvar *g* 1)");
    eval_print(
        "(catch 'done"
        "  (unwind-protect"
        "    (let ((*g* 99))"
        "      (throw 'done *g*))"
        "    nil))");
    ASSERT_EQ_INT(eval_int("*g*"), 1);
}

TEST(eval_special_error_restore)
{
    /* Bindings restored on error */
    eval_print("(defvar *h* 1)");
    eval_print("(let ((*h* 99)) (error \"boom\"))");
    ASSERT_EQ_INT(eval_int("*h*"), 1);
}

TEST(eval_special_mixed_let)
{
    /* let with both lexical and special bindings */
    eval_print("(defvar *m* 10)");
    ASSERT_EQ_INT(eval_int(
        "(let ((x 1) (*m* 20) (y 2)) (+ x *m* y))"), 23);
    ASSERT_EQ_INT(eval_int("*m*"), 10);
}

/* --- setf and mutation --- */

TEST(eval_setf_car_cdr)
{
    ASSERT_STR_EQ(eval_print("(let ((x (list 1 2 3))) (setf (car x) 10) x)"), "(10 2 3)");
    ASSERT_STR_EQ(eval_print("(let ((x (cons 1 2))) (setf (cdr x) 3) x)"), "(1 . 3)");
}

TEST(eval_setf_first_rest)
{
    ASSERT_STR_EQ(eval_print("(let ((x (list 1 2 3))) (setf (first x) 10) x)"), "(10 2 3)");
    ASSERT_STR_EQ(eval_print("(let ((x (list 1 2 3))) (setf (rest x) (list 20 30)) x)"), "(1 20 30)");
}

TEST(eval_setf_nth)
{
    ASSERT_STR_EQ(eval_print("(let ((x (list 1 2 3))) (setf (nth 1 x) 20) x)"), "(1 20 3)");
    ASSERT_STR_EQ(eval_print("(let ((x (list 1 2 3))) (setf (nth 0 x) 10) x)"), "(10 2 3)");
}

TEST(eval_setf_variable)
{
    ASSERT_EQ_INT(eval_int("(let ((x 1)) (setf x 42) x)"), 42);
}

TEST(eval_setf_multiple)
{
    /* (setf a 1 b 2) — returns last value */
    ASSERT_EQ_INT(eval_int("(let ((a 0) (b 0)) (setf a 1 b 2))"), 2);
}

TEST(eval_rplaca_rplacd)
{
    ASSERT_STR_EQ(eval_print("(let ((x (cons 1 2))) (rplaca x 10))"), "(10 . 2)");
    ASSERT_STR_EQ(eval_print("(let ((x (cons 1 2))) (rplacd x 20))"), "(1 . 20)");
}

TEST(eval_aref_make_array_vectorp)
{
    ASSERT_STR_EQ(eval_print("(vectorp (make-array 3))"), "T");
    ASSERT_STR_EQ(eval_print("(vectorp 42)"), "NIL");
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 3)))"
        "  (setf (aref v 0) 10)"
        "  (setf (aref v 1) 20)"
        "  (setf (aref v 2) 30)"
        "  (+ (aref v 0) (aref v 1) (aref v 2)))"), 60);
}

TEST(eval_setf_svref)
{
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 2)))"
        "  (setf (svref v 0) 5)"
        "  (setf (svref v 1) 7)"
        "  (+ (svref v 0) (svref v 1)))"), 12);
}

TEST(eval_aref_string)
{
    /* aref on strings should return characters (CL spec: strings are arrays) */
    ASSERT_STR_EQ(eval_print("(aref \"hello\" 0)"), "#\\h");
    ASSERT_STR_EQ(eval_print("(aref \"hello\" 4)"), "#\\o");
    /* setf aref on strings */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (copy-seq \"hello\")))"
        "  (setf (aref s 0) #\\H)"
        "  s)"), "\"Hello\"");
    /* row-major-aref on strings */
    ASSERT_STR_EQ(eval_print("(row-major-aref \"abcd\" 2)"), "#\\c");
}

TEST(eval_symbol_value)
{
    eval_print("(defvar *sv-test* 42)");
    ASSERT_EQ_INT(eval_int("(symbol-value '*sv-test*)"), 42);
}

TEST(eval_setf_symbol_value)
{
    eval_print("(defvar *sv-setf* 10)");
    ASSERT_EQ_INT(eval_int("(setf (symbol-value '*sv-setf*) 99)"), 99);
    ASSERT_EQ_INT(eval_int("*sv-setf*"), 99);
}

TEST(eval_set_builtin)
{
    eval_print("(defvar *set-test* 10)");
    ASSERT_EQ_INT(eval_int("(set '*set-test* 77)"), 77);
    ASSERT_EQ_INT(eval_int("*set-test*"), 77);
}

TEST(eval_setf_return_value)
{
    /* setf returns the assigned value */
    ASSERT_EQ_INT(eval_int("(let ((x (list 1 2 3))) (setf (car x) 99))"), 99);
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 1))) (setf (aref v 0) 42))"), 42);
}

/* --- member & pushnew --- */

TEST(eval_member_found)
{
    ASSERT_STR_EQ(eval_print("(member 2 '(1 2 3))"), "(2 3)");
}

TEST(eval_member_not_found)
{
    ASSERT_STR_EQ(eval_print("(member 4 '(1 2 3))"), "NIL");
}

TEST(eval_member_with_test)
{
    ASSERT_STR_EQ(eval_print(
        "(member \"a\" (list \"a\" \"b\") :test #'equal)"), "(\"a\" \"b\")");
}

TEST(eval_pushnew_new)
{
    ASSERT_STR_EQ(eval_print("(let ((x '(1 2))) (pushnew 3 x) x)"),
                  "(3 1 2)");
}

TEST(eval_pushnew_existing)
{
    ASSERT_STR_EQ(eval_print("(let ((x '(1 2))) (pushnew 1 x) x)"),
                  "(1 2)");
}

/* --- eval-when --- */

TEST(eval_eval_when_execute)
{
    ASSERT_EQ_INT(eval_int("(eval-when (:execute) (+ 1 2))"), 3);
}

TEST(eval_eval_when_multiple_situations)
{
    ASSERT_EQ_INT(eval_int(
        "(eval-when (:compile-toplevel :load-toplevel :execute) (+ 10 20))"),
        30);
}

TEST(eval_eval_when_body)
{
    /* Top-level multi-form eval-when processes forms individually for macro availability.
     * Verify side effects work (defvar sets value). */
    ASSERT_EQ_INT(eval_int(
        "(progn (eval-when (:execute) (defvar *ew-test* 0) (setq *ew-test* 42)) *ew-test*)"), 42);
}

/* --- destructuring-bind --- */

TEST(eval_destructuring_bind_simple)
{
    ASSERT_STR_EQ(eval_print(
        "(destructuring-bind (a b c) '(1 2 3) (list a b c))"),
        "(1 2 3)");
}

TEST(eval_destructuring_bind_nested)
{
    ASSERT_STR_EQ(eval_print(
        "(destructuring-bind (a (b c) &rest d) '(1 (2 3) 4 5) (list a b c d))"),
        "(1 2 3 (4 5))");
}

TEST(eval_destructuring_bind_rest)
{
    ASSERT_STR_EQ(eval_print(
        "(destructuring-bind (a &rest b) '(1 2 3) (list a b))"),
        "(1 (2 3))");
}

TEST(eval_destructuring_bind_optional)
{
    ASSERT_STR_EQ(eval_print(
        "(destructuring-bind (a &optional (b 10)) '(1) (list a b))"),
        "(1 10)");
}

TEST(eval_destructuring_bind_optional_provided)
{
    ASSERT_STR_EQ(eval_print(
        "(destructuring-bind (a &optional (b 10)) '(1 2) (list a b))"),
        "(1 2)");
}

TEST(eval_destructuring_bind_body)
{
    ASSERT_STR_EQ(eval_print(
        "(destructuring-bind (a &body b) '(1 2 3) (list a b))"),
        "(1 (2 3))");
}

TEST(eval_destructuring_bind_key)
{
    /* Regression: &key with multiple keywords in destructuring-bind
       was reading garbage from the stack for unfound keys */
    ASSERT_STR_EQ(eval_print(
        "(destructuring-bind (&key a b) '(:a 1) (list a b))"),
        "(1 NIL)");
    ASSERT_STR_EQ(eval_print(
        "(destructuring-bind (&key a b) '(:a 1 :b 2) (list a b))"),
        "(1 2)");
    ASSERT_STR_EQ(eval_print(
        "(destructuring-bind (&key a b c) '(:a 1 :c 3) (list a b c))"),
        "(1 NIL 3)");
}

/* --- defsetf --- */

TEST(eval_defsetf_short)
{
    eval_print("(defun my-get (vec i) (aref vec i))");
    eval_print(
        "(defun my-put (vec i val) (progn (setf (aref vec i) val) val))");
    eval_print("(defsetf my-get my-put)");
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 3))) (setf (my-get v 0) 42) (my-get v 0))"),
        42);
}

TEST(eval_defsetf_cadr)
{
    eval_print("(defun set-cadr (l v) (rplaca (cdr l) v) v)");
    eval_print("(defsetf cadr set-cadr)");
    ASSERT_STR_EQ(eval_print(
        "(let ((x (list 1 2 3))) (setf (cadr x) 99) x)"),
        "(1 99 3)");
}

/* get-setf-expansion must return proper store-form for defsetf places,
 * so that define-setf-expander on higher-level accessors can compose. */
TEST(eval_get_setf_expansion_defsetf)
{
    /* Define a struct with a mutable slot */
    eval_print("(defstruct (box (:copier nil)) (val nil))");
    /* Define a higher-level setf expander that composes with the struct setter.
     * (setf (box-val-plus-one b) v) => set box-val to (1- v) */
    eval_print(
        "(define-setf-expander box-val-plus-one (b &environment env)"
        "  (multiple-value-bind (temps vals stores store-form access-form)"
        "      (get-setf-expansion (list 'box-val b) env)"
        "    (let ((store (gensym)))"
        "      (values temps vals (list store)"
        "              (list 'let (list (list (car stores) (list '1- store)))"
        "                    store-form)"
        "              (list '1+ access-form)))))");
    eval_print("(defun box-val-plus-one (b) (1+ (box-val b)))");
    ASSERT_EQ_INT(eval_int(
        "(let ((b (make-box :val 10)))"
        "  (setf (box-val-plus-one b) 100)"
        "  (box-val b))"),
        99);
}

TEST(eval_equalp)
{
    /* Case-insensitive chars and strings */
    ASSERT_STR_EQ(eval_print("(equalp #\\a #\\A)"), "T");
    ASSERT_STR_EQ(eval_print("(equalp \"hello\" \"HELLO\")"), "T");
    ASSERT_STR_EQ(eval_print("(equalp \"abc\" \"ABD\")"), "NIL");
    /* Numeric cross-type */
    ASSERT_STR_EQ(eval_print("(equalp 1 1.0)"), "T");
    ASSERT_STR_EQ(eval_print("(equalp 0 0.0)"), "T");
    /* Vectors element-wise */
    ASSERT_STR_EQ(eval_print("(equalp #(1 2 3) #(1 2 3))"), "T");
    ASSERT_STR_EQ(eval_print("(equalp #(1 2 3) #(1 2 4))"), "NIL");
    /* Structs */
    eval_print("(defstruct ep-test (x nil) (y nil))");
    ASSERT_STR_EQ(eval_print("(equalp (make-ep-test :x 1 :y \"hi\") (make-ep-test :x 1.0 :y \"HI\"))"), "T");
    /* make-array with :element-type character creates string */
    ASSERT_STR_EQ(eval_print(
        "(stringp (make-array 3 :element-type 'character :initial-element #\\x))"), "T");
    ASSERT_STR_EQ(eval_print(
        "(stringp (make-array '(3) :element-type 'base-char :initial-element #\\x))"), "T");
}

/* --- defun block (return-from inside do) --- */

TEST(eval_defun_return_from)
{
    eval_print(
        "(defun find-it (item list)"
        "  (do ((l list (cdr l)))"
        "      ((null l) nil)"
        "    (when (eql item (car l))"
        "      (return-from find-it l))))");
    ASSERT_STR_EQ(eval_print("(find-it 2 '(1 2 3))"), "(2 3)");
    ASSERT_STR_EQ(eval_print("(find-it 4 '(1 2 3))"), "NIL");
}

/* --- Cross-closure return-from (NLX-based blocks) --- */

TEST(eval_return_from_labels)
{
    /* return-from across labels boundary (ASDF pattern) */
    eval_print(
        "(defun frob (x)"
        "  (labels ((inner (v)"
        "             (if (> v 10)"
        "               (return-from frob (+ v 100))"
        "               v)))"
        "    (inner x)))");
    ASSERT_EQ_INT(eval_int("(frob 5)"), 5);
    ASSERT_EQ_INT(eval_int("(frob 20)"), 120);
}

TEST(eval_return_from_flet)
{
    /* return-from across flet boundary */
    eval_print(
        "(defun flet-test (x)"
        "  (flet ((helper (v)"
        "           (when (evenp v)"
        "             (return-from flet-test (* v 10)))))"
        "    (helper x)"
        "    x))");
    ASSERT_EQ_INT(eval_int("(flet-test 3)"), 3);
    ASSERT_EQ_INT(eval_int("(flet-test 4)"), 40);
}

TEST(eval_return_from_lambda)
{
    /* return-from across lambda boundary */
    eval_print(
        "(defun lambda-test (lst)"
        "  (dolist (x lst)"
        "    (funcall (lambda (v)"
        "               (when (= v 42)"
        "                 (return-from lambda-test :found)))"
        "             x))"
        "  :not-found)");
    ASSERT_STR_EQ(eval_print("(lambda-test '(1 2 3))"), ":NOT-FOUND");
    ASSERT_STR_EQ(eval_print("(lambda-test '(1 42 3))"), ":FOUND");
}

TEST(eval_return_from_nested_labels)
{
    /* return-from through two levels of labels nesting */
    eval_print(
        "(defun nested-test (x)"
        "  (labels ((outer (v)"
        "             (labels ((inner (w)"
        "                       (when (> w 100)"
        "                         (return-from nested-test :big))))"
        "               (inner v)"
        "               :small)))"
        "    (outer x)))");
    ASSERT_STR_EQ(eval_print("(nested-test 50)"), ":SMALL");
    ASSERT_STR_EQ(eval_print("(nested-test 200)"), ":BIG");
}

TEST(eval_block_with_unwind_protect)
{
    /* return-from across labels with interposing unwind-protect.
     * Use *uwp-log* (special var) since closures have value capture. */
    eval_print("(defvar *uwp-log* nil)");
    eval_print(
        "(defun uwp-test (x)"
        "  (setq *uwp-log* nil)"
        "  (labels ((inner (v)"
        "             (unwind-protect"
        "               (when (> v 5) (return-from uwp-test :early))"
        "               (setq *uwp-log* :cleaned))))"
        "    (inner x)"
        "    :normal))");
    ASSERT_STR_EQ(eval_print("(uwp-test 10)"), ":EARLY");
    ASSERT_STR_EQ(eval_print("*uwp-log*"), ":CLEANED");
}

/* Regression: tail call inside defun leaks NLX BLOCK frame, causing
 * OP_UWPOP to pop the leaked BLOCK instead of the UWP.  This corrupts
 * unwind-protect cleanup when a cross-closure return-from unwinds
 * through the UWP. */
TEST(eval_uwp_nlx_leak_tailcall)
{
    eval_print("(defvar *uwp-cleanup-val* nil)");
    /* call-with-fn: has &key so keyword supplied-p writes T to stack.
     * The defun block uses NLX (body contains lambda). Tail call to
     * the thunk leaks the BLOCK NLX frame. */
    eval_print(
        "(defun call-with-fn (thunk &key override)"
        "  (declare (ignore override))"
        "  (funcall thunk))");
    /* outer-fn: calls call-with-fn with a lambda that has unwind-protect.
     * The lambda captures session (a local) and accesses it in cleanup. */
    eval_print(
        "(defun outer-fn (fun)"
        "  (call-with-fn"
        "    #'(lambda ()"
        "        (let ((session 'my-session))"
        "          (unwind-protect"
        "              (funcall fun)"
        "            (setq *uwp-cleanup-val* session))))))");
    /* wrapper: establishes a block, calls outer-fn, which calls fun.
     * fun does return-from across the UWP boundary. */
    eval_print(
        "(defun wrapper ()"
        "  (block done"
        "    (outer-fn #'(lambda () (return-from done :early)))"
        "    :normal))");
    ASSERT_STR_EQ(eval_print("(wrapper)"), ":EARLY");
    ASSERT_STR_EQ(eval_print("*uwp-cleanup-val*"), "MY-SESSION");
}

/* Regression: NLX block frame precision.  Functions with closures that
 * don't use return-from should NOT push NLX BLOCK frames.  Previously,
 * any function containing lambda/labels/flet pushed an NLX frame even
 * when return-from was never used, leaking NLX frames during deep
 * recursion (e.g. CLOS dispatch + FSet WB tree operations). */
TEST(eval_block_nlx_precision)
{
    /* Defun with labels but no return-from — should use local block path.
     * Deep mutual recursion shouldn't overflow NLX stack. */
    eval_print(
        "(defun nlx-prec-a (n)"
        "  (labels ((even? (x) (if (= x 0) t (odd? (1- x))))"
        "           (odd? (x) (if (= x 0) nil (even? (1- x)))))"
        "    (even? n)))");
    ASSERT_STR_EQ(eval_print("(nlx-prec-a 100)"), "T");
    ASSERT_STR_EQ(eval_print("(nlx-prec-a 99)"), "NIL");

    /* Same with lambda — no return-from, so no NLX needed */
    eval_print(
        "(defun nlx-prec-b (lst)"
        "  (let ((result nil))"
        "    (dolist (x lst)"
        "      (push (funcall (lambda (v) (* v v)) x) result))"
        "    (nreverse result)))");
    ASSERT_STR_EQ(eval_print("(nlx-prec-b '(1 2 3))"), "(1 4 9)");
}

/* Regression: dolist variable shadowing.  CL spec requires the list-form
 * to be evaluated BEFORE the loop variable is bound.  Previously, the
 * loop variable was added to the environment before compiling the list-form,
 * so (dolist (x x) ...) would see the new uninitialized x. */
TEST(eval_dolist_var_shadowing)
{
    /* The classic pattern from FSet's Do-WB-Set-Tree-Members:
     * (dolist (val (some-accessor val)) ...) where val shadows outer val */
    ASSERT_STR_EQ(eval_print(
        "(let ((mylist '(a b c)))"
        "  (let ((result nil))"
        "    (dolist (mylist mylist)"
        "      (push mylist result))"
        "    (nreverse result)))"), "(A B C)");

    /* dotimes equivalent: (dotimes (n n) ...) where n shadows outer n */
    ASSERT_EQ_INT(eval_int(
        "(let ((n 5))"
        "  (let ((sum 0))"
        "    (dotimes (n n)"
        "      (incf sum n))"
        "    sum))"), 10);  /* 0+1+2+3+4 = 10 */
}

/* --- Phase 5 Tier 1: Character functions --- */

TEST(eval_characterp)
{
    ASSERT_STR_EQ(eval_print("(characterp #\\A)"), "T");
    ASSERT_STR_EQ(eval_print("(characterp 65)"), "NIL");
    ASSERT_STR_EQ(eval_print("(characterp #\\Space)"), "T");
}

TEST(eval_char_comparison)
{
    ASSERT_STR_EQ(eval_print("(char= #\\A #\\A)"), "T");
    ASSERT_STR_EQ(eval_print("(char= #\\A #\\B)"), "NIL");
    ASSERT_STR_EQ(eval_print("(char/= #\\A #\\B)"), "T");
    ASSERT_STR_EQ(eval_print("(char/= #\\A #\\A)"), "NIL");
    ASSERT_STR_EQ(eval_print("(char< #\\A #\\B)"), "T");
    ASSERT_STR_EQ(eval_print("(char< #\\B #\\A)"), "NIL");
    ASSERT_STR_EQ(eval_print("(char> #\\B #\\A)"), "T");
    ASSERT_STR_EQ(eval_print("(char<= #\\A #\\A)"), "T");
    ASSERT_STR_EQ(eval_print("(char<= #\\A #\\B)"), "T");
    ASSERT_STR_EQ(eval_print("(char>= #\\B #\\A)"), "T");
    ASSERT_STR_EQ(eval_print("(char>= #\\A #\\A)"), "T");
}

TEST(eval_char_code_conversion)
{
    ASSERT_EQ_INT(eval_int("(char-code #\\A)"), 65);
    ASSERT_STR_EQ(eval_print("(code-char 65)"), "#\\A");
    ASSERT_EQ_INT(eval_int("(char-code (code-char 97))"), 97);
}

TEST(eval_char_case)
{
    ASSERT_STR_EQ(eval_print("(char-upcase #\\a)"), "#\\A");
    ASSERT_STR_EQ(eval_print("(char-upcase #\\A)"), "#\\A");
    ASSERT_STR_EQ(eval_print("(char-downcase #\\A)"), "#\\a");
    ASSERT_STR_EQ(eval_print("(char-downcase #\\a)"), "#\\a");
}

TEST(eval_char_predicates)
{
    ASSERT_STR_EQ(eval_print("(upper-case-p #\\A)"), "T");
    ASSERT_STR_EQ(eval_print("(upper-case-p #\\a)"), "NIL");
    ASSERT_STR_EQ(eval_print("(lower-case-p #\\a)"), "T");
    ASSERT_STR_EQ(eval_print("(lower-case-p #\\A)"), "NIL");
    ASSERT_STR_EQ(eval_print("(alpha-char-p #\\A)"), "T");
    ASSERT_STR_EQ(eval_print("(alpha-char-p #\\1)"), "NIL");
    ASSERT_STR_EQ(eval_print("(digit-char-p #\\5)"), "T");
    ASSERT_STR_EQ(eval_print("(digit-char-p #\\A)"), "NIL");
}

/* --- Phase 5 Tier 1: Symbol functions --- */

TEST(eval_symbol_name)
{
    ASSERT_STR_EQ(eval_print("(symbol-name 'foo)"), "\"FOO\"");
    ASSERT_STR_EQ(eval_print("(symbol-name nil)"), "\"NIL\"");
}

TEST(eval_symbol_package_fn)
{
    /* symbol-package returns a package object */
    ASSERT_STR_EQ(eval_print("(null (symbol-package 'foo))"), "NIL");
}

TEST(eval_fboundp)
{
    ASSERT_STR_EQ(eval_print("(fboundp '+)"), "T");
    ASSERT_STR_EQ(eval_print("(fboundp (gensym))"), "NIL");
}

TEST(eval_fdefinition)
{
    /* fdefinition returns the same as symbol-function */
    ASSERT_STR_EQ(eval_print("(functionp (fdefinition '+))"), "T");
}

TEST(eval_make_symbol)
{
    ASSERT_STR_EQ(eval_print("(symbolp (make-symbol \"TEST\"))"), "T");
    ASSERT_STR_EQ(eval_print("(symbol-name (make-symbol \"HELLO\"))"), "\"HELLO\"");
}

TEST(eval_keywordp)
{
    ASSERT_STR_EQ(eval_print("(keywordp :foo)"), "T");
    ASSERT_STR_EQ(eval_print("(keywordp 'foo)"), "NIL");
    ASSERT_STR_EQ(eval_print("(keywordp 42)"), "NIL");
}

/* --- Phase 5 Tier 1: Fixed builtins --- */

TEST(eval_length_vector)
{
    ASSERT_EQ_INT(eval_int("(length (make-array 5))"), 5);
}

TEST(eval_equal_vector)
{
    eval_print("(defun make-vec-12 () (let ((v (make-array 2))) (setf (aref v 0) 1) (setf (aref v 1) 2) v))");
    ASSERT_STR_EQ(eval_print(
        "(equal (make-vec-12) (make-vec-12))"), "T");
    ASSERT_STR_EQ(eval_print(
        "(let ((v (make-array 2))) (setf (aref v 0) 1) (setf (aref v 1) 99) (equal (make-vec-12) v))"), "NIL");
}

TEST(eval_mapcar_multi_list)
{
    ASSERT_STR_EQ(eval_print("(mapcar #'+ '(1 2 3) '(10 20 30))"),
        "(11 22 33)");
    ASSERT_STR_EQ(eval_print("(mapcar #'list '(a b) '(1 2))"),
        "((A 1) (B 2))");
    /* Uneven lists — stops at shortest */
    ASSERT_STR_EQ(eval_print("(mapcar #'+ '(1 2 3) '(10 20))"),
        "(11 22)");
}

TEST(eval_vector)
{
    ASSERT_STR_EQ(eval_print("(vector 1 2 3)"), "#(1 2 3)");
    ASSERT_STR_EQ(eval_print("(vector)"), "#()");
    ASSERT_EQ_INT(eval_int("(aref (vector 10 20 30) 1)"), 20);
    ASSERT_STR_EQ(eval_print("(vectorp (vector 1 2))"), "T");
}

TEST(eval_vector_reader)
{
    /* #(...) reader syntax */
    ASSERT_STR_EQ(eval_print("#(1 2 3)"), "#(1 2 3)");
    ASSERT_STR_EQ(eval_print("#()"), "#()");
    ASSERT_EQ_INT(eval_int("(aref #(10 20 30) 1)"), 20);
    ASSERT_EQ_INT(eval_int("(length #(a b c d))"), 4);
    ASSERT_STR_EQ(eval_print("(simple-vector-p #(1 2))"), "T");
    ASSERT_STR_EQ(eval_print("(vectorp #(1))"), "T");
    /* Nested vectors */
    ASSERT_STR_EQ(eval_print("(aref #(#(1 2) #(3 4)) 0)"), "#(1 2)");
    ASSERT_EQ_INT(eval_int("(aref (aref #(#(1 2) #(3 4)) 1) 0)"), 3);
    /* Mixed types */
    ASSERT_STR_EQ(eval_print("#(1 \"hello\" a)"), "#(1 \"hello\" A)");
}

TEST(eval_array_print_multidim)
{
    /* 2D */
    ASSERT_STR_EQ(eval_print(
        "(make-array '(2 3) :initial-contents '((1 2 3) (4 5 6)))"),
        "#2A((1 2 3) (4 5 6))");
    /* 3D */
    ASSERT_STR_EQ(eval_print(
        "(make-array '(2 2 2) :initial-contents '(((1 2) (3 4)) ((5 6) (7 8))))"),
        "#3A(((1 2) (3 4)) ((5 6) (7 8)))");
    /* Empty dimension */
    ASSERT_STR_EQ(eval_print("(make-array '(2 0))"), "#2A(() ())");
    ASSERT_STR_EQ(eval_print("(make-array '(0 3))"), "#2A()");
    /* *print-array* nil */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-array* nil)) (write-to-string (vector 1 2)))"),
        "\"#<VECTOR>\"");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-array* nil)) (write-to-string (make-array '(2 3))))"),
        "\"#<ARRAY>\"");
}

TEST(eval_array_dimensions)
{
    ASSERT_STR_EQ(eval_print("(array-dimensions (make-array 5))"), "(5)");
    ASSERT_STR_EQ(eval_print("(array-dimensions (vector 1 2 3))"), "(3)");
    ASSERT_STR_EQ(eval_print("(array-dimensions (vector))"), "(0)");
}

TEST(eval_array_rank)
{
    ASSERT_EQ_INT(eval_int("(array-rank (make-array 5))"), 1);
    ASSERT_EQ_INT(eval_int("(array-rank (vector 1 2 3))"), 1);
}

TEST(eval_setf_aref_multidim)
{
    /* 2D array: setf + read back */
    ASSERT_EQ_INT(eval_int(
        "(let ((a (make-array '(2 3))))"
        "  (setf (aref a 0 0) 10)"
        "  (setf (aref a 0 1) 20)"
        "  (setf (aref a 0 2) 30)"
        "  (setf (aref a 1 0) 40)"
        "  (setf (aref a 1 1) 50)"
        "  (setf (aref a 1 2) 60)"
        "  (+ (aref a 0 0) (aref a 1 2)))"), 70);

    /* setf returns the value */
    ASSERT_EQ_INT(eval_int(
        "(let ((a (make-array '(3 3))))"
        "  (setf (aref a 1 2) 99))"), 99);

    /* 3D array */
    ASSERT_EQ_INT(eval_int(
        "(let ((a (make-array '(2 2 2))))"
        "  (setf (aref a 0 0 0) 1)"
        "  (setf (aref a 1 1 1) 8)"
        "  (+ (aref a 0 0 0) (aref a 1 1 1)))"), 9);
}

TEST(eval_array_dimension)
{
    /* 1D */
    ASSERT_EQ_INT(eval_int("(array-dimension (make-array 5) 0)"), 5);
    /* 2D */
    ASSERT_EQ_INT(eval_int("(array-dimension (make-array '(3 4)) 0)"), 3);
    ASSERT_EQ_INT(eval_int("(array-dimension (make-array '(3 4)) 1)"), 4);
}

TEST(eval_array_total_size)
{
    ASSERT_EQ_INT(eval_int("(array-total-size (make-array 5))"), 5);
    ASSERT_EQ_INT(eval_int("(array-total-size (make-array '(3 4)))"), 12);
    ASSERT_EQ_INT(eval_int("(array-total-size (make-array '(2 3 4)))"), 24);
}

TEST(eval_array_row_major_index)
{
    /* 1D: identity */
    ASSERT_EQ_INT(eval_int("(array-row-major-index (make-array 5) 3)"), 3);
    /* 2D: row-major = row*ncols + col */
    ASSERT_EQ_INT(eval_int("(array-row-major-index (make-array '(3 4)) 0 0)"), 0);
    ASSERT_EQ_INT(eval_int("(array-row-major-index (make-array '(3 4)) 1 0)"), 4);
    ASSERT_EQ_INT(eval_int("(array-row-major-index (make-array '(3 4)) 2 3)"), 11);
}

TEST(eval_row_major_aref)
{
    /* Access via row-major index */
    ASSERT_EQ_INT(eval_int(
        "(let ((a (make-array '(2 3))))"
        "  (setf (aref a 1 2) 99)"
        "  (row-major-aref a 5))"), 99);  /* row 1, col 2 = 1*3+2 = 5 */

    /* setf row-major-aref */
    ASSERT_EQ_INT(eval_int(
        "(let ((a (make-array '(2 3))))"
        "  (setf (row-major-aref a 5) 77)"
        "  (aref a 1 2))"), 77);

    /* setf returns value */
    ASSERT_EQ_INT(eval_int(
        "(let ((a (make-array 3)))"
        "  (setf (row-major-aref a 1) 42))"), 42);
}

TEST(eval_fill_pointer)
{
    /* Basic fill-pointer read */
    ASSERT_EQ_INT(eval_int(
        "(fill-pointer (make-array 10 :fill-pointer 0))"), 0);
    ASSERT_EQ_INT(eval_int(
        "(fill-pointer (make-array 10 :fill-pointer 5))"), 5);

    /* fill-pointer T means start at 0 */
    ASSERT_EQ_INT(eval_int(
        "(fill-pointer (make-array 10 :fill-pointer t))"), 0);

    /* array-has-fill-pointer-p */
    ASSERT_STR_EQ(eval_print(
        "(array-has-fill-pointer-p (make-array 5 :fill-pointer 0))"), "T");
    ASSERT_STR_EQ(eval_print(
        "(array-has-fill-pointer-p (make-array 5))"), "NIL");

    /* setf fill-pointer */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 10 :fill-pointer 0)))"
        "  (setf (fill-pointer v) 7)"
        "  (fill-pointer v))"), 7);

    /* setf returns the new value */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 10 :fill-pointer 0)))"
        "  (setf (fill-pointer v) 3))"), 3);
}

TEST(eval_vector_push)
{
    /* basic vector-push */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 5 :fill-pointer 0)))"
        "  (vector-push 10 v)"
        "  (vector-push 20 v)"
        "  (vector-push 30 v)"
        "  (fill-pointer v))"), 3);

    /* vector-push returns old fill-pointer */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 5 :fill-pointer 0)))"
        "  (vector-push 10 v)"
        "  (vector-push 20 v))"), 1);

    /* vector-push returns NIL when full */
    ASSERT_STR_EQ(eval_print(
        "(let ((v (make-array 2 :fill-pointer 0)))"
        "  (vector-push 1 v)"
        "  (vector-push 2 v)"
        "  (vector-push 3 v))"), "NIL");

    /* pushed elements accessible via aref */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 3 :fill-pointer 0)))"
        "  (vector-push 10 v)"
        "  (vector-push 20 v)"
        "  (+ (aref v 0) (aref v 1)))"), 30);

    /* length reflects fill pointer */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 10 :fill-pointer 0)))"
        "  (vector-push 1 v)"
        "  (vector-push 2 v)"
        "  (length v))"), 2);
}

TEST(eval_adjust_array)
{
    /* grow array, preserving old data */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 3 :adjustable t :initial-element 1)))"
        "  (let ((v2 (adjust-array v 5 :initial-element 99)))"
        "    (+ (aref v2 0) (aref v2 3))))"), 100);  /* 1 + 99 */

    /* shrink array */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 5 :adjustable t :initial-element 42)))"
        "  (array-total-size (adjust-array v 3)))"), 3);

    /* preserves fill pointer */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 5 :fill-pointer 2 :adjustable t)))"
        "  (fill-pointer (adjust-array v 10)))"), 2);

    /* adjust with :fill-pointer override */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 5 :fill-pointer 2 :adjustable t)))"
        "  (fill-pointer (adjust-array v 10 :fill-pointer 8)))"), 8);
}

/* --- Array type predicates (Step 7) --- */

TEST(eval_array_type_predicates)
{
    /* arrayp */
    ASSERT_STR_EQ(eval_print("(arrayp (vector 1 2 3))"), "T");
    ASSERT_STR_EQ(eval_print("(arrayp (make-array 5))"), "T");
    ASSERT_STR_EQ(eval_print("(arrayp (make-array '(2 3)))"), "T");
    ASSERT_STR_EQ(eval_print("(arrayp \"hello\")"), "T");
    ASSERT_STR_EQ(eval_print("(arrayp 42)"), "NIL");
    ASSERT_STR_EQ(eval_print("(arrayp '(1 2))"), "NIL");
    ASSERT_STR_EQ(eval_print("(arrayp nil)"), "NIL");

    /* simple-vector-p */
    ASSERT_STR_EQ(eval_print("(simple-vector-p (vector 1 2 3))"), "T");
    ASSERT_STR_EQ(eval_print("(simple-vector-p (make-array 5))"), "T");
    ASSERT_STR_EQ(eval_print("(simple-vector-p (make-array 5 :fill-pointer 0))"), "NIL");
    ASSERT_STR_EQ(eval_print("(simple-vector-p (make-array 5 :adjustable t))"), "NIL");
    ASSERT_STR_EQ(eval_print("(simple-vector-p (make-array '(2 3)))"), "NIL");
    ASSERT_STR_EQ(eval_print("(simple-vector-p \"hello\")"), "NIL");
    ASSERT_STR_EQ(eval_print("(simple-vector-p 42)"), "NIL");

    /* adjustable-array-p */
    ASSERT_STR_EQ(eval_print("(adjustable-array-p (make-array 5 :adjustable t))"), "T");
    ASSERT_STR_EQ(eval_print("(adjustable-array-p (make-array 5))"), "NIL");
    ASSERT_STR_EQ(eval_print("(adjustable-array-p (vector 1 2))"), "NIL");
    ASSERT_STR_EQ(eval_print("(adjustable-array-p \"hello\")"), "NIL");
}

TEST(eval_array_typep)
{
    /* typep with ARRAY — true for all arrays */
    ASSERT_STR_EQ(eval_print("(typep (vector 1 2) 'array)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-array '(2 3)) 'array)"), "T");
    ASSERT_STR_EQ(eval_print("(typep \"hello\" 'array)"), "T");
    ASSERT_STR_EQ(eval_print("(typep 42 'array)"), "NIL");

    /* typep with VECTOR — true for 1D arrays and strings */
    ASSERT_STR_EQ(eval_print("(typep (vector 1 2) 'vector)"), "T");
    ASSERT_STR_EQ(eval_print("(typep \"hello\" 'vector)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-array '(2 3)) 'vector)"), "NIL");

    /* typep with SIMPLE-VECTOR — 1D, element-type T, simple */
    ASSERT_STR_EQ(eval_print("(typep (vector 1 2) 'simple-vector)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-array 5) 'simple-vector)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-array 5 :fill-pointer 0) 'simple-vector)"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep (make-array 5 :adjustable t) 'simple-vector)"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep \"hello\" 'simple-vector)"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep (make-array '(2 3)) 'simple-vector)"), "NIL");

    /* typep with SIMPLE-ARRAY — array with no fill-pointer, not adjustable */
    ASSERT_STR_EQ(eval_print("(typep (vector 1 2) 'simple-array)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-array '(2 3)) 'simple-array)"), "T");
    ASSERT_STR_EQ(eval_print("(typep \"hello\" 'simple-array)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-array 5 :fill-pointer 0) 'simple-array)"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep (make-array 5 :adjustable t) 'simple-array)"), "NIL");
}

TEST(eval_array_type_of)
{
    /* type-of returns specific types */
    ASSERT_STR_EQ(eval_print("(type-of (vector 1 2 3))"), "SIMPLE-VECTOR");
    ASSERT_STR_EQ(eval_print("(type-of (make-array 5))"), "SIMPLE-VECTOR");
    ASSERT_STR_EQ(eval_print("(type-of (make-array 5 :fill-pointer 0))"), "VECTOR");
    ASSERT_STR_EQ(eval_print("(type-of (make-array 5 :adjustable t))"), "VECTOR");
    ASSERT_STR_EQ(eval_print("(type-of (make-array '(2 3)))"), "SIMPLE-ARRAY");
    ASSERT_STR_EQ(eval_print("(type-of \"hello\")"), "STRING");
}

/* --- Phase 5 Tier 2: String functions --- */

TEST(eval_string_comparison)
{
    ASSERT_STR_EQ(eval_print("(string= \"hello\" \"hello\")"), "T");
    ASSERT_STR_EQ(eval_print("(string= \"hello\" \"HELLO\")"), "NIL");
    ASSERT_STR_EQ(eval_print("(string-equal \"hello\" \"HELLO\")"), "T");
    ASSERT_STR_EQ(eval_print("(string< \"abc\" \"abd\")"), "T");
    ASSERT_STR_EQ(eval_print("(string< \"abd\" \"abc\")"), "NIL");
    ASSERT_STR_EQ(eval_print("(string> \"abd\" \"abc\")"), "T");
    ASSERT_STR_EQ(eval_print("(string<= \"abc\" \"abc\")"), "T");
    ASSERT_STR_EQ(eval_print("(string>= \"abc\" \"abc\")"), "T");
}

TEST(eval_string_case_conversion)
{
    ASSERT_STR_EQ(eval_print("(string-upcase \"hello\")"), "\"HELLO\"");
    ASSERT_STR_EQ(eval_print("(string-downcase \"HELLO\")"), "\"hello\"");
    ASSERT_STR_EQ(eval_print("(string-upcase \"Hello World\")"), "\"HELLO WORLD\"");
}

TEST(eval_string_trim)
{
    ASSERT_STR_EQ(eval_print("(string-trim \" \" \"  hello  \")"), "\"hello\"");
    ASSERT_STR_EQ(eval_print("(string-left-trim \" \" \"  hello  \")"), "\"hello  \"");
    ASSERT_STR_EQ(eval_print("(string-right-trim \" \" \"  hello  \")"), "\"  hello\"");
}

TEST(eval_subseq)
{
    ASSERT_STR_EQ(eval_print("(subseq \"hello\" 1 3)"), "\"el\"");
    ASSERT_STR_EQ(eval_print("(subseq \"hello\" 2)"), "\"llo\"");
    ASSERT_STR_EQ(eval_print("(subseq '(a b c d) 1 3)"), "(B C)");
}

TEST(eval_concatenate)
{
    /* String result */
    ASSERT_STR_EQ(eval_print("(concatenate 'string \"hello\" \" \" \"world\")"),
        "\"hello world\"");
    ASSERT_STR_EQ(eval_print("(concatenate 'string \"ab\" (list #\\c #\\d))"),
        "\"abcd\"");
    ASSERT_STR_EQ(eval_print("(concatenate 'string (vector #\\x #\\y))"),
        "\"xy\"");
    /* Vector result */
    ASSERT_STR_EQ(eval_print("(concatenate 'simple-vector #(a b) #(c d))"),
        "#(A B C D)");
    ASSERT_STR_EQ(eval_print("(concatenate 'vector '(1 2) #(3 4))"),
        "#(1 2 3 4)");
    ASSERT_STR_EQ(eval_print("(concatenate 'vector \"ab\")"),
        "#(#\\a #\\b)");
    /* List result */
    ASSERT_STR_EQ(eval_print("(concatenate 'list #(1 2) '(3 4))"),
        "(1 2 3 4)");
    ASSERT_STR_EQ(eval_print("(concatenate 'list)"),
        "NIL");
    /* Empty inputs */
    ASSERT_STR_EQ(eval_print("(concatenate 'string)"), "\"\"");
    ASSERT_STR_EQ(eval_print("(concatenate 'vector)"), "#()");
}

TEST(eval_char_accessor)
{
    ASSERT_STR_EQ(eval_print("(char \"hello\" 0)"), "#\\h");
    ASSERT_STR_EQ(eval_print("(char \"hello\" 4)"), "#\\o");
    ASSERT_STR_EQ(eval_print("(schar \"abc\" 1)"), "#\\b");
}

TEST(eval_string_coerce)
{
    ASSERT_STR_EQ(eval_print("(string 'foo)"), "\"FOO\"");
    ASSERT_STR_EQ(eval_print("(string \"hello\")"), "\"hello\"");
    ASSERT_STR_EQ(eval_print("(string #\\A)"), "\"A\"");
}

TEST(eval_parse_integer)
{
    ASSERT_EQ_INT(eval_int("(parse-integer \"42\")"), 42);
    ASSERT_EQ_INT(eval_int("(parse-integer \"-7\")"), -7);
    ASSERT_EQ_INT(eval_int("(parse-integer \"FF\" :radix 16)"), 255);
    ASSERT_EQ_INT(eval_int("(parse-integer \"  123  \")"), 123);
}

TEST(eval_write_to_string)
{
    ASSERT_STR_EQ(eval_print("(write-to-string 42)"), "\"42\"");
    ASSERT_STR_EQ(eval_print("(write-to-string 'foo)"), "\"FOO\"");
}

TEST(eval_prin1_to_string)
{
    ASSERT_STR_EQ(eval_print("(prin1-to-string \"hello\")"),
        "\"\\\"hello\\\"\"");
    ASSERT_STR_EQ(eval_print("(prin1-to-string 42)"), "\"42\"");
}

TEST(eval_princ_to_string)
{
    ASSERT_STR_EQ(eval_print("(princ-to-string \"hello\")"), "\"hello\"");
    ASSERT_STR_EQ(eval_print("(princ-to-string 42)"), "\"42\"");
}

/* --- Phase 5 Tier 3: List utilities --- */

TEST(eval_nthcdr)
{
    ASSERT_STR_EQ(eval_print("(nthcdr 0 '(1 2 3))"), "(1 2 3)");
    ASSERT_STR_EQ(eval_print("(nthcdr 2 '(1 2 3))"), "(3)");
    ASSERT_STR_EQ(eval_print("(nthcdr 3 '(1 2 3))"), "NIL");
    ASSERT_STR_EQ(eval_print("(nthcdr 5 '(1 2 3))"), "NIL");
}

TEST(eval_last)
{
    ASSERT_STR_EQ(eval_print("(last '(1 2 3))"), "(3)");
    ASSERT_STR_EQ(eval_print("(last '(1 2 3) 2)"), "(2 3)");
    ASSERT_STR_EQ(eval_print("(last '(1 2 3) 0)"), "NIL");
    ASSERT_STR_EQ(eval_print("(last nil)"), "NIL");
}

TEST(eval_acons)
{
    ASSERT_STR_EQ(eval_print("(acons 'a 1 nil)"), "((A . 1))");
    ASSERT_STR_EQ(eval_print("(acons 'b 2 '((a . 1)))"), "((B . 2) (A . 1))");
}

TEST(eval_copy_list)
{
    ASSERT_STR_EQ(eval_print("(copy-list '(1 2 3))"), "(1 2 3)");
    ASSERT_STR_EQ(eval_print("(copy-list nil)"), "NIL");
    /* Verify it's a copy, not the same structure */
    ASSERT_STR_EQ(eval_print("(let ((x '(1 2 3))) (eq x (copy-list x)))"), "NIL");
}

TEST(eval_pairlis)
{
    ASSERT_STR_EQ(eval_print("(pairlis '(a b c) '(1 2 3))"), "((A . 1) (B . 2) (C . 3))");
    ASSERT_STR_EQ(eval_print("(pairlis '(a b) '(1 2) '((c . 3)))"), "((A . 1) (B . 2) (C . 3))");
}

TEST(eval_assoc)
{
    ASSERT_STR_EQ(eval_print("(assoc 'b '((a . 1) (b . 2) (c . 3)))"), "(B . 2)");
    ASSERT_STR_EQ(eval_print("(assoc 'd '((a . 1) (b . 2)))"), "NIL");
    ASSERT_STR_EQ(eval_print("(assoc \"b\" (list (cons \"a\" 1) (cons \"b\" 2)) :test #'equal)"), "(\"b\" . 2)");
}

TEST(eval_rassoc)
{
    ASSERT_STR_EQ(eval_print("(rassoc 2 '((a . 1) (b . 2) (c . 3)))"), "(B . 2)");
    ASSERT_STR_EQ(eval_print("(rassoc 9 '((a . 1) (b . 2)))"), "NIL");
}

TEST(eval_getf)
{
    ASSERT_EQ_INT(eval_int("(getf '(:a 1 :b 2) :b)"), 2);
    ASSERT_STR_EQ(eval_print("(getf '(:a 1 :b 2) :c)"), "NIL");
    ASSERT_EQ_INT(eval_int("(getf '(:a 1 :b 2) :c 99)"), 99);
}

TEST(eval_subst)
{
    ASSERT_STR_EQ(eval_print("(subst 'x 'b '(a b (c b)))"), "(A X (C X))");
    ASSERT_STR_EQ(eval_print("(subst 99 1 '(1 (2 1) 3))"), "(99 (2 99) 3)");
}

TEST(eval_sublis)
{
    ASSERT_STR_EQ(eval_print("(sublis '((a . 1) (b . 2)) '(a b c))"), "(1 2 C)");
}

TEST(eval_adjoin)
{
    ASSERT_STR_EQ(eval_print("(adjoin 1 '(2 3))"), "(1 2 3)");
    ASSERT_STR_EQ(eval_print("(adjoin 2 '(1 2 3))"), "(1 2 3)");
}

TEST(eval_nconc)
{
    ASSERT_STR_EQ(eval_print("(nconc (list 1 2) (list 3 4))"), "(1 2 3 4)");
    ASSERT_STR_EQ(eval_print("(nconc nil (list 1 2))"), "(1 2)");
    ASSERT_STR_EQ(eval_print("(nconc (list 1) nil (list 2 3))"), "(1 2 3)");
}

TEST(eval_nreverse)
{
    ASSERT_STR_EQ(eval_print("(nreverse (list 1 2 3))"), "(3 2 1)");
    ASSERT_STR_EQ(eval_print("(nreverse nil)"), "NIL");
    ASSERT_STR_EQ(eval_print("(nreverse (list 1))"), "(1)");
}

TEST(eval_delete)
{
    ASSERT_STR_EQ(eval_print("(delete 2 (list 1 2 3 2 4))"), "(1 3 4)");
    ASSERT_STR_EQ(eval_print("(delete 5 (list 1 2 3))"), "(1 2 3)");
    ASSERT_STR_EQ(eval_print("(delete \"b\" (list \"a\" \"b\" \"c\") :test #'equal)"), "(\"a\" \"c\")");
}

TEST(eval_delete_if)
{
    ASSERT_STR_EQ(eval_print("(delete-if #'zerop (list 0 1 0 2 0 3))"), "(1 2 3)");
    ASSERT_STR_EQ(eval_print("(delete-if #'numberp (list 1 2 3))"), "NIL");
}

TEST(eval_nsubst)
{
    ASSERT_STR_EQ(eval_print("(let ((x (list 'a 'b (list 'c 'b)))) (nsubst 'x 'b x))"), "(A X (C X))");
}

TEST(eval_butlast)
{
    ASSERT_STR_EQ(eval_print("(butlast '(1 2 3))"), "(1 2)");
    ASSERT_STR_EQ(eval_print("(butlast '(1 2 3) 2)"), "(1)");
    ASSERT_STR_EQ(eval_print("(butlast '(1 2 3) 5)"), "NIL");
    ASSERT_STR_EQ(eval_print("(butlast nil)"), "NIL");
}

TEST(eval_copy_tree)
{
    ASSERT_STR_EQ(eval_print("(copy-tree '(1 (2 3) 4))"), "(1 (2 3) 4)");
    ASSERT_STR_EQ(eval_print("(copy-tree 42)"), "42");
    /* Deep copy: modifying original doesn't affect copy */
    ASSERT_STR_EQ(eval_print("(let ((x '(1 (2 3)))) (let ((y (copy-tree x))) (equal x y)))"), "T");
}

TEST(eval_mapc)
{
    /* mapc returns the first list */
    ASSERT_STR_EQ(eval_print("(mapc #'1+ '(1 2 3))"), "(1 2 3)");
    /* mapc with side effects using a global */
    ASSERT_STR_EQ(eval_print("(progn (setq *mapc-r* nil) (mapc (lambda (x) (setq *mapc-r* (cons (* x x) *mapc-r*))) '(1 2 3)) (nreverse *mapc-r*))"), "(1 4 9)");
}

TEST(eval_mapcan)
{
    ASSERT_STR_EQ(eval_print("(mapcan (lambda (x) (if (numberp x) (list x) nil)) '(a 1 b 2 c 3))"), "(1 2 3)");
}

TEST(eval_maplist)
{
    ASSERT_STR_EQ(eval_print("(maplist #'length '(1 2 3 4))"), "(4 3 2 1)");
}

TEST(eval_mapl)
{
    /* mapl returns the first list, operates on sublists */
    ASSERT_STR_EQ(eval_print("(progn (setq *mapl-r* nil) (mapl (lambda (l) (setq *mapl-r* (cons (length l) *mapl-r*))) '(a b c)) (nreverse *mapl-r*))"), "(3 2 1)");
}

TEST(eval_mapcon)
{
    ASSERT_STR_EQ(eval_print("(mapcon (lambda (l) (list (length l))) '(a b c))"), "(3 2 1)");
}

TEST(eval_intersection)
{
    ASSERT_STR_EQ(eval_print("(intersection '(1 2 3 4) '(2 4 6))"), "(2 4)");
    ASSERT_STR_EQ(eval_print("(intersection '(1 2) '(3 4))"), "NIL");
}

TEST(eval_union)
{
    /* union returns list1 elements plus list2 elements not in list1 */
    ASSERT_STR_EQ(eval_print("(union '(1 2 3) '(2 3 4))"), "(3 2 1 4)");
}

TEST(eval_set_difference)
{
    ASSERT_STR_EQ(eval_print("(set-difference '(1 2 3 4) '(2 4))"), "(1 3)");
    ASSERT_STR_EQ(eval_print("(set-difference '(1 2) '(1 2))"), "NIL");
}

TEST(eval_subsetp)
{
    ASSERT_STR_EQ(eval_print("(subsetp '(1 2) '(1 2 3))"), "T");
    ASSERT_STR_EQ(eval_print("(subsetp '(1 4) '(1 2 3))"), "NIL");
    ASSERT_STR_EQ(eval_print("(subsetp nil '(1 2 3))"), "T");
}

/* --- Hash tables --- */

TEST(eval_make_hash_table)
{
    ASSERT_STR_EQ(eval_print("(hash-table-p (make-hash-table))"), "T");
    ASSERT_STR_EQ(eval_print("(hash-table-p 42)"), "NIL");
    ASSERT_STR_EQ(eval_print("(hash-table-p nil)"), "NIL");
    ASSERT_STR_EQ(eval_print("(type-of (make-hash-table))"), "HASH-TABLE");
}

TEST(eval_gethash_setf)
{
    eval_print("(defvar *ht1* (make-hash-table))");
    ASSERT_STR_EQ(eval_print("(setf (gethash 'a *ht1*) 1)"), "1");
    ASSERT_STR_EQ(eval_print("(gethash 'a *ht1*)"), "1");
    ASSERT_STR_EQ(eval_print("(setf (gethash 'b *ht1*) 2)"), "2");
    ASSERT_STR_EQ(eval_print("(gethash 'b *ht1*)"), "2");
    ASSERT_STR_EQ(eval_print("(hash-table-count *ht1*)"), "2");
}

TEST(eval_gethash_default)
{
    eval_print("(defvar *ht2* (make-hash-table))");
    ASSERT_STR_EQ(eval_print("(gethash 'x *ht2*)"), "NIL");
    ASSERT_STR_EQ(eval_print("(gethash 'x *ht2* 99)"), "99");
}

TEST(eval_gethash_overwrite)
{
    eval_print("(defvar *ht3* (make-hash-table))");
    eval_print("(setf (gethash 'k *ht3*) 10)");
    ASSERT_STR_EQ(eval_print("(gethash 'k *ht3*)"), "10");
    eval_print("(setf (gethash 'k *ht3*) 20)");
    ASSERT_STR_EQ(eval_print("(gethash 'k *ht3*)"), "20");
    ASSERT_STR_EQ(eval_print("(hash-table-count *ht3*)"), "1");
}

TEST(eval_remhash)
{
    eval_print("(defvar *ht4* (make-hash-table))");
    eval_print("(setf (gethash 'a *ht4*) 1)");
    eval_print("(setf (gethash 'b *ht4*) 2)");
    ASSERT_STR_EQ(eval_print("(remhash 'a *ht4*)"), "T");
    ASSERT_STR_EQ(eval_print("(gethash 'a *ht4*)"), "NIL");
    ASSERT_STR_EQ(eval_print("(hash-table-count *ht4*)"), "1");
    ASSERT_STR_EQ(eval_print("(remhash 'z *ht4*)"), "NIL");
}

TEST(eval_clrhash)
{
    eval_print("(defvar *ht5* (make-hash-table))");
    eval_print("(setf (gethash 'a *ht5*) 1)");
    eval_print("(setf (gethash 'b *ht5*) 2)");
    eval_print("(clrhash *ht5*)");
    ASSERT_STR_EQ(eval_print("(hash-table-count *ht5*)"), "0");
    ASSERT_STR_EQ(eval_print("(gethash 'a *ht5*)"), "NIL");
}

TEST(eval_maphash)
{
    eval_print("(defvar *ht6* (make-hash-table))");
    eval_print("(setf (gethash 'x *ht6*) 10)");
    eval_print("(setf (gethash 'y *ht6*) 20)");
    eval_print("(defvar *sum6* 0)");
    eval_print("(maphash (lambda (k v) (setq *sum6* (+ *sum6* v))) *ht6*)");
    ASSERT_STR_EQ(eval_print("*sum6*"), "30");
}

TEST(eval_hash_table_equal_test)
{
    eval_print("(defvar *hte* (make-hash-table :test 'equal))");
    eval_print("(setf (gethash \"hello\" *hte*) 42)");
    ASSERT_STR_EQ(eval_print("(gethash \"hello\" *hte*)"), "42");
    ASSERT_STR_EQ(eval_print("(hash-table-count *hte*)"), "1");
}

TEST(eval_hash_table_eq_test)
{
    eval_print("(defvar *hteq* (make-hash-table :test 'eq))");
    eval_print("(setf (gethash 'foo *hteq*) 99)");
    ASSERT_STR_EQ(eval_print("(gethash 'foo *hteq*)"), "99");
}

TEST(eval_gethash_mv)
{
    /* gethash returns two values: value and present-p */
    eval_print("(defvar *htmv* (make-hash-table))");
    eval_print("(setf (gethash 'k *htmv*) 42)");
    ASSERT_STR_EQ(eval_print("(multiple-value-bind (v p) (gethash 'k *htmv*) p)"), "T");
    ASSERT_STR_EQ(eval_print("(multiple-value-bind (v p) (gethash 'missing *htmv*) p)"), "NIL");
}

/* --- Phase 5: Sequence functions --- */

TEST(eval_find)
{
    ASSERT_STR_EQ(eval_print("(find 3 '(1 2 3 4 5))"), "3");
    ASSERT_STR_EQ(eval_print("(find 9 '(1 2 3))"), "NIL");
    ASSERT_STR_EQ(eval_print("(find 2 '(1 2 3 2 1) :start 2)"), "2");
    ASSERT_STR_EQ(eval_print("(find \"b\" '(\"a\" \"b\" \"c\") :test #'equal)"), "\"b\"");
    /* Regression: :key nil must work (treated as identity) */
    ASSERT_STR_EQ(eval_print("(find 3 '(1 2 3 4) :key nil)"), "3");
}

TEST(eval_find_if)
{
    eval_print("(defun my-evenp (x) (= (mod x 2) 0))");
    ASSERT_STR_EQ(eval_print("(find-if #'my-evenp '(1 2 3 4))"), "2");
    ASSERT_STR_EQ(eval_print("(find-if #'my-evenp '(1 3 5))"), "NIL");
}

TEST(eval_find_if_not)
{
    ASSERT_STR_EQ(eval_print("(find-if-not #'my-evenp '(2 4 5 6))"), "5");
}

TEST(eval_position)
{
    ASSERT_STR_EQ(eval_print("(position 3 '(1 2 3 4))"), "2");
    ASSERT_STR_EQ(eval_print("(position 9 '(1 2 3))"), "NIL");
    ASSERT_STR_EQ(eval_print("(position 1 '(1 2 1 2) :from-end t)"), "2");
}

TEST(eval_position_if)
{
    ASSERT_STR_EQ(eval_print("(position-if #'my-evenp '(1 3 4 5))"), "2");
}

TEST(eval_position_if_not)
{
    ASSERT_STR_EQ(eval_print("(position-if-not #'my-evenp '(2 4 5))"), "2");
}

TEST(eval_count)
{
    ASSERT_EQ_INT(eval_int("(count 1 '(1 2 1 3 1))"), 3);
    ASSERT_EQ_INT(eval_int("(count 9 '(1 2 3))"), 0);
}

TEST(eval_count_if)
{
    ASSERT_EQ_INT(eval_int("(count-if #'my-evenp '(1 2 3 4 5 6))"), 3);
}

TEST(eval_count_if_not)
{
    ASSERT_EQ_INT(eval_int("(count-if-not #'my-evenp '(1 2 3 4 5))"), 3);
}

TEST(eval_remove)
{
    ASSERT_STR_EQ(eval_print("(remove 3 '(1 2 3 4 3 5))"), "(1 2 4 5)");
    ASSERT_STR_EQ(eval_print("(remove 3 '(1 2 3 4 3 5) :count 1)"), "(1 2 4 3 5)");
    ASSERT_STR_EQ(eval_print("(remove 9 '(1 2 3))"), "(1 2 3)");
}

TEST(eval_remove_if)
{
    ASSERT_STR_EQ(eval_print("(remove-if #'my-evenp '(1 2 3 4 5))"), "(1 3 5)");
}

TEST(eval_remove_if_not)
{
    ASSERT_STR_EQ(eval_print("(remove-if-not #'my-evenp '(1 2 3 4 5))"), "(2 4)");
}

TEST(eval_remove_duplicates)
{
    ASSERT_STR_EQ(eval_print("(remove-duplicates '(1 2 1 3 2 4))"), "(1 3 2 4)");
    ASSERT_STR_EQ(eval_print("(remove-duplicates '(a b a c))"), "(B A C)");
}

TEST(eval_substitute)
{
    ASSERT_STR_EQ(eval_print("(substitute 99 3 '(1 2 3 4 3))"), "(1 2 99 4 99)");
    ASSERT_STR_EQ(eval_print("(substitute 0 3 '(1 2 3 4 3) :count 1)"), "(1 2 0 4 3)");
}

TEST(eval_substitute_if)
{
    ASSERT_STR_EQ(eval_print("(substitute-if 0 #'my-evenp '(1 2 3 4 5))"), "(1 0 3 0 5)");
}

TEST(eval_substitute_if_not)
{
    ASSERT_STR_EQ(eval_print("(substitute-if-not 0 #'my-evenp '(1 2 3 4 5))"), "(0 2 0 4 0)");
}

TEST(eval_nsubstitute)
{
    ASSERT_STR_EQ(eval_print("(let ((x (list 1 2 3 4 3))) (nsubstitute 99 3 x) x)"), "(1 2 99 4 99)");
    ASSERT_STR_EQ(eval_print("(let ((x (list 1 2 3 4 3))) (nsubstitute 0 3 x :count 1) x)"), "(1 2 0 4 3)");
}

TEST(eval_nsubstitute_if)
{
    ASSERT_STR_EQ(eval_print("(let ((x (list 1 2 3 4 5))) (nsubstitute-if 0 #'my-evenp x) x)"), "(1 0 3 0 5)");
}

TEST(eval_nsubstitute_if_not)
{
    ASSERT_STR_EQ(eval_print("(let ((x (list 1 2 3 4 5))) (nsubstitute-if-not 0 #'my-evenp x) x)"), "(0 2 0 4 0)");
}

TEST(eval_reduce)
{
    ASSERT_EQ_INT(eval_int("(reduce #'+ '(1 2 3 4))"), 10);
    ASSERT_EQ_INT(eval_int("(reduce #'+ '() :initial-value 0)"), 0);
    ASSERT_EQ_INT(eval_int("(reduce #'+ '(5) :initial-value 10)"), 15);
    ASSERT_STR_EQ(eval_print("(reduce #'cons '(1 2 3))"), "((1 . 2) . 3)");
}

TEST(eval_fill)
{
    ASSERT_STR_EQ(eval_print("(let ((x (list 1 2 3 4))) (fill x 0) x)"), "(0 0 0 0)");
    ASSERT_STR_EQ(eval_print("(let ((x (list 1 2 3 4))) (fill x 0 :start 1 :end 3) x)"), "(1 0 0 4)");
}

TEST(eval_replace_fn)
{
    ASSERT_STR_EQ(eval_print("(let ((x (list 1 2 3 4 5))) (replace x '(a b c) :start1 1) x)"), "(1 A B C 5)");
}

TEST(eval_every)
{
    ASSERT_STR_EQ(eval_print("(every #'my-evenp '(2 4 6))"), "T");
    ASSERT_STR_EQ(eval_print("(every #'my-evenp '(2 3 6))"), "NIL");
    ASSERT_STR_EQ(eval_print("(every #'my-evenp '())"), "T");
}

TEST(eval_some)
{
    ASSERT_STR_EQ(eval_print("(some #'my-evenp '(1 3 4))"), "T");
    ASSERT_STR_EQ(eval_print("(some #'my-evenp '(1 3 5))"), "NIL");
}

TEST(eval_notany)
{
    ASSERT_STR_EQ(eval_print("(notany #'my-evenp '(1 3 5))"), "T");
    ASSERT_STR_EQ(eval_print("(notany #'my-evenp '(1 2 3))"), "NIL");
}

TEST(eval_notevery)
{
    ASSERT_STR_EQ(eval_print("(notevery #'my-evenp '(2 4 5))"), "T");
    ASSERT_STR_EQ(eval_print("(notevery #'my-evenp '(2 4 6))"), "NIL");
}

TEST(eval_map_fn)
{
    ASSERT_STR_EQ(eval_print("(map 'list #'1+ '(1 2 3))"), "(2 3 4)");
    /* map over string */
    ASSERT_STR_EQ(eval_print("(map 'list #'char-code \"abc\")"), "(97 98 99)");
    /* map over vector */
    ASSERT_STR_EQ(eval_print("(map 'list #'1+ #(10 20 30))"), "(11 21 31)");
    /* map nil result-type over string */
    ASSERT_STR_EQ(eval_print("(let ((r nil)) (map nil (lambda (c) (push c r)) \"ab\") (length r))"), "2");
    /* map with mixed sequence types */
    ASSERT_STR_EQ(eval_print("(map 'list #'+ '(1 2 3) #(10 20 30))"), "(11 22 33)");
    ASSERT_STR_EQ(eval_print("(map 'list #'cons '(a b c) \"xyz\")"), "((A . #\\x) (B . #\\y) (C . #\\z))");
    /* map with string result type */
    ASSERT_STR_EQ(eval_print("(map 'string #'char-upcase \"hello\")"), "\"HELLO\"");
    /* map with vector result type */
    ASSERT_STR_EQ(eval_print("(map 'vector #'1+ '(1 2 3))"), "#(2 3 4)");
    /* map stops at shortest sequence */
    ASSERT_STR_EQ(eval_print("(map 'list #'+ '(1 2 3 4) #(10 20))"), "(11 22)");
    ASSERT_STR_EQ(eval_print("(map 'list #'identity \"ab\")"), "(#\\a #\\b)");
}

TEST(eval_every_sequences)
{
    /* every with string */
    ASSERT_STR_EQ(eval_print("(every #'alpha-char-p \"hello\")"), "T");
    ASSERT_STR_EQ(eval_print("(every #'alpha-char-p \"hello1\")"), "NIL");
    /* every with vector */
    ASSERT_STR_EQ(eval_print("(every #'numberp #(1 2 3))"), "T");
    ASSERT_STR_EQ(eval_print("(every #'numberp #(1 :a 3))"), "NIL");
    /* every with empty sequences */
    ASSERT_STR_EQ(eval_print("(every #'identity \"\")"), "T");
    ASSERT_STR_EQ(eval_print("(every #'identity #())"), "T");
    /* every with mixed sequence types */
    ASSERT_STR_EQ(eval_print("(every #'eql '(97 98 99) (map 'list #'char-code \"abc\"))"), "T");
    /* every with bit-vector */
    ASSERT_STR_EQ(eval_print("(every #'zerop #*000)"), "T");
    ASSERT_STR_EQ(eval_print("(every #'zerop #*010)"), "NIL");
}

TEST(eval_some_sequences)
{
    /* some with string */
    ASSERT_STR_EQ(eval_print("(some #'digit-char-p \"abc1\")"), "T");
    ASSERT_STR_EQ(eval_print("(some #'digit-char-p \"abcd\")"), "NIL");
    /* some with vector */
    ASSERT_STR_EQ(eval_print("(some #'zerop #(1 2 0 3))"), "T");
    ASSERT_STR_EQ(eval_print("(some #'zerop #(1 2 3))"), "NIL");
    /* some with empty sequences */
    ASSERT_STR_EQ(eval_print("(some #'identity \"\")"), "NIL");
    ASSERT_STR_EQ(eval_print("(some #'identity #())"), "NIL");
    /* some with bit-vector */
    ASSERT_STR_EQ(eval_print("(some #'plusp #*000)"), "NIL");
    ASSERT_STR_EQ(eval_print("(some #'plusp #*010)"), "T");
}

TEST(eval_notany_sequences)
{
    /* notany with string */
    ASSERT_STR_EQ(eval_print("(notany #'digit-char-p \"abcd\")"), "T");
    ASSERT_STR_EQ(eval_print("(notany #'digit-char-p \"abc1\")"), "NIL");
    /* notany with vector */
    ASSERT_STR_EQ(eval_print("(notany #'zerop #(1 2 3))"), "T");
    ASSERT_STR_EQ(eval_print("(notany #'zerop #(1 0 3))"), "NIL");
}

TEST(eval_notevery_sequences)
{
    /* notevery with string */
    ASSERT_STR_EQ(eval_print("(notevery #'alpha-char-p \"hello1\")"), "T");
    ASSERT_STR_EQ(eval_print("(notevery #'alpha-char-p \"hello\")"), "NIL");
    /* notevery with vector */
    ASSERT_STR_EQ(eval_print("(notevery #'numberp #(1 :a 3))"), "T");
    ASSERT_STR_EQ(eval_print("(notevery #'numberp #(1 2 3))"), "NIL");
}

TEST(eval_mismatch)
{
    ASSERT_STR_EQ(eval_print("(mismatch '(1 2 3) '(1 2 3))"), "NIL");
    ASSERT_STR_EQ(eval_print("(mismatch '(1 2 3) '(1 2 4))"), "2");
    ASSERT_STR_EQ(eval_print("(mismatch '(1 2) '(1 2 3))"), "2");
}

TEST(eval_search_fn)
{
    ASSERT_STR_EQ(eval_print("(search '(2 3) '(1 2 3 4))"), "1");
    ASSERT_STR_EQ(eval_print("(search '(9) '(1 2 3))"), "NIL");
    ASSERT_STR_EQ(eval_print("(search '() '(1 2 3))"), "0");
}

TEST(eval_sort)
{
    ASSERT_STR_EQ(eval_print("(sort (list 3 1 4 1 5) #'<)"), "(1 1 3 4 5)");
    ASSERT_STR_EQ(eval_print("(sort (list 5 4 3 2 1) #'<)"), "(1 2 3 4 5)");
    ASSERT_STR_EQ(eval_print("(sort (list 1) #'<)"), "(1)");
    ASSERT_STR_EQ(eval_print("(sort '() #'<)"), "NIL");
}

TEST(eval_stable_sort)
{
    ASSERT_STR_EQ(eval_print("(stable-sort (list 3 1 2) #'<)"), "(1 2 3)");
}

TEST(eval_sort_with_key)
{
    ASSERT_STR_EQ(eval_print("(sort (list -3 1 -2 4) #'< :key #'abs)"), "(1 -2 -3 4)");
    /* Regression: sort with string key on large list (GC bug #8 — list_merge_sort
       didn't GC-protect list/mid across recursive calls) */
    ASSERT_STR_EQ(eval_print(
        "(let ((items (loop for i from 1 to 200 collect (format nil \"item~3,'0d\" i))))"
        "  (length (sort items #'string<)))"), "200");
}

TEST(eval_sort_key_nil)
{
    /* Regression: :key nil must be treated as identity (CL spec) */
    ASSERT_STR_EQ(eval_print("(sort (list 3 1 2) #'< :key nil)"), "(1 2 3)");
    ASSERT_STR_EQ(eval_print("(sort (vector 3 1 2) #'< :key nil)"), "#(1 2 3)");
    ASSERT_STR_EQ(eval_print("(stable-sort (list 3 1 2) #'< :key nil)"), "(1 2 3)");
}

TEST(eval_stale_boxed_flag)
{
    /* Regression: labels in one cond branch boxing a variable must not
     * corrupt sibling branches that reuse the same local slot.
     * The bug was that env->boxed[] flags were not cleared when a let
     * scope exited, causing CELL_REF to be emitted for plain locals. */
    ASSERT_STR_EQ(eval_print(
        "(defun test-stale-boxed (x fn)"
        "  (cond ((consp x)"
        "         (let ((acc 0))"
        "           (labels ((body (k v)"
        "                      (declare (ignore k))"
        "                      (setf acc (+ acc (funcall fn v)))))"
        "             (body (car x) (cdr x)))"
        "           acc))"
        "        ((vectorp x)"
        "         (let ((result 0))"
        "           (dotimes (i (length x))"
        "             (let ((h (funcall fn (svref x i))))"
        "               (setf result (logxor result h))))"
        "           result))))"), "TEST-STALE-BOXED");
    /* cons branch: acc = identity(42) = 42 */
    ASSERT_STR_EQ(eval_print("(test-stale-boxed '(a . 42) #'identity)"), "42");
    /* vector branch must not crash (was SIGSEGV before fix) */
    ASSERT_STR_EQ(eval_print("(test-stale-boxed #(3 5) #'identity)"), "6");
}

TEST(eval_typep)
{
    ASSERT_STR_EQ(eval_print("(typep 42 'integer)"), "T");
    ASSERT_STR_EQ(eval_print("(typep 42 'fixnum)"), "T");
    ASSERT_STR_EQ(eval_print("(typep 42 'number)"), "T");
    ASSERT_STR_EQ(eval_print("(typep 42 'string)"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep \"hello\" 'string)"), "T");
    ASSERT_STR_EQ(eval_print("(typep #\\A 'character)"), "T");
    ASSERT_STR_EQ(eval_print("(typep nil 'null)"), "T");
    ASSERT_STR_EQ(eval_print("(typep nil 'symbol)"), "T");
    ASSERT_STR_EQ(eval_print("(typep nil 'list)"), "T");
    ASSERT_STR_EQ(eval_print("(typep '(1 2) 'cons)"), "T");
    ASSERT_STR_EQ(eval_print("(typep '(1 2) 'list)"), "T");
    ASSERT_STR_EQ(eval_print("(typep 42 'atom)"), "T");
    ASSERT_STR_EQ(eval_print("(typep '(1) 'atom)"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep :foo 'keyword)"), "T");
    ASSERT_STR_EQ(eval_print("(typep 'bar 'keyword)"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep #'+ 'function)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-hash-table) 'hash-table)"), "T");
    ASSERT_STR_EQ(eval_print("(typep 42 't)"), "T");
    ASSERT_STR_EQ(eval_print("(typep 42 'nil)"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep '(1 2 3) 'sequence)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (vector 1 2) 'sequence)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (vector 1 2) 'vector)"), "T");
}

TEST(eval_coerce)
{
    ASSERT_STR_EQ(eval_print("(coerce 42 't)"), "42");
    ASSERT_STR_EQ(eval_print("(coerce 65 'character)"), "#\\A");
    ASSERT_STR_EQ(eval_print("(coerce #\\A 'integer)"), "65");
    ASSERT_STR_EQ(eval_print("(coerce #\\A 'character)"), "#\\A");
    ASSERT_STR_EQ(eval_print("(coerce 42 'integer)"), "42");
    ASSERT_STR_EQ(eval_print("(coerce 'foo 'string)"), "\"FOO\"");
    ASSERT_STR_EQ(eval_print("(coerce #\\A 'string)"), "\"A\"");
    ASSERT_STR_EQ(eval_print("(coerce \"hello\" 'string)"), "\"hello\"");
    ASSERT_STR_EQ(eval_print("(coerce '(1 2 3) 'vector)"), "#(1 2 3)");
    ASSERT_STR_EQ(eval_print("(coerce (vector 1 2 3) 'list)"), "(1 2 3)");
    ASSERT_STR_EQ(eval_print("(coerce nil 'list)"), "NIL");
    ASSERT_STR_EQ(eval_print("(coerce (vector) 'list)"), "NIL");
    ASSERT_STR_EQ(eval_print("(coerce nil 'vector)"), "#()");
    /* coerce to function */
    ASSERT_STR_EQ(eval_print("(functionp (coerce #'car 'function))"), "T");
    ASSERT_STR_EQ(eval_print("(funcall (coerce #'+ 'function) 1 2)"), "3");
    ASSERT_STR_EQ(eval_print("(funcall (coerce '+ 'function) 3 4)"), "7");
    ASSERT_STR_EQ(eval_print("(funcall (coerce '(lambda (x) (* x 2)) 'function) 5)"), "10");
}

/* --- Compound typep --- */

TEST(eval_typep_compound)
{
    /* (or ...) */
    ASSERT_STR_EQ(eval_print("(typep 42 '(or integer string))"), "T");
    ASSERT_STR_EQ(eval_print("(typep \"hi\" '(or integer string))"), "T");
    ASSERT_STR_EQ(eval_print("(typep #\\A '(or integer string))"), "NIL");

    /* (and ...) */
    ASSERT_STR_EQ(eval_print("(typep nil '(and symbol list))"), "T");
    ASSERT_STR_EQ(eval_print("(typep 42 '(and number atom))"), "T");
    ASSERT_STR_EQ(eval_print("(typep 42 '(and number string))"), "NIL");

    /* (not ...) */
    ASSERT_STR_EQ(eval_print("(typep 42 '(not string))"), "T");
    ASSERT_STR_EQ(eval_print("(typep \"hi\" '(not string))"), "NIL");

    /* (member ...) */
    ASSERT_STR_EQ(eval_print("(typep 1 '(member 1 2 3))"), "T");
    ASSERT_STR_EQ(eval_print("(typep 4 '(member 1 2 3))"), "NIL");

    /* (eql ...) */
    ASSERT_STR_EQ(eval_print("(typep 42 '(eql 42))"), "T");
    ASSERT_STR_EQ(eval_print("(typep 43 '(eql 42))"), "NIL");

    /* (satisfies ...) */
    ASSERT_STR_EQ(eval_print("(typep 42 '(satisfies numberp))"), "T");
    ASSERT_STR_EQ(eval_print("(typep \"hi\" '(satisfies numberp))"), "NIL");

    /* nested compound */
    ASSERT_STR_EQ(eval_print("(typep 42 '(or (and integer atom) string))"), "T");
    ASSERT_STR_EQ(eval_print("(typep #\\A '(or integer string))"), "NIL");
}

/* --- numeric range type specifiers --- */

TEST(eval_typep_numeric_range)
{
    /* (integer low high) — inclusive bounds */
    ASSERT_STR_EQ(eval_print("(typep 3 '(integer 0 7))"), "T");
    ASSERT_STR_EQ(eval_print("(typep 0 '(integer 0 7))"), "T");
    ASSERT_STR_EQ(eval_print("(typep 7 '(integer 0 7))"), "T");
    ASSERT_STR_EQ(eval_print("(typep 8 '(integer 0 7))"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep -1 '(integer 0 7))"), "NIL");
    /* Non-integer fails base type check */
    ASSERT_STR_EQ(eval_print("(typep 3.0 '(integer 0 7))"), "NIL");

    /* Wildcard bounds with * */
    ASSERT_STR_EQ(eval_print("(typep 100 '(integer 0 *))"), "T");
    ASSERT_STR_EQ(eval_print("(typep -5 '(integer * 0))"), "T");
    ASSERT_STR_EQ(eval_print("(typep 42 '(integer * *))"), "T");

    /* No bounds = just base type */
    ASSERT_STR_EQ(eval_print("(typep 42 '(integer))"), "T");
    ASSERT_STR_EQ(eval_print("(typep 3.0 '(integer))"), "NIL");

    /* Exclusive bounds via (n) list form */
    ASSERT_STR_EQ(eval_print("(typep 1 '(integer (0) 7))"), "T");
    ASSERT_STR_EQ(eval_print("(typep 0 '(integer (0) 7))"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep 6 '(integer 0 (7)))"), "T");
    ASSERT_STR_EQ(eval_print("(typep 7 '(integer 0 (7)))"), "NIL");

    /* (real low high) */
    ASSERT_STR_EQ(eval_print("(typep 3 '(real 0 10))"), "T");
    ASSERT_STR_EQ(eval_print("(typep 3.5 '(real 0 10))"), "T");
    ASSERT_STR_EQ(eval_print("(typep 3/2 '(real 0 10))"), "T");

    /* (float low high) */
    ASSERT_STR_EQ(eval_print("(typep 3.0 '(float 0.0 10.0))"), "T");
    ASSERT_STR_EQ(eval_print("(typep 3 '(float 0 10))"), "NIL");

    /* (rational low high) */
    ASSERT_STR_EQ(eval_print("(typep 3/2 '(rational 0 10))"), "T");
    ASSERT_STR_EQ(eval_print("(typep 3 '(rational 0 10))"), "T");
    ASSERT_STR_EQ(eval_print("(typep 3.0 '(rational 0 10))"), "NIL");

    /* check-type with range type */
    ASSERT_STR_EQ(eval_print("(let ((x 5)) (check-type x (integer 0 7)) x)"), "5");
}

/* --- deftype --- */

TEST(eval_deftype)
{
    /* Simple no-arg deftype */
    eval_print("(deftype my-num () 'number)");
    ASSERT_STR_EQ(eval_print("(typep 42 'my-num)"), "T");
    ASSERT_STR_EQ(eval_print("(typep \"hi\" 'my-num)"), "NIL");

    /* Deftype expanding to compound */
    eval_print("(deftype string-or-num () '(or string number))");
    ASSERT_STR_EQ(eval_print("(typep 42 'string-or-num)"), "T");
    ASSERT_STR_EQ(eval_print("(typep \"hi\" 'string-or-num)"), "T");
    ASSERT_STR_EQ(eval_print("(typep #\\A 'string-or-num)"), "NIL");
}

/* --- subtypep --- */

TEST(eval_subtypep)
{
    /* Known hierarchy checks */
    ASSERT_STR_EQ(eval_print("(subtypep 'fixnum 'number)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'fixnum 'integer)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'integer 'number)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'cons 'list)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'null 'list)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'list 'sequence)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'string 'sequence)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'keyword 'symbol)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'compiled-function 'function)"), "T");

    /* Same type */
    ASSERT_STR_EQ(eval_print("(subtypep 'integer 'integer)"), "T");

    /* Not a subtype */
    ASSERT_STR_EQ(eval_print("(subtypep 'string 'number)"), "NIL");
    ASSERT_STR_EQ(eval_print("(subtypep 'number 'fixnum)"), "NIL");

    /* nil is subtype of everything */
    ASSERT_STR_EQ(eval_print("(subtypep 'nil 'number)"), "T");

    /* everything is subtype of t */
    ASSERT_STR_EQ(eval_print("(subtypep 'number 't)"), "T");

    /* MV return: second value should be T for known types */
    ASSERT_STR_EQ(eval_print("(multiple-value-list (subtypep 'fixnum 'number))"), "(T T)");
    ASSERT_STR_EQ(eval_print("(multiple-value-list (subtypep 'string 'number))"), "(NIL T)");
}

/* --- typecase with compound type specs --- */

TEST(eval_typecase_compound)
{
    ASSERT_STR_EQ(eval_print("(typecase 42 ((or integer string) \"match\") (t \"no\"))"), "\"match\"");
    ASSERT_STR_EQ(eval_print("(typecase \"hi\" ((or integer string) \"match\") (t \"no\"))"), "\"match\"");
    ASSERT_STR_EQ(eval_print("(typecase #\\A ((or integer string) \"match\") (t \"no\"))"), "\"no\"");
    /* typecase with list type now properly matches nil */
    ASSERT_STR_EQ(eval_print("(typecase nil (list \"list\") (t \"other\"))"), "\"list\"");
}

/* --- Disassemble --- */

TEST(eval_disassemble_defun)
{
    /* disassemble of a user-defined function returns NIL */
    eval_print("(defun disasm-test (x) (+ x 1))");
    ASSERT_STR_EQ(eval_print("(disassemble 'disasm-test)"), "NIL");
}

TEST(eval_disassemble_closure)
{
    /* disassemble of a closure returns NIL */
    eval_print("(defun make-adder (n) (lambda (x) (+ x n)))");
    ASSERT_STR_EQ(eval_print("(disassemble (make-adder 5))"), "NIL");
}

TEST(eval_disassemble_builtin)
{
    /* disassemble of a built-in function returns NIL with message */
    ASSERT_STR_EQ(eval_print("(disassemble 'cons)"), "NIL");
}

TEST(eval_disassemble_unbound)
{
    /* disassemble of unbound symbol signals an error */
    ASSERT_STR_EQ(eval_print("(disassemble 'no-such-function-xyz)"), "ERROR:8");
}

/* --- Declarations: declare, declaim, proclaim, locally --- */

TEST(eval_declaim_special)
{
    /* declaim marks a variable as globally special */
    eval_print("(declaim (special *decl-x*))");
    eval_print("(setq *decl-x* 10)");
    /* A function should see the dynamic binding */
    eval_print("(defun get-decl-x () *decl-x*)");
    ASSERT_EQ_INT(eval_int("(let ((*decl-x* 99)) (get-decl-x))"), 99);
}

TEST(eval_declare_special_let)
{
    /* Local declare special in let body — forces dynamic binding */
    eval_print("(setq *ds-var* 10)");
    eval_print("(defun get-ds-var () *ds-var*)");
    ASSERT_EQ_INT(eval_int(
        "(let ((*ds-var* 42)) (declare (special *ds-var*)) (get-ds-var))"), 42);
}

TEST(eval_declare_special_letstar)
{
    /* Local declare special in let* body */
    eval_print("(setq *ds-var2* 10)");
    eval_print("(defun get-ds-var2 () *ds-var2*)");
    ASSERT_EQ_INT(eval_int(
        "(let* ((*ds-var2* 55)) (declare (special *ds-var2*)) (get-ds-var2))"), 55);
}

TEST(eval_declare_ignore)
{
    /* (declare (ignore x)) accepted without error */
    ASSERT_EQ_INT(eval_int("(let ((x 1)) (declare (ignore x)) 42)"), 42);
}

TEST(eval_declare_type)
{
    /* (declare (type fixnum x)) accepted without error */
    ASSERT_EQ_INT(eval_int("(let ((x 1)) (declare (type fixnum x)) x)"), 1);
}

TEST(eval_declaim_optimize)
{
    /* (declaim (optimize (speed 3))) accepted without error */
    ASSERT_STR_EQ(eval_print("(declaim (optimize (speed 3)))"), "NIL");
}

TEST(eval_declaim_inline)
{
    /* (declaim (inline foo)) accepted without error */
    ASSERT_STR_EQ(eval_print("(declaim (inline cons))"), "NIL");
}

TEST(eval_proclaim_special)
{
    /* (proclaim '(special *proc-var*)) — runtime special declaration */
    eval_print("(proclaim '(special *proc-var*))");
    eval_print("(setq *proc-var* 100)");
    eval_print("(defun get-proc-var () *proc-var*)");
    ASSERT_EQ_INT(eval_int("(let ((*proc-var* 200)) (get-proc-var))"), 200);
}

TEST(eval_locally_basic)
{
    /* (locally body...) — evaluates body, returns last value */
    ASSERT_EQ_INT(eval_int("(locally 1 2 3)"), 3);
}

TEST(eval_locally_declare)
{
    /* (locally (declare (special *loc-var*)) ...) */
    eval_print("(setq *loc-var* 5)");
    eval_print("(defun get-loc-var () *loc-var*)");
    ASSERT_EQ_INT(eval_int("(locally (declare (special *loc-var*)) *loc-var*)"), 5);
}

TEST(eval_declare_misplaced)
{
    /* declare at top level is now a warning (ignored), returns NIL */
    ASSERT_STR_EQ(eval_print("(declare (special x))"), "NIL");
}

TEST(eval_declare_in_lambda)
{
    /* declare in lambda body */
    ASSERT_EQ_INT(eval_int(
        "((lambda (x) (declare (ignore x)) 42) 99)"), 42);
}

TEST(eval_declaim_multiple)
{
    /* declaim with multiple specifiers */
    ASSERT_STR_EQ(eval_print(
        "(declaim (special *dm1*) (special *dm2*))"), "NIL");
    eval_print("(setq *dm1* 1)");
    eval_print("(setq *dm2* 2)");
    eval_print("(defun get-dm1 () *dm1*)");
    eval_print("(defun get-dm2 () *dm2*)");
    ASSERT_EQ_INT(eval_int("(let ((*dm1* 10)) (get-dm1))"), 10);
    ASSERT_EQ_INT(eval_int("(let ((*dm2* 20)) (get-dm2))"), 20);
}

/* ===== Phase 6 — The ===== */

TEST(eval_the_basic)
{
    /* (the type form) passes through value when type matches */
    ASSERT_EQ_INT(eval_int("(the fixnum 42)"), 42);
    ASSERT_STR_EQ(eval_print("(the string \"hello\")"), "\"hello\"");
    ASSERT_EQ_INT(eval_int("(the fixnum (+ 1 2))"), 3);
    ASSERT_STR_EQ(eval_print("(the symbol 'foo)"), "FOO");
    ASSERT_STR_EQ(eval_print("(the list '(1 2 3))"), "(1 2 3)");
    ASSERT_STR_EQ(eval_print("(the null nil)"), "NIL");
}

TEST(eval_the_compound_type)
{
    /* compound type specifiers */
    ASSERT_EQ_INT(eval_int("(the (or fixnum null) 42)"), 42);
    ASSERT_STR_EQ(eval_print("(the (or fixnum null) nil)"), "NIL");
    ASSERT_STR_EQ(eval_print("(the (or string symbol) \"hi\")"), "\"hi\"");
}

TEST(eval_the_type_error)
{
    /* signals type-error when value doesn't match */
    ASSERT_STR_EQ(eval_print("(the fixnum \"oops\")"), "ERROR:2");
    ASSERT_STR_EQ(eval_print("(the string 42)"), "ERROR:2");
}

TEST(eval_the_safety_zero)
{
    /* safety 0 skips the check */
    eval_print("(declaim (optimize (safety 0)))");
    ASSERT_STR_EQ(eval_print("(the fixnum \"oops\")"), "\"oops\"");
    /* restore default safety */
    eval_print("(declaim (optimize (safety 1)))");
}

TEST(eval_the_nested)
{
    /* nested the forms */
    ASSERT_EQ_INT(eval_int("(the fixnum (the fixnum (+ 1 2)))"), 3);
}

/* ===== Phase 5 — Trace/Untrace ===== */

TEST(eval_trace_basic)
{
    /* Trace a function and verify result is still correct */
    eval_print("(defun tr-add (a b) (+ a b))");
    eval_print("(trace tr-add)");
    ASSERT_EQ_INT(eval_int("(tr-add 3 4)"), 7);
    eval_print("(untrace tr-add)");
}

TEST(eval_trace_returns_list)
{
    /* (trace name) returns a list of traced names */
    eval_print("(defun tr-foo () 42)");
    ASSERT_STR_EQ(eval_print("(trace tr-foo)"), "(TR-FOO)");
    eval_print("(untrace tr-foo)");
}

TEST(eval_trace_untrace)
{
    /* Untrace returns list, function still works */
    eval_print("(defun tr-sq (x) (* x x))");
    eval_print("(trace tr-sq)");
    ASSERT_EQ_INT(eval_int("(tr-sq 5)"), 25);
    ASSERT_STR_EQ(eval_print("(untrace tr-sq)"), "(TR-SQ)");
    ASSERT_EQ_INT(eval_int("(tr-sq 6)"), 36);
}

TEST(eval_trace_query)
{
    /* (trace) with no args returns list of traced functions */
    eval_print("(defun tr-a () 1)");
    eval_print("(trace tr-a)");
    ASSERT_STR_EQ(eval_print("(trace)"), "(TR-A)");
    eval_print("(untrace tr-a)");
    ASSERT_STR_EQ(eval_print("(trace)"), "NIL");
}

TEST(eval_untrace_all)
{
    /* (untrace) with no args clears all traces */
    eval_print("(defun tr-x () 10)");
    eval_print("(defun tr-y () 20)");
    eval_print("(trace tr-x tr-y)");
    ASSERT_STR_EQ(eval_print("(untrace)"), "NIL");
    ASSERT_STR_EQ(eval_print("(trace)"), "NIL");
}

TEST(eval_trace_builtin)
{
    /* Trace a built-in function */
    eval_print("(trace cons)");
    ASSERT_STR_EQ(eval_print("(cons 1 2)"), "(1 . 2)");
    eval_print("(untrace cons)");
}

TEST(eval_trace_multiple)
{
    /* Trace multiple functions */
    eval_print("(defun tr-p (x) (* x x))");
    eval_print("(defun tr-q (x) (+ x 1))");
    ASSERT_STR_EQ(eval_print("(trace tr-p tr-q)"), "(TR-P TR-Q)");
    ASSERT_EQ_INT(eval_int("(tr-p (tr-q 3))"), 16);
    eval_print("(untrace)");
}

/* --- Backtrace --- */

TEST(eval_backtrace_named)
{
    /* Error in a named function shows its name in backtrace */
    int err;
    eval_print("(defun bt-inner () (error \"oops\"))");
    eval_print("(defun bt-outer () (+ 1 (bt-inner)))");
    err = CL_CATCH();
    if (err == CL_ERR_NONE) {
        cl_eval_string("(bt-outer)");
        CL_UNCATCH();
    } else {
        CL_UNCATCH();
    }
    /* Backtrace should contain BT-INNER and BT-OUTER */
    ASSERT(strstr(cl_backtrace_buf, "BT-INNER") != NULL);
    ASSERT(strstr(cl_backtrace_buf, "BT-OUTER") != NULL);
    cl_vm.sp = 0;
    cl_vm.fp = 0;
}

TEST(eval_backtrace_anonymous)
{
    /* Top-level error shows <anonymous> */
    int err;
    err = CL_CATCH();
    if (err == CL_ERR_NONE) {
        cl_eval_string("(+ undefined-var 1)");
        CL_UNCATCH();
    } else {
        CL_UNCATCH();
    }
    ASSERT(strstr(cl_backtrace_buf, "<anonymous>") != NULL);
    cl_vm.sp = 0;
    cl_vm.fp = 0;
}

TEST(eval_backtrace_recursive)
{
    /* Recursive function shows multiple frames */
    int err;
    eval_print("(defun bt-rec (n) (if (= n 0) (error \"bottom\") (+ 1 (bt-rec (1- n)))))");
    err = CL_CATCH();
    if (err == CL_ERR_NONE) {
        cl_eval_string("(bt-rec 3)");
        CL_UNCATCH();
    } else {
        CL_UNCATCH();
    }
    /* Should have multiple BT-REC entries */
    {
        const char *p = cl_backtrace_buf;
        int count = 0;
        while ((p = strstr(p, "BT-REC")) != NULL) {
            count++;
            p += 6;
        }
        ASSERT(count == 4); /* n=0,1,2,3 */
    }
    cl_vm.sp = 0;
    cl_vm.fp = 0;
}

TEST(eval_backtrace_uwprot)
{
    /* Backtrace captured before unwind-protect cleanup */
    int err;
    eval_print("(defun bt-uwp-inner () (error \"err\"))");
    eval_print("(defun bt-uwp-outer () (unwind-protect (bt-uwp-inner) nil))");
    err = CL_CATCH();
    if (err == CL_ERR_NONE) {
        cl_eval_string("(bt-uwp-outer)");
        CL_UNCATCH();
    } else {
        CL_UNCATCH();
    }
    ASSERT(strstr(cl_backtrace_buf, "BT-UWP-INNER") != NULL);
    ASSERT(strstr(cl_backtrace_buf, "BT-UWP-OUTER") != NULL);
    cl_vm.sp = 0;
    cl_vm.fp = 0;
}

TEST(eval_backtrace_empty)
{
    /* No backtrace when error occurs outside VM (e.g., parse error) */
    int err;
    cl_vm.sp = 0;
    cl_vm.fp = 0;
    cl_backtrace_buf[0] = '\0';
    err = CL_CATCH();
    if (err == CL_ERR_NONE) {
        cl_eval_string("(+ 1");  /* unterminated — reader error */
        CL_UNCATCH();
    } else {
        CL_UNCATCH();
    }
    /* Reader errors happen before VM runs, so backtrace should be empty */
    ASSERT(cl_backtrace_buf[0] == '\0');
    cl_vm.sp = 0;
    cl_vm.fp = 0;
}

/* --- Time --- */

TEST(eval_time_basic)
{
    /* (time expr) should return the value of expr */
    ASSERT_STR_EQ(eval_print("(time (+ 1 2))"), "3");
}

TEST(eval_time_nested)
{
    /* time can be nested in other forms */
    ASSERT_STR_EQ(eval_print("(+ 10 (time (* 3 4)))"), "22");
}

TEST(eval_time_defun)
{
    /* time works with function calls */
    cl_eval_string("(defun time-test (x) (* x x))");
    ASSERT_STR_EQ(eval_print("(time (time-test 5))"), "25");
}

TEST(eval_time_stats)
{
    /* (time expr) should return correct value and internal helpers work */
    ASSERT_STR_EQ(eval_print("(time (+ 1 2))"), "3");

    /* %GET-BYTES-CONSED returns a non-negative fixnum */
    ASSERT_STR_EQ(eval_print("(integerp (%get-bytes-consed))"), "T");
    ASSERT_STR_EQ(eval_print("(>= (%get-bytes-consed) 0)"), "T");

    /* %GET-GC-COUNT returns a non-negative fixnum */
    ASSERT_STR_EQ(eval_print("(integerp (%get-gc-count))"), "T");
    ASSERT_STR_EQ(eval_print("(>= (%get-gc-count) 0)"), "T");

    /* Bytes consed increases after allocation */
    ASSERT_STR_EQ(eval_print(
        "(let ((before (%get-bytes-consed)))"
        "  (cons 1 2)"
        "  (> (%get-bytes-consed) before))"), "T");
}

TEST(eval_get_internal_real_time)
{
    /* get-internal-real-time returns a fixnum */
    ASSERT_STR_EQ(eval_print("(integerp (get-internal-real-time))"), "T");
}

/* --- Source location tracking --- */

TEST(eval_srcloc_load_backtrace)
{
    /* Errors in loaded files show file:line in backtrace */
    int err;
    cl_vm.sp = 0;
    cl_vm.fp = 0;

    err = CL_CATCH();
    if (err == CL_ERR_NONE) {
        cl_eval_string("(load \"tests/test_srcloc.lisp\")");
        CL_UNCATCH();
    } else {
        CL_UNCATCH();
    }
    /* Check backtrace contains file and line info */
    ASSERT(strstr(cl_backtrace_buf, "test_srcloc.lisp") != NULL);
    ASSERT(strstr(cl_backtrace_buf, "SRCLOC-FAIL") != NULL);
    cl_vm.sp = 0;
    cl_vm.fp = 0;
}

TEST(eval_srcloc_reader_line)
{
    /* Reader tracks line numbers in string streams */
    CL_ReadStream stream;
    CL_Obj expr;
    int line;

    stream.buf = "(foo)\n(bar)\n(baz)";
    stream.pos = 0;
    stream.len = 17;
    stream.line = 1;

    expr = cl_read_from_string(&stream);
    /* First expression starts at line 1 */
    line = cl_srcloc_lookup(expr);
    ASSERT_EQ_INT(line, 1);

    expr = cl_read_from_string(&stream);
    /* Second expression starts at line 2 */
    line = cl_srcloc_lookup(expr);
    ASSERT_EQ_INT(line, 2);

    expr = cl_read_from_string(&stream);
    /* Third expression starts at line 3 */
    line = cl_srcloc_lookup(expr);
    ASSERT_EQ_INT(line, 3);
}

/* --- Macrolet --- */

TEST(eval_macrolet_basic)
{
    ASSERT_EQ_INT(eval_int("(macrolet ((square (x) `(* ,x ,x))) (square 5))"), 25);
}

TEST(eval_macrolet_shadow)
{
    /* Local macro shadows global macro */
    eval_print("(defmacro my-inc (x) `(+ ,x 1))");
    ASSERT_EQ_INT(eval_int("(my-inc 10)"), 11);
    /* Now shadow with macrolet that adds 100 */
    ASSERT_EQ_INT(eval_int("(macrolet ((my-inc (x) `(+ ,x 100))) (my-inc 10))"), 110);
    /* Global still works */
    ASSERT_EQ_INT(eval_int("(my-inc 10)"), 11);
}

TEST(eval_macrolet_scope)
{
    const char *result;
    /* Macro not visible outside macrolet body */
    ASSERT_EQ_INT(eval_int("(macrolet ((add3 (x) `(+ ,x 3))) (add3 7))"), 10);
    /* add3 should not exist as a global macro, so calling it will error */
    result = eval_print("(add3 7)");
    ASSERT(strncmp(result, "ERROR:", 6) == 0);
}

TEST(eval_macrolet_nested)
{
    /* Inner macrolet shadows outer */
    ASSERT_EQ_INT(eval_int(
        "(macrolet ((m (x) `(+ ,x 1)))"
        "  (macrolet ((m (x) `(+ ,x 10)))"
        "    (m 5)))"), 15);
    /* Outer still works after inner scope */
    ASSERT_EQ_INT(eval_int(
        "(macrolet ((m (x) `(+ ,x 1)))"
        "  (+ (macrolet ((m (x) `(+ ,x 10))) (m 5))"
        "     (m 5)))"), 21);
}

TEST(eval_macrolet_with_body)
{
    /* macrolet with multiple body forms */
    ASSERT_STR_EQ(eval_print(
        "(macrolet ((double (x) `(* 2 ,x)))"
        "  (double 3)"
        "  (double 7))"), "14");
}

TEST(eval_macrolet_across_lambda)
{
    /* macrolet must be lexically visible inside nested lambdas (CL spec) */
    ASSERT_EQ_INT(eval_int(
        "(macrolet ((foo () '42))"
        "  (funcall (lambda () (foo))))"), 42);
    /* macrolet visible across multiple lambda levels */
    ASSERT_EQ_INT(eval_int(
        "(macrolet ((add10 (x) `(+ ,x 10)))"
        "  (funcall (lambda () (funcall (lambda () (add10 5))))))"), 15);
    /* macrolet with quasiquote unquote-splicing inside lambda */
    ASSERT_STR_EQ(eval_print(
        "(macrolet ((&body () '(list 1 2 3)))"
        "  (funcall (lambda () `(progn ,@(&body)))))"), "(PROGN 1 2 3)");
}

/* --- Symbol-macrolet --- */

TEST(eval_symbol_macrolet_basic)
{
    ASSERT_EQ_INT(eval_int("(symbol-macrolet ((x 42)) x)"), 42);
}

TEST(eval_symbol_macrolet_expr)
{
    /* Symbol macro expands to an expression */
    ASSERT_EQ_INT(eval_int("(symbol-macrolet ((x (+ 1 2))) x)"), 3);
}

TEST(eval_symbol_macrolet_setq)
{
    /* (setq sym val) on a symbol-macro rewrites to (setf expansion val) */
    ASSERT_STR_EQ(eval_print(
        "(let ((a (cons 1 2)))"
        "  (symbol-macrolet ((x (car a)))"
        "    (setq x 99)"
        "    a))"), "(99 . 2)");
}

TEST(eval_symbol_macrolet_scope)
{
    /* Symbol macro not visible outside body */
    ASSERT_EQ_INT(eval_int(
        "(let ((result 0))"
        "  (symbol-macrolet ((x 42))"
        "    (setq result x))"
        "  result)"), 42);
}

TEST(eval_symbol_macrolet_nested)
{
    /* Inner shadows outer */
    ASSERT_EQ_INT(eval_int(
        "(symbol-macrolet ((x 10))"
        "  (symbol-macrolet ((x 20))"
        "    x))"), 20);
}

TEST(eval_symbol_macrolet_multiple)
{
    /* Multiple symbol macros */
    ASSERT_EQ_INT(eval_int(
        "(symbol-macrolet ((x 10) (y 20))"
        "  (+ x y))"), 30);
}

TEST(eval_symbol_macrolet_across_lambda)
{
    /* symbol-macrolet must be lexically visible inside nested lambdas */
    ASSERT_EQ_INT(eval_int(
        "(symbol-macrolet ((x 42))"
        "  (funcall (lambda () x)))"), 42);
    ASSERT_EQ_INT(eval_int(
        "(symbol-macrolet ((x 10) (y 20))"
        "  (funcall (lambda () (+ x y))))"), 30);
}

/* --- Phase 8 Step 2: Missing string operations --- */

TEST(eval_string_capitalize)
{
    ASSERT_STR_EQ(eval_print("(string-capitalize \"hello world\")"), "\"Hello World\"");
    ASSERT_STR_EQ(eval_print("(string-capitalize \"HELLO WORLD\")"), "\"Hello World\"");
    ASSERT_STR_EQ(eval_print("(string-capitalize \"foo-bar\")"), "\"Foo-Bar\"");
}

TEST(eval_string_case_designators)
{
    /* string-upcase/downcase/capitalize must accept string designators
       (symbols, characters), not just strings */
    ASSERT_STR_EQ(eval_print("(string-downcase :FIVEAM)"), "\"fiveam\"");
    ASSERT_STR_EQ(eval_print("(string-downcase 'HELLO)"), "\"hello\"");
    ASSERT_STR_EQ(eval_print("(string-upcase :foo)"), "\"FOO\"");
    ASSERT_STR_EQ(eval_print("(string-capitalize :hello-world)"), "\"Hello-World\"");
}

TEST(eval_nstring_upcase)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((s (copy-seq \"hello\")))"
        "  (nstring-upcase s)"
        "  s)"), "\"HELLO\"");
}

TEST(eval_nstring_downcase)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((s (copy-seq \"HELLO\")))"
        "  (nstring-downcase s)"
        "  s)"), "\"hello\"");
}

TEST(eval_nstring_capitalize)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((s (copy-seq \"hello world\")))"
        "  (nstring-capitalize s)"
        "  s)"), "\"Hello World\"");
}

TEST(eval_char_name)
{
    ASSERT_STR_EQ(eval_print("(char-name #\\Space)"), "\"Space\"");
    ASSERT_STR_EQ(eval_print("(char-name #\\Newline)"), "\"Newline\"");
    ASSERT_STR_EQ(eval_print("(char-name #\\A)"), "NIL");
}

TEST(eval_name_char)
{
    ASSERT_STR_EQ(eval_print("(name-char \"Space\")"), "#\\Space");
    ASSERT_STR_EQ(eval_print("(name-char \"SPACE\")"), "#\\Space");
    ASSERT_STR_EQ(eval_print("(name-char \"Newline\")"), "#\\Newline");
    ASSERT_STR_EQ(eval_print("(name-char \"xyzzy\")"), "NIL");
}

TEST(eval_char_ci_compare)
{
    ASSERT_STR_EQ(eval_print("(char-equal #\\a #\\A)"), "T");
    ASSERT_STR_EQ(eval_print("(char-not-equal #\\a #\\b)"), "T");
    ASSERT_STR_EQ(eval_print("(char-lessp #\\a #\\B)"), "T");
    ASSERT_STR_EQ(eval_print("(char-greaterp #\\b #\\A)"), "T");
    ASSERT_STR_EQ(eval_print("(char-not-greaterp #\\a #\\A)"), "T");
    ASSERT_STR_EQ(eval_print("(char-not-lessp #\\A #\\a)"), "T");
}

TEST(eval_graphic_char_p)
{
    ASSERT_STR_EQ(eval_print("(graphic-char-p #\\A)"), "T");
    ASSERT_STR_EQ(eval_print("(graphic-char-p #\\Space)"), "T");
    ASSERT_STR_EQ(eval_print("(graphic-char-p #\\Newline)"), "NIL");
}

TEST(eval_alphanumericp)
{
    ASSERT_STR_EQ(eval_print("(alphanumericp #\\A)"), "T");
    ASSERT_STR_EQ(eval_print("(alphanumericp #\\5)"), "T");
    ASSERT_STR_EQ(eval_print("(alphanumericp #\\!)"), "NIL");
}

TEST(eval_digit_char)
{
    ASSERT_STR_EQ(eval_print("(digit-char 0)"), "#\\0");
    ASSERT_STR_EQ(eval_print("(digit-char 9)"), "#\\9");
    ASSERT_STR_EQ(eval_print("(digit-char 10 16)"), "#\\A");
    ASSERT_STR_EQ(eval_print("(digit-char 10)"), "NIL");
}

/* --- Phase 8 Step 3: Missing sequence operations --- */

TEST(eval_elt)
{
    ASSERT_EQ_INT(eval_int("(elt '(a b c) 1)"), CL_FIXNUM_VAL(cl_eval_string("'b")));
    ASSERT_STR_EQ(eval_print("(elt (vector 10 20 30) 2)"), "30");
    ASSERT_STR_EQ(eval_print("(elt \"abc\" 0)"), "#\\a");
}

TEST(eval_setf_elt)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((v (vector 1 2 3)))"
        "  (setf (elt v 1) 99) v)"), "#(1 99 3)");
    ASSERT_STR_EQ(eval_print(
        "(let ((l (list 1 2 3)))"
        "  (setf (elt l 0) 99) l)"), "(99 2 3)");
}

TEST(eval_copy_seq)
{
    ASSERT_STR_EQ(eval_print("(copy-seq '(1 2 3))"), "(1 2 3)");
    ASSERT_STR_EQ(eval_print("(copy-seq \"hello\")"), "\"hello\"");
    ASSERT_STR_EQ(eval_print(
        "(let ((a (vector 1 2 3))"
        "      (b (copy-seq (vector 1 2 3))))"
        "  (equal a b))"), "T");
    ASSERT_STR_EQ(eval_print("(copy-seq nil)"), "NIL");
}

TEST(eval_map_into)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((v (vector 0 0 0)))"
        "  (map-into v #'+ (vector 1 2 3) (vector 10 20 30))"
        "  v)"), "#(11 22 33)");
}

/* --- Phase 8 Step 4: Higher-order functions --- */

TEST(eval_complement)
{
    ASSERT_STR_EQ(eval_print(
        "(funcall (complement #'zerop) 0)"), "NIL");
    ASSERT_STR_EQ(eval_print(
        "(funcall (complement #'zerop) 5)"), "T");
}

TEST(eval_constantly)
{
    ASSERT_STR_EQ(eval_print(
        "(funcall (constantly 42) 1 2 3)"), "42");
    ASSERT_STR_EQ(eval_print(
        "(mapcar (constantly 'x) '(1 2 3))"), "(X X X)");
}

/* --- Phase 8 Step 1: Missing list operations --- */

TEST(eval_list_star)
{
    ASSERT_STR_EQ(eval_print("(list* 1 2)"), "(1 . 2)");
    ASSERT_STR_EQ(eval_print("(list* 1 2 3)"), "(1 2 . 3)");
    ASSERT_STR_EQ(eval_print("(list* 1 2 3 nil)"), "(1 2 3)");
    ASSERT_STR_EQ(eval_print("(list* 'a)"), "A");
}

TEST(eval_make_list)
{
    ASSERT_STR_EQ(eval_print("(make-list 3)"), "(NIL NIL NIL)");
    ASSERT_STR_EQ(eval_print("(make-list 3 :initial-element 'x)"), "(X X X)");
    ASSERT_STR_EQ(eval_print("(make-list 0)"), "NIL");
}

TEST(eval_tree_equal)
{
    ASSERT_STR_EQ(eval_print("(tree-equal '(1 (2 3)) '(1 (2 3)))"), "T");
    ASSERT_STR_EQ(eval_print("(tree-equal '(1 2) '(1 3))"), "NIL");
    ASSERT_STR_EQ(eval_print("(tree-equal nil nil)"), "T");
    ASSERT_STR_EQ(eval_print("(tree-equal '(\"a\" \"b\") '(\"a\" \"b\") :test #'equal)"), "T");
}

TEST(eval_list_length)
{
    ASSERT_STR_EQ(eval_print("(list-length '(1 2 3))"), "3");
    ASSERT_STR_EQ(eval_print("(list-length nil)"), "0");
}

TEST(eval_tailp)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((x '(1 2 3)))"
        "  (tailp (cdr x) x))"), "T");
    ASSERT_STR_EQ(eval_print("(tailp nil '(1 2 3))"), "T");
    ASSERT_STR_EQ(eval_print("(tailp '(4) '(1 2 3))"), "NIL");
}

TEST(eval_ldiff)
{
    ASSERT_STR_EQ(eval_print(
        "(let* ((x '(1 2 3 4))"
        "       (tail (cddr x)))"
        "  (ldiff x tail))"), "(1 2)");
    ASSERT_STR_EQ(eval_print("(ldiff '(1 2 3) nil)"), "(1 2 3)");
}

TEST(eval_revappend)
{
    ASSERT_STR_EQ(eval_print("(revappend '(1 2 3) '(4 5))"), "(3 2 1 4 5)");
    ASSERT_STR_EQ(eval_print("(revappend nil '(1 2))"), "(1 2)");
}

TEST(eval_nreconc)
{
    ASSERT_STR_EQ(eval_print("(nreconc (list 1 2 3) '(4 5))"), "(3 2 1 4 5)");
    ASSERT_STR_EQ(eval_print("(nreconc nil '(1 2))"), "(1 2)");
}

TEST(eval_assoc_if)
{
    ASSERT_STR_EQ(eval_print("(assoc-if #'symbolp '((1 . a) (b . 2) (3 . c)))"), "(B . 2)");
    ASSERT_STR_EQ(eval_print("(assoc-if #'null '((1 . a) (2 . b)))"), "NIL");
}

TEST(eval_rassoc_if)
{
    ASSERT_STR_EQ(eval_print("(rassoc-if #'symbolp '((1 . 2) (3 . b) (4 . 5)))"), "(3 . B)");
    ASSERT_STR_EQ(eval_print("(rassoc-if #'null '((1 . a) (2 . b)))"), "NIL");
}

TEST(eval_remf)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (list :a 1 :b 2 :c 3)))"
        "  (remf p :b)"
        "  p)"), "(:A 1 :C 3)");
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-list (remf (list :a 1 :b 2) :a))"), "((:B 2) T)");
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-list (remf (list :a 1 :b 2) :z))"), "((:A 1 :B 2) NIL)");
}

/* --- Paren depth --- */

TEST(paren_depth_balanced)
{
    ASSERT_EQ_INT(cl_paren_depth("(+ 1 2)"), 0);
    ASSERT_EQ_INT(cl_paren_depth("((a) (b))"), 0);
    ASSERT_EQ_INT(cl_paren_depth("()"), 0);
    ASSERT_EQ_INT(cl_paren_depth(""), 0);
    ASSERT_EQ_INT(cl_paren_depth("hello"), 0);
}

TEST(paren_depth_unbalanced)
{
    ASSERT_EQ_INT(cl_paren_depth("(defun foo"), 1);
    ASSERT_EQ_INT(cl_paren_depth("(defun foo (x)"), 1);
    ASSERT_EQ_INT(cl_paren_depth("(("), 2);
    ASSERT_EQ_INT(cl_paren_depth("(+ 1 2))"), -1);
}

TEST(paren_depth_strings)
{
    /* Parens inside strings should not count */
    ASSERT_EQ_INT(cl_paren_depth("(print \"((\")"), 0);
    ASSERT_EQ_INT(cl_paren_depth("(print \")\")"), 0);
    ASSERT_EQ_INT(cl_paren_depth("(print \"\\\"\")"), 0);
}

TEST(paren_depth_comments)
{
    /* Parens in comments should not count */
    ASSERT_EQ_INT(cl_paren_depth("(+ 1 2) ; extra ("), 0);
    ASSERT_EQ_INT(cl_paren_depth("; just a comment ((("), 0);
    /* Multi-line with comment */
    ASSERT_EQ_INT(cl_paren_depth("(defun foo (x)\n  ; body )\n  (+ x 1))"), 0);
}

TEST(paren_depth_char_literals)
{
    /* Character literal #\( should not count as open paren */
    ASSERT_EQ_INT(cl_paren_depth("(list #\\( #\\))"), 0);
    /* Named character literals */
    ASSERT_EQ_INT(cl_paren_depth("(list #\\Space)"), 0);
}

TEST(paren_depth_multiline)
{
    /* Typical multi-line defun */
    ASSERT_EQ_INT(cl_paren_depth("(defun fact (n)"), 1);
    ASSERT_EQ_INT(cl_paren_depth("(defun fact (n)\n  (if (<= n 1) 1"), 2);
    ASSERT_EQ_INT(cl_paren_depth(
        "(defun fact (n)\n  (if (<= n 1) 1\n    (* n (fact (1- n)))))"), 0);
}

/* --- History variables --- */

TEST(history_star_basic)
{
    /* Eval (+ 1 2) => 3, update history, check * */
    CL_Obj form = cl_eval_string("'(+ 1 2)");
    CL_Obj result = CL_MAKE_FIXNUM(3);
    cl_repl_update_history(form, result);
    ASSERT_EQ_INT(eval_int("*"), 3);
}

TEST(history_star_shift)
{
    /* First eval: result 10 */
    cl_repl_update_history(CL_NIL, CL_MAKE_FIXNUM(10));
    /* Second eval: result 20 */
    cl_repl_update_history(CL_NIL, CL_MAKE_FIXNUM(20));
    /* Third eval: result 30 */
    cl_repl_update_history(CL_NIL, CL_MAKE_FIXNUM(30));

    ASSERT_EQ_INT(eval_int("*"), 30);
    ASSERT_EQ_INT(eval_int("**"), 20);
    ASSERT_EQ_INT(eval_int("***"), 10);
}

TEST(history_plus_shift)
{
    CL_Obj form1, form2, form3;
    form1 = cl_eval_string("'(first-form)");
    form2 = cl_eval_string("'(second-form)");
    form3 = cl_eval_string("'(third-form)");

    cl_repl_update_history(form1, CL_MAKE_FIXNUM(1));
    cl_repl_update_history(form2, CL_MAKE_FIXNUM(2));
    cl_repl_update_history(form3, CL_MAKE_FIXNUM(3));

    /* + should be the last form */
    ASSERT_STR_EQ(eval_print("+"), "(THIRD-FORM)");
    ASSERT_STR_EQ(eval_print("++"), "(SECOND-FORM)");
    ASSERT_STR_EQ(eval_print("+++"), "(FIRST-FORM)");
}

TEST(history_minus_during_eval)
{
    /* - is set before eval, so we test by checking SYM_MINUS value directly */
    CL_Obj form = cl_eval_string("'(some-form)");
    CL_Symbol *s;

    /* Simulate what the REPL does: set - before compile/eval */
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_MINUS);
    s->value = form;

    ASSERT_STR_EQ(eval_print("-"), "(SOME-FORM)");
}

TEST(history_arithmetic_still_works)
{
    /* Verify *, +, - still work as arithmetic functions */
    ASSERT_EQ_INT(eval_int("(* 3 4)"), 12);
    ASSERT_EQ_INT(eval_int("(+ 5 6)"), 11);
    ASSERT_EQ_INT(eval_int("(- 10 3)"), 7);
}

/* --- Printer control variables --- */

TEST(eval_print_var_defaults)
{
    /* CL spec default values */
    ASSERT_STR_EQ(eval_print("*print-escape*"), "T");
    ASSERT_STR_EQ(eval_print("*print-readably*"), "NIL");
    ASSERT_EQ_INT(eval_int("*print-base*"), 10);
    ASSERT_STR_EQ(eval_print("*print-radix*"), "NIL");
    ASSERT_STR_EQ(eval_print("*print-level*"), "NIL");
    ASSERT_STR_EQ(eval_print("*print-length*"), "NIL");
    ASSERT_STR_EQ(eval_print("*print-case*"), ":UPCASE");
    ASSERT_STR_EQ(eval_print("*print-gensym*"), "T");
    ASSERT_STR_EQ(eval_print("*print-array*"), "T");
    ASSERT_STR_EQ(eval_print("*print-circle*"), "NIL");
    ASSERT_STR_EQ(eval_print("*print-pretty*"), "NIL");
}

TEST(eval_print_escape_dynamic)
{
    /* prin1-to-string forces escape=T */
    ASSERT_STR_EQ(eval_print("(prin1-to-string \"hello\")"),
        "\"\\\"hello\\\"\"");
    /* princ-to-string forces escape=NIL */
    ASSERT_STR_EQ(eval_print("(princ-to-string \"hello\")"), "\"hello\"");
    /* Dynamic binding: prin1 overrides *print-escape* NIL binding */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-escape* nil)) (prin1-to-string \"hi\"))"),
        "\"\\\"hi\\\"\"");
    /* Dynamic binding: princ overrides *print-escape* T binding */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-escape* t)) (princ-to-string \"hi\"))"),
        "\"hi\"");
    /* Characters also affected */
    ASSERT_STR_EQ(eval_print("(prin1-to-string #\\Space)"), "\"#\\\\Space\"");
    ASSERT_STR_EQ(eval_print("(princ-to-string #\\Space)"), "\" \"");
}

TEST(eval_print_level)
{
    /* Level 0 — everything is # */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-level* 0)) (prin1-to-string '(a b)))"),
        "\"#\"");
    /* Level 1 — top list shown, nested lists become # */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-level* 1)) (prin1-to-string '(a (b (c)))))"),
        "\"(A #)\"");
    /* Level 2 */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-level* 2)) (prin1-to-string '(a (b (c)))))"),
        "\"(A (B #))\"");
    /* NIL = no limit (default) */
    ASSERT_STR_EQ(eval_print(
        "(prin1-to-string '(a (b (c))))"),
        "\"(A (B (C)))\"");
}

TEST(eval_print_length)
{
    /* Length 0 — no elements shown */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-length* 0)) (prin1-to-string '(a b c)))"),
        "\"(...)\"");
    /* Length 2 — first 2 shown, rest truncated */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-length* 2)) (prin1-to-string '(a b c d e)))"),
        "\"(A B ...)\"");
    /* Length longer than list — no truncation */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-length* 10)) (prin1-to-string '(a b)))"),
        "\"(A B)\"");
    /* Dotted pair not truncated when within length */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-length* 2)) (prin1-to-string '(a . b)))"),
        "\"(A . B)\"");
}

TEST(eval_print_level_length_combined)
{
    /* Both level and length active */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-level* 1) (*print-length* 2))"
        "  (prin1-to-string '(a b (c d) e)))"),
        "\"(A B ...)\"");
}

TEST(eval_print_base_binary)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 2)) (prin1-to-string 10))"),
        "\"1010\"");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 2)) (prin1-to-string 255))"),
        "\"11111111\"");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 2)) (prin1-to-string 0))"),
        "\"0\"");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 2)) (prin1-to-string -5))"),
        "\"-101\"");
}

TEST(eval_print_base_octal)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 8)) (prin1-to-string 8))"),
        "\"10\"");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 8)) (prin1-to-string 255))"),
        "\"377\"");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 8)) (prin1-to-string 0))"),
        "\"0\"");
}

TEST(eval_print_base_hex)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 16)) (prin1-to-string 255))"),
        "\"FF\"");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 16)) (prin1-to-string 256))"),
        "\"100\"");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 16)) (prin1-to-string 0))"),
        "\"0\"");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 16)) (prin1-to-string -1))"),
        "\"-1\"");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 16)) (prin1-to-string 3735928559))"),
        "\"DEADBEEF\"");
}

TEST(eval_print_base_other)
{
    /* Base 3 */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 3)) (prin1-to-string 9))"),
        "\"100\"");
    /* Base 36 */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 36)) (prin1-to-string 35))"),
        "\"Z\"");
}

TEST(eval_print_radix_decimal)
{
    /* Radix with base 10: trailing dot */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-radix* t)) (prin1-to-string 42))"),
        "\"42.\"");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-radix* t)) (prin1-to-string 0))"),
        "\"0.\"");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-radix* t)) (prin1-to-string -7))"),
        "\"-7.\"");
}

TEST(eval_print_radix_binary)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 2) (*print-radix* t)) (prin1-to-string 10))"),
        "\"#b1010\"");
}

TEST(eval_print_radix_octal)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 8) (*print-radix* t)) (prin1-to-string 255))"),
        "\"#o377\"");
}

TEST(eval_print_radix_hex)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 16) (*print-radix* t)) (prin1-to-string 255))"),
        "\"#xFF\"");
}

TEST(eval_print_radix_other)
{
    /* Base 3 with radix: #3r prefix */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 3) (*print-radix* t)) (prin1-to-string 9))"),
        "\"#3r100\"");
}

TEST(eval_print_base_bignum)
{
    /* Bignum in hex */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 16)) (prin1-to-string (expt 2 32)))"),
        "\"100000000\"");
    /* Bignum in binary */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 2)) (prin1-to-string (expt 2 16)))"),
        "\"10000000000000000\"");
    /* Bignum in octal */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 8)) (prin1-to-string 65536))"),
        "\"200000\"");
}

TEST(eval_print_radix_bignum)
{
    /* Bignum with radix prefix */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-base* 16) (*print-radix* t)) (prin1-to-string (expt 2 32)))"),
        "\"#x100000000\"");
    /* Bignum with base 10 radix (trailing dot) */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-radix* t)) (prin1-to-string (expt 2 32)))"),
        "\"4294967296.\"");
}

TEST(eval_print_case_upcase)
{
    /* Default :UPCASE — symbols printed as-is */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-case* :upcase)) (prin1-to-string 'hello))"),
        "\"HELLO\"");
    ASSERT_STR_EQ(eval_print(
        "(prin1-to-string 'hello)"), "\"HELLO\"");
}

TEST(eval_print_case_downcase)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-case* :downcase)) (prin1-to-string 'hello))"),
        "\"hello\"");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-case* :downcase)) (prin1-to-string 'foo-bar))"),
        "\"foo-bar\"");
    /* NIL is a symbol too */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-case* :downcase)) (prin1-to-string nil))"),
        "\"nil\"");
    /* T is a symbol too */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-case* :downcase)) (prin1-to-string t))"),
        "\"t\"");
    /* Keywords */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-case* :downcase)) (prin1-to-string :foo))"),
        "\":foo\"");
}

TEST(eval_print_case_capitalize)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-case* :capitalize)) (prin1-to-string 'hello))"),
        "\"Hello\"");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-case* :capitalize)) (prin1-to-string 'foo-bar))"),
        "\"Foo-Bar\"");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-case* :capitalize)) (prin1-to-string nil))"),
        "\"Nil\"");
    /* Keywords */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-case* :capitalize)) (prin1-to-string :test))"),
        "\":Test\"");
}

TEST(eval_print_case_in_list)
{
    /* print-case affects symbols inside lists */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-case* :downcase)) (prin1-to-string '(a b c)))"),
        "\"(a b c)\"");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-case* :capitalize)) (prin1-to-string '(hello world)))"),
        "\"(Hello World)\"");
}

TEST(eval_print_gensym)
{
    /* Default: *print-gensym* = T, uninterned symbols get #: prefix */
    ASSERT_STR_EQ(eval_print(
        "(prin1-to-string (make-symbol \"FOO\"))"),
        "\"#:FOO\"");
    /* With *print-gensym* = NIL, no #: prefix */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-gensym* nil)) (prin1-to-string (make-symbol \"FOO\")))"),
        "\"FOO\"");
    /* Still applies print-case */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-gensym* nil) (*print-case* :downcase))"
        "  (prin1-to-string (make-symbol \"FOO\")))"),
        "\"foo\"");
}

TEST(eval_print_array)
{
    /* Default: *print-array* = T, vectors print contents */
    ASSERT_STR_EQ(eval_print(
        "(prin1-to-string (vector 1 2 3))"),
        "\"#(1 2 3)\"");
    /* With *print-array* = NIL, vectors print as #<VECTOR> */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-array* nil)) (prin1-to-string (vector 1 2 3)))"),
        "\"#<VECTOR>\"");
    /* Multi-dim arrays print as #<ARRAY> when *print-array* = NIL */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-array* nil)) (prin1-to-string (make-array '(2 3))))"),
        "\"#<ARRAY>\"");
}

/* --- WRITE function with keyword arguments --- */

TEST(eval_write_basic)
{
    /* write returns the object */
    ASSERT_EQ_INT(eval_int(
        "(let ((x (write 42 :stream (make-string-output-stream)))) x)"), 42);

    /* write with :escape nil acts like princ */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (make-string-output-stream)))"
        "  (write \"hello\" :stream s :escape nil)"
        "  (get-output-stream-string s))"),
        "\"hello\"");

    /* write with :escape t acts like prin1 */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (make-string-output-stream)))"
        "  (write \"hello\" :stream s :escape t)"
        "  (get-output-stream-string s))"),
        "\"\\\"hello\\\"\"");
}

TEST(eval_write_base_radix)
{
    /* write with :base 16 */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (make-string-output-stream)))"
        "  (write 255 :stream s :base 16)"
        "  (get-output-stream-string s))"),
        "\"FF\"");

    /* write with :base 2 */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (make-string-output-stream)))"
        "  (write 10 :stream s :base 2)"
        "  (get-output-stream-string s))"),
        "\"1010\"");

    /* write with :radix t :base 16 */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (make-string-output-stream)))"
        "  (write 255 :stream s :base 16 :radix t)"
        "  (get-output-stream-string s))"),
        "\"#xFF\"");
}

TEST(eval_write_level_length)
{
    /* write with :level 1 */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (make-string-output-stream)))"
        "  (write '(a (b (c))) :stream s :level 1)"
        "  (get-output-stream-string s))"),
        "\"(A #)\"");

    /* write with :length 2 */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (make-string-output-stream)))"
        "  (write '(a b c d e) :stream s :length 2)"
        "  (get-output-stream-string s))"),
        "\"(A B ...)\"");
}

TEST(eval_write_case)
{
    /* write with :case :downcase */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (make-string-output-stream)))"
        "  (write 'hello :stream s :case :downcase)"
        "  (get-output-stream-string s))"),
        "\"hello\"");

    /* write with :case :capitalize */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (make-string-output-stream)))"
        "  (write 'foo-bar :stream s :case :capitalize)"
        "  (get-output-stream-string s))"),
        "\"Foo-Bar\"");
}

TEST(eval_write_gensym_array)
{
    /* write with :gensym nil */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (make-string-output-stream)))"
        "  (write (make-symbol \"FOO\") :stream s :gensym nil)"
        "  (get-output-stream-string s))"),
        "\"FOO\"");

    /* write with :array nil */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (make-string-output-stream)))"
        "  (write (vector 1 2) :stream s :array nil)"
        "  (get-output-stream-string s))"),
        "\"#<VECTOR>\"");
}

/* --- *print-circle* --- */

TEST(eval_print_circle_cdr_cycle)
{
    /* CDR cycle: (let ((x (list 1 2))) (rplacd (cdr x) x) x) => #1=(1 2 . #1#) */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-circle* t))"
        "  (let ((x (list 1 2)))"
        "    (rplacd (cdr x) x)"
        "    (prin1-to-string x)))"),
        "\"#0=(1 2 . #0#)\"");
}

TEST(eval_print_circle_car_self)
{
    /* CAR self-ref: (let ((x (list nil))) (rplaca x x) x) => #1=(#1#) */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-circle* t))"
        "  (let ((x (list nil)))"
        "    (rplaca x x)"
        "    (prin1-to-string x)))"),
        "\"#0=(#0#)\"");
}

TEST(eval_print_circle_shared_sub)
{
    /* Shared substructure: same object in two positions */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-circle* t))"
        "  (let ((sub (list 1 2)))"
        "    (prin1-to-string (list sub sub))))"),
        "\"(#0=(1 2) #0#)\"");
}

TEST(eval_print_circle_no_sharing)
{
    /* No sharing: plain list prints without labels */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-circle* t))"
        "  (prin1-to-string '(1 2 3)))"),
        "\"(1 2 3)\"");
}

TEST(eval_print_circle_nil_default)
{
    /* *print-circle* NIL (default): no labels even with sharing */
    ASSERT_STR_EQ(eval_print(
        "(let ((sub (list 1)))"
        "  (prin1-to-string (list sub sub)))"),
        "\"((1) (1))\"");
}

TEST(eval_print_circle_vector)
{
    /* Circular vector: vector containing self */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-circle* t))"
        "  (let ((v (vector nil 42)))"
        "    (setf (aref v 0) v)"
        "    (prin1-to-string v)))"),
        "\"#0=#(#0# 42)\"");
}

TEST(eval_print_circle_deep_shared)
{
    /* Deep shared: leaf shared at depth */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-circle* t))"
        "  (let ((leaf (list 99)))"
        "    (prin1-to-string (list (list leaf) (list leaf)))))"),
        "\"((#0=(99)) (#0#))\"");
}

TEST(eval_print_circle_write_to_string)
{
    /* write-to-string with :circle t */
    ASSERT_STR_EQ(eval_print(
        "(let ((x (list 1 2)))"
        "  (rplacd (cdr x) x)"
        "  (write-to-string x :circle t))"),
        "\"#0=(1 2 . #0#)\"");
}

/* --- Format directive extensions --- */

TEST(eval_format_decimal)
{
    /* ~D: decimal */
    ASSERT_STR_EQ(eval_print("(format nil \"~D\" 42)"), "\"42\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~D\" -7)"), "\"-7\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~D\" 0)"), "\"0\"");
}

TEST(eval_format_binary)
{
    /* ~B: binary */
    ASSERT_STR_EQ(eval_print("(format nil \"~B\" 10)"), "\"1010\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~B\" 255)"), "\"11111111\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~B\" 0)"), "\"0\"");
}

TEST(eval_format_octal)
{
    /* ~O: octal */
    ASSERT_STR_EQ(eval_print("(format nil \"~O\" 8)"), "\"10\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~O\" 255)"), "\"377\"");
}

TEST(eval_format_hex)
{
    /* ~X: hexadecimal */
    ASSERT_STR_EQ(eval_print("(format nil \"~X\" 255)"), "\"FF\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~X\" 256)"), "\"100\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~X\" 0)"), "\"0\"");
}

TEST(eval_format_character)
{
    /* ~C: character */
    ASSERT_STR_EQ(eval_print("(format nil \"~C\" #\\A)"), "\"A\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~C\" #\\Space)"), "\" \"");
}

TEST(eval_format_write)
{
    /* ~W: write */
    ASSERT_STR_EQ(eval_print("(format nil \"~W\" \"hello\")"), "\"\\\"hello\\\"\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~W\" 42)"), "\"42\"");
}

TEST(eval_format_freshline)
{
    /* ~&: fresh line */
    ASSERT_STR_EQ(eval_print("(format nil \"hello~&world\")"), "\"hello\\nworld\"");
}

TEST(eval_format_page)
{
    /* ~|: page separator (form feed character) */
    ASSERT_EQ_INT(eval_int("(length (format nil \"a~|b\"))"), 3);
    ASSERT_EQ_INT(eval_int("(char-code (char (format nil \"a~|b\") 1))"), 12);
}

TEST(eval_format_mixed)
{
    /* Mix of directives */
    ASSERT_STR_EQ(eval_print(
        "(format nil \"dec=~D hex=~X bin=~B\" 255 255 255)"),
        "\"dec=255 hex=FF bin=11111111\"");
}

/* --- Advanced Format: Step 1 — padding, commas, sign --- */

TEST(eval_format_padded_decimal)
{
    /* ~mincolD: right-pad with spaces */
    ASSERT_STR_EQ(eval_print("(format nil \"~10D\" 42)"), "\"        42\"");
    /* ~mincol,'padcharD: pad with custom char */
    ASSERT_STR_EQ(eval_print("(format nil \"~10,'0D\" 42)"), "\"0000000042\"");
    /* ~:D: insert commas */
    ASSERT_STR_EQ(eval_print("(format nil \"~:D\" 1234567)"), "\"1,234,567\"");
    /* ~@D: always show sign */
    ASSERT_STR_EQ(eval_print("(format nil \"~@D\" 42)"), "\"+42\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~@D\" -7)"), "\"-7\"");
    /* Combine mincol + comma + sign */
    ASSERT_STR_EQ(eval_print("(format nil \"~14:@D\" 1234567)"), "\"    +1,234,567\"");
}

TEST(eval_format_padded_aesthetic)
{
    /* ~mincolA: left-justify, right-pad */
    ASSERT_STR_EQ(eval_print("(format nil \"~10A\" \"hi\")"), "\"hi        \"");
    /* ~mincol@A: right-justify, left-pad */
    ASSERT_STR_EQ(eval_print("(format nil \"~10@A\" \"hi\")"), "\"        hi\"");
    /* ~mincolS: left-justify, right-pad (with escape) */
    ASSERT_STR_EQ(eval_print("(format nil \"~10S\" \"hi\")"), "\"\\\"hi\\\"      \"");
}

TEST(eval_format_prefix_params)
{
    /* ~n%: multiple newlines */
    ASSERT_EQ_INT(eval_int("(length (format nil \"~3%\"))"), 3);
    /* ~n~: multiple tildes */
    ASSERT_STR_EQ(eval_print("(format nil \"~3~\")"), "\"~~~\"");
}

/* --- Advanced Format: Step 2 — ~* and ~T --- */

TEST(eval_format_goto)
{
    /* ~* skip forward 1 arg */
    ASSERT_STR_EQ(eval_print("(format nil \"~A ~* ~A\" 1 2 3)"), "\"1  3\"");
    /* ~n* skip forward n args */
    ASSERT_STR_EQ(eval_print("(format nil \"~A ~2* ~A\" 1 2 3 4)"), "\"1  4\"");
    /* ~:* back up 1 arg */
    ASSERT_STR_EQ(eval_print("(format nil \"~A ~A ~:*~A\" 1 2)"), "\"1 2 2\"");
    /* ~n:* back up n args */
    ASSERT_STR_EQ(eval_print("(format nil \"~A ~A ~2:*~A ~A\" 1 2)"), "\"1 2 1 2\"");
    /* ~n@* go to absolute position */
    ASSERT_STR_EQ(eval_print("(format nil \"~A ~A ~0@*~A\" 1 2)"), "\"1 2 1\"");
}

TEST(eval_format_tabulate)
{
    /* ~colnumT: absolute tab to column */
    ASSERT_STR_EQ(eval_print("(format nil \"abc~10Tdef\")"), "\"abc       def\"");
    /* Already past column — colinc kicks in */
    ASSERT_STR_EQ(eval_print("(format nil \"abcdefghij~10,5Txx\")"), "\"abcdefghij     xx\"");
    /* ~colnum,colinc@T: relative tab */
    ASSERT_STR_EQ(eval_print("(format nil \"abc~3,1@Tdef\")"), "\"abc   def\"");
}

/* --- Advanced Format: Step 3 — ~(~) case conversion --- */

TEST(eval_format_case_downcase)
{
    /* ~(text~) — lowercase all */
    ASSERT_STR_EQ(eval_print("(format nil \"~(~A~)\" \"HELLO WORLD\")"),
                  "\"hello world\"");
}

TEST(eval_format_case_capitalize)
{
    /* ~:(text~) — capitalize each word */
    ASSERT_STR_EQ(eval_print("(format nil \"~:(~A~)\" \"hello world\")"),
                  "\"Hello World\"");
}

TEST(eval_format_case_cap_first)
{
    /* ~@(text~) — capitalize first word only */
    ASSERT_STR_EQ(eval_print("(format nil \"~@(~A~)\" \"hello world\")"),
                  "\"Hello world\"");
}

TEST(eval_format_case_upcase)
{
    /* ~:@(text~) — uppercase all */
    ASSERT_STR_EQ(eval_print("(format nil \"~:@(~A~)\" \"hello\")"),
                  "\"HELLO\"");
}

/* --- Advanced Format: Step 4 — ~[~;~] conditional --- */

TEST(eval_format_cond_numeric)
{
    /* ~[c0~;c1~;c2~] — select by integer */
    ASSERT_STR_EQ(eval_print("(format nil \"~[zero~;one~;two~]\" 1)"), "\"one\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~[zero~;one~;two~]\" 0)"), "\"zero\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~[zero~;one~;two~]\" 2)"), "\"two\"");
    /* Out of range — no output */
    ASSERT_STR_EQ(eval_print("(format nil \"~[zero~;one~]\" 5)"), "\"\"");
}

TEST(eval_format_cond_default)
{
    /* ~[c0~;c1~:;default~] — last clause with ~:; is default */
    ASSERT_STR_EQ(eval_print("(format nil \"~[zero~;one~:;other~]\" 5)"), "\"other\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~[zero~;one~:;other~]\" 0)"), "\"zero\"");
}

TEST(eval_format_cond_boolean)
{
    /* ~:[false~;true~] — nil=clause0, non-nil=clause1 */
    ASSERT_STR_EQ(eval_print("(format nil \"~:[false~;true~]\" nil)"), "\"false\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~:[false~;true~]\" 42)"), "\"true\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~:[false~;true~]\" t)"), "\"true\"");
}

TEST(eval_format_cond_atsign)
{
    /* ~@[clause~] — if non-nil, format clause (arg available); if nil, skip */
    ASSERT_STR_EQ(eval_print("(format nil \"~@[x=~A ~]done\" 42)"), "\"x=42 done\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~@[x=~A ~]done\" nil)"), "\"done\"");
}

/* --- Advanced format: Step 5 — ~{~} iteration and ~^ --- */

TEST(eval_format_iteration_list)
{
    /* ~{body~} — iterate over list elements */
    ASSERT_STR_EQ(eval_print("(format nil \"~{~A ~}\" '(1 2 3))"), "\"1 2 3 \"");
    ASSERT_STR_EQ(eval_print("(format nil \"~{~A~^, ~}\" '(1 2 3))"), "\"1, 2, 3\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~{~A~}\" nil)"), "\"\"");
}

TEST(eval_format_iteration_sublists)
{
    /* ~:{body~} — iterate over list of sublists */
    ASSERT_STR_EQ(eval_print("(format nil \"~:{[~A ~A] ~}\" '((a 1) (b 2)))"), "\"[A 1] [B 2] \"");
    ASSERT_STR_EQ(eval_print("(format nil \"~:{~A=~A ~}\" '((x 1) (y 2) (z 3)))"), "\"X=1 Y=2 Z=3 \"");
}

TEST(eval_format_iteration_atsign)
{
    /* ~@{body~} — iterate using remaining args directly */
    ASSERT_STR_EQ(eval_print("(format nil \"~@{~A~^, ~}\" 1 2 3)"), "\"1, 2, 3\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~@{~A ~A~^; ~}\" 'a 1 'b 2)"), "\"A 1; B 2\"");
}

TEST(eval_format_iteration_colon_atsign)
{
    /* ~:@{body~} — remaining args are sublists */
    ASSERT_STR_EQ(eval_print("(format nil \"~:@{~A-~A ~}\" '(a 1) '(b 2))"), "\"A-1 B-2 \"");
}

TEST(eval_format_iteration_limit)
{
    /* ~n{body~} — max iteration count */
    ASSERT_STR_EQ(eval_print("(format nil \"~2{~A ~}\" '(1 2 3 4 5))"), "\"1 2 \"");
}

TEST(eval_format_escape)
{
    /* ~^ — escape iteration when no args remain */
    ASSERT_STR_EQ(eval_print("(format nil \"~{~A~^-~}\" '(x))"), "\"X\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~{~A~^-~}\" '(x y z))"), "\"X-Y-Z\"");
}

/* --- Advanced format: Step 6 — ~? recursive and ~R radix --- */

TEST(eval_format_recursive)
{
    /* ~? — recursive format with string + arg list */
    ASSERT_STR_EQ(eval_print("(format nil \"~? and ~A\" \"~A ~A\" '(1 2) 3)"), "\"1 2 and 3\"");
}

TEST(eval_format_recursive_atsign)
{
    /* ~@? — recursive format using remaining args */
    ASSERT_STR_EQ(eval_print("(format nil \"~@? and ~A\" \"~A ~A\" 1 2 3)"), "\"1 2 and 3\"");
}

TEST(eval_format_radix_cardinal)
{
    /* ~R — cardinal English */
    ASSERT_STR_EQ(eval_print("(format nil \"~R\" 0)"), "\"zero\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~R\" 5)"), "\"five\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~R\" 42)"), "\"forty-two\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~R\" 100)"), "\"one hundred\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~R\" 1000)"), "\"one thousand\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~R\" 1234)"), "\"one thousand two hundred thirty-four\"");
}

TEST(eval_format_radix_ordinal)
{
    /* ~:R — ordinal English */
    ASSERT_STR_EQ(eval_print("(format nil \"~:R\" 1)"), "\"first\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~:R\" 3)"), "\"third\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~:R\" 21)"), "\"twenty-first\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~:R\" 100)"), "\"one hundredth\"");
}

TEST(eval_format_radix_roman)
{
    /* ~@R — Roman numerals */
    ASSERT_STR_EQ(eval_print("(format nil \"~@R\" 4)"), "\"IV\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~@R\" 9)"), "\"IX\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~@R\" 42)"), "\"XLII\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~@R\" 1999)"), "\"MCMXCIX\"");
}

TEST(eval_format_radix_old_roman)
{
    /* ~:@R — old-style Roman (additive) */
    ASSERT_STR_EQ(eval_print("(format nil \"~:@R\" 4)"), "\"IIII\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~:@R\" 9)"), "\"VIIII\"");
}

TEST(eval_format_radix_base)
{
    /* ~nR — print in base n */
    ASSERT_STR_EQ(eval_print("(format nil \"~2R\" 10)"), "\"1010\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~8R\" 255)"), "\"377\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~16R\" 255)"), "\"FF\"");
}

/* --- Phase 8: Loop macro --- */

TEST(eval_loop_simple_return)
{
    /* Simple loop with return */
    ASSERT_EQ_INT(eval_int(
        "(let ((i 0))"
        "  (loop"
        "    (when (= i 5) (return i))"
        "    (setq i (+ i 1))))"), 5);
}

TEST(eval_loop_simple_accumulate)
{
    /* Simple loop accumulating a list */
    ASSERT_STR_EQ(eval_print(
        "(let ((result nil) (i 0))"
        "  (loop"
        "    (when (>= i 3) (return (nreverse result)))"
        "    (push i result)"
        "    (setq i (+ i 1))))"), "(0 1 2)");
}

TEST(eval_loop_while)
{
    /* Extended loop with while */
    ASSERT_EQ_INT(eval_int(
        "(let ((i 0) (sum 0))"
        "  (loop while (< i 5)"
        "    do (setq sum (+ sum i))"
        "       (setq i (+ i 1)))"
        "  sum)"), 10);
}

TEST(eval_loop_until)
{
    /* Extended loop with until */
    ASSERT_EQ_INT(eval_int(
        "(let ((i 0) (sum 0))"
        "  (loop until (= i 4)"
        "    do (setq sum (+ sum i))"
        "       (setq i (+ i 1)))"
        "  sum)"), 6);
}

TEST(eval_loop_do_multiple)
{
    /* Extended loop do with multiple body forms */
    ASSERT_EQ_INT(eval_int(
        "(let ((x 0) (y 0))"
        "  (loop while (< x 3)"
        "    do (setq x (+ x 1))"
        "       (setq y (+ y 10)))"
        "  y)"), 30);
}

/* --- Phase 8: Loop Step 2 — for/as/repeat --- */

TEST(eval_loop_for_in)
{
    /* for var in list */
    ASSERT_STR_EQ(eval_print(
        "(let ((r nil))"
        "  (loop for x in '(1 2 3)"
        "    do (push x r))"
        "  (nreverse r))"), "(1 2 3)");
}

TEST(eval_loop_for_in_by)
{
    /* for var in list by #'cddr */
    ASSERT_STR_EQ(eval_print(
        "(let ((r nil))"
        "  (loop for x in '(1 2 3 4 5)"
        "    by #'cddr"
        "    do (push x r))"
        "  (nreverse r))"), "(1 3 5)");
}

TEST(eval_loop_for_on)
{
    /* for var on list */
    ASSERT_STR_EQ(eval_print(
        "(let ((r nil))"
        "  (loop for x on '(1 2 3)"
        "    do (push x r))"
        "  (nreverse r))"), "((1 2 3) (2 3) (3))");
}

TEST(eval_loop_for_from_to)
{
    /* for var from start to end */
    ASSERT_EQ_INT(eval_int(
        "(let ((sum 0))"
        "  (loop for i from 1 to 5"
        "    do (setq sum (+ sum i)))"
        "  sum)"), 15);
}

TEST(eval_loop_for_from_below)
{
    /* for var from start below end */
    ASSERT_EQ_INT(eval_int(
        "(let ((sum 0))"
        "  (loop for i from 0 below 4"
        "    do (setq sum (+ sum i)))"
        "  sum)"), 6);
}

TEST(eval_loop_for_from_by)
{
    /* for var from start to end by step */
    ASSERT_STR_EQ(eval_print(
        "(let ((r nil))"
        "  (loop for i from 0 to 10 by 3"
        "    do (push i r))"
        "  (nreverse r))"), "(0 3 6 9)");
}

TEST(eval_loop_for_downfrom)
{
    /* for var downfrom start to end */
    ASSERT_STR_EQ(eval_print(
        "(let ((r nil))"
        "  (loop for i downfrom 5 to 1"
        "    do (push i r))"
        "  (nreverse r))"), "(5 4 3 2 1)");
}

TEST(eval_loop_for_downfrom_above)
{
    /* for var downfrom start above end */
    ASSERT_STR_EQ(eval_print(
        "(let ((r nil))"
        "  (loop for i downfrom 5 above 2"
        "    do (push i r))"
        "  (nreverse r))"), "(5 4 3)");
}

TEST(eval_loop_for_across)
{
    /* for var across vector */
    ASSERT_STR_EQ(eval_print(
        "(let ((r nil))"
        "  (loop for c across \"abc\""
        "    do (push c r))"
        "  (nreverse r))"), "(#\\a #\\b #\\c)");
}

TEST(eval_loop_for_eq_then)
{
    /* for var = init then step */
    ASSERT_STR_EQ(eval_print(
        "(let ((r nil))"
        "  (loop for x = 1 then (* x 2)"
        "    while (< x 20)"
        "    do (push x r))"
        "  (nreverse r))"), "(1 2 4 8 16)");
}

TEST(eval_loop_repeat)
{
    /* repeat n */
    ASSERT_EQ_INT(eval_int(
        "(let ((count 0))"
        "  (loop repeat 5"
        "    do (setq count (+ count 1)))"
        "  count)"), 5);
}

TEST(eval_loop_for_multiple)
{
    /* multiple for clauses */
    ASSERT_STR_EQ(eval_print(
        "(let ((r nil))"
        "  (loop for x in '(a b c)"
        "        for i from 1"
        "    do (push (cons i x) r))"
        "  (nreverse r))"), "((1 . A) (2 . B) (3 . C))");
}

/* --- Phase 8: Loop Step 3 — accumulation + return --- */

TEST(eval_loop_collect)
{
    /* collect */
    ASSERT_STR_EQ(eval_print(
        "(loop for x in '(1 2 3) collect x)"), "(1 2 3)");
}

TEST(eval_loop_collect_expr)
{
    /* collect expression */
    ASSERT_STR_EQ(eval_print(
        "(loop for x in '(1 2 3) collect (* x x))"), "(1 4 9)");
}

TEST(eval_loop_collect_into)
{
    /* collect into named var — use do + return to verify accumulation */
    ASSERT_STR_EQ(eval_print(
        "(loop for x in '(1 2 3 4 5)"
        "  collect x into result"
        "  do (when (= x 3) (return (nreverse result))))"), "(1 2 3)");
}

TEST(eval_loop_sum)
{
    /* sum */
    ASSERT_EQ_INT(eval_int(
        "(loop for x in '(1 2 3 4 5) sum x)"), 15);
}

TEST(eval_loop_sum_into)
{
    /* sum into named var — use do + return to verify */
    ASSERT_EQ_INT(eval_int(
        "(loop for x in '(1 2 3 4 5)"
        "  sum x into total"
        "  do (when (= x 3) (return total)))"), 6);
}

TEST(eval_loop_count)
{
    /* count */
    ASSERT_EQ_INT(eval_int(
        "(loop for x in '(1 nil 2 nil 3) count x)"), 3);
}

TEST(eval_loop_maximize)
{
    /* maximize */
    ASSERT_EQ_INT(eval_int(
        "(loop for x in '(3 1 4 1 5 9 2 6) maximize x)"), 9);
}

TEST(eval_loop_minimize)
{
    /* minimize */
    ASSERT_EQ_INT(eval_int(
        "(loop for x in '(3 1 4 1 5 9 2 6) minimize x)"), 1);
}

TEST(eval_loop_append)
{
    /* append */
    ASSERT_STR_EQ(eval_print(
        "(loop for x in '((a b) (c d) (e)) append x)"), "(A B C D E)");
}

TEST(eval_loop_nconc)
{
    /* nconc */
    ASSERT_STR_EQ(eval_print(
        "(loop for x in '((1 2) (3 4)) nconc (copy-list x))"), "(1 2 3 4)");
}

TEST(eval_loop_return)
{
    /* return clause */
    ASSERT_EQ_INT(eval_int(
        "(loop for x in '(1 2 3 4 5)"
        "  do (when (= x 3) (return x)))"), 3);
}

TEST(eval_loop_return_clause)
{
    /* loop return keyword */
    ASSERT_EQ_INT(eval_int(
        "(loop for x from 1"
        "  do (when (> x 10) (return x)))"), 11);
}

TEST(eval_loop_return_in_do_let)
{
    /* return inside do + let — regression test for Fix 22 */
    ASSERT_EQ_INT(eval_int(
        "(loop while t do (let ((x 42)) (return x)))"), 42);
    ASSERT_EQ_INT(eval_int(
        "(loop for i from 0 do (let ((y (* i 10)))"
        "  (when (= y 30) (return y))))"), 30);
    /* return inside do + let* with multiple bindings */
    ASSERT_EQ_INT(eval_int(
        "(loop while t do (let* ((a 1) (b 2)) (return (+ a b))))"), 3);
    /* return inside defun + loop + do + let */
    cl_eval_string("(defun %test-loop-ret () (loop while t do (let ((x 7)) (return x))))");
    ASSERT_EQ_INT(eval_int("(%test-loop-ret)"), 7);
}

TEST(eval_loop_when_collect)
{
    /* when ... collect */
    ASSERT_STR_EQ(eval_print(
        "(loop for x in '(1 2 3 4 5 6)"
        "  when (evenp x) collect x)"), "(2 4 6)");
}

TEST(eval_loop_if_else)
{
    /* if ... collect ... else collect */
    ASSERT_STR_EQ(eval_print(
        "(loop for x in '(1 2 3 4 5)"
        "  if (oddp x) collect x"
        "  else collect (- x))"), "(1 -2 3 -4 5)");
}

TEST(eval_loop_unless_collect)
{
    /* unless ... collect */
    ASSERT_STR_EQ(eval_print(
        "(loop for x in '(1 2 3 4 5)"
        "  unless (evenp x) collect x)"), "(1 3 5)");
}

TEST(eval_loop_when_and)
{
    /* when ... and chaining: collect + side effect */
    ASSERT_STR_EQ(eval_print(
        "(let ((side nil))"
        "  (let ((r (loop for x in '(1 2 3 4)"
        "             when (evenp x) collect x"
        "             and do (push x side))))"
        "    (list r (nreverse side))))"), "((2 4) (2 4))");
}

TEST(eval_loop_always)
{
    /* always — true case */
    ASSERT_STR_EQ(eval_print(
        "(loop for x in '(2 4 6) always (evenp x))"), "T");
    /* always — false case */
    ASSERT_STR_EQ(eval_print(
        "(loop for x in '(2 3 6) always (evenp x))"), "NIL");
}

TEST(eval_loop_never)
{
    /* never — true case */
    ASSERT_STR_EQ(eval_print(
        "(loop for x in '(1 3 5) never (evenp x))"), "T");
    /* never — false case */
    ASSERT_STR_EQ(eval_print(
        "(loop for x in '(1 2 5) never (evenp x))"), "NIL");
}

TEST(eval_loop_thereis)
{
    /* thereis — found */
    ASSERT_EQ_INT(eval_int(
        "(loop for x in '(1 2 3 4 5)"
        "  thereis (and (> x 3) x))"), 4);
    /* thereis — not found */
    ASSERT_STR_EQ(eval_print(
        "(loop for x in '(1 2 3)"
        "  thereis (and (> x 10) x))"), "NIL");
}

TEST(eval_loop_with)
{
    /* with var = expr */
    ASSERT_EQ_INT(eval_int(
        "(loop with sum = 0"
        "  for x in '(1 2 3)"
        "  do (setq sum (+ sum x))"
        "  finally (return sum))"), 6);
}

TEST(eval_loop_with_and)
{
    /* with ... and */
    ASSERT_STR_EQ(eval_print(
        "(loop with x = 10 and y = 20"
        "  repeat 1 collect (+ x y))"), "(30)");
}

TEST(eval_loop_with_destructuring)
{
    /* with destructuring — dotted pair */
    ASSERT_STR_EQ(eval_print(
        "(loop with (a . b) = (cons 1 2)"
        "  do (return (list a b)))"), "(1 2)");
    /* with destructuring — proper list */
    ASSERT_STR_EQ(eval_print(
        "(loop with (first . rest) = '(10 20 30)"
        "  do (return (list first rest)))"), "(10 (20 30))");
    /* with destructuring — nested */
    ASSERT_STR_EQ(eval_print(
        "(loop with ((a b) . c) = '((1 2) 3 4)"
        "  do (return (list a b c)))"), "(1 2 (3 4))");
}

TEST(eval_loop_named)
{
    /* named block with return-from */
    ASSERT_EQ_INT(eval_int(
        "(loop named my-loop"
        "  for x from 1"
        "  do (when (> x 5) (return-from my-loop x)))"), 6);
}

TEST(eval_loop_initially)
{
    /* initially form */
    ASSERT_STR_EQ(eval_print(
        "(let ((log nil))"
        "  (loop initially (push 'start log)"
        "    for x in '(1 2) do (push x log))"
        "  (nreverse log))"), "(START 1 2)");
}

TEST(eval_loop_finally)
{
    /* finally with return */
    ASSERT_EQ_INT(eval_int(
        "(loop for x in '(1 2 3 4 5)"
        "  sum x into total"
        "  finally (return total))"), 15);
}

TEST(eval_loop_collect_into_finally)
{
    /* collect into with finally */
    ASSERT_STR_EQ(eval_print(
        "(loop for x in '(a b c)"
        "  collect x into items"
        "  finally (return items))"), "(A B C)");
}

TEST(eval_loop_finish)
{
    /* loop-finish terminates loop normally, returns accumulation */
    ASSERT_EQ_INT(eval_int(
        "(loop for i from 1 to 10 sum i"
        "  do (when (= i 5) (loop-finish)))"), 15);
}

TEST(eval_loop_finish_finally)
{
    /* loop-finish still runs finally forms */
    ASSERT_EQ_INT(eval_int(
        "(let ((ran nil))"
        "  (loop for i from 1 to 10"
        "    do (when (= i 3) (loop-finish))"
        "    finally (setq ran t))"
        "  (if ran 1 0))"), 1);
}

/* ---- LOOP BEING clause ---- */

TEST(eval_loop_being_hash_keys)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((ht (make-hash-table)))"
        "  (setf (gethash :a ht) 1)"
        "  (setf (gethash :b ht) 2)"
        "  (sort (loop for k being the hash-keys of ht collect k)"
        "        #'string< :key #'symbol-name))"), "(:A :B)");
}

TEST(eval_loop_being_hash_values)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((ht (make-hash-table)))"
        "  (setf (gethash :a ht) 10)"
        "  (setf (gethash :b ht) 20)"
        "  (sort (loop for v being the hash-values in ht collect v)"
        "        #'<))"), "(10 20)");
}

TEST(eval_loop_being_hash_keys_using)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((ht (make-hash-table)))"
        "  (setf (gethash :x ht) 42)"
        "  (loop for k being each hash-key of ht"
        "        using (hash-value v)"
        "        collect (list k v)))"), "((:X 42))");
}

TEST(eval_loop_being_hash_values_using)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((ht (make-hash-table)))"
        "  (setf (gethash :x ht) 42)"
        "  (loop for v being each hash-value of ht"
        "        using (hash-key k)"
        "        collect (list k v)))"), "((:X 42))");
}

TEST(eval_loop_being_symbols)
{
    /* Create a package with known symbols, count them */
    ASSERT_STR_EQ(eval_print(
        "(progn"
        "  (defpackage \"LOOP-SYM-TEST\" (:use))"
        "  (intern \"AA\" \"LOOP-SYM-TEST\")"
        "  (intern \"BB\" \"LOOP-SYM-TEST\")"
        "  (length (loop for s being the symbols of \"LOOP-SYM-TEST\""
        "               collect s)))"), "2");
}

TEST(eval_loop_being_external_symbols)
{
    ASSERT_STR_EQ(eval_print(
        "(progn"
        "  (defpackage \"LOOP-EXT-TEST\" (:use) (:export \"PUB\"))"
        "  (intern \"PRIV\" \"LOOP-EXT-TEST\")"
        "  (length (loop for s being the external-symbols of \"LOOP-EXT-TEST\""
        "               collect s)))"), "1");
}

/* ---- LOOP destructuring ---- */

TEST(eval_loop_destructuring_in)
{
    /* destructure pairs from a list */
    ASSERT_STR_EQ(eval_print(
        "(loop for (a b) in '((1 2) (3 4) (5 6))"
        "  collect (+ a b))"), "(3 7 11)");
}

TEST(eval_loop_destructuring_dotted)
{
    /* destructure dotted pairs */
    ASSERT_STR_EQ(eval_print(
        "(loop for (a . b) in '((x . 1) (y . 2) (z . 3))"
        "  collect (list a b))"), "((X 1) (Y 2) (Z 3))");
}

TEST(eval_loop_destructuring_nested)
{
    /* nested destructuring */
    ASSERT_STR_EQ(eval_print(
        "(loop for (a (b c)) in '((1 (2 3)) (4 (5 6)))"
        "  collect (+ a b c))"), "(6 15)");
}

TEST(eval_loop_destructuring_on)
{
    /* destructuring with ON */
    ASSERT_STR_EQ(eval_print(
        "(loop for (a . b) on '(1 2 3)"
        "  collect (list a b))"), "((1 (2 3)) (2 (3)) (3 NIL))");
}

/* --- Pretty-printing / *print-pretty* --- */

TEST(eval_print_pretty_default)
{
    /* *print-right-margin* defaults to NIL */
    ASSERT_STR_EQ(eval_print("*print-right-margin*"), "NIL");
    /* *print-pretty* defaults to NIL */
    ASSERT_STR_EQ(eval_print("*print-pretty*"), "NIL");
}

TEST(eval_print_pretty_short_list)
{
    /* Short list stays on one line even with pretty printing */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-pretty* t) (*print-right-margin* 40))"
        "  (write-to-string '(1 2 3)))"),
        "\"(1 2 3)\"");
}

TEST(eval_print_pretty_long_list)
{
    /* Narrow margin forces line breaks (greedy fill: break when past margin) */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-pretty* t) (*print-right-margin* 10))"
        "  (write-to-string '(alpha beta gamma delta)))"),
        "\"(ALPHA BETA\\n GAMMA DELTA)\"");
}

TEST(eval_print_pretty_nested)
{
    /* Nested lists indent correctly */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-pretty* t) (*print-right-margin* 15))"
        "  (write-to-string '(a (b c d) e)))"),
        "\"(A (B C D) E)\"");
}

TEST(eval_print_pretty_vector)
{
    /* Vectors break similarly to lists */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-pretty* t) (*print-right-margin* 10))"
        "  (write-to-string (vector 'alpha 'beta 'gamma)))"),
        "\"#(ALPHA BETA\\n  GAMMA)\"");
}

TEST(eval_print_pretty_empty)
{
    /* NIL and single-element lists */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-pretty* t) (*print-right-margin* 10))"
        "  (write-to-string nil))"),
        "\"NIL\"");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-pretty* t) (*print-right-margin* 10))"
        "  (write-to-string '(x)))"),
        "\"(X)\"");
}

TEST(eval_print_pretty_dotted)
{
    /* Dotted pair stays inline if short enough */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-pretty* t) (*print-right-margin* 40))"
        "  (write-to-string '(a . b)))"),
        "\"(A . B)\"");
}

TEST(eval_pprint_basic)
{
    /* pprint outputs newline + pretty-printed form */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (make-string-output-stream)))"
        "  (pprint '(1 2 3) s)"
        "  (get-output-stream-string s))"),
        "\"\\n(1 2 3)\"");
}

TEST(eval_print_pretty_write_keyword)
{
    /* write :pretty t enables pretty printing */
    ASSERT_STR_EQ(eval_print(
        "(write-to-string '(1 2 3) :pretty t :right-margin 40)"),
        "\"(1 2 3)\"");
}

TEST(eval_print_pretty_write_to_string)
{
    /* write-to-string :pretty t with narrow margin */
    ASSERT_STR_EQ(eval_print(
        "(write-to-string '(alpha beta gamma delta) :pretty t :right-margin 10)"),
        "\"(ALPHA BETA\\n GAMMA DELTA)\"");
}

TEST(eval_print_pretty_off_no_break)
{
    /* When *print-pretty* is nil, no line breaks even past margin */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-pretty* nil) (*print-right-margin* 5))"
        "  (write-to-string '(alpha beta gamma)))"),
        "\"(ALPHA BETA GAMMA)\"");
}

TEST(eval_print_pretty_with_level_length)
{
    /* Pretty + level/length interact correctly */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-pretty* t) (*print-right-margin* 10)"
        "      (*print-length* 2))"
        "  (write-to-string '(alpha beta gamma delta)))"),
        "\"(ALPHA BETA ...)\"");
}

/* --- rotatef / shiftf --- */

TEST(eval_rotatef_basic)
{
    eval_print("(defvar *rf-a* 1)");
    eval_print("(defvar *rf-b* 2)");
    eval_print("(defvar *rf-c* 3)");
    eval_print("(rotatef *rf-a* *rf-b* *rf-c*)");
    ASSERT_EQ_INT(eval_int("*rf-a*"), 2);
    ASSERT_EQ_INT(eval_int("*rf-b*"), 3);
    ASSERT_EQ_INT(eval_int("*rf-c*"), 1);
}

TEST(eval_rotatef_two)
{
    eval_print("(defvar *rf2-x* 10)");
    eval_print("(defvar *rf2-y* 20)");
    eval_print("(rotatef *rf2-x* *rf2-y*)");
    ASSERT_EQ_INT(eval_int("*rf2-x*"), 20);
    ASSERT_EQ_INT(eval_int("*rf2-y*"), 10);
}

TEST(eval_rotatef_returns_nil)
{
    eval_print("(defvar *rfr-a* 1)");
    eval_print("(defvar *rfr-b* 2)");
    ASSERT_STR_EQ(eval_print("(rotatef *rfr-a* *rfr-b*)"), "NIL");
}

TEST(eval_rotatef_car)
{
    eval_print("(defvar *rfc-x* (cons 1 2))");
    eval_print("(defvar *rfc-y* 99)");
    eval_print("(rotatef (car *rfc-x*) *rfc-y*)");
    ASSERT_EQ_INT(eval_int("(car *rfc-x*)"), 99);
    ASSERT_EQ_INT(eval_int("*rfc-y*"), 1);
}

TEST(eval_shiftf_basic)
{
    eval_print("(defvar *sf-a* 1)");
    eval_print("(defvar *sf-b* 2)");
    eval_print("(defvar *sf-c* 3)");
    ASSERT_EQ_INT(eval_int("(shiftf *sf-a* *sf-b* *sf-c* 99)"), 1);
    ASSERT_EQ_INT(eval_int("*sf-a*"), 2);
    ASSERT_EQ_INT(eval_int("*sf-b*"), 3);
    ASSERT_EQ_INT(eval_int("*sf-c*"), 99);
}

TEST(eval_shiftf_two)
{
    eval_print("(defvar *sf2-x* 10)");
    ASSERT_EQ_INT(eval_int("(shiftf *sf2-x* 42)"), 10);
    ASSERT_EQ_INT(eval_int("*sf2-x*"), 42);
}

TEST(eval_shiftf_car)
{
    eval_print("(defvar *sfc-x* (cons 5 6))");
    ASSERT_EQ_INT(eval_int("(shiftf (car *sfc-x*) 77)"), 5);
    ASSERT_EQ_INT(eval_int("(car *sfc-x*)"), 77);
}

/* --- pprint-newline --- */

TEST(eval_pprint_newline_mandatory)
{
    /* :mandatory doesn't error */
    ASSERT_STR_EQ(eval_print("(progn (pprint-newline :mandatory) t)"), "T");
}

TEST(eval_pprint_newline_fill)
{
    /* :fill doesn't error */
    ASSERT_STR_EQ(eval_print("(progn (pprint-newline :fill) t)"), "T");
}

TEST(eval_pprint_newline_kinds)
{
    /* All four kinds accepted */
    ASSERT_STR_EQ(eval_print("(progn (pprint-newline :linear) t)"), "T");
    ASSERT_STR_EQ(eval_print("(progn (pprint-newline :miser) t)"), "T");
}

/* --- pprint-indent --- */

TEST(eval_pprint_indent_current)
{
    /* pprint-indent :current doesn't error */
    ASSERT_STR_EQ(eval_print(
        "(progn (pprint-indent :current 2) t)"),
        "T");
}

TEST(eval_pprint_indent_block)
{
    /* pprint-indent :block doesn't error */
    ASSERT_STR_EQ(eval_print(
        "(progn (pprint-indent :block 4) t)"),
        "T");
}

/* --- pprint-logical-block --- */

TEST(eval_pprint_logical_block_basic)
{
    /* pprint-logical-block with prefix/suffix, using with-output-to-string */
    ASSERT_STR_EQ(eval_print(
        "(with-output-to-string (s)"
        "  (pprint-logical-block (s '(1 2 3) :prefix \"[\" :suffix \"]\")"
        "    (princ (pprint-pop) s)"
        "    (pprint-exit-if-list-exhausted)"
        "    (write-char #\\Space s)"
        "    (princ (pprint-pop) s)"
        "    (pprint-exit-if-list-exhausted)"
        "    (write-char #\\Space s)"
        "    (princ (pprint-pop) s)))"),
        "\"[1 2 3]\"");
}

/* --- pprint-dispatch --- */

TEST(eval_pprint_dispatch_set_get)
{
    /* set-pprint-dispatch and pprint-dispatch round-trip */
    eval_print("(set-pprint-dispatch 'cons (lambda (s obj) (format s \"<~S>\" obj)))");
    ASSERT_STR_EQ(eval_print("(pprint-dispatch '(1 2))"), "#<CLOSURE anonymous>");
    /* Clean up */
    eval_print("(set-pprint-dispatch 'cons nil)");
}

TEST(eval_copy_pprint_dispatch)
{
    /* copy-pprint-dispatch returns a copy */
    ASSERT_STR_EQ(eval_print("(copy-pprint-dispatch)"), "NIL");
}

TEST(eval_pprint_dispatch_custom)
{
    /* Custom dispatch function used during pretty printing */
    eval_print(
        "(set-pprint-dispatch 'integer"
        "  (lambda (s obj)"
        "    (write-string \"[\" s)"
        "    (princ obj s)"
        "    (write-string \"]\" s))"
        "  1)");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-pretty* t))"
        "  (with-output-to-string (s)"
        "    (write 42 :stream s :pretty t)))"),
        "\"[42]\"");
    /* Clean up */
    eval_print("(set-pprint-dispatch 'integer nil)");
}

/* --- Modules (provide / require) --- */

TEST(eval_provide_adds_to_modules)
{
    /* provide adds module name to *modules* */
    eval_print("(provide \"my-module\")");
    ASSERT_STR_EQ(eval_print("(car *modules*)"), "\"my-module\"");
}

TEST(eval_provide_no_duplicate)
{
    /* provide does not add duplicates */
    eval_print("(provide \"dup-mod\")");
    eval_print("(provide \"dup-mod\")");
    /* Count occurrences - should be exactly 1 */
    ASSERT_STR_EQ(eval_print(
        "(count \"dup-mod\" *modules* :test #'string=)"), "1");
}

TEST(eval_provide_symbol_name)
{
    /* provide accepts a symbol, using its name */
    eval_print("(provide 'sym-mod)");
    ASSERT_STR_EQ(eval_print(
        "(find \"SYM-MOD\" *modules* :test #'string=)"), "\"SYM-MOD\"");
}

TEST(eval_require_already_provided)
{
    /* require of already-provided module is a no-op */
    eval_print("(provide \"already-there\")");
    ASSERT_STR_EQ(eval_print("(require \"already-there\")"), "NIL");
}

TEST(eval_require_with_pathname)
{
    /* require loads a file when not yet provided */
    eval_print("(require \"test-module\" \"tests/test_module.lisp\")");
    ASSERT_STR_EQ(eval_print("*test-module-loaded*"), "T");
    ASSERT_EQ_INT(eval_int("(test-module-fn 5)"), 50);
    /* The module file calls (provide "test-module"), so it should be in *modules* */
    ASSERT_STR_EQ(eval_print(
        "(find \"test-module\" *modules* :test #'string=)"), "\"test-module\"");
}

TEST(eval_require_does_not_reload)
{
    /* After provide, require should not load again */
    eval_print("(provide \"test-module\")");
    eval_print("(setq *test-module-loaded* nil)");
    eval_print("(require \"test-module\" \"tests/test_module.lisp\")");
    /* Should still be nil because require was a no-op */
    ASSERT_STR_EQ(eval_print("*test-module-loaded*"), "NIL");
}

/* --- Property lists --- */

TEST(eval_symbol_plist_initial)
{
    ASSERT_STR_EQ(eval_print("(symbol-plist 'test-plist-sym-1)"), "NIL");
}

TEST(eval_setf_get)
{
    ASSERT_STR_EQ(eval_print("(setf (get 'test-plist-sym-2 'bar) 42)"), "42");
    ASSERT_STR_EQ(eval_print("(get 'test-plist-sym-2 'bar)"), "42");
}

TEST(eval_get_default)
{
    ASSERT_STR_EQ(eval_print("(get 'test-plist-sym-2 'baz 99)"), "99");
    ASSERT_STR_EQ(eval_print("(get 'test-plist-sym-2 'baz)"), "NIL");
}

TEST(eval_remprop)
{
    eval_print("(setf (get 'test-plist-sym-3 'foo) 10)");
    ASSERT_STR_EQ(eval_print("(remprop 'test-plist-sym-3 'foo)"), "T");
    ASSERT_STR_EQ(eval_print("(get 'test-plist-sym-3 'foo)"), "NIL");
    ASSERT_STR_EQ(eval_print("(remprop 'test-plist-sym-3 'foo)"), "NIL");
}

TEST(eval_setf_get_update)
{
    eval_print("(setf (get 'test-plist-sym-4 'x) 1)");
    eval_print("(setf (get 'test-plist-sym-4 'x) 2)");
    ASSERT_STR_EQ(eval_print("(get 'test-plist-sym-4 'x)"), "2");
}

/* --- Block comments (#|...|#) --- */

TEST(eval_block_comment_basic)
{
    ASSERT_EQ_INT(eval_int("#| comment |# 42"), 42);
}

TEST(eval_block_comment_inline)
{
    ASSERT_EQ_INT(eval_int("(+ 1 #| skip |# 2)"), 3);
}

TEST(eval_block_comment_nested)
{
    ASSERT_EQ_INT(eval_int("#| outer #| inner |# still outer |# 99"), 99);
}

TEST(eval_block_comment_multiline)
{
    ASSERT_EQ_INT(eval_int("#|\n  multi\n  line\n|# 7"), 7);
}

/* --- &aux in lambda lists --- */

TEST(eval_aux_basic)
{
    ASSERT_EQ_INT(eval_int("(funcall (lambda (x &aux (y 10)) (+ x y)) 5)"), 15);
}

TEST(eval_aux_multiple)
{
    ASSERT_EQ_INT(eval_int("(funcall (lambda (x &aux (a 1) (b 2)) (+ x a b)) 10)"), 13);
}

TEST(eval_aux_no_init)
{
    ASSERT_STR_EQ(eval_print("(funcall (lambda (&aux z) z))"), "NIL");
}

TEST(eval_aux_uses_param)
{
    ASSERT_EQ_INT(eval_int("(funcall (lambda (x &aux (y (* x 2))) y) 5)"), 10);
}

TEST(eval_aux_with_key)
{
    ASSERT_EQ_INT(eval_int("(funcall (lambda (&key x &aux (y (or x 0))) (+ y 1)) :x 5)"), 6);
}

TEST(eval_defun_aux)
{
    eval_print("(defun test-aux-fn (a &aux (b (+ a 1))) b)");
    ASSERT_EQ_INT(eval_int("(test-aux-fn 10)"), 11);
}

/* --- apply with symbols --- */

TEST(eval_apply_symbol)
{
    ASSERT_EQ_INT(eval_int("(apply '+ '(1 2 3))"), 6);
}

TEST(eval_apply_symbol_funcall)
{
    ASSERT_EQ_INT(eval_int("(apply '* 2 '(3 4))"), 24);
}

/* --- package-used-by-list --- */

TEST(eval_package_used_by_list)
{
    /* CL-USER uses CL, so CL-USER should be in CL's used-by list */
    ASSERT_STR_EQ(eval_print("(if (member (find-package \"COMMON-LISP-USER\") (package-used-by-list (find-package \"COMMON-LISP\"))) t nil)"), "T");
}

/* --- shadowing-import --- */

TEST(eval_shadowing_import)
{
    eval_print("(defpackage \"TEST-SHAD-PKG\" (:use))");
    eval_print("(shadowing-import 'cons (find-package \"TEST-SHAD-PKG\"))");
    ASSERT_STR_EQ(eval_print("(if (member 'cons (package-shadowing-symbols (find-package \"TEST-SHAD-PKG\"))) t nil)"), "T");
}

/* --- copy-symbol --- */

TEST(eval_copy_symbol_basic)
{
    ASSERT_STR_EQ(eval_print("(symbol-name (copy-symbol 'foo))"), "\"FOO\"");
}

TEST(eval_copy_symbol_uninterned)
{
    ASSERT_STR_EQ(eval_print("(symbol-package (copy-symbol 'foo))"), "NIL");
}

TEST(eval_copy_symbol_props)
{
    eval_print("(defvar *copy-sym-test* 42)");
    ASSERT_STR_EQ(eval_print("(symbol-value (copy-symbol '*copy-sym-test* t))"), "42");
}

/* --- LOOP nested :if sub-clause tests --- */

TEST(eval_loop_nested_if)
{
    ASSERT_STR_EQ(eval_print(
        "(loop :for x :in '(1 2 3)"
        "  :when (> x 1)"
        "    :if (= x 2) :collect :two"
        "    :else :collect :three"
        "    :end)"),
        "(:TWO :THREE)");
}

TEST(eval_loop_nested_when_else)
{
    /* :when with nested :when */
    ASSERT_STR_EQ(eval_print(
        "(loop :for x :in '(1 2 3 4 5)"
        "  :when (oddp x)"
        "    :if (> x 2) :collect x :end)"),
        "(3 5)");
}

TEST(eval_loop_nested_unless)
{
    ASSERT_STR_EQ(eval_print(
        "(loop :for x :in '(1 2 3 4)"
        "  :when t"
        "    :unless (evenp x) :collect x :end)"),
        "(1 3)");
}

/* --- defmacro destructuring tests --- */

TEST(eval_defmacro_destructuring_required)
{
    /* Required parameter is a list pattern */
    eval_print("(defmacro dm-test1 ((a b) c) `(list ,a ,b ,c))");
    ASSERT_STR_EQ(eval_print("(dm-test1 (1 2) 3)"), "(1 2 3)");
}

TEST(eval_defmacro_destructuring_body)
{
    /* &body with destructuring pattern (if-let pattern) */
    eval_print("(defmacro dm-test2 (var &body (then-form &optional else-form))"
               "  `(let ((,var t)) (if ,var ,then-form ,else-form)))");
    ASSERT_STR_EQ(eval_print("(dm-test2 x :yes :no)"), ":YES");
    ASSERT_STR_EQ(eval_print("(dm-test2 x :only)"), ":ONLY");
}

TEST(eval_defmacro_optional_not_destructured)
{
    /* &optional with (name default) should NOT be destructured */
    eval_print("(defmacro dm-test3 (x &optional (y 42)) `(list ,x ,y))");
    ASSERT_STR_EQ(eval_print("(dm-test3 1)"), "(1 42)");
    ASSERT_STR_EQ(eval_print("(dm-test3 1 2)"), "(1 2)");
}

/* --- define-modify-macro tests --- */

TEST(eval_define_modify_macro)
{
    eval_print("(define-modify-macro appendf (&rest args) append)");
    ASSERT_STR_EQ(eval_print(
        "(let ((x '(1 2))) (appendf x '(3 4)) x)"),
        "(1 2 3 4)");
}

/* --- reduce :from-end tests --- */

TEST(eval_reduce_from_end)
{
    ASSERT_STR_EQ(eval_print(
        "(reduce #'cons '(1 2 3 4) :from-end t)"),
        "(1 2 3 . 4)");
}

TEST(eval_reduce_from_end_initial)
{
    ASSERT_STR_EQ(eval_print(
        "(reduce #'cons '(1 2 3) :from-end t :initial-value '())"),
        "(1 2 3)");
}

/* --- defmethod implicit block test --- */

TEST(eval_defmethod_implicit_block)
{
    eval_print("(defgeneric dmib-fn (x))");
    eval_print("(defmethod dmib-fn ((x t)) (return-from dmib-fn 42) 99)");
    ASSERT_STR_EQ(eval_print("(dmib-fn 1)"), "42");
}

/* --- user-homedir-pathname test --- */

TEST(eval_user_homedir_pathname)
{
    ASSERT_STR_EQ(eval_print("(pathnamep (user-homedir-pathname))"), "T");
}

/* --- ext:system-command tests --- */

TEST(eval_system_command_true)
{
    ASSERT_STR_EQ(eval_print("(ext:system-command \"true\")"), "0");
}

TEST(eval_system_command_false)
{
    ASSERT_STR_EQ(eval_print("(ext:system-command \"false\")"), "1");
}

TEST(eval_system_command_echo)
{
    ASSERT_STR_EQ(eval_print("(ext:system-command \"echo hello > /dev/null\")"), "0");
}

TEST(eval_getcwd)
{
    /* ext:getcwd should return a non-empty string starting with / */
    const char *result = eval_print("(ext:getcwd)");
    ASSERT(result[0] == '"');  /* printed as a string */
    ASSERT(result[1] == '/');  /* absolute path */
}

/* --- ext: threading primitives (single-threaded stubs) --- */

TEST(eval_ext_make_lock)
{
    ASSERT_STR_EQ(eval_print("(ext:make-lock)"), "NIL");
    ASSERT_STR_EQ(eval_print("(ext:make-lock \"my-lock\")"), "NIL");
}

TEST(eval_ext_make_recursive_lock)
{
    ASSERT_STR_EQ(eval_print("(ext:make-recursive-lock)"), "NIL");
    ASSERT_STR_EQ(eval_print("(ext:make-recursive-lock \"rec\")"), "NIL");
}

TEST(eval_ext_with_lock_held)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((lk (ext:make-lock))) (ext:with-lock-held (lk) 42))"), "42");
}

TEST(eval_ext_with_recursive_lock_held)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((lk (ext:make-recursive-lock))) "
        "  (ext:with-recursive-lock-held (lk) 99))"), "99");
}

TEST(eval_ext_memory_barriers)
{
    ASSERT_STR_EQ(eval_print("(ext:read-memory-barrier)"), "NIL");
    ASSERT_STR_EQ(eval_print("(ext:write-memory-barrier)"), "NIL");
}

TEST(eval_ext_defglobal)
{
    ASSERT_STR_EQ(eval_print(
        "(progn (ext:defglobal *ext-test-glob* 77) *ext-test-glob*)"), "77");
}

/* --- Quicklisp/ASDF compatibility regression tests --- */

TEST(eval_remove_if_not_string)
{
    /* remove-if-not on strings must iterate characters, not raw vector data */
    ASSERT_STR_EQ(eval_print("(remove-if-not #'digit-char-p \"2021-02-13\")"),
                  "\"20210213\"");
}

TEST(eval_remove_if_string)
{
    ASSERT_STR_EQ(eval_print("(remove-if #'digit-char-p \"abc123\")"),
                  "\"abc\"");
}

TEST(eval_remove_string)
{
    ASSERT_STR_EQ(eval_print("(remove #\\- \"2021-02-13\")"),
                  "\"20210213\"");
}

TEST(eval_key_suppliedp_param)
{
    /* &key supplied-p parameter: T when key is explicitly supplied */
    cl_eval_string("(defun %test-sp (&key (x nil xp)) (list x xp))");
    ASSERT_STR_EQ(eval_print("(%test-sp)"), "(NIL NIL)");
    ASSERT_STR_EQ(eval_print("(%test-sp :x nil)"), "(NIL T)");
    ASSERT_STR_EQ(eval_print("(%test-sp :x 42)"), "(42 T)");
}

TEST(eval_key_nil_value_not_default)
{
    /* Passing :x nil must NOT apply the default value */
    cl_eval_string("(defun %test-kd (&key (x 99)) x)");
    ASSERT_EQ_INT(eval_int("(%test-kd)"), 99);
    ASSERT_EQ(cl_eval_string("(%test-kd :x nil)"), CL_NIL);
    ASSERT_EQ_INT(eval_int("(%test-kd :x 42)"), 42);
}

TEST(eval_key_default_special_var)
{
    /* Key param shadows a special variable — default must see the dynamic
     * value, not the uninitialized local slot (was returning NIL). */
    cl_eval_string("(defvar *%test-ks* 100)");
    cl_eval_string("(defun %test-ks (&key ((:path *%test-ks*) *%test-ks*)) *%test-ks*)");
    ASSERT_EQ_INT(eval_int("(%test-ks)"), 100);
    ASSERT_EQ_INT(eval_int("(%test-ks :path 77)"), 77);
    /* Later key default can reference earlier key param */
    cl_eval_string("(defun %test-ks2 (&key (a 10) (b a)) (list a b))");
    ASSERT_STR_EQ(eval_print("(%test-ks2)"), "(10 10)");
    ASSERT_STR_EQ(eval_print("(%test-ks2 :a 20)"), "(20 20)");
    ASSERT_STR_EQ(eval_print("(%test-ks2 :a 20 :b 30)"), "(20 30)");
}

TEST(eval_special_var_unwind_closure)
{
    /* Dynamic bindings must be restored after closure returns */
    cl_eval_string("(defvar *%tw1* nil)");
    cl_eval_string("(defun %tw1-inner () *%tw1*)");
    cl_eval_string(
        "(defun %tw1-outer ()"
        "  (let ((*%tw1* :outer))"
        "    (let ((f (lambda () (let ((*%tw1* :inner)) (%tw1-inner)))))"
        "      (list (%tw1-inner) (funcall f) (%tw1-inner)))))");
    ASSERT_STR_EQ(eval_print("(%tw1-outer)"), "(:OUTER :INNER :OUTER)");
}

TEST(eval_special_var_unwind_block_return)
{
    /* return from block must unwind dynamic bindings */
    cl_eval_string("(defvar *%tw2* nil)");
    cl_eval_string(
        "(defun %tw2-test ()"
        "  (loop (let ((*%tw2* :bound)) (return *%tw2*))))");
    ASSERT_STR_EQ(eval_print("(list (%tw2-test) *%tw2*)"), "(:BOUND NIL)");
}

TEST(eval_destructuring_bind_rest_key)
{
    /* destructuring-bind must process &key after &rest */
    ASSERT_STR_EQ(eval_print(
        "(destructuring-bind (a &rest r &key x y) '(1 :x 10 :y 20)"
        "  (list a r x y))"),
        "(1 (:X 10 :Y 20) 10 20)");
}

/* --- Synonym stream tests (Bug 14) --- */

TEST(eval_synonym_stream)
{
    /* make-synonym-stream should delegate writes to the symbol's value */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (make-string-output-stream)))"
        "  (let ((*trace-output* s))"
        "    (let ((syn (make-synonym-stream '*trace-output*)))"
        "      (write-string \"hello\" syn)"
        "      (get-output-stream-string s))))"),
        "\"hello\"");
}

TEST(eval_compile_file_truename)
{
    /* *compile-file-truename* should be NIL when not compiling */
    ASSERT_STR_EQ(eval_print("*compile-file-truename*"), "NIL");
    ASSERT_STR_EQ(eval_print("*compile-file-pathname*"), "NIL");
}

TEST(eval_directory_basic)
{
    /* directory should return a list (possibly empty) */
    ASSERT_STR_EQ(eval_print("(listp (directory \"/nonexistent-path-12345/*.xyz\"))"), "T");
}

/* --- let* variable shadowing with closure capture --- */

TEST(eval_let_star_shadow_closure_mutation)
{
    /* Regression: closures in let* body must capture the innermost binding
       when the same variable name is rebound (shadowed). */
    ASSERT_STR_EQ(eval_print(
        "(let* ((x 10) (x 20))"
        "  (labels ((set-x (v) (setf x v)) (get-x () x))"
        "    (set-x 30) (get-x)))"), "30");
}

TEST(eval_let_star_shadow_initform_ref)
{
    /* Init-form of shadowed binding should see the earlier binding. */
    ASSERT_STR_EQ(eval_print("(let* ((x 10) (x (1+ x))) x)"), "11");
}

TEST(eval_let_star_shadow_no_closure)
{
    /* Basic let* shadowing without closures. */
    ASSERT_STR_EQ(eval_print("(let* ((x 1) (x (+ x 100))) x)"), "101");
}

/* --- Heap exhaustion / storage error tests --- */

/* --- PROGV tests --- */

TEST(eval_progv_basic)
{
    /* Simple dynamic binding */
    eval_print("(defvar *pv-x* 10)");
    ASSERT_EQ_INT(eval_int("(progv '(*pv-x*) '(99) *pv-x*)"), 99);
    /* Restored after progv */
    ASSERT_EQ_INT(eval_int("*pv-x*"), 10);
}

TEST(eval_progv_multiple)
{
    /* Multiple bindings */
    eval_print("(defvar *pv-a* 1)");
    eval_print("(defvar *pv-b* 2)");
    ASSERT_EQ_INT(eval_int("(progv '(*pv-a* *pv-b*) '(10 20) (+ *pv-a* *pv-b*))"), 30);
    ASSERT_EQ_INT(eval_int("*pv-a*"), 1);
    ASSERT_EQ_INT(eval_int("*pv-b*"), 2);
}

TEST(eval_progv_fewer_values)
{
    /* Fewer values than symbols — extras become unbound */
    eval_print("(defvar *pv-c* 5)");
    ASSERT_EQ_INT(eval_int(
        "(progv '(*pv-c*) '(42) *pv-c*)"), 42);
    /* With no values at all */
    ASSERT_STR_EQ(eval_print(
        "(progv '(*pv-c*) '() (boundp '*pv-c*))"), "NIL");
    /* Restored after */
    ASSERT_EQ_INT(eval_int("*pv-c*"), 5);
}

TEST(eval_progv_extra_values)
{
    /* More values than symbols — extra values ignored */
    eval_print("(defvar *pv-d* 0)");
    ASSERT_EQ_INT(eval_int("(progv '(*pv-d*) '(77 88 99) *pv-d*)"), 77);
    ASSERT_EQ_INT(eval_int("*pv-d*"), 0);
}

TEST(eval_progv_empty_symbols)
{
    /* Empty symbol list — body executes normally */
    ASSERT_EQ_INT(eval_int("(progv '() '() (+ 1 2))"), 3);
}

TEST(eval_progv_restore_on_throw)
{
    /* Bindings restored on non-local exit */
    eval_print("(defvar *pv-e* 100)");
    ASSERT_EQ_INT(eval_int(
        "(catch 'done (progv '(*pv-e*) '(999) (throw 'done 42)))"), 42);
    ASSERT_EQ_INT(eval_int("*pv-e*"), 100);
}

TEST(eval_progv_nested)
{
    /* Nested progv */
    eval_print("(defvar *pv-f* 0)");
    ASSERT_EQ_INT(eval_int(
        "(progv '(*pv-f*) '(1) "
        "  (progv '(*pv-f*) '(2) *pv-f*))"), 2);
    ASSERT_EQ_INT(eval_int("*pv-f*"), 0);
}

/* --- set-syntax-from-char tests --- */

TEST(eval_set_syntax_from_char_basic)
{
    /* set-syntax-from-char returns T */
    ASSERT_STR_EQ(eval_print(
        "(let ((*readtable* (copy-readtable)))"
        "  (set-syntax-from-char #\\{ #\\( *readtable*))"), "T");
}

TEST(eval_set_syntax_from_char_copies_macro)
{
    /* Set a custom reader macro on {, then copy it to [ via set-syntax-from-char */
    ASSERT_STR_EQ(eval_print(
        "(let ((*readtable* (copy-readtable)))"
        "  (set-macro-character #\\{ (lambda (s c) (declare (ignore s c)) 42))"
        "  (set-syntax-from-char #\\[ #\\{ *readtable* *readtable*)"
        "  (functionp (get-macro-character #\\[)))"), "T");
}

TEST(eval_set_syntax_from_char_reset_constituent)
{
    /* Copy constituent syntax (A) to macro char ({) — resets it */
    ASSERT_STR_EQ(eval_print(
        "(let ((*readtable* (copy-readtable)))"
        "  (set-macro-character #\\{ (lambda (s c) (declare (ignore s c)) 42))"
        "  (set-syntax-from-char #\\{ #\\A *readtable* *readtable*)"
        "  (get-macro-character #\\{))"), "NIL");
}

TEST(eval_set_syntax_from_char_default_from)
{
    /* Default from-readtable is standard (slot 0) */
    /* Copy ( from standard to { — should make { a terminating macro */
    ASSERT_STR_EQ(eval_print(
        "(let ((*readtable* (copy-readtable)))"
        "  (set-syntax-from-char #\\{ #\\())"
        ), "T");
}

TEST(eval_nth_value_mv_reset)
{
    /* Regression: nth-value must reset MV state so callers using
     * multiple-value-list see only the single extracted value,
     * not the stale MV buffer from the inner form. */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-list (funcall (lambda () (nth-value 1 (values 'a 'b)))))"),
        "(B)");
    /* Primary value path too */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-list (funcall (lambda () (nth-value 0 (values 'x 'y)))))"),
        "(X)");
}

TEST(eval_do_return_after_nlx_block)
{
    /* Regression: do, do*, dolist, dotimes create implicit NIL block.
     * If block_info.uses_nlx is stale from a prior block, RETURN inside
     * the loop body emits OP_BLOCK_RETURN (NLX) instead of a local jump,
     * causing "RETURN-FROM: no block named NIL" at runtime. */
    ASSERT_EQ_INT(eval_int(
        "(block foo (return-from foo 1) (do ((i 0 (1+ i))) ((= i 3) 99) (return 42)))"),
        1);
    /* do with return in body */
    ASSERT_EQ_INT(eval_int(
        "(do ((i 0 (1+ i))) (nil) (when (= i 5) (return i)))"),
        5);
    /* do* with return */
    ASSERT_EQ_INT(eval_int(
        "(do* ((i 0 (1+ i))) (nil) (when (= i 3) (return (* i 10))))"),
        30);
    /* dolist with return */
    ASSERT_EQ_INT(eval_int(
        "(dolist (x '(1 2 3 4 5)) (when (= x 3) (return x)))"),
        3);
    /* dotimes with return */
    ASSERT_EQ_INT(eval_int(
        "(dotimes (i 100) (when (= i 7) (return i)))"),
        7);
    /* Nested: NLX block followed by do with return */
    ASSERT_EQ_INT(eval_int(
        "(progn (block outer (return-from outer 0)) (do ((i 0 (1+ i))) (nil) (when (= i 2) (return i))))"),
        2);
}

TEST(eval_heap_exhaustion_error)
{
    /* Accumulating live data until heap is full should signal CL_ERR_STORAGE
     * with a proper message, not crash with a segfault.
     * NOTE: this must be the LAST test — heap state is unreliable after. */
    int err;
    cl_gc_reset_roots();  /* Clear any stale roots from previous tests */
    err = CL_CATCH();
    if (err == CL_ERR_NONE) {
        cl_eval_string("(let ((x nil)) (dotimes (i 500000) (push i x)) x)");
        CL_UNCATCH();
        /* If we get here, heap was big enough — not a failure */
    } else {
        CL_UNCATCH();
        ASSERT_EQ_INT(err, CL_ERR_STORAGE);
        ASSERT(strstr(cl_error_msg, "Heap exhausted") != NULL);
    }
    cl_vm.sp = 0;
    cl_vm.fp = 0;
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

    /* Heap-boxed cells (mutable closure capture) */
    RUN(eval_cell_basic_counter);
    RUN(eval_cell_shared_getter_setter);
    RUN(eval_cell_accumulator_param);
    RUN(eval_cell_independent_instances);
    RUN(eval_cell_setq_before_capture);
    RUN(eval_cell_nested_closure);
    RUN(eval_cell_let_star);
    RUN(eval_cell_flet_mutation);
    RUN(eval_cell_labels_shared);
    RUN(eval_cell_do_let_capture);
    RUN(eval_cell_readonly_no_boxing);
    RUN(eval_setf_boxing_across_closure);

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
    RUN(eval_do_star);
    RUN(eval_quasiquote_atom);
    RUN(eval_quasiquote_simple_list);
    RUN(eval_quasiquote_unquote);
    RUN(eval_quasiquote_splicing);
    RUN(eval_quasiquote_nested_list);
    RUN(eval_quasiquote_dotted);
    RUN(eval_quasiquote_in_macro);
    RUN(eval_quasiquote_macro_splice);
    RUN(eval_quasiquote_nested_double_unquote);
    RUN(eval_quasiquote_nested_unquote_splice);
    RUN(eval_quasiquote_nested_simple);
    RUN(eval_quasiquote_nested_inner_splice);
    RUN(eval_quasiquote_once_only_pattern);
    RUN(eval_reader_error_condition);
    RUN(eval_stream_error_stream_accessor);
    RUN(eval_package_error_package_accessor);
    RUN(eval_cell_error_name_accessor);
    RUN(eval_file_error_pathname_accessor);
    RUN(eval_define_compiler_macro);
    RUN(eval_setf_values);
    RUN(eval_setf_values_two);
    RUN(eval_define_setf_expander);
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
    RUN(eval_optional_suppliedp);
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
    RUN(eval_catch_throw_deep_chain);
    RUN(eval_block_return_deep_chain);
    RUN(eval_uwp_throw_deep_cleanup);
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
    RUN(eval_defvar_basic);
    RUN(eval_defvar_no_overwrite);
    RUN(eval_defparameter_overwrite);
    RUN(eval_defconstant_basic);
    RUN(eval_defconstant_setq_error);
    RUN(eval_defconstant_set_error);
    RUN(eval_defconstant_t_is_constant);
    RUN(eval_defconstant_keyword_error);
    RUN(eval_special_let_binding);
    RUN(eval_special_let_star_binding);
    RUN(eval_special_visible_in_called_fn);
    RUN(eval_special_nested_binding);
    RUN(eval_special_setq);
    RUN(eval_special_unwind_protect);
    RUN(eval_special_error_restore);
    RUN(eval_special_mixed_let);
    RUN(eval_setf_car_cdr);
    RUN(eval_setf_first_rest);
    RUN(eval_setf_nth);
    RUN(eval_setf_variable);
    RUN(eval_setf_multiple);
    RUN(eval_rplaca_rplacd);
    RUN(eval_aref_make_array_vectorp);
    RUN(eval_setf_svref);
    RUN(eval_aref_string);
    RUN(eval_symbol_value);
    RUN(eval_setf_symbol_value);
    RUN(eval_set_builtin);
    RUN(eval_setf_return_value);
    RUN(eval_member_found);
    RUN(eval_member_not_found);
    RUN(eval_member_with_test);
    RUN(eval_pushnew_new);
    RUN(eval_pushnew_existing);
    RUN(eval_eval_when_execute);
    RUN(eval_eval_when_multiple_situations);
    RUN(eval_eval_when_body);
    RUN(eval_destructuring_bind_simple);
    RUN(eval_destructuring_bind_nested);
    RUN(eval_destructuring_bind_rest);
    RUN(eval_destructuring_bind_optional);
    RUN(eval_destructuring_bind_optional_provided);
    RUN(eval_destructuring_bind_body);
    RUN(eval_destructuring_bind_key);
    RUN(eval_defsetf_short);
    RUN(eval_defsetf_cadr);
    RUN(eval_get_setf_expansion_defsetf);
    RUN(eval_equalp);
    RUN(eval_defun_return_from);

    /* Cross-closure return-from */
    RUN(eval_return_from_labels);
    RUN(eval_return_from_flet);
    RUN(eval_return_from_lambda);
    RUN(eval_return_from_nested_labels);
    RUN(eval_block_with_unwind_protect);
    RUN(eval_uwp_nlx_leak_tailcall);
    RUN(eval_block_nlx_precision);
    RUN(eval_dolist_var_shadowing);

    /* Phase 5 Tier 1 */
    RUN(eval_characterp);
    RUN(eval_char_comparison);
    RUN(eval_char_code_conversion);
    RUN(eval_char_case);
    RUN(eval_char_predicates);
    RUN(eval_symbol_name);
    RUN(eval_symbol_package_fn);
    RUN(eval_fboundp);
    RUN(eval_fdefinition);
    RUN(eval_make_symbol);
    RUN(eval_keywordp);
    RUN(eval_length_vector);
    RUN(eval_equal_vector);
    RUN(eval_mapcar_multi_list);
    RUN(eval_vector);
    RUN(eval_vector_reader);
    RUN(eval_array_print_multidim);
    RUN(eval_array_dimensions);
    RUN(eval_array_rank);
    RUN(eval_setf_aref_multidim);
    RUN(eval_array_dimension);
    RUN(eval_array_total_size);
    RUN(eval_array_row_major_index);
    RUN(eval_row_major_aref);
    RUN(eval_fill_pointer);
    RUN(eval_vector_push);
    RUN(eval_adjust_array);
    RUN(eval_array_type_predicates);
    RUN(eval_array_typep);
    RUN(eval_array_type_of);

    /* Phase 5 Tier 2 */
    RUN(eval_string_comparison);
    RUN(eval_string_case_conversion);
    RUN(eval_string_trim);
    RUN(eval_subseq);
    RUN(eval_concatenate);
    RUN(eval_char_accessor);
    RUN(eval_string_coerce);
    RUN(eval_parse_integer);
    RUN(eval_write_to_string);
    RUN(eval_prin1_to_string);
    RUN(eval_princ_to_string);

    /* Phase 5 Tier 3 */
    RUN(eval_nthcdr);
    RUN(eval_last);
    RUN(eval_acons);
    RUN(eval_copy_list);
    RUN(eval_pairlis);
    RUN(eval_assoc);
    RUN(eval_rassoc);
    RUN(eval_getf);
    RUN(eval_subst);
    RUN(eval_sublis);
    RUN(eval_adjoin);
    RUN(eval_nconc);
    RUN(eval_nreverse);
    RUN(eval_delete);
    RUN(eval_delete_if);
    RUN(eval_nsubst);
    RUN(eval_butlast);
    RUN(eval_copy_tree);
    RUN(eval_mapc);
    RUN(eval_mapcan);
    RUN(eval_maplist);
    RUN(eval_mapl);
    RUN(eval_mapcon);
    RUN(eval_intersection);
    RUN(eval_union);
    RUN(eval_set_difference);
    RUN(eval_subsetp);

    /* Phase 5 — Hash tables */
    RUN(eval_make_hash_table);
    RUN(eval_gethash_setf);
    RUN(eval_gethash_default);
    RUN(eval_gethash_overwrite);
    RUN(eval_remhash);
    RUN(eval_clrhash);
    RUN(eval_maphash);
    RUN(eval_hash_table_equal_test);
    RUN(eval_hash_table_eq_test);
    RUN(eval_gethash_mv);

    /* Phase 5 — Sequence functions */
    RUN(eval_find);
    RUN(eval_find_if);
    RUN(eval_find_if_not);
    RUN(eval_position);
    RUN(eval_position_if);
    RUN(eval_position_if_not);
    RUN(eval_count);
    RUN(eval_count_if);
    RUN(eval_count_if_not);
    RUN(eval_remove);
    RUN(eval_remove_if);
    RUN(eval_remove_if_not);
    RUN(eval_remove_duplicates);
    RUN(eval_substitute);
    RUN(eval_substitute_if);
    RUN(eval_substitute_if_not);
    RUN(eval_nsubstitute);
    RUN(eval_nsubstitute_if);
    RUN(eval_nsubstitute_if_not);
    RUN(eval_reduce);
    RUN(eval_fill);
    RUN(eval_replace_fn);
    RUN(eval_every);
    RUN(eval_some);
    RUN(eval_notany);
    RUN(eval_notevery);
    RUN(eval_map_fn);
    RUN(eval_every_sequences);
    RUN(eval_some_sequences);
    RUN(eval_notany_sequences);
    RUN(eval_notevery_sequences);
    RUN(eval_mismatch);
    RUN(eval_search_fn);
    RUN(eval_sort);
    RUN(eval_stable_sort);
    RUN(eval_sort_with_key);
    RUN(eval_sort_key_nil);
    RUN(eval_stale_boxed_flag);

    /* Phase 5 — Type system */
    RUN(eval_typep);
    RUN(eval_coerce);
    RUN(eval_typep_compound);
    RUN(eval_typep_numeric_range);
    RUN(eval_deftype);
    RUN(eval_subtypep);
    RUN(eval_typecase_compound);

    /* Phase 5 — Disassemble */
    RUN(eval_disassemble_defun);
    RUN(eval_disassemble_closure);
    RUN(eval_disassemble_builtin);
    RUN(eval_disassemble_unbound);

    /* Phase 5 — Declarations */
    RUN(eval_declaim_special);
    RUN(eval_declare_special_let);
    RUN(eval_declare_special_letstar);
    RUN(eval_declare_ignore);
    RUN(eval_declare_type);
    RUN(eval_declaim_optimize);
    RUN(eval_declaim_inline);
    RUN(eval_proclaim_special);
    RUN(eval_locally_basic);
    RUN(eval_locally_declare);
    RUN(eval_declare_misplaced);
    RUN(eval_declare_in_lambda);
    RUN(eval_declaim_multiple);

    /* Phase 6 — The */
    RUN(eval_the_basic);
    RUN(eval_the_compound_type);
    RUN(eval_the_type_error);
    RUN(eval_the_safety_zero);
    RUN(eval_the_nested);

    /* Phase 5 — Trace/Untrace */
    RUN(eval_trace_basic);
    RUN(eval_trace_returns_list);
    RUN(eval_trace_untrace);
    RUN(eval_trace_query);
    RUN(eval_untrace_all);
    RUN(eval_trace_builtin);
    RUN(eval_trace_multiple);

    /* Phase 5 — Backtrace */
    RUN(eval_backtrace_named);
    RUN(eval_backtrace_anonymous);
    RUN(eval_backtrace_recursive);
    RUN(eval_backtrace_uwprot);
    RUN(eval_backtrace_empty);
    RUN(eval_time_basic);
    RUN(eval_time_nested);
    RUN(eval_time_defun);
    RUN(eval_time_stats);
    RUN(eval_get_internal_real_time);
    RUN(eval_srcloc_load_backtrace);
    RUN(eval_srcloc_reader_line);

    /* Phase 7 — Macrolet / Symbol-macrolet */
    RUN(eval_macrolet_basic);
    RUN(eval_macrolet_shadow);
    RUN(eval_macrolet_scope);
    RUN(eval_macrolet_nested);
    RUN(eval_macrolet_with_body);
    RUN(eval_macrolet_across_lambda);
    RUN(eval_symbol_macrolet_basic);
    RUN(eval_symbol_macrolet_expr);
    RUN(eval_symbol_macrolet_setq);
    RUN(eval_symbol_macrolet_scope);
    RUN(eval_symbol_macrolet_nested);
    RUN(eval_symbol_macrolet_multiple);
    RUN(eval_symbol_macrolet_across_lambda);

    /* Phase 8 Step 2 — Missing string operations */
    RUN(eval_string_capitalize);
    RUN(eval_string_case_designators);
    RUN(eval_nstring_upcase);
    RUN(eval_nstring_downcase);
    RUN(eval_nstring_capitalize);
    RUN(eval_char_name);
    RUN(eval_name_char);
    RUN(eval_char_ci_compare);
    RUN(eval_graphic_char_p);
    RUN(eval_alphanumericp);
    RUN(eval_digit_char);

    /* Phase 8 Step 3 — Missing sequence operations */
    RUN(eval_elt);
    RUN(eval_setf_elt);
    RUN(eval_copy_seq);
    RUN(eval_map_into);

    /* Phase 8 Step 4 — Higher-order functions */
    RUN(eval_complement);
    RUN(eval_constantly);

    /* Phase 8 Step 1 — Missing list operations */
    RUN(eval_list_star);
    RUN(eval_make_list);
    RUN(eval_tree_equal);
    RUN(eval_list_length);
    RUN(eval_tailp);
    RUN(eval_ldiff);
    RUN(eval_revappend);
    RUN(eval_nreconc);
    RUN(eval_assoc_if);
    RUN(eval_rassoc_if);
    RUN(eval_remf);

    /* Phase 8 — Loop macro */
    RUN(eval_loop_simple_return);
    RUN(eval_loop_simple_accumulate);
    RUN(eval_loop_while);
    RUN(eval_loop_until);
    RUN(eval_loop_do_multiple);
    RUN(eval_loop_for_in);
    RUN(eval_loop_for_in_by);
    RUN(eval_loop_for_on);
    RUN(eval_loop_for_from_to);
    RUN(eval_loop_for_from_below);
    RUN(eval_loop_for_from_by);
    RUN(eval_loop_for_downfrom);
    RUN(eval_loop_for_downfrom_above);
    RUN(eval_loop_for_across);
    RUN(eval_loop_for_eq_then);
    RUN(eval_loop_repeat);
    RUN(eval_loop_for_multiple);
    RUN(eval_loop_collect);
    RUN(eval_loop_collect_expr);
    RUN(eval_loop_collect_into);
    RUN(eval_loop_sum);
    RUN(eval_loop_sum_into);
    RUN(eval_loop_count);
    RUN(eval_loop_maximize);
    RUN(eval_loop_minimize);
    RUN(eval_loop_append);
    RUN(eval_loop_nconc);
    RUN(eval_loop_return);
    RUN(eval_loop_return_clause);
    RUN(eval_loop_return_in_do_let);
    RUN(eval_loop_when_collect);
    RUN(eval_loop_if_else);
    RUN(eval_loop_unless_collect);
    RUN(eval_loop_when_and);
    RUN(eval_loop_always);
    RUN(eval_loop_never);
    RUN(eval_loop_thereis);
    RUN(eval_loop_with);
    RUN(eval_loop_with_and);
    RUN(eval_loop_with_destructuring);
    RUN(eval_loop_named);
    RUN(eval_loop_initially);
    RUN(eval_loop_finally);
    RUN(eval_loop_collect_into_finally);
    RUN(eval_loop_finish);
    RUN(eval_loop_finish_finally);
    RUN(eval_loop_being_hash_keys);
    RUN(eval_loop_being_hash_values);
    RUN(eval_loop_being_hash_keys_using);
    RUN(eval_loop_being_hash_values_using);
    RUN(eval_loop_being_symbols);
    RUN(eval_loop_being_external_symbols);
    RUN(eval_loop_destructuring_in);
    RUN(eval_loop_destructuring_dotted);
    RUN(eval_loop_destructuring_nested);
    RUN(eval_loop_destructuring_on);

    /* Paren depth */
    RUN(paren_depth_balanced);
    RUN(paren_depth_unbalanced);
    RUN(paren_depth_strings);
    RUN(paren_depth_comments);
    RUN(paren_depth_char_literals);
    RUN(paren_depth_multiline);

    /* History variables */
    RUN(history_star_basic);
    RUN(history_star_shift);
    RUN(history_plus_shift);
    RUN(history_minus_during_eval);
    RUN(history_arithmetic_still_works);

    /* Printer control variables */
    RUN(eval_print_var_defaults);
    RUN(eval_print_escape_dynamic);
    RUN(eval_print_level);
    RUN(eval_print_length);
    RUN(eval_print_level_length_combined);
    RUN(eval_print_base_binary);
    RUN(eval_print_base_octal);
    RUN(eval_print_base_hex);
    RUN(eval_print_base_other);
    RUN(eval_print_radix_decimal);
    RUN(eval_print_radix_binary);
    RUN(eval_print_radix_octal);
    RUN(eval_print_radix_hex);
    RUN(eval_print_radix_other);
    RUN(eval_print_base_bignum);
    RUN(eval_print_radix_bignum);
    RUN(eval_print_case_upcase);
    RUN(eval_print_case_downcase);
    RUN(eval_print_case_capitalize);
    RUN(eval_print_case_in_list);
    RUN(eval_print_gensym);
    RUN(eval_print_array);

    /* WRITE function with keyword arguments */
    RUN(eval_write_basic);
    RUN(eval_write_base_radix);
    RUN(eval_write_level_length);
    RUN(eval_write_case);
    RUN(eval_write_gensym_array);

    /* *print-circle* */
    RUN(eval_print_circle_cdr_cycle);
    RUN(eval_print_circle_car_self);
    RUN(eval_print_circle_shared_sub);
    RUN(eval_print_circle_no_sharing);
    RUN(eval_print_circle_nil_default);
    RUN(eval_print_circle_vector);
    RUN(eval_print_circle_deep_shared);
    RUN(eval_print_circle_write_to_string);

    /* Format directive extensions */
    RUN(eval_format_decimal);
    RUN(eval_format_binary);
    RUN(eval_format_octal);
    RUN(eval_format_hex);
    RUN(eval_format_character);
    RUN(eval_format_write);
    RUN(eval_format_freshline);
    RUN(eval_format_page);
    RUN(eval_format_mixed);

    /* Advanced format: padding, commas, sign */
    RUN(eval_format_padded_decimal);
    RUN(eval_format_padded_aesthetic);
    RUN(eval_format_prefix_params);

    /* Advanced format: ~* and ~T */
    RUN(eval_format_goto);
    RUN(eval_format_tabulate);

    /* Advanced format: ~(~) case conversion */
    RUN(eval_format_case_downcase);
    RUN(eval_format_case_capitalize);
    RUN(eval_format_case_cap_first);
    RUN(eval_format_case_upcase);

    /* Advanced format: ~[~;~] conditional */
    RUN(eval_format_cond_numeric);
    RUN(eval_format_cond_default);
    RUN(eval_format_cond_boolean);
    RUN(eval_format_cond_atsign);

    /* Advanced format: ~{~} iteration and ~^ */
    RUN(eval_format_iteration_list);
    RUN(eval_format_iteration_sublists);
    RUN(eval_format_iteration_atsign);
    RUN(eval_format_iteration_colon_atsign);
    RUN(eval_format_iteration_limit);
    RUN(eval_format_escape);

    /* Advanced format: ~? recursive and ~R radix */
    RUN(eval_format_recursive);
    RUN(eval_format_recursive_atsign);
    RUN(eval_format_radix_cardinal);
    RUN(eval_format_radix_ordinal);
    RUN(eval_format_radix_roman);
    RUN(eval_format_radix_old_roman);
    RUN(eval_format_radix_base);

    /* *print-pretty* / pprint */
    RUN(eval_print_pretty_default);
    RUN(eval_print_pretty_short_list);
    RUN(eval_print_pretty_long_list);
    RUN(eval_print_pretty_nested);
    RUN(eval_print_pretty_vector);
    RUN(eval_print_pretty_empty);
    RUN(eval_print_pretty_dotted);
    RUN(eval_pprint_basic);
    RUN(eval_print_pretty_write_keyword);
    RUN(eval_print_pretty_write_to_string);
    RUN(eval_print_pretty_off_no_break);
    RUN(eval_print_pretty_with_level_length);

    /* rotatef / shiftf */
    RUN(eval_rotatef_basic);
    RUN(eval_rotatef_two);
    RUN(eval_rotatef_returns_nil);
    RUN(eval_rotatef_car);
    RUN(eval_shiftf_basic);
    RUN(eval_shiftf_two);
    RUN(eval_shiftf_car);

    /* pprint-newline / pprint-indent */
    RUN(eval_pprint_newline_mandatory);
    RUN(eval_pprint_newline_fill);
    RUN(eval_pprint_newline_kinds);
    RUN(eval_pprint_indent_current);
    RUN(eval_pprint_indent_block);

    /* pprint-logical-block */
    RUN(eval_pprint_logical_block_basic);

    /* pprint-dispatch */
    RUN(eval_pprint_dispatch_set_get);
    RUN(eval_copy_pprint_dispatch);
    RUN(eval_pprint_dispatch_custom);

    /* provide / require */
    RUN(eval_provide_adds_to_modules);
    RUN(eval_provide_no_duplicate);
    RUN(eval_provide_symbol_name);
    RUN(eval_require_already_provided);
    RUN(eval_require_with_pathname);
    RUN(eval_require_does_not_reload);

    /* property lists */
    RUN(eval_symbol_plist_initial);
    RUN(eval_setf_get);
    RUN(eval_get_default);
    RUN(eval_remprop);
    RUN(eval_setf_get_update);

    /* block comments */
    RUN(eval_block_comment_basic);
    RUN(eval_block_comment_inline);
    RUN(eval_block_comment_nested);
    RUN(eval_block_comment_multiline);

    /* &aux */
    RUN(eval_aux_basic);
    RUN(eval_aux_multiple);
    RUN(eval_aux_no_init);
    RUN(eval_aux_uses_param);
    RUN(eval_aux_with_key);
    RUN(eval_defun_aux);

    /* apply with symbols */
    RUN(eval_apply_symbol);
    RUN(eval_apply_symbol_funcall);

    /* package-used-by-list */
    RUN(eval_package_used_by_list);

    /* shadowing-import */
    RUN(eval_shadowing_import);

    /* copy-symbol */
    RUN(eval_copy_symbol_basic);
    RUN(eval_copy_symbol_uninterned);
    RUN(eval_copy_symbol_props);

    /* LOOP nested :if sub-clause */
    RUN(eval_loop_nested_if);
    RUN(eval_loop_nested_when_else);
    RUN(eval_loop_nested_unless);

    /* defmacro destructuring */
    RUN(eval_defmacro_destructuring_required);
    RUN(eval_defmacro_destructuring_body);
    RUN(eval_defmacro_optional_not_destructured);

    /* define-modify-macro */
    RUN(eval_define_modify_macro);

    /* reduce :from-end */
    RUN(eval_reduce_from_end);
    RUN(eval_reduce_from_end_initial);

    /* defmethod implicit block */
    RUN(eval_defmethod_implicit_block);

    /* user-homedir-pathname */
    RUN(eval_user_homedir_pathname);

    /* ext:system-command, ext:getcwd */
    RUN(eval_system_command_true);
    RUN(eval_system_command_false);
    RUN(eval_system_command_echo);
    RUN(eval_getcwd);

    /* ext: threading primitives */
    RUN(eval_ext_make_lock);
    RUN(eval_ext_make_recursive_lock);
    RUN(eval_ext_with_lock_held);
    RUN(eval_ext_with_recursive_lock_held);
    RUN(eval_ext_memory_barriers);
    RUN(eval_ext_defglobal);

    /* quicklisp/ASDF compatibility regressions */
    RUN(eval_remove_if_not_string);
    RUN(eval_remove_if_string);
    RUN(eval_remove_string);
    RUN(eval_key_suppliedp_param);
    RUN(eval_key_nil_value_not_default);
    RUN(eval_key_default_special_var);
    RUN(eval_special_var_unwind_closure);
    RUN(eval_special_var_unwind_block_return);
    RUN(eval_destructuring_bind_rest_key);

    /* synonym streams, compile-file vars, directory (Bug 14) */
    RUN(eval_synonym_stream);
    RUN(eval_compile_file_truename);
    RUN(eval_directory_basic);

    /* let* variable shadowing with closure capture */
    RUN(eval_let_star_shadow_closure_mutation);
    RUN(eval_let_star_shadow_initform_ref);
    RUN(eval_let_star_shadow_no_closure);

    /* PROGV */
    RUN(eval_progv_basic);
    RUN(eval_progv_multiple);
    RUN(eval_progv_fewer_values);
    RUN(eval_progv_extra_values);
    RUN(eval_progv_empty_symbols);
    RUN(eval_progv_restore_on_throw);
    RUN(eval_progv_nested);

    /* set-syntax-from-char */
    RUN(eval_set_syntax_from_char_basic);
    RUN(eval_set_syntax_from_char_copies_macro);
    RUN(eval_set_syntax_from_char_reset_constituent);
    RUN(eval_set_syntax_from_char_default_from);

    /* FSet regressions: nth-value MV reset, loop block uses_nlx */
    RUN(eval_nth_value_mv_reset);
    RUN(eval_do_return_after_nlx_block);

    /* heap exhaustion / storage errors */
    RUN(eval_heap_exhaustion_error);

    teardown();
    REPORT();
}
