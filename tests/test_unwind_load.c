/* Regression tests for two non-local-exit bugs that only show up when an
 * UNWIND-PROTECT encloses code the runtime recovers from internally.  Both
 * were found building the ARexx development port (lib/dev-commands.lisp),
 * whose diagnostic capture is exactly this shape: WITH-OUTPUT-TO-STRING
 * (an UNWIND-PROTECT) wrapped around a LOAD that must keep going after a
 * bad top-level form.
 *
 * Bug 1 -- cl_error_unwind scanned the WHOLE NLX stack for an interposing
 * UNWIND-PROTECT, including frames established OUTSIDE the innermost C
 * error frame.  An error inside LOAD therefore longjmp'd to an enclosing
 * unwind-protect, jumping clean over bi_load's own per-top-level-form
 * CL_CATCH: (unwind-protect (load "f") ...) lost LOAD's error recovery
 * entirely, so the first bad form aborted the rest of the file.  Fixed by
 * flooring the scan at CL_ErrorFrame.saved_nlx_top.
 *
 * Bug 2 -- a THROW out of an unwind-protect cleanup that is running an
 * error unwind reached its catch without clearing cl_pending_throw, which
 * still held that error (pending_throw == 2).  The throw appeared to
 * succeed, and then the next enclosing OP_UWRETHROW resurrected the
 * abandoned error.  CLHS 5.2 says a non-local exit from a cleanup clause
 * abandons the original transfer.  Fixed by clearing the pending state at
 * the CATCH / BLOCK / TAGBODY landings, which are the TARGET of a
 * transfer (the UWPROT landing is a pass-through and must not clear).
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
#include "core/stream.h"
#include "core/repl.h"
#include "platform/platform.h"
#include <stdio.h>
#include <string.h>

#define UNWIND_LOAD_TMP "/tmp/cl_test_unwind_load.lisp"

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
    cl_stream_init();
    cl_repl_init();
}

static void teardown(void)
{
    cl_stream_shutdown();
    cl_current_source_file = NULL;
    cl_mem_shutdown();
    platform_shutdown();
}

static const char *eval_str(const char *expr)
{
    static char buf[4096];
    int err;

    CL_CATCH(err);
    if (err == CL_ERR_NONE) {
        CL_Obj result = cl_eval_string(expr);
        cl_prin1_to_string(result, buf, sizeof(buf));
        CL_UNCATCH();
        return buf;
    } else {
        CL_UNCATCH();
        snprintf(buf, sizeof(buf), "ERROR:%d", err);
        return buf;
    }
}

/* Three top-level forms; the middle one errors.  A LOAD that recovers per
 * form runs all three and leaves *M* == (:TWO :ONE); one that aborts at the
 * first error leaves (:ONE). */
static int write_tmp_lisp(void)
{
    FILE *f = fopen(UNWIND_LOAD_TMP, "w");
    if (!f) return 0;
    fprintf(f, "(push :one cl-user::*unwind-load-marks*)\n");
    fprintf(f, "(error \"deliberate mid-file error\")\n");
    fprintf(f, "(push :two cl-user::*unwind-load-marks*)\n");
    fclose(f);
    return 1;
}

/* --- Bug 1: LOAD's per-form recovery survives an enclosing UNWIND-PROTECT */

TEST(load_recovers_per_form_with_no_enclosing_unwind_protect)
{
    /* Baseline: this always worked, and pins the expected marker list. */
    ASSERT(write_tmp_lisp());
    ASSERT(strcmp(eval_str("(defvar cl-user::*unwind-load-marks* nil)"),
                  "ERROR:1") != 0);
    eval_str("(setq cl-user::*unwind-load-marks* nil)");
    eval_str("(load \"" UNWIND_LOAD_TMP "\")");
    ASSERT_STR_EQ(eval_str("(reverse cl-user::*unwind-load-marks*)"),
                  "(:ONE :TWO)");
}

TEST(load_recovers_per_form_inside_unwind_protect)
{
    /* Bug 1: with the unwind-protect, the error used to longjmp straight to
     * the cleanup and :TWO was never pushed. */
    ASSERT(write_tmp_lisp());
    eval_str("(setq cl-user::*unwind-load-marks* nil)");
    eval_str("(unwind-protect (load \"" UNWIND_LOAD_TMP "\") nil)");
    ASSERT_STR_EQ(eval_str("(reverse cl-user::*unwind-load-marks*)"),
                  "(:ONE :TWO)");
}

TEST(load_recovers_per_form_inside_with_output_to_string)
{
    /* The shape the diagnostic capture actually uses: WITH-OUTPUT-TO-STRING
     * expands to an UNWIND-PROTECT, so this is bug 1 via the real caller. */
    ASSERT(write_tmp_lisp());
    eval_str("(setq cl-user::*unwind-load-marks* nil)");
    eval_str("(with-output-to-string (s)"
             "  (let ((*standard-output* s) (*error-output* s))"
             "    (load \"" UNWIND_LOAD_TMP "\")))");
    ASSERT_STR_EQ(eval_str("(reverse cl-user::*unwind-load-marks*)"),
                  "(:ONE :TWO)");
}

