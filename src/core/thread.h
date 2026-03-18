#ifndef CL_THREAD_H
#define CL_THREAD_H

/*
 * CL_Thread: per-thread execution state.
 *
 * Phase 0: single global instance, no actual threading.
 * All legacy global/static per-thread variables are redirected here
 * via compatibility macros (e.g. cl_vm -> CT->vm).
 */

#include "vm.h"
#include "error.h"
#include "mem.h"

/* Forward declarations */
struct CL_Compiler_s;

/* Constants for thread-local array sizes */
#define CL_PP_INDENT_MAX     32
#define CL_CIRCLE_HT_SIZE    256
#define CL_VM_TRACE_SIZE     64

typedef struct CL_Thread_s {
    /* ---- Thread metadata (Phase 1+) ---- */
    uint32_t id;
    CL_Obj   name;           /* CL string or NIL */
    uint8_t  status;         /* 0=created, 1=running, 2=finished, 3=aborted */
    CL_Obj   result;

    /* ---- VM instance ---- */
    CL_VM    vm;

    /* ---- Dynamic binding stack ---- */
    CL_DynBinding     dyn_stack[CL_MAX_DYN_BINDINGS];
    int               dyn_top;

    /* ---- NLX stack (heap-allocated) ---- */
    CL_NLXFrame      *nlx_stack;
    int               nlx_max;
    int               nlx_top;

    /* ---- Handler stack ---- */
    CL_HandlerBinding handler_stack[CL_MAX_HANDLER_BINDINGS];
    int               handler_top;
    int               handler_floor;

    /* ---- Restart stack ---- */
    CL_RestartBinding restart_stack[CL_MAX_RESTART_BINDINGS];
    int               restart_top;
    int               restart_floor;

    /* ---- Pending throw state ---- */
    int    pending_throw;      /* 0=none, 1=throw, 2=error */
    CL_Obj pending_tag;
    CL_Obj pending_value;
    int    pending_error_code;
    char   pending_error_msg[512];

    /* ---- Error frames ---- */
    CL_ErrorFrame error_frames[CL_MAX_ERROR_FRAMES];
    int           error_frame_top;
    int           error_code;
    char          error_msg[512];
    int           exit_code;

    /* ---- Multiple values ---- */
    CL_Obj mv_values[CL_MAX_MV];
    int    mv_count;

    /* ---- GC root stack (per-thread) ---- */
    CL_Obj *gc_roots[CL_GC_ROOT_STACK_SIZE];
    int     gc_root_count;

    /* ---- Reader state ---- */
    CL_Obj      rd_stream;
    int         rd_eof;
    int         rd_suppress;   /* counter for nested #+/#- */
    int         rd_line;
    const char *current_source_file;
    uint16_t    current_file_id;

    /* ---- Printer state ---- */
    int32_t pr_depth;
    int32_t pr_column;
    int32_t pr_indent_stack[CL_PP_INDENT_MAX];
    int32_t pr_indent_top;
    int32_t pr_block_start[CL_PP_INDENT_MAX];
    CL_Obj  pr_circle_keys[CL_CIRCLE_HT_SIZE];
    int32_t pr_circle_vals[CL_CIRCLE_HT_SIZE];
    int     pr_circle_count;
    int     pr_circle_next_label;
    int     pr_circle_active;
    CL_Obj  pr_stream;
    int     pr_to_buffer;
    char   *pr_out_buf;
    int     pr_out_pos;
    int     pr_out_size;
    int     pr_pprint_dispatch_active;

    /* ---- Compiler chain ---- */
    struct CL_Compiler_s *active_compiler;
    CL_Obj pending_lambda_name;

    /* ---- Trace & debug ---- */
    int    trace_depth;
    int    trace_count;
    char   backtrace_buf[CL_BACKTRACE_BUF_SIZE];
    char  *c_stack_base;

    /* ---- VM extras ---- */
    CL_Obj vm_extra_args_buf[256];
    int    vm_extra_count;
    CL_Obj vm_flat_args_buf[64];

    /* ---- VM eval depth tracking ---- */
    int    vm_eval_depth_val;
    int    vm_max_eval_depth;
    long   c_stack_max_val;

    /* ---- VM trace buffer (crash diagnostics) ---- */
    struct {
        uint8_t  op;
        uint32_t ip;
        int      fp;
        int      sp;
        uint8_t *code;
    } vm_trace_buf[CL_VM_TRACE_SIZE];
    int vm_trace_pos;

    /* ---- Crash diagnostics ---- */
    volatile uint8_t  dbg_last_op;
    volatile uint32_t dbg_last_ip;
    volatile int      dbg_last_fp;
    volatile uint8_t *dbg_last_code;

    /* ---- Debugger state ---- */
    int in_debugger;

    /* ---- GC coordination (Phase 2+) ---- */
    volatile uint8_t gc_requested;
    volatile uint8_t gc_stopped;

    /* ---- Thread registry (Phase 2+) ---- */
    struct CL_Thread_s *next;
    void *platform_handle;
} CL_Thread;

