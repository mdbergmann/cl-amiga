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
#include <stddef.h>
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

/* --- Regression: a long, deep backtrace must not overflow cl_backtrace_buf --- *
 *
 * cl_capture_backtrace() formats up to ~20 frames into the fixed 2048-byte
 * per-thread backtrace_buf, which sits immediately before c_stack_base in the
 * thread struct.  snprintf returns the length it WOULD have written, so a naive
 * `pos += snprintf(...)` can push pos past the buffer; the next snprintf then
 * computes a NEGATIVE (CL_BACKTRACE_BUF_SIZE - pos), which wraps to a huge
 * size_t and writes "  ... N more frames\n" unbounded past the buffer — smashing
 * c_stack_base.  A corrupted c_stack_base makes every later cl_check_c_stack
 * compute a garbage "used" value and signal a bogus "C stack overflow", which is
 * exactly what broke the ASDF source-registry walk (deep frames, long file
 * paths) under `make test-extra`.
 *
 * This test builds a deep chain of distinct frames with very long source-file
 * names, signals an error (cl_error captures the backtrace), and then scans the
 * memory immediately AFTER the buffer for the overflow's signature text
 * ("more frames", emitted only by the tail snprintf).  That text can never
 * legitimately live in the struct fields past backtrace_buf, so finding it is
 * unambiguous proof of an out-of-bounds write.  It also checks c_stack_base is
 * intact and that ordinary evaluation afterward does not spuriously overflow. */
TEST(backtrace_long_frames_no_buffer_overflow)
{
    CL_Thread *t = cl_get_current_thread();
    char *base_before;
    int err;

    /* ~280-char namestring: every "<anonymous> (FILE:LINE)" line is huge, so the
     * frame loop blows past the 2048-byte buffer after only a handful of frames
     * (forcing the overshoot), while the >20-frame depth makes the tail
     * "... N more frames" snprintf fire — the exact unbounded write being
     * guarded against. */
    static const char long_file[] =
        "a-really-long-source-file-namestring-used-to-make-each-backtrace-frame-"
        "line-very-large-so-that-the-formatted-backtrace-text-comfortably-exceeds-"
        "the-fixed-two-kilobyte-cl-backtrace-buf-and-would-overflow-into-the-"
        "adjacent-c-stack-base-field-if-pos-were-not-clamped.lisp";

    /* A chain of DISTINCT functions f1->f2->...->f25, f25 errors.  Distinct
     * names/lines defeat the backtrace cycle-collapser (a self-recursive
     * function would be summarised to a couple of lines and never overflow),
     * so cl_capture_backtrace formats the full max_show window of long lines —
     * comfortably more than 2048 bytes of text. */
    {
        int i;
        char def[128];
        eval_in_file(long_file, "(defun f25 () (error \"bottom\"))");
        for (i = 24; i >= 1; i--) {
            snprintf(def, sizeof(def),
                     "(defun f%d () (let ((r (f%d))) r))", i, i + 1);
            eval_in_file(long_file, def);
        }
    }

    base_before = t->c_stack_base;
    ASSERT(base_before != NULL);  /* set during init */

    CL_CATCH(err);
    if (err == CL_ERR_NONE) {
        eval_in_file(long_file, "(f1)");  /* signals -> cl_capture_backtrace */
        CL_UNCATCH();
    } else {
        CL_UNCATCH();
    }

    /* Primary detector: the tail "... N more frames" string must never have been
     * written past the end of the buffer.  Scan the 512 bytes following the
     * buffer for that signature (which cannot occur there legitimately). */
    ASSERT(memmem((char *)t + offsetof(CL_Thread, backtrace_buf) + CL_BACKTRACE_BUF_SIZE, 512,
                  "more frames", 11) == NULL);
    /* The adjacent c_stack_base field must be intact, and the formatted text
     * must stay NUL-terminated within the buffer bounds. */
    ASSERT(t->c_stack_base == base_before);
    ASSERT(strnlen(t->backtrace_buf, CL_BACKTRACE_BUF_SIZE) < CL_BACKTRACE_BUF_SIZE);

    cl_current_source_file = NULL;

    /* And ordinary shallow evaluation must not spuriously overflow now. */
    ASSERT(cl_eval_string("(+ 1 2)") != CL_NIL);
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
    RUN(backtrace_long_frames_no_buffer_overflow);

    teardown();
    REPORT();
}
