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

/* === Step 1: %class-of builtin === */

TEST(class_of_fixnum)
{
    ASSERT_STR_EQ(eval_print("(%class-of 42)"), "FIXNUM");
}

TEST(class_of_nil)
{
    ASSERT_STR_EQ(eval_print("(%class-of nil)"), "NULL");
}

TEST(class_of_symbol)
{
    ASSERT_STR_EQ(eval_print("(%class-of 'foo)"), "SYMBOL");
}

TEST(class_of_cons)
{
    ASSERT_STR_EQ(eval_print("(%class-of '(1 2))"), "CONS");
}

TEST(class_of_string)
{
    ASSERT_STR_EQ(eval_print("(%class-of \"hello\")"), "STRING");
}

TEST(class_of_character)
{
    ASSERT_STR_EQ(eval_print("(%class-of #\\A)"), "CHARACTER");
}

TEST(class_of_function)
{
    ASSERT_STR_EQ(eval_print("(%class-of #'car)"), "FUNCTION");
}

TEST(class_of_lambda)
{
    ASSERT_STR_EQ(eval_print("(%class-of (lambda (x) x))"), "FUNCTION");
}

TEST(class_of_vector)
{
    ASSERT_STR_EQ(eval_print("(%class-of (vector 1 2 3))"), "VECTOR");
}

TEST(class_of_hashtable)
{
    ASSERT_STR_EQ(eval_print("(%class-of (make-hash-table))"), "HASH-TABLE");
}

TEST(class_of_struct)
{
    eval_print("(defstruct clos-test-pt (x 0) (y 0))");
    ASSERT_STR_EQ(eval_print("(%class-of (make-clos-test-pt :x 1 :y 2))"), "CLOS-TEST-PT");
}

TEST(class_of_float)
{
    ASSERT_STR_EQ(eval_print("(%class-of 3.14)"), "SINGLE-FLOAT");
}

TEST(class_of_ratio)
{
    ASSERT_STR_EQ(eval_print("(%class-of 1/3)"), "RATIO");
}

TEST(class_of_pathname)
{
    ASSERT_STR_EQ(eval_print("(%class-of #P\"/tmp/foo\")"), "PATHNAME");
}

TEST(class_of_package)
{
    ASSERT_STR_EQ(eval_print("(%class-of (find-package :cl))"), "PACKAGE");
}

TEST(class_of_stream)
{
    ASSERT_STR_EQ(eval_print("(%class-of (make-string-input-stream \"hi\"))"), "STREAM");
}

/* === Step 2: Bootstrap core classes === */

/* Load CLOS module */
TEST(clos_require)
{
    ASSERT_STR_EQ(eval_print("(require \"clos\")"), "T");
}

/* find-class returns a class metaobject */
TEST(find_class_integer)
{
    ASSERT_STR_EQ(eval_print("(structurep (find-class 'integer))"), "T");
}

/* class-name returns the class name symbol */
TEST(class_name_integer)
{
    ASSERT_STR_EQ(eval_print("(class-name (find-class 'integer))"), "INTEGER");
}

TEST(class_name_string)
{
    ASSERT_STR_EQ(eval_print("(class-name (find-class 'string))"), "STRING");
}

TEST(class_name_cons)
{
    ASSERT_STR_EQ(eval_print("(class-name (find-class 'cons))"), "CONS");
}

TEST(class_name_t)
{
    ASSERT_STR_EQ(eval_print("(class-name (find-class 't))"), "T");
}

/* find-class with errorp nil returns nil for unknown */
TEST(find_class_unknown_nil)
{
    ASSERT_STR_EQ(eval_print("(find-class 'no-such-class nil)"), "NIL");
}

/* find-class with errorp t (default) signals error */
TEST(find_class_unknown_error)
{
    ASSERT(strncmp(eval_print("(find-class 'no-such-class)"), "ERROR:", 6) == 0);
}

/* class-of returns class metaobject for built-in types */
TEST(class_of_42_is_fixnum_class)
{
    ASSERT_STR_EQ(eval_print("(class-name (class-of 42))"), "FIXNUM");
}

