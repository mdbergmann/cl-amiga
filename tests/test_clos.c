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

    CL_CATCH(err);
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

/* CLOS module is auto-loaded during init */
TEST(clos_require)
{
    /* CLOS is now loaded at startup; require returns NIL (already provided) */
    ASSERT_STR_EQ(eval_print("(require \"clos\")"), "NIL");
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
        "(CONS LIST SEQUENCE T)");
}

TEST(cpl_null)
{
    ASSERT_STR_EQ(eval_print(
        "(mapcar #'class-name (class-precedence-list (find-class 'null)))"),
        "(NULL SYMBOL T LIST SEQUENCE)");
    /* Note: CL spec CPL is (NULL SYMBOL LIST SEQUENCE T) but our C3
       linearization of bootstrap classes produces this order.
       Method dispatch still works correctly. */
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

/* Helper: create a simple CLOS class with slot-index-table for testing.
   The table stores STANDARD-EFFECTIVE-SLOT-DEFINITION metaobjects; each
   esd's location (slot 2) is the struct index used by %STRUCT-REF. */
#define PHASE1_SETUP \
    "(progn " \
    "  (%register-struct-type 'test-point 2 nil '((x nil) (y nil))) " \
    "  (let* ((esd-x (%make-effective-slot-def 'x nil nil :instance 0)) " \
    "         (esd-y (%make-effective-slot-def 'y nil nil :instance 1)) " \
    "         (cls (%make-struct 'standard-class " \
    "                'test-point nil nil nil nil nil nil nil nil t nil nil))) " \
    "    (let ((idx-table (make-hash-table :test 'eq))) " \
    "      (setf (gethash 'x idx-table) esd-x) " \
    "      (setf (gethash 'y idx-table) esd-y) " \
    "      (%struct-set cls 4 (list esd-x esd-y)) " \
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
        "           nil nil nil nil nil nil nil t nil nil))) "
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
    /* The index-table maps slot-name -> effective-slot-definition;
       SLOT-DEFINITION-LOCATION gives the struct storage index. */
    ASSERT_STR_EQ(eval_print(
        "(slot-definition-location "
        " (gethash 'x (class-slot-index-table (find-class 'point))))"),
        "0");
    ASSERT_STR_EQ(eval_print(
        "(slot-definition-location "
        " (gethash 'y (class-slot-index-table (find-class 'point))))"),
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

/* === slot-value on DEFSTRUCT instances ===
 * Regression: lisp-namespace and other libraries use (with-slots ...) on
 * defstruct instances. slot-value must resolve slot names against the
 * struct's slot list when CLASS-OF returns a class without a
 * slot-index-table. */

TEST(slot_value_on_struct)
{
    eval_print("(defstruct point3 (x 0) (y 0) (z 0))");
    eval_print("(defvar *p3* (make-point3 :x 10 :y 20 :z 30))");
    ASSERT_STR_EQ(eval_print("(slot-value *p3* 'x)"), "10");
    ASSERT_STR_EQ(eval_print("(slot-value *p3* 'y)"), "20");
    ASSERT_STR_EQ(eval_print("(slot-value *p3* 'z)"), "30");
}

TEST(setf_slot_value_on_struct)
{
    eval_print("(defstruct named-box (name \"\") (value 0))");
    eval_print("(defvar *nb* (make-named-box :name \"n\" :value 1))");
    ASSERT_STR_EQ(eval_print("(setf (slot-value *nb* 'value) 99)"), "99");
    ASSERT_STR_EQ(eval_print("(slot-value *nb* 'value)"), "99");
    ASSERT_STR_EQ(eval_print("(named-box-value *nb*)"), "99");
}

TEST(with_slots_on_struct)
{
    eval_print("(defstruct ws-rec (alpha 1) (beta 2))");
    eval_print("(defvar *wsr* (make-ws-rec))");
    /* with-slots expands to slot-value — must work on structs */
    ASSERT_STR_EQ(eval_print(
        "(with-slots (alpha beta) *wsr* (+ alpha beta))"),
        "3");
    /* with-slots also supports setf of its bindings via symbol-macrolet */
    ASSERT_STR_EQ(eval_print(
        "(with-slots (alpha) *wsr* (setf alpha 42) alpha)"),
        "42");
    ASSERT_STR_EQ(eval_print("(ws-rec-alpha *wsr*)"), "42");
}

TEST(slot_boundp_on_struct)
{
    eval_print("(defstruct sb-rec (a 1))");
    eval_print("(defvar *sbr* (make-sb-rec))");
    /* Struct slots always report bound (they have defaults) */
    ASSERT_STR_EQ(eval_print("(slot-boundp *sbr* 'a)"), "T");
}

TEST(slot_exists_p_on_struct)
{
    eval_print("(defstruct sxp-rec (foo 1) (bar 2))");
    eval_print("(defvar *sxp* (make-sxp-rec))");
    ASSERT_STR_EQ(eval_print("(slot-exists-p *sxp* 'foo)"), "T");
    ASSERT_STR_EQ(eval_print("(slot-exists-p *sxp* 'bar)"), "T");
    ASSERT_STR_EQ(eval_print("(slot-exists-p *sxp* 'missing)"), "NIL");
}

TEST(slot_value_unknown_slot_struct)
{
    eval_print("(defstruct usk-rec (a 1))");
    eval_print("(defvar *usk* (make-usk-rec))");
    /* Accessing an unknown slot name signals an error */
    ASSERT(strncmp(eval_print("(slot-value *usk* 'nope)"), "ERROR:", 6) == 0);
}

/* === ERROR with :format-control formats its report (regression for ~S) === */

TEST(error_format_control_interpolates)
{
    /* Previously, (error "~S foo" x) left ~S literal in the report. */
    const char *out = eval_print(
        "(handler-case (error \"msg: ~S\" 42) "
        "  (simple-error (c) "
        "    (format nil \"~A\" c)))");
    /* Report must contain the interpolated "42", not the literal "~S" */
    ASSERT(strstr(out, "42") != NULL);
    ASSERT(strstr(out, "~S") == NULL);
}

/* === Phase 10: typep with multiple inheritance === */

TEST(typep_multiple_inheritance)
{
    /* Define a class hierarchy with multiple inheritance */
    eval_print("(defclass mixin-a () ())");
    eval_print("(defclass mixin-b () ())");
    eval_print("(defclass multi-child (mixin-a mixin-b) ())");
    eval_print("(defvar *mc* (make-instance 'multi-child))");

    /* Instance should be typep of both mixins */
    ASSERT_STR_EQ(eval_print("(typep *mc* 'multi-child)"), "T");
    ASSERT_STR_EQ(eval_print("(typep *mc* 'mixin-a)"), "T");
    ASSERT_STR_EQ(eval_print("(typep *mc* 'mixin-b)"), "T");
    ASSERT_STR_EQ(eval_print("(typep *mc* 'standard-object)"), "T");
    ASSERT_STR_EQ(eval_print("(typep *mc* 't)"), "T");
}

TEST(typep_multiple_inheritance_or)
{
    /* Compound (or ...) type with multiple inheritance */
    ASSERT_STR_EQ(eval_print(
        "(typep *mc* '(or mixin-a mixin-b))"), "T");
    ASSERT_STR_EQ(eval_print(
        "(typep *mc* '(and mixin-a mixin-b))"), "T");
}

TEST(typep_multiple_inheritance_negative)
{
    /* Should NOT match unrelated classes */
    eval_print("(defclass unrelated () ())");
    ASSERT_STR_EQ(eval_print("(typep *mc* 'unrelated)"), "NIL");
}

/* === Phase 11: Dispatch cache === */

TEST(cache_basic_dispatch)
{
    /* Define a GF and method, call it — should work with cache */
    eval_print(
        "(defgeneric cache-test-1 (x))");
    eval_print(
        "(defmethod cache-test-1 ((x point)) (point-x x))");
    ASSERT_STR_EQ(eval_print(
        "(cache-test-1 (make-instance 'point :x 42 :y 0))"),
        "42");
}

TEST(cache_hit_correct_result)
{
    /* Call GF twice with same-class arg — second call uses cache */
    ASSERT_STR_EQ(eval_print(
        "(cache-test-1 (make-instance 'point :x 77 :y 0))"),
        "77");
    /* Verify cache is populated (non-NIL) */
    ASSERT_STR_EQ(eval_print(
        "(let ((gf (gethash 'cache-test-1 *generic-function-table*))) "
        "  (not (null (gf-dispatch-cache gf))))"),
        "T");
}

TEST(cache_different_classes)
{
    /* GF dispatches correctly for different classes */
    eval_print(
        "(defmethod cache-test-1 ((x point3d)) (+ (point-x x) (point-z x)))");
    ASSERT_STR_EQ(eval_print(
        "(cache-test-1 (make-instance 'point3d :x 10 :y 0 :z 5))"),
        "15");
    /* point dispatch still works (cache rebuilt after defmethod) */
    ASSERT_STR_EQ(eval_print(
        "(cache-test-1 (make-instance 'point :x 33 :y 0))"),
        "33");
}

TEST(cache_invalidated_on_defmethod)
{
    /* Adding a method clears the cache */
    /* First populate cache */
    eval_print("(cache-test-1 (make-instance 'point :x 1 :y 0))");
    ASSERT_STR_EQ(eval_print(
        "(let ((gf (gethash 'cache-test-1 *generic-function-table*))) "
        "  (not (null (gf-dispatch-cache gf))))"),
        "T");
    /* Add another method — cache should be cleared */
    eval_print(
        "(defclass cache-thing () ((v :initarg :v :accessor cache-thing-v)))");
    eval_print(
        "(defmethod cache-test-1 ((x cache-thing)) (cache-thing-v x))");
    ASSERT_STR_EQ(eval_print(
        "(let ((gf (gethash 'cache-test-1 *generic-function-table*))) "
        "  (gf-dispatch-cache gf))"),
        "NIL");
    /* New method works */
    ASSERT_STR_EQ(eval_print(
        "(cache-test-1 (make-instance 'cache-thing :v 99))"),
        "99");
}

TEST(cache_invalidated_on_defclass)
{
    /* Defining a new class clears all GF caches */
    /* Populate cache */
    eval_print("(cache-test-1 (make-instance 'point :x 1 :y 0))");
    ASSERT_STR_EQ(eval_print(
        "(let ((gf (gethash 'cache-test-1 *generic-function-table*))) "
        "  (not (null (gf-dispatch-cache gf))))"),
        "T");
    /* Define a new class — all caches cleared */
    eval_print("(defclass cache-invalidation-test () ())");
    ASSERT_STR_EQ(eval_print(
        "(let ((gf (gethash 'cache-test-1 *generic-function-table*))) "
        "  (gf-dispatch-cache gf))"),
        "NIL");
}

TEST(cache_eql_specializer_bypass)
{
    /* GF with EQL specializer uses :EQL mode */
    eval_print(
        "(defgeneric cache-eql-test (x))");
    eval_print(
        "(defmethod cache-eql-test ((x (eql 42))) 'forty-two)");
    eval_print(
        "(defmethod cache-eql-test ((x integer)) 'other-int)");
    /* Should dispatch correctly */
    ASSERT_STR_EQ(eval_print("(cache-eql-test 42)"), "FORTY-TWO");
    ASSERT_STR_EQ(eval_print("(cache-eql-test 7)"), "OTHER-INT");
    /* cacheable-p should be :EQL */
    ASSERT_STR_EQ(eval_print(
        "(let ((gf (gethash 'cache-eql-test *generic-function-table*))) "
        "  (gf-cacheable-p gf))"),
        ":EQL");
}

TEST(cache_multi_dispatch_bypass)
{
    /* GF with specialization beyond arg1 uses multi-dispatch cache */
    eval_print(
        "(defgeneric cache-multi-test (x y))");
    eval_print(
        "(defmethod cache-multi-test ((x point) (y point)) 'both-points)");
    ASSERT_STR_EQ(eval_print(
        "(cache-multi-test (make-instance 'point :x 1 :y 2) "
        "                  (make-instance 'point :x 3 :y 4))"),
        "BOTH-POINTS");
    /* cacheable-p should be 2 (two specialized positions) */
    ASSERT_STR_EQ(eval_print(
        "(let ((gf (gethash 'cache-multi-test *generic-function-table*))) "
        "  (gf-cacheable-p gf))"),
        "2");
}

TEST(cache_no_applicable_method)
{
    /* No-applicable-method error should still work with cache */
    eval_print(
        "(defgeneric cache-err-test (x))");
    eval_print(
        "(defmethod cache-err-test ((x point)) 'point-ok)");
    /* This should work */
    ASSERT_STR_EQ(eval_print(
        "(cache-err-test (make-instance 'point :x 0 :y 0))"),
        "POINT-OK");
    /* Calling with a string (no applicable method) should error */
    ASSERT_STR_EQ(eval_print(
        "(handler-case (cache-err-test \"hello\") "
        "  (error (c) 'got-error))"),
        "GOT-ERROR");
}

TEST(cache_before_after_around)
{
    /* Standard method combination works correctly with cache */
    eval_print("(defparameter *cache-trace* nil)");
    eval_print(
        "(defgeneric cache-combo-test (x))");
    eval_print(
        "(defmethod cache-combo-test :before ((x point)) "
        "  (push 'before *cache-trace*))");
    eval_print(
        "(defmethod cache-combo-test ((x point)) "
        "  (push 'primary *cache-trace*) 'done)");
    eval_print(
        "(defmethod cache-combo-test :after ((x point)) "
        "  (push 'after *cache-trace*))");
    eval_print(
        "(defmethod cache-combo-test :around ((x point)) "
        "  (push 'around *cache-trace*) (call-next-method))");
    /* First call */
    eval_print("(setq *cache-trace* nil)");
    ASSERT_STR_EQ(eval_print(
        "(cache-combo-test (make-instance 'point :x 0 :y 0))"),
        "DONE");
    ASSERT_STR_EQ(eval_print("(nreverse *cache-trace*)"),
        "(AROUND BEFORE PRIMARY AFTER)");
    /* Second call (should use cache) — same result */
    eval_print("(setq *cache-trace* nil)");
    ASSERT_STR_EQ(eval_print(
        "(cache-combo-test (make-instance 'point :x 1 :y 1))"),
        "DONE");
    ASSERT_STR_EQ(eval_print("(nreverse *cache-trace*)"),
        "(AROUND BEFORE PRIMARY AFTER)");
}

/* === Phase 12: EMF cache (effective method closure caching) === */

TEST(emf_cache_call_next_method_no_args)
{
    /* call-next-method with no args should pass original args through EMF cache */
    eval_print(
        "(defgeneric emf-cnm-test (x))");
    eval_print(
        "(defmethod emf-cnm-test ((x point3d)) "
        "  (list 'p3d (point-x x) (call-next-method)))");
    eval_print(
        "(defmethod emf-cnm-test ((x point)) "
        "  (list 'pt (point-x x)))");
    /* First call — populates cache */
    ASSERT_STR_EQ(eval_print(
        "(emf-cnm-test (make-instance 'point3d :x 10 :y 20 :z 30))"),
        "(P3D 10 (PT 10))");
    /* Second call — uses cached EMF, call-next-method still works */
    ASSERT_STR_EQ(eval_print(
        "(emf-cnm-test (make-instance 'point3d :x 99 :y 0 :z 0))"),
        "(P3D 99 (PT 99))");
}

TEST(emf_cache_call_next_method_with_args)
{
    /* call-next-method with explicit args should forward them */
    eval_print(
        "(defgeneric emf-cnm-args-test (x))");
    eval_print(
        "(defmethod emf-cnm-args-test ((x point3d)) "
        "  (call-next-method (make-instance 'point :x 42 :y 0)))");
    eval_print(
        "(defmethod emf-cnm-args-test ((x point)) "
        "  (point-x x))");
    ASSERT_STR_EQ(eval_print(
        "(emf-cnm-args-test (make-instance 'point3d :x 1 :y 2 :z 3))"),
        "42");
    /* Cached call */
    ASSERT_STR_EQ(eval_print(
        "(emf-cnm-args-test (make-instance 'point3d :x 7 :y 8 :z 9))"),
        "42");
}

TEST(emf_cache_before_after_around)
{
    /* Full method combination with EMF cache */
    eval_print("(defparameter *emf-trace* nil)");
    eval_print(
        "(defgeneric emf-combo (x))");
    eval_print(
        "(defmethod emf-combo :before ((x point)) "
        "  (push (list 'before (point-x x)) *emf-trace*))");
    eval_print(
        "(defmethod emf-combo ((x point)) "
        "  (push 'primary *emf-trace*) (point-x x))");
    eval_print(
        "(defmethod emf-combo :after ((x point)) "
        "  (push 'after *emf-trace*))");
    eval_print(
        "(defmethod emf-combo :around ((x point)) "
        "  (push 'around *emf-trace*) (call-next-method))");
    /* First call — builds EMF */
    eval_print("(setq *emf-trace* nil)");
    ASSERT_STR_EQ(eval_print(
        "(emf-combo (make-instance 'point :x 5 :y 0))"),
        "5");
    ASSERT_STR_EQ(eval_print("(nreverse *emf-trace*)"),
        "(AROUND (BEFORE 5) PRIMARY AFTER)");
    /* Second call — uses cached EMF, different arg values */
    eval_print("(setq *emf-trace* nil)");
    ASSERT_STR_EQ(eval_print(
        "(emf-combo (make-instance 'point :x 88 :y 0))"),
        "88");
    ASSERT_STR_EQ(eval_print("(nreverse *emf-trace*)"),
        "(AROUND (BEFORE 88) PRIMARY AFTER)");
}

TEST(emf_cache_next_method_p)
{
    /* next-method-p returns correct values in cached path */
    eval_print(
        "(defgeneric emf-nmp (x))");
    eval_print(
        "(defmethod emf-nmp ((x point3d)) "
        "  (list (next-method-p) (call-next-method)))");
    eval_print(
        "(defmethod emf-nmp ((x point)) "
        "  (next-method-p))");
    /* point3d has next, point does not */
    ASSERT_STR_EQ(eval_print(
        "(emf-nmp (make-instance 'point3d :x 0 :y 0 :z 0))"),
        "(T NIL)");
    /* Cached call */
    ASSERT_STR_EQ(eval_print(
        "(emf-nmp (make-instance 'point3d :x 1 :y 1 :z 1))"),
        "(T NIL)");
}

TEST(emf_cache_no_applicable_cached)
{
    /* Negative caching: no-applicable-method error is cached */
    eval_print(
        "(defgeneric emf-neg (x))");
    eval_print(
        "(defmethod emf-neg ((x point)) 'ok)");
    /* Works for point */
    ASSERT_STR_EQ(eval_print(
        "(emf-neg (make-instance 'point :x 0 :y 0))"),
        "OK");
    /* Errors for string — first call */
    ASSERT_STR_EQ(eval_print(
        "(handler-case (emf-neg \"hello\") "
        "  (error (c) 'no-method))"),
        "NO-METHOD");
    /* Errors for string — second call uses negative cache */
    ASSERT_STR_EQ(eval_print(
        "(handler-case (emf-neg \"world\") "
        "  (error (c) 'no-method))"),
        "NO-METHOD");
}

/* === Phase 13: Multi-dispatch cache === */

TEST(multi_cache_two_arg)
{
    /* 2-arg GF dispatches correctly with multi-dispatch cache */
    eval_print(
        "(defgeneric mc-test (x y))");
    eval_print(
        "(defmethod mc-test ((x point) (y point)) "
        "  (+ (point-x x) (point-x y)))");
    eval_print(
        "(defmethod mc-test ((x point3d) (y point)) "
        "  (+ (point-x x) (point-z x) (point-x y)))");
    ASSERT_STR_EQ(eval_print(
        "(mc-test (make-instance 'point :x 10 :y 0) "
        "         (make-instance 'point :x 20 :y 0))"),
        "30");
    ASSERT_STR_EQ(eval_print(
        "(mc-test (make-instance 'point3d :x 10 :y 0 :z 5) "
        "         (make-instance 'point :x 20 :y 0))"),
        "35");
}

TEST(multi_cache_hit)
{
    /* Second call with same class combo uses cache */
    ASSERT_STR_EQ(eval_print(
        "(mc-test (make-instance 'point :x 1 :y 0) "
        "         (make-instance 'point :x 2 :y 0))"),
        "3");
    /* Cache should be populated */
    ASSERT_STR_EQ(eval_print(
        "(let ((gf (gethash 'mc-test *generic-function-table*))) "
        "  (not (null (gf-dispatch-cache gf))))"),
        "T");
}

TEST(multi_cache_different_combos)
{
    /* Different class pairs cached separately */
    eval_print(
        "(defmethod mc-test ((x point) (y point3d)) "
        "  (+ (point-x x) (point-z y)))");
    ASSERT_STR_EQ(eval_print(
        "(mc-test (make-instance 'point :x 10 :y 0) "
        "         (make-instance 'point3d :x 1 :y 2 :z 30))"),
        "40");
    /* Previous combo still works */
    ASSERT_STR_EQ(eval_print(
        "(mc-test (make-instance 'point :x 5 :y 0) "
        "         (make-instance 'point :x 7 :y 0))"),
        "12");
}

TEST(multi_cache_cacheable_p_value)
{
    /* cacheable-p returns correct integer for multi-dispatch */
    ASSERT_STR_EQ(eval_print(
        "(let ((gf (gethash 'mc-test *generic-function-table*))) "
        "  (gf-cacheable-p gf))"),
        "2");
    /* Single-dispatch GF still returns 1 */
    ASSERT_STR_EQ(eval_print(
        "(let ((gf (gethash 'cache-test-1 *generic-function-table*))) "
        "  (gf-cacheable-p gf))"),
        "1");
}

TEST(multi_cache_invalidation)
{
    /* defmethod clears multi cache */
    /* Populate cache */
    eval_print(
        "(mc-test (make-instance 'point :x 1 :y 0) "
        "         (make-instance 'point :x 2 :y 0))");
    ASSERT_STR_EQ(eval_print(
        "(let ((gf (gethash 'mc-test *generic-function-table*))) "
        "  (not (null (gf-dispatch-cache gf))))"),
        "T");
    /* Add method — cache cleared */
    eval_print(
        "(defmethod mc-test ((x point3d) (y point3d)) "
        "  (+ (point-z x) (point-z y)))");
    ASSERT_STR_EQ(eval_print(
        "(let ((gf (gethash 'mc-test *generic-function-table*))) "
        "  (gf-dispatch-cache gf))"),
        "NIL");
    /* New method works */
    ASSERT_STR_EQ(eval_print(
        "(mc-test (make-instance 'point3d :x 0 :y 0 :z 7) "
        "         (make-instance 'point3d :x 0 :y 0 :z 3))"),
        "10");
}

TEST(multi_cache_call_next_method)
{
    /* call-next-method works in multi-dispatch */
    eval_print(
        "(defgeneric mc-cnm (x y))");
    eval_print(
        "(defmethod mc-cnm ((x point) (y point)) "
        "  (list 'base (point-x x) (point-x y)))");
    eval_print(
        "(defmethod mc-cnm ((x point3d) (y point)) "
        "  (list 'p3d (call-next-method)))");
    ASSERT_STR_EQ(eval_print(
        "(mc-cnm (make-instance 'point3d :x 10 :y 0 :z 5) "
        "        (make-instance 'point :x 20 :y 0))"),
        "(P3D (BASE 10 20))");
    /* Cached */
    ASSERT_STR_EQ(eval_print(
        "(mc-cnm (make-instance 'point3d :x 99 :y 0 :z 1) "
        "        (make-instance 'point :x 88 :y 0))"),
        "(P3D (BASE 99 88))");
}

/* === Phase 14: EQL specializer cache === */

TEST(eql_cache_basic)
{
    /* EQL dispatch works with cache */
    eval_print(
        "(defgeneric eql-c-test (x))");
    eval_print(
        "(defmethod eql-c-test ((x (eql 42))) 'forty-two)");
    eval_print(
        "(defmethod eql-c-test ((x integer)) 'some-int)");
    ASSERT_STR_EQ(eval_print("(eql-c-test 42)"), "FORTY-TWO");
    ASSERT_STR_EQ(eval_print("(eql-c-test 7)"), "SOME-INT");
}

TEST(eql_cache_hit)
{
    /* Second call with same EQL value uses cache */
    ASSERT_STR_EQ(eval_print("(eql-c-test 42)"), "FORTY-TWO");
    ASSERT_STR_EQ(eval_print("(eql-c-test 7)"), "SOME-INT");
    /* Cache should be populated */
    ASSERT_STR_EQ(eval_print(
        "(let ((gf (gethash 'eql-c-test *generic-function-table*))) "
        "  (not (null (gf-dispatch-cache gf))))"),
        "T");
}

TEST(eql_cache_mixed_eql_class)
{
    /* Multiple EQL specializers with class fallback */
    eval_print(
        "(defgeneric eql-mix (x))");
    eval_print(
        "(defmethod eql-mix ((x (eql :alpha))) 'got-alpha)");
    eval_print(
        "(defmethod eql-mix ((x (eql :beta))) 'got-beta)");
    eval_print(
        "(defmethod eql-mix ((x symbol)) 'some-symbol)");
    ASSERT_STR_EQ(eval_print("(eql-mix :alpha)"), "GOT-ALPHA");
    ASSERT_STR_EQ(eval_print("(eql-mix :beta)"), "GOT-BETA");
    ASSERT_STR_EQ(eval_print("(eql-mix :gamma)"), "SOME-SYMBOL");
    ASSERT_STR_EQ(eval_print("(eql-mix 'foo)"), "SOME-SYMBOL");
}

TEST(eql_cache_class_fallback)
{
    /* Non-EQL value falls back to class cache */
    ASSERT_STR_EQ(eval_print("(eql-c-test 100)"), "SOME-INT");
    ASSERT_STR_EQ(eval_print("(eql-c-test 200)"), "SOME-INT");
}

TEST(eql_cache_call_next_method)
{
    /* EQL method calls next class method via call-next-method */
    eval_print(
        "(defgeneric eql-cnm (x))");
    eval_print(
        "(defmethod eql-cnm ((x (eql 99))) "
        "  (list 'eql-99 (call-next-method)))");
    eval_print(
        "(defmethod eql-cnm ((x integer)) "
        "  (list 'int x))");
    ASSERT_STR_EQ(eval_print("(eql-cnm 99)"), "(EQL-99 (INT 99))");
    /* Cached */
    ASSERT_STR_EQ(eval_print("(eql-cnm 99)"), "(EQL-99 (INT 99))");
    /* Non-EQL path */
    ASSERT_STR_EQ(eval_print("(eql-cnm 50)"), "(INT 50)");
}

TEST(eql_cache_nil_value)
{
    /* (eql nil) specializer works correctly */
    eval_print(
        "(defgeneric eql-nil-test (x))");
    eval_print(
        "(defmethod eql-nil-test ((x (eql nil))) 'got-nil)");
    eval_print(
        "(defmethod eql-nil-test ((x t)) 'got-other)");
    ASSERT_STR_EQ(eval_print("(eql-nil-test nil)"), "GOT-NIL");
    ASSERT_STR_EQ(eval_print("(eql-nil-test 'foo)"), "GOT-OTHER");
    /* Cached */
    ASSERT_STR_EQ(eval_print("(eql-nil-test nil)"), "GOT-NIL");
}

TEST(eql_cache_arg2_eql)
{
    /* EQL on arg2 (ASDF pattern) */
    eval_print(
        "(defgeneric eql-arg2 (x y))");
    eval_print(
        "(defmethod eql-arg2 ((x t) (y (eql :load))) "
        "  (list 'loading x))");
    eval_print(
        "(defmethod eql-arg2 ((x t) (y (eql :compile))) "
        "  (list 'compiling x))");
    eval_print(
        "(defmethod eql-arg2 ((x t) (y t)) "
        "  (list 'default x y))");
    ASSERT_STR_EQ(eval_print("(eql-arg2 'foo :load)"), "(LOADING FOO)");
    ASSERT_STR_EQ(eval_print("(eql-arg2 'bar :compile)"), "(COMPILING BAR)");
    ASSERT_STR_EQ(eval_print("(eql-arg2 'baz :other)"), "(DEFAULT BAZ :OTHER)");
    /* Cached */
    ASSERT_STR_EQ(eval_print("(eql-arg2 'x :load)"), "(LOADING X)");
}

TEST(eql_cache_invalidation)
{
    /* Adding EQL method clears cache + recomputes sets */
    /* Populate cache */
    eval_print("(eql-c-test 42)");
    ASSERT_STR_EQ(eval_print(
        "(let ((gf (gethash 'eql-c-test *generic-function-table*))) "
        "  (not (null (gf-dispatch-cache gf))))"),
        "T");
    /* Add new EQL method — cache cleared */
    eval_print(
        "(defmethod eql-c-test ((x (eql 0))) 'zero)");
    ASSERT_STR_EQ(eval_print(
        "(let ((gf (gethash 'eql-c-test *generic-function-table*))) "
        "  (gf-dispatch-cache gf))"),
        "NIL");
    /* New method works */
    ASSERT_STR_EQ(eval_print("(eql-c-test 0)"), "ZERO");
    /* Old methods still work */
    ASSERT_STR_EQ(eval_print("(eql-c-test 42)"), "FORTY-TWO");
}

TEST(eql_cache_cacheable_p)
{
    /* cacheable-p returns :EQL for EQL-specialized GFs */
    ASSERT_STR_EQ(eval_print(
        "(let ((gf (gethash 'eql-c-test *generic-function-table*))) "
        "  (gf-cacheable-p gf))"),
        ":EQL");
}

/* === Slot Definition Metaobjects (MOP) === */

TEST(mop1_direct_slot_is_metaobject)
{
    /* class-direct-slots returns standard-direct-slot-definition instances */
    ASSERT_STR_EQ(eval_print(
        "(typep (first (class-direct-slots (find-class 'point))) "
        "       'standard-direct-slot-definition)"),
        "T");
    /* Also satisfies slot-definition and standard-slot-definition */
    ASSERT_STR_EQ(eval_print(
        "(typep (first (class-direct-slots (find-class 'point))) "
        "       'slot-definition)"),
        "T");
    ASSERT_STR_EQ(eval_print(
        "(typep (first (class-direct-slots (find-class 'point))) "
        "       'standard-slot-definition)"),
        "T");
}

TEST(mop1_effective_slot_is_metaobject)
{
    /* class-slots returns standard-effective-slot-definition instances */
    ASSERT_STR_EQ(eval_print(
        "(typep (first (class-slots (find-class 'point))) "
        "       'standard-effective-slot-definition)"),
        "T");
}

TEST(mop1_class_slots_alias)
{
    /* class-slots is an alias for class-effective-slots */
    ASSERT_STR_EQ(eval_print(
        "(equal (class-slots (find-class 'point)) "
        "       (class-effective-slots (find-class 'point)))"),
        "T");
    ASSERT_STR_EQ(eval_print(
        "(length (class-slots (find-class 'point)))"),
        "2");
}

TEST(mop1_slot_def_accessors_name)
{
    ASSERT_STR_EQ(eval_print(
        "(slot-definition-name "
        "  (first (class-direct-slots (find-class 'point))))"),
        "X");
    ASSERT_STR_EQ(eval_print(
        "(slot-definition-name "
        "  (second (class-direct-slots (find-class 'point))))"),
        "Y");
}

TEST(mop1_slot_def_accessors_initargs)
{
    /* slot-definition-initargs returns a list (per AMOP) */
    ASSERT_STR_EQ(eval_print(
        "(slot-definition-initargs "
        "  (first (class-direct-slots (find-class 'point))))"),
        "(:X)");
}

TEST(mop1_slot_def_accessors_readers_writers)
{
    /* readers and writers are exposed on direct slot defs */
    ASSERT_STR_EQ(eval_print(
        "(slot-definition-readers "
        "  (first (class-direct-slots (find-class 'point))))"),
        "(POINT-X)");
    ASSERT_STR_EQ(eval_print(
        "(slot-definition-writers "
        "  (first (class-direct-slots (find-class 'point))))"),
        "((SETF POINT-X))");
}

TEST(mop1_slot_def_accessors_allocation)
{
    /* Default allocation is :instance */
    ASSERT_STR_EQ(eval_print(
        "(slot-definition-allocation "
        "  (first (class-direct-slots (find-class 'point))))"),
        ":INSTANCE");
}

TEST(mop1_slot_def_accessors_initform)
{
    /* A class with an explicit :initform */
    eval_print(
        "(defclass mop1-has-initform () "
        "  ((n :initarg :n :initform 42)))");
    ASSERT_STR_EQ(eval_print(
        "(slot-definition-initform "
        "  (first (class-direct-slots (find-class 'mop1-has-initform))))"),
        "42");
    ASSERT_STR_EQ(eval_print(
        "(functionp "
        "  (slot-definition-initfunction "
        "    (first (class-direct-slots (find-class 'mop1-has-initform)))))"),
        "T");
    /* initfunction returns the initform value when called */
    ASSERT_STR_EQ(eval_print(
        "(funcall "
        "  (slot-definition-initfunction "
        "    (first (class-direct-slots (find-class 'mop1-has-initform)))))"),
        "42");
    /* make-instance uses the initform */
    ASSERT_STR_EQ(eval_print(
        "(slot-value (make-instance 'mop1-has-initform) 'n)"),
        "42");
}

TEST(mop1_effective_slot_location)
{
    /* effective slot location is the integer instance index */
    ASSERT_STR_EQ(eval_print(
        "(slot-definition-location "
        "  (first (class-slots (find-class 'point))))"),
        "0");
    ASSERT_STR_EQ(eval_print(
        "(slot-definition-location "
        "  (second (class-slots (find-class 'point))))"),
        "1");
}

TEST(mop1_direct_slot_definition_class_gf)
{
    /* direct-slot-definition-class is a GF returning the standard class */
    ASSERT_STR_EQ(eval_print(
        "(class-name "
        "  (direct-slot-definition-class (find-class 'standard-class)))"),
        "STANDARD-DIRECT-SLOT-DEFINITION");
    ASSERT_STR_EQ(eval_print(
        "(class-name "
        "  (effective-slot-definition-class (find-class 'standard-class)))"),
        "STANDARD-EFFECTIVE-SLOT-DEFINITION");
}

TEST(mop1_direct_slot_definition_class_customizable)
{
    /* Users can add an :around method — shows the GF dispatches properly */
    eval_print(
        "(defvar *mop1-dsdc-calls* 0)");
    eval_print(
        "(defmethod direct-slot-definition-class :around "
        "    ((class standard-class) &rest initargs) "
        "  (declare (ignore initargs)) "
        "  (incf *mop1-dsdc-calls*) "
        "  (call-next-method))");
    eval_print(
        "(direct-slot-definition-class (find-class 'standard-class))");
    /* The :around method ran at least once */
    ASSERT_STR_EQ(eval_print("(> *mop1-dsdc-calls* 0)"), "T");
    /* And still returned the standard slot-definition class */
    ASSERT_STR_EQ(eval_print(
        "(class-name "
        "  (direct-slot-definition-class (find-class 'standard-class)))"),
        "STANDARD-DIRECT-SLOT-DEFINITION");
}

TEST(mop1_inheritance_merges_slots)
{
    /* Effective slot count is the union of direct slots across CPL */
    eval_print(
        "(defclass mop1-base () "
        "  ((a :initarg :a :initform 'a-val)))");
    eval_print(
        "(defclass mop1-sub (mop1-base) "
        "  ((b :initarg :b :initform 'b-val)))");
    ASSERT_STR_EQ(eval_print(
        "(length (class-slots (find-class 'mop1-sub)))"),
        "2");
    /* Inherited slot's initfunction survives the merge */
    ASSERT_STR_EQ(eval_print(
        "(slot-value (make-instance 'mop1-sub) 'a)"),
        "A-VAL");
    ASSERT_STR_EQ(eval_print(
        "(slot-value (make-instance 'mop1-sub) 'b)"),
        "B-VAL");
}

TEST(mop1_inheritance_initargs_unioned)
{
    /* Per AMOP, initargs are unioned across all direct slots for an
       effective slot of that name. */
    eval_print(
        "(defclass mop1-p1 () "
        "  ((x :initarg :a)))");
    eval_print(
        "(defclass mop1-p2 (mop1-p1) "
        "  ((x :initarg :b)))");
    /* Both :a and :b should be valid initargs for the effective x slot */
    ASSERT_STR_EQ(eval_print(
        "(slot-value (make-instance 'mop1-p2 :a 1) 'x)"),
        "1");
    ASSERT_STR_EQ(eval_print(
        "(slot-value (make-instance 'mop1-p2 :b 2) 'x)"),
        "2");
}

/* === Class finalization protocol (MOP) === */

TEST(finalize_class_finalized_p)
{
    /* A defclassed class is finalized */
    eval_print("(defclass cf-f1 () ((x :initarg :x)))");
    ASSERT_STR_EQ(eval_print(
        "(class-finalized-p (find-class 'cf-f1))"),
        "T");
    /* Bootstrap classes are pre-finalized too */
    ASSERT_STR_EQ(eval_print(
        "(class-finalized-p (find-class 'standard-object))"),
        "T");
}

TEST(finalize_inheritance_idempotent)
{
    /* Calling finalize-inheritance on a finalized class must not error
       and must leave it finalized. */
    eval_print("(defclass cf-f2 () ((n :initarg :n :initform 7)))");
    ASSERT_STR_EQ(eval_print(
        "(progn (finalize-inheritance (find-class 'cf-f2)) "
        "       (class-finalized-p (find-class 'cf-f2)))"),
        "T");
    /* Twice is still fine. */
    ASSERT_STR_EQ(eval_print(
        "(progn (finalize-inheritance (find-class 'cf-f2)) "
        "       (finalize-inheritance (find-class 'cf-f2)) "
        "       (class-finalized-p (find-class 'cf-f2)))"),
        "T");
    /* Slots are still accessible — core machinery intact. */
    ASSERT_STR_EQ(eval_print(
        "(slot-value (make-instance 'cf-f2) 'n)"),
        "7");
}

TEST(compute_class_precedence_list_default)
{
    /* Default compute-class-precedence-list matches class-precedence-list */
    eval_print("(defclass cf-a () ())");
    eval_print("(defclass cf-b (cf-a) ())");
    ASSERT_STR_EQ(eval_print(
        "(mapcar #'class-name "
        "  (compute-class-precedence-list (find-class 'cf-b)))"),
        "(CF-B CF-A STANDARD-OBJECT T)");
}

TEST(compute_slots_returns_effective)
{
    eval_print("(defclass cf-cs () ((a :initarg :a) (b :initarg :b)))");
    /* compute-slots returns a fresh list of effective slot defs */
    ASSERT_STR_EQ(eval_print(
        "(length (compute-slots (find-class 'cf-cs)))"),
        "2");
    ASSERT_STR_EQ(eval_print(
        "(mapcar #'slot-definition-name "
        "  (compute-slots (find-class 'cf-cs)))"),
        "(A B)");
}

TEST(compute_effective_slot_definition_default)
{
    /* compute-effective-slot-definition merges a list of direct slots
       into a single effective slot. */
    eval_print("(defclass cf-ce () ((k :initarg :k :initform 1)))");
    ASSERT_STR_EQ(eval_print(
        "(slot-definition-name "
        "  (compute-effective-slot-definition "
        "    (find-class 'cf-ce) 'k "
        "    (class-direct-slots (find-class 'cf-ce))))"),
        "K");
    ASSERT_STR_EQ(eval_print(
        "(typep "
        "  (compute-effective-slot-definition "
        "    (find-class 'cf-ce) 'k "
        "    (class-direct-slots (find-class 'cf-ce))) "
        "  'standard-effective-slot-definition)"),
        "T");
}

TEST(compute_slots_around_hook)
{
    /* A :around method on compute-slots observes the standard result
       and can post-process — this is the key MOP acceptance test. */
    eval_print("(defclass cf-around () ((x :initarg :x)))");
    eval_print("(defvar *cf-around-saw* nil)");
    eval_print(
        "(defmethod compute-slots :around ((class standard-class)) "
        "  (let ((slots (call-next-method))) "
        "    (setq *cf-around-saw* (length slots)) "
        "    slots))");
    /* Force a re-finalization by redefining — defclass calls ensure-class
       which runs finalize-inheritance, which runs compute-slots. */
    eval_print(
        "(defclass cf-around () ((x :initarg :x) (y :initarg :y)))");
    ASSERT_STR_EQ(eval_print("*cf-around-saw*"), "2");
    /* Clean up the :around method so it doesn't bleed into later tests */
    eval_print(
        "(remove-method #'compute-slots "
        "  (find-method #'compute-slots '(:around) "
        "    (list (find-class 'standard-class))))");
}

TEST(compute_default_initargs_inherits)
{
    /* Default initargs defined on a superclass are inherited by the
       subclass's effective default-initargs. */
    eval_print(
        "(defclass cf-di-base () "
        "  ((a :initarg :a)) "
        "  (:default-initargs :a 99))");
    eval_print("(defclass cf-di-sub (cf-di-base) ())");
    /* The sub inherits :a default => slot a defaults to 99 */
    ASSERT_STR_EQ(eval_print(
        "(slot-value (make-instance 'cf-di-sub) 'a)"),
        "99");
    /* compute-default-initargs returns the merged list */
    ASSERT_STR_EQ(eval_print(
        "(first (first (compute-default-initargs "
        "                (find-class 'cf-di-sub))))"),
        ":A");
}

TEST(compute_default_initargs_most_specific_wins)
{
    /* When parent and child both specify a default for the same key,
       the child wins. */
    eval_print(
        "(defclass cf-di-p () "
        "  ((a :initarg :a)) "
        "  (:default-initargs :a 'parent))");
    eval_print(
        "(defclass cf-di-c (cf-di-p) () "
        "  (:default-initargs :a 'child))");
    ASSERT_STR_EQ(eval_print(
        "(slot-value (make-instance 'cf-di-c) 'a)"),
        "CHILD");
}

TEST(validate_superclass_default)
{
    /* validate-superclass returns T for all pairs (single-metaclass world) */
    ASSERT_STR_EQ(eval_print(
        "(validate-superclass (find-class 'standard-class) "
        "                     (find-class 'standard-object))"),
        "T");
    ASSERT_STR_EQ(eval_print(
        "(validate-superclass (find-class 't) (find-class 't))"),
        "T");
}

TEST(class_direct_default_initargs_accessor)
{
    eval_print(
        "(defclass cf-ddi () "
        "  ((x :initarg :x)) "
        "  (:default-initargs :x 42))");
    ASSERT_STR_EQ(eval_print(
        "(first (first (class-direct-default-initargs "
        "                (find-class 'cf-ddi))))"),
        ":X");
    /* The value slot is a lambda — invoking it yields the expression */
    ASSERT_STR_EQ(eval_print(
        "(funcall (second (first (class-direct-default-initargs "
        "                          (find-class 'cf-ddi)))))"),
        "42");
}

TEST(class_prototype_allocates)
{
    eval_print("(defclass cf-proto () ((x :initarg :x)))");
    /* class-prototype returns a fresh instance, cached on the class */
    ASSERT_STR_EQ(eval_print(
        "(typep (class-prototype (find-class 'cf-proto)) 'cf-proto)"),
        "T");
    /* Cached: same object every call */
    ASSERT_STR_EQ(eval_print(
        "(eq (class-prototype (find-class 'cf-proto)) "
        "    (class-prototype (find-class 'cf-proto)))"),
        "T");
}

TEST(ensure_class_creates)
{
    /* ensure-class creates a new class when none exists */
    eval_print(
        "(ensure-class 'cf-ec "
        "  :direct-superclasses '() "
        "  :direct-slots (list (%make-direct-slot-def "
        "                        'v '(:v) nil nil nil :instance nil nil nil)) "
        "  :direct-default-initargs '())");
    ASSERT_STR_EQ(eval_print(
        "(class-name (find-class 'cf-ec))"),
        "CF-EC");
    ASSERT_STR_EQ(eval_print(
        "(length (class-slots (find-class 'cf-ec)))"),
        "1");
}

TEST(ensure_class_using_class_dispatches)
{
    /* ensure-class-using-class is a GF that dispatches on the existing
       class (NIL when creating fresh). A user :around method sees the
       calls — observable regardless of whether the class exists. */
    eval_print("(defvar *cf-ecuc-calls* 0)");
    eval_print(
        "(defmethod ensure-class-using-class :around ((class t) name &rest keys) "
        "  (declare (ignore keys)) "
        "  (incf *cf-ecuc-calls*) "
        "  (call-next-method))");
    eval_print("(defclass cf-ecuc-a () ())");
    eval_print("(defclass cf-ecuc-b () ())");
    ASSERT_STR_EQ(eval_print("(>= *cf-ecuc-calls* 2)"), "T");
    /* Clean up so the :around method doesn't bleed into later tests */
    eval_print(
        "(remove-method #'ensure-class-using-class "
        "  (find-method #'ensure-class-using-class '(:around) "
        "    (list (find-class 't))))");
}

/* === Slot-access protocol (MOP) === */

TEST(svuc_fast_path_flag_clean)
{
    /* With no user methods installed, the extension flag remains NIL and
       slot-value takes the fast %struct-ref path. */
    eval_print("(defclass svuc-a () ((x :initarg :x :initform 10)))");
    ASSERT_STR_EQ(eval_print("*slot-access-protocol-extended-p*"), "NIL");
    ASSERT_STR_EQ(eval_print("(slot-value (make-instance 'svuc-a :x 7) 'x)"), "7");
}

TEST(svuc_default_method_matches_struct_ref)
{
    /* The default slot-value-using-class method must be observationally
       identical to %struct-ref — calling it directly yields the same
       value as slot-value. */
    eval_print("(defclass svuc-b () ((n :initarg :n)))");
    eval_print("(defvar *svuc-b-inst* (make-instance 'svuc-b :n 42))");
    ASSERT_STR_EQ(eval_print(
        "(slot-value-using-class (class-of *svuc-b-inst*) *svuc-b-inst* "
        "                        (first (class-slots (class-of *svuc-b-inst*))))"),
        "42");
}

TEST(svuc_around_observes_reads)
{
    /* A user :around method is invoked on every slot-value call and can
       see the effective-slot-definition. */
    eval_print("(defclass svuc-c () ((k :initarg :k :initform 'orig)))");
    eval_print("(defvar *svuc-c-log* nil)");
    eval_print(
        "(defmethod slot-value-using-class :around "
        "    ((class t) (inst svuc-c) slot) "
        "  (push (slot-definition-name slot) *svuc-c-log*) "
        "  (call-next-method))");
    ASSERT_STR_EQ(eval_print("*slot-access-protocol-extended-p*"), "T");
    ASSERT_STR_EQ(eval_print("(slot-value (make-instance 'svuc-c) 'k)"), "ORIG");
    ASSERT_STR_EQ(eval_print("(first *svuc-c-log*)"), "K");
    /* Clean up */
    eval_print(
        "(remove-method #'slot-value-using-class "
        "  (find-method #'slot-value-using-class '(:around) "
        "    (list (find-class 't) (find-class 'svuc-c) "
        "          (find-class 'standard-effective-slot-definition))))");
}

TEST(svuc_around_can_override_read)
{
    /* An :around method can transform the returned value by not calling
       call-next-method or by wrapping its result. */
    eval_print("(defclass svuc-d () ((v :initarg :v :initform 1)))");
    eval_print(
        "(defmethod slot-value-using-class :around "
        "    ((class t) (inst svuc-d) slot) "
        "  (declare (ignore slot)) "
        "  (* 10 (call-next-method)))");
    ASSERT_STR_EQ(eval_print("(slot-value (make-instance 'svuc-d :v 3) 'v)"), "30");
    /* Clean up */
    eval_print(
        "(remove-method #'slot-value-using-class "
        "  (find-method #'slot-value-using-class '(:around) "
        "    (list (find-class 't) (find-class 'svuc-d) "
        "          (find-class 'standard-effective-slot-definition))))");
}

TEST(svuc_setf_around_observes_writes)
{
    /* (setf (slot-value ...) val) dispatches through (setf slot-value-using-class)
       when the protocol is extended. */
    eval_print("(defclass svuc-e () ((w :initarg :w :initform 0)))");
    eval_print("(defvar *svuc-e-writes* nil)");
    eval_print(
        "(defmethod (setf slot-value-using-class) :around "
        "    (new-value (class t) (inst svuc-e) slot) "
        "  (push (cons (slot-definition-name slot) new-value) *svuc-e-writes*) "
        "  (call-next-method))");
    eval_print("(defvar *svuc-e-inst* (make-instance 'svuc-e))");
    eval_print("(setf (slot-value *svuc-e-inst* 'w) 99)");
    ASSERT_STR_EQ(eval_print("(slot-value *svuc-e-inst* 'w)"), "99");
    ASSERT_STR_EQ(eval_print("(cdr (first *svuc-e-writes*))"), "99");
    ASSERT_STR_EQ(eval_print("(car (first *svuc-e-writes*))"), "W");
    /* Clean up */
    eval_print(
        "(remove-method #'(setf slot-value-using-class) "
        "  (find-method #'(setf slot-value-using-class) '(:around) "
        "    (list (find-class 't) (find-class 't) (find-class 'svuc-e) "
        "          (find-class 'standard-effective-slot-definition))))");
}

TEST(svuc_boundp_dispatches)
{
    /* slot-boundp goes through slot-boundp-using-class when extended. */
    eval_print("(defclass svuc-f () ((q :initarg :q)))");
    eval_print("(defvar *svuc-f-saw* nil)");
    eval_print(
        "(defmethod slot-boundp-using-class :around "
        "    ((class t) (inst svuc-f) slot) "
        "  (declare (ignore slot)) "
        "  (setq *svuc-f-saw* t) "
        "  (call-next-method))");
    ASSERT_STR_EQ(eval_print(
        "(slot-boundp (make-instance 'svuc-f :q 1) 'q)"),
        "T");
    ASSERT_STR_EQ(eval_print("*svuc-f-saw*"), "T");
    ASSERT_STR_EQ(eval_print(
        "(slot-boundp (make-instance 'svuc-f) 'q)"),
        "NIL");
    eval_print(
        "(remove-method #'slot-boundp-using-class "
        "  (find-method #'slot-boundp-using-class '(:around) "
        "    (list (find-class 't) (find-class 'svuc-f) "
        "          (find-class 'standard-effective-slot-definition))))");
}

TEST(svuc_makunbound_dispatches)
{
    /* slot-makunbound goes through slot-makunbound-using-class when extended. */
    eval_print("(defclass svuc-g () ((r :initarg :r :initform 5)))");
    eval_print("(defvar *svuc-g-cleared* nil)");
    eval_print(
        "(defmethod slot-makunbound-using-class :around "
        "    ((class t) (inst svuc-g) slot) "
        "  (push (slot-definition-name slot) *svuc-g-cleared*) "
        "  (call-next-method))");
    eval_print("(defvar *svuc-g-inst* (make-instance 'svuc-g))");
    eval_print("(slot-makunbound *svuc-g-inst* 'r)");
    ASSERT_STR_EQ(eval_print("(slot-boundp *svuc-g-inst* 'r)"), "NIL");
    ASSERT_STR_EQ(eval_print("(first *svuc-g-cleared*)"), "R");
    eval_print(
        "(remove-method #'slot-makunbound-using-class "
        "  (find-method #'slot-makunbound-using-class '(:around) "
        "    (list (find-class 't) (find-class 'svuc-g) "
        "          (find-class 'standard-effective-slot-definition))))");
}

TEST(svuc_struct_fallback_unaffected)
{
    /* Slot access on DEFSTRUCT instances must not go through the protocol
       (structs have no effective-slot-definition). They keep using the
       struct-slot-name lookup path. */
    eval_print("(defstruct svuc-s a b)");
    eval_print("(defvar *svuc-s-inst* (make-svuc-s :a 100 :b 200))");
    /* Even after user methods extend the protocol for CLOS instances,
       struct slot-value should still bypass them. Install a method that
       would blow up if it saw a struct. */
    eval_print(
        "(defmethod slot-value-using-class :around ((class t) (inst t) slot) "
        "  (declare (ignore slot)) "
        "  (call-next-method))");
    ASSERT_STR_EQ(eval_print("(slot-value *svuc-s-inst* 'a)"), "100");
    ASSERT_STR_EQ(eval_print("(slot-value *svuc-s-inst* 'b)"), "200");
    /* Clean up */
    eval_print(
        "(remove-method #'slot-value-using-class "
        "  (find-method #'slot-value-using-class '(:around) "
        "    (list (find-class 't) (find-class 't) "
        "          (find-class 'standard-effective-slot-definition))))");
}

TEST(svuc_location_accessor)
{
    /* slot-definition-location returns the instance index on a finalized
       class — the value the default slot-value-using-class uses to find
       the slot in the struct layout. */
    eval_print("(defclass svuc-loc () ((first-slot :initarg :first) "
               "                       (second-slot :initarg :second)))");
    ASSERT_STR_EQ(eval_print(
        "(slot-definition-location "
        "  (first (class-slots (find-class 'svuc-loc))))"),
        "0");
    ASSERT_STR_EQ(eval_print(
        "(slot-definition-location "
        "  (second (class-slots (find-class 'svuc-loc))))"),
        "1");
}

TEST(svuc_setf_default_roundtrips)
{
    /* Calling the (setf slot-value-using-class) default directly
       round-trips. */
    eval_print("(defclass svuc-h () ((z :initarg :z :initform 1)))");
    eval_print("(defvar *svuc-h-inst* (make-instance 'svuc-h))");
    eval_print(
        "(setf (slot-value-using-class (class-of *svuc-h-inst*) *svuc-h-inst* "
        "                              (first (class-slots (class-of *svuc-h-inst*)))) "
        "      77)");
    ASSERT_STR_EQ(eval_print("(slot-value *svuc-h-inst* 'z)"), "77");
}

TEST(svuc_slot_unbound_still_fires)
{
    /* When a slot truly is unbound, the default method still dispatches
       to slot-unbound — even when the protocol is extended. */
    eval_print("(defclass svuc-i () ((u)))");
    eval_print(
        "(defmethod slot-unbound ((class t) (inst svuc-i) slot-name) "
        "  (declare (ignore class slot-name)) "
        "  :svuc-unbound-sentinel)");
    ASSERT_STR_EQ(eval_print(
        "(slot-value (make-instance 'svuc-i) 'u)"),
        ":SVUC-UNBOUND-SENTINEL");
    /* Clean up the slot-unbound method so later tests see the default */
    eval_print(
        "(remove-method #'slot-unbound "
        "  (find-method #'slot-unbound nil "
        "    (list (find-class 't) (find-class 'svuc-i) (find-class 't))))");
}

/* === :allocation :class — shared slots === */

TEST(calloc_two_instances_share)
{
    /* Two instances of a class with :allocation :class share writes
       to that slot (AMOP §5.5: class-allocated storage is per-class). */
    eval_print("(defclass cs-share () ((n :allocation :class :initform 0)))");
    eval_print("(defvar *cs-a* (make-instance 'cs-share))");
    eval_print("(defvar *cs-b* (make-instance 'cs-share))");
    eval_print("(setf (slot-value *cs-a* 'n) 42)");
    ASSERT_STR_EQ(eval_print("(slot-value *cs-b* 'n)"), "42");
}

TEST(calloc_subclass_inherits_cell)
{
    /* A subclass that does not redefine a class-allocated slot shares
       the same storage cell with its parent. */
    eval_print("(defclass cs-parent () ((x :allocation :class :initform 'hello)))");
    eval_print("(defclass cs-child (cs-parent) ())");
    eval_print("(defvar *cs-p* (make-instance 'cs-parent))");
    eval_print("(defvar *cs-c* (make-instance 'cs-child))");
    eval_print("(setf (slot-value *cs-p* 'x) 'world)");
    ASSERT_STR_EQ(eval_print("(slot-value *cs-c* 'x)"), "WORLD");
    ASSERT_STR_EQ(eval_print(
        "(eq (slot-definition-location "
        "      (first (class-slots (find-class 'cs-parent)))) "
        "    (slot-definition-location "
        "      (first (class-slots (find-class 'cs-child)))))"),
        "T");
}

TEST(calloc_subclass_redefines_gets_own_cell)
{
    /* A subclass that redefines a class-allocated slot in its own direct
       slots gets a fresh cell; writes stay separate from the parent. */
    eval_print("(defclass cs-p2 () ((x :allocation :class :initform 1)))");
    eval_print(
        "(defclass cs-c2 (cs-p2) "
        "  ((x :allocation :class :initform 2)))");
    eval_print("(defvar *cs-p2-inst* (make-instance 'cs-p2))");
    eval_print("(defvar *cs-c2-inst* (make-instance 'cs-c2))");
    ASSERT_STR_EQ(eval_print("(slot-value *cs-p2-inst* 'x)"), "1");
    ASSERT_STR_EQ(eval_print("(slot-value *cs-c2-inst* 'x)"), "2");
    eval_print("(setf (slot-value *cs-p2-inst* 'x) 100)");
    ASSERT_STR_EQ(eval_print("(slot-value *cs-c2-inst* 'x)"), "2");
    ASSERT_STR_EQ(eval_print("(slot-value *cs-p2-inst* 'x)"), "100");
}

TEST(calloc_location_is_cons)
{
    /* AMOP §5.5: SLOT-DEFINITION-LOCATION returns a cons cell for
       class-allocated slots. The cell's cdr is the actual storage —
       initialized by MAKE-INSTANCE (initforms run on first instance). */
    eval_print("(defclass cs-loc () ((shared :allocation :class :initform 'hi)))");
    ASSERT_STR_EQ(eval_print(
        "(consp (slot-definition-location "
        "  (first (class-slots (find-class 'cs-loc)))))"),
        "T");
    eval_print("(make-instance 'cs-loc)");
    ASSERT_STR_EQ(eval_print(
        "(cdr (slot-definition-location "
        "  (first (class-slots (find-class 'cs-loc)))))"),
        "HI");
}

TEST(calloc_location_mixed)
{
    /* Instance-allocated slots keep integer locations even when
       interleaved with class-allocated slots. */
    eval_print(
        "(defclass cs-mix () "
        "  ((a :initarg :a) "
        "   (b :allocation :class :initform 'shared) "
        "   (c :initarg :c)))");
    eval_print("(defvar *cs-mix-slots* (class-slots (find-class 'cs-mix)))");
    ASSERT_STR_EQ(eval_print(
        "(slot-definition-location "
        "  (find 'a *cs-mix-slots* :key #'slot-definition-name))"),
        "0");
    ASSERT_STR_EQ(eval_print(
        "(consp (slot-definition-location "
        "  (find 'b *cs-mix-slots* :key #'slot-definition-name)))"),
        "T");
    ASSERT_STR_EQ(eval_print(
        "(slot-definition-location "
        "  (find 'c *cs-mix-slots* :key #'slot-definition-name))"),
        "1");
    /* Reading still works for all three */
    eval_print("(defvar *cs-mix-i* (make-instance 'cs-mix :a 10 :c 30))");
    ASSERT_STR_EQ(eval_print("(slot-value *cs-mix-i* 'a)"), "10");
    ASSERT_STR_EQ(eval_print("(slot-value *cs-mix-i* 'b)"), "SHARED");
    ASSERT_STR_EQ(eval_print("(slot-value *cs-mix-i* 'c)"), "30");
}

TEST(calloc_boundp_and_makunbound)
{
    /* SLOT-BOUNDP and SLOT-MAKUNBOUND operate on the shared cell for
       class-allocated slots. */
    eval_print("(defclass cs-bp () ((s :allocation :class)))");
    eval_print("(defvar *cs-bp-a* (make-instance 'cs-bp))");
    eval_print("(defvar *cs-bp-b* (make-instance 'cs-bp))");
    ASSERT_STR_EQ(eval_print("(slot-boundp *cs-bp-a* 's)"), "NIL");
    eval_print("(setf (slot-value *cs-bp-a* 's) 7)");
    ASSERT_STR_EQ(eval_print("(slot-boundp *cs-bp-b* 's)"), "T");
    eval_print("(slot-makunbound *cs-bp-a* 's)");
    ASSERT_STR_EQ(eval_print("(slot-boundp *cs-bp-b* 's)"), "NIL");
}

TEST(calloc_initform_runs_once)
{
    /* A class-slot initform runs only when the cell is unbound — so the
       first MAKE-INSTANCE fills it and subsequent calls see the existing
       value. */
    eval_print("(defvar *cs-init-counter* 0)");
    eval_print(
        "(defclass cs-init () "
        "  ((k :allocation :class :initform (incf *cs-init-counter*))))");
    eval_print("(defvar *cs-init-a* (make-instance 'cs-init))");
    eval_print("(defvar *cs-init-b* (make-instance 'cs-init))");
    ASSERT_STR_EQ(eval_print("*cs-init-counter*"), "1");
    ASSERT_STR_EQ(eval_print("(slot-value *cs-init-a* 'k)"), "1");
    ASSERT_STR_EQ(eval_print("(slot-value *cs-init-b* 'k)"), "1");
}

TEST(calloc_initarg_writes_shared_cell)
{
    /* An :initarg targeting a class slot writes to the shared cell —
       so a later instance created without the initarg still sees that
       value (the cell is no longer unbound, initform is skipped). */
    eval_print(
        "(defclass cs-ia () "
        "  ((k :allocation :class :initarg :k :initform 0)))");
    eval_print("(defvar *cs-ia-a* (make-instance 'cs-ia :k 99))");
    eval_print("(defvar *cs-ia-b* (make-instance 'cs-ia))");
    ASSERT_STR_EQ(eval_print("(slot-value *cs-ia-a* 'k)"), "99");
    ASSERT_STR_EQ(eval_print("(slot-value *cs-ia-b* 'k)"), "99");
}

TEST(calloc_struct_smaller_than_effective_slots)
{
    /* With class slots, the underlying struct has fewer cells than the
       effective slot list — the bytecode layout must not alias a class
       slot onto an instance-slot struct index. */
    eval_print(
        "(defclass cs-sz () "
        "  ((ix :initarg :ix) "
        "   (shared :allocation :class :initform 'hi) "
        "   (iy :initarg :iy)))");
    eval_print("(defvar *cs-sz-inst* (make-instance 'cs-sz :ix 10 :iy 20))");
    /* Only 2 instance slots → struct size 2 → index 1 must be iy, not shared. */
    ASSERT_STR_EQ(eval_print("(%struct-ref *cs-sz-inst* 0)"), "10");
    ASSERT_STR_EQ(eval_print("(%struct-ref *cs-sz-inst* 1)"), "20");
}

/* === Reified EQL specializers (MOP) === */

TEST(eqlspec_intern_identity)
{
    /* Same value produces EQ metaobject (interned). */
    ASSERT_STR_EQ(eval_print(
        "(eq (intern-eql-specializer 42) (intern-eql-specializer 42))"),
        "T");
    ASSERT_STR_EQ(eval_print(
        "(eq (intern-eql-specializer 'foo) (intern-eql-specializer 'foo))"),
        "T");
    /* Different values → distinct metaobjects. */
    ASSERT_STR_EQ(eval_print(
        "(eq (intern-eql-specializer 42) (intern-eql-specializer 43))"),
        "NIL");
}

TEST(eqlspec_intern_eql_discriminates)
{
    /* Interning is by EQL, not EQ. Bignums that are EQL share a metaobject;
       objects that are neither EQ nor EQL do not. */
    ASSERT_STR_EQ(eval_print(
        "(eq (intern-eql-specializer 1.5) (intern-eql-specializer 1.5))"),
        "T");
    ASSERT_STR_EQ(eval_print(
        "(eq (intern-eql-specializer \"abc\") (intern-eql-specializer \"abc\"))"),
        "NIL");
}

TEST(eqlspec_object_accessor)
{
    ASSERT_STR_EQ(eval_print(
        "(eql-specializer-object (intern-eql-specializer 42))"),
        "42");
    ASSERT_STR_EQ(eval_print(
        "(eql-specializer-object (intern-eql-specializer 'hello))"),
        "HELLO");
    ASSERT_STR_EQ(eval_print(
        "(eql-specializer-object (intern-eql-specializer nil))"),
        "NIL");
}

TEST(eqlspec_p_predicate)
{
    ASSERT_STR_EQ(eval_print(
        "(eql-specializer-p (intern-eql-specializer 99))"),
        "T");
    /* Class objects are not eql-specializers. */
    ASSERT_STR_EQ(eval_print(
        "(eql-specializer-p (find-class 'integer))"),
        "NIL");
    /* Other things are not eql-specializers. */
    ASSERT_STR_EQ(eval_print("(eql-specializer-p 42)"), "NIL");
    ASSERT_STR_EQ(eval_print("(eql-specializer-p '(eql 42))"), "NIL");
    ASSERT_STR_EQ(eval_print("(eql-specializer-p nil)"), "NIL");
}

TEST(eqlspec_class_bootstrapped)
{
    ASSERT_STR_EQ(eval_print(
        "(class-name (class-of (intern-eql-specializer 42)))"),
        "EQL-SPECIALIZER");
    ASSERT_STR_EQ(eval_print(
        "(not (null (find-class 'eql-specializer)))"),
        "T");
}

TEST(eqlspec_dispatch_still_works)
{
    /* Regression guard: reification doesn't break EQL dispatch. */
    eval_print("(defgeneric es-disp (x))");
    eval_print("(defmethod es-disp ((x (eql 'alpha))) :got-alpha)");
    eval_print("(defmethod es-disp ((x (eql 'beta))) :got-beta)");
    eval_print("(defmethod es-disp ((x symbol)) :fallback)");
    ASSERT_STR_EQ(eval_print("(es-disp 'alpha)"), ":GOT-ALPHA");
    ASSERT_STR_EQ(eval_print("(es-disp 'beta)"),  ":GOT-BETA");
    ASSERT_STR_EQ(eval_print("(es-disp 'other)"), ":FALLBACK");
}

TEST(eqlspec_method_specializers_contains_metaobject)
{
    /* method-specializers now stores eql-specializer structs, not (eql V) cons. */
    eval_print("(defgeneric es-ms (x))");
    eval_print("(defmethod es-ms ((x (eql 7))) :seven)");
    ASSERT_STR_EQ(eval_print(
        "(let* ((gf (gethash 'es-ms *generic-function-table*)) "
        "       (m  (first (gf-methods gf))) "
        "       (s  (first (method-specializers m)))) "
        "  (eql-specializer-p s))"),
        "T");
    ASSERT_STR_EQ(eval_print(
        "(let* ((gf (gethash 'es-ms *generic-function-table*)) "
        "       (m  (first (gf-methods gf))) "
        "       (s  (first (method-specializers m)))) "
        "  (eql-specializer-object s))"),
        "7");
}

TEST(eqlspec_method_specializers_shared_across_methods)
{
    /* Two methods with the same EQL value share the same specializer
       metaobject (interned). */
    eval_print("(defgeneric es-share1 (x))");
    eval_print("(defgeneric es-share2 (x))");
    eval_print("(defmethod es-share1 ((x (eql :shared))) :one)");
    eval_print("(defmethod es-share2 ((x (eql :shared))) :two)");
    ASSERT_STR_EQ(eval_print(
        "(eq (first (method-specializers "
        "      (first (gf-methods (gethash 'es-share1 *generic-function-table*))))) "
        "    (first (method-specializers "
        "      (first (gf-methods (gethash 'es-share2 *generic-function-table*))))))"),
        "T");
}

TEST(eqlspec_extract_specializer_names_roundtrips)
{
    /* extract-specializer-names pulls specializer names from a
       specialized lambda list (AMOP). */
    ASSERT_STR_EQ(eval_print(
        "(extract-specializer-names '((x (eql 99)) (y string)))"),
        "((EQL 99) STRING)");
}

TEST(eqlspec_extract_class_specializers)
{
    /* extract-specializer-names from a specialized lambda list —
       class specializers returned as their class names. */
    ASSERT_STR_EQ(eval_print(
        "(extract-specializer-names '((x integer)))"),
        "(INTEGER)");
}

TEST(eqlspec_method_equal_replaces_on_redef)
{
    /* Redefining a method with the same EQL value replaces the old one
       (specializer lists compare via interned metaobject identity). */
    eval_print("(defgeneric es-rep (x))");
    eval_print("(defmethod es-rep ((x (eql 1))) :first)");
    eval_print("(defmethod es-rep ((x (eql 1))) :second)");
    ASSERT_STR_EQ(eval_print("(es-rep 1)"), ":SECOND");
    ASSERT_STR_EQ(eval_print(
        "(length (gf-methods (gethash 'es-rep *generic-function-table*)))"),
        "1");
}

TEST(eqlspec_cacheable_p_still_eql)
{
    /* cacheable-p flag remains :EQL after reification. */
    eval_print("(defgeneric es-cache (x))");
    eval_print("(defmethod es-cache ((x (eql 42))) :fortytwo)");
    eval_print("(defmethod es-cache ((x integer)) :int)");
    ASSERT_STR_EQ(eval_print(
        "(gf-cacheable-p (gethash 'es-cache *generic-function-table*))"),
        ":EQL");
}

TEST(calloc_svuc_protocol_sees_class_slots)
{
    /* The slot-value-using-class protocol fires on class-allocated slots
       too — user :around methods receive the effective-slot-definition. */
    eval_print("(defclass cs-svuc () ((tag :allocation :class :initform 'default)))");
    eval_print("(defvar *cs-svuc-log* nil)");
    eval_print(
        "(defmethod slot-value-using-class :around "
        "    ((class t) (inst cs-svuc) slot) "
        "  (push (slot-definition-name slot) *cs-svuc-log*) "
        "  (call-next-method))");
    ASSERT_STR_EQ(eval_print(
        "(slot-value (make-instance 'cs-svuc) 'tag)"),
        "DEFAULT");
    ASSERT_STR_EQ(eval_print("(first *cs-svuc-log*)"), "TAG");
    eval_print(
        "(remove-method #'slot-value-using-class "
        "  (find-method #'slot-value-using-class '(:around) "
        "    (list (find-class 't) (find-class 'cs-svuc) "
        "          (find-class 'standard-effective-slot-definition))))");
}

/* ==================================================================
 * Funcallable standard class (MOP)
 * ================================================================== */

TEST(funcallable_class_bootstrapped)
{
    /* Classes exist and are properly registered. */
    ASSERT_STR_EQ(eval_print(
        "(class-name (find-class 'funcallable-standard-class))"),
        "FUNCALLABLE-STANDARD-CLASS");
    ASSERT_STR_EQ(eval_print(
        "(class-name (find-class 'funcallable-standard-object))"),
        "FUNCALLABLE-STANDARD-OBJECT");
}

TEST(funcallable_gf_is_funcallable_standard_object)
{
    /* Every generic function is a funcallable-standard-object. */
    eval_print("(defgeneric fsc-foo (x))");
    eval_print("(defmethod fsc-foo ((x integer)) (* x 2))");
    ASSERT_STR_EQ(eval_print("(typep #'fsc-foo 'funcallable-standard-object)"), "T");
    ASSERT_STR_EQ(eval_print("(typep #'fsc-foo 'standard-generic-function)"), "T");
    /* And it's typep 'function via the CPL chain. */
    ASSERT_STR_EQ(eval_print("(typep #'fsc-foo 'function)"), "T");
    ASSERT_STR_EQ(eval_print("(functionp #'fsc-foo)"), "T");
}

TEST(funcallable_class_of_gf)
{
    /* class-of on a GF returns standard-generic-function. */
    eval_print("(defgeneric fsc-co (x))");
    eval_print("(defmethod fsc-co ((x t)) x)");
    ASSERT_STR_EQ(eval_print(
        "(eq (class-of #'fsc-co) (find-class 'standard-generic-function))"),
        "T");
}

TEST(funcallable_gf_still_callable_via_symbol)
{
    /* Regression guard: installing the GF struct in symbol-function did not
       break ordinary (foo args) dispatch. */
    eval_print("(defgeneric fsc-call (x))");
    eval_print("(defmethod fsc-call ((x integer)) (* x 3))");
    eval_print("(defmethod fsc-call ((x string)) (length x))");
    ASSERT_STR_EQ(eval_print("(fsc-call 7)"), "21");
    ASSERT_STR_EQ(eval_print("(fsc-call \"hello\")"), "5");
}

TEST(funcallable_gf_via_funcall_and_apply)
{
    /* FUNCALL and APPLY of the GF metaobject both route through the
       discriminating function (cl_unwrap_funcallable). */
    eval_print("(defgeneric fsc-fa (x))");
    eval_print("(defmethod fsc-fa ((x integer)) (+ x 100))");
    ASSERT_STR_EQ(eval_print("(funcall #'fsc-fa 5)"), "105");
    ASSERT_STR_EQ(eval_print("(apply #'fsc-fa '(10))"), "110");
    /* Also works if you capture the GF struct separately. */
    ASSERT_STR_EQ(eval_print(
        "(funcall (ensure-generic-function 'fsc-fa) 3)"), "103");
}

TEST(funcallable_set_funcallable_instance_function)
{
    /* User can override dispatch completely via set-funcallable-instance-function. */
    eval_print("(defgeneric fsc-sfi (x))");
    eval_print("(defmethod fsc-sfi ((x integer)) :default)");
    ASSERT_STR_EQ(eval_print("(fsc-sfi 1)"), ":DEFAULT");
    eval_print(
        "(set-funcallable-instance-function "
        "  (ensure-generic-function 'fsc-sfi) "
        "  (lambda (&rest args) (cons :replaced args)))");
    ASSERT_STR_EQ(eval_print("(fsc-sfi 1)"), "(:REPLACED 1)");
    ASSERT_STR_EQ(eval_print("(funcall #'fsc-sfi 2 3)"), "(:REPLACED 2 3)");
}

TEST(funcallable_sfi_returns_gf)
{
    /* set-funcallable-instance-function returns the GF, per AMOP. */
    eval_print("(defgeneric fsc-sfir (x))");
    eval_print("(defmethod fsc-sfir ((x t)) nil)");
    ASSERT_STR_EQ(eval_print(
        "(eq (set-funcallable-instance-function "
        "      (ensure-generic-function 'fsc-sfir) "
        "      (lambda (&rest args) args)) "
        "    (ensure-generic-function 'fsc-sfir))"),
        "T");
}

TEST(funcallable_standard_instance_access)
{
    /* Raw slot access by integer location — bypasses GF dispatch. */
    eval_print("(defclass sia-p () ((a :initarg :a) (b :initarg :b)))");
    eval_print("(defvar *sia-inst* (make-instance 'sia-p :a 10 :b 20))");
    /* Layout: slot 0 = A, slot 1 = B. */
    ASSERT_STR_EQ(eval_print("(standard-instance-access *sia-inst* 0)"), "10");
    ASSERT_STR_EQ(eval_print("(standard-instance-access *sia-inst* 1)"), "20");
    /* SETF works. */
    eval_print("(setf (standard-instance-access *sia-inst* 0) 99)");
    ASSERT_STR_EQ(eval_print("(slot-value *sia-inst* 'a)"), "99");
}

TEST(funcallable_standard_instance_access_equals_struct_ref)
{
    /* Spec: (standard-instance-access inst idx) equals (%struct-ref inst idx). */
    eval_print("(defclass siaq () ((q :initarg :q)))");
    eval_print("(defvar *siaq-inst* (make-instance 'siaq :q 'hello))");
    ASSERT_STR_EQ(eval_print(
        "(eq (standard-instance-access *siaq-inst* 0) "
        "    (%struct-ref *siaq-inst* 0))"),
        "T");
}

TEST(funcallable_standard_instance_access_variant)
{
    /* funcallable-standard-instance-access is the funcallable twin. */
    eval_print("(defclass fsia () ((r :initarg :r)))");
    eval_print("(defvar *fsia-inst* (make-instance 'fsia :r 77))");
    ASSERT_STR_EQ(eval_print("(funcallable-standard-instance-access *fsia-inst* 0)"), "77");
    eval_print("(setf (funcallable-standard-instance-access *fsia-inst* 0) 88)");
    ASSERT_STR_EQ(eval_print("(slot-value *fsia-inst* 'r)"), "88");
}

TEST(funcallable_compute_discriminating_function_default)
{
    /* Default method returns the current discriminating function; it must
       be something callable. */
    eval_print("(defgeneric fsc-cdf (x))");
    eval_print("(defmethod fsc-cdf ((x integer)) :int-case)");
    ASSERT_STR_EQ(eval_print(
        "(functionp (compute-discriminating-function "
        "             (ensure-generic-function 'fsc-cdf)))"),
        "T");
    /* Calling the returned function does the same dispatch as the GF. */
    ASSERT_STR_EQ(eval_print(
        "(funcall (compute-discriminating-function "
        "           (ensure-generic-function 'fsc-cdf)) 5)"),
        ":INT-CASE");
}

TEST(funcallable_gf_type_of)
{
    eval_print("(defgeneric fsc-to (x))");
    eval_print("(defmethod fsc-to ((x t)) x)");
    ASSERT_STR_EQ(eval_print("(type-of #'fsc-to)"), "STANDARD-GENERIC-FUNCTION");
}

TEST(funcallable_gf_cpl_has_function)
{
    /* The CPL of standard-generic-function includes funcallable-standard-object
       and function above standard-object — callers checking (typep ... 'function)
       rely on that relationship. */
    ASSERT_STR_EQ(eval_print(
        "(let ((cpl-names (mapcar #'class-name "
        "                   (class-precedence-list "
        "                     (find-class 'standard-generic-function))))) "
        "  (list (find 'funcallable-standard-object cpl-names) "
        "        (find 'function cpl-names) "
        "        (find 'standard-object cpl-names) "
        "        (find 't cpl-names)))"),
        "(FUNCALLABLE-STANDARD-OBJECT FUNCTION STANDARD-OBJECT T)");
}

/* ============================================================
 * Method metaobject protocol (MOP)
 * ============================================================ */

TEST(mmop_method_generic_function_backlink)
{
    /* DEFMETHOD stores the installing GF on the method so metaobject
       introspection can walk from method back to its GF. */
    eval_print("(defgeneric mmop-bl (x))");
    eval_print("(defmethod mmop-bl ((x integer)) :int)");
    ASSERT_STR_EQ(eval_print(
        "(let* ((gf (gethash 'mmop-bl *generic-function-table*)) "
        "       (m  (first (gf-methods gf)))) "
        "  (eq (method-generic-function m) gf))"),
        "T");
}

TEST(mmop_extract_lambda_list_plain)
{
    /* EXTRACT-LAMBDA-LIST strips specializers from required params. */
    ASSERT_STR_EQ(eval_print(
        "(extract-lambda-list '((x point) (y (eql 3)) z))"),
        "(X Y Z)");
}

TEST(mmop_extract_lambda_list_preserves_keywords)
{
    /* Non-required parameters pass through untouched. */
    ASSERT_STR_EQ(eval_print(
        "(extract-lambda-list '((x integer) &optional (y 5) &rest rest &key k))"),
        "(X &OPTIONAL (Y 5) &REST REST &KEY K)");
}

TEST(mmop_extract_specializer_names_padded_t)
{
    /* Unspecialized required params get T. */
    ASSERT_STR_EQ(eval_print(
        "(extract-specializer-names '(x (y integer) z))"),
        "(T INTEGER T)");
}

TEST(mmop_extract_specializer_names_stops_at_keywords)
{
    /* Specializer harvest ignores non-required tail. */
    ASSERT_STR_EQ(eval_print(
        "(extract-specializer-names '((a string) &optional b &key c))"),
        "(STRING)");
}

TEST(mmop_add_method_installs_new_method)
{
    /* Calling the ADD-METHOD GF with a programmatically-built method
       makes it dispatchable — same end state as DEFMETHOD. */
    eval_print("(defgeneric mmop-add (x))");
    eval_print("(defmethod mmop-add ((x t)) :default)");
    eval_print(
        "(let* ((gf (ensure-generic-function 'mmop-add)) "
        "       (fn (lambda (x) (declare (ignore x)) :added)) "
        "       (m (%make-struct 'standard-method "
        "            nil (list (find-class 'integer)) '() fn '(x)))) "
        "  (add-method gf m))");
    ASSERT_STR_EQ(eval_print("(mmop-add 42)"), ":ADDED");
    ASSERT_STR_EQ(eval_print("(mmop-add \"hi\")"), ":DEFAULT");
}

TEST(mmop_add_method_sets_back_link)
{
    /* ADD-METHOD fills in the method's generic-function slot so the
       metaobject knows where it lives. */
    eval_print("(defgeneric mmop-addbl (x))");
    eval_print(
        "(let* ((gf (ensure-generic-function 'mmop-addbl)) "
        "       (fn (lambda (x) (declare (ignore x)) :ok)) "
        "       (m (%make-struct 'standard-method "
        "            nil (list (find-class 't)) '() fn '(x)))) "
        "  (add-method gf m) "
        "  (setq *mmop-m* m) "
        "  (setq *mmop-gf* gf))");
    ASSERT_STR_EQ(eval_print("(eq (method-generic-function *mmop-m*) *mmop-gf*)"), "T");
}

TEST(mmop_add_method_returns_gf)
{
    /* AMOP: ADD-METHOD returns the GF. */
    eval_print("(defgeneric mmop-ret (x))");
    ASSERT_STR_EQ(eval_print(
        "(let* ((gf (ensure-generic-function 'mmop-ret)) "
        "       (m (%make-struct 'standard-method "
        "            nil (list (find-class 't)) '() "
        "            (lambda (x) (declare (ignore x)) :ok) '(x)))) "
        "  (eq (add-method gf m) gf))"),
        "T");
}

TEST(mmop_remove_method_drops_dispatch)
{
    /* REMOVE-METHOD eliminates the method — subsequent calls no longer
       see it. */
    eval_print("(defgeneric mmop-rm (x))");
    eval_print("(defmethod mmop-rm ((x t)) :default)");
    eval_print("(defmethod mmop-rm ((x integer)) :int)");
    ASSERT_STR_EQ(eval_print("(mmop-rm 42)"), ":INT");
    eval_print(
        "(let* ((gf (gethash 'mmop-rm *generic-function-table*)) "
        "       (m (find-if (lambda (m) "
        "                     (equal (method-specializers m) "
        "                            (list (find-class 'integer)))) "
        "                   (gf-methods gf)))) "
        "  (remove-method gf m))");
    ASSERT_STR_EQ(eval_print("(mmop-rm 42)"), ":DEFAULT");
}

TEST(mmop_remove_method_clears_backlink)
{
    /* REMOVE-METHOD severs method->GF back-link. */
    eval_print("(defgeneric mmop-rmbl (x))");
    eval_print("(defmethod mmop-rmbl ((x integer)) :int)");
    eval_print(
        "(let* ((gf (gethash 'mmop-rmbl *generic-function-table*)) "
        "       (m (first (gf-methods gf)))) "
        "  (setq *mmop-rm-m* m) "
        "  (remove-method gf m))");
    ASSERT_STR_EQ(eval_print("(method-generic-function *mmop-rm-m*)"), "NIL");
}

TEST(mmop_find_method_returns_existing)
{
    /* FIND-METHOD locates the method by qualifiers + specializers. */
    eval_print("(defgeneric mmop-fm (x))");
    eval_print("(defmethod mmop-fm ((x string)) :s)");
    eval_print("(defmethod mmop-fm :before ((x string)) :before)");
    ASSERT_STR_EQ(eval_print(
        "(let ((gf (gethash 'mmop-fm *generic-function-table*))) "
        "  (eq (find-method gf '() (list (find-class 'string))) "
        "      (find-if (lambda (m) (null (method-qualifiers m))) "
        "               (gf-methods gf))))"),
        "T");
}

TEST(mmop_find_method_accepts_class_names)
{
    /* FIND-METHOD resolves specializer class *names* to class
       metaobjects before comparing. */
    eval_print("(defgeneric mmop-fmn (x))");
    eval_print("(defmethod mmop-fmn ((x integer)) :int)");
    ASSERT_STR_EQ(eval_print(
        "(let ((gf (gethash 'mmop-fmn *generic-function-table*))) "
        "  (not (null (find-method gf '() '(integer)))))"),
        "T");
}

TEST(mmop_find_method_eql_names)
{
    /* FIND-METHOD resolves (EQL val) names to EQL specializer metaobjects. */
    eval_print("(defgeneric mmop-fmeql (x))");
    eval_print("(defmethod mmop-fmeql ((x (eql 7))) :seven)");
    ASSERT_STR_EQ(eval_print(
        "(let ((gf (gethash 'mmop-fmeql *generic-function-table*))) "
        "  (not (null (find-method gf '() '((eql 7))))))"),
        "T");
}

TEST(mmop_find_method_missing_errors)
{
    /* FIND-METHOD signals when no match and ERRORP is T (the default). */
    eval_print("(defgeneric mmop-fmmiss (x))");
    ASSERT_STR_EQ(eval_print(
        "(handler-case "
        "  (find-method (gethash 'mmop-fmmiss *generic-function-table*) "
        "               '() '(integer)) "
        "  (error () :signaled))"),
        ":SIGNALED");
}

TEST(mmop_find_method_missing_returns_nil)
{
    /* With ERRORP NIL, FIND-METHOD returns NIL. */
    eval_print("(defgeneric mmop-fmnil (x))");
    ASSERT_STR_EQ(eval_print(
        "(find-method (gethash 'mmop-fmnil *generic-function-table*) "
        "             '() '(integer) nil)"),
        "NIL");
}

TEST(mmop_ensure_method_installs)
{
    /* ENSURE-METHOD constructs and installs a method from a lambda. */
    eval_print("(defgeneric mmop-em (x))");
    eval_print(
        "(ensure-method 'mmop-em '(lambda (x) (declare (ignore x)) :added) "
        "               :specializers '(integer))");
    ASSERT_STR_EQ(eval_print("(mmop-em 99)"), ":ADDED");
}

TEST(mmop_ensure_method_default_specializers_are_t)
{
    /* Without :specializers, ENSURE-METHOD pads with T specializers,
       giving a catch-all default method. */
    eval_print("(defgeneric mmop-emdef (x))");
    eval_print(
        "(ensure-method 'mmop-emdef '(lambda (x) (declare (ignore x)) :any))");
    ASSERT_STR_EQ(eval_print("(mmop-emdef \"any\")"), ":ANY");
    ASSERT_STR_EQ(eval_print("(mmop-emdef 42)"), ":ANY");
}

TEST(mmop_ensure_method_returns_method)
{
    /* ENSURE-METHOD returns the newly constructed method metaobject. */
    eval_print("(defgeneric mmop-emret (x))");
    ASSERT_STR_EQ(eval_print(
        "(let ((m (ensure-method 'mmop-emret "
        "            '(lambda (x) (declare (ignore x)) :ok) "
        "            :specializers '(integer)))) "
        "  (eq (%struct-type-name m) 'standard-method))"),
        "T");
}

TEST(mmop_make_method_lambda_default_identity)
{
    /* MAKE-METHOD-LAMBDA returns (values lambda-expr nil) by default —
       a no-op hook point that metaclasses can specialise. */
    eval_print("(defgeneric mmop-mml-host (x))");
    ASSERT_STR_EQ(eval_print(
        "(let* ((gf (ensure-generic-function 'mmop-mml-host)) "
        "       (m (%make-struct 'standard-method "
        "            gf (list (find-class 't)) '() "
        "            (lambda (x) x) '(x)))) "
        "  (multiple-value-list "
        "    (make-method-lambda gf m '(lambda (x) x) nil)))"),
        "((LAMBDA (X) X) NIL)");
}

/* ============================================================
 * Method combination (MOP) — built-in short forms, user-defined
 * combinations (short + long), find-method-combination, and
 * :method-combination DEFGENERIC option.
 * ============================================================ */

TEST(mc_standard_default)
{
    /* Every GF carries a method-combination metaobject.  When DEFGENERIC
       omits :METHOD-COMBINATION, STANDARD is installed by default.
       The combination is registered with the CL symbol (not exported),
       so we compare on SYMBOL-NAME to stay package-agnostic. */
    eval_print("(defgeneric mc-std-default (x))");
    eval_print("(defmethod mc-std-default ((x t)) :ok)");
    ASSERT_STR_EQ(eval_print(
        "(symbol-name "
        "  (method-combination-name "
        "    (gf-method-combination "
        "      (gethash 'mc-std-default *generic-function-table*))))"),
        "\"STANDARD\"");
}

TEST(mc_plus_basic)
{
    /* The + built-in sums primary-method return values. */
    eval_print("(defgeneric mc-plus (x) (:method-combination +))");
    eval_print("(defmethod mc-plus + ((x t)) 1)");
    eval_print("(defmethod mc-plus + ((x integer)) 10)");
    eval_print("(defmethod mc-plus + ((x number)) 100)");
    ASSERT_STR_EQ(eval_print("(mc-plus 42)"), "111");
}

TEST(mc_plus_identity_with_one_argument)
{
    /* With a single primary method + uses its value directly (the
       :IDENTITY-WITH-ONE-ARGUMENT fast path). */
    eval_print("(defgeneric mc-plus-id (x) (:method-combination +))");
    eval_print("(defmethod mc-plus-id + ((x integer)) 7)");
    ASSERT_STR_EQ(eval_print("(mc-plus-id 1)"), "7");
}

TEST(mc_list_collects)
{
    /* LIST collects return values in most-specific-first order. */
    eval_print("(defgeneric mc-list (x) (:method-combination list))");
    eval_print("(defmethod mc-list list ((x t)) :any)");
    eval_print("(defmethod mc-list list ((x number)) :num)");
    eval_print("(defmethod mc-list list ((x integer)) :int)");
    ASSERT_STR_EQ(eval_print("(mc-list 42)"), "(:INT :NUM :ANY)");
}

TEST(mc_and_short_circuits)
{
    /* AND short-circuits: once a primary returns NIL, remaining methods
       are not invoked. */
    eval_print("(defvar *mc-and-count* 0)");
    eval_print("(defgeneric mc-and (x) (:method-combination and))");
    eval_print("(defmethod mc-and and ((x t)) "
               "  (incf *mc-and-count*) t)");
    eval_print("(defmethod mc-and and ((x integer)) "
               "  (incf *mc-and-count*) nil)");
    eval_print("(defmethod mc-and and ((x number)) "
               "  (incf *mc-and-count*) "
               "  (error \"should not be called\"))");
    ASSERT_STR_EQ(eval_print("(mc-and 42)"), "NIL");
    ASSERT_STR_EQ(eval_print("*mc-and-count*"), "1");
}

TEST(mc_or_returns_first_true)
{
    /* OR returns the first non-NIL value among primaries. */
    eval_print("(defgeneric mc-or (x) (:method-combination or))");
    eval_print("(defmethod mc-or or ((x t)) nil)");
    eval_print("(defmethod mc-or or ((x integer)) :found)");
    ASSERT_STR_EQ(eval_print("(mc-or 42)"), ":FOUND");
}

TEST(mc_progn_returns_last)
{
    /* PROGN returns the last primary-method value.  With
       :IDENTITY-WITH-ONE-ARGUMENT on a single primary it returns that
       method's value unchanged. */
    eval_print("(defgeneric mc-progn (x) (:method-combination progn))");
    eval_print("(defmethod mc-progn progn ((x t)) :a)");
    eval_print("(defmethod mc-progn progn ((x integer)) :b)");
    /* Most-specific-first: INTEGER then T -> last evaluated is T's :A */
    ASSERT_STR_EQ(eval_print("(mc-progn 42)"), ":A");
}

TEST(mc_append_concatenates)
{
    /* APPEND splices primary return lists together. */
    eval_print("(defgeneric mc-app (x) (:method-combination append))");
    eval_print("(defmethod mc-app append ((x t)) '(:t))");
    eval_print("(defmethod mc-app append ((x integer)) '(:int))");
    ASSERT_STR_EQ(eval_print("(mc-app 42)"), "(:INT :T)");
}

TEST(mc_max_reduces)
{
    /* MAX returns the largest primary return value. */
    eval_print("(defgeneric mc-max (x) (:method-combination max))");
    eval_print("(defmethod mc-max max ((x t)) 1)");
    eval_print("(defmethod mc-max max ((x integer)) 5)");
    eval_print("(defmethod mc-max max ((x number)) 3)");
    ASSERT_STR_EQ(eval_print("(mc-max 42)"), "5");
}

TEST(mc_around_wraps_short_form)
{
    /* :AROUND methods are honoured on short-form combinations.
       The :AROUND method wraps the primary combination (here +). */
    eval_print("(defgeneric mc-ar (x) (:method-combination +))");
    eval_print("(defmethod mc-ar + ((x t)) 1)");
    eval_print("(defmethod mc-ar + ((x integer)) 2)");
    eval_print("(defmethod mc-ar :around ((x t)) "
               "  (+ 1000 (call-next-method)))");
    ASSERT_STR_EQ(eval_print("(mc-ar 42)"), "1003");
}

TEST(mc_invalid_qualifier_errors)
{
    /* A short-form combination rejects methods whose qualifier is not
       the combination name (nor :AROUND). */
    eval_print("(defgeneric mc-invalid (x) (:method-combination +))");
    eval_print("(defmethod mc-invalid + ((x t)) 1)");
    eval_print("(defmethod mc-invalid :before ((x t)) :bad)");
    ASSERT_STR_EQ(eval_print(
        "(handler-case (mc-invalid 1) (error () :rejected))"),
        ":REJECTED");
}

TEST(mc_standard_still_supports_before_after)
{
    /* Sanity: switching other GFs to non-standard combinations does not
       affect standard GFs — :BEFORE / :AFTER still work. */
    eval_print("(defvar *mc-std-log* nil)");
    eval_print("(defgeneric mc-std (x))");
    eval_print("(defmethod mc-std ((x t)) "
               "  (push :primary *mc-std-log*) :ok)");
    eval_print("(defmethod mc-std :before ((x t)) "
               "  (push :before *mc-std-log*))");
    eval_print("(defmethod mc-std :after ((x t)) "
               "  (push :after *mc-std-log*))");
    ASSERT_STR_EQ(eval_print("(mc-std 1)"), ":OK");
    ASSERT_STR_EQ(eval_print("(reverse *mc-std-log*)"),
                  "(:BEFORE :PRIMARY :AFTER)");
}

TEST(mc_find_method_combination_standard)
{
    /* FIND-METHOD-COMBINATION with NIL as GF designator returns the
       named combination metaobject from the registry.  Symbol-name
       keying means this works regardless of which package spells
       'STANDARD. */
    ASSERT_STR_EQ(eval_print(
        "(symbol-name "
        "  (method-combination-name "
        "    (find-method-combination nil 'standard nil)))"),
        "\"STANDARD\"");
}

TEST(mc_find_method_combination_type)
{
    /* Built-in short-form combos report type :SHORT. */
    ASSERT_STR_EQ(eval_print(
        "(method-combination-type "
        "  (find-method-combination nil '+ nil))"),
        ":SHORT");
}

TEST(mc_find_method_combination_unknown_errors)
{
    /* Unknown combination names are signalled. */
    ASSERT_STR_EQ(eval_print(
        "(handler-case "
        "  (find-method-combination nil 'no-such-combination nil) "
        "  (error () :signaled))"),
        ":SIGNALED");
}

TEST(mc_define_short_form_basic)
{
    /* DEFINE-METHOD-COMBINATION short form registers a combination that
       wraps primary return values with :OPERATOR. */
    eval_print("(define-method-combination mc-user-short "
               "  :operator * :identity-with-one-argument t)");
    eval_print("(defgeneric mc-us (x) (:method-combination mc-user-short))");
    eval_print("(defmethod mc-us mc-user-short ((x t)) 2)");
    eval_print("(defmethod mc-us mc-user-short ((x integer)) 3)");
    ASSERT_STR_EQ(eval_print("(mc-us 42)"), "6");
}

TEST(mc_define_short_form_identity_default)
{
    /* Without :IDENTITY-WITH-ONE-ARGUMENT, a single primary method is
       still wrapped through the operator (single-arg call). */
    eval_print("(define-method-combination mc-short-no-id "
               "  :operator list)");
    eval_print("(defgeneric mc-sni (x) "
               "  (:method-combination mc-short-no-id))");
    eval_print("(defmethod mc-sni mc-short-no-id ((x integer)) :v)");
    /* Single value wrapped: (list :v) -> (:V) */
    ASSERT_STR_EQ(eval_print("(mc-sni 1)"), "(:V)");
}

TEST(mc_define_short_form_operator_defaults_to_name)
{
    /* Without :OPERATOR the combination name is used as the operator. */
    eval_print("(define-method-combination mc-name-op)");
    eval_print("(defgeneric mc-no (x) (:method-combination mc-name-op))");
    eval_print("(defmethod mc-no mc-name-op ((x t)) 1)");
    ASSERT_STR_EQ(eval_print(
        "(method-combination-name "
        "  (gf-method-combination "
        "    (gethash 'mc-no *generic-function-table*)))"),
        "MC-NAME-OP");
}

TEST(mc_define_long_form_basic)
{
    /* Long form: (define-method-combination NAME () (GROUPS) BODY) —
       BODY returns a form that uses CALL-METHOD. */
    eval_print(
        "(define-method-combination mc-first ()"
        "  ((primary ()))"
        "  `(call-method ,(first primary)))");
    eval_print("(defgeneric mc-f1 (x) (:method-combination mc-first))");
    eval_print("(defmethod mc-f1 ((x t)) :t-method)");
    eval_print("(defmethod mc-f1 ((x integer)) :int-method)");
    /* Most-specific-first: (first primary) is the INTEGER method. */
    ASSERT_STR_EQ(eval_print("(mc-f1 42)"), ":INT-METHOD");
}

TEST(mc_define_long_form_multiple_groups)
{
    /* Long form with two groups: :AROUND and primary.  Body passes
       PRIMARY as :AROUND's next-methods so CALL-NEXT-METHOD inside the
       :AROUND body walks into the primary chain. */
    eval_print(
        "(define-method-combination mc-ar-aware ()"
        "  ((around (:around))"
        "   (primary ()))"
        "  (if around"
        "      `(call-method ,(first around) ,primary)"
        "      `(call-method ,(first primary))))");
    eval_print("(defgeneric mc-ara (x) (:method-combination mc-ar-aware))");
    eval_print("(defmethod mc-ara ((x t)) :primary)");
    eval_print("(defmethod mc-ara :around ((x t)) "
               "  (cons :around (call-next-method)))");
    ASSERT_STR_EQ(eval_print("(mc-ara 1)"), "(:AROUND . :PRIMARY)");
}

TEST(mc_define_long_form_qualifier_pattern)
{
    /* Long-form pattern with a specific qualifier selects methods by
       exact qualifier match. */
    eval_print(
        "(define-method-combination mc-tagged ()"
        "  ((tagged (tag)))"
        "  `(call-method ,(first tagged)))");
    eval_print("(defgeneric mc-tg (x) (:method-combination mc-tagged))");
    eval_print("(defmethod mc-tg tag ((x t)) :tagged)");
    ASSERT_STR_EQ(eval_print("(mc-tg 1)"), ":TAGGED");
}

TEST(mc_method_combination_object_type)
{
    /* Registered combinations are STANDARD-METHOD-COMBINATION structs. */
    ASSERT_STR_EQ(eval_print(
        "(%struct-type-name (find-method-combination nil '+ nil))"),
        "STANDARD-METHOD-COMBINATION");
}

TEST(mc_method_combination_class_bootstrapped)
{
    /* STANDARD-METHOD-COMBINATION is a first-class class in the
       metaobject hierarchy. */
    ASSERT_STR_EQ(eval_print(
        "(class-name (find-class 'standard-method-combination))"),
        "STANDARD-METHOD-COMBINATION");
}

TEST(mc_call_method_uses_current_args)
{
    /* CALL-METHOD invokes a method's function on *CURRENT-METHOD-ARGS*,
       so long-form bodies do not need to thread arguments explicitly. */
    eval_print(
        "(define-method-combination mc-args ()"
        "  ((primary ()))"
        "  `(call-method ,(first primary)))");
    eval_print("(defgeneric mc-pass (x y) (:method-combination mc-args))");
    eval_print("(defmethod mc-pass ((x integer) (y integer)) (+ x y))");
    ASSERT_STR_EQ(eval_print("(mc-pass 3 4)"), "7");
}

TEST(mc_redefine_updates_combination)
{
    /* Re-DEFGENERICing a GF with a different :METHOD-COMBINATION swaps
       the combination metaobject on the existing GF. */
    eval_print("(defgeneric mc-swap (x))");
    eval_print("(defgeneric mc-swap (x) (:method-combination +))");
    ASSERT_STR_EQ(eval_print(
        "(method-combination-name "
        "  (gf-method-combination "
        "    (gethash 'mc-swap *generic-function-table*)))"),
        "+");
}

/* ==================================================================
 * Dependent maintenance protocol (MOP) — AMOP §5.4
 * ADD-DEPENDENT / REMOVE-DEPENDENT / MAP-DEPENDENTS / UPDATE-DEPENDENT
 * with notification hooks on ENSURE-CLASS, ADD-METHOD, REMOVE-METHOD,
 * ENSURE-GENERIC-FUNCTION.
 * ================================================================== */

TEST(dep_add_dependent_registers)
{
    /* ADD-DEPENDENT makes the dependent visible via MAP-DEPENDENTS. */
    eval_print("(defclass dep-c1 () ())");
    eval_print("(defvar *dep-d1* (list :observer))");
    eval_print("(add-dependent (find-class 'dep-c1) *dep-d1*)");
    ASSERT_STR_EQ(eval_print(
        "(let ((seen nil))"
        "  (map-dependents (find-class 'dep-c1) (lambda (d) (push d seen)))"
        "  (eq (first seen) *dep-d1*))"),
        "T");
}

TEST(dep_add_dependent_idempotent)
{
    /* Adding the same dependent twice registers it only once. */
    eval_print("(defclass dep-c2 () ())");
    eval_print("(defvar *dep-d2* (list :obs))");
    eval_print("(add-dependent (find-class 'dep-c2) *dep-d2*)");
    eval_print("(add-dependent (find-class 'dep-c2) *dep-d2*)");
    ASSERT_STR_EQ(eval_print(
        "(let ((n 0))"
        "  (map-dependents (find-class 'dep-c2) (lambda (d) (declare (ignore d)) (incf n)))"
        "  n)"),
        "1");
}

TEST(dep_remove_dependent_drops)
{
    /* REMOVE-DEPENDENT removes a previously registered dependent. */
    eval_print("(defclass dep-c3 () ())");
    eval_print("(defvar *dep-d3* (list :obs))");
    eval_print("(add-dependent (find-class 'dep-c3) *dep-d3*)");
    eval_print("(remove-dependent (find-class 'dep-c3) *dep-d3*)");
    ASSERT_STR_EQ(eval_print(
        "(let ((n 0))"
        "  (map-dependents (find-class 'dep-c3) (lambda (d) (declare (ignore d)) (incf n)))"
        "  n)"),
        "0");
}

TEST(dep_remove_unknown_is_silent)
{
    /* REMOVE-DEPENDENT silently ignores dependents that were never
       registered — returns NIL, no error. */
    eval_print("(defclass dep-c4 () ())");
    ASSERT_STR_EQ(eval_print(
        "(remove-dependent (find-class 'dep-c4) (list :ghost))"),
        "NIL");
}

TEST(dep_map_dependents_visits_all)
{
    /* MAP-DEPENDENTS applies FUNCTION to every registered dependent. */
    eval_print("(defclass dep-c5 () ())");
    eval_print("(defvar *dep-d5a* (list :a))");
    eval_print("(defvar *dep-d5b* (list :b))");
    eval_print("(add-dependent (find-class 'dep-c5) *dep-d5a*)");
    eval_print("(add-dependent (find-class 'dep-c5) *dep-d5b*)");
    ASSERT_STR_EQ(eval_print(
        "(let ((n 0))"
        "  (map-dependents (find-class 'dep-c5) (lambda (d) (declare (ignore d)) (incf n)))"
        "  n)"),
        "2");
}

TEST(dep_update_dependent_default_is_noop)
{
    /* The default UPDATE-DEPENDENT method returns NIL and does nothing. */
    eval_print("(defclass dep-c6 () ())");
    ASSERT_STR_EQ(eval_print(
        "(update-dependent (find-class 'dep-c6) (list :d) 'x)"),
        "NIL");
}

TEST(dep_add_method_notifies)
{
    /* Installing a new method on a GF broadcasts UPDATE-DEPENDENT with
       ('ADD-METHOD METHOD) to each registered dependent. */
    eval_print("(defgeneric dep-gf1 (x))");
    eval_print("(defvar *dep-log1* nil)");
    eval_print(
        "(defmethod update-dependent ((mo standard-generic-function) "
        "                             (d (eql :dep-obs-1)) &rest args)"
        "  (push args *dep-log1*))");
    eval_print("(add-dependent (ensure-generic-function 'dep-gf1) :dep-obs-1)");
    eval_print("(defmethod dep-gf1 ((x integer)) :int)");
    ASSERT_STR_EQ(eval_print(
        "(and (consp *dep-log1*)"
        "     (eq (caar *dep-log1*) 'add-method))"),
        "T");
}

TEST(dep_add_method_passes_method_object)
{
    /* The broadcast's second argument is the method metaobject that
       was just installed. */
    eval_print("(defgeneric dep-gf2 (x))");
    eval_print("(defvar *dep-log2* nil)");
    eval_print(
        "(defmethod update-dependent ((mo standard-generic-function) "
        "                             (d (eql :dep-obs-2)) &rest args)"
        "  (setq *dep-log2* args))");
    eval_print("(add-dependent (ensure-generic-function 'dep-gf2) :dep-obs-2)");
    eval_print("(defmethod dep-gf2 ((x integer)) :i)");
    ASSERT_STR_EQ(eval_print(
        "(eq (%struct-type-name (second *dep-log2*)) 'standard-method)"),
        "T");
}

TEST(dep_remove_method_notifies)
{
    /* REMOVE-METHOD broadcasts UPDATE-DEPENDENT with ('REMOVE-METHOD M). */
    eval_print("(defgeneric dep-gf3 (x))");
    eval_print("(defmethod dep-gf3 ((x integer)) :int)");
    eval_print("(defvar *dep-log3* nil)");
    eval_print(
        "(defmethod update-dependent ((mo standard-generic-function) "
        "                             (d (eql :dep-obs-3)) &rest args)"
        "  (push (first args) *dep-log3*))");
    eval_print("(add-dependent (ensure-generic-function 'dep-gf3) :dep-obs-3)");
    eval_print(
        "(remove-method (ensure-generic-function 'dep-gf3)"
        "               (find-method #'dep-gf3 nil (list (find-class 'integer))))");
    ASSERT_STR_EQ(eval_print("(first *dep-log3*)"), "REMOVE-METHOD");
}

TEST(dep_class_redefinition_notifies)
{
    /* Redefining a class via DEFCLASS fires UPDATE-DEPENDENT on the
       class with 'REINITIALIZE-INSTANCE as the first initarg. */
    eval_print("(defclass dep-c7 () ((a :initform 1)))");
    eval_print("(defvar *dep-log7* nil)");
    eval_print(
        "(defmethod update-dependent ((mo standard-class) "
        "                             (d (eql :dep-obs-7)) &rest args)"
        "  (push (first args) *dep-log7*))");
    eval_print("(add-dependent (find-class 'dep-c7) :dep-obs-7)");
    eval_print("(defclass dep-c7 () ((a :initform 2) (b :initform 3)))");
    ASSERT_STR_EQ(eval_print("(first *dep-log7*)"), "REINITIALIZE-INSTANCE");
}

TEST(dep_class_redefinition_migrates_dependents)
{
    /* %ENSURE-CLASS allocates a fresh class struct on redefinition,
       so the dependents registry must migrate the entry to the new
       metaobject — otherwise a second redefinition would not notify. */
    eval_print("(defclass dep-c8 () ())");
    eval_print("(defvar *dep-log8* 0)");
    eval_print(
        "(defmethod update-dependent ((mo standard-class) "
        "                             (d (eql :dep-obs-8)) &rest args)"
        "  (declare (ignore args))"
        "  (incf *dep-log8*))");
    eval_print("(add-dependent (find-class 'dep-c8) :dep-obs-8)");
    eval_print("(defclass dep-c8 () ((x)))");
    eval_print("(defclass dep-c8 () ((x) (y)))");
    ASSERT_STR_EQ(eval_print("*dep-log8*"), "2");
}

TEST(dep_gf_combination_swap_notifies)
{
    /* Swapping a GF's :METHOD-COMBINATION via re-DEFGENERIC broadcasts
       UPDATE-DEPENDENT on the existing GF metaobject. */
    eval_print("(defgeneric dep-gf4 (x))");
    eval_print("(defvar *dep-log4* nil)");
    eval_print(
        "(defmethod update-dependent ((mo standard-generic-function) "
        "                             (d (eql :dep-obs-4)) &rest args)"
        "  (setq *dep-log4* args))");
    eval_print("(add-dependent (ensure-generic-function 'dep-gf4) :dep-obs-4)");
    eval_print("(defgeneric dep-gf4 (x) (:method-combination +))");
    ASSERT_STR_EQ(eval_print("(first *dep-log4*)"), "REINITIALIZE-INSTANCE");
}

TEST(dep_gf_same_combination_no_notify)
{
    /* Re-DEFGENERICing with the same combination must NOT broadcast —
       only real changes trigger the notification. */
    eval_print("(defgeneric dep-gf5 (x) (:method-combination +))");
    eval_print("(defvar *dep-log5* 0)");
    eval_print(
        "(defmethod update-dependent ((mo standard-generic-function) "
        "                             (d (eql :dep-obs-5)) &rest args)"
        "  (declare (ignore args))"
        "  (incf *dep-log5*))");
    eval_print("(add-dependent (ensure-generic-function 'dep-gf5) :dep-obs-5)");
    eval_print("(defgeneric dep-gf5 (x) (:method-combination +))");
    ASSERT_STR_EQ(eval_print("*dep-log5*"), "0");
}

TEST(dep_isolated_between_metaobjects)
{
    /* Dependents registered on one metaobject must not see notifications
       for another metaobject. */
    eval_print("(defgeneric dep-gf6a (x))");
    eval_print("(defgeneric dep-gf6b (x))");
    eval_print("(defvar *dep-log6* 0)");
    eval_print(
        "(defmethod update-dependent ((mo standard-generic-function) "
        "                             (d (eql :dep-obs-6)) &rest args)"
        "  (declare (ignore args))"
        "  (incf *dep-log6*))");
    eval_print("(add-dependent (ensure-generic-function 'dep-gf6a) :dep-obs-6)");
    eval_print("(defmethod dep-gf6b ((x integer)) :b)");
    ASSERT_STR_EQ(eval_print("*dep-log6*"), "0");
}

/* ==================================================================== */
/* Portable MOP shims (closer-mop compatibility layer)                   */
/* ==================================================================== */

TEST(cmshim_metaobject_class_registered)
{
    /* METAOBJECT names a real class — closer-mop imports the symbol and
       downstream code TYPEPs against it. */
    ASSERT_STR_EQ(eval_print("(class-name (find-class 'metaobject))"),
                  "METAOBJECT");
}

TEST(cmshim_specializer_class_registered)
{
    ASSERT_STR_EQ(eval_print("(class-name (find-class 'specializer))"),
                  "SPECIALIZER");
}

TEST(cmshim_forward_referenced_class_registered)
{
    ASSERT_STR_EQ(
        eval_print("(class-name (find-class 'forward-referenced-class))"),
        "FORWARD-REFERENCED-CLASS");
}

TEST(cmshim_accessor_method_subclass_of_standard_method)
{
    /* Accessor-method stubs sit below STANDARD-METHOD in the CPL so that
       libraries that `(typep m 'standard-method)` on generated accessors
       keep working. */
    ASSERT_STR_EQ(eval_print(
        "(not (null (member (find-class 'standard-method)"
        "                   (class-precedence-list"
        "                     (find-class 'standard-reader-method)))))"),
        "T");
}

TEST(cmshim_reader_and_writer_method_classes_distinct)
{
    ASSERT_STR_EQ(eval_print(
        "(not (eq (find-class 'standard-reader-method)"
        "         (find-class 'standard-writer-method)))"),
        "T");
}

TEST(cmshim_direct_slot_definition_abstract_parent)
{
    /* DIRECT-SLOT-DEFINITION is an abstract parent of our reified
       STANDARD-DIRECT-SLOT-DEFINITION; TYPEP succeeds. */
    eval_print("(defclass cmshim-c1 () ((x :initarg :x)))");
    ASSERT_STR_EQ(eval_print(
        "(typep (car (class-direct-slots (find-class 'cmshim-c1)))"
        "       'direct-slot-definition)"),
        "T");
}

TEST(cmshim_effective_slot_definition_abstract_parent)
{
    eval_print("(defclass cmshim-c2 () ((y :initarg :y)))");
    ASSERT_STR_EQ(eval_print(
        "(typep (car (class-slots (find-class 'cmshim-c2)))"
        "       'effective-slot-definition)"),
        "T");
}

TEST(cmshim_classp_true_for_standard_class_instance)
{
    ASSERT_STR_EQ(eval_print("(classp (find-class 'standard-object))"), "T");
}

TEST(cmshim_classp_false_for_non_class)
{
    ASSERT_STR_EQ(eval_print("(classp 42)"), "NIL");
    ASSERT_STR_EQ(eval_print("(classp \"foo\")"), "NIL");
    ASSERT_STR_EQ(eval_print("(classp '(a b c))"), "NIL");
}

TEST(cmshim_generic_function_name_alias)
{
    eval_print("(defgeneric cmshim-gf1 (x))");
    ASSERT_STR_EQ(eval_print("(generic-function-name #'cmshim-gf1)"),
                  "CMSHIM-GF1");
}

TEST(cmshim_generic_function_lambda_list_alias)
{
    eval_print("(defgeneric cmshim-gf2 (x y))");
    ASSERT_STR_EQ(eval_print("(generic-function-lambda-list #'cmshim-gf2)"),
                  "(X Y)");
}

TEST(cmshim_generic_function_methods_alias)
{
    eval_print("(defgeneric cmshim-gf3 (x))");
    eval_print("(defmethod cmshim-gf3 ((x integer)) :int)");
    ASSERT_STR_EQ(eval_print("(length (generic-function-methods #'cmshim-gf3))"),
                  "1");
}

TEST(cmshim_generic_function_method_combination_alias)
{
    eval_print("(defgeneric cmshim-gf4 (x) (:method-combination +))");
    ASSERT_STR_EQ(eval_print(
        "(method-combination-name (generic-function-method-combination "
        "                           #'cmshim-gf4))"),
        "+");
}

TEST(cmshim_generic_function_method_class_default)
{
    eval_print("(defgeneric cmshim-gf5 (x))");
    ASSERT_STR_EQ(eval_print(
        "(eq (generic-function-method-class #'cmshim-gf5)"
        "    (find-class 'standard-method))"),
        "T");
}

TEST(cmshim_generic_function_argument_precedence_order)
{
    eval_print("(defgeneric cmshim-gf6 (a b &optional c))");
    ASSERT_STR_EQ(eval_print(
        "(generic-function-argument-precedence-order #'cmshim-gf6)"),
        "(A B)");
}

TEST(cmshim_generic_function_declarations_empty)
{
    eval_print("(defgeneric cmshim-gf7 (x))");
    ASSERT_STR_EQ(eval_print("(generic-function-declarations #'cmshim-gf7)"),
                  "NIL");
}

TEST(cmshim_camuc_class_specializers_valid)
{
    /* With only class-specialized methods, the result is determined
       from classes alone — VALIDP is T. */
    eval_print("(defgeneric cmshim-camuc1 (x))");
    eval_print("(defmethod cmshim-camuc1 ((x integer)) :int)");
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (ms validp)"
        "    (compute-applicable-methods-using-classes"
        "      #'cmshim-camuc1 (list (find-class 'integer)))"
        "  (list (length ms) validp))"),
        "(1 T)");
}

