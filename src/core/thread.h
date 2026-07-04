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
#include "../platform/platform_thread.h"  /* platform_tls_get (inline on POSIX) */

/* Forward declarations */
struct CL_Compiler_s;

/* Constants for thread-local array sizes */
#define CL_PP_INDENT_MAX     32
#define CL_CIRCLE_HT_SIZE    256
#define CL_VM_TRACE_SIZE     64

/* Saved pending-throw stack depth (pushed once per active UWP arming) */
#define CL_MAX_SAVED_PENDING 256

/* ---- Thread-Local Value (TLV) table ---- */
#define CL_TLV_TABLE_SIZE  256
#define CL_TLV_ABSENT      ((CL_Obj)0xFFFFFFFE)  /* "no TLV entry" sentinel */

typedef struct {
    CL_Obj symbol;  /* CL_NIL = empty slot, CL_UNBOUND = tombstone */
    CL_Obj value;
} CL_TLVEntry;

/* Snapshot of all pending-throw fields, saved/restored around UWP cleanups
 * so a nested unwind-protect inside a cleanup cannot clobber the outer
 * non-local transfer. */
typedef struct {
    int    pending_throw;
    CL_Obj pending_tag;
    CL_Obj pending_value;
    int    pending_mv_count;
    CL_Obj pending_mv_values[CL_MAX_MV];
    int    pending_error_code;
    char   pending_error_msg[512];
    int    entered_via_longjmp; /* 1 if our UWPROT longjmp was triggered */
} CL_SavedPending;

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

    /* ---- Saved pending-throw stack (heap-allocated) ----
     * Pushed once per active OP_UWPROT arming so nested UWPs inside a
     * cleanup cannot clobber the outer non-local transfer state. */
    CL_SavedPending  *saved_pending_stack;
    int               saved_pending_top;
    int               saved_pending_max;

    /* ---- Handler stack ---- */
    CL_HandlerBinding handler_stack[CL_MAX_HANDLER_BINDINGS];
    int               handler_top;
    int               handler_floor;
    /* Per-handler "active" bit (bit j = handler_stack[j] enabled).  This is the
     * authoritative replacement for the per-frame `active` flag: it is snapshot
     * into every NLX / error frame at establishment and restored verbatim on
     * unwind, so the CLHS 9.1.4 "disabled band" set by a running handler is
     * automatically restored when the handler exits via a NON-LOCAL transfer
     * (e.g. a handler that INVOKE-RESTARTs a restart in its own dynamic extent
     * — fiveam's process-failure pattern).  Before this, the band-restore lived
     * only in C locals in cl_signal_condition and was skipped by the longjmp,
     * leaving the handler permanently disabled so the next signal escaped.
     * CL_MAX_HANDLER_BINDINGS == 64 fits exactly in a uint64_t. */
    uint64_t          handler_active_mask;

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
    int    pending_mv_count;
    CL_Obj pending_mv_values[CL_MAX_MV];

    /* ---- Error frames ---- */
    CL_ErrorFrame error_frames[CL_MAX_ERROR_FRAMES];
    int           error_frame_top;
    int           error_code;
    char          error_msg[512];
    int           exit_code;

    /* ---- Multiple values ---- */
    CL_Obj mv_values[CL_MAX_MV];
    int    mv_count;
    /* mv state captured by call_builtin before its per-call reset; lets
     * NLX builtins (THROW) see the multiple values of their last argument. */
    int    pre_call_mv_count;
    CL_Obj pre_call_mv_values[CL_MAX_MV];

    /* ---- Lisp-visible thread object (CL_ThreadObj wrapping this thread) ----
     * Set at thread creation, returned by (mp:current-thread). Must remain
     * eq across calls so callers can use it as a hash-table key (e.g.
     * bordeaux-threads-2's .known-threads. which keys by native-thread). */
    CL_Obj thread_obj;

    /* ---- GC root stack (per-thread) ---- */
    CL_Obj *gc_roots[CL_GC_ROOT_STACK_SIZE];
    int     gc_root_count;
#ifdef DEBUG_GC
    const char *gc_root_files[CL_GC_ROOT_STACK_SIZE];
    int         gc_root_lines[CL_GC_ROOT_STACK_SIZE];