TEST(enclosing_unwind_protect_cleanup_still_runs_after_load)
{
    /* The floor must not cost the enclosing cleanup its run: LOAD returns
     * normally (it recovered), so the cleanup fires on the way out. */
    ASSERT(write_tmp_lisp());
    eval_str("(setq cl-user::*unwind-load-marks* nil)");
    eval_str("(unwind-protect (load \"" UNWIND_LOAD_TMP "\")"
             "  (push :cleanup cl-user::*unwind-load-marks*))");
    ASSERT_STR_EQ(eval_str("(reverse cl-user::*unwind-load-marks*)"),
                  "(:ONE :TWO :CLEANUP)");
}

TEST(error_outside_load_still_reaches_enclosing_unwind_protect)
{
    /* The counter-check for the floor: an unwind-protect that really IS the
     * innermost interposing frame must still get its cleanup run when the
     * error unwinds past it. */
    eval_str("(setq cl-user::*unwind-load-marks* nil)");
    eval_str("(handler-case"
             "    (unwind-protect (error \"plain\")"
             "      (push :cleanup cl-user::*unwind-load-marks*))"
             "  (error () nil))");
    ASSERT_STR_EQ(eval_str("cl-user::*unwind-load-marks*"), "(:CLEANUP)");
}

/* --- Bug 2: THROW from a cleanup abandons the error being unwound ------- */

/* GUARD returns (values RESULT NIL) normally and (values NIL T) when a
 * condition unwound out of THUNK -- the escape guard used by the ARexx
 * command layer, which cannot use HANDLER-CASE without pre-empting LOAD's
 * per-form recovery (bug 1's territory). */
#define GUARD_DEFN                                              \
    "(defun cl-user::guard (tag thunk)"                         \
    "  (let ((escaped t) (result nil))"                         \
    "    (catch tag"                                            \
    "      (unwind-protect"                                     \
    "           (progn (setf result (funcall thunk))"           \
    "                  (setf escaped nil))"                     \
    "        (when escaped (throw tag nil))))"                  \
    "    (values result escaped)))"

TEST(throw_from_cleanup_catches_the_error)
{
    eval_str(GUARD_DEFN);
    ASSERT_STR_EQ(
        eval_str("(multiple-value-list"
                 "  (cl-user::guard 'a (lambda () (error \"x\"))))"),
        "(NIL T)");
}

TEST(throw_from_cleanup_does_not_resurrect_the_error)
{
    /* Bug 2: the throw landed in the catch with cl_pending_throw still set
     * to the error, and the ENCLOSING unwind-protect's UWRETHROW re-raised
     * it -- so this form errored instead of returning. */
    eval_str(GUARD_DEFN);
    ASSERT_STR_EQ(
        eval_str("(multiple-value-list"
                 "  (unwind-protect"
                 "       (cl-user::guard 'a (lambda () (error \"x\")))"
                 "    nil))"),
        "(NIL T)");
}

TEST(throw_from_cleanup_nested_guards)
{
    /* Same bug through two levels: the inner guard's throw must not leave
     * pending state for the outer guard's unwind-protect to re-raise. */
    eval_str(GUARD_DEFN);
    ASSERT_STR_EQ(
        eval_str("(multiple-value-list"
                 "  (cl-user::guard 'outer"
                 "    (lambda ()"
                 "      (multiple-value-list"
                 "        (cl-user::guard 'inner (lambda () (error \"x\")))))))"),
        "((NIL T) NIL)");
}

TEST(throw_from_cleanup_inside_with_output_to_string)
{
    /* WITH-OUTPUT-TO-STRING is the enclosing unwind-protect in the real
     * caller; the guard must survive being nested inside it. */
    eval_str(GUARD_DEFN);
    ASSERT_STR_EQ(
        eval_str("(let ((r nil))"
                 "  (with-output-to-string (s)"
                 "    (declare (ignorable s))"
                 "    (setf r (multiple-value-list"
                 "              (cl-user::guard 'a (lambda () (error \"x\"))))))"
                 "  r)"),
        "(NIL T)");
}

TEST(guard_passes_values_through_when_nothing_escapes)
{
    eval_str(GUARD_DEFN);
    ASSERT_STR_EQ(
        eval_str("(multiple-value-list"
                 "  (cl-user::guard 'a (lambda () :fine)))"),
        "(:FINE NIL)");
}

TEST(pending_error_does_not_leak_into_a_later_form)
{
    /* The abandoned error must be gone for good, not merely deferred: a
     * subsequent unwind-protect that exits normally must not re-raise it. */
    eval_str(GUARD_DEFN);
    eval_str("(cl-user::guard 'a (lambda () (error \"x\")))");
    ASSERT_STR_EQ(eval_str("(unwind-protect :later nil)"), ":LATER");
}

int main(void)
{
    test_init();
    setup();

    RUN(load_recovers_per_form_with_no_enclosing_unwind_protect);
    RUN(load_recovers_per_form_inside_unwind_protect);
    RUN(load_recovers_per_form_inside_with_output_to_string);
    RUN(enclosing_unwind_protect_cleanup_still_runs_after_load);
    RUN(error_outside_load_still_reaches_enclosing_unwind_protect);

    RUN(throw_from_cleanup_catches_the_error);
    RUN(throw_from_cleanup_does_not_resurrect_the_error);
    RUN(throw_from_cleanup_nested_guards);
    RUN(throw_from_cleanup_inside_with_output_to_string);
    RUN(guard_passes_values_through_when_nothing_escapes);
    RUN(pending_error_does_not_leak_into_a_later_form);

    teardown();
    remove(UNWIND_LOAD_TMP);
    REPORT();
}
