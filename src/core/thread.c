/*
 * thread.c — Thread system initialization and registry.
 *
 * Allocates and initializes the main thread's CL_Thread struct.
 * cl_current_thread is TLS-backed via platform_tls_get/set.
 * Thread registry: linked list of all CL_Thread instances.
 * GC safepoint: threads pause here during stop-the-world GC.
 */

#define CL_THREAD_NO_MACROS  /* access struct members directly */
#include "thread.h"
#include "symbol.h"
#include "error.h"
#include "vm.h"
#include "../platform/platform.h"
#include "../platform/platform_thread.h"
#include <string.h>

static CL_Thread cl_main_thread;
CL_Thread *cl_main_thread_ptr = NULL;

/* ---- Thread registry ---- */
CL_Thread  *cl_thread_list      = NULL;
void       *cl_thread_list_lock = NULL;
uint32_t    cl_thread_count     = 0;

/* ---- GC coordination ---- */
static void *gc_mutex   = NULL;  /* protects GC state transitions */
static void *gc_condvar = NULL;  /* threads wait here during STW; initiator waits for all stopped */

/* Register a thread in the global thread list */
void cl_thread_register(CL_Thread *t)
{
    platform_mutex_lock(cl_thread_list_lock);
    t->next = cl_thread_list;
    cl_thread_list = t;
    cl_thread_count++;
    platform_mutex_unlock(cl_thread_list_lock);
}

/* Unregister a thread from the global thread list */
void cl_thread_unregister(CL_Thread *t)
{
    CL_Thread **pp;
    platform_mutex_lock(cl_thread_list_lock);
    for (pp = &cl_thread_list; *pp; pp = &(*pp)->next) {
        if (*pp == t) {
            *pp = t->next;
            t->next = NULL;
            cl_thread_count--;
            break;
        }
    }
    platform_mutex_unlock(cl_thread_list_lock);
}

/* ---- GC safepoint (slow path) ---- */

void cl_gc_safepoint(void)
{
    CL_Thread *self = (CL_Thread *)platform_tls_get();
    if (!self || !self->gc_requested)
        return;

    platform_mutex_lock(gc_mutex);
    self->gc_stopped = 1;
    /* Wake GC initiator who may be waiting for all threads to stop */
    platform_condvar_broadcast(gc_condvar);
    /* Wait until GC is complete (gc_requested cleared by initiator) */
    while (self->gc_requested)
        platform_condvar_wait(gc_condvar, gc_mutex);
    self->gc_stopped = 0;
    platform_mutex_unlock(gc_mutex);
}

/* ---- Safe regions (blocking syscalls outside the heap) ---- */

void cl_gc_enter_safe_region(void)
{
    CL_Thread *self = (CL_Thread *)platform_tls_get();
    if (!self) return;
    platform_mutex_lock(gc_mutex);
    self->in_safe_region = 1;
    /* Wake any STW initiator counting safe-region threads as stopped */
    platform_condvar_broadcast(gc_condvar);
    platform_mutex_unlock(gc_mutex);
}

void cl_gc_leave_safe_region(void)
{
    CL_Thread *self = (CL_Thread *)platform_tls_get();
    if (!self) return;
    platform_mutex_lock(gc_mutex);
    /* If a GC is currently running, park here until it completes — we
     * cannot return to the heap-touching caller while the world is
     * stopped, otherwise we'd race the mark/sweep. */
    while (self->gc_requested) {
        self->gc_stopped = 1;
        platform_condvar_broadcast(gc_condvar);
        platform_condvar_wait(gc_condvar, gc_mutex);
    }
    self->gc_stopped = 0;
    self->in_safe_region = 0;
    platform_mutex_unlock(gc_mutex);
}

/* ---- Stop-the-world GC coordination ---- */

/* Called by the GC initiator (from cl_gc) to stop all other threads.
 * The caller must NOT hold alloc_mutex when calling this.
 *
 * Reentrancy: if another thread is already inside STW we cannot blocking-lock
 * gc_mutex — that other thread has already requested *us* to stop and is
 * waiting for us to reach a safepoint, but a blocked mutex_lock never yields
 * to one.  We use trylock and either (a) win the race and become initiator,
 * (b) lose to a peer that hasn't requested us yet (yield + retry), or (c)
 * acquire gc_mutex but find we've been requested to stop in the interim
 * (drop the mutex so the requester can continue, park at a safepoint, and
 * try again afterwards). */
