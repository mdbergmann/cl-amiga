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

/* === Phase 1: Slot Access Infrastructure === */

/* Helper: create a simple CLOS class with slot-index-table for testing */
#define PHASE1_SETUP \
    "(progn " \
    "  (%register-struct-type 'test-point 2 nil '((x nil) (y nil))) " \
    "  (let ((cls (%make-struct 'standard-class " \
    "               'test-point nil nil nil nil nil nil nil nil t))) " \
    "    (let ((idx-table (make-hash-table :test 'eq))) " \
    "      (setf (gethash 'x idx-table) 0) " \
    "      (setf (gethash 'y idx-table) 1) " \
    "      (%struct-set cls 5 idx-table) " \
    "      (setf (find-class 'test-point) cls))) " \
    "  t)"

TEST(phase1_setup)
{
    ASSERT_STR_EQ(eval_print(PHASE1_SETUP), "T");
}

TEST(slot_value_read)
{
    eval_print("(defvar *test-pt* (%make-struct 'test-point 10 20))");
    ASSERT_STR_EQ(eval_print("(slot-value *test-pt* 'x)"), "10");
    ASSERT_STR_EQ(eval_print("(slot-value *test-pt* 'y)"), "20");
}

TEST(slot_value_write)
{
    ASSERT_STR_EQ(eval_print("(setf (slot-value *test-pt* 'x) 42)"), "42");
    ASSERT_STR_EQ(eval_print("(slot-value *test-pt* 'x)"), "42");
}

TEST(slot_boundp_true)
{
    ASSERT_STR_EQ(eval_print("(slot-boundp *test-pt* 'x)"), "T");
}

TEST(slot_makunbound_and_boundp)
{
    eval_print("(slot-makunbound *test-pt* 'x)");
    ASSERT_STR_EQ(eval_print("(slot-boundp *test-pt* 'x)"), "NIL");
}

TEST(slot_value_unbound_error)
{
    /* After makunbound, reading should error */
    ASSERT(strncmp(eval_print("(slot-value *test-pt* 'x)"), "ERROR:", 6) == 0);
}

TEST(slot_exists_p_true)
{
    ASSERT_STR_EQ(eval_print("(slot-exists-p *test-pt* 'x)"), "T");
}

TEST(slot_exists_p_false)
{
    ASSERT_STR_EQ(eval_print("(slot-exists-p *test-pt* 'z)"), "NIL");
}

TEST(slot_value_no_such_slot)
{
    ASSERT(strncmp(eval_print("(slot-value *test-pt* 'z)"), "ERROR:", 6) == 0);
}

/* === Phase 2: C3 Linearization === */

/* Test C3 with single inheritance (should match simple CPL) */
TEST(c3_single_inheritance)
{
    /* fixnum already has a CPL from bootstrap; verify C3 gives the same */
    ASSERT_STR_EQ(eval_print(
        "(mapcar #'class-name "
        "  (%compute-class-precedence-list (find-class 'fixnum)))"),
        "(FIXNUM INTEGER RATIONAL REAL NUMBER T)");
}

/* Test C3 with multiple inheritance — the classic diamond */
TEST(c3_diamond)
{
    /* Create: A, B(A), C(A), D(B C) */
    eval_print(
        "(progn "
        "  (%make-bootstrap-class 'c3-a (list (find-class 't))) "
        "  (%make-bootstrap-class 'c3-b (list (find-class 'c3-a))) "
        "  (%make-bootstrap-class 'c3-c (list (find-class 'c3-a))) "
        "  t)");
    ASSERT_STR_EQ(eval_print(
        "(let ((d (%make-struct 'standard-class "
        "           'c3-d "
        "           (list (find-class 'c3-b) (find-class 'c3-c)) "
        "           nil nil nil nil nil nil nil t))) "
        "  (setf (find-class 'c3-d) d) "
        "  (%set-class-cpl d (%compute-class-precedence-list d)) "
        "  (mapcar #'class-name (class-precedence-list d)))"),
        "(C3-D C3-B C3-C C3-A T)");
}

