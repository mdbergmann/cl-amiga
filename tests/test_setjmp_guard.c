#include "test.h"
#include "core/types.h"
#include "core/error.h"
#include "platform/platform.h"
#include <setjmp.h>
#include <string.h>

/* Regression test for the MorphOS PPC setjmp buffer-overrun guard.
 *
 * On MorphOS PPC (-noixemul) setjmp() writes a register save area LARGER than
 * the toolchain's jmp_buf typedef reports (the header omits the AltiVec vector
 * registers).  The extra bytes overran whatever struct fields followed an
 * embedded jmp_buf — silently zeroing CL_NLXFrame.vm_sp/vm_fp/tag (breaking
 * tagbody/GO) and CL_ErrorFrame's saved state.  The fix reserves CL_JMPBUF_GUARD
 * dead space immediately after every embedded jmp_buf (types.h) and validates it
 * at boot via cl_setjmp_overrun_check().
 *
 * This bug only manifests on the affected target — on the host the overrun is 0,
 * so these tests can't reproduce the corruption.  What they DO lock in, on every
 * platform, is that the self-check runs to completion (aborts loudly rather than
 * corrupting memory if the guard is ever undersized on the build target) and
 * that a jmp_buf followed by the guard leaves a trailing sentinel intact. */

/* Measure how many bytes setjmp() writes past sizeof(jmp_buf), the same way
 * cl_setjmp_overrun_check does.  Returns 0 on targets with no overrun. */
static int measure_overrun(void)
{
    struct { jmp_buf b; unsigned char tail[1024]; } probe;
    int i, last = -1;
    memset(probe.tail, 0xA5, sizeof(probe.tail));
    (void)CL_SETJMP(probe.b);
    for (i = 0; i < (int)sizeof(probe.tail); i++)
        if (probe.tail[i] != 0xA5) last = i;
    return last + 1;
}

TEST(guard_covers_actual_setjmp_overrun)
{
    /* THE invariant: the space actually reserved on this build target
     * (CL_JMPBUF_GUARD_RESERVED_BYTES — 0 off MorphOS, CL_JMPBUF_GUARD_BYTES
     * on MorphOS; see types.h) must be at least as large as the real overrun.
     * cl_setjmp_overrun_check() aborts the process if this is violated, so
     * reaching the assert already proves it — we re-measure here to document
     * the numbers and fail cleanly if not.  Comparing against the bare
     * CL_JMPBUF_GUARD_BYTES constant instead would wrongly pass on a
     * non-MorphOS target with a nonzero overrun, since CL_JMPBUF_GUARD
     * reserves 0 bytes there. */
    int overrun = measure_overrun();
    ASSERT(overrun <= CL_JMPBUF_GUARD_RESERVED_BYTES);
}

TEST(self_check_completes_without_abort)
{
    /* If the guard were undersized on this platform, cl_setjmp_overrun_check
     * would abort() and the test binary would never reach REPORT().  Reaching
     * the assert below is the pass condition. */
    cl_setjmp_overrun_check();
    ASSERT(1);
}

TEST(sentinel_after_guarded_jmpbuf_survives)
{
    /* A jmp_buf followed by CL_JMPBUF_GUARD and then a live field: the field
     * must survive setjmp() regardless of how far the overrun reaches.  This
     * mirrors the CL_NLXFrame / CL_ErrorFrame layout. */
    struct { jmp_buf b; CL_JMPBUF_GUARD volatile uint32_t sentinel; } frame;
    frame.sentinel = 0xC0FFEEu;
    (void)CL_SETJMP(frame.b);
    ASSERT_EQ_INT((int)frame.sentinel, (int)0xC0FFEEu);
}

TEST(thread_init_saves_and_restores_main_tls)
{
    /* cl_thread_init() overwrites this task's TLS slot (tc_UserData on
     * AmigaOS/MorphOS; a plain __thread var on POSIX) with a CL_Thread*, but
     * must first save whatever was there so cl_thread_restore_main_tls() can
     * put it back before the process exits — otherwise the MorphOS
     * -noixemul crt0's post-main teardown re-reads tc_UserData, finds our
     * stale CL_Thread*, and freezes the machine.  This runs on every
     * platform: platform_tls_get/set work on POSIX too, and
     * cl_thread_restore_main_tls() is documented as safe/idempotent
     * everywhere. */
    void *before = platform_tls_get();
    cl_thread_init();
    ASSERT(platform_tls_get() != before);
    cl_thread_restore_main_tls();
    ASSERT(platform_tls_get() == before);
}

int main(void)
{
    test_init();
    platform_init();

    RUN(guard_covers_actual_setjmp_overrun);
    RUN(self_check_completes_without_abort);
    RUN(sentinel_after_guarded_jmpbuf_survives);
    RUN(thread_init_saves_and_restores_main_tls);

    platform_shutdown();
    REPORT();
}