#endif

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
    CL_Obj      rd_labels;     /* Alist (label-fixnum . object) for the
                                * #n= / #n# shared/circular-structure reader
                                * macros (CLHS 2.4.8.15-16).  Scoped to one
                                * top-level READ call. */
    int         rd_label_backrefs; /* count of #n# that resolved to a still-
                                * incomplete label (i.e. a circular forward
                                * reference); when >0, #n= must fix up the
                                * placeholder after reading its object. */
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

    /* In-progress object stack for print-object-hook re-entrancy detection.
     * Pushed before cl_vm_apply on a hook, popped after.  If the hook would
     * be fired for an object already on the stack, we emit "#<...>" and
     * skip the apply — terminates Lisp-side circular print-object recursion
     * (e.g. sento's actor-cell ↔ message-box ↔ queue ↔ message-item cycle). */
#define CL_PR_INPROG_MAX 32
    CL_Obj  pr_inprog[CL_PR_INPROG_MAX];
    int     pr_inprog_top;

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
    int    debug_base_fp;   /* VM frame depth snapshot at error time, so the
                             * debugger-hook (SLDB) sees the error-time
                             * backtrace rather than its own pushed frames. */

    /* ---- JIT native-stack GC scan window ----
     * jit_depth: number of nested cl_jit_invoke frames currently on the
     *   C stack for this thread.  0 = thread is not inside JIT'd code.
     * jit_stack_top: captured SP of the OUTERMOST cl_jit_invoke frame
     *   (higher address on a stack-grows-down architecture).  The GC
     *   conservatively scans [current SP .. jit_stack_top) for words
     *   that look like heap offsets.  Only meaningful when jit_depth>0.
     *
     * See specs/native-backend.md §"GC interaction" — option A. */
    int    jit_depth;
    void  *jit_stack_top;
    /* Captured C stack pointer at the moment this thread PARKED for a
     * stop-the-world GC (safepoint or safe-region entry).  A peer thread that
     * initiates a compaction cannot use its own SP as the lower bound when
     * conservatively scanning THIS thread's JIT native stack — it must use the
     * point below which this thread's JIT-spilled operands live, i.e. where it
     * froze.  The GC scans [jit_park_sp .. jit_stack_top) for such peers.  Only
     * meaningful while the thread is stopped with jit_depth > 0. */
    void  *jit_park_sp;

    /* nargs the innermost JIT-entry was invoked with.  Backs OP_ARGC
     * inside JIT'd code (which has no `frame` pointer the way the VM
     * does).  cl_jit_invoke save/restores this around every native
     * call so nested entries return to their caller's value.  Unset
     * (any prior value) when jit_depth == 0 — callers must only read
     * it from inside a JIT'd frame. */
    int32_t jit_current_nargs;

    /* ---- VM extras ---- */
    CL_Obj vm_extra_args_buf[256];
    int    vm_extra_count;


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
    /* Nesting depth of cl_invoke_debugger on this thread.  Bounded by
     * CL_DEBUGGER_MAX_DEPTH to stop a re-signalling debugger-hook / restart
     * from recursing the C stack into a SIGSEGV.  Snapshotted/restored by the
     * error-frame machinery (CL_ErrorFrame.saved_debugger_depth). */
    int debugger_depth;

    /* ---- Thread-Local Value (TLV) table ---- */
    CL_TLVEntry tlv_table[CL_TLV_TABLE_SIZE];
    uint32_t    tlv_entry_count;  /* number of active TLV entries — 0 = skip probes */

    /* ---- GC coordination ---- */
    volatile uint8_t gc_requested;
    volatile uint8_t gc_stopped;
    /* 0 = newborn: registered in cl_thread_list (so GC marks/forwards its
     * roots) but its OS thread has not yet reached the online barrier, so it
     * cannot cooperate with stop-the-world.  1 = live: participates in STW.
     * A stop-the-world initiator waits only for `gc_live` threads; a newborn
     * would never reach a safepoint and would hang the world.  The
     * newborn→live transition happens under gc_mutex in cl_gc_thread_online,
     * serialized against the STW request so a thread that comes online during
     * an in-progress STW parks instead of touching the stopped heap.
     * See cl_gc_thread_online / cl_gc_stop_the_world. */
    volatile uint8_t gc_live;
    /* Non-zero while the thread is in a blocking syscall that does not touch
     * the heap (e.g. pthread_join).  STW treats such a thread as already
     * stopped — it cannot reach a Lisp-level safepoint, but it also cannot
     * race the GC. */
    volatile uint8_t in_safe_region;
    /* Nesting depth for cl_gc_enter/leave_safe_region.  Owner-only.  Safe
     * regions nest (e.g. the platform layer brackets a blocking syscall
     * that a builtin already bracketed): only the OUTERMOST enter/leave
     * pair touches gc_mutex / in_safe_region — an inner leave clearing the
     * flag early would let this thread return to heap-touching code while
     * a peer's STW GC still counts it as stopped. */
    int safe_region_depth;

    /* ---- Thread interruption ---- */
    volatile uint8_t interrupt_pending;   /* 1 = check interrupt_func or destroy */
    volatile uint8_t destroy_requested;   /* 1 = abort at next safepoint */
    CL_Obj interrupt_func;               /* function for interrupt-thread */

    /* ---- Thread registry ---- */
    /* Set (under cl_thread_list_lock) by the JOIN-THREAD that claimed this
     * worker's platform handle.  While set, the zombie reaper and any
     * concurrent JOIN must not free the worker — the claimant owns the
     * cleanup; late joiners wait for the table slot to clear and read the
     * result from the GC-managed wrapper. */
    volatile uint8_t join_in_progress;
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

    /* ---- Wait-state diagnostics (deadlock triage) ----
     * What this thread is currently blocked on.  Set just before a thread
     * parks in MP:CONDITION-WAIT / a blocking MP:ACQUIRE-LOCK / a stop-the-world
     * GC barrier and cleared on wake.  Read by another thread via
     * (MP:DUMP-THREAD-WAITS) to tell a lost-wakeup (a worker still parked on its
     * queue condvar after a notify) apart from a lock-ordering deadlock (a thread
     * blocked acquiring a held lock) or a stalled stop-the-world GC.  Single-writer
     * (the owning thread) so the racy cross-thread read is fine for diagnostics. */
    volatile int wait_kind;     /* 0=running,1=condwait,2=condwait/timeout,3=lock-acquire,4=GC-STW-wait */
    volatile int wait_cv_id;    /* condvar id when wait_kind is condwait */
    volatile int wait_lock_id;  /* lock id when condwait/lock-acquire; straggler tid for GC-STW-wait */

    /* ---- COMPILE-FILE per-thread state ---- */
    /* Set to 1 while COMPILE-FILE is active (thread-local so concurrent
     * compile-file calls on different threads don't race on this flag). */
    int compiling_to_file;
    /* Pending LOAD-TIME-VALUE (cell, thunk) pairs registered during lambda
     * compilation.  compile_defun reads these after compile_lambda returns
     * and emits inline init code into the TOP-LEVEL compiler so the init
     * runs at FASL load time within the same FASL unit.  FASL deduplication
     * then ensures the cell in the init code and in the function body are
     * the same deserialized object (CLHS 3.2.4.4). */
