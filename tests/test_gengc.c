/*
 * test_gengc.c — generational collector semantics (CL_GENGC, host-only).
 *
 * Covers the load-bearing behaviors on top of the write-watch platform
 * layer (tested in test_gengc_watch.c):
 *   - minor collection promotes survivors below the watermark and resets
 *     the nursery bump (young garbage costs nothing to reclaim)
 *   - the old->young dirty-page path: a young object reachable ONLY from
 *     a mutated old object must survive a minor (fault handler -> dirty
 *     bitmap -> precise page scan), and the old slot must be forwarded
 *   - protected C roots are forwarded across minors
 *   - a major (compaction) resets the watermark and re-arms protection
 *   - CLAMIGA_GENGC=0 pins the classic collector
 *   - heap re-init tears gen state down cleanly (stale-static bug class)
 *
 * On classic-only builds (no CL_GENGC) the whole suite compiles to a
 * trivial pass.
 */

#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "core/thread.h"
#include "platform/platform.h"

#include <string.h>
#include <stdlib.h>

static void setup(void)
{
    unsetenv("CLAMIGA_GENGC");
    platform_init();
    cl_thread_init();
    cl_error_init();
    cl_mem_init(4 * 1024 * 1024);
    cl_package_init();
    cl_symbol_init();
}

static void teardown(void)
{
    cl_mem_shutdown();
    cl_thread_shutdown();
    platform_shutdown();
}

#ifdef CL_GENGC

TEST(gen_enabled_by_default_on_host)
{
    ASSERT_EQ_INT(cl_gengc_enabled(), 1);
}

TEST(minor_promotes_survivor_and_resets_bump)
{
    CL_Obj keep, probe;
    uint32_t old_top0, old_top1, bump_after, i;

    keep = cl_cons(CL_MAKE_FIXNUM(7), CL_NIL);
    CL_GC_PROTECT(keep);

    cl_gengc_stats(NULL, NULL, NULL, &old_top0, NULL);
    ASSERT_EQ_INT(cl_gc_minor(cl_heap.gc_count), 1);
    cl_gengc_stats(NULL, NULL, NULL, &old_top1, NULL);

    /* Survivor was promoted: it now lives below the watermark, which
     * advanced by (at least) its size; the nursery bump was reset onto
     * the watermark. */
    ASSERT(old_top1 > old_top0);
    ASSERT(keep < old_top1);
    ASSERT_EQ((unsigned long)cl_heap.bump, (unsigned long)old_top1);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(keep)), 7);

    /* Pure garbage must not advance the watermark measurably: cons ~64K
     * of unreachable pairs, minor, and require the watermark to grow by
     * less than a fraction of that (roots may pin a few incidentals). */
    for (i = 0; i < 4096; i++)
        probe = cl_cons(CL_MAKE_FIXNUM((int32_t)i), CL_NIL);
    (void)probe;
    probe = CL_NIL;
    old_top0 = old_top1;
    ASSERT_EQ_INT(cl_gc_minor(cl_heap.gc_count), 1);
    cl_gengc_stats(NULL, NULL, NULL, &old_top1, NULL);
    bump_after = cl_heap.bump;
    ASSERT_EQ((unsigned long)bump_after, (unsigned long)old_top1);
    ASSERT(old_top1 - old_top0 < 4096u * 16u / 4u);

    CL_GC_UNPROTECT(1);
}

