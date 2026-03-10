#ifndef CL_ERROR_H
#define CL_ERROR_H

#include "types.h"
#include <setjmp.h>

/*
 * Error handling via setjmp/longjmp.
 * Provides a simple condition system for the REPL to catch errors
 * and continue rather than crashing.
 */

#define CL_ERR_NONE       0
#define CL_ERR_GENERAL    1
#define CL_ERR_TYPE       2
#define CL_ERR_UNBOUND    3
#define CL_ERR_ARGS       4
#define CL_ERR_PARSE      5
#define CL_ERR_OVERFLOW   6
#define CL_ERR_DIVZERO    7
#define CL_ERR_UNDEFINED  8
#define CL_ERR_STORAGE    9
#define CL_ERR_EXIT      10
#define CL_ERR_FILE      11

/* Error handler frame stack */
#define CL_MAX_ERROR_FRAMES 16

typedef struct {
    jmp_buf buf;
    int active;
} CL_ErrorFrame;

extern CL_ErrorFrame cl_error_frames[CL_MAX_ERROR_FRAMES];
extern int cl_error_frame_top;
extern int cl_error_code;
extern char cl_error_msg[512];
extern int cl_exit_code;

/* Push a new error frame, returns 0 on setjmp, error code on longjmp */
#define CL_CATCH() \
    (cl_error_frame_top < CL_MAX_ERROR_FRAMES ? \
     (cl_error_frames[cl_error_frame_top].active = 1, \
      setjmp(cl_error_frames[cl_error_frame_top++].buf)) : \
     CL_ERR_OVERFLOW)

/* Pop error frame (call in matching block after CL_CATCH) */
#define CL_UNCATCH() \
    do { if (cl_error_frame_top > 0) { \
        cl_error_frame_top--; \
        cl_error_frames[cl_error_frame_top].active = 0; \
    } } while(0)

/* Signal an error — jumps to nearest CL_CATCH */
void cl_error(int code, const char *fmt, ...);

/* Print the current error message */
void cl_error_print(void);

/* Initialize error system */
void cl_error_init(void);

#endif /* CL_ERROR_H */
