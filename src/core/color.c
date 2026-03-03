#include "color.h"
#include "../platform/platform.h"

int cl_repl_color = 0;

void cl_color_set(const char *ansi_code)
{
    if (cl_repl_color)
        platform_write_string(ansi_code);
}

void cl_color_reset(void)
{
    if (cl_repl_color)
        platform_write_string(CL_COLOR_RESET);
}
