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

/* Reader-GF fast dispatch lives in its own test image on purpose.
 *
 * A GF whose whole method set is DEFCLASS-generated readers for one slot is
 * "promoted": its inline cache holds (TYPE-NAME . SLOT-INDEX) and OP_CALL
 * answers the call directly, without unwrapping to the discriminating
 * function.  Promotion requires *SLOT-ACCESS-PROTOCOL-EXTENDED-P* to be NIL.
 *
 * That flag is a ONE-WAY LATCH: defining any SLOT-VALUE-USING-CLASS method
 * sets it for the life of the image, and REMOVE-METHOD does not clear it.
 * test_clos.c defines such a method partway through, so every reader GF in
 * that image is permanently demoted and these tests would pass vacuously on
 * the slow path.  Hence a separate binary.
 *
 * CLAMIGA:*READER-GFS* records promotion, so each test below asserts the fast
 * path is genuinely engaged before exercising it.
 */

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

TEST(reader_gf_promoted_and_reads)
{
    eval_print("(defclass rg-p () ((x :initform 1 :reader rg-px)))");
    ASSERT_STR_EQ(eval_print("(and (gethash #'rg-px clamiga:*reader-gfs*) t)"), "T");
    ASSERT_STR_EQ(eval_print("(rg-px (make-instance 'rg-p))"), "1");
}

TEST(reader_gf_polymorphic_differing_slot_indices)
{
    /* Same reader on two classes where X sits at a DIFFERENT index.  A cache
     * keyed on a captured index rather than the receiver's type would return
     * the wrong slot for one of them. */
    eval_print("(defclass rg-q () ((pad :initform 0) (x :initform 2 :reader rg-px)))");
    ASSERT_STR_EQ(eval_print("(rg-px (make-instance 'rg-q))"), "2");
    ASSERT_STR_EQ(eval_print("(rg-px (make-instance 'rg-p))"), "1");
    ASSERT_STR_EQ(eval_print(
        "(let ((a (make-instance 'rg-p)) (b (make-instance 'rg-q)) (acc nil))"
        "  (dotimes (i 3) (push (rg-px a) acc) (push (rg-px b) acc))"
        "  (nreverse acc))"), "(1 2 1 2 1 2)");
}

TEST(reader_gf_inherited_by_subclass)
{
    eval_print("(defclass rg-r (rg-p) ((extra :initform 9)))");
    ASSERT_STR_EQ(eval_print("(rg-px (make-instance 'rg-r))"), "1");
}

TEST(reader_gf_nil_value_is_not_unbound)
{
    /* The VM path has no caller-supplied MISS sentinel; a slot holding NIL
     * must not be mistaken for the unbound marker. */
    eval_print("(defclass rg-niln () ((x :initform nil :reader rg-nilx)))");
    ASSERT_STR_EQ(eval_print("(and (gethash #'rg-nilx clamiga:*reader-gfs*) t)"), "T");
    ASSERT_STR_EQ(eval_print("(rg-nilx (make-instance 'rg-niln))"), "NIL");
    ASSERT_STR_EQ(eval_print("(rg-nilx (make-instance 'rg-niln))"), "NIL");
}

TEST(reader_gf_unbound_slot_signals)
{
    /* An unbound slot's storage holds the marker, which reads back as a cache
     * miss and routes to the SLOT-UNBOUND protocol.  It must signal every
     * time, and never leak the marker as a value. */
    eval_print("(defclass rg-u () ((x :reader rg-ux)))");
    ASSERT_STR_EQ(eval_print("(and (gethash #'rg-ux clamiga:*reader-gfs*) t)"), "T");
    ASSERT_STR_EQ(eval_print(
        "(handler-case (rg-ux (make-instance 'rg-u)) (error () :signaled))"), ":SIGNALED");
    ASSERT_STR_EQ(eval_print(
        "(handler-case (rg-ux (make-instance 'rg-u)) (error () :signaled))"), ":SIGNALED");
    /* ... and reads normally once bound. */
    ASSERT_STR_EQ(eval_print(
        "(let ((o (make-instance 'rg-u)))"
        "  (setf (slot-value o 'x) :bound) (rg-ux o))"), ":BOUND");
}