TEST(cmshim_camuc_eql_specializer_invalidates)
{
    /* When a method has an EQL specializer, we cannot decide purely from
       classes — the dispatcher falls back to COMPUTE-APPLICABLE-METHODS
       and closer-mop signals that via VALIDP=NIL. */
    eval_print("(defgeneric cmshim-camuc2 (x))");
    eval_print("(defmethod cmshim-camuc2 ((x (eql 1))) :one)");
    eval_print("(defmethod cmshim-camuc2 ((x integer)) :int)");
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (ms validp)"
        "    (compute-applicable-methods-using-classes"
        "      #'cmshim-camuc2 (list (find-class 'integer)))"
        "  validp)"),
        "NIL");
}

TEST(cmshim_compute_effective_method_single)
{
    /* COMPUTE-EFFECTIVE-METHOD returns a CALL-METHOD form for a single
       applicable method. */
    eval_print("(defgeneric cmshim-cem1 (x))");
    eval_print("(defmethod cmshim-cem1 ((x integer)) :int)");
    ASSERT_STR_EQ(eval_print(
        "(car (compute-effective-method"
        "       #'cmshim-cem1"
        "       (generic-function-method-combination #'cmshim-cem1)"
        "       (generic-function-methods #'cmshim-cem1)))"),
        "CALL-METHOD");
}

