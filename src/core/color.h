#ifndef CL_COLOR_H
#define CL_COLOR_H

/* Global flag: 1 = emit ANSI color codes, 0 = plain text */
extern int cl_repl_color;

/* Emit ANSI escape to set color (no-op if cl_repl_color == 0) */
void cl_color_set(const char *ansi_code);

/* Emit ANSI reset (no-op if cl_repl_color == 0) */
void cl_color_reset(void);

/* ANSI color code constants */
#define CL_COLOR_LIGHT_BLUE   "\033[94m"
#define CL_COLOR_DIM_GREEN    "\033[32m"
#define CL_COLOR_DIM_CYAN     "\033[36m"
#define CL_COLOR_RED          "\033[31m"
#define CL_COLOR_YELLOW       "\033[33m"
#define CL_COLOR_BOLD_RED     "\033[1;31m"
#define CL_COLOR_DIM_MAGENTA  "\033[35m"
#define CL_COLOR_RESET        "\033[0m"

#endif