/* C3 with T root — basic case */
TEST(c3_root)
{
    ASSERT_STR_EQ(eval_print(
        "(mapcar #'class-name "
        "  (%compute-class-precedence-list (find-class 't)))"),
        "(T)");
}

/* === Phase 3: defclass === */

TEST(defclass_basic)
{
    /* defclass returns class metaobject — hook should print it concisely */
    ASSERT_STR_EQ(eval_print(
        "(defclass point () "
        "  ((x :initarg :x :accessor point-x) "
        "   (y :initarg :y :accessor point-y)))"),
        "#<STANDARD-CLASS POINT>");
}

TEST(defclass_find_class)
{
    ASSERT_STR_EQ(eval_print("(class-name (find-class 'point))"), "POINT");
}

TEST(defclass_cpl)
{
    ASSERT_STR_EQ(eval_print(
        "(mapcar #'class-name (class-precedence-list (find-class 'point)))"),
        "(POINT STANDARD-OBJECT T)");
}

TEST(defclass_effective_slots)
{
    /* Should have 2 effective slots */
    ASSERT_STR_EQ(eval_print(
        "(length (class-effective-slots (find-class 'point)))"),
        "2");
}

TEST(defclass_slot_index_table)
{
    ASSERT_STR_EQ(eval_print(
        "(gethash 'x (class-slot-index-table (find-class 'point)))"),
        "0");
    ASSERT_STR_EQ(eval_print(
        "(gethash 'y (class-slot-index-table (find-class 'point)))"),
        "1");
}

TEST(defclass_accessor_defined)
{
    /* point-x and point-y should be functions */
    ASSERT_STR_EQ(eval_print("(functionp #'point-x)"), "T");
    ASSERT_STR_EQ(eval_print("(functionp #'point-y)"), "T");
}

TEST(defclass_inheritance)
{
    eval_print(
        "(defclass point3d (point) "
        "  ((z :initarg :z :accessor point-z)))");
    ASSERT_STR_EQ(eval_print(
        "(length (class-effective-slots (find-class 'point3d)))"),
        "3");
    ASSERT_STR_EQ(eval_print(
        "(mapcar #'class-name (class-precedence-list (find-class 'point3d)))"),
        "(POINT3D POINT STANDARD-OBJECT T)");
}

TEST(defclass_reader_writer)
{
    eval_print(
        "(defclass rw-test () "
        "  ((val :reader get-val :writer set-val)))");
    ASSERT_STR_EQ(eval_print("(functionp #'get-val)"), "T");
    ASSERT_STR_EQ(eval_print("(functionp #'set-val)"), "T");
}

TEST(defclass_no_supers)
{
    /* No explicit supers → defaults to standard-object */
    eval_print("(defclass empty-class () ())");
    ASSERT_STR_EQ(eval_print(
        "(mapcar #'class-name "
        "  (class-direct-superclasses (find-class 'empty-class)))"),
        "(STANDARD-OBJECT)");
}

/* === Phase 4: make-instance === */

TEST(make_instance_basic)
{
    /* point class already defined from Phase 3 tests */
    ASSERT_STR_EQ(eval_print(
        "(let ((p (make-instance 'point :x 10 :y 20))) "
        "  (list (point-x p) (point-y p)))"),
        "(10 20)");
}

TEST(make_instance_initform)
{
    eval_print(
        "(defclass counted () "
        "  ((count :initform 0 :accessor obj-count) "
        "   (name :initarg :name :accessor obj-name)))");
    ASSERT_STR_EQ(eval_print(
        "(let ((c (make-instance 'counted :name \"test\"))) "
        "  (list (obj-count c) (obj-name c)))"),
        "(0 \"test\")");
}

