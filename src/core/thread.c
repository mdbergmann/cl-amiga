/*
 * thread.c — Thread system initialization (Phase 1: TLS-backed).
 *
 * Allocates and initializes the main thread's CL_Thread struct.
 * cl_current_thread is now TLS-backed via platform_tls_get/set.
 * All per-thread state lives here; compatibility macros in thread.h
 * redirect legacy global names to cl_current_thread->field.
 */

#define CL_THREAD_NO_MACROS  /* access struct members directly */
#include "thread.h"
#include "../platform/platform.h"
#include "../platform/platform_thread.h"
#include <string.h>

static CL_Thread cl_main_thread;
CL_Thread *cl_main_thread_ptr = NULL;

CL_Thread *cl_get_current_thread(void)
{
    return (CL_Thread *)platform_tls_get();
}

void cl_thread_init(void)
{
    memset(&cl_main_thread, 0, sizeof(CL_Thread));

    /* Initialize TLS and set main thread as current */
    platform_tls_init();
    platform_tls_set(&cl_main_thread);
    cl_main_thread_ptr = &cl_main_thread;

    /* Allocate NLX stack */
    cl_main_thread.nlx_stack = (CL_NLXFrame *)platform_alloc(
        CL_MAX_NLX_FRAMES * sizeof(CL_NLXFrame));
    cl_main_thread.nlx_max = CL_MAX_NLX_FRAMES;
    cl_main_thread.nlx_top = 0;

    /* Default: single-value mode */
    cl_main_thread.mv_count = 1;

    /* Mark as running */
    cl_main_thread.id = 0;
    cl_main_thread.status = 1; /* running */
}

void cl_thread_shutdown(void)
{
    if (cl_main_thread.nlx_stack) {
        platform_free(cl_main_thread.nlx_stack);
        cl_main_thread.nlx_stack = NULL;
    }
    cl_main_thread_ptr = NULL;
}
