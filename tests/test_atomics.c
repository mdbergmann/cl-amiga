/*
 * test_atomics.c — MP:COMPARE-AND-SWAP (alias MP:CAS), MP:ATOMIC-INCF and
 * MP:ATOMIC-DECF: the implementation-provided compare-and-swap that library
 * backends (Shinmera's atomics, bordeaux-threads v2) map onto.
 *
 * Contract under test (see the docstrings in lib/boot.lisp):
 *   - CAS returns the value the place held when the comparison was made —
 *     EQ to OLD exactly when the swap happened (SBCL's CAS convention).
 *   - Places: (car c) (cdr c) (first c) (rest c), (svref v i) / (aref v i) on a
 *     simple-vector, (symbol-value s), a special variable (thread-local
 *     binding if one is in effect), (slot-value o 's), defstruct accessors,
 *     and macro / symbol-macro / (the ...) wrappers of those.
 *   - Subforms of the place are evaluated once, left to right, before OLD and
 *     NEW (and before DELTA for ATOMIC-INCF).
 *   - ATOMIC-INCF/DECF return the new value; non-fixnum operands or a
 *     non-fixnum result signal and leave the place unchanged.
 *   - Atomicity across threads: concurrent ATOMIC-INCF loses no update,
 *     a CAS-retry push loop loses no element.
 *   - The cell primitives keep working on cells the generational collector
 *     has promoted to (read-protected) old space.
 */

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
#include "core/thread.h"
#include "platform/platform.h"
#include "platform/platform_thread.h"

#include <string.h>
#include <stdio.h>

static void setup(void)
{
    platform_init();
    cl_thread_init();
    cl_error_init();
    cl_mem_init(8 * 1024 * 1024);
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
    cl_vm_shutdown();
    cl_mem_shutdown();
    cl_thread_shutdown();
    platform_shutdown();
}

/* Eval a Lisp string; return its printed result, or "ERROR:<code>". */
static const char *eval_print(const char *str)
{
    static char buf[4096];
    int err;

    CL_CATCH(err);
    if (err == CL_ERR_NONE) {
        CL_Obj result = cl_eval_string(str);
        cl_prin1_to_string(result, buf, sizeof(buf));
        CL_UNCATCH();
        return buf;
    } else {
        CL_UNCATCH();
        cl_vm.sp = 0;
        cl_vm.fp = 0;
        snprintf(buf, sizeof(buf), "ERROR:%d", err);
        return buf;
    }
}

/* ================================================================
 * Cons cells
 * ================================================================ */

TEST(cas_car_swaps_and_returns_previous)
{
    eval_print("(defvar *ac* (cons nil nil))");
    /* success: returns OLD (EQ) and the cell changes */
    ASSERT_STR_EQ(eval_print("(mp:compare-and-swap (car *ac*) nil t)"), "NIL");
    ASSERT_STR_EQ(eval_print("(car *ac*)"), "T");
    /* failure: returns the current content, cell untouched */
    ASSERT_STR_EQ(eval_print("(mp:compare-and-swap (car *ac*) nil :x)"), "T");
    ASSERT_STR_EQ(eval_print("(car *ac*)"), "T");
}

TEST(cas_cdr_first_rest_and_alias)
{
    eval_print("(defvar *ac2* (cons 1 2))");
    ASSERT_STR_EQ(eval_print("(mp:cas (cdr *ac2*) 2 3)"), "2");
    ASSERT_STR_EQ(eval_print("(mp:cas (first *ac2*) 1 10)"), "1");
    ASSERT_STR_EQ(eval_print("(mp:cas (rest *ac2*) 3 30)"), "3");
    ASSERT_STR_EQ(eval_print("*ac2*"), "(10 . 30)");
    /* EQ, not EQL: a fresh bignum/float is never EQ */
    ASSERT_STR_EQ(eval_print("(let ((c (cons 1.5 nil))) (mp:cas (car c) 1.5 2) (car c))"), "1.5");
}

TEST(cas_non_cons_signals_type_error)
{
    ASSERT_STR_EQ(eval_print(
        "(handler-case (let ((x 5)) (mp:cas (car x) nil t))"
        "  (type-error () :type-error))"), ":TYPE-ERROR");
}

/* ================================================================
 * Simple vectors
 * ================================================================ */

