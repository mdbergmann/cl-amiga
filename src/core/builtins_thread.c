/*
 * builtins_thread.c — MP (multiprocessing) package builtins.
 *
 * Phase 4: CL-level threading API.
 * Threads, locks (mutexes), condition variables.
 */

#define CL_THREAD_NO_MACROS  /* access CL_Thread fields directly */
#include "thread.h"
#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "vm.h"
#include "stream.h"
#include "float.h"
#include "compiler.h"
#include "string_utils.h"
#include "../platform/platform.h"
#include "../platform/platform_thread.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ================================================================
 * Pre-interned keyword symbols
 * ================================================================ */

static CL_Obj KW_NAME_THR = 0;  /* :NAME keyword for make-thread */

/* ================================================================
 * Main thread's Lisp-visible thread object
 * ================================================================ */

static CL_Obj main_thread_obj = CL_NIL;

/* Cached `(lambda (&rest _) nil)` used as the thread-top ABORT
 * restart handler.  bi_invoke_restart calls the handler before
 * throwing to the catch tag, so we need a callable that swallows
 * any args and returns NIL. */
static CL_Obj thread_abort_handler = CL_NIL;

static CL_Obj get_thread_abort_handler(void)
{
    /* Lazy init must be serialized: two racing first-worker creations
     * would otherwise BOTH see NIL and BOTH cl_gc_register_root the
     * same address.  gc_forward is not idempotent, so a double-
     * registered root is forwarded twice on compaction and ends up
     * pointing at an unrelated object (and the unlocked
     * n_global_roots++ can additionally lose a concurrent
     * registration).  The roots themselves are registered once in
     * cl_builtins_thread_init (registering a NIL-holding global is
     * free); only the cached value is built lazily.
     *
     * cl_thread_list_lock must never be held across the allocating
     * cl_eval_string call below: it is the exact lock cl_gc_stop_the_world
     * takes to enumerate threads, and it is explicitly on the "do NOT use
     * cl_gc_safe_mutex_lock" list in thread.c (its critical section is one
     * GC itself touches).  Holding a raw platform_mutex_lock on it across
     * an allocating call risks the lock's holder self-deadlocking as STW
     * initiator, or a peer blocked on the lock (not at a safepoint) hanging
     * STW's wait loop.  So build the value with no lock held at all, and
     * only take the lock — briefly, non-allocating — to publish it
     * first-writer-wins; a losing racer's redundant value is simply
     * unreferenced and collected normally. */
    if (CL_NULL_P(thread_abort_handler)) {
        extern CL_Obj cl_eval_string(const char *str);
        CL_Obj built = cl_eval_string(
            "(lambda (&rest args) (declare (ignore args)) nil)");
        platform_mutex_lock(cl_thread_list_lock);
        if (CL_NULL_P(thread_abort_handler))
            thread_abort_handler = built;
        platform_mutex_unlock(cl_thread_list_lock);
    }
    return thread_abort_handler;
}

/* Cached :report string for the thread-top ABORT restart.  Shared
 * read-only by every worker thread's restart — safe because strings
 * are immutable here and the printer only reads it.  Wording matches
 * clamiga's interactive debugger (src/core/debugger.c). */
static CL_Obj thread_abort_report = CL_NIL;

static CL_Obj get_thread_abort_report(void)
{
    /* See get_thread_abort_handler for why this is locked, why the lock is
     * never held across the allocating call, and why the root registration
     * lives in cl_builtins_thread_init. */
    if (CL_NULL_P(thread_abort_report)) {
        CL_Obj built = cl_make_string("Return to top level", 19);
        platform_mutex_lock(cl_thread_list_lock);
        if (CL_NULL_P(thread_abort_report))
            thread_abort_report = built;
        platform_mutex_unlock(cl_thread_list_lock);
    }
    return thread_abort_report;
}

/* Accessor for GC root marking (called from mem.c) */
CL_Obj cl_main_thread_lisp_obj(void)
{
    return main_thread_obj;
}

/* Pointer accessor for GC compaction (called from mem.c) */
CL_Obj *cl_main_thread_lisp_obj_ptr(void)
{
    return &main_thread_obj;
}

/* ================================================================
 * Thread entry wrapper
 *
 * The OS thread runs this function.  It:
 * 1. Sets up TLS so CT points to this thread's CL_Thread
 * 2. Inherits dynamic bindings (TLV snapshot done by parent)
 * 3. Sets up error handler to catch all errors
 * 4. Calls the user function
 * 5. Stores result and sets status
 * 6. Cleans up
 * ================================================================ */