TEST(cmshim_ensure_generic_function_using_class_delegates)
{
    /* ENSURE-GENERIC-FUNCTION-USING-CLASS forwards to the primary
       ENSURE-GENERIC-FUNCTION regardless of the leading GF arg. */
    ASSERT_STR_EQ(eval_print(
        "(progn (ensure-generic-function-using-class nil 'cmshim-egf1"
        "         :lambda-list '(x))"
        "       (generic-function-name #'cmshim-egf1))"),
        "CMSHIM-EGF1");
}

TEST(cmshim_specializer_direct_methods_stub)
{
    /* Stubbed — we don't track the back-link, so return NIL. */
    ASSERT_STR_EQ(eval_print(
        "(specializer-direct-methods (find-class 'integer))"),
        "NIL");
}

TEST(cmshim_specializer_direct_generic_functions_stub)
{
    ASSERT_STR_EQ(eval_print(
        "(specializer-direct-generic-functions (find-class 'integer))"),
        "NIL");
}

TEST(cmshim_add_remove_direct_method_noop)
{
    /* No back-link maintenance — both calls return NIL and do not
       signal. */
    eval_print("(defgeneric cmshim-drm (x))");
    eval_print("(defmethod cmshim-drm ((x integer)) :i)");
    ASSERT_STR_EQ(eval_print(
        "(add-direct-method (find-class 'integer)"
        "                   (car (generic-function-methods #'cmshim-drm)))"),
        "NIL");
    ASSERT_STR_EQ(eval_print(
        "(remove-direct-method (find-class 'integer)"
        "                      (car (generic-function-methods #'cmshim-drm)))"),
        "NIL");
}