#define CL_LTV_INIT_MAX 32
    CL_Obj ltv_init_cells[CL_LTV_INIT_MAX];
    CL_Obj ltv_init_thunks[CL_LTV_INIT_MAX];
    int    ltv_init_count;
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

/* Worker thread VM/NLX/C-stack sizes.
 *
 * HOST: match the main thread's budgets.  Worker threads used to get a 4x-16x
 * smaller VM stack / call-frame stack / NLX stack / OS stack than the main
 * thread, which was a silent-death bug: a worker overflowed its VM frame stack
 * at ~256 nested calls where the main thread tolerates ~1024, and the overflow
 * is UNRECOVERABLE (running the Lisp handler that would report it itself needs
 * frames, and there are none), so the worker just dies -- status 3, no debugger,
 * no result.  That is what made (asdf:load-system ...) hang under Sly: slynk
 * evaluates on a channel worker thread, and a big load's ASDF + CLOS-dispatch +
 * macroexpansion call chains exceed 256 frames but fit under 1024, so it loads
 * fine headless (main thread) yet silently kills the slynk worker -- Emacs never
 * gets a :return (REPL hangs forever) and a later request answers "Thread not
 * found".  See tests/test_threads.c:worker_deep_recursion_survives_like_main.
 *
 * AMIGA: keep the historical compact sizes -- byte-for-byte what shipped before
 * this fix.  Two reasons: (1) RAM is scarce on the 8MB target, and (2) the
 * decisive one -- bumping these sizes shifts heap/binary layout enough to tip a
 * PRE-EXISTING, layout-fragile moving-GC bug into firing (a Guru partway through
 * the Amiga test suite; the parent commit passes the whole suite).  The bug is
 * NOT in this code: the crash lands on the main thread before any worker exists,
 * and moves to a different, unrelated test for a different layout (the
 * heisenbug signature).  Until that GC bug is fixed separately, the worker-size
 * fix stays host-only -- which is where it is needed: Sly connects to the host
 * binary, so the hang is a host problem, and Amiga worker code keeps the
 * pre-existing 256-frame budget it has always run with. */
