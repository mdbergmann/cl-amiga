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

/* Push an error frame.  Returns the frame index, or -1 on overflow.
 * Must be called BEFORE setjmp so that all side-effects are sequenced
 * before the setjmp point (C99 7.13.1.1 restricts contexts for setjmp). */
int cl_error_frame_push(void);

/* Push a new error frame and setjmp into it.
 * Stores 0 (direct return), an error code (longjmp return),
 * or CL_ERR_OVERFLOW into err_var. */
#define CL_CATCH(err_var) do { \
    int _cl_cf_ = cl_error_frame_push(); \
    (err_var) = (_cl_cf_ < 0) ? CL_ERR_OVERFLOW \
              : setjmp(cl_error_frames[_cl_cf_].buf); \
} while(0)

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

#include "thread.h"

#endif /* CL_ERROR_H */
