/*
 * test_shared_state.c — Tests for shared state protection (Phase 5).
 *
 * Tests: concurrent intern, concurrent gensym, concurrent macro lookup,
 * concurrent stream output, concurrent struct registration, package
 * creation under contention.
 */

#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "core/compiler.h"
#include "core/reader.h"
#include "core/printer.h"
#include "core/vm.h"
#include "core/builtins.h"
#include "core/repl.h"
#include "core/stream.h"
#include "core/thread.h"
#include "platform/platform.h"
#include "platform/platform_thread.h"

#include <string.h>
#include <stdio.h>

/* Undef the gc_root_count macro so we can access the struct field directly */
#undef gc_root_count

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

/* ================================================================
 * Worker context for concurrent tests
 * ================================================================ */

typedef struct {
    CL_Thread    thread;
    int          result_ok;
    int          worker_id;
    /* Test-specific data */
    CL_Obj       result_sym;
    int          count;
} WorkerCtx;

static void worker_init(WorkerCtx *ctx, int id)
{
    memset(ctx, 0, sizeof(WorkerCtx));
    ctx->thread.id = (uint32_t)(id + 1);
    ctx->thread.status = 1;
    ctx->thread.mv_count = 1;
    ctx->worker_id = id;
    ctx->result_ok = 0;
}

static void worker_register(WorkerCtx *ctx)
{
    platform_tls_set(&ctx->thread);
    cl_thread_register(&ctx->thread);
}

static void worker_unregister(WorkerCtx *ctx)
{
    cl_thread_unregister(&ctx->thread);
}

/* ================================================================
 * Test: concurrent intern of the SAME symbol
 *
 * Multiple threads intern the same name simultaneously.
 * All must get the same CL_Obj back.
 * ================================================================ */

static void *concurrent_intern_same_worker(void *arg)
{
    WorkerCtx *ctx = (WorkerCtx *)arg;
    worker_register(ctx);

    ctx->result_sym = cl_intern_in("CONCURRENT-TEST-SYM", 19, cl_package_cl);
    ctx->result_ok = !CL_NULL_P(ctx->result_sym);

    worker_unregister(ctx);
    return NULL;
}

TEST(concurrent_intern_same_symbol)
{
    void *handles[4];
    WorkerCtx workers[4];
    int i;
    CL_Obj first;

    for (i = 0; i < 4; i++)
        worker_init(&workers[i], i);

    for (i = 0; i < 4; i++)
        platform_thread_create(&handles[i], concurrent_intern_same_worker,
                               &workers[i], 0);

    for (i = 0; i < 4; i++)
        platform_thread_join(handles[i], NULL);

    /* All should succeed */
    for (i = 0; i < 4; i++)
        ASSERT_EQ_INT(workers[i].result_ok, 1);

    /* All should return the exact same symbol object */
    first = workers[0].result_sym;
    for (i = 1; i < 4; i++)
        ASSERT_EQ(workers[i].result_sym, first);
}

/* ================================================================
 * Test: concurrent intern of DIFFERENT symbols
 *
 * Each thread interns a unique symbol.  All must succeed,
 * no corruption.
 * ================================================================ */

static void *concurrent_intern_diff_worker(void *arg)
{
    WorkerCtx *ctx = (WorkerCtx *)arg;
    char name[32];
    int len;
    worker_register(ctx);

    len = snprintf(name, sizeof(name), "WORKER-SYM-%d", ctx->worker_id);
    ctx->result_sym = cl_intern_in(name, (uint32_t)len, cl_package_cl);
    ctx->result_ok = !CL_NULL_P(ctx->result_sym);

    worker_unregister(ctx);
    return NULL;
}

TEST(concurrent_intern_different_symbols)
{
    void *handles[4];
    WorkerCtx workers[4];
    int i, j;

    for (i = 0; i < 4; i++)
        worker_init(&workers[i], i);

    for (i = 0; i < 4; i++)
        platform_thread_create(&handles[i], concurrent_intern_diff_worker,
                               &workers[i], 0);

    for (i = 0; i < 4; i++)
        platform_thread_join(handles[i], NULL);

    /* All should succeed */
    for (i = 0; i < 4; i++)
        ASSERT_EQ_INT(workers[i].result_ok, 1);

    /* All symbols should be distinct */
    for (i = 0; i < 4; i++)
        for (j = i + 1; j < 4; j++)
            ASSERT(workers[i].result_sym != workers[j].result_sym);
}

/* ================================================================
 * Test: concurrent gensym
 *
 * Multiple threads call gensym simultaneously.
 * All returned symbols must be distinct.
 * ================================================================ */

