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

/* --- Deep backtraces: buffer growth, frame limits, no overflow ------------ *
 *
 * ~280-char namestring: every "N: FNN (FILE:LINE)" line is huge, so a 20-frame
 * window is several times the 2048-byte inline block.  That makes these tests
 * exercise the growth path (and, with the ceiling pinned down, the refusal
 * path) rather than fitting comfortably either way. */
static const char bt_long_file[] =
    "a-really-long-source-file-namestring-used-to-make-each-backtrace-frame-"
    "line-very-large-so-that-the-formatted-backtrace-text-comfortably-exceeds-"
    "the-fixed-two-kilobyte-cl-backtrace-buf-and-would-overflow-into-the-"
    "adjacent-c-stack-base-field-if-pos-were-not-clamped.lisp";

/* Define a chain of DISTINCT functions f1->f2->...->fDEPTH, fDEPTH errors.
 * Distinct names and lines defeat the backtrace cycle-collapser (a
 * self-recursive function would be summarised to a couple of lines), so the
 * renderer formats the whole frame window of long lines. */
static void define_long_chain(int depth)
{
    int i;
    char def[128];
    snprintf(def, sizeof(def), "(defun f%d () (error \"bottom\"))", depth);
    eval_in_file(bt_long_file, def);
    for (i = depth - 1; i >= 1; i--) {
        snprintf(def, sizeof(def),
                 "(defun f%d () (let ((r (f%d))) r))", i, i + 1);
        eval_in_file(bt_long_file, def);
    }
}

/* Call (f1) and swallow the error, so cl_capture_backtrace has run.  A bare
 * CL_CATCH does not rewind cl_vm.fp (its callers normally do), so the
 * error-time frames are still live afterwards — the same state the debugger
 * renders `:bt N` from, since it runs before any unwinding. */
static void signal_from_chain(void)
{
    int err;
    CL_CATCH(err);
    if (err == CL_ERR_NONE) {
        eval_in_file(bt_long_file, "(f1)");
        CL_UNCATCH();
    } else {
        CL_UNCATCH();
    }
    cl_current_source_file = NULL;
}

/* Count "  <n>: " frame lines in formatted backtrace text. */
static int count_frame_lines(const char *s)
{
    int n = 0;
    const char *p = s;
    while (*p != '\0') {
        const char *eol;
        if (p[0] == ' ' && p[1] == ' ' && p[2] >= '0' && p[2] <= '9') {
            const char *q = p + 2;
            while (*q >= '0' && *q <= '9') q++;
            if (q[0] == ':' && q[1] == ' ') n++;
        }
        eol = strchr(p, '\n');
        if (eol == NULL) break;
        p = eol + 1;
    }
    return n;
}

/* --- Regression: a long, deep backtrace must not overflow the inline block - *
 *
 * cl_capture_backtrace formats into the per-thread buffer, which starts as
 * backtrace_inline — sitting immediately before c_stack_base in the thread
 * struct.  snprintf returns the length it WOULD have written, so a naive
 * `pos += snprintf(...)` can push pos past the buffer; the next call then
 * computes a NEGATIVE remaining size, which wraps to a huge size_t and writes
 * "  ... N more frames\n" unbounded past the buffer — smashing c_stack_base.
 * A corrupted c_stack_base makes every later cl_check_c_stack compute a garbage
 * "used" value and signal a bogus "C stack overflow", which is exactly what
 * broke the ASDF source-registry walk (deep frames, long file paths) under
 * `make test-extra`.
 *
 * Growth would normally absorb this text, so the ceiling is pinned at the
 * inline size to force the refusal path — precisely the configuration where a
 * runaway append still has somewhere to run.  The test then scans the memory
 * immediately AFTER the buffer for the overflow's signature ("more frames",
 * emitted only by the tail append).  That text can never legitimately live in
 * the struct fields past backtrace_inline, so finding it is unambiguous proof
 * of an out-of-bounds write. */
TEST(backtrace_long_frames_no_buffer_overflow)
{
    CL_Thread *t = cl_get_current_thread();
    char *base_before;
    int saved_limit = cl_backtrace_cap_limit;

    cl_thread_backtrace_release(t);   /* start from the inline block */
    cl_backtrace_cap_limit = CL_BACKTRACE_BUF_SIZE;   /* refuse all growth */

    define_long_chain(25);

    base_before = t->c_stack_base;
    ASSERT(base_before != NULL);  /* set during init */

    signal_from_chain();

    /* Growth was refused, so the text must still be in the inline block. */
    ASSERT(t->backtrace_buf == t->backtrace_inline);
    ASSERT(t->backtrace_cap == CL_BACKTRACE_BUF_SIZE);
    ASSERT(memmem((char *)t + offsetof(CL_Thread, backtrace_inline)
                  + CL_BACKTRACE_BUF_SIZE, 512, "more frames", 11) == NULL);
    /* The adjacent c_stack_base field must be intact, and the formatted text
     * must stay NUL-terminated within the buffer bounds. */
    ASSERT(t->c_stack_base == base_before);
    ASSERT(strnlen(t->backtrace_inline, CL_BACKTRACE_BUF_SIZE)
           < CL_BACKTRACE_BUF_SIZE);

    cl_backtrace_cap_limit = saved_limit;

    /* And ordinary shallow evaluation must not spuriously overflow now. */
    ASSERT(cl_eval_string("(+ 1 2)") != CL_NIL);
}

