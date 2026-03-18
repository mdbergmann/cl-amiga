/*
 * test_thread_platform.c — Tests for Phase 1 platform threading primitives.
 *
 * Tests: mutex correctness, condvar signal/broadcast, thread create/join,
 * atomic increment from multiple threads, TLS isolation.
 */

#include "test.h"
#include "platform/platform.h"
#include "platform/platform_thread.h"
#include "core/thread.h"

#include <string.h>

/* ================================================================
 * Mutex tests
 * ================================================================ */

TEST(mutex_init_destroy)
{
    void *m = NULL;
    int ret = platform_mutex_init(&m);
    ASSERT_EQ_INT(ret, 0);
    ASSERT(m != NULL);
    platform_mutex_destroy(m);
}

TEST(mutex_lock_unlock)
{
    void *m = NULL;
    platform_mutex_init(&m);
    platform_mutex_lock(m);
    platform_mutex_unlock(m);
    /* If we get here without deadlock, it works */
    ASSERT(1);
    platform_mutex_destroy(m);
}

TEST(mutex_trylock)
{
    void *m = NULL;
    int ret;
    platform_mutex_init(&m);

    /* Should succeed on unlocked mutex */
    ret = platform_mutex_trylock(m);
    ASSERT_EQ_INT(ret, 0);
    platform_mutex_unlock(m);

    platform_mutex_destroy(m);
}

/* ================================================================
 * Condvar tests
 * ================================================================ */

TEST(condvar_init_destroy)
{
    void *cv = NULL;
    int ret = platform_condvar_init(&cv);
    ASSERT_EQ_INT(ret, 0);
    ASSERT(cv != NULL);
    platform_condvar_destroy(cv);
}

/* Signal from one thread, wait in another */
static void *cv_signaler(void *arg)
{
    void **params = (void **)arg;
    void *mutex = params[0];
    void *cv    = params[1];
    volatile int *flag = (volatile int *)params[2];

    platform_mutex_lock(mutex);
    *flag = 1;
    platform_condvar_signal(cv);
    platform_mutex_unlock(mutex);

    return NULL;
}

TEST(condvar_signal_wait)
{
    void *mutex = NULL, *cv = NULL, *th = NULL;
    volatile int flag = 0;
    void *params[3];

    platform_mutex_init(&mutex);
    platform_condvar_init(&cv);

    params[0] = mutex;
    params[1] = cv;
    params[2] = (void *)&flag;

    platform_mutex_lock(mutex);

    /* Spawn thread that will signal us */
    platform_thread_create(&th, cv_signaler, params, 0);

    /* Wait for signal */
    while (!flag)
        platform_condvar_wait(cv, mutex);
    platform_mutex_unlock(mutex);

    ASSERT_EQ_INT(flag, 1);

    platform_thread_join(th, NULL);
    platform_condvar_destroy(cv);
    platform_mutex_destroy(mutex);
}

/* Broadcast wakes multiple waiters */
typedef struct {
    void *mutex;
    void *cv;
    volatile int *counter;
    volatile int *go;
} BcastArg;

static void *bcast_waiter(void *arg)
{
    BcastArg *ba = (BcastArg *)arg;

    platform_mutex_lock(ba->mutex);
    while (!*ba->go)
        platform_condvar_wait(ba->cv, ba->mutex);
    (*ba->counter)++;
    platform_mutex_unlock(ba->mutex);

    return NULL;
}

