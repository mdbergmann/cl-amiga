#include "error.h"
#include "mem.h"
#include "vm.h"
#include "debugger.h"
#include "color.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

CL_ErrorFrame cl_error_frames[CL_MAX_ERROR_FRAMES];
int cl_error_frame_top = 0;
int cl_error_code = CL_ERR_NONE;
char cl_error_msg[512];
int cl_exit_code = 0;

void cl_error_init(void)
{
    cl_error_frame_top = 0;
    cl_error_code = CL_ERR_NONE;
    cl_error_msg[0] = '\0';
}

void cl_error(int code, const char *fmt, ...)
{
    va_list ap;

    cl_error_code = code;

    va_start(ap, fmt);
    vsnprintf(cl_error_msg, sizeof(cl_error_msg), fmt, ap);
    va_end(ap);

    /* Exit request: skip debugger/conditions, just unwind */
    if (code == CL_ERR_EXIT) {
        cl_nlx_top = 0;
        cl_pending_throw = 0;
        cl_dynbind_restore_to(0);
        cl_handler_top = 0;
        cl_restart_top = 0;
        cl_gc_reset_roots();
        if (cl_error_frame_top > 0) {
            cl_error_frame_top--;
            cl_error_frames[cl_error_frame_top].active = 0;
            longjmp(cl_error_frames[cl_error_frame_top].buf, code);
        }
        exit(cl_exit_code);
    }

    /* Capture backtrace while VM frames are still intact */
    cl_capture_backtrace();

    /* Signal through condition handler stack before unwinding */
    {
        CL_Obj cond = cl_create_condition_from_error(code, cl_error_msg);
        cl_signal_condition(cond);
        /* Invoke debugger before unwinding (returns if user picks "top level") */
        cl_invoke_debugger(cond);
    }

    /* Check for interposing unwind-protect frames in NLX stack */
    {
        int i;
        for (i = cl_nlx_top - 1; i >= 0; i--) {
            if (cl_nlx_stack[i].type == CL_NLX_UWPROT) {
                /* Set pending error, longjmp to UWPROT cleanup */
                cl_pending_throw = 2;
                cl_pending_error_code = code;
                strncpy(cl_pending_error_msg, cl_error_msg,
                        sizeof(cl_pending_error_msg) - 1);
                cl_pending_error_msg[sizeof(cl_pending_error_msg) - 1] = '\0';
                cl_nlx_top = i;
                longjmp(cl_nlx_stack[i].buf, 1);
            }
        }
    }

    /* No UWPROT found — propagating to C error handler. */
    if (cl_error_frame_top > 1) {
        /* Nested error frame — jump to it without destroying global state.
         * The caller is responsible for restoring VM/binding state. */
        cl_error_frame_top--;
        cl_error_frames[cl_error_frame_top].active = 0;
        longjmp(cl_error_frames[cl_error_frame_top].buf, code);
    }

    /* Outermost error frame (REPL) — full cleanup.
     * NLX frames (catch/uwprot) are invalid once we leave the VM,
     * so clear the NLX stack and reset pending state.
     * Restore all dynamic bindings before leaving the VM.
     * Reset GC root stack — longjmp invalidates stack-local roots. */
    cl_nlx_top = 0;
    cl_pending_throw = 0;
    cl_dynbind_restore_to(0);
    cl_handler_top = 0;
    cl_restart_top = 0;
    cl_gc_reset_roots();

    if (cl_error_frame_top > 0) {
        cl_error_frame_top--;
        cl_error_frames[cl_error_frame_top].active = 0;
        longjmp(cl_error_frames[cl_error_frame_top].buf, code);
    }

    /* No error frame — fatal */
    cl_color_set(CL_COLOR_RED);
    platform_write_string("FATAL ERROR: ");
    platform_write_string(cl_error_msg);
    cl_color_reset();
    platform_write_string("\n");
    exit(1);
}

void cl_error_print(void)
{
    cl_color_set(CL_COLOR_RED);
    platform_write_string("ERROR: ");
    platform_write_string(cl_error_msg);
    cl_color_reset();
    platform_write_string("\n");
    if (cl_backtrace_buf[0] != '\0') {
        platform_write_string("Backtrace:\n");
        platform_write_string(cl_backtrace_buf);
    }
}