#if defined(PLATFORM_AMIGA)
#define CL_WORKER_VM_STACK_SIZE  4096   /* 4K entries = 16KB (pre-fix value) */
#define CL_WORKER_VM_FRAME_SIZE  256    /* 256 frames        (pre-fix value) */
#define CL_WORKER_NLX_FRAMES     256    /* 256 NLX frames    (pre-fix value) */
#define CL_WORKER_SAVED_PENDING   64    /* 64 saved-pending  (pre-fix value) */
#define CL_WORKER_C_STACK_SIZE     0    /* OS default (~64KB, pre-fix value) */
#else
#define CL_WORKER_VM_STACK_SIZE  CL_VM_STACK_SIZE      /* 16K entries = 64KB */
#define CL_WORKER_VM_FRAME_SIZE  CL_VM_FRAME_SIZE      /* 1024 frames */
#define CL_WORKER_NLX_FRAMES     CL_MAX_NLX_FRAMES     /* 2048 NLX frames */
#define CL_WORKER_SAVED_PENDING  CL_MAX_SAVED_PENDING  /* 256 saved-pending */
#define CL_WORKER_C_STACK_SIZE   (8u * 1024u * 1024u)  /* match main (8MB) */
#endif

/* Thread side table: maps thread_id -> CL_Thread* */
#define CL_MAX_THREADS 256
extern CL_Thread *cl_thread_table[CL_MAX_THREADS];
/* Per-slot generation counter, bumped every time a slot is (re)claimed by
 * cl_thread_table_alloc.  A CL_ThreadObj wrapper snapshots the value at
 * creation (table_gen); a mismatch later means the slot was reused for an
 * unrelated worker and the wrapper's thread has already exited.  Read/compared
 * under cl_thread_list_lock (or during STW GC, when no peer can run). */
extern uint32_t cl_thread_table_gen[CL_MAX_THREADS];

/* Lock side table: maps lock_id -> void* (platform mutex).
 * Sized for sento workloads: each actor allocates ~3 locks (queue, mbox state,
 * eventstream registry), plus one withreply-lock per ASK in flight.  Tests
 * like ASK--SHARED--TIMEOUT--MANY create 2000 actors × ASK simultaneously,
 * needing >6000 simultaneously-live locks.  16384 fits in 128KB on 64-bit
 * (64KB on 32-bit Amiga) and absorbs that working set with headroom.  GC
 * still reclaims dead slots; this is the upper bound when the entire working
 * set is reachable.
 *
 * On AmigaOS (8MB target) the high-concurrency sento workloads aren't
 * realistic — shrink to 256 to free ~63KB of BSS per table.  Programs
 * that exceed it get a clean error rather than silent corruption. */
#ifdef PLATFORM_AMIGA
#define CL_MAX_LOCKS    256
#define CL_MAX_CONDVARS 256
#else
#define CL_MAX_LOCKS    16384
#define CL_MAX_CONDVARS 16384
#endif
extern void *cl_lock_table[CL_MAX_LOCKS];

/* Owner tracking for cl_lock_table entries: cl_lock_held[id] is set by the
 * acquiring thread AFTER platform_mutex_lock succeeds and cleared by the
 * releasing thread BEFORE platform_mutex_unlock (owner-only mutation while
 * holding the mutex).  gc_finalize_dead(TYPE_LOCK) reads it during STW
 * (race-free) and LEAKS the OS mutex instead of destroying a held one
 * (pthread_mutex_destroy of a locked mutex is UB).
 *
 * cl_lock_depth[id] counts nested acquires by the current owner (recursive
 * locks via MP:MAKE-RECURSIVE-LOCK can be acquired N>1 times by the same
 * thread and are still genuinely OS-locked after only one release).
 * bi_acquire_lock increments it on every successful acquire; bi_release_lock
 * decrements it and only clears cl_lock_held once it reaches 0, so a
 * recursive lock stays correctly "held" for gc_finalize_dead until it is
 * released as many times as it was acquired.  gc_finalize_dead resets both
 * to 0 when it frees a table slot (held or not) so a reused lock_id starts
 * clean. */