TEST(make_instance_initarg_overrides_initform)
{
    /* Define class with initarg AND initform on same slot */
    eval_print(
        "(defclass counter () "
        "  ((val :initarg :val :initform 0 :accessor counter-val)))");
    ASSERT_STR_EQ(eval_print(
        "(counter-val (make-instance 'counter :val 42))"),
        "42");
    /* Without initarg, should get initform default */
    ASSERT_STR_EQ(eval_print(
        "(counter-val (make-instance 'counter))"),
        "0");
}

TEST(make_instance_unbound_slot)
{
    /* Slot without initarg or initform stays unbound */
    ASSERT_STR_EQ(eval_print(
        "(let ((c (make-instance 'counted))) "
        "  (slot-boundp c 'name))"),
        "NIL");
}

TEST(make_instance_by_class_object)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (make-instance (find-class 'point) :x 1 :y 2))) "
        "  (point-x p))"),
        "1");
}

TEST(make_instance_inherited_slots)
{
    /* point3d inherits x, y from point, adds z */
    ASSERT_STR_EQ(eval_print(
        "(let ((p (make-instance 'point3d :x 1 :y 2 :z 3))) "
        "  (list (point-x p) (point-y p) (point-z p)))"),
        "(1 2 3)");
}

/* === Phase 5: defgeneric + defmethod + dispatch === */

TEST(defgeneric_basic)
{
    eval_print("(defgeneric greet (obj))");
    ASSERT_STR_EQ(eval_print(
        "(gethash 'greet *generic-function-table*)"),
        "#<STANDARD-GENERIC-FUNCTION GREET>");
}

TEST(defmethod_basic)
{
    eval_print(
        "(defmethod greet ((p point)) "
        "  (list 'point (point-x p) (point-y p)))");
    ASSERT_STR_EQ(eval_print(
        "(greet (make-instance 'point :x 10 :y 20))"),
        "(POINT 10 20)");
}

TEST(defmethod_dispatch_by_class)
{
    eval_print(
        "(defmethod greet ((p point3d)) "
        "  (list 'point3d (point-x p) (point-y p) (point-z p)))");
    /* point3d gets its own method */
    ASSERT_STR_EQ(eval_print(
        "(greet (make-instance 'point3d :x 1 :y 2 :z 3))"),
        "(POINT3D 1 2 3)");
    /* point still gets the point method */
    ASSERT_STR_EQ(eval_print(
        "(greet (make-instance 'point :x 5 :y 6))"),
        "(POINT 5 6)");
}

TEST(defmethod_call_next_method)
{
    eval_print("(defgeneric describe-obj (obj))");
    eval_print(
        "(defmethod describe-obj ((p point)) "
        "  (list 'base-point (point-x p)))");
    eval_print(
        "(defmethod describe-obj ((p point3d)) "
        "  (cons 'extended (call-next-method)))");
    ASSERT_STR_EQ(eval_print(
        "(describe-obj (make-instance 'point3d :x 7 :y 8 :z 9))"),
        "(EXTENDED BASE-POINT 7)");
}

TEST(defmethod_before_after)
{
    eval_print("(defvar *trace* nil)");
    eval_print("(defgeneric traced-op (obj))");
    eval_print(
        "(defmethod traced-op :before ((p point)) "
        "  (push 'before *trace*))");
    eval_print(
        "(defmethod traced-op ((p point)) "
        "  (push 'primary *trace*) 'done)");
    eval_print(
        "(defmethod traced-op :after ((p point)) "
        "  (push 'after *trace*))");
    eval_print("(setq *trace* nil)");
    ASSERT_STR_EQ(eval_print(
        "(traced-op (make-instance 'point :x 0 :y 0))"),
        "DONE");
    ASSERT_STR_EQ(eval_print(
        "(nreverse *trace*)"),
        "(BEFORE PRIMARY AFTER)");
}

TEST(defmethod_around)
{
    eval_print("(defgeneric wrapped-op (obj))");
    eval_print(
        "(defmethod wrapped-op ((p point)) "
        "  (point-x p))");
    eval_print(
        "(defmethod wrapped-op :around ((p point)) "
        "  (+ 100 (call-next-method)))");
    ASSERT_STR_EQ(eval_print(
        "(wrapped-op (make-instance 'point :x 5 :y 0))"),
        "105");
}

