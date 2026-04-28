#ifndef CL_THREAD_H
#define CL_THREAD_H

/*
 * CL_Thread: per-thread execution state.
 *
 * Phase 0: single global instance, compatibility macros.
 * Phase 1: TLS-backed cl_current_thread.
 * Phase 2: thread registry, GC coordination (safepoints, STW).
 */

#include "vm.h"
#include "error.h"
#include "mem.h"

/* Forward declarations */
struct CL_Compiler_s;
void *platform_tls_get(void);  /* from platform_thread.h */

/* Constants for thread-local array sizes */
#define CL_PP_INDENT_MAX     32
#define CL_CIRCLE_HT_SIZE    256
#define CL_VM_TRACE_SIZE     64

/* ---- Thread-Local Value (TLV) table ---- */
#define CL_TLV_TABLE_SIZE  256
#define CL_TLV_ABSENT      ((CL_Obj)0xFFFFFFFE)  /* "no TLV entry" sentinel */

typedef struct {
    CL_Obj symbol;  /* CL_NIL = empty slot, CL_UNBOUND = tombstone */
    CL_Obj value;
} CL_TLVEntry;

typedef struct CL_Thread_s {
    /* ---- Thread metadata ---- */
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

    /* ---- Lisp-visible thread object (CL_ThreadObj wrapping this thread) ----
     * Set at thread creation, returned by (mp:current-thread). Must remain
     * eq across calls so callers can use it as a hash-table key (e.g.
     * bordeaux-threads-2's .known-threads. which keys by native-thread). */
    CL_Obj thread_obj;

    /* ---- GC root stack (per-thread) ---- */
    CL_Obj *gc_roots[CL_GC_ROOT_STACK_SIZE];
    int     gc_root_count;

    /* ---- Reader state ---- */
    CL_Obj      rd_stream;
    int         rd_eof;
    int         rd_suppress;   /* counter for nested #+/#- */
    int         rd_line;
    int         rd_last_eof;   /* EOF flag of the most recently completed
                                * cl_read_from_stream — survives across
                                * nested reader invocations, so bi_read
                                * still sees whether its own read hit EOF. */
    CL_Obj      rd_uninterned; /* Alist (name . symbol) of #:foo tokens
                                * read so far in the current READ call.
                                * Per CLHS 2.4.8.5: within one READ, all
                                * #:foo with the same name denote the
                                * same uninterned symbol. */
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

    /* ---- Macroexpansion lexical environment ----
     * Set by cl_macroexpand_1_env before cl_vm_apply of the expander,
     * restored afterwards.  Exposed to user macros as the value of
     * their &environment parameter via the CLAMIGA::%MACROEXPAND-ENV
     * builtin.  Represented as an alist of (SYMBOL . EXPANSION) pairs
     * capturing the symbol-macros active at the macro call site.
     * CL_NIL = empty env. */
    CL_Obj current_lex_env;

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

    /* ---- Thread-Local Value (TLV) table ---- */
    CL_TLVEntry tlv_table[CL_TLV_TABLE_SIZE];
    uint32_t    tlv_entry_count;  /* number of active TLV entries — 0 = skip probes */

    /* ---- GC coordination ---- */
    volatile uint8_t gc_requested;
    volatile uint8_t gc_stopped;
    /* Non-zero while the thread is in a blocking syscall that does not touch
     * the heap (e.g. pthread_join).  STW treats such a thread as already
     * stopped — it cannot reach a Lisp-level safepoint, but it also cannot
     * race the GC. */
    volatile uint8_t in_safe_region;

    /* ---- Thread interruption ---- */
    volatile uint8_t interrupt_pending;   /* 1 = check interrupt_func or destroy */
    volatile uint8_t destroy_requested;   /* 1 = abort at next safepoint */
    CL_Obj interrupt_func;               /* function for interrupt-thread */

    /* ---- Thread registry ---- */
    struct CL_Thread_s *next;
    void *platform_handle;

    /* ---- rwlock leak diagnostics ----
     * Per-thread reader-hold count for cl_tables_rwlock and cl_package_rwlock.
     * Bumped/decremented by cl_tables_rdlock / cl_tables_rwunlock (and the
     * package equivalents) so a writer that gets stuck can identify which
     * thread leaked a reader lock, and bi_condition_wait can refuse to sleep
     * while holding one.  rdlock_tables_sites stores the call-site of each
     * outstanding rdlock so the diagnostic can name the leaking caller. */
#define CL_RDLOCK_SITES_MAX 32
    int         rdlock_tables_held;
    const char *rdlock_tables_sites[CL_RDLOCK_SITES_MAX];
    int         rdlock_tables_sites_top;
    int         rdlock_package_held;
} CL_Thread;

/* Current thread pointer — TLS-backed.
 * platform_tls_get() returns the CL_Thread* for the calling OS thread.
 * A fast-path global is kept for single-threaded hot paths (Phase 0 compat). */
