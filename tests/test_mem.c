#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "core/thread.h"
#include "platform/platform.h"

/* thread.h defines gc_root_count as (CT->gc_root_count) for the common
 * case of accessing the *current* thread's root stack; undef it here so
 * tests can address a specific CL_Thread's gc_root_count field by name. */
#undef gc_root_count

static void setup(void)
{
    platform_init();
    cl_thread_init();
    cl_error_init();
    cl_mem_init(CL_DEFAULT_HEAP_SIZE);
    cl_package_init();
    cl_symbol_init();
}

static void teardown(void)
{
    cl_mem_shutdown();
    platform_shutdown();
}

TEST(alloc_cons)
{
    CL_Obj c = cl_cons(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(2));
    ASSERT(!CL_NULL_P(c));
    ASSERT(CL_CONS_P(c));
}

TEST(alloc_many_conses)
{
    int i;
    CL_Obj list = CL_NIL;
    for (i = 0; i < 1000; i++) {
        list = cl_cons(CL_MAKE_FIXNUM(i), list);
    }
    /* Verify first and last */
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(list)), 999);
    /* Walk to end */
    for (i = 0; i < 999; i++) list = cl_cdr(list);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(list)), 0);
    ASSERT(CL_NULL_P(cl_cdr(list)));
}

TEST(alloc_string)
{
    CL_Obj s = cl_make_string("test", 4);
    CL_String *str = (CL_String *)CL_OBJ_TO_PTR(s);
    ASSERT(!CL_NULL_P(s));
    ASSERT_EQ_INT((int)str->length, 4);
    ASSERT_STR_EQ(str->data, "test");
}

TEST(alloc_vector)
{
    CL_Obj v = cl_make_vector(10);
    CL_Vector *vec;
    ASSERT(!CL_NULL_P(v));
    ASSERT(CL_VECTOR_P(v));
    vec = (CL_Vector *)CL_OBJ_TO_PTR(v);
    ASSERT_EQ_INT((int)vec->length, 10);
    /* All elements should be NIL */
    ASSERT(CL_NULL_P(cl_vector_data(vec)[0]));
    ASSERT(CL_NULL_P(cl_vector_data(vec)[9]));
}

TEST(gc_basic)
{
    /* Allocate some objects, run GC, check nothing crashes */
    CL_Obj kept = cl_cons(CL_MAKE_FIXNUM(42), CL_NIL);
    int i;

    CL_GC_PROTECT(kept);

    /* Allocate garbage */
    for (i = 0; i < 100; i++) {
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);
    }

    cl_gc();

    /* Protected object should survive */
    ASSERT(CL_CONS_P(kept));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(kept)), 42);

    CL_GC_UNPROTECT(1);
}

TEST(gc_chain)
{
    /* Build a list, protect head, GC should keep entire chain */
    CL_Obj list = CL_NIL;
    int i;

    for (i = 0; i < 50; i++) {
        list = cl_cons(CL_MAKE_FIXNUM(i), list);
    }
    CL_GC_PROTECT(list);

    /* Create garbage */
    for (i = 0; i < 200; i++) {
        cl_cons(CL_MAKE_FIXNUM(i + 1000), CL_NIL);
    }

    cl_gc();

    /* Verify list is intact */
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(list)), 49);
    for (i = 0; i < 49; i++) list = cl_cdr(list);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(list)), 0);

    CL_GC_UNPROTECT(1);
}

TEST(gc_reclaims_memory)
{
    /* Allocate far more garbage than fits in heap.
     * With 4MB heap, each cons is 16 bytes, so ~250K conses fill the heap.
     * Allocating 1M conses requires GC to reclaim dead objects multiple times.
     * Before the coalescing fix, overlapping free list entries caused
     * corruption and "Heap exhausted" errors. */
    CL_Obj kept = cl_cons(CL_MAKE_FIXNUM(42), CL_NIL);
    uint32_t gc_before;
    int i;

    CL_GC_PROTECT(kept);
    gc_before = cl_heap.gc_count;

    for (i = 0; i < 1000000; i++) {
        CL_Obj c = cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);
        ASSERT(!CL_NULL_P(c));  /* Must never fail */
        (void)c;
    }

    /* GC must have run at least once */
    ASSERT(cl_heap.gc_count > gc_before);

    /* Protected object must survive all GC cycles intact */
    ASSERT(CL_CONS_P(kept));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(kept)), 42);

    CL_GC_UNPROTECT(1);
}

