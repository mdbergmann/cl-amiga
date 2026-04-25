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
#include "../platform/platform.h"
#include "../platform/platform_thread.h"
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Pre-interned keyword symbols
 * ================================================================ */

static CL_Obj KW_NAME_THR = 0;  /* :NAME keyword for make-thread */

/* ================================================================
 * Main thread's Lisp-visible thread object
 * ================================================================ */

static CL_Obj main_thread_obj = CL_NIL;

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

    /* 2. TLV snapshot was done by parent before thread_create */

    /* 3. Mark as running */
    t->status = 1;

    /* 4. Retrieve stashed function (stored in result field by parent) */
    func = t->result;
    t->result = CL_NIL;

    /* GC-protect func via gc_roots (already set up by parent) */

    /* 5. Call user function inside error handler.
     *    CL_CATCH/CL_UNCATCH macros use compatibility names that are
     *    suppressed by CL_THREAD_NO_MACROS, so expand them inline. */
    if (t->error_frame_top < CL_MAX_ERROR_FRAMES) {
        t->error_frames[t->error_frame_top].active = 1;
        err = setjmp(t->error_frames[t->error_frame_top++].buf);
    } else {
        err = CL_ERR_OVERFLOW;
    }

    if (err == 0) {
        CL_Obj result = cl_vm_apply(func, NULL, 0);
        t->result = result;
        t->status = 2; /* finished */
    } else {
        /* Thread aborted due to error */
        t->status = 3; /* aborted */
    }

    /* CL_UNCATCH inline */
    if (t->error_frame_top > 0) {
        t->error_frame_top--;
        t->error_frames[t->error_frame_top].active = 0;
    }

    /* 6. Clear gc_roots — we're done using func */
    t->gc_root_count = 0;

    /* 7. Unregister from thread list */
    cl_thread_unregister(t);

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
    CL_Thread *parent;
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

    /* Allocate side table slot */
    thread_id = cl_thread_table_alloc(child);
    if (thread_id < 0) {
        cl_thread_free_worker(child);
        cl_error(CL_ERR_GENERAL, "MP:MAKE-THREAD: thread table full (max %d)",
                 CL_MAX_THREADS);
    }

    child->id = (uint32_t)thread_id;
    child->name = name;

    /* Snapshot parent's TLV table to child */
    parent = (CL_Thread *)platform_tls_get();
    cl_tlv_snapshot(child, parent);

    /* Stash func in child->result for the entry wrapper to retrieve */
    child->result = func;

    /* GC-protect func: set up gc_roots in child so GC can mark it */
    child->gc_roots[0] = &child->result;
    child->gc_root_count = 1;

    /* Also protect child->name */
    if (!CL_NULL_P(name)) {
        child->gc_roots[1] = &child->name;
        child->gc_root_count = 2;
    }

    /* Register child in thread list BEFORE creating OS thread,
     * so GC can mark its roots even if child hasn't started yet */
    cl_thread_register(child);

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

    /* Create OS thread */
    if (platform_thread_create(&child->platform_handle,
                               thread_entry, child, 0) != 0) {
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

    /* Wait for thread to finish */
    platform_thread_join(t->platform_handle, NULL);

    result = t->result;

    /* Clean up worker thread resources */
    cl_thread_table_free((int)tobj->thread_id);
    cl_thread_free_worker(t);

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
    CL_Obj thread_obj;
    CL_ThreadObj *tobj;
    int i;
    CL_UNUSED(args);
    CL_UNUSED(n);

    CL_GC_PROTECT(result);
    for (i = CL_MAX_THREADS - 1; i >= 0; i--) {
        if (!cl_thread_table[i]) continue;

        if (i == 0) {
            /* Main thread */
            result = cl_cons(main_thread_obj, result);
        } else {
            tobj = (CL_ThreadObj *)cl_alloc(TYPE_THREAD,
                                             sizeof(CL_ThreadObj));
            if (!tobj) break;
            tobj->thread_id = (uint32_t)i;
            tobj->name = cl_thread_table[i]->name;
            thread_obj = CL_PTR_TO_OBJ(tobj);
            result = cl_cons(thread_obj, result);
        }
    }
    CL_GC_UNPROTECT(1);
    return result;
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
static CL_Obj bi_make_lock(CL_Obj *args, int n)
{
    CL_Obj name = (n > 0) ? args[0] : CL_NIL;
    void *mutex_handle = NULL;
    int lock_id;
    CL_Lock *lk;

    if (platform_mutex_init(&mutex_handle) != 0)
        cl_error(CL_ERR_GENERAL, "MP:MAKE-LOCK: failed to create mutex");

    lock_id = cl_lock_table_alloc(mutex_handle);
    if (lock_id < 0) {
        platform_mutex_destroy(mutex_handle);
        cl_error(CL_ERR_GENERAL, "MP:MAKE-LOCK: lock table full (max %d)",
                 CL_MAX_LOCKS);
    }

    CL_GC_PROTECT(name);
    lk = (CL_Lock *)cl_alloc(TYPE_LOCK, sizeof(CL_Lock));
    CL_GC_UNPROTECT(1);
    if (!lk) {
        cl_lock_table_free(lock_id);
        platform_mutex_destroy(mutex_handle);
        cl_error(CL_ERR_STORAGE, "MP:MAKE-LOCK: cannot allocate lock object");
    }

    lk->lock_id = (uint32_t)lock_id;
    lk->name = name;
    return CL_PTR_TO_OBJ(lk);
}

/* (mp:make-recursive-lock &optional name) -> lock
 * A recursive lock can be acquired multiple times by the same thread; it must
 * be released the same number of times before another thread can acquire it.
 * On AmigaOS this is identical to make-lock (SignalSemaphore is naturally
 * recursive); on POSIX it uses PTHREAD_MUTEX_RECURSIVE. */
static CL_Obj bi_make_recursive_lock(CL_Obj *args, int n)
{
    CL_Obj name = (n > 0) ? args[0] : CL_NIL;
    void *mutex_handle = NULL;
    int lock_id;
    CL_Lock *lk;

    if (platform_mutex_init_recursive(&mutex_handle) != 0)
        cl_error(CL_ERR_GENERAL,
                 "MP:MAKE-RECURSIVE-LOCK: failed to create recursive mutex");

    lock_id = cl_lock_table_alloc(mutex_handle);
    if (lock_id < 0) {
        platform_mutex_destroy(mutex_handle);
        cl_error(CL_ERR_GENERAL,
                 "MP:MAKE-RECURSIVE-LOCK: lock table full (max %d)",
                 CL_MAX_LOCKS);
    }

    CL_GC_PROTECT(name);
    lk = (CL_Lock *)cl_alloc(TYPE_LOCK, sizeof(CL_Lock));
    CL_GC_UNPROTECT(1);
    if (!lk) {
        cl_lock_table_free(lock_id);
        platform_mutex_destroy(mutex_handle);
        cl_error(CL_ERR_STORAGE,
                 "MP:MAKE-RECURSIVE-LOCK: cannot allocate lock object");
    }

    lk->lock_id = (uint32_t)lock_id;
    lk->name = name;
    return CL_PTR_TO_OBJ(lk);
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
        platform_mutex_lock(mutex);
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

    if (n > 2 && !CL_NULL_P(args[2])) {
        /* Timed wait: convert seconds to milliseconds */
        double secs = cl_to_double(args[2]);
        uint32_t ms;
        if (secs <= 0.0)
            ms = 0;
        else if (secs > 4294967.0)
            ms = 0xFFFFFFFFu;
        else
            ms = (uint32_t)(secs * 1000.0);
        /* Returns 1 if timed out, 0 if signaled */
        return platform_condvar_wait_timeout(cv_handle, mutex, ms) ? CL_NIL : CL_T;
    }

    platform_condvar_wait(cv_handle, mutex);
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

    /* Self-destruction: error immediately */
    if (tobj->thread_id == self->id)
        cl_error(CL_ERR_GENERAL, "Thread destroyed");

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
    s->value = fn;
    cl_export_symbol(sym, cl_package_mp);
}

void cl_builtins_thread_init(void)
{
    CL_ThreadObj *tobj;

    /* Pre-intern keywords */
    KW_NAME_THR = cl_intern_keyword("NAME", 4);

    /* Create main thread's Lisp-visible thread object */
    tobj = (CL_ThreadObj *)cl_alloc(TYPE_THREAD, sizeof(CL_ThreadObj));
    if (tobj) {
        tobj->thread_id = 0;
        tobj->name = CL_NIL;
        main_thread_obj = CL_PTR_TO_OBJ(tobj);
    }

    /* Register MP builtins */
    mp_defun("MAKE-THREAD",             bi_make_thread,             1, -1);
    mp_defun("JOIN-THREAD",             bi_join_thread,             1, 1);
    mp_defun("THREAD-ALIVE-P",          bi_thread_alive_p,         1, 1);
    mp_defun("CURRENT-THREAD",          bi_current_thread,          0, 0);
    mp_defun("ALL-THREADS",             bi_all_threads,             0, 0);
    mp_defun("THREAD-NAME",             bi_thread_name,             1, 1);
    mp_defun("THREAD-YIELD",            bi_thread_yield,            0, 0);

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