TEST(defmethod_unspecialized)
{
    eval_print("(defgeneric identity-gf (x))");
    eval_print("(defmethod identity-gf (x) x)");
    ASSERT_STR_EQ(eval_print("(identity-gf 42)"), "42");
    ASSERT_STR_EQ(eval_print("(identity-gf \"hello\")"), "\"hello\"");
}

TEST(defmethod_no_applicable)
{
    eval_print("(defgeneric specific-only (x))");
    eval_print(
        "(defmethod specific-only ((x point)) 'ok)");
    ASSERT(strncmp(eval_print("(specific-only 42)"), "ERROR:", 6) == 0);
}

TEST(next_method_p_test)
{
    eval_print("(defgeneric nmp-test (x))");
    eval_print(
        "(defmethod nmp-test ((x point)) "
        "  (if (next-method-p) 'has-next 'no-next))");
    ASSERT_STR_EQ(eval_print(
        "(nmp-test (make-instance 'point :x 0 :y 0))"),
        "NO-NEXT");
}

/* === Phase 6: with-slots === */

TEST(with_slots_read)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (make-instance 'point :x 10 :y 20))) "
        "  (with-slots (x y) p (list x y)))"),
        "(10 20)");
}

TEST(with_slots_write)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (make-instance 'point :x 1 :y 2))) "
        "  (with-slots (x) p (setf x 99)) "
        "  (point-x p))"),
        "99");
}

TEST(with_slots_renamed)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (make-instance 'point :x 5 :y 6))) "
        "  (with-slots ((px x) (py y)) p (+ px py)))"),
        "11");
}

/* === Phase 7: Generic function accessors + init protocol === */

TEST(gf_accessor)
{
    /* Accessors defined after Phase 7 should be generic functions */
    eval_print(
        "(defclass gf-point () "
        "  ((x :initarg :x :accessor gf-point-x) "
        "   (y :initarg :y :accessor gf-point-y)))");
    ASSERT_STR_EQ(eval_print(
        "(gethash 'gf-point-x *generic-function-table*)"),
        "#<STANDARD-GENERIC-FUNCTION GF-POINT-X>");
}

TEST(gf_accessor_works)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (make-instance 'gf-point :x 10 :y 20))) "
        "  (list (gf-point-x p) (gf-point-y p)))"),
        "(10 20)");
}

TEST(gf_accessor_setf)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (make-instance 'gf-point :x 1 :y 2))) "
        "  (setf (gf-point-x p) 99) "
        "  (gf-point-x p))"),
        "99");
}

TEST(gf_initialize_instance)
{
    /* initialize-instance should be a GF now — add a custom :after method */
    eval_print("(defvar *init-log* nil)");
    eval_print(
        "(defmethod initialize-instance :after ((p gf-point) &rest initargs) "
        "  (push 'gf-point-initialized *init-log*))");
    eval_print("(setq *init-log* nil)");
    eval_print("(make-instance 'gf-point :x 1 :y 2)");
    ASSERT_STR_EQ(eval_print("*init-log*"), "(GF-POINT-INITIALIZED)");
}

/* === Phase 8: change-class + reinitialize-instance === */

TEST(reinitialize_instance_basic)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (make-instance 'gf-point :x 1 :y 2))) "
        "  (reinitialize-instance p :x 99) "
        "  (gf-point-x p))"),
        "99");
}

TEST(change_class_basic)
{
    /* Define a color-point with x, y, color */
    eval_print(
        "(defclass color-point (gf-point) "
        "  ((color :initarg :color :initform :red :accessor point-color)))");
    ASSERT_STR_EQ(eval_print(
        "(let* ((p (make-instance 'gf-point :x 10 :y 20)) "
        "       (cp (change-class p 'color-point))) "
        "  (list (gf-point-x cp) (gf-point-y cp) (point-color cp)))"),
        "(10 20 :RED)");
}