TEST(condvar_broadcast)
{
    void *mutex = NULL, *cv = NULL;
    void *threads[4];
    BcastArg args[4];
    volatile int counter = 0, go = 0;
    int i;

    platform_mutex_init(&mutex);
    platform_condvar_init(&cv);

    for (i = 0; i < 4; i++) {
        args[i].mutex = mutex;
        args[i].cv = cv;
        args[i].counter = &counter;
        args[i].go = &go;
        platform_thread_create(&threads[i], bcast_waiter, &args[i], 0);
    }

    /* Give threads time to enter wait */
    platform_sleep_ms(50);

    /* Broadcast */
    platform_mutex_lock(mutex);
    go = 1;
    platform_condvar_broadcast(cv);
    platform_mutex_unlock(mutex);

    for (i = 0; i < 4; i++)
        platform_thread_join(threads[i], NULL);

    ASSERT_EQ_INT((int)counter, 4);

    platform_condvar_destroy(cv);
    platform_mutex_destroy(mutex);
}

TEST(condvar_wait_timeout)
{
    void *mutex = NULL, *cv = NULL;
    int ret;

    platform_mutex_init(&mutex);
    platform_condvar_init(&cv);

    platform_mutex_lock(mutex);
    ret = platform_condvar_wait_timeout(cv, mutex, 50); /* 50ms timeout */
    platform_mutex_unlock(mutex);

    /* Should have timed out */
    ASSERT_EQ_INT(ret, 1);

    platform_condvar_destroy(cv);
    platform_mutex_destroy(mutex);
}

/* ================================================================
 * Thread create/join tests
 * ================================================================ */

static void *return_42(void *arg)
{
    (void)arg;
    return (void *)(uintptr_t)42;
}

TEST(thread_create_join)
{
    void *th = NULL;
    void *result = NULL;
    int ret;

    ret = platform_thread_create(&th, return_42, NULL, 0);
    ASSERT_EQ_INT(ret, 0);
    ASSERT(th != NULL);

    ret = platform_thread_join(th, &result);
    ASSERT_EQ_INT(ret, 0);
    ASSERT_EQ_INT((int)(uintptr_t)result, 42);
}

static void *add_offset(void *arg)
{
    int val = (int)(uintptr_t)arg;
    return (void *)(uintptr_t)(val + 100);
}

TEST(thread_multiple)
{
    void *threads[8];
    void *results[8];
    int i;

    for (i = 0; i < 8; i++)
        platform_thread_create(&threads[i], add_offset, (void *)(uintptr_t)i, 0);

    for (i = 0; i < 8; i++) {
        platform_thread_join(threads[i], &results[i]);
        ASSERT_EQ_INT((int)(uintptr_t)results[i], i + 100);
    }
}

/* ================================================================
 * Atomic tests
 * ================================================================ */

static volatile uint32_t atomic_counter = 0;

static void *atomic_incrementer(void *arg)
{
    int count = (int)(uintptr_t)arg;
    int i;
    for (i = 0; i < count; i++)
        platform_atomic_inc(&atomic_counter);
    return NULL;
}

TEST(atomic_inc_single)
{
    volatile uint32_t val = 0;
    ASSERT_EQ_INT((int)platform_atomic_inc(&val), 1);
    ASSERT_EQ_INT((int)platform_atomic_inc(&val), 2);
    ASSERT_EQ_INT((int)platform_atomic_inc(&val), 3);
    ASSERT_EQ_INT((int)val, 3);
}

TEST(atomic_dec_single)
{
    volatile uint32_t val = 10;
    ASSERT_EQ_INT((int)platform_atomic_dec(&val), 9);
    ASSERT_EQ_INT((int)platform_atomic_dec(&val), 8);
    ASSERT_EQ_INT((int)val, 8);
}

TEST(atomic_cas)
{
    volatile uint32_t val = 5;
    /* CAS should succeed when expected matches */
    ASSERT_EQ_INT(platform_atomic_cas(&val, 5, 10), 1);
    ASSERT_EQ_INT((int)val, 10);
    /* CAS should fail when expected doesn't match */
    ASSERT_EQ_INT(platform_atomic_cas(&val, 5, 20), 0);
    ASSERT_EQ_INT((int)val, 10); /* unchanged */
}

