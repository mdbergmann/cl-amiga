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

/* All error state now lives in CL_Thread.
 * Compatibility macros in thread.h redirect the old names. */

int cl_error_frame_push(void)
{
    if (cl_error_frame_top >= CL_MAX_ERROR_FRAMES) return -1;
    cl_error_frames[cl_error_frame_top].active = 1;
    return cl_error_frame_top++;
}

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

    /* Check for interposing unwind-protect frames in NLX stack.
     * Skip stale frames whose VM frame was reused by a tail call —
     * longjmping to a stale UWPROT restores wrong code/constants. */
    {
        int i;
        for (i = cl_nlx_top - 1; i >= 0; i--) {
            if (cl_nlx_stack[i].type == CL_NLX_UWPROT) {
                /* Stale check: if the VM frame was reused by a different
                 * function (tail call), the UWPROT's saved state is invalid */
                CL_Frame *tf = &cl_vm.frames[cl_nlx_stack[i].vm_fp - 1];
                if (tf->code != cl_nlx_stack[i].code)
                    continue;
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