TEST(dirty_page_keeps_old_to_young_reference_alive)
{
    CL_Obj vec, young, reloaded;
    CL_Vector *v;
    uint32_t old_top;

    /* Make an old vector: allocate, then minor-promote it. */
    vec = cl_make_vector(8);
    CL_GC_PROTECT(vec);
    ASSERT_EQ_INT(cl_gc_minor(cl_heap.gc_count), 1);
    cl_gengc_stats(NULL, NULL, NULL, &old_top, NULL);
    ASSERT(vec < old_top);   /* it is old space now (and protected) */

    /* Store a nursery cons into the old vector.  This very store faults
     * on the read-only old page; the write-watch handler dirties the
     * page and retries it.  NOTE: `young` is deliberately NOT protected —
     * the mutated old object is its only reference. */
    young = cl_cons(CL_MAKE_FIXNUM(1234), CL_NIL);
    v = (CL_Vector *)CL_OBJ_TO_PTR(vec);
    v->data[0] = young;
    young = CL_NIL;

    /* Minor: the dirty-page scan must find, mark, promote the cons and
     * forward the old vector's slot. */
    ASSERT_EQ_INT(cl_gc_minor(cl_heap.gc_count), 1);

    v = (CL_Vector *)CL_OBJ_TO_PTR(vec);   /* re-derive after a move */
    reloaded = v->data[0];
    ASSERT(CL_CONS_P(reloaded));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(reloaded)), 1234);
    cl_gengc_stats(NULL, NULL, NULL, &old_top, NULL);
    ASSERT(reloaded < old_top);            /* promoted */

    CL_GC_UNPROTECT(1);
}

TEST(unreferenced_young_dies_in_minor)
{
    CL_Obj vec, young;
    CL_Vector *v;
    uint32_t old_top0, old_top1;

    /* Old vector whose young ref is CLEARED again before the minor: the
     * page is dirty, but the scan finds no young pointer — the cons must
     * NOT be retained (no false liveness from the dirty machinery). */
    vec = cl_make_vector(4);
    CL_GC_PROTECT(vec);
    ASSERT_EQ_INT(cl_gc_minor(cl_heap.gc_count), 1);

    young = cl_cons(CL_MAKE_FIXNUM(1), CL_NIL);
    v = (CL_Vector *)CL_OBJ_TO_PTR(vec);
    v->data[0] = young;      /* dirties the page */
    v->data[0] = CL_NIL;     /* ...but the reference is gone again */
    young = CL_NIL;

    cl_gengc_stats(NULL, NULL, NULL, &old_top0, NULL);
    ASSERT_EQ_INT(cl_gc_minor(cl_heap.gc_count), 1);
    cl_gengc_stats(NULL, NULL, NULL, &old_top1, NULL);
    /* Nothing (beyond incidental root-reachable allocs) promoted. */
    ASSERT(old_top1 - old_top0 < 512);

    CL_GC_UNPROTECT(1);
}

TEST(major_resets_watermark_to_live_top)
{
    CL_Obj keep;
    uint32_t old_top, i;

    keep = cl_cons(CL_MAKE_FIXNUM(9), CL_NIL);
    CL_GC_PROTECT(keep);

    /* Grow old space with garbage that survives minors only because it
     * was live at promotion time... simplest: promote real garbage by
     * keeping it reachable across the minor, then dropping it. */
    {
        CL_Obj hold = CL_NIL;
        CL_GC_PROTECT(hold);
        for (i = 0; i < 2048; i++)
            hold = cl_cons(CL_MAKE_FIXNUM((int32_t)i), hold);
        ASSERT_EQ_INT(cl_gc_minor(cl_heap.gc_count), 1);  /* promotes list */
        hold = CL_NIL;                                    /* now floating */
        CL_GC_UNPROTECT(1);
    }
    cl_gengc_stats(NULL, NULL, NULL, &old_top, NULL);
    ASSERT(old_top > 2048u * 16u);   /* floating garbage sits in old space */

    /* A major compaction collects the floating old garbage and resets the
     * watermark to the true live top. */
    cl_gc_compact();
    cl_gengc_stats(NULL, NULL, NULL, &old_top, NULL);
    ASSERT_EQ((unsigned long)old_top, (unsigned long)cl_heap.bump);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(keep)), 9);

    /* And minors keep working after the reset (fresh crossing map). */
    ASSERT_EQ_INT(cl_gc_minor(cl_heap.gc_count), 1);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(keep)), 9);

    CL_GC_UNPROTECT(1);
}

TEST(minor_stats_accumulate)
{
    uint32_t minors0, minors1;
    uint64_t promoted0, promoted1;
    CL_Obj keep = cl_make_string("promote-me", 10);
    CL_GC_PROTECT(keep);
    cl_gengc_stats(&minors0, NULL, &promoted0, NULL, NULL);
    ASSERT_EQ_INT(cl_gc_minor(cl_heap.gc_count), 1);
    cl_gengc_stats(&minors1, NULL, &promoted1, NULL, NULL);
    ASSERT_EQ_INT((int)(minors1 - minors0), 1);
    ASSERT(promoted1 > promoted0);
    CL_GC_UNPROTECT(1);
}

