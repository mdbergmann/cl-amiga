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
        return buf;
    }
}

/* Helper: eval and get fixnum value */
static int eval_int(const char *str)
{
    CL_Obj result = cl_eval_string(str);
    return CL_FIXNUM_VAL(result);
}

/* Helper: eval and expect an error, return 1 if error occurred */
static int eval_errors(const char *str)
{
    int err = CL_CATCH();
    if (err == CL_ERR_NONE) {
        cl_eval_string(str);
        CL_UNCATCH();
        return 0;
    } else {
        CL_UNCATCH();
        return 1;
    }
}

/* ============================================================ */
/* make-array: 1D construction                                  */
/* ============================================================ */

TEST(make_array_simple)
{
    /* Basic 1D array */
    ASSERT_EQ_INT(eval_int("(length (make-array 5))"), 5);
    ASSERT_EQ_INT(eval_int("(array-rank (make-array 5))"), 1);
    /* Default elements are NIL */
    ASSERT_STR_EQ(eval_print("(aref (make-array 3) 0)"), "NIL");
}

TEST(make_array_initial_element)
{
    ASSERT_EQ_INT(eval_int("(aref (make-array 3 :initial-element 42) 0)"), 42);
    ASSERT_EQ_INT(eval_int("(aref (make-array 3 :initial-element 42) 2)"), 42);
}

TEST(make_array_initial_contents)
{
    ASSERT_STR_EQ(eval_print("(make-array 3 :initial-contents '(10 20 30))"),
        "#(10 20 30)");
}

TEST(make_array_fill_pointer)
{
    /* :fill-pointer T starts at array size (CLHS) */
    ASSERT_EQ_INT(eval_int("(fill-pointer (make-array 10 :fill-pointer t))"), 10);
    /* :fill-pointer integer */
    ASSERT_EQ_INT(eval_int("(fill-pointer (make-array 10 :fill-pointer 5))"), 5);
    /* length respects fill pointer */
    ASSERT_EQ_INT(eval_int(
        "(length (make-array 10 :fill-pointer 3))"), 3);
}

TEST(make_array_fill_pointer_t_string)
{
    /* :fill-pointer T for character arrays should init to array size */
    ASSERT_EQ_INT(eval_int(
        "(fill-pointer (make-array 3 :element-type 'character :fill-pointer t :adjustable t))"), 3);
    /* Verify content is accessible */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (make-array 2 :element-type 'character :fill-pointer t :adjustable t)))"
        "  (setf (aref s 0) #\\a) (setf (aref s 1) #\\b) s)"), "\"ab\"");
    /* vector-push-extend works after fill-pointer T */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (make-array 2 :element-type 'character :fill-pointer t :adjustable t)))"
        "  (setf (aref s 0) #\\x) (setf (aref s 1) #\\y)"
        "  (vector-push-extend #\\z s) s)"), "\"xyz\"");
}

TEST(make_array_initial_contents_string)
{
    /* :initial-contents with a string source */
    ASSERT_STR_EQ(eval_print(
        "(coerce (make-array 3 :element-type 'character :fill-pointer t :adjustable t"
        "                    :initial-contents \"abc\") 'string)"), "\"abc\"");
    /* :initial-contents with a vector source */
    ASSERT_STR_EQ(eval_print(
        "(make-array 3 :element-type 'character :initial-contents '(#\\x #\\y #\\z))"), "\"xyz\"");
}

TEST(make_array_adjustable)
{
    ASSERT_STR_EQ(eval_print(
        "(adjustable-array-p (make-array 5 :adjustable t))"), "T");
    ASSERT_STR_EQ(eval_print(
        "(adjustable-array-p (make-array 5))"), "NIL");
}

/* ============================================================ */
/* make-array: multi-dimensional                                */
/* ============================================================ */

TEST(make_array_2d)
{
    ASSERT_STR_EQ(eval_print("(array-dimensions (make-array '(2 3)))"), "(2 3)");
    ASSERT_EQ_INT(eval_int("(array-rank (make-array '(2 3)))"), 2);
    ASSERT_EQ_INT(eval_int("(array-total-size (make-array '(2 3)))"), 6);
}