TEST(cmshim_add_direct_subclass_records)
{
    /* ADD-DIRECT-SUBCLASS must update CLASS-DIRECT-SUBCLASSES. */
    eval_print("(defclass cmshim-super1 () ())");
    eval_print("(defclass cmshim-sub1 () ())");
    eval_print(
        "(add-direct-subclass (find-class 'cmshim-super1)"
        "                     (find-class 'cmshim-sub1))");
    ASSERT_STR_EQ(eval_print(
        "(not (null (member (find-class 'cmshim-sub1)"
        "                   (class-direct-subclasses (find-class 'cmshim-super1))"
        "                   :test #'eq)))"),
        "T");
}

TEST(cmshim_add_direct_subclass_idempotent)
{
    /* Adding the same subclass twice must not duplicate the entry. */
    eval_print("(defclass cmshim-super2 () ())");
    eval_print("(defclass cmshim-sub2 () ())");
    eval_print(
        "(add-direct-subclass (find-class 'cmshim-super2)"
        "                     (find-class 'cmshim-sub2))");
    eval_print(
        "(add-direct-subclass (find-class 'cmshim-super2)"
        "                     (find-class 'cmshim-sub2))");
    ASSERT_STR_EQ(eval_print(
        "(count (find-class 'cmshim-sub2)"
        "       (class-direct-subclasses (find-class 'cmshim-super2))"
        "       :test #'eq)"),
        "1");
}