extern CL_Thread *cl_lock_held[CL_MAX_LOCKS];
extern uint32_t cl_lock_depth[CL_MAX_LOCKS];

/* Condvar side table: maps condvar_id -> void* (platform condvar).
 * Sized to mirror CL_MAX_LOCKS so condvar-paired lock workloads scale. */
extern void *cl_condvar_table[CL_MAX_CONDVARS];

/* Global parking lot for contended blocking MP:ACQUIRE-LOCK (all locks
 * share it).  A blocking acquire that exhausts its trylock/yield spin
 * phase registers in cl_lock_park_waiters (under cl_lock_park_mutex) and
 * parks on cl_lock_park_cv with a timed backstop; bi_release_lock
 * broadcasts the cv whenever waiters are registered, so a lock handoff
 * wakes the waiter immediately instead of on a sleep-poll grid (the
 * 10ms-sleep escalation this replaces collapsed sento message throughput
 * ~6x).  wake_interrupted_waiter broadcasts it too, so interrupt/destroy
 * reaches lock-parked threads promptly (I5); the timed backstop bounds
 * delivery even on a lost wake.  Sharing one cv means a broadcast wakes
 * parked waiters of unrelated locks — they retrylock, fail, and re-park;
 * with realistic thread counts that herd is tiny, and it frees us from
 * per-lock condvar lifecycle management (16K table slots, finalizers). */
extern void *cl_lock_park_mutex;
extern void *cl_lock_park_cv;
extern volatile uint32_t cl_lock_park_waiters;

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

/* Allocate a fresh CL_Lock heap object backed by a freshly-initialized
 * platform mutex.  recursive != 0 selects a recursive mutex.  Errors out
 * via cl_error() on failure; err_prefix tags the message for the caller
 * (e.g. "MP:MAKE-LOCK", "FASL"). */
CL_Obj cl_lock_alloc_obj(int recursive, CL_Obj name, const char *err_prefix);

/* Allocate a side table slot for a condvar, returns id or -1 */
int cl_condvar_table_alloc(void *handle);
void cl_condvar_table_free(int id);

/* ---- Thread registry ---- */
extern CL_Thread  *cl_thread_list;      /* linked list of all threads */
extern void       *cl_thread_list_lock; /* mutex protecting the list */
extern uint32_t    cl_thread_count;     /* number of registered threads */

void cl_thread_register(CL_Thread *t);
/* Register a make-thread child whose OS thread does not exist yet: a newborn
 * skipped by the STW wait loop until it reaches cl_gc_thread_online. */
void cl_thread_register_newborn(CL_Thread *t);
void cl_thread_unregister(CL_Thread *t);

/* ---- GC coordination ---- */

/* ---- Multi-thread check ---- */
/* True when more than one thread is registered — used to skip locking
 * in the common single-threaded case for zero overhead on 68020. */
#define CL_MT() (cl_thread_count > 1)

/* Capture the frame address of the calling function.
 * On every stack-grows-down ABI we target (m68k SysV, host x86-64),
 * the actual SP at the time of the call is at a lower address than
 * this value, so it serves as a valid upper bound for the JIT
 * native-stack scan window.  Implemented as a macro so the captured
 * address is in the CALLER's frame, not a helper's.
 *
 * __builtin_frame_address(0) is supported by gcc on m68k and host. */
#define CL_CAPTURE_SP() ((void *)__builtin_frame_address(0))

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

/* Bring a freshly-created worker online: the first thing its OS thread must do
 * (before touching the heap) is transition from newborn to `gc_live` under
 * gc_mutex.  If a stop-the-world GC is in progress and already requested this
 * thread, the barrier parks here until it completes instead of racing the
 * stopped/compacting heap.  This closes the newborn-vs-STW hang: the parent
 * registers the child before creating its OS thread (so GC roots are marked),
 * but until the child reaches this barrier it is invisible to the STW wait
 * loop, which only waits for `gc_live` threads. */
void cl_gc_thread_online(CL_Thread *self);