TEST(class_of_string_is_string_class)
{
    ASSERT_STR_EQ(eval_print("(class-name (class-of \"hello\"))"), "STRING");
}

TEST(class_of_nil_is_null_class)
{
    ASSERT_STR_EQ(eval_print("(class-name (class-of nil))"), "NULL");
}

TEST(class_of_cons_is_cons_class)
{
    ASSERT_STR_EQ(eval_print("(class-name (class-of '(1 2)))"), "CONS");
}

/* class-of returns class object (same as find-class) */
TEST(class_of_eq_find_class)
{
    ASSERT_STR_EQ(eval_print("(eq (class-of 42) (find-class 'fixnum))"), "T");
}

/* class-direct-superclasses */
TEST(direct_supers_integer)
{
    ASSERT_STR_EQ(eval_print(
        "(class-name (car (class-direct-superclasses (find-class 'integer))))"),
        "RATIONAL");
}

TEST(direct_supers_t)
{
    ASSERT_STR_EQ(eval_print(
        "(class-direct-superclasses (find-class 't))"), "NIL");
}

/* class-precedence-list */
TEST(cpl_fixnum)
{
    ASSERT_STR_EQ(eval_print(
        "(mapcar #'class-name (class-precedence-list (find-class 'fixnum)))"),
        "(FIXNUM INTEGER RATIONAL REAL NUMBER T)");
}

TEST(cpl_cons)
{
    ASSERT_STR_EQ(eval_print(
        "(mapcar #'class-name (class-precedence-list (find-class 'cons)))"),
        "(CONS T)");
}

TEST(cpl_null)
{
    ASSERT_STR_EQ(eval_print(
        "(mapcar #'class-name (class-precedence-list (find-class 'null)))"),
        "(NULL SYMBOL T)");
}

/* setf find-class */
TEST(setf_find_class)
{
    eval_print("(progn (setf (find-class 'my-test-class) (find-class 'integer)) nil)");
    ASSERT_STR_EQ(eval_print("(class-name (find-class 'my-test-class))"), "INTEGER");
}

/* class-direct-subclasses */
TEST(direct_subclasses_integer)
{
    /* INTEGER should have FIXNUM and BIGNUM as direct subclasses */
    ASSERT_STR_EQ(eval_print(
        "(let ((subs (mapcar #'class-name (class-direct-subclasses (find-class 'integer)))))"
        "  (and (member 'fixnum subs) (member 'bignum subs) t))"),
        "T");
}

int main(void)
{
    test_init();
    setup();

    /* Step 1: %class-of */
    RUN(class_of_fixnum);
    RUN(class_of_nil);
    RUN(class_of_symbol);
    RUN(class_of_cons);
    RUN(class_of_string);
    RUN(class_of_character);
    RUN(class_of_function);
    RUN(class_of_lambda);
    RUN(class_of_vector);
    RUN(class_of_hashtable);
    RUN(class_of_struct);
    RUN(class_of_float);
    RUN(class_of_ratio);
    RUN(class_of_pathname);
    RUN(class_of_package);
    RUN(class_of_stream);

    /* Step 2: Bootstrap core classes (requires loading clos.lisp) */
    RUN(clos_require);
    RUN(find_class_integer);
    RUN(class_name_integer);
    RUN(class_name_string);
    RUN(class_name_cons);
    RUN(class_name_t);
    RUN(find_class_unknown_nil);
    RUN(find_class_unknown_error);
    RUN(class_of_42_is_fixnum_class);
    RUN(class_of_string_is_string_class);
    RUN(class_of_nil_is_null_class);
    RUN(class_of_cons_is_cons_class);
    RUN(class_of_eq_find_class);
    RUN(direct_supers_integer);
    RUN(direct_supers_t);
    RUN(cpl_fixnum);
    RUN(cpl_cons);
    RUN(cpl_null);
    RUN(setf_find_class);
    RUN(direct_subclasses_integer);

    teardown();
    REPORT();
}
