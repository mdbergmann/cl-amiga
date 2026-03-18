/*
 * thread.c — Thread system initialization and registry (Phase 2).
 *
 * Allocates and initializes the main thread's CL_Thread struct.
 * cl_current_thread is TLS-backed via platform_tls_get/set.
 * Thread registry: linked list of all CL_Thread instances.
 * GC safepoint: threads pause here during stop-the-world GC.
 */

#define CL_THREAD_NO_MACROS  /* access struct members directly */
#include "thread.h"
#include "symbol.h"
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

CL_Thread *cl_get_current_thread(void)
{
    return (CL_Thread *)platform_tls_get();
}

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

/* ---- Stop-the-world GC coordination ---- */

/* Called by the GC initiator (from cl_gc) to stop all other threads.
 * The caller must NOT hold alloc_mutex when calling this. */
void cl_gc_stop_the_world(void)
{
    CL_Thread *self = (CL_Thread *)platform_tls_get();
    CL_Thread *t;

    platform_mutex_lock(gc_mutex);

    /* Request all other threads to stop */
    platform_mutex_lock(cl_thread_list_lock);
    for (t = cl_thread_list; t; t = t->next) {
        if (t != self)
            t->gc_requested = 1;
    }
    platform_mutex_unlock(cl_thread_list_lock);

    /* Wait until all other threads have reached a safepoint */
    for (;;) {
        int all_stopped = 1;
        platform_mutex_lock(cl_thread_list_lock);
        for (t = cl_thread_list; t; t = t->next) {
            if (t != self && t->gc_requested && !t->gc_stopped) {
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

/* ---- TLV table operations (Phase 3) ---- */

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
            return;
        }
    }
    if (first_tombstone < CL_TLV_TABLE_SIZE) {
        t->tlv_table[first_tombstone].symbol = sym;
        t->tlv_table[first_tombstone].value  = val;
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
            return;
        }
    }
}

/* High-level TLV-aware accessors */

CL_Obj cl_symbol_value(CL_Obj sym)
{
    CL_Thread *t = (CL_Thread *)platform_tls_get();
    CL_Obj v = cl_tlv_get(t, sym);
    if (v != CL_TLV_ABSENT) return v;
    return ((CL_Symbol *)CL_OBJ_TO_PTR(sym))->value;
}

void cl_set_symbol_value(CL_Obj sym, CL_Obj val)
{
    CL_Thread *t = (CL_Thread *)platform_tls_get();
    CL_Obj v = cl_tlv_get(t, sym);
    if (v != CL_TLV_ABSENT)
        cl_tlv_set(t, sym, val);
    else
        ((CL_Symbol *)CL_OBJ_TO_PTR(sym))->value = val;
}

int cl_symbol_boundp(CL_Obj sym)
{
    CL_Thread *t = (CL_Thread *)platform_tls_get();
    CL_Obj v = cl_tlv_get(t, sym);
    if (v != CL_TLV_ABSENT) return v != CL_UNBOUND;
    return ((CL_Symbol *)CL_OBJ_TO_PTR(sym))->value != CL_UNBOUND;
}

/* TLV snapshot (for Phase 4 thread inheritance) */
void cl_tlv_snapshot(CL_Thread *dst, CL_Thread *src)
{
    memcpy(dst->tlv_table, src->tlv_table, sizeof(src->tlv_table));
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

    /* Register main thread */
    cl_thread_register(&cl_main_thread);
}

void cl_thread_shutdown(void)
{
    /* Unregister main thread */
    cl_thread_unregister(&cl_main_thread);

    if (cl_main_thread.nlx_stack) {
        platform_free(cl_main_thread.nlx_stack);
        cl_main_thread.nlx_stack = NULL;
    }
    cl_main_thread_ptr = NULL;

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
