/* Tests for EXT:*EXIT-HOOKS* — the shutdown hooks main() runs on the way out.
 *
 *   EXT:ADD-EXIT-HOOK fn     -> push a function designator (EQL-idempotent)
 *   EXT:REMOVE-EXIT-HOOK fn  -> T if it was on the list, NIL otherwise
 *   EXT:*EXIT-HOOKS*         -> the list itself, most recently added first
 *
 * cl_run_exit_hooks() is what main.c calls at the head of its shutdown funnel
 * (before any subsystem teardown), so calling it directly here exercises the
 * production path.  The end-to-end legs — real process exits through (QUIT),
 * --script, --non-interactive and REPL EOF — are tests/test_exit_hooks.sh.
 *
 * Note: the error-tolerance tests deliberately provoke hook failures, so a few
 * "; Warning: error in exit hook ..." lines on stderr are expected output. */

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
    cl_debugger_init();
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

/* Each test starts from an empty hook list and a fresh log. */
static void reset_hooks(void)
{
    cl_eval_string("(setq ext:*exit-hooks* nil)");
    cl_eval_string("(defparameter *log* nil)");
}

/* --- Ordering: most recently added runs first (atexit / UNWIND-PROTECT) --- */

TEST(exit_hooks_run_most_recent_first)
{
    reset_hooks();
    cl_eval_string("(ext:add-exit-hook (lambda () (push :a *log*)))");
    cl_eval_string("(ext:add-exit-hook (lambda () (push :b *log*)))");
    cl_eval_string("(ext:add-exit-hook (lambda () (push :c *log*)))");
    ASSERT(truthy("(= (length ext:*exit-hooks*) 3)"));

    cl_run_exit_hooks();

    /* C ran first, then B, then A — so the pushes stack up as (:a :b :c). */
    ASSERT(truthy("(equal *log* '(:a :b :c))"));
}

/* --- The list is taken before the first hook runs --- */

TEST(exit_hooks_cleared_after_running)
{
    reset_hooks();
    cl_eval_string("(ext:add-exit-hook (lambda () (push :once *log*)))");

    cl_run_exit_hooks();
    ASSERT(truthy("(null ext:*exit-hooks*)"));
    ASSERT(truthy("(equal *log* '(:once))"));

    /* A second pass (embedded runtime, or a shutdown reached twice) must not
     * re-run anything. */
    cl_run_exit_hooks();
    ASSERT(truthy("(equal *log* '(:once))"));
}

TEST(exit_hooks_added_while_running_do_not_run)
{
    reset_hooks();
    cl_eval_string(
        "(ext:add-exit-hook"
        "  (lambda () (push :outer *log*)"
        "             (ext:add-exit-hook (lambda () (push :inner *log*)))))");

    cl_run_exit_hooks();
    /* Documented behaviour: the list is taken up front, so a hook registered
     * from inside a hook is queued for a shutdown that will never come. */
    ASSERT(truthy("(equal *log* '(:outer))"));
    ASSERT(truthy("(= (length ext:*exit-hooks*) 1)"));
}

/* --- One bad hook must not cost the others (or the shutdown) --- */

TEST(exit_hook_error_does_not_stop_the_rest)
{
    reset_hooks();
    cl_eval_string("(ext:add-exit-hook (lambda () (push :first *log*)))");
    cl_eval_string("(ext:add-exit-hook (lambda () (error \"boom\")))");
    cl_eval_string("(ext:add-exit-hook (lambda () (push :last *log*)))");

    cl_run_exit_hooks();

    /* :last ran, the erroring hook was reported and skipped, :first still ran */
    ASSERT(truthy("(equal *log* '(:first :last))"));
    ASSERT(truthy("(null ext:*exit-hooks*)"));
}

TEST(exit_hook_error_leaves_vm_usable)
{
    reset_hooks();
    cl_eval_string("(ext:add-exit-hook (lambda () (car 7)))");   /* type error */
    cl_run_exit_hooks();

    /* The VM must still evaluate after an unwound hook — main.c goes on to
     * tear down streams/threads, and the C unit tests keep running here. */
    ASSERT(truthy("(= (+ 1 2) 3)"));
}

/* --- Function designators: symbols resolve when the hook RUNS --- */

TEST(exit_hook_symbol_resolved_at_run_time)
{
    reset_hooks();
    cl_eval_string("(ext:add-exit-hook 'late-hook)");
    /* Defined only after registration — and redefined before the run, so the
     * hook must pick up the LATEST definition, not one captured at add time. */
    cl_eval_string("(defun late-hook () (push :wrong *log*))");
    cl_eval_string("(defun late-hook () (push :late *log*))");

    cl_run_exit_hooks();
    ASSERT(truthy("(equal *log* '(:late))"));
}

TEST(exit_hook_undefined_symbol_is_reported_not_fatal)
{
    reset_hooks();
    cl_eval_string("(ext:add-exit-hook (lambda () (push :good *log*)))");
    cl_eval_string("(ext:add-exit-hook 'no-such-exit-hook-function)");

    cl_run_exit_hooks();
    ASSERT(truthy("(equal *log* '(:good))"));
}

TEST(add_exit_hook_rejects_non_designators)
{
    reset_hooks();
    ASSERT(truthy(
        "(eq :error (handler-case (ext:add-exit-hook 42) (error () :error)))"));
    ASSERT(truthy(
        "(eq :error (handler-case (ext:add-exit-hook \"f\") (error () :error)))"));
    /* NIL is rejected at registration rather than failing silently at exit. */
    ASSERT(truthy(
        "(eq :error (handler-case (ext:add-exit-hook nil) (error () :error)))"));
    ASSERT(truthy("(null ext:*exit-hooks*)"));
}

/* --- ADD is idempotent; REMOVE reports whether it removed anything --- */