static void *thread_entry(void *arg)
{
    CL_Thread *t = (CL_Thread *)arg;
    CL_Obj func;
    int err;

    /* 1. Set up TLS for this OS thread */
    platform_tls_set(t);

    /* 1a. Come online for stop-the-world GC.  Until now the child was a
     *     newborn: registered in cl_thread_list (so GC marks/forwards its
     *     roots — notably t->result below) but invisible to the STW wait loop.
     *     This barrier transitions it to `gc_live` under gc_mutex, parking here
     *     if a peer STW is already in progress so we never touch the stopped
     *     heap.  MUST run before the `func = t->result` read: if a compaction
     *     ran during the parent's create window, t->result was forwarded and
     *     we must read the post-barrier (up-to-date) value. */
    cl_gc_thread_online(t);

    /* 2. TLV snapshot was done by parent before thread_create */

    /* 3. Mark as running */
    t->status = 1;

    /* 4. Retrieve stashed function (stored in result field by parent).
     *
     *    DO NOT clear t->result here.  t->result is a GC root: it is marked by
     *    gc_mark_thread_roots and forwarded by gc_update_thread_roots as thread
     *    metadata, so it is the ONLY thing keeping `func` (a freshly-allocated
     *    closure object) alive while we apply it below.  Nulling it would leave
     *    `func` reachable only through this unrooted C local — a concurrent
     *    stop-the-world mark-and-sweep (e.g. another thread calling (gc))
     *    would then sweep the closure mid-apply, and cl_vm_apply would run
     *    on freed memory, erroring out so t->result stays NIL and
     *    join-thread returns NIL instead of the real result.  We overwrite
     *    t->result with the computed result only AFTER cl_vm_apply returns.
     *
     *    Reading t->result AFTER cl_gc_thread_online (step 1a) is deliberate:
     *    any compaction during the parent's create window has already forwarded
     *    this slot, so we read the live (relocated) closure offset. */
    func = t->result;

    /* func stays GC-safe via t->result (see the note above at step 4) —
     * no gc_roots[] entry is added here; do not reintroduce one. */

    /* 5. Call user function inside error handler.
     *    CL_CATCH/CL_UNCATCH macros use compatibility names that are
     *    suppressed by CL_THREAD_NO_MACROS, so expand them inline.
     *
     *    Mirror cl_error_frame_push(): snapshot gc_root_count into
     *    saved_gc_roots so cl_error_unwind can drop CL_GC_PROTECT entries
     *    that belong to C frames it is unwinding out of.  Without this,
     *    a worker's stale gc_roots[] survives the longjmp back here and
     *    a subsequent gc_mark_thread_roots walks dangling stack pointers
     *    — manifesting as gc_mark SEGV under sento workloads. */
    if (t->error_frame_top < CL_MAX_ERROR_FRAMES) {
        t->error_frames[t->error_frame_top].active = 1;
        t->error_frames[t->error_frame_top].saved_gc_roots = t->gc_root_count;
        err = CL_SETJMP(t->error_frames[t->error_frame_top++].buf);
    } else {
        err = CL_ERR_OVERFLOW;
    }

    /* Status to publish at the very end — see ordering note at end of
     * function.  2 = finished cleanly (or via ABORT), 3 = errored. */
    int final_status = 3;

    if (err == 0) {
        CL_Obj result = CL_NIL;
        CL_Obj abort_handler;
        CL_Obj abort_report;
        CL_Obj abort_tag;
        int my_nlx_idx = -1;
        int my_restart_idx = -1;
        int aborted = 0;

#ifdef DEBUG_THREAD
        fprintf(stderr, "[THR] tid=%u CT=%p func=0x%08x type=%d starting\n",
                t->id, (void *)t, func,
                CL_HEAP_P(func) ? (int)CL_HDR_TYPE(CL_OBJ_TO_PTR(func)) : -1);
        fflush(stderr);
#endif

        /* Establish a top-level ABORT restart for the thread.
         * `(abort)` anywhere in the thread body unwinds here and
         * the thread exits cleanly — matching SBCL/CCL semantics
         * (with-simple-restart abort at the top of every thread).
         * Without this, `(abort)` in a worker raises
         * "Restart ABORT not found" which is not what frameworks
         * (bordeaux-threads, sento) expect. */
        /* Pre-compute report before abort_tag is allocated: on first thread
         * creation get_thread_abort_report() calls cl_make_string (allocating),
         * which can trigger compaction.  If called as an argument expression
         * inside cl_make_restart() after abort_tag is assigned, C's
         * unspecified evaluation order may read abort_tag after relocation. */
        abort_report  = get_thread_abort_report();
        /* GC-protect abort_report across the remaining allocations: on a worker
         * thread this setup runs concurrently with other threads, so any of the
         * allocating calls below (get_thread_abort_handler, cl_cons,
         * cl_make_restart) can be interrupted by a PEER thread's stop-the-world
         * compaction that relocates these objects.  Without protection, the
         * unprotected C locals hold stale offsets and cl_make_restart bakes them
         * into the abort restart — permanently corrupting restart_stack[0], so
         * every later GC marks a garbage offset (ORing the mark bit into a live
         * data word).  Single-thread never hit this: the worker's own allocs
         * only compact under this thread, at points the convention already
         * covers, and the main thread's abort setup runs before any peer exists. */
        CL_GC_PROTECT(abort_report);
        abort_handler = get_thread_abort_handler();
        CL_GC_PROTECT(abort_handler);
        abort_tag = cl_cons(SYM_ABORT, CL_NIL);
        CL_GC_PROTECT(abort_tag);

        if (t->nlx_top < t->nlx_max) {
            CL_NLXFrame *frame = &t->nlx_stack[t->nlx_top];
            frame->type = CL_NLX_CATCH;
            frame->tag = abort_tag;
            frame->vm_sp = t->vm.sp;
            frame->vm_fp = t->vm.fp;
            frame->result = CL_NIL;
            frame->dyn_mark = t->dyn_top;
            frame->handler_mark = t->handler_top;
            /* Snapshot the disabled-handler band like the vm.c/jit NLX setup
             * sites, so an unwind to this thread-abort frame restores it
             * consistently (harmless today — mask is 0 at thread entry — but
             * required once a handler is established before this frame). */
            frame->handler_active_mask = t->handler_active_mask;
            frame->restart_mark = t->restart_top;
            frame->gc_root_mark = t->gc_root_count;
            frame->mv_count = 1;
            my_nlx_idx = t->nlx_top;
            t->nlx_top++;

            if (t->restart_top < CL_MAX_RESTART_BINDINGS) {
                CL_Obj abort_restart =
                    cl_make_restart(SYM_ABORT, abort_handler,
                                    abort_report,
                                    CL_NIL, CL_NIL, abort_tag);
                {
                    CL_Restart *rp = (CL_Restart *)CL_OBJ_TO_PTR(abort_restart);
                    t->restart_stack[t->restart_top].name    = rp->name;
                    t->restart_stack[t->restart_top].handler = rp->function;
                    t->restart_stack[t->restart_top].tag     = rp->tag;
                    t->restart_stack[t->restart_top].restart = abort_restart;
                }
                my_restart_idx = t->restart_top;
                t->restart_top++;
            }

            /* abort_tag/report/handler are now rooted via the nlx frame and the
             * abort restart entry (or discarded); release the startup pins.
             * Done before CL_SETJMP so the normal path balances; the abort
             * longjmp path restores gc_root_count from frame->gc_root_mark. */
            CL_GC_UNPROTECT(3);

            if (CL_SETJMP(frame->buf) != 0)
                aborted = 1;
        }

        if (!aborted) {
            /* Re-read func from t->result: the abort-restart setup above
             * (cl_cons / cl_make_restart, and get_thread_abort_* on the first
             * thread) allocates, and any of those allocations — or a peer
             * thread's stop-the-world compaction during them — can relocate the
             * closure.  t->result is a GC root (marked+forwarded as thread
             * metadata), so it holds the live offset; the `func` C-local read
             * back at thread entry does NOT get forwarded and is now stale.
             * Applying the stale local was the multi-thread "Not a function:
             * heap object type N" corruption. */
            func = t->result;
            result = cl_vm_apply(func, NULL, 0);
            t->result = result;
        } else {
            /* (abort) was invoked — thread exits with NIL result */
            t->result = CL_NIL;
        }
        final_status = 2; /* finished (cleanly or via abort) — published below */

#ifdef DEBUG_THREAD
        fprintf(stderr, "[THR] tid=%u CT=%p finished result=0x%08x aborted=%d\n",
                t->id, (void *)t, t->result, aborted);
        fflush(stderr);
#endif

        /* Pop NLX/restart frames on normal completion.
         * Aborted path: cl_throw_to_tag truncated t->nlx_top to my_nlx_idx
         * already; replicate the OP_CATCH restore for handler/restart/dyn. */
        if (!aborted) {
            if (my_nlx_idx >= 0) t->nlx_top = my_nlx_idx;
            if (my_restart_idx >= 0) t->restart_top = my_restart_idx;
        } else {
            t->restart_top = t->nlx_stack[my_nlx_idx].restart_mark;
            t->handler_top = t->nlx_stack[my_nlx_idx].handler_mark;
            t->handler_active_mask = t->nlx_stack[my_nlx_idx].handler_active_mask;
            cl_dynbind_restore_to(t->nlx_stack[my_nlx_idx].dyn_mark);
        }
    } else {
        /* Thread errored — final_status stays at 3 (aborted by error).
         * Clear the stashed function out of t->result so JOIN-THREAD on an
         * errored worker returns NIL (its prior behaviour) rather than the
         * leftover closure object we deliberately kept there for GC during
         * the apply above. */
        t->result = CL_NIL;
        /* Diagnostic: a worker that dies via an uncaught error otherwise
         * vanishes silently (status 3), which is very hard to debug — e.g. a
         * Hunchentoot request thread aborting mid-handler just closes the
         * connection.  Surface the error code+message when CLAMIGA_THREAD_ERRORS
         * is set in the environment. */
        {
            char envbuf[8];
            if (platform_getenv("CLAMIGA_THREAD_ERRORS", envbuf, sizeof(envbuf))) {
                fprintf(stderr,
                        "[THREAD-ERROR] worker tid=%u died: err=%d msg=\"%s\"\n",
                        t->id, err,
                        t->pending_error_msg[0] ? t->pending_error_msg : "(none)");
                fflush(stderr);
            }
        }
    }

    /* CL_UNCATCH inline */
    if (t->error_frame_top > 0) {
        t->error_frame_top--;
        t->error_frames[t->error_frame_top].active = 0;
    }

    /* 6. Clear gc_roots — we're done using func */
    t->gc_root_count = 0;

    /* 6a. Publish the result into the GC-managed wrapper BEFORE unregistering.
     *   After step 7 unregisters this worker, gc_mark_thread_roots no longer
     *   marks or forwards t->result, so a peer thread's compaction that runs
     *   while JOIN-THREAD is parked (in cl_gc_leave_safe_region around
     *   pthread_join) would sweep or relocate the result object out from under
     *   the reader — JOIN-THREAD then returns a stale offset (garbage / an
     *   unrelated live object).  The wrapper (CL_ThreadObj) IS a GC-managed
     *   heap object, kept reachable by the user's thread reference and
     *   marked+forwarded via gc_{mark,update} TYPE_THREAD, so storing the
     *   result there keeps it live and current until JOIN reads it.  We are
     *   still registered here, so t->thread_obj is a valid, forwarded offset. */
    if (CL_THREAD_P(t->thread_obj)) {
        CL_ThreadObj *wrap = (CL_ThreadObj *)CL_OBJ_TO_PTR(t->thread_obj);
        wrap->result = t->result;
    }

    /* 7. Unregister from thread list BEFORE publishing the terminal status.
     *
     *   Both bi_make_thread's zombie reaper and gc_finalize_dead(TYPE_THREAD)
     *   treat `status >= 2` as the signal that `t` is safe to free.  If we
     *   set status first, there is a window where this worker is still
     *   linked in `cl_thread_list` AND status >= 2 — the reaper would free
     *   `t` (along with `t->vm.stack`, `t->vm.frames`, `t->nlx_stack`),
     *   leaving a dangling pointer in `cl_thread_list`.  The next
     *   stop-the-world `gc_mark` walks that list and SEGVs in
     *   `gc_mark_thread_roots` while reading freed memory.
     *
     *   By unregistering FIRST, status >= 2 implies "no longer in
     *   cl_thread_list", and gc_mark cannot reach a freed worker via the
     *   list walk.
     */
    cl_thread_unregister(t);

    /* 8. Publish terminal status LAST.  After this write, observers
     *    (mp:thread-alive-p, the reaper, gc_finalize_dead) may free `t` at
     *    any moment.  The OS thread must not touch `t` from here on. */
    t->status = final_status;

    return NULL;
}

