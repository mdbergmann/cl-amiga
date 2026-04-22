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
 * or CL_ERR_OVERFLOW into err_var.
 *
 * C99 §7.13.1.1 restricts setjmp invocation to one of:
 *   - the entire controlling expression of a selection/iteration statement
 *   - a single operand of a relational/equality comparison with an integer
 *     constant expression, itself the entire controlling expression
 *   - the operand of unary !, itself the entire controlling expression
 *   - the entire expression of an expression statement (possibly (void)-cast)
 *
 * `err_var = setjmp(...)` is NOT in any of these contexts — it is UB.  With
 * -O3/LTO the compiler can exploit this UB and emit code that corrupts the
 * stack after longjmp (observed: saved fp/lr slots in the caller's frame
 * get overwritten with stale register spills, so the next epilogue pops
 * bogus return addresses).  The rewrite below puts setjmp in the controlling
 * expression of an `else if`, which is conformant. The longjmp'd code
 * is communicated via the thread-local cl_error_code (set by cl_error()). */
#define CL_CATCH(err_var) do { \
    int _cl_cf_ = cl_error_frame_push(); \
    if (_cl_cf_ < 0) { \
        (err_var) = CL_ERR_OVERFLOW; \
    } else if (setjmp(cl_error_frames[_cl_cf_].buf) != 0) { \
        (err_var) = cl_error_code; \
    } else { \
        (err_var) = CL_ERR_NONE; \
    } \
} while(0)

/* Pop error frame (call in matching block after CL_CATCH) */
#define CL_UNCATCH() \
    do { if (cl_error_frame_top > 0) { \
        cl_error_frame_top--; \
        cl_error_frames[cl_error_frame_top].active = 0; \
    } } while(0)

/* Signal an error — jumps to nearest CL_CATCH */
void cl_error(int code, const char *fmt, ...);

/* Unwind with an existing condition object. Caller must have already
 * signaled via cl_signal_condition. Preserves the condition so the
 * debugger can dispatch PRINT-OBJECT for a meaningful report. */
void cl_error_from_condition(CL_Obj condition);

/* Print the current error message */
void cl_error_print(void);

/* Initialize error system */
void cl_error_init(void);

#include "thread.h"

#endif /* CL_ERROR_H */