TEST(make_array_3d)
{
    ASSERT_EQ_INT(eval_int("(array-rank (make-array '(2 3 4)))"), 3);
    ASSERT_EQ_INT(eval_int("(array-total-size (make-array '(2 3 4)))"), 24);
    ASSERT_STR_EQ(eval_print("(array-dimensions (make-array '(2 3 4)))"), "(2 3 4)");
}

TEST(make_array_2d_initial_contents)
{
    ASSERT_STR_EQ(eval_print(
        "(make-array '(2 3) :initial-contents '((1 2 3) (4 5 6)))"),
        "#2A((1 2 3) (4 5 6))");
}

TEST(make_array_3d_initial_contents)
{
    ASSERT_STR_EQ(eval_print(
        "(make-array '(2 2 2) :initial-contents '(((1 2) (3 4)) ((5 6) (7 8))))"),
        "#3A(((1 2) (3 4)) ((5 6) (7 8)))");
}

TEST(make_array_2d_initial_element)
{
    ASSERT_EQ_INT(eval_int(
        "(aref (make-array '(2 3) :initial-element 99) 1 2)"), 99);
}

TEST(make_array_empty_dims)
{
    /* Empty rows */
    ASSERT_STR_EQ(eval_print("(make-array '(2 0))"), "#2A(() ())");
    ASSERT_STR_EQ(eval_print("(make-array '(0 3))"), "#2A()");
    ASSERT_EQ_INT(eval_int("(array-total-size (make-array '(0 3)))"), 0);
}

/* ============================================================ */
/* vector function                                              */
/* ============================================================ */

TEST(vector_constructor)
{
    ASSERT_STR_EQ(eval_print("(vector 1 2 3)"), "#(1 2 3)");
    ASSERT_STR_EQ(eval_print("(vector)"), "#()");
    ASSERT_EQ_INT(eval_int("(length (vector 'a 'b 'c 'd))"), 4);
}

/* ============================================================ */
/* aref / svref                                                 */
/* ============================================================ */

TEST(aref_1d)
{
    ASSERT_EQ_INT(eval_int("(aref (vector 10 20 30) 0)"), 10);
    ASSERT_EQ_INT(eval_int("(aref (vector 10 20 30) 2)"), 30);
}

TEST(aref_2d)
{
    ASSERT_EQ_INT(eval_int(
        "(let ((a (make-array '(2 3) :initial-contents '((1 2 3) (4 5 6)))))"
        "  (aref a 1 2))"), 6);
    ASSERT_EQ_INT(eval_int(
        "(let ((a (make-array '(2 3) :initial-contents '((1 2 3) (4 5 6)))))"
        "  (aref a 0 0))"), 1);
}

TEST(aref_3d)
{
    ASSERT_EQ_INT(eval_int(
        "(let ((a (make-array '(2 2 2) :initial-contents '(((1 2) (3 4)) ((5 6) (7 8))))))"
        "  (aref a 1 1 1))"), 8);
}

TEST(aref_bounds_error)
{
    ASSERT(eval_errors("(aref (vector 1 2 3) 5)"));
    ASSERT(eval_errors("(aref (vector 1 2 3) -1)"));
}

TEST(svref_basic)
{
    ASSERT_EQ_INT(eval_int("(svref (vector 10 20 30) 1)"), 20);
}

TEST(svref_rejects_nonsimple)
{
    ASSERT(eval_errors("(svref (make-array 3 :fill-pointer 0) 0)"));
}

/* ============================================================ */
/* setf aref                                                    */
/* ============================================================ */

TEST(setf_aref_1d)
{
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 3)))"
        "  (setf (aref v 0) 42) (aref v 0))"), 42);
    /* setf returns value */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 1))) (setf (aref v 0) 99))"), 99);
}

TEST(setf_aref_2d)
{
    ASSERT_EQ_INT(eval_int(
        "(let ((a (make-array '(2 3))))"
        "  (setf (aref a 1 2) 77) (aref a 1 2))"), 77);
}

TEST(setf_aref_3d)
{
    ASSERT_EQ_INT(eval_int(
        "(let ((a (make-array '(2 2 2))))"
        "  (setf (aref a 1 0 1) 55) (aref a 1 0 1))"), 55);
}