TEST(gc_coalesce_no_overlap)
{
    /* Verify that after GC with adjacent dead objects, the free list
     * has no overlapping blocks. Allocate a batch of garbage, GC,
     * then verify we can fill the freed space without corruption. */
    CL_Obj sentinel = cl_cons(CL_MAKE_FIXNUM(99), CL_NIL);
    int i;
    uint32_t alloc_before, alloc_after;

    CL_GC_PROTECT(sentinel);

    /* Fill heap with garbage conses */
    for (i = 0; i < 200000; i++) {
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);
    }

    cl_gc();
    alloc_before = cl_heap.total_allocated;

    /* Re-allocate into freed space — should not corrupt sentinel */
    for (i = 0; i < 200000; i++) {
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);
    }

    cl_gc();
    alloc_after = cl_heap.total_allocated;

    /* Sentinel must survive both GC cycles */
    ASSERT(CL_CONS_P(sentinel));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(sentinel)), 99);

    /* Allocation counts should be similar (same live set) */
    ASSERT(alloc_after <= alloc_before + 1024);

    CL_GC_UNPROTECT(1);
}

TEST(gc_root_registries_no_double_registration)
{
    /* Regression: cl_package_init pushed 8 package roots (9 on Amiga)
     * but popped only 6 (7), leaving &cl_package_cl and
     * &cl_package_keyword on the main thread's root stack while the
     * same addresses were ALSO registered via cl_gc_register_root.
     * gc_forward is not idempotent, so on a compaction that moved
     * either package the doubly-reachable slot was forwarded twice
     * into an unrelated object.  The audit must be clean after init. */
    ASSERT_EQ_INT(cl_gc_audit_roots(), 0);
}

TEST(gc_root_audit_detects_same_thread_duplicate)
{
    /* Regression: cl_gc_audit_roots only checked global-vs-global and
     * global-vs-thread duplicates, missing an accidental double
     * CL_GC_PROTECT of the same variable on one thread's own gc_roots[]
     * stack -- the same non-idempotent-gc_forward hazard. */
    CL_Obj slot = CL_NIL;

    CL_GC_PROTECT(slot);
    CL_GC_PROTECT(slot);
    ASSERT(cl_gc_audit_roots() >= 1);
    CL_GC_UNPROTECT(2);

    ASSERT_EQ_INT(cl_gc_audit_roots(), 0);
}

TEST(gc_root_audit_detects_cross_thread_duplicate)
{
    /* Regression: cl_gc_audit_roots never compared two different threads'
     * root stacks against each other, missing the same slot address
     * registered on both -- also a double-forward hazard. */
    static CL_Thread fake_thread;
    CL_Obj slot = CL_NIL;

    memset(&fake_thread, 0, sizeof(fake_thread));

    CL_GC_PROTECT(slot);

    fake_thread.gc_roots[0] = &slot;
    fake_thread.gc_root_count = 1;
    fake_thread.next = cl_thread_list;
    cl_thread_list = &fake_thread;

    ASSERT(cl_gc_audit_roots() >= 1);

    cl_thread_list = fake_thread.next;
    CL_GC_UNPROTECT(1);

    ASSERT_EQ_INT(cl_gc_audit_roots(), 0);
}

TEST(gc_root_audit_stops_the_world_when_multithreaded)
{
    /* Regression: cl_gc_audit_roots walked cl_thread_list and every
     * thread's gc_roots[]/gc_root_count without any synchronization,
     * racing thread create/exit (t->next) and peer CL_GC_PROTECT/UNPROTECT
     * (gc_roots[]/gc_root_count) whenever cl_thread_count > 1. It must
     * now bracket the walk with cl_gc_stop_the_world()/resume so the
     * traversal only ever runs single-threaded from the collector's
     * point of view. With only the current thread registered, forcing
     * cl_thread_count > 1 must not hang (no peer thread to wait for) and
     * the audit result must be unaffected. */
    cl_thread_count = 2;

    ASSERT_EQ_INT(cl_gc_audit_roots(), 0);

    cl_thread_count = 1;
}

TEST(heap_stats)
{
    /* Verify arena is initialized and stats don't crash */
    cl_mem_stats();
    ASSERT(cl_heap.arena != NULL);
    ASSERT(cl_heap.arena_size == CL_DEFAULT_HEAP_SIZE);
    ASSERT(cl_heap.bump > 0);  /* Some objects allocated during init */
}

int main(void)
{
    test_init();
    setup();

    RUN(alloc_cons);
    RUN(alloc_many_conses);
    RUN(alloc_string);
    RUN(alloc_vector);
    RUN(gc_basic);
    RUN(gc_chain);
    RUN(gc_reclaims_memory);
    RUN(gc_coalesce_no_overlap);
    RUN(gc_root_registries_no_double_registration);
    RUN(gc_root_audit_detects_same_thread_duplicate);
    RUN(gc_root_audit_detects_cross_thread_duplicate);
    RUN(gc_root_audit_stops_the_world_when_multithreaded);
    RUN(heap_stats);

    teardown();
    REPORT();
}
