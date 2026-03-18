/*
 * test_gc_threaded.c — Tests for Phase 2 GC coordination.
 *
 * Tests: concurrent allocation stress test, thread registry,
 * allocation mutex correctness, safepoint/STW GC under contention.
 */

#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "core/thread.h"
#include "platform/platform.h"
#include "platform/platform_thread.h"

#include <string.h>

/* Undef the gc_root_count macro so we can access the struct field directly
 * on CL_Thread instances that aren't the current thread. */
#undef gc_root_count

static void setup(void)
{
    platform_init();
    cl_thread_init();
    cl_error_init();
    cl_mem_init(4 * 1024 * 1024);  /* 4MB heap */
    cl_package_init();
    cl_symbol_init();
}

static void teardown(void)
{
    cl_mem_shutdown();
    cl_thread_shutdown();
    platform_shutdown();
}

/* ================================================================
 * Thread registry tests
 * ================================================================ */

TEST(thread_registry_main_registered)
{
    /* Main thread should be registered during cl_thread_init */
    ASSERT(cl_thread_list != NULL);
    ASSERT_EQ_INT((int)cl_thread_count, 1);
    ASSERT(cl_thread_list == cl_main_thread_ptr);
}

TEST(thread_registry_add_remove)
{
    CL_Thread extra;
    memset(&extra, 0, sizeof(CL_Thread));
    extra.id = 99;

    cl_thread_register(&extra);
    ASSERT_EQ_INT((int)cl_thread_count, 2);

    cl_thread_unregister(&extra);
    ASSERT_EQ_INT((int)cl_thread_count, 1);
}

/* ================================================================
 * Single-thread allocation under mutex (regression: must not deadlock)
 * ================================================================ */

TEST(alloc_single_thread_under_mutex)
{
    /* Allocating many objects should work fine with the alloc_mutex */
    int i;
    CL_Obj list = CL_NIL;
    CL_GC_PROTECT(list);

    for (i = 0; i < 1000; i++) {
        list = cl_cons(CL_MAKE_FIXNUM(i), list);
    }

    ASSERT(!CL_NULL_P(list));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(list)), 999);

    CL_GC_UNPROTECT(1);
}

/* ================================================================
 * Single-thread GC under new STW path (regression)
 * ================================================================ */

TEST(gc_single_thread_stw)
{
    /* Force GC multiple times — should work with STW path (no-op for 1 thread) */
    int i;
    CL_Obj list = CL_NIL;
    CL_GC_PROTECT(list);

    for (i = 0; i < 500; i++) {
        list = cl_cons(CL_MAKE_FIXNUM(i), list);
    }

    cl_gc();  /* Explicit GC */
    cl_gc();  /* Again */

    /* List should survive GC */
    ASSERT(!CL_NULL_P(list));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(list)), 499);

    CL_GC_UNPROTECT(1);
}

/* ================================================================
 * Concurrent allocation stress test
 *
 * Multiple threads allocate cons cells concurrently.  This exercises:
 * - alloc_mutex correctness (no heap corruption)
 * - Safepoint in cl_alloc()
 * - STW GC with multiple registered threads
 *
 * Each thread has its own CL_Thread with its own gc_roots, so
 * GC root protection works per-thread.
 * ================================================================ */

/* Per-thread work context */
typedef struct {
    CL_Thread    thread;
    int          alloc_count;  /* how many conses to allocate */
    int          result_ok;    /* 1 if validation passed */
} WorkerCtx;

static void *concurrent_allocator(void *arg)
{
    WorkerCtx *ctx = (WorkerCtx *)arg;
    CL_Thread *t = &ctx->thread;
    int i;
    CL_Obj list = CL_NIL;
    CL_Obj *root_ptr;

    /* Set up this thread's TLS and register */
    platform_tls_set(t);
    cl_thread_register(t);

    /* GC-protect our list root */
    root_ptr = &list;
    t->gc_roots[0] = root_ptr;
    t->gc_root_count = 1;

    /* Allocate cons cells */
    for (i = 0; i < ctx->alloc_count; i++) {
        list = cl_cons(CL_MAKE_FIXNUM(i), list);
    }

    /* Validate the list */
    ctx->result_ok = 1;
    {
        CL_Obj walk = list;
        int expected = ctx->alloc_count - 1;
        while (!CL_NULL_P(walk)) {
            if (!CL_CONS_P(walk) || CL_FIXNUM_VAL(cl_car(walk)) != expected) {
                ctx->result_ok = 0;
                break;
            }
            walk = cl_cdr(walk);
            expected--;
        }
        if (expected != -1)
            ctx->result_ok = 0;
    }

    t->gc_root_count = 0;
    cl_thread_unregister(t);
    return NULL;
}