/* ============================================================ */
/* array-dimension / array-total-size / array-row-major-index   */
/* ============================================================ */

TEST(array_dimension)
{
    ASSERT_EQ_INT(eval_int("(array-dimension (make-array 5) 0)"), 5);
    ASSERT_EQ_INT(eval_int("(array-dimension (make-array '(3 4)) 0)"), 3);
    ASSERT_EQ_INT(eval_int("(array-dimension (make-array '(3 4)) 1)"), 4);
}

TEST(array_total_size)
{
    ASSERT_EQ_INT(eval_int("(array-total-size (make-array 5))"), 5);
    ASSERT_EQ_INT(eval_int("(array-total-size (make-array '(3 4)))"), 12);
    ASSERT_EQ_INT(eval_int("(array-total-size (make-array '(2 3 4)))"), 24);
}

TEST(array_row_major_index)
{
    ASSERT_EQ_INT(eval_int("(array-row-major-index (make-array 5) 3)"), 3);
    ASSERT_EQ_INT(eval_int("(array-row-major-index (make-array '(3 4)) 0 0)"), 0);
    ASSERT_EQ_INT(eval_int("(array-row-major-index (make-array '(3 4)) 1 0)"), 4);
    ASSERT_EQ_INT(eval_int("(array-row-major-index (make-array '(3 4)) 2 3)"), 11);
}

/* ============================================================ */
/* row-major-aref / (setf row-major-aref)                       */
/* ============================================================ */

TEST(row_major_aref)
{
    ASSERT_EQ_INT(eval_int(
        "(let ((a (make-array '(2 3) :initial-contents '((1 2 3) (4 5 6)))))"
        "  (row-major-aref a 5))"), 6);
    ASSERT_EQ_INT(eval_int(
        "(let ((a (make-array '(2 3) :initial-contents '((1 2 3) (4 5 6)))))"
        "  (row-major-aref a 0))"), 1);
}

TEST(setf_row_major_aref)
{
    ASSERT_EQ_INT(eval_int(
        "(let ((a (make-array '(2 3))))"
        "  (setf (row-major-aref a 5) 77)"
        "  (aref a 1 2))"), 77);
}

/* ============================================================ */
/* fill-pointer / (setf fill-pointer) / array-has-fill-pointer-p*/
/* ============================================================ */

TEST(fill_pointer_ops)
{
    ASSERT_EQ_INT(eval_int(
        "(fill-pointer (make-array 10 :fill-pointer 0))"), 0);
    ASSERT_EQ_INT(eval_int(
        "(fill-pointer (make-array 10 :fill-pointer 5))"), 5);
    ASSERT_STR_EQ(eval_print(
        "(array-has-fill-pointer-p (make-array 5 :fill-pointer 0))"), "T");
    ASSERT_STR_EQ(eval_print(
        "(array-has-fill-pointer-p (make-array 5))"), "NIL");
}

TEST(setf_fill_pointer)
{
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 10 :fill-pointer 0)))"
        "  (setf (fill-pointer v) 7)"
        "  (fill-pointer v))"), 7);
    /* returns new value */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 10 :fill-pointer 0)))"
        "  (setf (fill-pointer v) 3))"), 3);
}

TEST(fill_pointer_bounds)
{
    /* Cannot set fill-pointer beyond length */
    ASSERT(eval_errors(
        "(let ((v (make-array 5 :fill-pointer 0)))"
        "  (setf (fill-pointer v) 10))"));
    /* Cannot get fill-pointer on array without one */
    ASSERT(eval_errors("(fill-pointer (make-array 5))"));
}

/* ============================================================ */
/* vector-push / vector-push-extend                             */
/* ============================================================ */