void cl_gc_stop_the_world(void)
{
    CL_Thread *self = (CL_Thread *)platform_tls_get();
    CL_Thread *t;

    for (;;) {
        if (platform_mutex_trylock(gc_mutex) == 0) {
            if (!self || !self->gc_requested) break;  /* initiator */
            /* We won the race for gc_mutex but in the meantime another
             * thread (that hasn't released yet through resume_the_world)
             * already marked us for stop.  Release the mutex so they can
             * see all_stopped, then park ourselves. */
            platform_mutex_unlock(gc_mutex);
        }
        cl_gc_safepoint();           /* park if we've been requested */
        platform_thread_yield();
    }

    /* Request all other threads to stop */
    platform_mutex_lock(cl_thread_list_lock);
    for (t = cl_thread_list; t; t = t->next) {
        if (t != self)
            t->gc_requested = 1;
    }
    platform_mutex_unlock(cl_thread_list_lock);

    /* Wait until all other threads have reached a safepoint or are inside
     * a safe region (blocking syscall not touching the heap). */
    for (;;) {
        int all_stopped = 1;
        platform_mutex_lock(cl_thread_list_lock);
        for (t = cl_thread_list; t; t = t->next) {
            if (t != self && t->gc_requested
                && !t->gc_stopped && !t->in_safe_region) {
                all_stopped = 0;
                break;
            }
        }
        platform_mutex_unlock(cl_thread_list_lock);

        if (all_stopped) break;
        platform_condvar_wait(gc_condvar, gc_mutex);
    }
    /* gc_mutex remains held — caller runs GC then calls resume */
}

/* Called after GC completes to wake all stopped threads */
void cl_gc_resume_the_world(void)
{
    CL_Thread *t;

    /* Clear gc_requested on all threads */
    platform_mutex_lock(cl_thread_list_lock);
    for (t = cl_thread_list; t; t = t->next) {
        t->gc_requested = 0;
        /* gc_stopped will be cleared by the thread itself in cl_gc_safepoint */
    }
    platform_mutex_unlock(cl_thread_list_lock);

    /* Wake all waiting threads */
    platform_condvar_broadcast(gc_condvar);
    platform_mutex_unlock(gc_mutex);
}

/* ---- Thread interrupt handling (slow path) ---- */

void cl_thread_handle_interrupt(CL_Thread *t)
{
    CL_Obj func;

    if (!t->interrupt_pending)
        return;

    /* destroy-thread: abort the thread by raising an error */
    if (t->destroy_requested) {
        t->destroy_requested = 0;
        t->interrupt_pending = 0;
        t->interrupt_func = CL_NIL;
        cl_error(CL_ERR_GENERAL, "Thread destroyed");
        return;  /* not reached — cl_error longjmps */
    }

    /* interrupt-thread: call the pending function */
    func = t->interrupt_func;
    t->interrupt_func = CL_NIL;
    t->interrupt_pending = 0;

    if (!CL_NULL_P(func)) {
        cl_vm_apply(func, NULL, 0);
    }
}

/* ---- TLV table operations ---- */

CL_Obj cl_tlv_get(CL_Thread *t, CL_Obj sym)
{
    uint32_t idx = (sym >> 2) & (CL_TLV_TABLE_SIZE - 1);
    uint32_t i;
    for (i = 0; i < CL_TLV_TABLE_SIZE; i++) {
        uint32_t slot = (idx + i) & (CL_TLV_TABLE_SIZE - 1);
        CL_Obj k = t->tlv_table[slot].symbol;
        if (k == CL_NIL) return CL_TLV_ABSENT;
        if (k == sym)     return t->tlv_table[slot].value;
    }
    return CL_TLV_ABSENT;
}

void cl_tlv_set(CL_Thread *t, CL_Obj sym, CL_Obj val)
{
    uint32_t idx = (sym >> 2) & (CL_TLV_TABLE_SIZE - 1);
    uint32_t first_tombstone = CL_TLV_TABLE_SIZE;
    uint32_t i;
    for (i = 0; i < CL_TLV_TABLE_SIZE; i++) {
        uint32_t slot = (idx + i) & (CL_TLV_TABLE_SIZE - 1);
        CL_Obj k = t->tlv_table[slot].symbol;
        if (k == sym) {
            t->tlv_table[slot].value = val;
            return;
        }
        if (k == CL_UNBOUND && first_tombstone == CL_TLV_TABLE_SIZE)
            first_tombstone = slot;
        if (k == CL_NIL) {
            uint32_t target = (first_tombstone < CL_TLV_TABLE_SIZE)
                              ? first_tombstone : slot;
            t->tlv_table[target].symbol = sym;
            t->tlv_table[target].value  = val;
            t->tlv_entry_count++;
            return;
        }
    }
    if (first_tombstone < CL_TLV_TABLE_SIZE) {
        t->tlv_table[first_tombstone].symbol = sym;
        t->tlv_table[first_tombstone].value  = val;
        t->tlv_entry_count++;
    }
}