/* ================================================================
 * Thread builtins
 * ================================================================ */

/* (mp:make-thread function &key name) -> thread */
static CL_Obj bi_make_thread(CL_Obj *args, int n)
{
    CL_Obj func = args[0];
    CL_Obj name = CL_NIL;
    CL_Thread *child;
    int thread_id;
    CL_ThreadObj *tobj;
    CL_Obj thread_obj;
    int i;

    /* Parse &key name */
    for (i = 1; i + 1 < n; i += 2) {
        if (args[i] == KW_NAME_THR)
            name = args[i + 1];
    }

    /* Validate function argument */
    func = cl_coerce_funcdesig(func, "MP:MAKE-THREAD");

    /* Allocate worker CL_Thread */
    child = cl_thread_alloc_worker();
    if (!child)
        cl_error(CL_ERR_STORAGE, "MP:MAKE-THREAD: cannot allocate thread");

    /* Allocate side table slot.  The table is bounded but slots are
     * reclaimed at GC sweep when the wrapping CL_ThreadObj becomes
     * unreachable (see gc_finalize_dead, TYPE_THREAD).  Run GC once
     * and retry — same pattern as bi_make_lock. */
    thread_id = cl_thread_table_alloc(child);
    if (thread_id < 0) {
        cl_gc();
        thread_id = cl_thread_table_alloc(child);
    }
    if (thread_id < 0) {
        /* GC didn't free any slot: every occupant has a still-reachable
         * wrapper.  In real workloads this happens because external
         * registries (e.g. bordeaux-threads' .known-threads. weak hash
         * — which is non-weak under cl-amiga today) hold wrappers for
         * workers that have long since finished.  Reap any slot whose
         * worker reached status >= 2 (finished/aborted): NULL the slot,
         * detach the OS handle, free the worker.  The wrapper stays
         * alive (so EQ identity survives, name accessor still works),
         * and gc_finalize_dead's `t->thread_obj == this wrapper` guard
         * prevents the wrapper's eventual finalize from double-freeing
         * an unrelated worker if the slot is later reused. */
        CL_Thread *zombie;
        int i;
        platform_mutex_lock(cl_thread_list_lock);
        for (i = 1; i < CL_MAX_THREADS; i++) {
            zombie = cl_thread_table[i];
            if (!zombie || zombie->status < 2) continue;
            cl_thread_table[i] = NULL;
            if (zombie->platform_handle) {
                platform_thread_detach(zombie->platform_handle);
                zombie->platform_handle = NULL;
            }
            cl_thread_free_worker(zombie);
        }
        platform_mutex_unlock(cl_thread_list_lock);
        thread_id = cl_thread_table_alloc(child);
    }
    if (thread_id < 0) {
        cl_thread_free_worker(child);
        cl_error(CL_ERR_GENERAL, "MP:MAKE-THREAD: thread table full (max %d)",
                 CL_MAX_THREADS);
    }

    child->id = (uint32_t)thread_id;
    child->name = name;

    /* A new thread starts with a FRESH dynamic environment: per CL /
     * bordeaux-threads semantics it sees only the GLOBAL values of special
     * variables, never the dynamic (LET/PROGV) bindings active in the parent
     * at spawn time.  Global values live in the symbol's value slot; the
     * worker's TLV table holds only that thread's own active bindings.  Since
     * cl_thread_alloc_worker() zeroes the worker, its TLV table is already
     * empty (tlv_entry_count == 0) and reads fall through to globals — so we
     * deliberately do NOT copy the parent's TLV table here. */

    /* Stash func in child->result for the entry wrapper to retrieve.
     *
     * child->result (and child->name above) are GC roots automatically: once
     * the child is registered, gc_mark_thread_roots marks t->result / t->name
     * as thread metadata and gc_update_thread_roots forwards those exact slots
     * after a compaction (see mem.c).  So the closure stays live and its offset
     * is kept current across any peer thread's stop-the-world GC.
     *
     * Do NOT also register &child->result / &child->name in child->gc_roots[].
     * gc_update_thread_roots would then forward each slot TWICE — once via the
     * gc_roots[] entry and once via the direct t->result / t->name update — and
     * gc_forward() is not idempotent: re-forwarding an already-relocated offset
     * re-maps it through whatever live object now occupies that arena slot,
     * leaving child->result pointing at the wrong object.  That was the
     * multi-thread "func has wrong type at thread entry → TYPE-ERROR" corruption
     * exposed once concurrent make-thread stopped hanging.  gc_root_count stays
     * 0; the worker uses gc_roots[] for its own CL_GC_PROTECT calls. */
    child->result = func;

    /* Register child in thread list BEFORE creating OS thread, so GC can mark
     * its roots even though the child hasn't started yet.  Register it as a
     * NEWBORN (gc_live == 0): it has no OS thread and cannot reach a safepoint,
     * so a concurrent stop-the-world must NOT wait for it — otherwise the world
     * hangs forever waiting for a thread that will never stop.  The child flips
     * itself to live via cl_gc_thread_online once its OS thread starts. */
    cl_thread_register_newborn(child);

    /* Create the Lisp-visible thread object */
    CL_GC_PROTECT(func);
    CL_GC_PROTECT(name);
    tobj = (CL_ThreadObj *)cl_alloc(TYPE_THREAD, sizeof(CL_ThreadObj));
    CL_GC_UNPROTECT(2);
    if (!tobj) {
        cl_thread_unregister(child);
        cl_thread_table_free(thread_id);
        cl_thread_free_worker(child);
        cl_error(CL_ERR_STORAGE, "MP:MAKE-THREAD: cannot allocate thread object");
    }
    tobj->thread_id = (uint32_t)thread_id;
    tobj->name = name;
    thread_obj = CL_PTR_TO_OBJ(tobj);

    /* Cache it on the worker so (mp:current-thread) returns the same CL_Obj.
     * Required for bordeaux-threads-2 .known-threads. eql lookups. */
    child->thread_obj = thread_obj;

    /* Create OS thread.  Pass an explicit C stack size (CL_WORKER_C_STACK_SIZE)
     * rather than 0/OS-default: the default is far smaller than the main
     * thread's stack (512KB vs 8MB on macOS, 64KB on AmigaOS), which would make
     * a worker crash/corrupt at a call depth main handles.  See the define. */
    if (platform_thread_create(&child->platform_handle,
                               thread_entry, child,
                               CL_WORKER_C_STACK_SIZE) != 0) {
        cl_thread_unregister(child);
        cl_thread_table_free(thread_id);
        cl_thread_free_worker(child);
        cl_error(CL_ERR_GENERAL, "MP:MAKE-THREAD: failed to create OS thread");
    }

    return thread_obj;
}

