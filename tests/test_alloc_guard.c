#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "platform/platform.h"

/* Regression tests for the cl_alloc oversized-request guard.
 *
 * The >CL_HDR_SIZE_MASK (23-bit header) guard used to run AFTER
 * alloc_from_bump had already advanced the bump pointer.  For a request
 * that fit the remaining bump space (needs an arena larger than 8MB,
 * hence the dedicated 64MB setup here), the guard's cl_storage_error
 * longjmp'd out leaving a headerless region inside the walked bump
 * front: every later arena walk (sweep, mark-overflow rescan,
 * forwarding, slide) desynced at the hole, silently corrupting live
 * objects above it.  Reachable from pure Lisp via (make-array 3000000)
 * — 12MB of bytes.  The guard now runs at cl_alloc entry, BEFORE any
 * allocator state changes. */

static void setup(void)
{
    platform_init();
    cl_thread_init();
    cl_error_init();
    cl_mem_init(64u * 1024 * 1024);
    cl_package_init();
    cl_symbol_init();
}

static void teardown(void)
{
    cl_mem_shutdown();
    platform_shutdown();
}

TEST(oversized_alloc_signals_storage_error)
{
    int err;
    void *p = NULL;
    CL_CATCH(err);
    if (err == CL_ERR_NONE) {
        p = cl_alloc(TYPE_VECTOR, 12u * 1024 * 1024);
        CL_UNCATCH();
    } else {
        CL_UNCATCH();
    }
    ASSERT(p == NULL);
    ASSERT_EQ_INT(err, CL_ERR_STORAGE);
}

TEST(oversized_alloc_does_not_advance_bump)
{
    /* THE regression: the failed allocation must leave the allocator
     * exactly where it was — no headerless hole in the bump front. */
    uint32_t bump_before, allocated_before;
    int err;

    bump_before = cl_heap.bump;
    allocated_before = cl_heap.total_allocated;

    CL_CATCH(err);
    if (err == CL_ERR_NONE) {
        cl_alloc(TYPE_VECTOR, 12u * 1024 * 1024);
        CL_UNCATCH();
    } else {
        CL_UNCATCH();
    }
    ASSERT_EQ_INT(err, CL_ERR_STORAGE);
    ASSERT_EQ_INT((int)(cl_heap.bump - bump_before), 0);
    ASSERT_EQ_INT((int)(cl_heap.total_allocated - allocated_before), 0);
}

TEST(heap_walkable_after_oversized_alloc)
{
    /* Allocate live data, fail an oversized request, allocate more on
     * top, then force a full mark-sweep + compaction cycle.  With the
     * old late guard the walk desynced at the hole between the two
     * generations; with the entry guard everything survives. */
    CL_Obj before = CL_NIL, after = CL_NIL;
    int i, err;

    CL_GC_PROTECT(before);
    CL_GC_PROTECT(after);

    for (i = 0; i < 100; i++)
        before = cl_cons(CL_MAKE_FIXNUM(i), before);

    CL_CATCH(err);
    if (err == CL_ERR_NONE) {
        cl_alloc(TYPE_VECTOR, 12u * 1024 * 1024);
        CL_UNCATCH();
    } else {
        CL_UNCATCH();
    }
    ASSERT_EQ_INT(err, CL_ERR_STORAGE);

    /* cl_storage_error ran cl_gc_reset_roots(), dropping the two
     * protects above (no GC has run since, so the locals are still
     * valid offsets) — re-root them for the collections below. */
    CL_GC_PROTECT(before);
    CL_GC_PROTECT(after);

    for (i = 0; i < 100; i++)
        after = cl_cons(CL_MAKE_FIXNUM(i), after);

    cl_gc();
    cl_gc_compact();

    /* Both generations fully intact and walkable */
    for (i = 99; i >= 0; i--) {
        ASSERT(CL_CONS_P(before));
        ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(before)), i);
        before = cl_cdr(before);
        ASSERT(CL_CONS_P(after));
        ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(after)), i);
        after = cl_cdr(after);
    }
    ASSERT(CL_NULL_P(before));
    ASSERT(CL_NULL_P(after));

    CL_GC_UNPROTECT(2);
}

int main(void)
{
    test_init();
    setup();

    RUN(oversized_alloc_signals_storage_error);
    RUN(oversized_alloc_does_not_advance_bump);
    RUN(heap_walkable_after_oversized_alloc);

    teardown();
    REPORT();
}