TEST(change_class_with_initargs)
{
    ASSERT_STR_EQ(eval_print(
        "(let* ((p (make-instance 'gf-point :x 1 :y 2)) "
        "       (cp (change-class p 'color-point :color :blue))) "
        "  (point-color cp))"),
        ":BLUE");
}

/* === Phase 9: print-object === */

TEST(print_object_class)
{
    ASSERT_STR_EQ(eval_print("(find-class 'integer)"),
        "#<STANDARD-CLASS INTEGER>");
}

TEST(print_object_gf)
{
    ASSERT_STR_EQ(eval_print("(gethash 'greet *generic-function-table*)"),
        "#<STANDARD-GENERIC-FUNCTION GREET>");
}

TEST(print_object_custom)
{
    eval_print(
        "(defclass printable () "
        "  ((tag :initarg :tag :accessor printable-tag)))");
    eval_print(
        "(defmethod print-object ((obj printable) stream) "
        "  (concatenate 'string \"#<PRINTABLE \" "
        "    (printable-tag obj) \">\"))");
    ASSERT_STR_EQ(eval_print(
        "(make-instance 'printable :tag \"hello\")"),
        "#<PRINTABLE hello>");
}

TEST(print_object_default_struct)
{
    /* Structs without a print-object method use default #S(...) */
    eval_print("(defstruct plain-st (a 1))");
    ASSERT_STR_EQ(eval_print("(make-plain-st)"),
        "#S(PLAIN-ST :A 1)");
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

    /* Phase 1: Slot access */
    RUN(phase1_setup);
    RUN(slot_value_read);
    RUN(slot_value_write);
    RUN(slot_boundp_true);
    RUN(slot_makunbound_and_boundp);
    RUN(slot_value_unbound_error);
    RUN(slot_exists_p_true);
    RUN(slot_exists_p_false);
    RUN(slot_value_no_such_slot);

    /* Phase 2: C3 linearization */
    RUN(c3_single_inheritance);
    RUN(c3_diamond);
    RUN(c3_root);

    /* Phase 3: defclass */
    RUN(defclass_basic);
    RUN(defclass_find_class);
    RUN(defclass_cpl);
    RUN(defclass_effective_slots);
    RUN(defclass_slot_index_table);
    RUN(defclass_accessor_defined);
    RUN(defclass_inheritance);
    RUN(defclass_reader_writer);
    RUN(defclass_no_supers);

    /* Phase 4: make-instance */
    RUN(make_instance_basic);
    RUN(make_instance_initform);
    RUN(make_instance_initarg_overrides_initform);
    RUN(make_instance_unbound_slot);
    RUN(make_instance_by_class_object);
    RUN(make_instance_inherited_slots);

    /* Phase 5: defgeneric + defmethod */
    RUN(defgeneric_basic);
    RUN(defmethod_basic);
    RUN(defmethod_dispatch_by_class);
    RUN(defmethod_call_next_method);
    RUN(defmethod_before_after);
    RUN(defmethod_around);
    RUN(defmethod_unspecialized);
    RUN(defmethod_no_applicable);
    RUN(next_method_p_test);

    /* Phase 6: with-slots */
    RUN(with_slots_read);
    RUN(with_slots_write);
    RUN(with_slots_renamed);

    /* Phase 7: GF accessors + init protocol */
    RUN(gf_accessor);
    RUN(gf_accessor_works);
    RUN(gf_accessor_setf);
    RUN(gf_initialize_instance);

    /* Phase 8: change-class + reinitialize-instance */
    RUN(reinitialize_instance_basic);
    RUN(change_class_basic);
    RUN(change_class_with_initargs);

    /* Phase 9: print-object */
    RUN(print_object_class);
    RUN(print_object_gf);
    RUN(print_object_custom);
    RUN(print_object_default_struct);

    teardown();
    REPORT();
}