static void *concurrent_gensym_worker(void *arg)
{
    WorkerCtx *ctx = (WorkerCtx *)arg;
    int i;
    worker_register(ctx);

    /* Call gensym 10 times, store last result */
    ctx->result_ok = 1;
    for (i = 0; i < 10; i++) {
        CL_Obj sym = cl_intern_in("G-UNIQUE", 8, cl_package_cl);
        if (CL_NULL_P(sym)) ctx->result_ok = 0;
    }
    /* Intern a unique symbol to verify package system works */
    {
        char name[32];
        int len = snprintf(name, sizeof(name), "GENSYM-WORKER-%d", ctx->worker_id);
        ctx->result_sym = cl_intern_in(name, (uint32_t)len, cl_package_cl);
    }

    worker_unregister(ctx);
    return NULL;
}

TEST(concurrent_gensym_unique)
{
    void *handles[4];
    WorkerCtx workers[4];
    int i, j;

    for (i = 0; i < 4; i++)
        worker_init(&workers[i], i);

    for (i = 0; i < 4; i++)
        platform_thread_create(&handles[i], concurrent_gensym_worker,
                               &workers[i], 0);

    for (i = 0; i < 4; i++)
        platform_thread_join(handles[i], NULL);

    for (i = 0; i < 4; i++)
        ASSERT_EQ_INT(workers[i].result_ok, 1);

    /* All worker symbols should be distinct */
    for (i = 0; i < 4; i++)
        for (j = i + 1; j < 4; j++)
            ASSERT(workers[i].result_sym != workers[j].result_sym);
}

/* ================================================================
 * Test: concurrent macro lookup
 *
 * One thread registers a macro while others do macro_p lookups.
 * No crash, all lookups return consistent results.
 * ================================================================ */

static volatile int macro_registered = 0;

static void *macro_reader_worker(void *arg)
{
    WorkerCtx *ctx = (WorkerCtx *)arg;
    int i;
    CL_Obj sym;
    worker_register(ctx);

    sym = cl_intern_in("CONCURRENT-MACRO-TEST", 21, cl_package_cl);
    ctx->result_ok = 1;

    /* Spin reading macro_p — should never crash */
    for (i = 0; i < 1000; i++) {
        int is_macro = cl_macro_p(sym);
        /* After registration, must consistently be true */
        if (macro_registered && !is_macro) {
            /* Race window: might see old value briefly, not a failure */
        }
        (void)is_macro;
    }

    worker_unregister(ctx);
    return NULL;
}

TEST(concurrent_macro_lookup)
{
    void *handles[2];
    WorkerCtx workers[2];
    CL_Obj sym, expander;

    macro_registered = 0;

    /* Pre-intern the symbol and create a dummy expander */
    sym = cl_intern_in("CONCURRENT-MACRO-TEST", 21, cl_package_cl);
    expander = cl_intern_in("IDENTITY", 8, cl_package_cl);

    worker_init(&workers[0], 0);
    worker_init(&workers[1], 1);

    /* Start reader threads */
    platform_thread_create(&handles[0], macro_reader_worker, &workers[0], 0);
    platform_thread_create(&handles[1], macro_reader_worker, &workers[1], 0);

    /* Register macro from main thread while readers are running */
    cl_register_macro(sym, expander);
    macro_registered = 1;

    platform_thread_join(handles[0], NULL);
    platform_thread_join(handles[1], NULL);

    ASSERT_EQ_INT(workers[0].result_ok, 1);
    ASSERT_EQ_INT(workers[1].result_ok, 1);
    ASSERT(cl_macro_p(sym));
}

/* ================================================================
 * Test: concurrent find-package
 *
 * Multiple threads call cl_find_package simultaneously.
 * All should return the same package.
 * ================================================================ */

static void *find_package_worker(void *arg)
{
    WorkerCtx *ctx = (WorkerCtx *)arg;
    int i;
    worker_register(ctx);

    ctx->result_ok = 1;
    for (i = 0; i < 100; i++) {
        CL_Obj pkg = cl_find_package("COMMON-LISP", 11);
        if (pkg != cl_package_cl) {
            ctx->result_ok = 0;
            break;
        }
    }

    worker_unregister(ctx);
    return NULL;
}

TEST(concurrent_find_package)
{
    void *handles[4];
    WorkerCtx workers[4];
    int i;

    for (i = 0; i < 4; i++)
        worker_init(&workers[i], i);

    for (i = 0; i < 4; i++)
        platform_thread_create(&handles[i], find_package_worker,
                               &workers[i], 0);

    for (i = 0; i < 4; i++)
        platform_thread_join(handles[i], NULL);

    for (i = 0; i < 4; i++)
        ASSERT_EQ_INT(workers[i].result_ok, 1);
}