TEST(gc_count_advances_on_minor_and_epoch_dedup_applies)
{
    uint32_t gc0 = cl_heap.gc_count;
    ASSERT_EQ_INT(cl_gc_minor(gc0), 1);
    ASSERT_EQ_INT((int)(cl_heap.gc_count - gc0), 1);
    /* Single-threaded: no dedup (no race is possible) — it runs again. */
    ASSERT_EQ_INT(cl_gc_minor(gc0), 1);
    ASSERT_EQ_INT((int)(cl_heap.gc_count - gc0), 2);
}

TEST(env_kill_switch_pins_classic_collector)
{
    cl_mem_shutdown();
    setenv("CLAMIGA_GENGC", "0", 1);
    cl_mem_init(4 * 1024 * 1024);
    ASSERT_EQ_INT(cl_gengc_enabled(), 0);
    ASSERT_EQ_INT(cl_gc_minor(cl_heap.gc_count), 0);  /* refused */
    /* Classic cl_gc sweeps (no compaction requirement); smoke it. */
    cl_gc();
    cl_gc();
    /* Restore gen mode with a fresh heap. */
    cl_mem_shutdown();
    unsetenv("CLAMIGA_GENGC");
    cl_mem_init(4 * 1024 * 1024);
    ASSERT_EQ_INT(cl_gengc_enabled(), 1);
}

TEST(repeated_reinit_tears_gen_state_down)
{
    int i;
    for (i = 0; i < 5; i++) {
        CL_Obj o;
        cl_mem_shutdown();
        cl_mem_init(2 * 1024 * 1024);
        ASSERT_EQ_INT(cl_gengc_enabled(), 1);
        o = cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);
        CL_GC_PROTECT(o);
        ASSERT_EQ_INT(cl_gc_minor(cl_heap.gc_count), 1);
        cl_gc_compact();
        ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(o)), i);
        CL_GC_UNPROTECT(1);
    }
    /* Leave a normal-sized heap for any later tests. */
    cl_mem_shutdown();
    cl_mem_init(4 * 1024 * 1024);
}

TEST(pretouch_is_safe_on_protected_old_range)
{
    /* cl_gc_pretouch must make a protected old range kernel-writable
     * without tripping anything; correctness of the range is covered by
     * the dirty-page test (the bits it sets feed the same scan). */
    CL_Obj str = cl_make_string("pretouch-target", 15);
    CL_String *s;
    CL_GC_PROTECT(str);
    ASSERT_EQ_INT(cl_gc_minor(cl_heap.gc_count), 1);   /* promote + protect */
    s = (CL_String *)CL_OBJ_TO_PTR(str);
    cl_gc_pretouch(s->data, 15);
    memcpy(s->data, "PRETOUCH-TARGET", 15);            /* must not fault */
    ASSERT(memcmp(s->data, "PRETOUCH-TARGET", 15) == 0);
    CL_GC_UNPROTECT(1);
}

#else /* !CL_GENGC */

TEST(gengc_not_compiled_in)
{
    ASSERT(1);
}

#endif /* CL_GENGC */

int main(void)
{
    test_init();
    setup();

#ifdef CL_GENGC
    RUN(gen_enabled_by_default_on_host);
    RUN(minor_promotes_survivor_and_resets_bump);
    RUN(dirty_page_keeps_old_to_young_reference_alive);
    RUN(unreferenced_young_dies_in_minor);
    RUN(major_resets_watermark_to_live_top);
    RUN(minor_stats_accumulate);
    RUN(gc_count_advances_on_minor_and_epoch_dedup_applies);
    RUN(env_kill_switch_pins_classic_collector);
    RUN(repeated_reinit_tears_gen_state_down);
    RUN(pretouch_is_safe_on_protected_old_range);
#else
    RUN(gengc_not_compiled_in);
#endif

    teardown();
    REPORT();
}