TEST(vector_push)
{
    /* Returns index of pushed element */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 5 :fill-pointer 0)))"
        "  (vector-push 10 v))"), 0);
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 5 :fill-pointer 0)))"
        "  (vector-push 10 v)"
        "  (vector-push 20 v))"), 1);
    /* Fill pointer advances */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 5 :fill-pointer 0)))"
        "  (vector-push 10 v)"
        "  (vector-push 20 v)"
        "  (vector-push 30 v)"
        "  (fill-pointer v))"), 3);
    /* Returns NIL when full */
    ASSERT_STR_EQ(eval_print(
        "(let ((v (make-array 2 :fill-pointer 0)))"
        "  (vector-push 1 v)"
        "  (vector-push 2 v)"
        "  (vector-push 3 v))"), "NIL");
    /* Data is stored correctly */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 3 :fill-pointer 0)))"
        "  (vector-push 10 v)"
        "  (vector-push 20 v)"
        "  (+ (aref v 0) (aref v 1)))"), 30);
}

TEST(vector_push_extend_basic)
{
    /* Extends when full, returns old fill-pointer index */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 2 :fill-pointer 0 :adjustable t)))"
        "  (vector-push-extend 10 v)"
        "  (vector-push-extend 20 v)"
        "  (vector-push-extend 30 v))"), 2);
    /* Fill pointer advances past original capacity */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 2 :fill-pointer 0 :adjustable t)))"
        "  (vector-push-extend 10 v)"
        "  (vector-push-extend 20 v)"
        "  (vector-push-extend 30 v)"
        "  (fill-pointer v))"), 3);
    /* Data is accessible after extension */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 2 :fill-pointer 0 :adjustable t)))"
        "  (vector-push-extend 10 v)"
        "  (vector-push-extend 20 v)"
        "  (vector-push-extend 30 v)"
        "  (+ (aref v 0) (aref v 1) (aref v 2)))"), 60);
}

TEST(vector_push_extend_multiple)
{
    /* Multiple extensions work correctly */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 1 :fill-pointer 0 :adjustable t)))"
        "  (dotimes (i 20) (vector-push-extend i v))"
        "  (fill-pointer v))"), 20);
    /* Verify data integrity after many extensions */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 1 :fill-pointer 0 :adjustable t)))"
        "  (dotimes (i 10) (vector-push-extend (* i 10) v))"
        "  (+ (aref v 0) (aref v 5) (aref v 9)))"), 140);  /* 0 + 50 + 90 */
}

TEST(vector_push_extend_identity)
{
    /* Vector identity preserved (EQ) after extension */
    ASSERT_STR_EQ(eval_print(
        "(let ((v (make-array 1 :fill-pointer 0 :adjustable t)))"
        "  (let ((v2 v))"
        "    (vector-push-extend 42 v)"
        "    (vector-push-extend 99 v)"
        "    (eq v v2)))"), "T");
}

TEST(vector_push_extend_zero_capacity)
{
    /* Extending from zero-length vector */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 0 :fill-pointer 0 :adjustable t)))"
        "  (vector-push-extend 42 v)"
        "  (aref v 0))"), 42);
}

/* ============================================================ */
/* adjust-array                                                 */
/* ============================================================ */

TEST(adjust_array_grow)
{
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 3 :adjustable t :initial-element 1)))"
        "  (let ((v2 (adjust-array v 5 :initial-element 99)))"
        "    (+ (aref v2 0) (aref v2 3))))"), 100);  /* 1 + 99 */
}

TEST(adjust_array_shrink)
{
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 5 :adjustable t :initial-element 42)))"
        "  (array-total-size (adjust-array v 3)))"), 3);
}

TEST(adjust_array_preserves_fp)
{
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 5 :fill-pointer 2 :adjustable t)))"
        "  (fill-pointer (adjust-array v 10)))"), 2);
}

TEST(adjust_array_override_fp)
{
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 5 :fill-pointer 2 :adjustable t)))"
        "  (fill-pointer (adjust-array v 10 :fill-pointer 8)))"), 8);
}

TEST(adjust_array_identity)
{
    /* Adjustable arrays: adjust-array returns same object (EQ) */
    ASSERT_STR_EQ(eval_print(
        "(let ((v (make-array 3 :adjustable t)))"
        "  (eq v (adjust-array v 10)))"), "T");
    /* Data accessible through original reference after adjust */
    ASSERT_EQ_INT(eval_int(
        "(let ((v (make-array 3 :adjustable t :initial-element 5)))"
        "  (adjust-array v 6 :initial-element 99)"
        "  (+ (aref v 0) (aref v 4)))"), 104);  /* 5 + 99 */
}