TEST(cas_svref_and_aref)
{
    eval_print("(defvar *av* (make-array 3 :initial-element nil))");
    ASSERT_STR_EQ(eval_print("(mp:cas (svref *av* 1) nil :a)"), "NIL");
    ASSERT_STR_EQ(eval_print("(mp:cas (aref *av* 1) :a :b)"), ":A");
    ASSERT_STR_EQ(eval_print("(mp:cas (aref *av* 1) :a :c)"), ":B");
    ASSERT_STR_EQ(eval_print("*av*"), "#(NIL :B NIL)");
}

TEST(cas_svref_rejects_bad_index_and_non_simple_vector)
{
    ASSERT_STR_EQ(eval_print(
        "(handler-case (mp:cas (svref *av* 3) nil t) (error () :error))"), ":ERROR");
    ASSERT_STR_EQ(eval_print(
        "(handler-case (mp:cas (svref *av* -1) nil t) (error () :error))"), ":ERROR");
    ASSERT_STR_EQ(eval_print(
        "(handler-case (let ((v (make-array 2 :fill-pointer 2 :initial-element nil)))"
        "                (mp:cas (aref v 0) nil t))"
        "  (type-error () :type-error))"), ":TYPE-ERROR");
    ASSERT_STR_EQ(eval_print(
        "(handler-case (mp:cas (svref \"abc\" 0) #\\a #\\b)"
        "  (type-error () :type-error))"), ":TYPE-ERROR");
}

/* ================================================================
 * Symbol values and special variables
 * ================================================================ */

TEST(cas_symbol_value_global)
{
    eval_print("(defvar *asv* :root)");
    ASSERT_STR_EQ(eval_print("(mp:cas (symbol-value '*asv*) :root :new)"), ":ROOT");
    ASSERT_STR_EQ(eval_print("*asv*"), ":NEW");
    ASSERT_STR_EQ(eval_print("(mp:cas (symbol-value '*asv*) :root :other)"), ":NEW");
    ASSERT_STR_EQ(eval_print("*asv*"), ":NEW");
}

TEST(cas_symbol_value_uninterned)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((s (make-symbol \"S\")))"
        "  (setf (symbol-value s) 1)"
        "  (list (mp:cas (symbol-value s) 1 2) (symbol-value s)))"), "(1 2)");
}

/* A special variable place follows the thread's dynamic binding: the CAS
 * hits the LET binding and the global cell is untouched (SBCL semantics,
 * atomics' cas-special test). */
TEST(cas_special_targets_dynamic_binding)
{
    eval_print("(defvar *asp* :root)");
    ASSERT_STR_EQ(eval_print(
        "(let ((*asp* nil)) (list (mp:cas *asp* nil t) *asp*))"), "(NIL T)");
    ASSERT_STR_EQ(eval_print("*asp*"), ":ROOT");
    ASSERT_STR_EQ(eval_print(
        "(let ((*asp* nil)) (mp:cas (symbol-value '*asp*) nil 1) *asp*)"), "1");
    ASSERT_STR_EQ(eval_print("*asp*"), ":ROOT");
    /* a worker thread's binding is its own */
    ASSERT_STR_EQ(eval_print(
        "(let ((r (mp:join-thread (mp:make-thread"
        "           (lambda () (let ((*asp* 5)) (mp:cas *asp* 5 6) *asp*))))))"
        "  (list r *asp*))"), "(6 :ROOT)");
}

TEST(cas_symbol_value_unbound_and_constant_signal)
{
    ASSERT_STR_EQ(eval_print(
        "(handler-case (mp:cas (symbol-value (make-symbol \"U\")) nil t)"
        "  (unbound-variable () :unbound))"), ":UNBOUND");
    ASSERT_STR_EQ(eval_print(
        "(handler-case (mp:cas (symbol-value 'pi) nil t) (error () :error))"), ":ERROR");
    ASSERT_STR_EQ(eval_print("(handler-case (mp:cas (symbol-value nil) nil t) (error () :error))"), ":ERROR");
    ASSERT_STR_EQ(eval_print(
        "(handler-case (mp:cas (symbol-value 42) nil t) (type-error () :type-error))"), ":TYPE-ERROR");
}