/* (mp:join-thread thread) -> result */
static CL_Obj bi_join_thread(CL_Obj *args, int n)
{
    CL_ThreadObj *tobj;
    CL_Thread *t;
    CL_Obj result;
    CL_UNUSED(n);

    if (!CL_THREAD_P(args[0]))
        cl_error(CL_ERR_TYPE, "MP:JOIN-THREAD: argument must be a thread");

    tobj = (CL_ThreadObj *)CL_OBJ_TO_PTR(args[0]);
    if (tobj->thread_id >= CL_MAX_THREADS)
        cl_error(CL_ERR_GENERAL, "MP:JOIN-THREAD: invalid thread id");

    t = cl_thread_table[tobj->thread_id];
    if (!t)
        cl_error(CL_ERR_GENERAL, "MP:JOIN-THREAD: thread already collected");

    if (!t->platform_handle)
        cl_error(CL_ERR_GENERAL, "MP:JOIN-THREAD: cannot join main thread");

    /* Wait for thread to finish.  pthread_join blocks the caller outside
     * the VM dispatch loop — bracket it with the safe-region marker so a
     * concurrent stop-the-world GC counts us as already stopped. */
    cl_gc_enter_safe_region();
    platform_thread_join(t->platform_handle, NULL);
    cl_gc_leave_safe_region();

    /* Re-derive tobj: cl_gc_leave_safe_region() can PARK this thread while a
     * peer thread runs a stop-the-world compaction, which relocates the wrapper
     * object.  The `tobj` C-pointer computed before the safe region is then
     * stale — it points at the wrapper's pre-move address.  Using it below would
     * read a garbage thread_id (so cl_thread_table_free clears the wrong slot,
     * leaving cl_thread_table[id] pointing at the worker we free here) and write
     * `-1` to the stale address (so the real wrapper keeps thread_id=id).  The
     * result: gc_finalize_dead later frees this same worker a SECOND time —
     * the multi-thread "pointer being freed was not allocated" crash.  args[0]
     * is a VM-stack root, forwarded by the compaction, so re-read through it. */
    tobj = (CL_ThreadObj *)CL_OBJ_TO_PTR(args[0]);

    /* Read the result from the GC-managed wrapper, not the worker's t->result:
     * the worker published it there before unregistering (see thread_entry step
     * 6a), and the wrapper's slot is marked+forwarded across any compaction that
     * ran while we were parked in the safe region above.  t->result is no longer
     * a GC root once the worker unregistered, so it may be stale here. */
    result = tobj->result;

    /* Clean up worker thread resources, then invalidate this wrapper's
     * slot index.  Without the invalidation, if MAKE-THREAD reuses the
     * slot for a new worker, this wrapper still points at thread_id=N
     * which now refers to a different CL_Thread; when the wrapper later
     * dies, gc_finalize_dead would free the unrelated worker.  Setting
     * thread_id out of range makes finalize skip this wrapper. */
    cl_thread_table_free((int)tobj->thread_id);
    cl_thread_free_worker(t);
    tobj->thread_id = (uint32_t)-1;
    /* platform_handle was free()d inside platform_thread_join; clear so
     * any subsequent code that might pick up `t` doesn't dereference. */
    /* (t itself is gone now — nothing more to do) */

    return result;
}

