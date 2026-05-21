/* Tests for EXT:BACKTRACE and EXT:FRAME-LOCALS — the native primitives behind
 * the Sly/SLYNK SLDB (debugger) backend.
 *
 *   EXT:BACKTRACE      -> list of (INDEX NAME FILE LINE), innermost first.
 *   EXT:FRAME-LOCALS N -> list of (PLACEHOLDER-NAME . VALUE) for frame N.
 *
 * Both walk the error-time frame window captured in cl_debug_base_fp (set by
 * cl_capture_backtrace) so a *debugger-hook* sees the error-time stack rather
 * than its own pushed frames; outside an error they report the live stack.
 *
 * Note: each call must be NON-tail, otherwise the VM's tail-call optimization
 * collapses the intermediate frames (correct, but uninteresting here) — so the
 * helper functions wrap the inner call in a LET initform. */

#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "core/reader.h"
#include "core/printer.h"
#include "core/compiler.h"
#include "core/thread.h"
#include "core/vm.h"
#include "core/builtins.h"
#include "core/debugger.h"
#include "core/repl.h"
#include "platform/platform.h"
#include <string.h>

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
    cl_debugger_init();   /* interns *DEBUGGER-HOOK* as special */
    cl_repl_init();
}

static void teardown(void)
{
    cl_current_source_file = NULL;
    cl_mem_shutdown();
    platform_shutdown();
}

static int truthy(const char *expr)
{
    return cl_eval_string(expr) != CL_NIL;
}

/* Evaluate FORM as if read from FILE (so frames carry file/line). */
static void eval_in_file(const char *file, const char *form)
{
    const char *prev = cl_current_source_file;
    cl_current_source_file = cl_intern_source_file(file);
    cl_eval_string(form);
    cl_current_source_file = prev;
}

/* --- EXT:BACKTRACE structure (live frames) --- */

TEST(backtrace_live_structure)
{
    /* g1 -> g2 -> g3, each non-tail; g3 returns the backtrace. */
    cl_eval_string(
        "(progn"
        "  (defun g3 (z) (declare (ignore z)) (ext:backtrace))"
        "  (defun g2 (y) (let ((r (g3 (* y 2)))) r))"
        "  (defun g1 (x) (let ((r (g2 (+ x 1)))) r))"
        "  (defparameter *bt* (g1 5)))");

    /* At least g3, g2, g1 (plus the anonymous top-level form). */
    ASSERT(truthy("(>= (length *bt*) 3)"));
    /* Each entry is (index name file line). */
    ASSERT(truthy("(integerp (first (first *bt*)))"));
    ASSERT(truthy("(eql (first (first *bt*)) 0)"));
    /* Innermost-first ordering with the right names. */
    ASSERT(truthy("(string= (symbol-name (second (first *bt*))) \"G3\")"));
    ASSERT(truthy("(string= (symbol-name (second (second *bt*))) \"G2\")"));
    ASSERT(truthy("(string= (symbol-name (second (third *bt*))) \"G1\")"));
}

TEST(backtrace_source_file_line)
{
    /* Compiled "from a file" -> frames carry namestring + line. */
    eval_in_file("bt-src.lisp",
        "(progn"
        "  (defun s3 () (ext:backtrace))"
        "  (defun s2 () (let ((r (s3))) r))"
        "  (defparameter *bts* (s2)))");

    ASSERT(truthy("(stringp (third (first *bts*)))"));
    ASSERT(truthy("(string= (third (first *bts*)) \"bt-src.lisp\")"));
    ASSERT(truthy("(integerp (fourth (first *bts*)))"));
    ASSERT(truthy("(> (fourth (first *bts*)) 0)"));
}

/* --- EXT:FRAME-LOCALS --- */

TEST(frame_locals_values)
{
    cl_eval_string(
        "(progn"
        "  (defun lf (a b) (let ((c 99)) (ext:frame-locals 0)))"
        "  (defparameter *lcl* (lf 1 2)))");

    ASSERT(truthy("(consp *lcl*)"));
    /* The argument and let-bound values are all present. */
    ASSERT(truthy("(member 1 (mapcar #'cdr *lcl*))"));
    ASSERT(truthy("(member 2 (mapcar #'cdr *lcl*))"));
    ASSERT(truthy("(member 99 (mapcar #'cdr *lcl*))"));
    /* Placeholder names are uninterned symbols (no home package). */
    ASSERT(truthy("(symbolp (car (first *lcl*)))"));
    ASSERT(truthy("(null (symbol-package (car (first *lcl*))))"));
}

TEST(frame_locals_out_of_range)
{
    ASSERT(truthy("(eq (ext:frame-locals 9999) :not-available)"));
    ASSERT(truthy("(eq (ext:frame-locals -1) :not-available)"));
}

/* --- The real SLDB scenario: capture from a *debugger-hook* on error --- */

TEST(backtrace_from_debugger_hook)
{
    int err;

    cl_eval_string("(defparameter *hbt* :none)");
    cl_eval_string("(defparameter *hlocals* :none)");
    cl_eval_string("(defun boom (q) (let ((w (* q q))) (error \"x ~A\" w)))");
    cl_eval_string("(defun mid (p) (let ((r (boom p))) r))");

    /* The error is UNHANDLED (no handler-case), so *debugger-hook* fires
     * before the unwind.  Catch the ensuing unwind in this C frame. */
    CL_CATCH(err);
    if (err == CL_ERR_NONE) {
        cl_eval_string(
            "(let ((*debugger-hook*"
            "        (lambda (c h) (declare (ignore c h))"
            "          (setf *hbt* (ext:backtrace))"
            "          (setf *hlocals* (ext:frame-locals 0)))))"
            "  (mid 7))");
        CL_UNCATCH();
    } else {
        CL_UNCATCH();
    }

    /* Error-time backtrace: innermost frame is BOOM (where ERROR was called),
     * captured even though the hook pushed its own frames on top. */
    ASSERT(truthy("(consp *hbt*)"));
    ASSERT(truthy("(string= (symbol-name (second (first *hbt*))) \"BOOM\")"));
    ASSERT(truthy("(string= (symbol-name (second (second *hbt*))) \"MID\")"));

    /* Error-time locals of BOOM: q=7 and w=q*q=49 survive into the hook. */
    ASSERT(truthy("(consp *hlocals*)"));
    ASSERT(truthy("(member 7 (mapcar #'cdr *hlocals*))"));
    ASSERT(truthy("(member 49 (mapcar #'cdr *hlocals*))"));
}

int main(void)
{
    test_init();
    setup();

    RUN(backtrace_live_structure);
    RUN(backtrace_source_file_line);
    RUN(frame_locals_values);
    RUN(frame_locals_out_of_range);
    RUN(backtrace_from_debugger_hook);

    teardown();
    REPORT();
}