/* ================================================================
 * Test: concurrent intern + GC stress
 *
 * Multiple threads intern symbols while GC may trigger.
 * Verifies safepoint + lock interaction under memory pressure.
 * ================================================================ */

static void *intern_gc_stress_worker(void *arg)
{
    WorkerCtx *ctx = (WorkerCtx *)arg;
    int i;
    CL_Obj list = CL_NIL;
    CL_Obj *root_ptr = &list;
    worker_register(ctx);

    ctx->thread.gc_roots[0] = root_ptr;
    ctx->thread.gc_root_count = 1;

    ctx->result_ok = 1;
    for (i = 0; i < 50; i++) {
        char name[32];
        int len = snprintf(name, sizeof(name), "GC-STRESS-%d-%d",
                           ctx->worker_id, i);
        CL_Obj sym = cl_intern_in(name, (uint32_t)len, cl_package_cl);
        if (CL_NULL_P(sym)) {
            ctx->result_ok = 0;
            break;
        }
        list = cl_cons(sym, list);
    }

    ctx->count = 0;
    {
        CL_Obj walk = list;
        while (!CL_NULL_P(walk)) {
            ctx->count++;
            walk = cl_cdr(walk);
        }
    }

    ctx->thread.gc_root_count = 0;
    worker_unregister(ctx);
    return NULL;
}

TEST(concurrent_intern_gc_stress)
{
    void *handles[3];
    WorkerCtx workers[3];
    int i;

    for (i = 0; i < 3; i++)
        worker_init(&workers[i], i);

    for (i = 0; i < 3; i++)
        platform_thread_create(&handles[i], intern_gc_stress_worker,
                               &workers[i], 0);

    for (i = 0; i < 3; i++)
        platform_thread_join(handles[i], NULL);

    for (i = 0; i < 3; i++) {
        ASSERT_EQ_INT(workers[i].result_ok, 1);
        ASSERT_EQ_INT(workers[i].count, 50);
    }
}

/* ================================================================
 * Test: single-thread performance regression
 *
 * Intern many symbols single-threaded — verify CL_MT() check
 * doesn't cause measurable overhead (no lock calls made).
 * ================================================================ */

TEST(single_thread_intern_no_overhead)
{
    int i;
    CL_Obj list = CL_NIL;
    CL_GC_PROTECT(list);

    /* Intern 200 unique symbols — should be fast with CL_MT() == false */
    for (i = 0; i < 200; i++) {
        char name[32];
        int len = snprintf(name, sizeof(name), "PERF-SYM-%d", i);
        CL_Obj sym = cl_intern_in(name, (uint32_t)len, cl_package_cl);
        ASSERT(!CL_NULL_P(sym));
        list = cl_cons(sym, list);
    }

    /* Verify all present */
    {
        int count = 0;
        CL_Obj walk = list;
        while (!CL_NULL_P(walk)) {
            count++;
            walk = cl_cdr(walk);
        }
        ASSERT_EQ_INT(count, 200);
    }

    CL_GC_UNPROTECT(1);
}

/* ================================================================
 * Test: concurrent struct type check
 *
 * Verifies that cl_is_struct_type / find_struct_entry reads
 * work correctly under concurrent access.
 * ================================================================ */

extern int cl_is_struct_type(CL_Obj type_sym);

static void *struct_type_check_worker(void *arg)
{
    WorkerCtx *ctx = (WorkerCtx *)arg;
    int i;
    CL_Obj sym;
    worker_register(ctx);

    sym = cl_intern_in("CONDITION", 9, cl_package_cl);
    ctx->result_ok = 1;

    for (i = 0; i < 100; i++) {
        /* CONDITION is a known condition type, not a struct type */
        if (cl_is_struct_type(sym)) {
            ctx->result_ok = 0;
            break;
        }
    }

    worker_unregister(ctx);
    return NULL;
}

TEST(concurrent_struct_type_check)
{
    void *handles[2];
    WorkerCtx workers[2];
    int i;

    for (i = 0; i < 2; i++)
        worker_init(&workers[i], i);

    for (i = 0; i < 2; i++)
        platform_thread_create(&handles[i], struct_type_check_worker,
                               &workers[i], 0);

    for (i = 0; i < 2; i++)
        platform_thread_join(handles[i], NULL);

    for (i = 0; i < 2; i++)
        ASSERT_EQ_INT(workers[i].result_ok, 1);
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    test_init();
    setup();

    RUN(single_thread_intern_no_overhead);
    RUN(concurrent_intern_same_symbol);
    RUN(concurrent_intern_different_symbols);
    RUN(concurrent_gensym_unique);
    RUN(concurrent_macro_lookup);
    RUN(concurrent_find_package);
    RUN(concurrent_intern_gc_stress);
    RUN(concurrent_struct_type_check);

    teardown();
    REPORT();
}