void cl_tlv_remove(CL_Thread *t, CL_Obj sym)
{
    uint32_t idx = (sym >> 2) & (CL_TLV_TABLE_SIZE - 1);
    uint32_t i;
    for (i = 0; i < CL_TLV_TABLE_SIZE; i++) {
        uint32_t slot = (idx + i) & (CL_TLV_TABLE_SIZE - 1);
        CL_Obj k = t->tlv_table[slot].symbol;
        if (k == CL_NIL) return;
        if (k == sym) {
            t->tlv_table[slot].symbol = CL_UNBOUND;
            t->tlv_table[slot].value  = CL_NIL;
            if (t->tlv_entry_count > 0)
                t->tlv_entry_count--;
            return;
        }
    }
}

/* High-level TLV-aware accessors.
 * Fast path: when tlv_entry_count == 0, no dynamic bindings are active
 * on this thread, so skip the TLV hash probe entirely.  This turns a
 * 30-50 cycle probe into a single compare + direct field read. */

CL_Obj cl_symbol_value(CL_Obj sym)
{
    CL_Thread *t = (cl_thread_count <= 1)
                   ? cl_main_thread_ptr
                   : (CL_Thread *)platform_tls_get();
    if (t->tlv_entry_count > 0) {
        CL_Obj v = cl_tlv_get(t, sym);
        if (v != CL_TLV_ABSENT) return v;
    }
    return ((CL_Symbol *)CL_OBJ_TO_PTR(sym))->value;
}

void cl_set_symbol_value(CL_Obj sym, CL_Obj val)
{
    CL_Thread *t = (cl_thread_count <= 1)
                   ? cl_main_thread_ptr
                   : (CL_Thread *)platform_tls_get();
    if (t->tlv_entry_count > 0) {
        CL_Obj v = cl_tlv_get(t, sym);
        if (v != CL_TLV_ABSENT) {
            cl_tlv_set(t, sym, val);
            return;
        }
    }
    ((CL_Symbol *)CL_OBJ_TO_PTR(sym))->value = val;
}

int cl_symbol_boundp(CL_Obj sym)
{
    CL_Thread *t = (cl_thread_count <= 1)
                   ? cl_main_thread_ptr
                   : (CL_Thread *)platform_tls_get();
    if (t->tlv_entry_count > 0) {
        CL_Obj v = cl_tlv_get(t, sym);
        if (v != CL_TLV_ABSENT) return v != CL_UNBOUND;
    }
    return ((CL_Symbol *)CL_OBJ_TO_PTR(sym))->value != CL_UNBOUND;
}

/* TLV snapshot (for thread inheritance) */
void cl_tlv_snapshot(CL_Thread *dst, CL_Thread *src)
{
    uint32_t i, count = 0;
    memcpy(dst->tlv_table, src->tlv_table, sizeof(src->tlv_table));
    /* Recompute entry count from the copied table */
    for (i = 0; i < CL_TLV_TABLE_SIZE; i++) {
        if (dst->tlv_table[i].symbol != CL_NIL &&
            dst->tlv_table[i].symbol != CL_UNBOUND)
            count++;
    }
    dst->tlv_entry_count = count;
}

/* ---- Side tables and worker thread allocation ---- */

CL_Thread *cl_thread_table[CL_MAX_THREADS];
void *cl_lock_table[CL_MAX_LOCKS];
void *cl_condvar_table[CL_MAX_CONDVARS];

int cl_thread_table_alloc(CL_Thread *t)
{
    int i, result = -1;
    platform_mutex_lock(cl_thread_list_lock);
    for (i = 0; i < CL_MAX_THREADS; i++) {
        if (!cl_thread_table[i]) {
            cl_thread_table[i] = t;
            result = i;
            break;
        }
    }
    platform_mutex_unlock(cl_thread_list_lock);
    return result;
}

void cl_thread_table_free(int id)
{
    platform_mutex_lock(cl_thread_list_lock);
    if (id >= 0 && id < CL_MAX_THREADS)
        cl_thread_table[id] = NULL;
    platform_mutex_unlock(cl_thread_list_lock);
}

int cl_lock_table_alloc(void *handle)
{
    int i, result = -1;
    platform_mutex_lock(cl_thread_list_lock);
    for (i = 0; i < CL_MAX_LOCKS; i++) {
        if (!cl_lock_table[i]) {
            cl_lock_table[i] = handle;
            result = i;
            break;
        }
    }
    platform_mutex_unlock(cl_thread_list_lock);
    return result;
}

void cl_lock_table_free(int id)
{
    platform_mutex_lock(cl_thread_list_lock);
    if (id >= 0 && id < CL_MAX_LOCKS)
        cl_lock_table[id] = NULL;
    platform_mutex_unlock(cl_thread_list_lock);
}

int cl_condvar_table_alloc(void *handle)
{
    int i, result = -1;
    platform_mutex_lock(cl_thread_list_lock);
    for (i = 0; i < CL_MAX_CONDVARS; i++) {
        if (!cl_condvar_table[i]) {
            cl_condvar_table[i] = handle;
            result = i;
            break;
        }
    }
    platform_mutex_unlock(cl_thread_list_lock);
    return result;
}

