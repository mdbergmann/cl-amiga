#include "error.h"
#include "vm.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

CL_ErrorFrame cl_error_frames[CL_MAX_ERROR_FRAMES];
int cl_error_frame_top = 0;
int cl_error_code = CL_ERR_NONE;
char cl_error_msg[512];

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

    /* Capture backtrace while VM frames are still intact */
    cl_capture_backtrace();

    /* Signal through condition handler stack before unwinding */
    {
        CL_Obj cond = cl_create_condition_from_error(code, cl_error_msg);
        cl_signal_condition(cond);
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

    /* No UWPROT found — propagating to C error handler.
     * NLX frames (catch/uwprot) are invalid once we leave the VM,
     * so clear the NLX stack and reset pending state.
     * Restore all dynamic bindings before leaving the VM. */
    cl_nlx_top = 0;
    cl_pending_throw = 0;
    cl_dynbind_restore_to(0);
    cl_handler_top = 0;

    if (cl_error_frame_top > 0) {
        cl_error_frame_top--;
        cl_error_frames[cl_error_frame_top].active = 0;
        longjmp(cl_error_frames[cl_error_frame_top].buf, code);
    }

    /* No error frame — fatal */
    platform_write_string("FATAL ERROR: ");
    platform_write_string(cl_error_msg);
    platform_write_string("\n");
    exit(1);
}

void cl_error_print(void)
{
    platform_write_string("ERROR: ");
    platform_write_string(cl_error_msg);
    platform_write_string("\n");
    if (cl_backtrace_buf[0] != '\0') {
        platform_write_string("Backtrace:\n");
        platform_write_string(cl_backtrace_buf);
    }
}
