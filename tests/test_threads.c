/*
 * test_threads.c — Tests for Phase 4: CL-level threading API (MP package).
 *
 * Tests: thread creation/join, thread-alive-p, current-thread, all-threads,
 * lock acquire/release, condvar wait/notify, dynamic binding isolation,
 * error handling in threads, GC under threaded execution.
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

/* ================================================================
 * Setup / teardown
 * ================================================================ */

static void setup(void)
{
    platform_init();
    cl_thread_init();
    cl_error_init();
    cl_mem_init(4 * 1024 * 1024);  /* 4MB heap */
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

/* Helper: eval a Lisp string, return printed result */
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
 * Thread creation and join
 * ================================================================ */

TEST(thread_make_and_join)
{
    const char *r = eval_print(
        "(mp:join-thread (mp:make-thread (lambda () 42)))");
    ASSERT_STR_EQ(r, "42");
}

TEST(thread_make_with_name)
{
    const char *r = eval_print(
        "(mp:thread-name (mp:make-thread (lambda () nil) :name \"worker\"))");
    ASSERT_STR_EQ(r, "\"worker\"");
}

TEST(thread_join_returns_result)
{
    const char *r = eval_print(
        "(mp:join-thread (mp:make-thread (lambda () (+ 10 20 30))))");
    ASSERT_STR_EQ(r, "60");
}

TEST(thread_join_cons_result)
{
    const char *r = eval_print(
        "(mp:join-thread (mp:make-thread (lambda () (list 1 2 3))))");
    ASSERT_STR_EQ(r, "(1 2 3)");
}

/* ================================================================
 * Thread predicates
 * ================================================================ */

TEST(thread_alive_p_before_join)
{
    /* A newly created thread may or may not have finished by the time we check */
    const char *r = eval_print(
        "(let ((thr (mp:make-thread (lambda () (dotimes (i 1000) i)))))"
        "  (prog1 (mp:thread-alive-p thr) (mp:join-thread thr)))");
    ASSERT(strcmp(r, "T") == 0 || strcmp(r, "NIL") == 0);
}

TEST(thread_alive_p_after_join)
{
    const char *r = eval_print(
        "(let ((thr (mp:make-thread (lambda () 42))))"
        "  (mp:join-thread thr)"
        "  (mp:thread-alive-p thr))");
    ASSERT_STR_EQ(r, "NIL");
}

/* ================================================================
 * Current thread and all-threads
 * ================================================================ */

TEST(current_thread_returns_thread)
{
    const char *r = eval_print("(mp:current-thread)");
    ASSERT(strstr(r, "#<THREAD") != NULL);
}

TEST(all_threads_includes_main)
{
    const char *r = eval_print("(>= (length (mp:all-threads)) 1)");
    ASSERT_STR_EQ(r, "T");
}

/* ================================================================
 * Thread yield
 * ================================================================ */

TEST(thread_yield_no_crash)
{
    const char *r = eval_print("(mp:thread-yield)");
    ASSERT_STR_EQ(r, "NIL");
}

/* ================================================================
 * Lock operations
 * ================================================================ */

TEST(lock_make)
{
    const char *r = eval_print("(mp:make-lock \"test-lock\")");
    ASSERT(strstr(r, "#<LOCK") != NULL);
}

TEST(lock_acquire_release)
{
    const char *r = eval_print(
        "(let ((lk (mp:make-lock)))"
        "  (mp:acquire-lock lk)"
        "  (mp:release-lock lk)"
        "  t)");
    ASSERT_STR_EQ(r, "T");
}

TEST(lock_trylock)
{
    const char *r = eval_print(
        "(let ((lk (mp:make-lock)))"
        "  (mp:acquire-lock lk nil))");
    ASSERT_STR_EQ(r, "T");
}

TEST(lock_with_lock_held)
{
    const char *r = eval_print(
        "(let ((lk (mp:make-lock)))"
        "  (mp:with-lock-held (lk)"
        "    42))");
    ASSERT_STR_EQ(r, "42");
}

TEST(lock_contention_two_threads)
{
    /* Two threads increment a shared counter protected by a lock */
    const char *r = eval_print(
        "(let ((counter (list 0))"
        "      (lk (mp:make-lock)))"
        "  (let ((t1 (mp:make-thread"
        "              (lambda ()"
        "                (dotimes (i 100)"
        "                  (mp:with-lock-held (lk)"
        "                    (setf (car counter) (+ (car counter) 1)))))))"
        "        (t2 (mp:make-thread"
        "              (lambda ()"
        "                (dotimes (i 100)"
        "                  (mp:with-lock-held (lk)"
        "                    (setf (car counter) (+ (car counter) 1))))))))"
        "    (mp:join-thread t1)"
        "    (mp:join-thread t2)"
        "    (car counter)))");
    ASSERT_STR_EQ(r, "200");
}

/* ================================================================
 * Condition variables
 * ================================================================ */

TEST(condvar_make)
{
    const char *r = eval_print("(mp:make-condition-variable)");
    ASSERT(strstr(r, "#<CONDITION-VARIABLE>") != NULL);
}

TEST(condvar_notify_wait)
{
    /* Producer-consumer: one thread waits, another notifies */
    const char *r = eval_print(
        "(let ((lk (mp:make-lock))"
        "      (cv (mp:make-condition-variable))"
        "      (ready (list nil)))"
        "  (let ((consumer (mp:make-thread"
        "                    (lambda ()"
        "                      (mp:acquire-lock lk)"
        "                      (loop until (car ready)"
        "                            do (mp:condition-wait cv lk))"
        "                      (mp:release-lock lk)"
        "                      (car ready)))))"
        "    (mp:thread-yield)"
        "    (mp:acquire-lock lk)"
        "    (setf (car ready) t)"
        "    (mp:condition-notify cv)"
        "    (mp:release-lock lk)"
        "    (mp:join-thread consumer)))");
    ASSERT_STR_EQ(r, "T");
}

/* ================================================================
 * Dynamic binding isolation (TLV inheritance)
 * ================================================================ */

TEST(thread_inherits_dynamic_bindings)
{
    /* Child thread should see parent's *package* */
    const char *r = eval_print(
        "(mp:join-thread"
        "  (mp:make-thread"
        "    (lambda () (package-name *package*))))");
    ASSERT_STR_EQ(r, "\"COMMON-LISP-USER\"");
}

TEST(thread_let_binding_isolation)
{
    /* With LET, child's dynamic binding is truly isolated */
    const char *r = eval_print(
        "(progn"
        "  (defvar *test-let-var* :parent)"
        "  (let ((child (mp:make-thread"
        "                 (lambda ()"
        "                   (let ((*test-let-var* :child))"
        "                     *test-let-var*)))))"
        "    (let ((child-result (mp:join-thread child)))"
        "      (list *test-let-var* child-result))))");
    ASSERT_STR_EQ(r, "(:PARENT :CHILD)");
}

/* ================================================================
 * Error handling in threads
 * ================================================================ */

TEST(thread_error_sets_aborted)
{
    /* Thread that errors should not crash the parent */
    const char *r = eval_print(
        "(let ((thr (mp:make-thread"
        "             (lambda () (error \"boom\")))))"
        "  (mp:join-thread thr)"
        "  :survived)");
    ASSERT_STR_EQ(r, ":SURVIVED");
}

/* ================================================================
 * Multiple threads
 * ================================================================ */

TEST(thread_multiple_concurrent)
{
    const char *r = eval_print(
        "(let ((threads (list"
        "  (mp:make-thread (lambda () (* 1 10)))"
        "  (mp:make-thread (lambda () (* 2 10)))"
        "  (mp:make-thread (lambda () (* 3 10)))"
        "  (mp:make-thread (lambda () (* 4 10))))))"
        "  (mapcar #'mp:join-thread threads))");
    ASSERT_STR_EQ(r, "(10 20 30 40)");
}

/* ================================================================
 * Named condition variables
 * ================================================================ */

TEST(condvar_make_with_name)
{
    const char *r = eval_print(
        "(mp:make-condition-variable \"my-cv\")");
    ASSERT(strstr(r, "#<CONDITION-VARIABLE") != NULL);
    ASSERT(strstr(r, "my-cv") != NULL);
}

TEST(condvar_name_accessor)
{
    const char *r = eval_print(
        "(mp:condition-name (mp:make-condition-variable \"test-cv\"))");
    ASSERT_STR_EQ(r, "\"test-cv\"");
}

TEST(condvar_name_nil_when_unnamed)
{
    const char *r = eval_print(
        "(mp:condition-name (mp:make-condition-variable))");
    ASSERT_STR_EQ(r, "NIL");
}

/* ================================================================
 * Lock name accessor
 * ================================================================ */

TEST(lock_name_accessor)
{
    const char *r = eval_print(
        "(mp:lock-name (mp:make-lock \"my-lock\"))");
    ASSERT_STR_EQ(r, "\"my-lock\"");
}

TEST(lock_name_nil_when_unnamed)
{
    const char *r = eval_print(
        "(mp:lock-name (mp:make-lock))");
    ASSERT_STR_EQ(r, "NIL");
}

/* ================================================================
 * Type predicates
 * ================================================================ */

TEST(threadp_true)
{
    const char *r = eval_print("(mp:threadp (mp:current-thread))");
    ASSERT_STR_EQ(r, "T");
}

TEST(threadp_false)
{
    const char *r = eval_print("(mp:threadp 42)");
    ASSERT_STR_EQ(r, "NIL");
}

TEST(lockp_true)
{
    const char *r = eval_print("(mp:lockp (mp:make-lock))");
    ASSERT_STR_EQ(r, "T");
}

TEST(lockp_false)
{
    const char *r = eval_print("(mp:lockp \"not-a-lock\")");
    ASSERT_STR_EQ(r, "NIL");
}

TEST(condition_variable_p_true)
{
    const char *r = eval_print(
        "(mp:condition-variable-p (mp:make-condition-variable))");
    ASSERT_STR_EQ(r, "T");
}

TEST(condition_variable_p_false)
{
    const char *r = eval_print("(mp:condition-variable-p nil)");
    ASSERT_STR_EQ(r, "NIL");
}

/* ================================================================
 * interrupt-thread
 * ================================================================ */

TEST(interrupt_thread_basic)
{
    /* Interrupt a running thread to set a shared flag */
    const char *r = eval_print(
        "(let ((flag (list nil))"
        "      (lk (mp:make-lock))"
        "      (cv (mp:make-condition-variable)))"
        "  (let ((thr (mp:make-thread"
        "               (lambda ()"
        "                 (mp:acquire-lock lk)"
        "                 (loop until (car flag)"
        "                       do (mp:condition-wait cv lk))"
        "                 (mp:release-lock lk)"
        "                 (car flag)))))"
        "    (mp:thread-yield)"
        "    (mp:interrupt-thread thr"
        "      (lambda () (setf (car flag) :interrupted)))"
        "    (mp:acquire-lock lk)"
        "    (mp:condition-notify cv)"
        "    (mp:release-lock lk)"
        "    (mp:join-thread thr)))");
    ASSERT_STR_EQ(r, ":INTERRUPTED");
}

TEST(interrupt_thread_self)
{
    /* Interrupting current thread calls function immediately */
    const char *r = eval_print(
        "(let ((x 0))"
        "  (mp:interrupt-thread (mp:current-thread)"
        "    (lambda () (setf x 42)))"
        "  x)");
    ASSERT_STR_EQ(r, "42");
}

/* ================================================================
 * destroy-thread
 * ================================================================ */

TEST(destroy_thread_basic)
{
    /* Destroyed thread should be joinable and report aborted status */
    const char *r = eval_print(
        "(let ((thr (mp:make-thread"
        "             (lambda ()"
        "               (loop (mp:thread-yield))))))"
        "  (mp:thread-yield)"
        "  (mp:destroy-thread thr)"
        "  (mp:join-thread thr)"
        "  :destroyed)");
    ASSERT_STR_EQ(r, ":DESTROYED");
}

TEST(destroy_thread_not_alive_after)
{
    const char *r = eval_print(
        "(let ((thr (mp:make-thread"
        "             (lambda ()"
        "               (loop (mp:thread-yield))))))"
        "  (mp:thread-yield)"
        "  (mp:destroy-thread thr)"
        "  (mp:join-thread thr)"
        "  (mp:thread-alive-p thr))");
    ASSERT_STR_EQ(r, "NIL");
}

TEST(destroy_thread_interrupts_sleep)
{
    /* SLEEP must surface a pending MP:DESTROY-THREAD before the full
     * nominal duration elapses; otherwise sento's
     * stop--with-wait--destroy-thread test (and any caller of
     * bt2:destroy-thread on a thread blocked in (sleep …)) waits the
     * full sleep and the test's wall-clock budget blows up.
     *
     * Spawn a thread that sleeps 30s, destroy it, then time the join.
     * Should join in well under one second on a healthy build. */
    const char *r = eval_print(
        "(let* ((thr (mp:make-thread (lambda () (sleep 30))))"
        "       (start (get-internal-real-time)))"
        "  (sleep 0.05)"
        "  (mp:destroy-thread thr)"
        "  (mp:join-thread thr)"
        "  (let ((elapsed-ms (* 1000.0 (/ (- (get-internal-real-time) start)"
        "                                 internal-time-units-per-second))))"
        "    (if (< elapsed-ms 1000) :ok elapsed-ms)))");
    ASSERT_STR_EQ(r, ":OK");
}

/* ================================================================
 * GC stress under threads
 * ================================================================ */

TEST(thread_gc_stress)
{
    const char *r = eval_print(
        "(let ((t1 (mp:make-thread"
        "            (lambda () (length (make-list 200)))))"
        "      (t2 (mp:make-thread"
        "            (lambda () (length (make-list 200))))))"
        "  (+ (mp:join-thread t1) (mp:join-thread t2)))");
    ASSERT_STR_EQ(r, "400");
}

/* Recursive lock: same thread acquires twice and releases twice without
 * deadlocking. Required by log4cl/serapeum which use bt:make-recursive-lock
 * and bt:with-recursive-lock-held with potential nesting. */
TEST(recursive_lock_self_reacquire)
{
    const char *r = eval_print(
        "(let ((lk (mp:make-recursive-lock \"rec\")))"
        "  (mp:acquire-lock lk t)"
        "  (mp:acquire-lock lk t)"
        "  (mp:release-lock lk)"
        "  (mp:release-lock lk)"
        "  :ok)");
    ASSERT_STR_EQ(r, ":OK");
}

TEST(recursive_lock_with_nested_macro)
{
    const char *r = eval_print(
        "(let ((lk (mp:make-recursive-lock)))"
        "  (mp:with-recursive-lock-held (lk)"
        "    (mp:with-recursive-lock-held (lk)"
        "      :inner)))");
    ASSERT_STR_EQ(r, ":INNER");
}

/* (mp:current-thread) must return the same eq object across calls so it
 * can be used as a hash-table key (required by bordeaux-threads-2's
 * .known-threads. registry). */
TEST(current_thread_identity_main)
{
    const char *r = eval_print(
        "(eq (mp:current-thread) (mp:current-thread))");
    ASSERT_STR_EQ(r, "T");
}

/* Identity must also hold inside a worker thread, AND between the value
 * returned by mp:make-thread and (mp:current-thread) called from inside. */
TEST(current_thread_identity_in_worker)
{
    const char *r = eval_print(
        "(let* ((slot nil)"
        "       (th (mp:make-thread"
        "             (lambda () (setq slot (mp:current-thread)) t))))"
        "  (mp:join-thread th)"
        "  (eq slot th))");
    ASSERT_STR_EQ(r, "T");
}

/* The lock-table is bounded (CL_MAX_LOCKS = 1024) but slots are reclaimed
 * by GC when their wrapping CL_Lock heap object dies.  Heap pressure alone
 * does not reliably trigger GC for tiny lock objects, so MAKE-LOCK does a
 * GC + retry when the table is full.  Sento's `ask-s` allocates one lock
 * per call; without this, 800 concurrent ask-s calls exhausted the table. */
TEST(lock_table_slot_reclaimed_by_gc)
{
    const char *r = eval_print(
        "(progn (dotimes (i 4000) (mp:make-lock)) :ok)");
    ASSERT_STR_EQ(r, ":OK");
}

/* Same for condition-variables (CL_MAX_CONDVARS = 1024). */
TEST(condvar_table_slot_reclaimed_by_gc)
{
    const char *r = eval_print(
        "(progn (dotimes (i 4000) (mp:make-condition-variable)) :ok)");
    ASSERT_STR_EQ(r, ":OK");
}

/* CL_MAX_THREADS = 256, but sento's actor-system pattern creates many
 * worker threads per test fixture and shuts them down without explicitly
 * calling mp:join-thread.  thread_entry sets status=2 and unregisters
 * the worker from cl_thread_list, but until 2026-04-28 the side-table
 * slot was never reclaimed.  After ~50 sento tests the cascade
 * "MP:MAKE-THREAD: thread table full (max 256)" failed every fixture in
 * AGENT.ARRAY-TESTS and beyond.
 *
 * Fix: gc_finalize_dead reclaims the slot when the wrapping CL_ThreadObj
 * is unreachable (which implies the worker is unregistered, since while
 * registered gc_mark_thread_roots keeps the wrapper alive via
 * t->thread_obj).  bi_make_thread runs cl_gc and retries on table-full,
 * mirroring the lock-allocator pattern.
 *
 * Test creates >> 256 unjoined threads.  Polls thread-alive-p so each
 * worker is guaranteed to have set status=2 + unregistered before we
 * drop the wrapper, eliminating scheduling flakiness. */
TEST(thread_table_slot_reclaimed_by_gc)
{
    const char *r = eval_print(
        "(progn"
        "  (dotimes (i 400)"
        "    (let ((th (mp:make-thread (lambda () nil))))"
        "      (do () ((not (mp:thread-alive-p th)))"
        "        (mp:thread-yield))))"
        "  :ok)");
    ASSERT_STR_EQ(r, ":OK");
}

/* Slot-reuse race: bi_join_thread frees the side-table slot, but the
 * Lisp-visible CL_ThreadObj wrapper keeps the now-stale thread_id.  If
 * MAKE-THREAD reuses that slot for a new worker, and the OLD wrapper
 * later becomes unreachable, gc_finalize_dead would key off the stale
 * id and free the unrelated new worker — corrupting whatever code was
 * still holding the new thread.  Fixed by setting tobj->thread_id to
 * (uint32_t)-1 in bi_join_thread after the slot is released. */
TEST(thread_slot_reuse_after_join_no_double_free)
{
    const char *r = eval_print(
        "(let ((th1 (mp:make-thread (lambda () 42))))"
        "  (mp:join-thread th1)"
        "  (let ((th2 (mp:make-thread (lambda () 99))))"
        "    (setq th1 nil)"
        "    (gc)"
        "    (mp:join-thread th2)))");
    ASSERT_STR_EQ(r, "99");
}

/* Zombie reaper: when GC can't reclaim slots because user code still
 * holds wrappers (e.g. bordeaux-threads' .known-threads. registry, which
 * is non-weak in cl-amiga), bi_make_thread falls back to scanning the
 * table for status>=2 entries and reaps them, invalidating the
 * wrapper's thread_id so it becomes a zombie (alive-p => NIL).
 *
 * Test: pin every spawned wrapper into a hash table so GC can't free
 * them, then create > CL_MAX_THREADS workers.  Without the reaper,
 * make-thread would error after 256.  After the reaper kicks in, the
 * pinned wrappers should report thread-alive-p => NIL (zombie), and
 * make-thread should keep working. */
TEST(thread_zombie_reaper_under_pinned_wrappers)
{
    const char *r = eval_print(
        "(let ((registry (make-hash-table)))"
        "  (dotimes (i 400)"
        "    (let ((th (mp:make-thread (lambda () nil))))"
        "      (do () ((not (mp:thread-alive-p th)))"
        "        (mp:thread-yield))"
        "      (setf (gethash i registry) th)))"
        "  (let ((alive 0) (zombie 0))"
        "    (maphash (lambda (k v) (declare (ignore k))"
        "               (if (mp:thread-alive-p v)"
        "                   (incf alive)"
        "                   (incf zombie)))"
        "             registry)"
        "    (list :total (hash-table-count registry)"
        "          :alive alive :zombie zombie)))");
    /* All 400 wrappers retained, all should be zombie now. */
    ASSERT_STR_EQ(r, "(:TOTAL 400 :ALIVE 0 :ZOMBIE 400)");
}

/* MP:ALL-THREADS used to allocate fresh CL_ThreadObj wrappers for each
 * non-main slot, producing aliases that pointed to the same thread_id
 * as the canonical wrapper.  Now it reuses the canonical wrapper from
 * CL_Thread.thread_obj so that EQ identity matches across (current-
 * thread), (make-thread) and (all-threads). */
TEST(all_threads_returns_canonical_wrapper)
{
    const char *r = eval_print(
        "(let* ((th  (mp:make-thread (lambda () (mp:thread-yield) :done)))"
        "       (lst (mp:all-threads)))"
        "  (prog1 (eq th (find th lst))"
        "    (mp:join-thread th)))");
    ASSERT_STR_EQ(r, "T");
}

/* Two threads racing to allocate locks past the table limit must NOT
 * deadlock.  Used to: thread A grabbed gc_mutex inside cl_gc_stop_the_world
 * and waited for B to reach a safepoint; B was blocked on gc_mutex inside
 * its own cl_gc call and never yielded.  Fixed by trylock+safepoint loop
 * in cl_gc_stop_the_world. */
TEST(make_lock_concurrent_no_deadlock)
{
    const char *r = eval_print(
        "(let ((t1 (mp:make-thread"
        "            (lambda () (dotimes (i 2000) (mp:make-lock)) :a)))"
        "      (t2 (mp:make-thread"
        "            (lambda () (dotimes (i 2000) (mp:make-lock)) :b))))"
        "  (list (mp:join-thread t1) (mp:join-thread t2)))");
    ASSERT_STR_EQ(r, "(:A :B)");
}

/* ================================================================
 * main
 * ================================================================ */

int main(void)
{
    test_init();
    setup();

    /* Thread creation and join */
    RUN(thread_make_and_join);
    RUN(thread_make_with_name);
    RUN(thread_join_returns_result);
    RUN(thread_join_cons_result);

    /* Thread predicates */
    RUN(thread_alive_p_before_join);
    RUN(thread_alive_p_after_join);

    /* Current thread */
    RUN(current_thread_returns_thread);
    RUN(all_threads_includes_main);

    /* Yield */
    RUN(thread_yield_no_crash);

    /* Locks */
    RUN(lock_make);
    RUN(lock_acquire_release);
    RUN(lock_trylock);
    RUN(lock_with_lock_held);
    RUN(lock_contention_two_threads);

    /* Condition variables */
    RUN(condvar_make);
    RUN(condvar_notify_wait);
    RUN(condvar_make_with_name);
    RUN(condvar_name_accessor);
    RUN(condvar_name_nil_when_unnamed);

    /* Lock name */
    RUN(lock_name_accessor);
    RUN(lock_name_nil_when_unnamed);

    /* Type predicates */
    RUN(threadp_true);
    RUN(threadp_false);
    RUN(lockp_true);
    RUN(lockp_false);
    RUN(condition_variable_p_true);
    RUN(condition_variable_p_false);

    /* Interrupt thread */
    RUN(interrupt_thread_basic);
    RUN(interrupt_thread_self);

    /* Destroy thread */
    RUN(destroy_thread_basic);
    RUN(destroy_thread_not_alive_after);
    RUN(destroy_thread_interrupts_sleep);

    /* Dynamic bindings */
    RUN(thread_inherits_dynamic_bindings);
    RUN(thread_let_binding_isolation);

    /* Error handling */
    RUN(thread_error_sets_aborted);

    /* Multiple threads */
    RUN(thread_multiple_concurrent);

    /* GC stress */
    RUN(thread_gc_stress);

    /* Recursive locks (required by log4cl/serapeum) */
    RUN(recursive_lock_self_reacquire);
    RUN(recursive_lock_with_nested_macro);

    /* Thread object identity (required by bordeaux-threads-2 hash registry) */
    RUN(current_thread_identity_main);
    RUN(current_thread_identity_in_worker);

    /* GC reclaims MP table slots */
    RUN(lock_table_slot_reclaimed_by_gc);
    RUN(condvar_table_slot_reclaimed_by_gc);
    RUN(thread_table_slot_reclaimed_by_gc);
    RUN(thread_slot_reuse_after_join_no_double_free);
    RUN(thread_zombie_reaper_under_pinned_wrappers);
    RUN(all_threads_returns_canonical_wrapper);
    RUN(make_lock_concurrent_no_deadlock);

    teardown();

    REPORT();
}