TEST(reader_gf_no_applicable_method)
{
    eval_print("(defclass rg-unrelated () ((x :initform 5)))");
    ASSERT_STR_EQ(eval_print(
        "(handler-case (rg-px (make-instance 'rg-unrelated)) (error () :nam))"), ":NAM");
    ASSERT_STR_EQ(eval_print("(rg-px (make-instance 'rg-p))"), "1");
}

TEST(reader_gf_around_method_demotes)
{
    eval_print("(defclass rg-s () ((x :initform 10 :reader rg-sx)))");
    ASSERT_STR_EQ(eval_print("(rg-sx (make-instance 'rg-s))"), "10");
    eval_print("(defmethod rg-sx :around ((o rg-s)) (* 100 (call-next-method)))");
    ASSERT_STR_EQ(eval_print("(and (gethash #'rg-sx clamiga:*reader-gfs*) t)"), "NIL");
    ASSERT_STR_EQ(eval_print("(rg-sx (make-instance 'rg-s))"), "1000");
}

TEST(reader_gf_foreign_primary_demotes)
{
    eval_print("(defclass rg-t1 () ((x :initform 7 :reader rg-tx)))");
    ASSERT_STR_EQ(eval_print("(rg-tx (make-instance 'rg-t1))"), "7");
    eval_print("(defclass rg-t2 () ())");
    eval_print("(defmethod rg-tx ((o rg-t2)) :from-t2)");
    ASSERT_STR_EQ(eval_print("(and (gethash #'rg-tx clamiga:*reader-gfs*) t)"), "NIL");
    ASSERT_STR_EQ(eval_print("(rg-tx (make-instance 'rg-t2))"), ":FROM-T2");
    ASSERT_STR_EQ(eval_print("(rg-tx (make-instance 'rg-t1))"), "7");
}

TEST(reader_gf_accessor_writer_roundtrip)
{
    eval_print("(defclass rg-w () ((x :initform 1 :accessor rg-wx)))");
    ASSERT_STR_EQ(eval_print(
        "(let ((o (make-instance 'rg-w)))"
        "  (list (rg-wx o) (progn (setf (rg-wx o) 42) (rg-wx o))))"), "(1 42)");
}

TEST(reader_gf_class_redefinition_reorders_slots)
{
    /* The cache stores a slot INDEX.  Redefining the class so the slot moves
     * must invalidate it, or the reader returns the wrong slot. */
    eval_print("(defclass rg-redef () ((a :initform :one :reader rg-redef-a)))");
    ASSERT_STR_EQ(eval_print("(rg-redef-a (make-instance 'rg-redef))"), ":ONE");
    eval_print("(defclass rg-redef () ((pad :initform :pad)"
               "                       (a :initform :two :reader rg-redef-a)))");
    ASSERT_STR_EQ(eval_print("(rg-redef-a (make-instance 'rg-redef))"), ":TWO");
}

TEST(reader_gf_tailcall_funcall_and_apply_paths)
{
    /* OP_TAILCALL is excluded from the VM fast path (it must unwind the
     * caller's frame); FUNCALL / APPLY route through slot 3.  All three must
     * still produce the right value. */
    eval_print("(defclass rg-tc () ((x :initform :val :reader rg-tcx)))");
    eval_print("(defun rg-tail-read (o) (rg-tcx o))");
    ASSERT_STR_EQ(eval_print("(rg-tail-read (make-instance 'rg-tc))"), ":VAL");
    ASSERT_STR_EQ(eval_print("(funcall #'rg-tcx (make-instance 'rg-tc))"), ":VAL");
    ASSERT_STR_EQ(eval_print("(apply #'rg-tcx (list (make-instance 'rg-tc)))"), ":VAL");
}