extern CL_Thread *cl_main_thread_ptr;   /* fast access to main thread */
extern uint32_t    cl_thread_count;      /* number of registered threads */

/* Inline fast path: when single-threaded (the common case), return the
 * cached main thread pointer directly, avoiding the TLS lookup. */
static inline CL_Thread *cl_get_current_thread(void)
{
    if (cl_thread_count <= 1)
        return cl_main_thread_ptr;
    return (CL_Thread *)platform_tls_get();
}

#define cl_current_thread cl_get_current_thread()
#define CT cl_current_thread

/* Initialize/shutdown thread system */
void cl_thread_init(void);
void cl_thread_shutdown(void);

/* ---- Thread creation API ---- */

/* Worker thread VM/NLX sizes (compact: ~123KB per thread on Amiga) */
#define CL_WORKER_VM_STACK_SIZE  4096   /* 4K entries = 16KB */
#define CL_WORKER_VM_FRAME_SIZE  256    /* 256 frames = ~15KB */
#define CL_WORKER_NLX_FRAMES     256    /* 256 NLX frames = ~50KB */

/* Thread side table: maps thread_id -> CL_Thread* */
#define CL_MAX_THREADS 256
extern CL_Thread *cl_thread_table[CL_MAX_THREADS];

/* Lock side table: maps lock_id -> void* (platform mutex) */
#define CL_MAX_LOCKS 1024
extern void *cl_lock_table[CL_MAX_LOCKS];

/* Condvar side table: maps condvar_id -> void* (platform condvar) */
#define CL_MAX_CONDVARS 1024
extern void *cl_condvar_table[CL_MAX_CONDVARS];

/* Allocate and initialize a new CL_Thread for a worker */
CL_Thread *cl_thread_alloc_worker(void);

/* Free a worker CL_Thread's resources */
void cl_thread_free_worker(CL_Thread *t);

/* Allocate a side table slot for a thread, returns id or -1 */
int cl_thread_table_alloc(CL_Thread *t);
void cl_thread_table_free(int id);

/* Allocate a side table slot for a lock, returns id or -1 */
int cl_lock_table_alloc(void *handle);
void cl_lock_table_free(int id);

/* Allocate a side table slot for a condvar, returns id or -1 */
int cl_condvar_table_alloc(void *handle);
void cl_condvar_table_free(int id);

/* ---- Thread registry ---- */
extern CL_Thread  *cl_thread_list;      /* linked list of all threads */
extern void       *cl_thread_list_lock; /* mutex protecting the list */
extern uint32_t    cl_thread_count;     /* number of registered threads */

void cl_thread_register(CL_Thread *t);
void cl_thread_unregister(CL_Thread *t);

/* ---- GC coordination ---- */

/* ---- Multi-thread check ---- */
/* True when more than one thread is registered — used to skip locking
 * in the common single-threaded case for zero overhead on 68020. */
#define CL_MT() (cl_thread_count > 1)

/* Safepoint check — insert at function calls and backward jumps.
 * The volatile reads are cheap; the slow paths handle GC and interrupts.
 * GC is handled first (critical), then pending interrupts. */
#define CL_SAFEPOINT() \
    do { \
        CL_Thread *_sp = cl_get_current_thread(); \
        if (_sp->gc_requested) cl_gc_safepoint(); \
        if (_sp->interrupt_pending) cl_thread_handle_interrupt(_sp); \
    } while (0)

void cl_gc_safepoint(void);           /* slow path: stop until GC completes */
void cl_thread_handle_interrupt(CL_Thread *t);  /* slow path: handle pending interrupt */

/* Mark the current thread as entering / leaving a blocking syscall that does
 * not touch the heap.  Bracket calls like platform_thread_join with
 * cl_gc_enter_safe_region() / cl_gc_leave_safe_region() so STW does not
 * deadlock waiting for a thread that cannot reach a safepoint. */
void cl_gc_enter_safe_region(void);
void cl_gc_leave_safe_region(void);

/* ---- TLV functions ---- */

/* Snapshot TLV table from src to dst (for thread inheritance) */
void cl_tlv_snapshot(CL_Thread *dst, CL_Thread *src);

/* TLV table operations */
CL_Obj cl_tlv_get(CL_Thread *t, CL_Obj sym);
void   cl_tlv_set(CL_Thread *t, CL_Obj sym, CL_Obj val);
void   cl_tlv_remove(CL_Thread *t, CL_Obj sym);

/* High-level TLV-aware accessors */
CL_Obj cl_symbol_value(CL_Obj sym);
void   cl_set_symbol_value(CL_Obj sym, CL_Obj val);
int    cl_symbol_boundp(CL_Obj sym);

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

/* Macroexpansion lexical environment (current &environment value) */
#define cl_current_lex_env  (CT->current_lex_env)

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