/* Current thread pointer — single global for Phase 0, TLS in Phase 1+ */
extern CL_Thread *cl_current_thread;
#define CT cl_current_thread

/* Initialize/shutdown thread system */
void cl_thread_init(void);
void cl_thread_shutdown(void);

/* ================================================================
 * Compatibility macros
 *
 * Map legacy global names to CL_Thread fields so that existing code
 * compiles unchanged.  Guarded by CL_THREAD_NO_MACROS so that
 * thread.c (which accesses the struct directly) can suppress them.
 * ================================================================ */

#ifndef CL_THREAD_NO_MACROS

/* VM */
#define cl_vm               (CT->vm)

/* Dynamic bindings */
#define cl_dyn_stack        (CT->dyn_stack)
#define cl_dyn_top          (CT->dyn_top)

/* NLX stack */
#define cl_nlx_stack        (CT->nlx_stack)
#define cl_nlx_top          (CT->nlx_top)

/* Handler stack */
#define cl_handler_stack    (CT->handler_stack)
#define cl_handler_top      (CT->handler_top)
#define cl_handler_floor    (CT->handler_floor)

/* Restart stack */
#define cl_restart_stack    (CT->restart_stack)
#define cl_restart_top      (CT->restart_top)
#define cl_restart_floor    (CT->restart_floor)

/* Pending throw */
#define cl_pending_throw      (CT->pending_throw)
#define cl_pending_tag        (CT->pending_tag)
#define cl_pending_value      (CT->pending_value)
#define cl_pending_error_code (CT->pending_error_code)
#define cl_pending_error_msg  (CT->pending_error_msg)

/* Error frames */
#define cl_error_frames     (CT->error_frames)
#define cl_error_frame_top  (CT->error_frame_top)
#define cl_error_code       (CT->error_code)
#define cl_error_msg        (CT->error_msg)
#define cl_exit_code        (CT->exit_code)

/* Multiple values */
#define cl_mv_values        (CT->mv_values)
#define cl_mv_count         (CT->mv_count)

/* GC roots */
#define gc_root_count       (CT->gc_root_count)

/* Trace & debug */
#define cl_trace_depth      (CT->trace_depth)
#define cl_trace_count      (CT->trace_count)
#define cl_backtrace_buf    (CT->backtrace_buf)
#define cl_c_stack_base     (CT->c_stack_base)

/* Compiler chain */
#define cl_active_compiler  (CT->active_compiler)
#define pending_lambda_name (CT->pending_lambda_name)

/* Source location tracking */
#define cl_current_source_file (CT->current_source_file)
#define cl_current_file_id     (CT->current_file_id)

/* Debugger */
#define cl_in_debugger      (CT->in_debugger)

/* VM debug (used under #ifdef DEBUG_VM, but always present in struct) */
#define vm_eval_depth       (CT->vm_eval_depth_val)
#define c_stack_max_seen    (CT->c_stack_max_val)

/* Crash diagnostics */
#define dbg_last_op         (CT->dbg_last_op)
#define dbg_last_ip         (CT->dbg_last_ip)
#define dbg_last_fp         (CT->dbg_last_fp)
#define dbg_last_code       (CT->dbg_last_code)

#endif /* CL_THREAD_NO_MACROS */

#endif /* CL_THREAD_H */