void cl_condvar_table_free(int id)
{
    platform_mutex_lock(cl_thread_list_lock);
    if (id >= 0 && id < CL_MAX_CONDVARS)
        cl_condvar_table[id] = NULL;
    platform_mutex_unlock(cl_thread_list_lock);
}

CL_Thread *cl_thread_alloc_worker(void)
{
    CL_Thread *t = (CL_Thread *)platform_alloc(sizeof(CL_Thread));
    if (!t) return NULL;
    memset(t, 0, sizeof(CL_Thread));

    /* Allocate VM stack and frames (compact worker sizes) */
    t->vm.stack = (CL_Obj *)platform_alloc(
        CL_WORKER_VM_STACK_SIZE * sizeof(CL_Obj));
    if (!t->vm.stack) { platform_free(t); return NULL; }
    t->vm.stack_size = CL_WORKER_VM_STACK_SIZE;
    t->vm.frames = (CL_Frame *)platform_alloc(
        CL_WORKER_VM_FRAME_SIZE * sizeof(CL_Frame));
    if (!t->vm.frames) {
        platform_free(t->vm.stack);
        platform_free(t);
        return NULL;
    }
    t->vm.frame_size = CL_WORKER_VM_FRAME_SIZE;

    /* Allocate NLX stack */
    t->nlx_stack = (CL_NLXFrame *)platform_alloc(
        CL_WORKER_NLX_FRAMES * sizeof(CL_NLXFrame));
    if (!t->nlx_stack) {
        platform_free(t->vm.frames);
        platform_free(t->vm.stack);
        platform_free(t);
        return NULL;
    }
    t->nlx_max = CL_WORKER_NLX_FRAMES;

    /* Default: single-value mode */
    t->mv_count = 1;
    t->status = 0; /* created */

    return t;
}

void cl_thread_free_worker(CL_Thread *t)
{
    if (!t) return;
    if (t->nlx_stack)  platform_free(t->nlx_stack);
    if (t->vm.frames)  platform_free(t->vm.frames);
    if (t->vm.stack)   platform_free(t->vm.stack);
    platform_free(t);
}

/* ---- Init / Shutdown ---- */

void cl_thread_init(void)
{
    memset(&cl_main_thread, 0, sizeof(CL_Thread));

    /* Initialize TLS and set main thread as current */
    platform_tls_init();
    platform_tls_set(&cl_main_thread);
    cl_main_thread_ptr = &cl_main_thread;

    /* Initialize thread registry */
    platform_mutex_init(&cl_thread_list_lock);
    cl_thread_list = NULL;
    cl_thread_count = 0;

    /* Initialize GC coordination */
    platform_mutex_init(&gc_mutex);
    platform_condvar_init(&gc_condvar);

    /* Allocate NLX stack */
    cl_main_thread.nlx_stack = (CL_NLXFrame *)platform_alloc(
        CL_MAX_NLX_FRAMES * sizeof(CL_NLXFrame));
    cl_main_thread.nlx_max = CL_MAX_NLX_FRAMES;
    cl_main_thread.nlx_top = 0;

    /* Default: single-value mode */
    cl_main_thread.mv_count = 1;

    /* Mark as running */
    cl_main_thread.id = 0;
    cl_main_thread.status = 1; /* running */

    /* Initialize side tables */
    memset(cl_thread_table, 0, sizeof(cl_thread_table));
    memset(cl_lock_table, 0, sizeof(cl_lock_table));
    memset(cl_condvar_table, 0, sizeof(cl_condvar_table));

    /* Register main thread in both registry and side table */
    cl_thread_register(&cl_main_thread);
    cl_thread_table[0] = &cl_main_thread;
}

void cl_thread_shutdown(void)
{
    /* Unregister main thread */
    cl_thread_unregister(&cl_main_thread);

    if (cl_main_thread.nlx_stack) {
        platform_free(cl_main_thread.nlx_stack);
        cl_main_thread.nlx_stack = NULL;
    }
    /* Keep cl_main_thread_ptr valid — the crash handler accesses
     * thread state (cl_vm.sp, etc.) via the CT macro.  Setting it
     * to NULL would cause a SIGSEGV in the handler itself. */

    /* Destroy GC coordination */
    if (gc_condvar) {
        platform_condvar_destroy(gc_condvar);
        gc_condvar = NULL;
    }
    if (gc_mutex) {
        platform_mutex_destroy(gc_mutex);
        gc_mutex = NULL;
    }

    /* Destroy thread registry lock */
    if (cl_thread_list_lock) {
        platform_mutex_destroy(cl_thread_list_lock);
        cl_thread_list_lock = NULL;
    }
}