/* --- The buffer grows so the frame limit is the ONLY thing that truncates --- *
 *
 * Same deep, long-pathed chain, but with the default ceiling: the full
 * CL_BACKTRACE_DEFAULT_FRAMES window is many times the inline block, so the
 * buffer must move to the heap and hold every one of those frames.  Before it
 * could grow, the text stopped after ~7 frames mid-line and the rest of the
 * window was silently lost. */
TEST(backtrace_buffer_grows_past_inline_block)
{
    CL_Thread *t = cl_get_current_thread();
    char *base_before;

    cl_thread_backtrace_release(t);
    ASSERT(t->backtrace_buf == t->backtrace_inline);
    ASSERT(t->backtrace_cap == CL_BACKTRACE_BUF_SIZE);

    define_long_chain(25);
    base_before = t->c_stack_base;

    signal_from_chain();

    ASSERT(t->backtrace_buf != t->backtrace_inline);
    ASSERT(t->backtrace_cap > CL_BACKTRACE_BUF_SIZE);
    ASSERT(t->backtrace_cap <= cl_backtrace_cap_limit);
    ASSERT(strlen(t->backtrace_buf) > CL_BACKTRACE_BUF_SIZE);
    ASSERT((int)strlen(t->backtrace_buf) < t->backtrace_cap);

    /* The whole default window is rendered: innermost F25 down to F6. */
    ASSERT(count_frame_lines(t->backtrace_buf) == CL_BACKTRACE_DEFAULT_FRAMES);
    ASSERT(strstr(t->backtrace_buf, "F25 ") != NULL);
    ASSERT(strstr(t->backtrace_buf, "F6 ") != NULL);
    ASSERT(strstr(t->backtrace_buf, "more frames") != NULL);
    ASSERT(t->c_stack_base == base_before);
}

/* --- cl_render_backtrace: the frame limit behind the debugger's `:bt [n]` --- *
 *
 * Re-renders the error-time window at a caller-chosen depth.  0 means every
 * frame, which is the only way to see what the "... N more frames" tail is
 * hiding.  That tail must also COUNT correctly: it used to report
 * `cl_vm.fp - max_show`, i.e. raw frames including the cl_vm_apply stubs the
 * renderer skips, so on the JIT path it over-reported what was left out. */
TEST(render_backtrace_frame_limit)
{
    CL_Thread *t = cl_get_current_thread();
    int total, shown;
    char want[64];

    cl_thread_backtrace_release(t);
    define_long_chain(25);
    signal_from_chain();

    /* Every frame: deeper than the default window, and nothing withheld. */
    cl_render_backtrace(0);
    total = count_frame_lines(t->backtrace_buf);
    ASSERT(total > CL_BACKTRACE_DEFAULT_FRAMES);
    ASSERT(strstr(t->backtrace_buf, "more frames") == NULL);
    /* The outermost user frame of the chain is now visible; it never was
     * inside the default 20-frame window. */
    ASSERT(strstr(t->backtrace_buf, "F1 ") != NULL);

    /* A five-frame window, with the tail accounting for exactly the rest. */
    cl_render_backtrace(5);
    shown = count_frame_lines(t->backtrace_buf);
    ASSERT(shown == 5);
    snprintf(want, sizeof(want), "... %d more frames", total - 5);
    ASSERT(strstr(t->backtrace_buf, want) != NULL);

    /* A limit at or beyond the real depth shows everything, no tail. */
    cl_render_backtrace(total);
    ASSERT(count_frame_lines(t->backtrace_buf) == total);
    ASSERT(strstr(t->backtrace_buf, "more frames") == NULL);

    /* Re-rendering never leaks the previous, longer text past the NUL. */
    ASSERT((int)strlen(t->backtrace_buf) < t->backtrace_cap);
}