/* (mp:thread-alive-p thread) -> bool */
static CL_Obj bi_thread_alive_p(CL_Obj *args, int n)
{
    CL_ThreadObj *tobj;
    CL_Thread *t;
    CL_UNUSED(n);

    if (!CL_THREAD_P(args[0]))
        cl_error(CL_ERR_TYPE, "MP:THREAD-ALIVE-P: argument must be a thread");

    tobj = (CL_ThreadObj *)CL_OBJ_TO_PTR(args[0]);
    if (tobj->thread_id >= CL_MAX_THREADS)
        return CL_NIL;

    t = cl_thread_table[tobj->thread_id];
    if (!t) return CL_NIL;

    /* status: 0=created, 1=running, 2=finished, 3=aborted */
    return (t->status <= 1) ? CL_T : CL_NIL;
}

/* (mp:current-thread) -> thread */
static CL_Obj bi_current_thread(CL_Obj *args, int n)
{
    CL_Thread *self = (CL_Thread *)platform_tls_get();
    CL_UNUSED(args);
    CL_UNUSED(n);

    /* Main thread returns the pre-allocated object */
    if (self->id == 0)
        return main_thread_obj;

    /* Worker threads return the cached thread object set at make-thread time.
     * Identity matters: bordeaux-threads-2 keys .known-threads. by the value
     * returned here, then looks up via (mp:current-thread) inside the new
     * thread; the two must be eql. */
    if (!CL_NULL_P(self->thread_obj))
        return self->thread_obj;

    /* Fallback (should not normally happen): allocate a fresh wrapper and
     * cache it so subsequent calls return the same object. */
    {
        CL_ThreadObj *tobj = (CL_ThreadObj *)cl_alloc(TYPE_THREAD,
                                                       sizeof(CL_ThreadObj));
        if (!tobj) return CL_NIL;
        tobj->thread_id = self->id;
        tobj->name = self->name;
        self->thread_obj = CL_PTR_TO_OBJ(tobj);
        return self->thread_obj;
    }
}

/* (mp:all-threads) -> list */
static CL_Obj bi_all_threads(CL_Obj *args, int n)
{
    CL_Obj result = CL_NIL;
    CL_Obj wrapper;
    int i;
    CL_UNUSED(args);
    CL_UNUSED(n);

    CL_GC_PROTECT(result);
    for (i = CL_MAX_THREADS - 1; i >= 0; i--) {
        CL_Thread *t = cl_thread_table[i];
        if (!t) continue;

        if (i == 0) {
            /* Main thread */
            result = cl_cons(main_thread_obj, result);
        } else {
            /* Skip finished (status=2) / aborted (status=3) workers.
             * The slot+wrapper survive until the wrapper is GC'd so
             * accessors like THREAD-NAME still work on a held handle,
             * but ALL-THREADS must report only live threads
             * (bordeaux-threads / SBCL / CCL contract). */
            if (t->status >= 2) continue;

            /* Reuse the canonical wrapper that was created at make-thread
             * time (stashed in CL_Thread->thread_obj).  Allocating a
             * fresh wrapper would create an alias with the same
             * thread_id; when one wrapper later dies, gc_finalize_dead
             * would race over which one "owns" the slot. */
            wrapper = t->thread_obj;
            if (CL_NULL_P(wrapper)) continue;
            result = cl_cons(wrapper, result);
        }
    }
    CL_GC_UNPROTECT(1);
    return result;
}

/* Print a CL string name to stderr (NIL -> "(unnamed)"), char by char so we
 * need no transient C buffer allocation. */
static void dump_print_name(CL_Obj name)
{
    if (CL_NULL_P(name) || !CL_STRING_P(name)) {
        fprintf(stderr, "(unnamed)");
        return;
    }
    {
        uint32_t len = cl_string_length(name), i;
        for (i = 0; i < len; i++) {
            int c = cl_string_char_at(name, i);
            fputc((c >= 32 && c < 127) ? c : '?', stderr);
        }
    }
}

/* (mp:dump-thread-waits) -> nil
 * Diagnostic: print, for every live thread, what synchronization primitive it
 * is currently blocked on.  Distinguishes a lost-wakeup (a worker still parked
 * in condwait on its queue condvar after the producer already notified) from a
 * lock-ordering deadlock (a thread blocked acquiring a held lock).  Intended to
 * be called from a watchdog when a hang is detected. */
static CL_Obj bi_dump_thread_waits(CL_Obj *args, int n)
{
    int i;
    CL_UNUSED(args);
    CL_UNUSED(n);
    fprintf(stderr, "==== MP:DUMP-THREAD-WAITS (%u live threads) ====\n",
            cl_thread_count);
    for (i = 0; i < CL_MAX_THREADS; i++) {
        CL_Thread *t = cl_thread_table[i];
        const char *st, *wk;
        if (!t) continue;
        switch (t->status) {
        case 0: st = "created";  break;
        case 1: st = "running";  break;
        case 2: st = "finished"; break;
        case 3: st = "aborted";  break;
        default: st = "?";       break;
        }
        switch (t->wait_kind) {
        case 0: wk = "RUN";                 break;
        case 1: wk = "CONDWAIT";            break;
        case 2: wk = "CONDWAIT/timeout";    break;
        case 3: wk = "LOCK-ACQUIRE(block)"; break;
        case 4: wk = "GC-STW-WAIT";         break;
        default: wk = "?";                  break;
        }
        fprintf(stderr, "  tid=%-3u status=%-8s name=\"", t->id, st);
        dump_print_name(t->name);
        fprintf(stderr, "\" %s", wk);
        if (t->wait_kind == 1 || t->wait_kind == 2)
            fprintf(stderr, " cv=%d lock=%d", t->wait_cv_id, t->wait_lock_id);
        else if (t->wait_kind == 3)
            fprintf(stderr, " lock=%d", t->wait_lock_id);
        else if (t->wait_kind == 4)
            fprintf(stderr, " waiting-for-tid=%d", t->wait_lock_id);
        /* GC coordination flags — a thread with gc_req=1 but stopped=0 and
         * safe=0 is the straggler holding up a stop-the-world GC. */
        fprintf(stderr, " [gc_req=%d stopped=%d safe=%d]",
                (int)t->gc_requested, (int)t->gc_stopped, (int)t->in_safe_region);
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "==== end ====\n");
    fflush(stderr);
    return CL_NIL;
}

/* (mp:thread-name thread) -> string/nil */
static CL_Obj bi_thread_name(CL_Obj *args, int n)
{
    CL_ThreadObj *tobj;
    CL_UNUSED(n);

    if (!CL_THREAD_P(args[0]))
        cl_error(CL_ERR_TYPE, "MP:THREAD-NAME: argument must be a thread");

    tobj = (CL_ThreadObj *)CL_OBJ_TO_PTR(args[0]);
    return tobj->name;
}

/* (mp:thread-yield) -> nil */
static CL_Obj bi_thread_yield(CL_Obj *args, int n)
{
    CL_UNUSED(args);
    CL_UNUSED(n);
    platform_thread_yield();
    return CL_NIL;
}

/* ================================================================
 * Lock builtins
 * ================================================================ */