/* ================================================================
 * Structures and CLOS instances
 * ================================================================ */

TEST(cas_defstruct_accessor)
{
    eval_print("(defstruct atpt (x nil) (n 0))");
    eval_print("(defvar *apt* (make-atpt))");
    ASSERT_STR_EQ(eval_print("(mp:cas (atpt-x *apt*) nil :y)"), "NIL");
    ASSERT_STR_EQ(eval_print("(mp:cas (atpt-n *apt*) 0 5)"), "0");
    ASSERT_STR_EQ(eval_print("(mp:cas (atpt-n *apt*) 0 7)"), "5");
    ASSERT_STR_EQ(eval_print("(list (atpt-x *apt*) (atpt-n *apt*))"), "(:Y 5)");
    /* (slot-value struct 'slot) reaches the same cell */
    ASSERT_STR_EQ(eval_print("(mp:cas (slot-value *apt* 'n) 5 6)"), "5");
    ASSERT_STR_EQ(eval_print("(atpt-n *apt*)"), "6");
    /* wrong struct type for the accessor's cell primitive */
    ASSERT_STR_EQ(eval_print(
        "(handler-case (mp:cas (atpt-n 3) 0 1) (type-error () :type-error))"), ":TYPE-ERROR");
}

TEST(cas_slot_value_standard_object)
{
    eval_print("(defclass atck () ((s :initform nil) (u) (cs :allocation :class :initform 0)))");
    eval_print("(defvar *ack* (make-instance 'atck))");
    ASSERT_STR_EQ(eval_print("(mp:cas (slot-value *ack* 's) nil t)"), "NIL");
    ASSERT_STR_EQ(eval_print("(slot-value *ack* 's)"), "T");
    ASSERT_STR_EQ(eval_print("(mp:cas (slot-value *ack* 's) nil :no)"), "T");
    /* :class slot — shared cell, visible through another instance */
    ASSERT_STR_EQ(eval_print("(mp:cas (slot-value *ack* 'cs) 0 1)"), "0");
    ASSERT_STR_EQ(eval_print("(slot-value (make-instance 'atck) 'cs)"), "1");
    /* unbound slot goes through SLOT-UNBOUND; unknown slot is an error */
    ASSERT_STR_EQ(eval_print(
        "(handler-case (mp:cas (slot-value *ack* 'u) nil t) (error () :error))"), ":ERROR");
    ASSERT_STR_EQ(eval_print("(slot-boundp *ack* 'u)"), "NIL");
    ASSERT_STR_EQ(eval_print(
        "(handler-case (mp:cas (slot-value *ack* 'zz) nil t) (error () :error))"), ":ERROR");
    ASSERT_STR_EQ(eval_print(
        "(handler-case (mp:cas (slot-value 42 's) nil t) (error () :error))"), ":ERROR");
}

/* ================================================================
 * Place analysis
 * ================================================================ */

TEST(cas_through_macro_the_and_symbol_macro)
{
    eval_print("(defmacro at-my-place (c) `(car ,c))");
    ASSERT_STR_EQ(eval_print(
        "(let ((c (cons 1 2)))"
        "  (list (mp:cas (at-my-place c) 1 9)"
        "        (mp:cas (the fixnum (cdr c)) 2 8)"
        "        (symbol-macrolet ((h (car c))) (mp:cas h 9 10))"
        "        c))"), "(1 2 9 (10 . 8))");
}

TEST(cas_evaluates_subforms_once_in_order)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((log '()) (c (cons :a nil)) (v (vector 0)))"
        "  (mp:cas (car (progn (push 1 log) c)) (progn (push 2 log) :a) (progn (push 3 log) :b))"
        "  (mp:cas (svref (progn (push 4 log) v) (progn (push 5 log) 0)) 0 1)"
        "  (mp:atomic-incf (svref (progn (push 6 log) v) (progn (push 7 log) 0))"
        "                  (progn (push 8 log) 2))"
        "  (list (reverse log) (car c) (svref v 0)))"), "((1 2 3 4 5 6 7 8) :B 3)");
}