TEST(concurrent_alloc_2_threads)
{
    void *handles[2];
    WorkerCtx workers[2];
    int i;

    for (i = 0; i < 2; i++) {
        memset(&workers[i], 0, sizeof(WorkerCtx));
        workers[i].thread.id = (uint32_t)(i + 1);
        workers[i].thread.status = 1;
        workers[i].thread.mv_count = 1;
        workers[i].alloc_count = 200;
        workers[i].result_ok = 0;
    }

    for (i = 0; i < 2; i++)
        platform_thread_create(&handles[i], concurrent_allocator, &workers[i], 0);

    for (i = 0; i < 2; i++)
        platform_thread_join(handles[i], NULL);

    for (i = 0; i < 2; i++)
        ASSERT_EQ_INT(workers[i].result_ok, 1);
}

TEST(concurrent_alloc_4_threads)
{
    void *handles[4];
    WorkerCtx workers[4];
    int i;

    for (i = 0; i < 4; i++) {
        memset(&workers[i], 0, sizeof(WorkerCtx));
        workers[i].thread.id = (uint32_t)(i + 1);
        workers[i].thread.status = 1;
        workers[i].thread.mv_count = 1;
        workers[i].alloc_count = 100;
        workers[i].result_ok = 0;
    }

    for (i = 0; i < 4; i++)
        platform_thread_create(&handles[i], concurrent_allocator, &workers[i], 0);

    for (i = 0; i < 4; i++)
        platform_thread_join(handles[i], NULL);

    for (i = 0; i < 4; i++)
        ASSERT_EQ_INT(workers[i].result_ok, 1);
}

/* ================================================================
 * Concurrent allocation with forced GC
 *
 * Use a small heap so GC is triggered frequently, exercising the
 * STW path under contention.
 * ================================================================ */

TEST(concurrent_alloc_with_gc)
{
    void *handles[3];
    WorkerCtx workers[3];
    int i;

    /* Shrink heap to force frequent GC */
    cl_mem_shutdown();
    cl_mem_init(256 * 1024);  /* 256KB — forces GC during concurrent alloc */

    for (i = 0; i < 3; i++) {
        memset(&workers[i], 0, sizeof(WorkerCtx));
        workers[i].thread.id = (uint32_t)(i + 1);
        workers[i].thread.status = 1;
        workers[i].thread.mv_count = 1;
        workers[i].alloc_count = 50;  /* modest count to avoid exhaustion */
        workers[i].result_ok = 0;
    }

    for (i = 0; i < 3; i++)
        platform_thread_create(&handles[i], concurrent_allocator, &workers[i], 0);

    for (i = 0; i < 3; i++)
        platform_thread_join(handles[i], NULL);

    for (i = 0; i < 3; i++)
        ASSERT_EQ_INT(workers[i].result_ok, 1);

    /* Restore normal heap */
    cl_mem_shutdown();
    cl_mem_init(4 * 1024 * 1024);
}

/* ================================================================
 * Safepoint smoke test — verify CL_SAFEPOINT() doesn't crash
 * when gc_requested is 0
 * ================================================================ */

TEST(safepoint_no_gc)
{
    CL_SAFEPOINT();
    CL_SAFEPOINT();
    CL_SAFEPOINT();
    ASSERT(1);  /* If we get here, safepoints are no-ops when no GC requested */
}

/* ================================================================
 * main
 * ================================================================ */

int main(void)
{
    test_init();
    setup();

    /* Registry */
    RUN(thread_registry_main_registered);
    RUN(thread_registry_add_remove);

    /* Single-thread regression */
    RUN(alloc_single_thread_under_mutex);
    RUN(gc_single_thread_stw);
    RUN(safepoint_no_gc);

    /* Multi-thread allocation */
    RUN(concurrent_alloc_2_threads);
    RUN(concurrent_alloc_4_threads);
    RUN(concurrent_alloc_with_gc);

    teardown();

    REPORT();
}