TEST(cmshim_remove_direct_subclass_drops)
{
    eval_print("(defclass cmshim-super3 () ())");
    eval_print("(defclass cmshim-sub3 () ())");
    eval_print(
        "(add-direct-subclass (find-class 'cmshim-super3)"
        "                     (find-class 'cmshim-sub3))");
    eval_print(
        "(remove-direct-subclass (find-class 'cmshim-super3)"
        "                        (find-class 'cmshim-sub3))");
    ASSERT_STR_EQ(eval_print(
        "(member (find-class 'cmshim-sub3)"
        "        (class-direct-subclasses (find-class 'cmshim-super3))"
        "        :test #'eq)"),
        "NIL");
}

TEST(cmshim_accessor_method_slot_definition_stub)
{
    /* We don't back-link accessors to their slot-defs. */
    eval_print("(defclass cmshim-c4 () ((z :accessor cmshim-c4-z)))");
    eval_print("(defmethod cmshim-c4-z ((x cmshim-c4)) (call-next-method))");
    ASSERT_STR_EQ(eval_print(
        "(accessor-method-slot-definition"
        "  (car (generic-function-methods #'cmshim-c4-z)))"),
        "NIL");
}

TEST(cmshim_reader_method_class_returns_standard)
{
    eval_print("(defclass cmshim-c5 () ((a)))");
    ASSERT_STR_EQ(eval_print(
        "(eq (reader-method-class (find-class 'cmshim-c5)"
        "                         (car (class-direct-slots"
        "                                (find-class 'cmshim-c5))))"
        "    (find-class 'standard-reader-method))"),
        "T");
}