/* (mp:make-lock &optional name) -> lock */
/* Allocate a fresh CL_Lock with a freshly-initialized platform mutex.
 * recursive != 0 selects PTHREAD_MUTEX_RECURSIVE on POSIX; on AmigaOS
 * SignalSemaphore is recursive in either case.  Used by MP:MAKE-LOCK,
 * MP:MAKE-RECURSIVE-LOCK, and the FASL reader (so a lock embedded in a
 * struct/closure constant comes back as a usable, fresh-at-load-time
 * lock instead of NIL).  Errors out via cl_error() — the err_prefix
 * parameter customizes the message so callers see e.g. "MP:MAKE-LOCK: ..."
 * vs. "FASL: ...". */
CL_Obj cl_lock_alloc_obj(int recursive, CL_Obj name, const char *err_prefix)
{
    void *mutex_handle = NULL;
    int lock_id;
    CL_Lock *lk;
    int rc = recursive
        ? platform_mutex_init_recursive(&mutex_handle)
        : platform_mutex_init(&mutex_handle);

    if (rc != 0)
        cl_error(CL_ERR_GENERAL, "%s: failed to create mutex", err_prefix);

    lock_id = cl_lock_table_alloc(mutex_handle);
    if (lock_id < 0) {
        /* Lock table is bounded but slots are reclaimed at GC sweep when
         * the wrapping CL_Lock heap object becomes unreachable.  Run GC
         * once and retry — typical pattern for GC-managed external slots. */
        cl_gc();
        lock_id = cl_lock_table_alloc(mutex_handle);
    }
    if (lock_id < 0) {
        platform_mutex_destroy(mutex_handle);
        cl_error(CL_ERR_GENERAL, "%s: lock table full (max %d)",
                 err_prefix, CL_MAX_LOCKS);
    }

    CL_GC_PROTECT(name);
    lk = (CL_Lock *)cl_alloc(TYPE_LOCK, sizeof(CL_Lock));
    CL_GC_UNPROTECT(1);
    if (!lk) {
        cl_lock_table_free(lock_id);
        platform_mutex_destroy(mutex_handle);
        cl_error(CL_ERR_STORAGE, "%s: cannot allocate lock object",
                 err_prefix);
    }

    lk->lock_id = (uint32_t)lock_id;
    lk->name = name;
    lk->flags = recursive ? CL_LOCK_FLAG_RECURSIVE : 0;
    return CL_PTR_TO_OBJ(lk);
}

static CL_Obj bi_make_lock(CL_Obj *args, int n)
{
    CL_Obj name = (n > 0) ? args[0] : CL_NIL;
    return cl_lock_alloc_obj(0, name, "MP:MAKE-LOCK");
}

/* (mp:make-recursive-lock &optional name) -> lock
 * A recursive lock can be acquired multiple times by the same thread; it must
 * be released the same number of times before another thread can acquire it.
 * On AmigaOS this is identical to make-lock (SignalSemaphore is naturally
 * recursive); on POSIX it uses PTHREAD_MUTEX_RECURSIVE. */
static CL_Obj bi_make_recursive_lock(CL_Obj *args, int n)
{
    CL_Obj name = (n > 0) ? args[0] : CL_NIL;
    return cl_lock_alloc_obj(1, name, "MP:MAKE-RECURSIVE-LOCK");
}

/* (mp:acquire-lock lock &optional wait) -> bool */
static CL_Obj bi_acquire_lock(CL_Obj *args, int n)
{
    CL_Lock *lk;
    void *mutex;
    int wait_p = 1;  /* default: blocking */

    if (!CL_LOCK_P(args[0]))
        cl_error(CL_ERR_TYPE, "MP:ACQUIRE-LOCK: argument must be a lock");

    if (n > 1 && CL_NULL_P(args[1]))
        wait_p = 0;

    lk = (CL_Lock *)CL_OBJ_TO_PTR(args[0]);
    if (lk->lock_id >= CL_MAX_LOCKS)
        cl_error(CL_ERR_GENERAL, "MP:ACQUIRE-LOCK: invalid lock id");

    mutex = cl_lock_table[lk->lock_id];
    if (!mutex)
        cl_error(CL_ERR_GENERAL, "MP:ACQUIRE-LOCK: lock has been destroyed");

    if (wait_p) {
        /* Blocking acquire — bracket with safe region so a concurrent
         * stop-the-world GC does not deadlock waiting on this thread. */
        CL_Thread *self = CL_MT() ? cl_get_current_thread() : NULL;
        if (self) {
            self->wait_kind = 3;
            self->wait_lock_id = (int)lk->lock_id;
            self->wait_cv_id = -1;
        }
        cl_gc_enter_safe_region();
        platform_mutex_lock(mutex);
        cl_gc_leave_safe_region();
        if (self) self->wait_kind = 0;
        return CL_T;
    } else {
        return (platform_mutex_trylock(mutex) == 0) ? CL_T : CL_NIL;
    }
}

/* (mp:release-lock lock) -> nil */
static CL_Obj bi_release_lock(CL_Obj *args, int n)
{
    CL_Lock *lk;
    void *mutex;
    CL_UNUSED(n);

    if (!CL_LOCK_P(args[0]))
        cl_error(CL_ERR_TYPE, "MP:RELEASE-LOCK: argument must be a lock");

    lk = (CL_Lock *)CL_OBJ_TO_PTR(args[0]);
    if (lk->lock_id >= CL_MAX_LOCKS)
        cl_error(CL_ERR_GENERAL, "MP:RELEASE-LOCK: invalid lock id");

    mutex = cl_lock_table[lk->lock_id];
    if (!mutex)
        cl_error(CL_ERR_GENERAL, "MP:RELEASE-LOCK: lock has been destroyed");

    platform_mutex_unlock(mutex);
    return CL_NIL;
}

/* ================================================================
 * Condition variable builtins
 * ================================================================ */

/* (mp:make-condition-variable &optional name) -> cv */
static CL_Obj bi_make_condition_variable(CL_Obj *args, int n)
{
    CL_Obj name = (n > 0) ? args[0] : CL_NIL;
    void *cv_handle = NULL;
    int cv_id;
    CL_CondVar *cv;

    if (platform_condvar_init(&cv_handle) != 0)
        cl_error(CL_ERR_GENERAL,
                 "MP:MAKE-CONDITION-VARIABLE: failed to create condvar");

    cv_id = cl_condvar_table_alloc(cv_handle);
    if (cv_id < 0) {
        /* See bi_make_lock for rationale on the GC retry. */
        cl_gc();
        cv_id = cl_condvar_table_alloc(cv_handle);
    }
    if (cv_id < 0) {
        platform_condvar_destroy(cv_handle);
        cl_error(CL_ERR_GENERAL,
                 "MP:MAKE-CONDITION-VARIABLE: condvar table full (max %d)",
                 CL_MAX_CONDVARS);
    }

    CL_GC_PROTECT(name);
    cv = (CL_CondVar *)cl_alloc(TYPE_CONDVAR, sizeof(CL_CondVar));
    CL_GC_UNPROTECT(1);
    if (!cv) {
        cl_condvar_table_free(cv_id);
        platform_condvar_destroy(cv_handle);
        cl_error(CL_ERR_STORAGE,
                 "MP:MAKE-CONDITION-VARIABLE: cannot allocate condvar object");
    }

    cv->condvar_id = (uint32_t)cv_id;
    cv->name = name;
    return CL_PTR_TO_OBJ(cv);
}