/* ============================================================ */
/* Displaced arrays                                             */
/* ============================================================ */

TEST(displaced_vector_basic)
{
    /* Displaced vector shares data with target */
    ASSERT_EQ_INT(eval_int(
        "(let* ((a (vector 10 20 30 40 50))"
        "       (d (make-array 3 :displaced-to a :displaced-index-offset 1)))"
        "  (aref d 0))"), 20);
    ASSERT_EQ_INT(eval_int(
        "(let* ((a (vector 10 20 30 40 50))"
        "       (d (make-array 3 :displaced-to a :displaced-index-offset 1)))"
        "  (aref d 2))"), 40);
}

TEST(displaced_vector_offset_zero)
{
    /* Default offset = 0 */
    ASSERT_EQ_INT(eval_int(
        "(let* ((a (vector 10 20 30))"
        "       (d (make-array 2 :displaced-to a)))"
        "  (aref d 0))"), 10);
    ASSERT_EQ_INT(eval_int(
        "(let* ((a (vector 10 20 30))"
        "       (d (make-array 2 :displaced-to a)))"
        "  (aref d 1))"), 20);
}

TEST(displaced_vector_write_through)
{
    /* Writes through displaced array modify the backing array */
    ASSERT_EQ_INT(eval_int(
        "(let* ((a (vector 1 2 3 4 5))"
        "       (d (make-array 3 :displaced-to a :displaced-index-offset 2)))"
        "  (setf (aref d 0) 99)"
        "  (aref a 2))"), 99);
}

TEST(displaced_vector_length)
{
    /* Length of displaced vector is the specified length */
    ASSERT_EQ_INT(eval_int(
        "(let* ((a (vector 1 2 3 4 5))"
        "       (d (make-array 3 :displaced-to a :displaced-index-offset 1)))"
        "  (length d))"), 3);
}

TEST(displaced_string_copy)
{
    /* Displaced to string: creates a copy of the substring */
    ASSERT_STR_EQ(eval_print(
        "(make-array 4 :element-type 'character"
        "  :displaced-to \"hello world\" :displaced-index-offset 6)"),
        "\"worl\"");
}

TEST(displaced_string_equalp)
{
    /* The log4cl kw= pattern: displaced string compared with equalp */
    ASSERT_STR_EQ(eval_print(
        "(let* ((s \":downcase\")"
        "       (sub (make-array (1- (length s))"
        "              :element-type (array-element-type s)"
        "              :displaced-to s :displaced-index-offset 1)))"
        "  (equalp sub \"downcase\"))"), "T");
}

TEST(displaced_char_vector)
{
    /* Displaced to a character vector (fill-pointer string) */
    ASSERT_STR_EQ(eval_print(
        "(let* ((a (make-array 5 :element-type 'character"
        "           :fill-pointer t :initial-element #\\x))"
        "       (d (make-array 3 :displaced-to a :displaced-index-offset 1)))"
        "  d)"), "\"xxx\"");
}

TEST(array_displacement_query)
{
    /* array-displacement returns backing array and offset */
    ASSERT_STR_EQ(eval_print(
        "(let* ((a (vector 1 2 3))"
        "       (d (make-array 2 :displaced-to a :displaced-index-offset 1)))"
        "  (multiple-value-bind (arr off) (array-displacement d)"
        "    (list (eq arr a) off)))"), "(T 1)");
}

TEST(array_displacement_not_displaced)
{
    /* Non-displaced array returns NIL, 0 */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (arr off) (array-displacement (vector 1 2 3))"
        "  (list arr off))"), "(NIL 0)");
}

TEST(displaced_vector_error_bounds)
{
    /* Error when displaced bounds exceed target */
    ASSERT(eval_errors(
        "(make-array 5 :displaced-to (vector 1 2 3) :displaced-index-offset 0)"));
}