TEST(cas_unsupported_place_is_a_macroexpansion_error)
{
    ASSERT_STR_EQ(eval_print(
        "(handler-case (macroexpand '(mp:cas (gethash 1 h) nil t)) (error () :error))"), ":ERROR");
    ASSERT_STR_EQ(eval_print(
        "(handler-case (macroexpand '(mp:cas (car) nil t)) (error () :error))"), ":ERROR");
    ASSERT_STR_EQ(eval_print(
        "(handler-case (macroexpand '(mp:cas 42 nil t)) (error () :error))"), ":ERROR");
    /* the message names the offending place and lists the supported ones */
    ASSERT_STR_EQ(eval_print(
        "(handler-case (macroexpand '(mp:cas (gethash 1 h) nil t))"
        "  (error (e) (let ((m (princ-to-string e)))"
        "               (and (search \"(GETHASH 1 H)\" m) (search \"SVREF\" m) t))))"), "T");
}

/* A bare symbol place only has a shared cell to CAS when it names a
 * SPECIAL variable — an ordinary LET-bound lexical has none, and must be
 * rejected at macroexpansion time rather than silently CASing whatever
 * unrelated global happens to share its name (mp::%atomic-place).  EVAL
 * (like MACROEXPAND above) is a runtime function call, so its compile-time
 * macroexpansion error is signalled inside the HANDLER-CASE's dynamic
 * extent and so is catchable, unlike writing the erroring form directly
 * in the body (which would fail to compile the whole toplevel form). */
TEST(cas_bare_lexical_symbol_is_a_macroexpansion_error)
{
    ASSERT_STR_EQ(eval_print(
        "(handler-case (eval '(let ((x 5)) (mp:cas x 5 6))) (error () :error))"), ":ERROR");
    ASSERT_STR_EQ(eval_print(
        "(handler-case (eval '(let ((x 5)) (mp:atomic-incf x))) (error () :error))"), ":ERROR");
    /* the message names the offending symbol */
    ASSERT_STR_EQ(eval_print(
        "(handler-case (macroexpand '(mp:cas zzz-not-special 1 2))"
        "  (error (e) (let ((m (princ-to-string e)))"
        "               (and (search \"ZZZ-NOT-SPECIAL\" m) (search \"SPECIAL\" m) t))))"), "T");
    /* a symbol proclaimed special still works */
    ASSERT_STR_EQ(eval_print(
        "(progn (defvar *cas-bare* 1) (mp:cas *cas-bare* 1 2))"), "1");
}

/* ================================================================
 * ATOMIC-INCF / ATOMIC-DECF
 * ================================================================ */

TEST(atomic_incf_decf_return_new_value)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((c (cons 0 nil)))"
        "  (list (mp:atomic-incf (car c)) (mp:atomic-incf (car c) 5)"
        "        (mp:atomic-decf (car c)) (mp:atomic-decf (car c) 2) (car c)))"), "(1 6 5 3 3)");
    ASSERT_STR_EQ(eval_print(
        "(let ((v (vector 10)) (p (make-atpt :n 1)))"
        "  (defvar *acnt* 0)"
        "  (list (mp:atomic-incf (svref v 0) -3) (mp:atomic-incf (atpt-n p) 41)"
        "        (mp:atomic-incf *acnt*) (mp:atomic-decf (symbol-value '*acnt*) 3)))"), "(7 42 1 -2)");
}

TEST(atomic_incf_fixnum_only_and_place_unchanged_on_error)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((c (cons 1.5 nil)))"
        "  (list (handler-case (mp:atomic-incf (car c)) (type-error () :type-error)) (car c)))"),
        "(:TYPE-ERROR 1.5)");
    ASSERT_STR_EQ(eval_print(
        "(let ((c (cons 1 nil)))"
        "  (list (handler-case (mp:atomic-incf (car c) 1.5) (type-error () :type-error)) (car c)))"),
        "(:TYPE-ERROR 1)");
    ASSERT_STR_EQ(eval_print(
        "(let ((c (cons most-positive-fixnum nil)))"
        "  (list (handler-case (mp:atomic-incf (car c)) (error () :overflow))"
        "        (eql (car c) most-positive-fixnum)))"), "(:OVERFLOW T)");
    ASSERT_STR_EQ(eval_print(
        "(let ((c (cons most-negative-fixnum nil)))"
        "  (list (handler-case (mp:atomic-decf (car c)) (error () :overflow))"
        "        (eql (car c) most-negative-fixnum)))"), "(:OVERFLOW T)");
    /* the largest legal step in each direction */
    ASSERT_STR_EQ(eval_print(
        "(let ((c (cons 0 nil)))"
        "  (mp:atomic-incf (car c) most-positive-fixnum)"
        "  (mp:atomic-decf (car c) most-positive-fixnum)"
        "  (mp:atomic-decf (car c) (- most-negative-fixnum))"
        "  (eql (car c) most-negative-fixnum))"), "T");
}