TEST(add_exit_hook_is_idempotent_and_returns_its_argument)
{
    reset_hooks();
    cl_eval_string("(defun dup-hook () (push :dup *log*))");
    ASSERT(truthy("(eq 'dup-hook (ext:add-exit-hook 'dup-hook))"));
    cl_eval_string("(ext:add-exit-hook 'dup-hook)");
    cl_eval_string("(ext:add-exit-hook 'dup-hook)");
    ASSERT(truthy("(equal ext:*exit-hooks* '(dup-hook))"));

    cl_run_exit_hooks();
    ASSERT(truthy("(equal *log* '(:dup))"));
}

TEST(remove_exit_hook)
{
    reset_hooks();
    cl_eval_string("(defun ha () (push :a *log*))");
    cl_eval_string("(defun hb () (push :b *log*))");
    cl_eval_string("(defun hc () (push :c *log*))");
    cl_eval_string("(progn (ext:add-exit-hook 'ha)"
                   "       (ext:add-exit-hook 'hb)"
                   "       (ext:add-exit-hook 'hc))");

    /* Middle of the list: spliced out, order of the rest preserved. */
    ASSERT(truthy("(eq t (ext:remove-exit-hook 'hb))"));
    ASSERT(truthy("(equal ext:*exit-hooks* '(hc ha))"));
    /* Head of the list. */
    ASSERT(truthy("(eq t (ext:remove-exit-hook 'hc))"));
    ASSERT(truthy("(equal ext:*exit-hooks* '(ha))"));
    /* Not on the list -> NIL, list untouched. */
    ASSERT(truthy("(null (ext:remove-exit-hook 'hb))"));
    ASSERT(truthy("(equal ext:*exit-hooks* '(ha))"));

    cl_run_exit_hooks();
    ASSERT(truthy("(equal *log* '(:a))"));
}

TEST(remove_exit_hook_on_empty_list)
{
    reset_hooks();
    ASSERT(truthy("(null (ext:remove-exit-hook 'ha))"));
    ASSERT(truthy("(null (ext:remove-exit-hook (lambda () nil)))"));
}

/* --- A hook that quits ends the sequence instead of re-entering shutdown --- */

TEST(exit_hook_calling_quit_stops_the_sequence)
{
    reset_hooks();
    cl_eval_string("(ext:add-exit-hook (lambda () (push :not-reached *log*)))");
    cl_eval_string("(ext:add-exit-hook (lambda () (push :quitter *log*) (quit 7)))");

    cl_run_exit_hooks();

    ASSERT(truthy("(equal *log* '(:quitter))"));
    ASSERT(truthy("(null ext:*exit-hooks*)"));
    /* And the runtime is still healthy for the shutdown steps that follow. */
    ASSERT(truthy("(= (+ 1 2) 3)"));
}

/* --- Hooks allocate; the list cursor must survive a moving GC --- */

TEST(exit_hooks_survive_compaction_mid_sequence)
{
    reset_hooks();
    cl_eval_string("(defparameter *n* 0)");
    /* Distinct closures (each captures its own K, so the EQL de-duplication
     * cannot collapse them).  Every hook conses and forces a compacting GC,
     * which relocates the very list cl_run_exit_hooks is walking. */
    cl_eval_string(
        "(dotimes (i 25)"
        "  (let ((k i))"
        "    (ext:add-exit-hook"
        "      (lambda () (ext:gc-compact)"
        "                 (setq *n* (+ *n* k 1))"
        "                 (make-string 200 :initial-element #\\x)))))");
    ASSERT(truthy("(= (length ext:*exit-hooks*) 25)"));

    cl_run_exit_hooks();

    /* Sum of (k+1) for k = 0..24 = 325: every hook ran exactly once. */
    ASSERT(truthy("(= *n* 325)"));
    ASSERT(truthy("(null ext:*exit-hooks*)"));
}

/* --- The variable itself is a normal special the user may just SETF --- */

TEST(exit_hooks_variable_can_be_set_directly)
{
    reset_hooks();
    cl_eval_string("(defun sa () (push :sa *log*))");
    cl_eval_string("(defun sb () (push :sb *log*))");
    cl_eval_string("(setq ext:*exit-hooks* (list #'sa 'sb))");

    cl_run_exit_hooks();
    ASSERT(truthy("(equal *log* '(:sb :sa))"));
}

TEST(exit_hooks_junk_value_is_ignored)
{
    reset_hooks();
    /* A non-list value must not crash the shutdown path. */
    cl_eval_string("(setq ext:*exit-hooks* :not-a-list)");
    cl_run_exit_hooks();
    ASSERT(truthy("(= (+ 1 2) 3)"));
    cl_eval_string("(setq ext:*exit-hooks* nil)");
}

int main(void)
{
    test_init();
    setup();

    RUN(exit_hooks_run_most_recent_first);
    RUN(exit_hooks_cleared_after_running);
    RUN(exit_hooks_added_while_running_do_not_run);
    RUN(exit_hook_error_does_not_stop_the_rest);
    RUN(exit_hook_error_leaves_vm_usable);
    RUN(exit_hook_symbol_resolved_at_run_time);
    RUN(exit_hook_undefined_symbol_is_reported_not_fatal);
    RUN(add_exit_hook_rejects_non_designators);
    RUN(add_exit_hook_is_idempotent_and_returns_its_argument);
    RUN(remove_exit_hook);
    RUN(remove_exit_hook_on_empty_list);
    RUN(exit_hook_calling_quit_stops_the_sequence);
    RUN(exit_hooks_survive_compaction_mid_sequence);
    RUN(exit_hooks_variable_can_be_set_directly);
    RUN(exit_hooks_junk_value_is_ignored);

    teardown();
    REPORT();
}