TEST(displaced_with_fill_pointer)
{
    /* Displaced vector with fill pointer */
    ASSERT_EQ_INT(eval_int(
        "(let* ((a (vector 10 20 30 40 50))"
        "       (d (make-array 4 :displaced-to a :displaced-index-offset 1"
        "           :fill-pointer 2)))"
        "  (length d))"), 2);
    ASSERT_EQ_INT(eval_int(
        "(let* ((a (vector 10 20 30 40 50))"
        "       (d (make-array 4 :displaced-to a :displaced-index-offset 1"
        "           :fill-pointer 2)))"
        "  (aref d 1))"), 30);
}

/* ============================================================ */
/* Type predicates: arrayp, vectorp, simple-vector-p,           */
/*                  adjustable-array-p                          */
/* ============================================================ */

TEST(arrayp)
{
    ASSERT_STR_EQ(eval_print("(arrayp (vector 1 2 3))"), "T");
    ASSERT_STR_EQ(eval_print("(arrayp (make-array 5))"), "T");
    ASSERT_STR_EQ(eval_print("(arrayp (make-array '(2 3)))"), "T");
    ASSERT_STR_EQ(eval_print("(arrayp \"hello\")"), "T");
    ASSERT_STR_EQ(eval_print("(arrayp 42)"), "NIL");
    ASSERT_STR_EQ(eval_print("(arrayp '(1 2))"), "NIL");
    ASSERT_STR_EQ(eval_print("(arrayp nil)"), "NIL");
}

TEST(vectorp)
{
    ASSERT_STR_EQ(eval_print("(vectorp (vector 1 2))"), "T");
    ASSERT_STR_EQ(eval_print("(vectorp (make-array 3))"), "T");
    ASSERT_STR_EQ(eval_print("(vectorp 42)"), "NIL");
    /* Multi-dim is not a vector */
    ASSERT_STR_EQ(eval_print("(vectorp (make-array '(2 3)))"), "NIL");
}

TEST(simple_vector_p)
{
    ASSERT_STR_EQ(eval_print("(simple-vector-p (vector 1 2 3))"), "T");
    ASSERT_STR_EQ(eval_print("(simple-vector-p (make-array 5))"), "T");
    ASSERT_STR_EQ(eval_print("(simple-vector-p (make-array 5 :fill-pointer 0))"), "NIL");
    ASSERT_STR_EQ(eval_print("(simple-vector-p (make-array 5 :adjustable t))"), "NIL");
    ASSERT_STR_EQ(eval_print("(simple-vector-p (make-array '(2 3)))"), "NIL");
    ASSERT_STR_EQ(eval_print("(simple-vector-p \"hello\")"), "NIL");
    ASSERT_STR_EQ(eval_print("(simple-vector-p 42)"), "NIL");
}

TEST(adjustable_array_p)
{
    ASSERT_STR_EQ(eval_print("(adjustable-array-p (make-array 5 :adjustable t))"), "T");
    ASSERT_STR_EQ(eval_print("(adjustable-array-p (make-array 5))"), "NIL");
    ASSERT_STR_EQ(eval_print("(adjustable-array-p (vector 1 2))"), "NIL");
    ASSERT_STR_EQ(eval_print("(adjustable-array-p \"hello\")"), "NIL");
}

/* ============================================================ */
/* typep with array type specifiers                             */
/* ============================================================ */

TEST(typep_array)
{
    ASSERT_STR_EQ(eval_print("(typep (vector 1 2) 'array)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-array '(2 3)) 'array)"), "T");
    ASSERT_STR_EQ(eval_print("(typep \"hello\" 'array)"), "T");
    ASSERT_STR_EQ(eval_print("(typep 42 'array)"), "NIL");
}

