#include "error.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

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
}
