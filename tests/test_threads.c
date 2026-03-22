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

    err = CL_CATCH();
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

    /* Dynamic bindings */
    RUN(thread_inherits_dynamic_bindings);
    RUN(thread_let_binding_isolation);

    /* Error handling */
    RUN(thread_error_sets_aborted);

    /* Multiple threads */
    RUN(thread_multiple_concurrent);

    /* GC stress */
    RUN(thread_gc_stress);

    teardown();

    REPORT();
}