TEST(typep_vector)
{
    ASSERT_STR_EQ(eval_print("(typep (vector 1 2) 'vector)"), "T");
    ASSERT_STR_EQ(eval_print("(typep \"hello\" 'vector)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-array '(2 3)) 'vector)"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep 42 'vector)"), "NIL");
}

TEST(typep_simple_vector)
{
    ASSERT_STR_EQ(eval_print("(typep (vector 1 2) 'simple-vector)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-array 5) 'simple-vector)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-array 5 :fill-pointer 0) 'simple-vector)"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep (make-array 5 :adjustable t) 'simple-vector)"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep \"hello\" 'simple-vector)"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep (make-array '(2 3)) 'simple-vector)"), "NIL");
}

TEST(typep_simple_array)
{
    ASSERT_STR_EQ(eval_print("(typep (vector 1 2) 'simple-array)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-array '(2 3)) 'simple-array)"), "T");
    ASSERT_STR_EQ(eval_print("(typep \"hello\" 'simple-array)"), "T");
    ASSERT_STR_EQ(eval_print("(typep (make-array 5 :fill-pointer 0) 'simple-array)"), "NIL");
    ASSERT_STR_EQ(eval_print("(typep (make-array 5 :adjustable t) 'simple-array)"), "NIL");
}

/* ============================================================ */
/* type-of for arrays                                           */
/* ============================================================ */

TEST(type_of_array)
{
    ASSERT_STR_EQ(eval_print("(type-of (vector 1 2 3))"), "SIMPLE-VECTOR");
    ASSERT_STR_EQ(eval_print("(type-of (make-array 5))"), "SIMPLE-VECTOR");
    ASSERT_STR_EQ(eval_print("(type-of (make-array 5 :fill-pointer 0))"), "VECTOR");
    ASSERT_STR_EQ(eval_print("(type-of (make-array 5 :adjustable t))"), "VECTOR");
    ASSERT_STR_EQ(eval_print("(type-of (make-array '(2 3)))"), "SIMPLE-ARRAY");
    ASSERT_STR_EQ(eval_print("(type-of \"hello\")"), "STRING");
}

/* ============================================================ */
/* Reader #(...) syntax                                         */
/* ============================================================ */

TEST(reader_vector_literal)
{
    ASSERT_STR_EQ(eval_print("#(1 2 3)"), "#(1 2 3)");
    ASSERT_STR_EQ(eval_print("#()"), "#()");
    ASSERT_EQ_INT(eval_int("(aref #(10 20 30) 1)"), 20);
    ASSERT_EQ_INT(eval_int("(length #(a b c d))"), 4);
    ASSERT_STR_EQ(eval_print("(simple-vector-p #(1 2))"), "T");
    ASSERT_STR_EQ(eval_print("(vectorp #(1))"), "T");
}

TEST(reader_vector_nested)
{
    ASSERT_STR_EQ(eval_print("(aref #(#(1 2) #(3 4)) 0)"), "#(1 2)");
    ASSERT_EQ_INT(eval_int("(aref (aref #(#(1 2) #(3 4)) 1) 0)"), 3);
}

TEST(reader_vector_mixed_types)
{
    ASSERT_STR_EQ(eval_print("#(1 \"hello\" a)"), "#(1 \"hello\" A)");
}

/* ============================================================ */
/* Printer: 1D vectors                                          */
/* ============================================================ */

TEST(print_1d_vector)
{
    ASSERT_STR_EQ(eval_print("(vector 1 2 3)"), "#(1 2 3)");
    ASSERT_STR_EQ(eval_print("(vector)"), "#()");
    /* Fill pointer: only active elements printed */
    ASSERT_STR_EQ(eval_print(
        "(let ((v (make-array 5 :fill-pointer 0)))"
        "  (vector-push 10 v)"
        "  (vector-push 20 v)"
        "  v)"), "#(10 20)");
}

TEST(print_1d_with_print_length)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-length* 2)) (write-to-string (vector 1 2 3 4)))"),
        "\"#(1 2...)\"");
}

/* ============================================================ */
/* Printer: multi-dimensional                                   */
/* ============================================================ */

TEST(print_2d)
{
    ASSERT_STR_EQ(eval_print(
        "(make-array '(2 3) :initial-contents '((1 2 3) (4 5 6)))"),
        "#2A((1 2 3) (4 5 6))");
}

TEST(print_3d)
{
    ASSERT_STR_EQ(eval_print(
        "(make-array '(2 2 2) :initial-contents '(((1 2) (3 4)) ((5 6) (7 8))))"),
        "#3A(((1 2) (3 4)) ((5 6) (7 8)))");
}

TEST(print_2d_empty)
{
    ASSERT_STR_EQ(eval_print("(make-array '(2 0))"), "#2A(() ())");
    ASSERT_STR_EQ(eval_print("(make-array '(0 3))"), "#2A()");
}

