/*
 * thread.c — Thread system initialization (Phase 0: single thread).
 *
 * Allocates and initializes the main thread's CL_Thread struct.
 * All per-thread state lives here; compatibility macros in thread.h
 * redirect legacy global names to cl_current_thread->field.
 */

#define CL_THREAD_NO_MACROS  /* access struct members directly */
#include "thread.h"
#include "../platform/platform.h"
#include <string.h>

static CL_Thread cl_main_thread;
CL_Thread *cl_current_thread = NULL;

void cl_thread_init(void)
{
    memset(&cl_main_thread, 0, sizeof(CL_Thread));
    cl_current_thread = &cl_main_thread;

    /* Allocate NLX stack */
    cl_main_thread.nlx_stack = (CL_NLXFrame *)platform_alloc(
        CL_MAX_NLX_FRAMES * sizeof(CL_NLXFrame));
    cl_main_thread.nlx_max = CL_MAX_NLX_FRAMES;
    cl_main_thread.nlx_top = 0;

    /* Default: single-value mode */
    cl_main_thread.mv_count = 1;
}

void cl_thread_shutdown(void)
{
    if (cl_main_thread.nlx_stack) {
        platform_free(cl_main_thread.nlx_stack);
        cl_main_thread.nlx_stack = NULL;
    }
}