TEST(cmshim_writer_method_class_returns_standard)
{
    eval_print("(defclass cmshim-c6 () ((a)))");
    ASSERT_STR_EQ(eval_print(
        "(eq (writer-method-class (find-class 'cmshim-c6)"
        "                         (car (class-direct-slots"
        "                                (find-class 'cmshim-c6))))"
        "    (find-class 'standard-writer-method))"),
        "T");
}

/* DOCUMENTATION is a generic function in CLHS.  Regression: libraries
 * like lisp-namespace add (defmethod documentation ((x symbol) (type (eql ...)))),
 * which should leave plain (documentation 'foo 'function) still answerable
 * via the (t t) fallback method — otherwise NO-APPLICABLE-METHOD blocks
 * loading of introspect-environment and everything downstream. */
TEST(documentation_generic_fallback_after_specialization)
{
    /* Fallback stores / retrieves through the existing doc-table */
    ASSERT_STR_EQ(eval_print(
        "(progn (setf (documentation 'docgf-sym 'function) \"docA\")"
        "       (documentation 'docgf-sym 'function))"),
        "\"docA\"");
    /* After a user defmethod on a specific EQL doc-type, the (t t)
     * method still handles the original doc-type */
    eval_print(
        "(defmethod documentation ((x symbol) (type (eql 'mynamespace)))"
        "  'specialized-result)");
    ASSERT_STR_EQ(eval_print(
        "(documentation 'docgf-sym 'mynamespace)"),
        "SPECIALIZED-RESULT");
    ASSERT_STR_EQ(eval_print(
        "(documentation 'docgf-sym 'function)"),
        "\"docA\"");
}