TEST(print_array_nil)
{
    /* *print-array* nil: unreadable format */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-array* nil)) (write-to-string (vector 1 2)))"),
        "\"#<VECTOR>\"");
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-array* nil)) (write-to-string (make-array '(2 3))))"),
        "\"#<ARRAY>\"");
}

/* ============================================================ */
/* subtypep for array types                                     */
/* ============================================================ */

TEST(subtypep_array)
{
    ASSERT_STR_EQ(eval_print("(subtypep 'simple-vector 'vector)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'simple-vector 'array)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'simple-vector 'sequence)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'vector 'array)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'vector 'sequence)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'string 'vector)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'string 'array)"), "T");
    ASSERT_STR_EQ(eval_print("(subtypep 'simple-array 'array)"), "T");
    /* Not subtypes */
    ASSERT_STR_EQ(eval_print("(subtypep 'array 'vector)"), "NIL");
    ASSERT_STR_EQ(eval_print("(subtypep 'vector 'simple-vector)"), "NIL");
}

/* ============================================================ */
/* main                                                         */
/* ============================================================ */

int main(void)
{
    test_init();
    setup();

    /* Construction */
    RUN(make_array_simple);
    RUN(make_array_initial_element);
    RUN(make_array_initial_contents);
    RUN(make_array_fill_pointer);
    RUN(make_array_fill_pointer_t_string);
    RUN(make_array_initial_contents_string);
    RUN(make_array_adjustable);
    RUN(make_array_2d);
    RUN(make_array_3d);
    RUN(make_array_2d_initial_contents);
    RUN(make_array_3d_initial_contents);
    RUN(make_array_2d_initial_element);
    RUN(make_array_empty_dims);

    /* vector function */
    RUN(vector_constructor);

    /* Access */
    RUN(aref_1d);
    RUN(aref_2d);
    RUN(aref_3d);
    RUN(aref_bounds_error);
    RUN(svref_basic);
    RUN(svref_rejects_nonsimple);
    RUN(setf_aref_1d);
    RUN(setf_aref_2d);
    RUN(setf_aref_3d);

    /* Query */
    RUN(array_dimension);
    RUN(array_total_size);
    RUN(array_row_major_index);
    RUN(row_major_aref);
    RUN(setf_row_major_aref);

    /* Fill pointer */
    RUN(fill_pointer_ops);
    RUN(setf_fill_pointer);
    RUN(fill_pointer_bounds);
    RUN(vector_push);
    RUN(vector_push_extend_basic);
    RUN(vector_push_extend_multiple);
    RUN(vector_push_extend_identity);
    RUN(vector_push_extend_zero_capacity);

    /* Adjust */
    RUN(adjust_array_grow);
    RUN(adjust_array_shrink);
    RUN(adjust_array_preserves_fp);
    RUN(adjust_array_override_fp);
    RUN(adjust_array_identity);

    /* Displaced arrays */
    RUN(displaced_vector_basic);
    RUN(displaced_vector_offset_zero);
    RUN(displaced_vector_write_through);
    RUN(displaced_vector_length);
    RUN(displaced_string_copy);
    RUN(displaced_string_equalp);
    RUN(displaced_char_vector);
    RUN(array_displacement_query);
    RUN(array_displacement_not_displaced);
    RUN(displaced_vector_error_bounds);
    RUN(displaced_with_fill_pointer);

    /* Type predicates */
    RUN(arrayp);
    RUN(vectorp);
    RUN(simple_vector_p);
    RUN(adjustable_array_p);

    /* typep */
    RUN(typep_array);
    RUN(typep_vector);
    RUN(typep_simple_vector);
    RUN(typep_simple_array);

    /* type-of */
    RUN(type_of_array);

    /* Reader */
    RUN(reader_vector_literal);
    RUN(reader_vector_nested);
    RUN(reader_vector_mixed_types);

    /* Printer */
    RUN(print_1d_vector);
    RUN(print_1d_with_print_length);
    RUN(print_2d);
    RUN(print_3d);
    RUN(print_2d_empty);
    RUN(print_array_nil);

    /* subtypep */
    RUN(subtypep_array);

    teardown();
    REPORT();
}