TEST(reader_gf_set_funcallable_instance_function_wins)
{
    /* OP_CALL bypasses slot 3 for a promoted reader GF, so retargeting slot 3
     * must first demote it -- otherwise FN would never run. */
    eval_print("(defclass rg-sfi () ((x :initform :orig :reader rg-sfix)))");
    ASSERT_STR_EQ(eval_print("(rg-sfix (make-instance 'rg-sfi))"), ":ORIG");
    eval_print("(set-funcallable-instance-function"
               "  #'rg-sfix (lambda (o) (declare (ignore o)) :hijacked))");
    ASSERT_STR_EQ(eval_print("(and (gethash #'rg-sfix clamiga:*reader-gfs*) t)"), "NIL");
    ASSERT_STR_EQ(eval_print("(rg-sfix (make-instance 'rg-sfi))"), ":HIJACKED");
    ASSERT_STR_EQ(eval_print("(rg-sfix (make-instance 'rg-sfi))"), ":HIJACKED");
    /* an unrelated reader keeps its fast path */
    ASSERT_STR_EQ(eval_print("(rg-px (make-instance 'rg-p))"), "1");
}

/* MUST BE THE LAST reader-GF test, and effectively the last test in this
 * file: defining a SLOT-VALUE-USING-CLASS method latches
 * *SLOT-ACCESS-PROTOCOL-EXTENDED-P* for the remainder of the image, demoting
 * every reader GF permanently. */
TEST(reader_gf_slot_value_using_class_demotes_all)
{
    ASSERT_STR_EQ(eval_print("clamiga:*slot-access-protocol-extended-p*"), "NIL");
    eval_print("(defclass rg-tracked () ((x :initform 3 :reader rg-tkx)))");
    ASSERT_STR_EQ(eval_print("(rg-tkx (make-instance 'rg-tracked))"), "3");
    eval_print("(defmethod slot-value-using-class :around"
               "    ((c standard-class) obj slotd)"
               "  (let ((v (call-next-method)))"
               "    (if (numberp v) (* 1000 v) v)))");
    ASSERT_STR_EQ(eval_print("clamiga:*slot-access-protocol-extended-p*"), "T");
    ASSERT_STR_EQ(eval_print("(and (gethash #'rg-tkx clamiga:*reader-gfs*) t)"), "NIL");
    ASSERT_STR_EQ(eval_print("(and (gethash #'rg-px clamiga:*reader-gfs*) t)"), "NIL");
    /* the protocol is now honoured by readers, not bypassed */
    ASSERT_STR_EQ(eval_print("(rg-tkx (make-instance 'rg-tracked))"), "3000");
    ASSERT_STR_EQ(eval_print("(rg-px (make-instance 'rg-p))"), "1000");
}

int main(void)
{
    test_init();
    setup();

    /* Order matters: reader_gf_slot_value_using_class_demotes_all latches
     * *SLOT-ACCESS-PROTOCOL-EXTENDED-P* for the rest of the image, demoting
     * every reader GF.  It must run last. */
    RUN(reader_gf_promoted_and_reads);
    RUN(reader_gf_polymorphic_differing_slot_indices);
    RUN(reader_gf_inherited_by_subclass);
    RUN(reader_gf_nil_value_is_not_unbound);
    RUN(reader_gf_unbound_slot_signals);
    RUN(reader_gf_no_applicable_method);
    RUN(reader_gf_around_method_demotes);
    RUN(reader_gf_foreign_primary_demotes);
    RUN(reader_gf_accessor_writer_roundtrip);
    RUN(reader_gf_class_redefinition_reorders_slots);
    RUN(reader_gf_tailcall_funcall_and_apply_paths);
    RUN(reader_gf_set_funcallable_instance_function_wins);
    RUN(reader_gf_slot_value_using_class_demotes_all);

    teardown();
    REPORT();
}