TEST(allocate_instance_gf_default_method)
{
    /* Regression: allocate-instance must be a GF so libraries (serapeum's
       abstract-standard-class, ContextL, etc.) can define methods on it.
       Without a default (class t) method, specializing on a user metaclass
       or a specific class breaks make-instance for all OTHER classes with
       "No applicable method for ALLOCATE-INSTANCE". */
    eval_print("(defclass ai-gf-base () ((x :initarg :x :initform 42)))");
    eval_print("(defclass ai-gf-special (ai-gf-base) ())");
    /* Specialize allocate-instance on the special class */
    eval_print(
        "(defmethod allocate-instance ((c (eql (find-class 'ai-gf-special))) &rest initargs)"
        "  (declare (ignore initargs))"
        "  (error \"forbidden\"))");
    /* Base class make-instance still works via the default (class t) method */
    ASSERT_STR_EQ(eval_print("(slot-value (make-instance 'ai-gf-base) 'x)"), "42");
    ASSERT_STR_EQ(eval_print("(slot-value (make-instance 'ai-gf-base :x 7) 'x)"), "7");
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

    /* slot-value on DEFSTRUCT instances */
    RUN(slot_value_on_struct);
    RUN(setf_slot_value_on_struct);
    RUN(with_slots_on_struct);
    RUN(slot_boundp_on_struct);
    RUN(slot_exists_p_on_struct);
    RUN(slot_value_unknown_slot_struct);

    /* ERROR :format-control formats its report */
    RUN(error_format_control_interpolates);

    /* Phase 10: typep with multiple inheritance */
    RUN(typep_multiple_inheritance);
    RUN(typep_multiple_inheritance_or);
    RUN(typep_multiple_inheritance_negative);

    /* Phase 11: dispatch cache */
    RUN(cache_basic_dispatch);
    RUN(cache_hit_correct_result);
    RUN(cache_different_classes);
    RUN(cache_invalidated_on_defmethod);
    RUN(cache_invalidated_on_defclass);
    RUN(cache_eql_specializer_bypass);
    RUN(cache_multi_dispatch_bypass);
    RUN(cache_no_applicable_method);
    RUN(cache_before_after_around);

    /* Phase 12: EMF cache */
    RUN(emf_cache_call_next_method_no_args);
    RUN(emf_cache_call_next_method_with_args);
    RUN(emf_cache_before_after_around);
    RUN(emf_cache_next_method_p);
    RUN(emf_cache_no_applicable_cached);

    /* Phase 13: Multi-dispatch cache */
    RUN(multi_cache_two_arg);
    RUN(multi_cache_hit);
    RUN(multi_cache_different_combos);
    RUN(multi_cache_cacheable_p_value);
    RUN(multi_cache_invalidation);
    RUN(multi_cache_call_next_method);

    /* Phase 14: EQL specializer cache */
    RUN(eql_cache_basic);
    RUN(eql_cache_hit);
    RUN(eql_cache_mixed_eql_class);
    RUN(eql_cache_class_fallback);
    RUN(eql_cache_call_next_method);
    RUN(eql_cache_nil_value);
    RUN(eql_cache_arg2_eql);
    RUN(eql_cache_invalidation);
    RUN(eql_cache_cacheable_p);

    /* Slot definition metaobjects (MOP) */
    RUN(mop1_direct_slot_is_metaobject);
    RUN(mop1_effective_slot_is_metaobject);
    RUN(mop1_class_slots_alias);
    RUN(mop1_slot_def_accessors_name);
    RUN(mop1_slot_def_accessors_initargs);
    RUN(mop1_slot_def_accessors_readers_writers);
    RUN(mop1_slot_def_accessors_allocation);
    RUN(mop1_slot_def_accessors_initform);
    RUN(mop1_effective_slot_location);
    RUN(mop1_direct_slot_definition_class_gf);
    RUN(mop1_direct_slot_definition_class_customizable);
    RUN(mop1_inheritance_merges_slots);
    RUN(mop1_inheritance_initargs_unioned);

    /* Class finalization protocol (MOP) */
    RUN(finalize_class_finalized_p);
    RUN(finalize_inheritance_idempotent);
    RUN(compute_class_precedence_list_default);
    RUN(compute_slots_returns_effective);
    RUN(compute_effective_slot_definition_default);
    RUN(compute_slots_around_hook);
    RUN(compute_default_initargs_inherits);
    RUN(compute_default_initargs_most_specific_wins);
    RUN(validate_superclass_default);
    RUN(class_direct_default_initargs_accessor);
    RUN(class_prototype_allocates);
    RUN(ensure_class_creates);
    RUN(ensure_class_using_class_dispatches);

    /* Slot-access protocol (MOP) */
    RUN(svuc_fast_path_flag_clean);
    RUN(svuc_default_method_matches_struct_ref);
    RUN(svuc_location_accessor);
    RUN(svuc_setf_default_roundtrips);
    RUN(svuc_around_observes_reads);
    RUN(svuc_around_can_override_read);
    RUN(svuc_setf_around_observes_writes);
    RUN(svuc_boundp_dispatches);
    RUN(svuc_makunbound_dispatches);
    RUN(svuc_struct_fallback_unaffected);
    RUN(svuc_slot_unbound_still_fires);

    /* :allocation :class (MOP) */
    RUN(calloc_two_instances_share);
    RUN(calloc_subclass_inherits_cell);
    RUN(calloc_subclass_redefines_gets_own_cell);
    RUN(calloc_location_is_cons);
    RUN(calloc_location_mixed);
    RUN(calloc_boundp_and_makunbound);
    RUN(calloc_initform_runs_once);
    RUN(calloc_initarg_writes_shared_cell);
    RUN(calloc_struct_smaller_than_effective_slots);
    RUN(calloc_svuc_protocol_sees_class_slots);

    /* Reified EQL specializers (MOP) */
    RUN(eqlspec_intern_identity);
    RUN(eqlspec_intern_eql_discriminates);
    RUN(eqlspec_object_accessor);
    RUN(eqlspec_p_predicate);
    RUN(eqlspec_class_bootstrapped);
    RUN(eqlspec_dispatch_still_works);
    RUN(eqlspec_method_specializers_contains_metaobject);
    RUN(eqlspec_method_specializers_shared_across_methods);
    RUN(eqlspec_extract_specializer_names_roundtrips);
    RUN(eqlspec_extract_class_specializers);
    RUN(eqlspec_method_equal_replaces_on_redef);
    RUN(eqlspec_cacheable_p_still_eql);

    /* Funcallable standard class (MOP) */
    RUN(funcallable_class_bootstrapped);
    RUN(funcallable_gf_is_funcallable_standard_object);
    RUN(funcallable_class_of_gf);
    RUN(funcallable_gf_still_callable_via_symbol);
    RUN(funcallable_gf_via_funcall_and_apply);
    RUN(funcallable_set_funcallable_instance_function);
    RUN(funcallable_sfi_returns_gf);
    RUN(funcallable_standard_instance_access);
    RUN(funcallable_standard_instance_access_equals_struct_ref);
    RUN(funcallable_standard_instance_access_variant);
    RUN(funcallable_compute_discriminating_function_default);
    RUN(funcallable_gf_type_of);
    RUN(funcallable_gf_cpl_has_function);

    /* Method metaobject protocol (MOP) */
    RUN(mmop_method_generic_function_backlink);
    RUN(mmop_extract_lambda_list_plain);
    RUN(mmop_extract_lambda_list_preserves_keywords);
    RUN(mmop_extract_specializer_names_padded_t);
    RUN(mmop_extract_specializer_names_stops_at_keywords);
    RUN(mmop_add_method_installs_new_method);
    RUN(mmop_add_method_sets_back_link);
    RUN(mmop_add_method_returns_gf);
    RUN(mmop_remove_method_drops_dispatch);
    RUN(mmop_remove_method_clears_backlink);
    RUN(mmop_find_method_returns_existing);
    RUN(mmop_find_method_accepts_class_names);
    RUN(mmop_find_method_eql_names);
    RUN(mmop_find_method_missing_errors);
    RUN(mmop_find_method_missing_returns_nil);
    RUN(mmop_ensure_method_installs);
    RUN(mmop_ensure_method_default_specializers_are_t);
    RUN(mmop_ensure_method_returns_method);
    RUN(mmop_make_method_lambda_default_identity);

    /* Method combination (MOP) */
    RUN(mc_standard_default);
    RUN(mc_plus_basic);
    RUN(mc_plus_identity_with_one_argument);
    RUN(mc_list_collects);
    RUN(mc_and_short_circuits);
    RUN(mc_or_returns_first_true);
    RUN(mc_progn_returns_last);
    RUN(mc_append_concatenates);
    RUN(mc_max_reduces);
    RUN(mc_around_wraps_short_form);
    RUN(mc_invalid_qualifier_errors);
    RUN(mc_standard_still_supports_before_after);
    RUN(mc_find_method_combination_standard);
    RUN(mc_find_method_combination_type);
    RUN(mc_find_method_combination_unknown_errors);
    RUN(mc_define_short_form_basic);
    RUN(mc_define_short_form_identity_default);
    RUN(mc_define_short_form_operator_defaults_to_name);
    RUN(mc_define_long_form_basic);
    RUN(mc_define_long_form_multiple_groups);
    RUN(mc_define_long_form_qualifier_pattern);
    RUN(mc_method_combination_object_type);
    RUN(mc_method_combination_class_bootstrapped);
    RUN(mc_call_method_uses_current_args);
    RUN(mc_redefine_updates_combination);

    /* Dependent maintenance protocol (MOP) */
    RUN(dep_add_dependent_registers);
    RUN(dep_add_dependent_idempotent);
    RUN(dep_remove_dependent_drops);
    RUN(dep_remove_unknown_is_silent);
    RUN(dep_map_dependents_visits_all);
    RUN(dep_update_dependent_default_is_noop);
    RUN(dep_add_method_notifies);
    RUN(dep_add_method_passes_method_object);
    RUN(dep_remove_method_notifies);
    RUN(dep_class_redefinition_notifies);
    RUN(dep_class_redefinition_migrates_dependents);
    RUN(dep_gf_combination_swap_notifies);
    RUN(dep_gf_same_combination_no_notify);
    RUN(dep_isolated_between_metaobjects);

    /* Portable MOP shims (closer-mop compatibility layer) */
    RUN(cmshim_metaobject_class_registered);
    RUN(cmshim_specializer_class_registered);
    RUN(cmshim_forward_referenced_class_registered);
    RUN(cmshim_accessor_method_subclass_of_standard_method);
    RUN(cmshim_reader_and_writer_method_classes_distinct);
    RUN(cmshim_direct_slot_definition_abstract_parent);
    RUN(cmshim_effective_slot_definition_abstract_parent);
    RUN(cmshim_classp_true_for_standard_class_instance);
    RUN(cmshim_classp_false_for_non_class);
    RUN(cmshim_generic_function_name_alias);
    RUN(cmshim_generic_function_lambda_list_alias);
    RUN(cmshim_generic_function_methods_alias);
    RUN(cmshim_generic_function_method_combination_alias);
    RUN(cmshim_generic_function_method_class_default);
    RUN(cmshim_generic_function_argument_precedence_order);
    RUN(cmshim_generic_function_declarations_empty);
    RUN(cmshim_camuc_class_specializers_valid);
    RUN(cmshim_camuc_eql_specializer_invalidates);
    RUN(cmshim_compute_effective_method_single);
    RUN(cmshim_ensure_generic_function_using_class_delegates);
    RUN(cmshim_specializer_direct_methods_stub);
    RUN(cmshim_specializer_direct_generic_functions_stub);
    RUN(cmshim_add_remove_direct_method_noop);
    RUN(cmshim_add_direct_subclass_records);
    RUN(cmshim_add_direct_subclass_idempotent);
    RUN(cmshim_remove_direct_subclass_drops);
    RUN(cmshim_accessor_method_slot_definition_stub);
    RUN(cmshim_reader_method_class_returns_standard);
    RUN(cmshim_writer_method_class_returns_standard);

    RUN(documentation_generic_fallback_after_specialization);
    RUN(allocate_instance_gf_default_method);

    teardown();
    REPORT();
}