/* --- Cycle-collapse tail count must share the printed loop's raw basis --- *
 *
 * bt_render's cycle-detection branch (the "infinite recursion" summary: "---
 * above N frames repeat M times ---") prints its prefix/cycle/remaining
 * frames with no frame_is_stub() filter, unlike the normal path just below
 * it.  Its trailing "... N more frames" tail must therefore count on that
 * same raw (unfiltered) basis -- using the filtered bt_count_real() there
 * would describe a different population of frames than the one actually
 * printed above it.
 *
 * Every call in this fixture goes through MAPCAR rather than a direct call,
 * FUNCALL, or (source-level) APPLY -- the compiler special-cases both FUNCALL
 * and APPLY into inline VM opcodes when they appear literally as the operator
 * (see compile_expr's SYM_FUNCALL / SYM_APPLY cases), so neither ever touches
 * cl_vm_apply or pushes a stub frame.  MAPCAR has no such compiler fast path:
 * bi_mapcar is an ordinary builtin that calls cl_vm_apply directly on each
 * element, which pushes a stub frame ahead of a bytecode/closure callee (see
 * frame_is_stub) -- the same mechanism JIT'd calls use on Amiga, reproducible
 * here on host.  So both the self-recursive tail (which the cycle-collapser
 * summarises) and the outer wrapper chain (left in the untouched "more
 * frames" remainder) are stub/real pairs throughout.
 *
 * Rather than hardcode frame indices (fragile against unrelated VM changes),
 * this checks the exact accounting identity that must hold no matter where
 * the cycle is found or how deep the chain is:
 *
 *   raw_total == printed_frame_lines + cycle_len*(repeat_count-1) + tail_count
 *
 * cycle_len*(repeat_count-1) is the frame count the summary line collapses
 * away (only cycle_len of the M repeats are printed).  Every raw frame from
 * cl_debug_base_fp down to 0 lands in exactly one of: a printed line, the
 * collapsed run, or the tail -- so the identity breaks whenever the tail
 * count is computed on a different basis than what was printed, which is
 * exactly the bug this guards against (it fails on the pre-fix
 * bt_count_real(j + 1), which undercounts by the stub frames sitting in the
 * unshown remainder). */
static void define_cycle_fixture(int n_outer, int rec_depth, const char *file)
{
    int i;
    char def[192];

    /* Self-recursive base: the IF's THEN and ELSE both sit on this one
     * source line, so every REC frame -- including the n=0 base case that
     * calls ERROR -- reports the same (name, line) to bt_detect_cycle. */
    snprintf(def, sizeof(def),
             "(defun cycrec (n)"
             " (if (= n 0) (error \"bottom\")"
             " (let ((r (car (mapcar (function cycrec) (list (- n 1)))))) r)))");
    eval_in_file(file, def);

    /* Defined innermost-first so each MAPCAR target already exists when
     * referenced, like define_long_chain above.  Every co<N> ignores its
     * one argument -- MAPCAR always calls its function with one arg per
     * list, so the whole chain (including the entry point) needs arity 1. */
    snprintf(def, sizeof(def),
             "(defun co%d (x) (declare (ignore x))"
             " (let ((r (car (mapcar (function cycrec) (list %d))))) r))",
             n_outer, rec_depth);
    eval_in_file(file, def);

    for (i = n_outer - 1; i >= 1; i--) {
        snprintf(def, sizeof(def),
                 "(defun co%d (x) (declare (ignore x))"
                 " (let ((r (car (mapcar (function co%d) (list nil))))) r))",
                 i, i + 1);
        eval_in_file(file, def);
    }
}

static void signal_from_cycle_fixture(const char *file)
{
    int err;
    CL_CATCH(err);
    if (err == CL_ERR_NONE) {
        eval_in_file(file, "(co1 nil)");
        CL_UNCATCH();
    } else {
        CL_UNCATCH();
    }
    cl_current_source_file = NULL;
}

TEST(render_backtrace_cycle_tail_count_matches_printed_basis)
{
    static const char cyc_file[] = "cycle-collapse.lisp";
    CL_Thread *t = cl_get_current_thread();
    int raw_total, printed, cycle_len, repeat_count, tail_count;
    const char *p;

    cl_thread_backtrace_release(t);
    define_cycle_fixture(20, 30, cyc_file);
    signal_from_cycle_fixture(cyc_file);

    raw_total = cl_debug_base_fp;
    ASSERT(raw_total > 0);

    cl_render_backtrace(5);

    /* Cycle collapse must have fired, with a tail left over from the outer
     * wrapper chain (both preconditions for the identity below). */
    p = strstr(t->backtrace_buf, "--- above ");
    ASSERT(p != NULL);
    ASSERT(sscanf(p, "--- above %d frames repeat %d times",
                  &cycle_len, &repeat_count) == 2);

    p = strstr(t->backtrace_buf, "... ");
    ASSERT(p != NULL);
    ASSERT(sscanf(p, "... %d more frames", &tail_count) == 1);

    printed = count_frame_lines(t->backtrace_buf);

    ASSERT(raw_total == printed + cycle_len * (repeat_count - 1) + tail_count);
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
    RUN(backtrace_buffer_grows_past_inline_block);
    RUN(render_backtrace_frame_limit);
    RUN(render_backtrace_cycle_tail_count_matches_printed_basis);

    teardown();
    REPORT();
}