TEST(atomic_inc_multithread)
{
    void *threads[4];
    int i;

    atomic_counter = 0;

    for (i = 0; i < 4; i++)
        platform_thread_create(&threads[i], atomic_incrementer,
                               (void *)(uintptr_t)10000, 0);

    for (i = 0; i < 4; i++)
        platform_thread_join(threads[i], NULL);

    ASSERT_EQ_INT((int)atomic_counter, 40000);
}

/* ================================================================
 * Mutex contention test (multi-threaded)
 * ================================================================ */

static void *mutex_counter_thread = NULL;
static int shared_counter = 0;

static void *mutex_incrementer(void *arg)
{
    void *m = arg;
    int i;
    for (i = 0; i < 10000; i++) {
        platform_mutex_lock(m);
        shared_counter++;
        platform_mutex_unlock(m);
    }
    return NULL;
}

TEST(mutex_contention)
{
    void *m = NULL;
    void *threads[4];
    int i;

    platform_mutex_init(&m);
    shared_counter = 0;

    for (i = 0; i < 4; i++)
        platform_thread_create(&threads[i], mutex_incrementer, m, 0);

    for (i = 0; i < 4; i++)
        platform_thread_join(threads[i], NULL);

    ASSERT_EQ_INT(shared_counter, 40000);

    platform_mutex_destroy(m);
}

/* ================================================================
 * TLS tests
 * ================================================================ */

static void *tls_setter(void *arg)
{
    int val = (int)(uintptr_t)arg;

    /* Set a unique TLS value in this thread */
    platform_tls_set((void *)(uintptr_t)(val * 1000));

    /* Yield to let other threads run */
    platform_thread_yield();

    /* Should still see our own value */
    return platform_tls_get();
}

TEST(tls_isolation)
{
    void *threads[4];
    void *results[4];
    int i;
    void *main_val;

    /* Save and restore main thread TLS */
    main_val = platform_tls_get();

    for (i = 0; i < 4; i++)
        platform_thread_create(&threads[i], tls_setter,
                               (void *)(uintptr_t)(i + 1), 0);

    for (i = 0; i < 4; i++) {
        platform_thread_join(threads[i], &results[i]);
        ASSERT_EQ_INT((int)(uintptr_t)results[i], (i + 1) * 1000);
    }

    /* Main thread TLS should be unchanged */
    ASSERT(platform_tls_get() == main_val);
}

/* ================================================================
 * CL_Thread TLS integration test
 * ================================================================ */

TEST(cl_thread_tls_backed)
{
    /* cl_current_thread should return the main thread via TLS */
    CL_Thread *t = cl_get_current_thread();
    ASSERT(t != NULL);
    ASSERT(t == cl_main_thread_ptr);
    /* CT macro should work too */
    ASSERT(CT != NULL);
    ASSERT(CT == cl_main_thread_ptr);
}

/* ================================================================
 * Thread yield (smoke test — just verify it doesn't crash)
 * ================================================================ */

TEST(thread_yield)
{
    platform_thread_yield();
    ASSERT(1);
}

/* ================================================================
 * main
 * ================================================================ */

int main(void)
{
    test_init();
    platform_init();
    cl_thread_init();

    /* Mutex */
    RUN(mutex_init_destroy);
    RUN(mutex_lock_unlock);
    RUN(mutex_trylock);
    RUN(mutex_contention);

    /* Condvar */
    RUN(condvar_init_destroy);
    RUN(condvar_signal_wait);
    RUN(condvar_broadcast);
    RUN(condvar_wait_timeout);

    /* Thread */
    RUN(thread_create_join);
    RUN(thread_multiple);
    RUN(thread_yield);

    /* Atomics */
    RUN(atomic_inc_single);
    RUN(atomic_dec_single);
    RUN(atomic_cas);
    RUN(atomic_inc_multithread);

    /* TLS */
    RUN(tls_isolation);
    RUN(cl_thread_tls_backed);

    cl_thread_shutdown();
    platform_shutdown();

    REPORT();
}