/* (mp:condition-wait cv lock &optional timeout) -> bool
 * timeout is in seconds (real number).  Returns T if signaled, NIL if timed out. */
static CL_Obj bi_condition_wait(CL_Obj *args, int n)
{
    CL_CondVar *cv;
    CL_Lock *lk;
    void *cv_handle, *mutex;

    if (!CL_CONDVAR_P(args[0]))
        cl_error(CL_ERR_TYPE,
                 "MP:CONDITION-WAIT: first argument must be a condition-variable");
    if (!CL_LOCK_P(args[1]))
        cl_error(CL_ERR_TYPE,
                 "MP:CONDITION-WAIT: second argument must be a lock");

    cv = (CL_CondVar *)CL_OBJ_TO_PTR(args[0]);
    lk = (CL_Lock *)CL_OBJ_TO_PTR(args[1]);

    cv_handle = cl_condvar_table[cv->condvar_id];
    mutex = cl_lock_table[lk->lock_id];

    if (!cv_handle || !mutex)
        cl_error(CL_ERR_GENERAL,
                 "MP:CONDITION-WAIT: condvar or lock has been destroyed");

    /* If we're about to park while holding a cl_tables_rwlock reader, every
     * other thread that needs to take the writer lock will block forever.
     * Treat as a hard bug: dump the holders and abort so we can find the
     * leaky path before we ship a deadlock. */
    if (CL_MT() && cl_get_current_thread()->rdlock_tables_held > 0) {
        cl_tables_dump_rdlock_holders(
            "[BUG] mp:condition-wait while holding cl_tables_rwlock reader:");
        cl_capture_backtrace();
        fprintf(stderr, "%s", cl_get_current_thread()->backtrace_buf);
        fflush(stderr);
        abort();
    }

    if (n > 2 && !CL_NULL_P(args[2])) {
        /* Timed wait: convert seconds to milliseconds */
        double secs = cl_to_double(args[2]);
        uint32_t ms;
        int timed_out;
        CL_Thread *self = CL_MT() ? cl_get_current_thread() : NULL;
        if (secs <= 0.0)
            ms = 0;
        else if (secs > 4294967.0)
            ms = 0xFFFFFFFFu;
        else
            ms = (uint32_t)(secs * 1000.0);
        if (self) {
            self->wait_kind = 2;
            self->wait_cv_id = (int)cv->condvar_id;
            self->wait_lock_id = (int)lk->lock_id;
        }
        cl_gc_enter_safe_region();
        timed_out = platform_condvar_wait_timeout(cv_handle, mutex, ms);
        cl_gc_leave_safe_region();
        if (self) self->wait_kind = 0;
        return timed_out ? CL_NIL : CL_T;
    }

    {
        CL_Thread *self = CL_MT() ? cl_get_current_thread() : NULL;
        if (self) {
            self->wait_kind = 1;
            self->wait_cv_id = (int)cv->condvar_id;
            self->wait_lock_id = (int)lk->lock_id;
        }
        cl_gc_enter_safe_region();
        platform_condvar_wait(cv_handle, mutex);
        cl_gc_leave_safe_region();
        if (self) self->wait_kind = 0;
    }
    return CL_T;
}

/* (mp:condition-notify cv) -> nil */
static CL_Obj bi_condition_notify(CL_Obj *args, int n)
{
    CL_CondVar *cv;
    void *cv_handle;
    CL_UNUSED(n);

    if (!CL_CONDVAR_P(args[0]))
        cl_error(CL_ERR_TYPE,
                 "MP:CONDITION-NOTIFY: argument must be a condition-variable");

    cv = (CL_CondVar *)CL_OBJ_TO_PTR(args[0]);
    cv_handle = cl_condvar_table[cv->condvar_id];

    if (!cv_handle)
        cl_error(CL_ERR_GENERAL,
                 "MP:CONDITION-NOTIFY: condvar has been destroyed");

    platform_condvar_signal(cv_handle);
    return CL_NIL;
}

/* (mp:condition-broadcast cv) -> nil */
static CL_Obj bi_condition_broadcast(CL_Obj *args, int n)
{
    CL_CondVar *cv;
    void *cv_handle;
    CL_UNUSED(n);

    if (!CL_CONDVAR_P(args[0]))
        cl_error(CL_ERR_TYPE,
                 "MP:CONDITION-BROADCAST: argument must be a condition-variable");

    cv = (CL_CondVar *)CL_OBJ_TO_PTR(args[0]);
    cv_handle = cl_condvar_table[cv->condvar_id];

    if (!cv_handle)
        cl_error(CL_ERR_GENERAL,
                 "MP:CONDITION-BROADCAST: condvar has been destroyed");

    platform_condvar_broadcast(cv_handle);
    return CL_NIL;
}

/* ================================================================
 * Thread interruption
 * ================================================================ */

/* (mp:interrupt-thread thread function) -> t */
static CL_Obj bi_interrupt_thread(CL_Obj *args, int n)
{
    CL_ThreadObj *tobj;
    CL_Thread *self, *target;
    CL_Obj func;
    CL_UNUSED(n);

    if (!CL_THREAD_P(args[0]))
        cl_error(CL_ERR_TYPE,
                 "MP:INTERRUPT-THREAD: first argument must be a thread");

    func = cl_coerce_funcdesig(args[1], "MP:INTERRUPT-THREAD");

    tobj = (CL_ThreadObj *)CL_OBJ_TO_PTR(args[0]);
    if (tobj->thread_id >= CL_MAX_THREADS)
        cl_error(CL_ERR_GENERAL, "MP:INTERRUPT-THREAD: invalid thread id");

    self = (CL_Thread *)platform_tls_get();

    /* Self-interruption: call the function directly */
    if (tobj->thread_id == self->id) {
        cl_vm_apply(func, NULL, 0);
        return CL_T;
    }

    target = cl_thread_table[tobj->thread_id];
    if (!target)
        cl_error(CL_ERR_GENERAL,
                 "MP:INTERRUPT-THREAD: thread has already exited");

    /* status: 0=created, 1=running, 2=finished, 3=aborted */
    if (target->status >= 2)
        cl_error(CL_ERR_GENERAL,
                 "MP:INTERRUPT-THREAD: thread is no longer running");

    /* Store function and set pending flag.
     * Write order matters: func before flag (strongly ordered on x86/68020). */
    target->interrupt_func = func;
    target->interrupt_pending = 1;

    return CL_T;
}