/* Mark the current thread as entering / leaving a blocking syscall that does
 * not touch the heap.  Bracket calls like platform_thread_join with
 * cl_gc_enter_safe_region() / cl_gc_leave_safe_region() so STW does not
 * deadlock waiting for a thread that cannot reach a safepoint. */
void cl_gc_enter_safe_region(void);
void cl_gc_leave_safe_region(void);

/* Acquire an internal C mutex that may be held across a GC safepoint or a
 * blocking-syscall safe region without stalling a concurrent stop-the-world GC.
 * A plain platform_mutex_lock() on such a contended lock is neither a safepoint
 * nor a safe region, so a peer running STW waits for the blocked thread forever
 * while the current holder parks in leave_safe_region still holding the lock.
 * Fast (uncontended) path is a single trylock; pair with platform_mutex_unlock. */
void cl_gc_safe_mutex_lock(void *mutex);

/* ---- TLV functions ---- */

/* TLV table operations */
CL_Obj cl_tlv_get(CL_Thread *t, CL_Obj sym);
/* C-level dynamic bind (thread-local, like OP_DYNBIND); pair with
 * cl_dynbind_restore_to(mark) from vm.h. */
void   cl_dynbind_c(CL_Obj sym, CL_Obj val);
void   cl_tlv_set(CL_Thread *t, CL_Obj sym, CL_Obj val);
void   cl_tlv_remove(CL_Thread *t, CL_Obj sym);
/* Rebuild the TLV table after a compacting GC relocated symbols (the table is
 * keyed by symbol arena-offset, which changes when the object moves). */
void   cl_tlv_rehash(CL_Thread *t);

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
/* Per-thread NLX capacity: the number of CL_NLXFrame slots actually allocated
 * for THIS thread's nlx_stack (CL_MAX_NLX_FRAMES for the main thread,
 * CL_WORKER_NLX_FRAMES for workers — which is smaller on AmigaOS).  Every NLX
 * push guard and diagnostic loop MUST bound against this, NOT the global
 * CL_MAX_NLX_FRAMES constant: a worker with a smaller allocation would
 * otherwise be allowed to write past the end of its nlx_stack (heap
 * corruption).  This must hold on every platform, including AmigaOS — a
 * memory-safety guard is not allowed to be loosened to dodge an unrelated,
 * pre-existing layout-fragile moving-GC bug; that bug needs a real fix (see
 * memory note sly_load_hang_worker_vm_frame_budget), not a wider NLX bound. */
#define cl_nlx_max          (CT->nlx_max)

/* Saved pending-throw stack */
#define cl_saved_pending_stack  (CT->saved_pending_stack)
#define cl_saved_pending_top    (CT->saved_pending_top)
#define cl_saved_pending_max    (CT->saved_pending_max)

/* Handler stack */
#define cl_handler_stack    (CT->handler_stack)
#define cl_handler_top      (CT->handler_top)
#define cl_handler_floor    (CT->handler_floor)
#define cl_handler_active_mask (CT->handler_active_mask)

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
#define cl_pending_mv_count   (CT->pending_mv_count)
#define cl_pending_mv_values  (CT->pending_mv_values)

/* Error frames */
#define cl_error_frames     (CT->error_frames)
#define cl_error_frame_top  (CT->error_frame_top)
#define cl_error_code       (CT->error_code)
#define cl_error_msg        (CT->error_msg)
#define cl_exit_code        (CT->exit_code)

/* Multiple values */
#define cl_mv_values           (CT->mv_values)
#define cl_mv_count            (CT->mv_count)
#define cl_pre_call_mv_count   (CT->pre_call_mv_count)
#define cl_pre_call_mv_values  (CT->pre_call_mv_values)

/* GC roots */
#define gc_root_count       (CT->gc_root_count)

/* Trace & debug */
#define cl_trace_depth      (CT->trace_depth)
#define cl_trace_count      (CT->trace_count)
#define cl_backtrace_buf    (CT->backtrace_buf)
#define cl_c_stack_base     (CT->c_stack_base)
#define cl_debug_base_fp    (CT->debug_base_fp)

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
#define cl_debugger_depth   (CT->debugger_depth)

/* COMPILE-FILE state */
#define cl_compiling_to_file (CT->compiling_to_file)
#define cl_ltv_init_count    (CT->ltv_init_count)

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
