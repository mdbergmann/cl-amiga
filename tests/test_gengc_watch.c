/*
 * test_gengc_watch.c — platform page write-watch (generational GC dirty
 * tracking infrastructure).
 *
 * The generational collector protects old-space pages read-only after each
 * GC; the first store to a clean page must fault into the handler, set the
 * page's dirty bit, re-enable writes, and let the store retry — all
 * transparently to the mutator.  These tests exercise the raw platform
 * layer (mmap arena, fault handler, bitmap, protection toggles) before any
 * GC logic sits on top of it.
 */

#include "test.h"
#include "platform/platform.h"

#include <string.h>
#include <stdint.h>

#define REGION_PAGES 8

static uint8_t *region = NULL;
static uint32_t pagesz = 0;
static uint32_t region_len = 0;
/* 1 bit per page; one byte is plenty for 8 pages but keep it sized. */
static volatile uint8_t dirty[(REGION_PAGES + 7) / 8];

static int bit(int page)
{
    return (dirty[page >> 3] >> (page & 7)) & 1;
}

TEST(alloc_pages_aligned_and_zeroed)
{
    uint32_t i;
    pagesz = platform_page_size();
    ASSERT(pagesz >= 4096);
    region_len = REGION_PAGES * pagesz;
    region = (uint8_t *)platform_alloc_pages(region_len);
    ASSERT(region != NULL);
    ASSERT_EQ_INT((int)((uintptr_t)region & (pagesz - 1)), 0);
    for (i = 0; i < region_len; i += pagesz)
        ASSERT_EQ_INT(region[i], 0);
}

TEST(watch_install)
{
    memset((void *)dirty, 0, sizeof(dirty));
    ASSERT_EQ_INT(platform_write_watch_install(region, region_len, dirty), 0);
}

TEST(protected_write_faults_dirties_and_retries)
{
    volatile uint8_t *p2 = region + 2 * pagesz;

    /* Protect pages 2 and 3 read-only. */
    ASSERT_EQ_INT(platform_page_protect(region + 2 * pagesz, 2 * pagesz, 1), 0);

    ASSERT_EQ_INT(bit(2), 0);
    *p2 = 0xAB;                    /* faults -> handler -> retries */
    ASSERT_EQ_INT(*p2, 0xAB);      /* the store went through */
    ASSERT_EQ_INT(bit(2), 1);      /* and was recorded */
    ASSERT_EQ_INT(bit(3), 0);      /* sibling page untouched */

    /* Second store to the now-RW page: no fault, bit simply stays set. */
    *p2 = 0xCD;
    ASSERT_EQ_INT(*p2, 0xCD);
    ASSERT_EQ_INT(bit(2), 1);
}

TEST(unprotected_write_does_not_dirty)
{
    volatile uint8_t *p5 = region + 5 * pagesz;
    *p5 = 0x11;                    /* page 5 was never protected */
    ASSERT_EQ_INT(*p5, 0x11);
    ASSERT_EQ_INT(bit(5), 0);      /* dirty bits come only from faults */
}

TEST(each_protected_page_faults_independently)
{
    volatile uint8_t *p3 = region + 3 * pagesz + 100;
    *p3 = 0x77;                    /* page 3 still protected from above */
    ASSERT_EQ_INT(*p3, 0x77);
    ASSERT_EQ_INT(bit(3), 1);
}

TEST(reprotect_after_manual_unprotect_faults_again)
{
    volatile uint8_t *p2 = region + 2 * pagesz + 8;

    /* Simulate the post-GC cycle: clear bits, protect old space again. */
    memset((void *)dirty, 0, sizeof(dirty));
    ASSERT_EQ_INT(platform_page_protect(region, 4 * pagesz, 1), 0);

    *p2 = 0x42;
    ASSERT_EQ_INT(*p2, 0x42);
    ASSERT_EQ_INT(bit(2), 1);
    ASSERT_EQ_INT(bit(0), 0);
    ASSERT_EQ_INT(bit(1), 0);
    ASSERT_EQ_INT(bit(3), 0);

    /* GC-side bulk unprotect (readonly=0) must not fault or dirty. */
    ASSERT_EQ_INT(platform_page_protect(region, 4 * pagesz, 0), 0);
    region[0] = 0x01;
    ASSERT_EQ_INT(bit(0), 0);
}

TEST(watch_remove_and_reinstall)
{
    volatile uint8_t *p1 = region + 1 * pagesz;

    platform_write_watch_remove();

    /* Re-install and verify the handler still works. */
    memset((void *)dirty, 0, sizeof(dirty));
    ASSERT_EQ_INT(platform_write_watch_install(region, region_len, dirty), 0);
    ASSERT_EQ_INT(platform_page_protect(region + pagesz, pagesz, 1), 0);
    *p1 = 0x99;
    ASSERT_EQ_INT(*p1, 0x99);
    ASSERT_EQ_INT(bit(1), 1);

    platform_write_watch_remove();
    ASSERT_EQ_INT(platform_page_protect(region + pagesz, pagesz, 0), 0);
    platform_free_pages(region, region_len);
    region = NULL;
}

int main(void)
{
    test_init();

    RUN(alloc_pages_aligned_and_zeroed);
    RUN(watch_install);
    RUN(protected_write_faults_dirties_and_retries);
    RUN(unprotected_write_does_not_dirty);
    RUN(each_protected_page_faults_independently);
    RUN(reprotect_after_manual_unprotect_faults_again);
    RUN(watch_remove_and_reinstall);

    REPORT();
}