/* (mp:destroy-thread thread) -> t */
static CL_Obj bi_destroy_thread(CL_Obj *args, int n)
{
    CL_ThreadObj *tobj;
    CL_Thread *self, *target;
    CL_UNUSED(n);

    if (!CL_THREAD_P(args[0]))
        cl_error(CL_ERR_TYPE,
                 "MP:DESTROY-THREAD: argument must be a thread");

    tobj = (CL_ThreadObj *)CL_OBJ_TO_PTR(args[0]);
    if (tobj->thread_id >= CL_MAX_THREADS)
        cl_error(CL_ERR_GENERAL, "MP:DESTROY-THREAD: invalid thread id");

    self = (CL_Thread *)platform_tls_get();

    /* Self-destruction: abort immediately (quietly, like the interrupt path) */
    if (tobj->thread_id == self->id)
        cl_abort_current_thread("Thread destroyed");

    target = cl_thread_table[tobj->thread_id];
    if (!target)
        cl_error(CL_ERR_GENERAL,
                 "MP:DESTROY-THREAD: thread has already exited");

    if (target->status >= 2)
        cl_error(CL_ERR_GENERAL,
                 "MP:DESTROY-THREAD: thread is no longer running");

    /* Set destroy flag and pending flag */
    target->destroy_requested = 1;
    target->interrupt_pending = 1;

    return CL_T;
}

/* ================================================================
 * Accessors and predicates
 * ================================================================ */

/* (mp:condition-name cv) -> string/nil */
static CL_Obj bi_condition_name(CL_Obj *args, int n)
{
    CL_CondVar *cv;
    CL_UNUSED(n);

    if (!CL_CONDVAR_P(args[0]))
        cl_error(CL_ERR_TYPE,
                 "MP:CONDITION-NAME: argument must be a condition-variable");

    cv = (CL_CondVar *)CL_OBJ_TO_PTR(args[0]);
    return cv->name;
}

/* (mp:lock-name lock) -> string/nil */
static CL_Obj bi_lock_name(CL_Obj *args, int n)
{
    CL_Lock *lk;
    CL_UNUSED(n);

    if (!CL_LOCK_P(args[0]))
        cl_error(CL_ERR_TYPE, "MP:LOCK-NAME: argument must be a lock");

    lk = (CL_Lock *)CL_OBJ_TO_PTR(args[0]);
    return lk->name;
}

/* (mp:threadp obj) -> bool */
static CL_Obj bi_threadp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_THREAD_P(args[0]) ? CL_T : CL_NIL;
}

/* (mp:lockp obj) -> bool */
static CL_Obj bi_lockp(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_LOCK_P(args[0]) ? CL_T : CL_NIL;
}

/* (mp:condition-variable-p obj) -> bool */
static CL_Obj bi_condition_variable_p(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_CONDVAR_P(args[0]) ? CL_T : CL_NIL;
}

/* ================================================================
 * Registration
 * ================================================================ */

/* Helper: register a builtin in the MP package and export it */
static void mp_defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_mp);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    cl_export_symbol(sym, cl_package_mp);
}

void cl_builtins_thread_init(void)
{
    CL_ThreadObj *tobj;
    CL_Obj main_name;

    /* Pre-intern keywords */
    KW_NAME_THR = cl_intern_keyword("NAME", 4);

    /* Register the lazily-built abort handler/report caches as GC roots
     * exactly once, here at (single-threaded) boot.  Doing it inside the
     * lazy getters raced: two concurrent first-worker creations could
     * both register the same address, and gc_forward's non-idempotence
     * turns a double-registered root into silent corruption on the next
     * compaction. */
    cl_gc_register_root(&thread_abort_handler);
    cl_gc_register_root(&thread_abort_report);

    /* Create main thread's Lisp-visible thread object.  Give it a default
     * name ("main thread") to match bordeaux-threads / SBCL / CCL, which
     * name the initial thread rather than leaving it NIL — so ALL-THREADS
     * prints #<THREAD main thread> instead of #<THREAD NIL>.
     *
     * Build the name string first and GC-protect it: cl_alloc(TYPE_THREAD)
     * below can trigger compaction, which would relocate an unprotected
     * string. */
    main_name = cl_make_string("main thread", 11);
    CL_GC_PROTECT(main_name);
    tobj = (CL_ThreadObj *)cl_alloc(TYPE_THREAD, sizeof(CL_ThreadObj));
    if (tobj) {
        tobj->thread_id = 0;
        tobj->name = main_name;
        main_thread_obj = CL_PTR_TO_OBJ(tobj);
    }
    CL_GC_UNPROTECT(1);

    /* Keep the C-level main-thread record's name in sync, so the
     * bi_current_thread fallback path (which copies self->name) and any
     * THREAD-NAME on a freshly-built wrapper agree with main_thread_obj. */
    if (cl_main_thread_ptr)
        cl_main_thread_ptr->name = main_name;

    /* Register MP builtins */
    mp_defun("MAKE-THREAD",             bi_make_thread,             1, -1);
    mp_defun("JOIN-THREAD",             bi_join_thread,             1, 1);
    mp_defun("THREAD-ALIVE-P",          bi_thread_alive_p,         1, 1);
    mp_defun("CURRENT-THREAD",          bi_current_thread,          0, 0);
    mp_defun("ALL-THREADS",             bi_all_threads,             0, 0);
    mp_defun("THREAD-NAME",             bi_thread_name,             1, 1);
    mp_defun("THREAD-YIELD",            bi_thread_yield,            0, 0);
    mp_defun("DUMP-THREAD-WAITS",       bi_dump_thread_waits,       0, 0);

    mp_defun("MAKE-LOCK",               bi_make_lock,               0, 1);
    mp_defun("%MAKE-RECURSIVE-LOCK",    bi_make_recursive_lock,     0, 1);
    mp_defun("ACQUIRE-LOCK",            bi_acquire_lock,            1, 2);
    mp_defun("RELEASE-LOCK",            bi_release_lock,            1, 1);

    mp_defun("MAKE-CONDITION-VARIABLE",  bi_make_condition_variable, 0, 1);
    mp_defun("CONDITION-WAIT",           bi_condition_wait,          2, 3);
    mp_defun("CONDITION-NOTIFY",         bi_condition_notify,        1, 1);
    mp_defun("CONDITION-BROADCAST",      bi_condition_broadcast,     1, 1);
    mp_defun("CONDITION-NAME",           bi_condition_name,          1, 1);

    mp_defun("LOCK-NAME",               bi_lock_name,               1, 1);

    mp_defun("INTERRUPT-THREAD",         bi_interrupt_thread,        2, 2);
    mp_defun("DESTROY-THREAD",           bi_destroy_thread,          1, 1);

    mp_defun("THREADP",                  bi_threadp,                 1, 1);
    mp_defun("LOCKP",                    bi_lockp,                   1, 1);
    mp_defun("CONDITION-VARIABLE-P",     bi_condition_variable_p,    1, 1);

    /* Register cached symbols for GC compaction forwarding */
    cl_gc_register_root(&KW_NAME_THR);
}