/* ================================================================
 * Threads: no lost updates
 * ================================================================ */

TEST(atomic_incf_concurrent_loses_no_update)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((c (cons 0 nil)) (v (vector 0)))"
        "  (defvar *amt* 0)"
        "  (let ((ths (loop repeat 4 collect"
        "               (mp:make-thread (lambda ()"
        "                 (dotimes (i 2000)"
        "                   (mp:atomic-incf (car c))"
        "                   (mp:atomic-incf (svref v 0) 2)"
        "                   (mp:atomic-decf *amt*)))))))"
        "    (mapc #'mp:join-thread ths))"
        "  (list (car c) (svref v 0) *amt*))"), "(8000 16000 -8000)");
}

TEST(cas_retry_push_concurrent_loses_no_element)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((head (list nil)))"
        "  (let ((ths (loop for k below 4 collect"
        "               (let ((k k))"
        "                 (mp:make-thread (lambda ()"
        "                   (dotimes (i 1000)"
        "                     (loop (let* ((old (car head))"
        "                                  (new (cons (+ (* k 1000) i) old)))"
        "                             (when (eq old (mp:cas (car head) old new))"
        "                               (return)))))))))))"
        "    (mapc #'mp:join-thread ths))"
        "  (list (length (car head)) (reduce #'+ (car head))))"), "(4000 7998000)");
}

/* ================================================================
 * GC interaction
 * ================================================================ */

/* Cells promoted to old space are read-protected by the generational
 * collector on the host; the CAS instruction must take the write-watch
 * fault and complete like an ordinary store. */
TEST(cas_on_promoted_old_space_cells)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((c (cons :a nil)) (v (vector :a)) (p (make-atpt :x :a)))"
        "  (gc) (gc) (gc)"
        "  (list (mp:cas (car c) :a :b) (mp:cas (svref v 0) :a :b) (mp:cas (atpt-x p) :a :b)"
        "        (car c) (svref v 0) (atpt-x p)))"), "(:A :A :A :B :B :B)");
}

/* The stored NEW may be a young object placed into an old cell — the
 * collector must keep it alive across further collections. */
TEST(cas_stores_young_into_old_survives_gc)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((c (cons nil nil)))"
        "  (gc) (gc)"
        "  (mp:cas (car c) nil (list 1 2 3))"
        "  (gc) (gc)"
        "  (car c))"), "(1 2 3)");
}

/* ================================================================
 * Runner
 * ================================================================ */

int main(void)
{
    setup();

    RUN(cas_car_swaps_and_returns_previous);
    RUN(cas_cdr_first_rest_and_alias);
    RUN(cas_non_cons_signals_type_error);

    RUN(cas_svref_and_aref);
    RUN(cas_svref_rejects_bad_index_and_non_simple_vector);

    RUN(cas_symbol_value_global);
    RUN(cas_symbol_value_uninterned);
    RUN(cas_special_targets_dynamic_binding);
    RUN(cas_symbol_value_unbound_and_constant_signal);

    RUN(cas_defstruct_accessor);
    RUN(cas_slot_value_standard_object);

    RUN(cas_through_macro_the_and_symbol_macro);
    RUN(cas_evaluates_subforms_once_in_order);
    RUN(cas_unsupported_place_is_a_macroexpansion_error);
    RUN(cas_bare_lexical_symbol_is_a_macroexpansion_error);

    RUN(atomic_incf_decf_return_new_value);
    RUN(atomic_incf_fixnum_only_and_place_unchanged_on_error);

    RUN(atomic_incf_concurrent_loses_no_update);
    RUN(cas_retry_push_concurrent_loses_no_element);

    RUN(cas_on_promoted_old_space_cells);
    RUN(cas_stores_young_into_old_survives_gc);

    teardown();

    REPORT();
}
