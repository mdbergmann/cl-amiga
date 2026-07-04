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
#ifdef DEBUG_THREAD_RACE_HOOKS
#include <stdio.h>
#include <stdlib.h>
#endif

static CL_Thread cl_main_thread;
CL_Thread *cl_main_thread_ptr = NULL;

/* ---- Thread registry ---- */
CL_Thread  *cl_thread_list      = NULL;
void       *cl_thread_list_lock = NULL;
uint32_t    cl_thread_count     = 0;

/* ---- GC coordination ---- */
static void *gc_mutex   = NULL;  /* protects GC state transitions */
static void *gc_condvar = NULL;  /* threads wait here during STW; initiator waits for all stopped */

#ifdef DEBUG_THREAD_RACE_HOOKS
/* Set immediately before the STW initiator (cl_gc_stop_the_world) parks in
 * platform_condvar_wait, i.e. the instant it has found a straggler and is
 * about to sleep for a wakeup.  cl_debug_thread_exit_race_selftest()'s
 * worker spins on this so its unregister always lands inside the exact
 * window cl_thread_unregister's gc_condvar broadcast fixes, instead of
 * relying on scheduler timing to hit it (see selftest below). */
static volatile int dbg_race_stw_parked = 0;
#endif

/* Register a thread in the global thread list.
 * `live` = 1 marks it as participating in stop-the-world immediately; use this
 * when the calling context IS the thread's own running OS thread (it can reach
 * safepoints).  `live` = 0 registers a NEWBORN: linked so GC marks its roots,
 * but skipped by the STW wait loop until its OS thread reaches
 * cl_gc_thread_online — see gc_live in thread.h. */
static void cl_thread_register_ex(CL_Thread *t, uint8_t live)
{
    platform_mutex_lock(cl_thread_list_lock);
    t->gc_live = live;
    t->next = cl_thread_list;
    cl_thread_list = t;
    cl_thread_count++;
    platform_mutex_unlock(cl_thread_list_lock);
}

/* Register an already-running thread (self-registration): live immediately. */
void cl_thread_register(CL_Thread *t)
{
    cl_thread_register_ex(t, 1);
}

/* Register a thread whose OS thread does not exist yet (the make-thread child):
 * a newborn that becomes live only when it reaches cl_gc_thread_online. */
void cl_thread_register_newborn(CL_Thread *t)
{
    cl_thread_register_ex(t, 0);
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

    /* Wake a possible STW initiator.  cl_gc_stop_the_world may be parked
     * in platform_condvar_wait(gc_condvar) waiting for exactly this
     * thread to reach a safepoint; a thread past its last safepoint that
     * exits instead never stops, so without this broadcast the initiator
     * re-scans only on other threads' safepoint broadcasts — with none
     * left, GC (and the whole process) hangs forever.  Must be done
     * AFTER releasing the list lock: the initiator acquires gc_mutex
     * then the list lock, so taking gc_mutex while holding the list lock
     * here would be an ABBA deadlock.  Holding gc_mutex around the
     * broadcast closes the lost-wakeup window (the initiator either
     * holds gc_mutex scanning — and will see the shrunken list — or is
     * inside condvar_wait and receives the broadcast). */
    if (gc_mutex) {
        platform_mutex_lock(gc_mutex);
        platform_condvar_broadcast(gc_condvar);
        platform_mutex_unlock(gc_mutex);
    }
}

/* ---- GC safepoint (slow path) ---- */

void cl_gc_safepoint(void)
{
    CL_Thread *self = (CL_Thread *)platform_tls_get();
    if (!self || !self->gc_requested)
        return;
    /* Shutdown teardown window: cl_thread_shutdown nulls gc_mutex; a
     * straggling worker must not lock a destroyed mutex
     * (ObtainSemaphore(NULL) on Amiga) — same guard as enter/leave_safe_region. */
    if (!gc_mutex)
        return;

    /* Record where we froze so a peer's compaction can conservatively scan
     * THIS thread's JIT native stack ([jit_park_sp .. jit_stack_top)) — the
     * peer cannot use its own SP as the lower bound.  Captured before we mark
     * ourselves stopped so it is valid the instant the initiator observes us. */
    self->jit_park_sp = CL_CAPTURE_SP();

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
    /* No GC coordination (e.g. torn down at shutdown) -> nothing to coordinate
     * with, so a "safe region" is a no-op.  Guards against locking a destroyed
     * gc_mutex (ObtainSemaphore(NULL) on Amiga) during exit teardown. */
    if (!gc_mutex) return;
    /* Freeze point for JIT-stack scanning: the blocking syscall we are about to
     * enter runs on stack BELOW here, while any JIT-spilled operands live ABOVE
     * — so [jit_park_sp .. jit_stack_top) covers exactly this thread's JIT
     * frames for a peer's compaction (see jit_park_sp in thread.h). */
    self->jit_park_sp = CL_CAPTURE_SP();
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
    if (!gc_mutex) return;  /* see cl_gc_enter_safe_region */
    /* Re-capture the freeze point: the syscall has returned, so we are about to
     * park at this frame (below our still-intact JIT frames) until GC finishes. */
    self->jit_park_sp = CL_CAPTURE_SP();
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

/* Acquire an internal C mutex GC-cooperatively.  See thread.h for the deadlock
 * this avoids.  Fast path: an uncontended trylock — no gc_mutex traffic, so the
 * hot I/O paths pay only one atomic.  Slow path (contended): bracket the
 * blocking wait in a safe region so a peer's stop-the-world GC counts this
 * thread as stopped and proceeds; cl_gc_leave_safe_region() on the far side
 * parks us if a GC is now running.  We hold the lock while parked, which is safe
 * because every other contender waits the same GC-cooperative way — none stalls
 * the world — and GC touches no data these locks protect.
 *
 * Use this ONLY for a lock that can be held across a blocking syscall / GC safe
 * region (the per-stream I/O locks).  Do NOT use it for a lock whose critical
 * section a GC touches (e.g. the stream side-table mutex): the safe region turns
 * the acquisition into a peer-compaction safepoint, and such short, non-blocking
 * critical sections don't need it anyway — a waiter is only briefly delayed. */
void cl_gc_safe_mutex_lock(void *mutex)
{
    if (!mutex) return;
    if (!CL_MT()) { platform_mutex_lock(mutex); return; }
    if (platform_mutex_trylock(mutex) == 0)
        return;                       /* uncontended — no GC coordination */
    cl_gc_enter_safe_region();
    platform_mutex_lock(mutex);
    cl_gc_leave_safe_region();
}

/* ---- Newborn → live transition (thread startup barrier) ---- */

/* Called once at the top of a worker's entry function, BEFORE it touches the
 * heap.  Atomically (under gc_mutex) transitions the thread from newborn
 * (gc_live == 0) to live (gc_live == 1) so it starts participating in
 * stop-the-world GC — but only after making sure it does not come online in
 * the middle of an STW that has already stopped the world.
 *
 * Race analysis (the whole point of this function):
 *   The STW initiator sets gc_requested on all other threads and then, in its
 *   wait loop, waits ONLY for `gc_live` threads (a newborn has no OS thread to
 *   reach a safepoint, so waiting for it would hang the world).  Both the
 *   request/wait and this barrier serialize on gc_mutex, so relative to the
 *   initiator holding gc_mutex this thread is either fully before or fully
 *   after:
 *     - Before the initiator's request: we see gc_requested == 0, become live,
 *       and unlock.  The initiator then requests us (we're live now) and waits
 *       for us; we reach a safepoint at our first allocation and park.  Safe.
 *     - After (initiator already requested us and is in its wait loop, which
 *       releases gc_mutex only inside condvar_wait): we see gc_requested == 1
 *       and park here WITHOUT becoming live or touching the heap.  The wait
 *       loop skips us (gc_live still 0), so the world stops without us; resume
 *       clears gc_requested and wakes us, and we then become live.  Safe.
 */
void cl_gc_thread_online(CL_Thread *self)
{
    if (!self) return;
    if (!gc_mutex) { self->gc_live = 1; return; }
    platform_mutex_lock(gc_mutex);
    /* Park if a stop-the-world is in progress and requested us: we must not
     * enter the heap-touching caller while the world is stopped.  We stay a
     * newborn (gc_live == 0) so the initiator's wait loop does not wait on us. */
    while (self->gc_requested) {
        self->gc_stopped = 1;
        platform_condvar_broadcast(gc_condvar);
        platform_condvar_wait(gc_condvar, gc_mutex);
    }
    self->gc_stopped = 0;
    self->gc_live = 1;   /* now participate in stop-the-world */
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
    if (self) self->wait_kind = 4;  /* GC-STW-WAIT (diagnostic) */
    for (;;) {
        int all_stopped = 1;
        platform_mutex_lock(cl_thread_list_lock);
        for (t = cl_thread_list; t; t = t->next) {
            /* Skip newborns (gc_live == 0): they are registered so GC marks
             * their roots, but their OS thread has not reached the online
             * barrier and can never hit a safepoint — waiting for one would
             * hang the world.  cl_gc_thread_online parks any newborn that
             * comes online while this STW is in progress, so skipping them
             * here cannot let a thread touch the stopped heap. */
            /* A thread whose registration landed AFTER the request phase
             * above has gc_requested==0 and would be treated as stopped
             * while its OS thread runs.  Today that is unreachable only
             * because bi_make_thread's wrapper alloc is a mandatory
             * safepoint between registering and creating the OS thread —
             * an incidental invariant.  Make it structural: (re)request
             * any live, unrequested peer during each rescan (idempotent,
             * under the list lock); it then parks at its next safepoint
             * before all_stopped can become true. */
            if (t != self && t->gc_live && !t->gc_requested)
                t->gc_requested = 1;
            if (t != self && t->gc_live && t->gc_requested
                && !t->gc_stopped && !t->in_safe_region) {
                all_stopped = 0;
                /* Record which thread is holding up the world so a watchdog
                 * dump can name the straggler that never reached a safepoint
                 * (e.g. a thread blocked in a syscall outside a safe region). */
                if (self) self->wait_lock_id = (int)t->id;
                break;
            }
        }
        platform_mutex_unlock(cl_thread_list_lock);

        if (all_stopped) break;
#ifdef DEBUG_THREAD_RACE_HOOKS
        dbg_race_stw_parked = 1;
#endif
        platform_condvar_wait(gc_condvar, gc_mutex);
    }
    if (self) self->wait_kind = 0;
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

#ifdef DEBUG_THREAD_RACE_HOOKS
/* ---- Deterministic repro/regression check for the STW-hang-on-exit fix ----
 *
 * The bug cl_thread_unregister's gc_condvar broadcast fixes needs a worker
 * to exit (unregister) at the exact instant the STW initiator has found it
 * as the sole straggler and is parking in platform_condvar_wait — too
 * narrow a window to land reliably via ordinary thread-churn timing on a
 * multi-core host.  This self-test forces the window directly: the worker
 * never reaches a safepoint (simulating one that already crossed its last
 * one) and only unregisters once dbg_race_stw_parked confirms the
 * initiator is about to sleep.  If cl_thread_unregister's wakeup were
 * missing or broken, cl_gc_stop_the_world below never returns and the
 * process hangs — the shell test wraps this binary in `timeout` to turn
 * that hang into a deterministic FAIL. */

static void *dbg_race_worker_fn(void *arg)
{
    CL_Thread *t = (CL_Thread *)arg;
    cl_thread_register(t);            /* live=1: the STW below will target it */
    while (!dbg_race_stw_parked)      /* wait for the initiator to park */
        platform_thread_yield();
    cl_thread_unregister(t);          /* exits WITHOUT ever hitting a safepoint */
    return NULL;
}

/* Runs the race deterministically and reports the outcome on stdout/exit
 * code; invoked automatically at process start when this build is compiled
 * with -DDEBUG_THREAD_RACE_HOOKS (see tests/test_mt_thread_exit_gc.sh and
 * the `test-mt-thread-exit-race` Makefile target). */
static void dbg_race_selftest_and_exit(void)
{
    CL_Thread *worker;
    void *handle;
    int spins;

    cl_thread_init();
    worker = cl_thread_alloc_worker();
    if (!worker) {
        fprintf(stderr, "RACE-SELFTEST: worker alloc failed\n");
        exit(2);
    }

    if (platform_thread_create(&handle, dbg_race_worker_fn, worker,
                               CL_WORKER_C_STACK_SIZE) != 0) {
        fprintf(stderr, "RACE-SELFTEST: thread_create failed\n");
        cl_thread_free_worker(worker);
        exit(2);
    }

    /* Wait for the worker to register (bounded — this is not the race
     * window under test, just startup). */
    for (spins = 0; cl_thread_count < 2 && spins < 1000000; spins++)
        platform_thread_yield();
    if (cl_thread_count < 2) {
        fprintf(stderr, "RACE-SELFTEST: worker never registered\n");
        exit(2);
    }

    cl_gc_stop_the_world();   /* hangs here if the fix regresses */
    cl_gc_resume_the_world();

    printf("RACE-SELFTEST-OK\n");
    exit(0);
}

__attribute__((constructor))
static void dbg_race_selftest_ctor(void)
{
    dbg_race_selftest_and_exit();  /* never returns */
}
#endif /* DEBUG_THREAD_RACE_HOOKS */

/* ---- Thread interrupt handling (slow path) ---- */

void cl_thread_handle_interrupt(CL_Thread *t)
{
    CL_Obj func;

    if (!t->interrupt_pending)
        return;

    /* Defer while holding an internal rwlock reader: an interrupt closure
     * that signals — or a destroy — unwinds via longjmp, and nothing
     * restores rdlock_tables_held/rdlock_package_held on that path.  The
     * leaked reader count then blocks the next writer FOREVER (process-
     * wide hang).  Leaving interrupt_pending set retries at the next
     * safepoint outside the locked section. */
    if (t->rdlock_tables_held > 0 || t->rdlock_package_held > 0)
        return;

    /* Pair with the publisher's barrier (bi_interrupt_thread /
     * bi_destroy_thread): having observed interrupt_pending == 1, order
     * the payload reads (destroy_requested / interrupt_func) after it —
     * on ARM64 the loads can otherwise be satisfied in the reverse
     * order and consume the flag with a stale NIL payload. */
    platform_memory_barrier();

    /* Consume under cl_thread_list_lock — the same lock the publishers
     * hold.  The old unlocked read-then-clear raced a concurrent second
     * publish: this thread's NIL/0 stores clobbered the fresh payload
     * (interrupt silently dropped), or cleared interrupt_pending while a
     * concurrent destroy_requested stayed set — the destroy was then
     * never seen again (mp:destroy-thread of a busy worker never took
     * effect). */
    if (cl_thread_list_lock) platform_mutex_lock(cl_thread_list_lock);
    if (!t->interrupt_pending) {
        if (cl_thread_list_lock) platform_mutex_unlock(cl_thread_list_lock);
        return;
    }

    /* destroy-thread: abort the thread by raising an error */
    if (t->destroy_requested) {
        t->destroy_requested = 0;
        t->interrupt_pending = 0;
        t->interrupt_func = CL_NIL;
        if (cl_thread_list_lock) platform_mutex_unlock(cl_thread_list_lock);
        /* Quiet abort: runs handler-case / unwind-protect cleanups but does
         * not enter the debugger, so a destroyed worker unwinds silently
         * instead of dropping into SLDB / fgets(stdin). */
        cl_abort_current_thread("Thread destroyed");
        return;  /* not reached — longjmps */
    }

    /* interrupt-thread: call the pending function */
    func = t->interrupt_func;
    t->interrupt_func = CL_NIL;
    t->interrupt_pending = 0;
    if (cl_thread_list_lock) platform_mutex_unlock(cl_thread_list_lock);

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

/* Rebuild the TLV table after a compacting GC has relocated symbols.
 * Entries are keyed by symbol arena-offset (sym >> 2); when the GC moves a
 * symbol it updates the stored offset in place but the entry stays in its old
 * probe slot, so cl_tlv_get would recompute a different slot and miss — the
 * dynamic binding silently reverts to the symbol's global value. Re-insert
 * every live entry by its new hash. Tombstones are dropped: a clean rebuild
 * reconstructs all probe chains and needs no deleted-markers. */
void cl_tlv_rehash(CL_Thread *t)
{
    CL_TLVEntry live[CL_TLV_TABLE_SIZE];
    uint32_t n = 0, i;

    if (!t || t->tlv_entry_count == 0)
        return;

    /* Snapshot live entries (skip empties and tombstones) */
    for (i = 0; i < CL_TLV_TABLE_SIZE; i++) {
        CL_Obj sym = t->tlv_table[i].symbol;
        if (sym != CL_NIL && sym != CL_UNBOUND) {
            live[n].symbol = sym;
            live[n].value  = t->tlv_table[i].value;
            n++;
        }
    }

    /* Clear the table, then re-insert by the new (post-move) hashes */
    for (i = 0; i < CL_TLV_TABLE_SIZE; i++) {
        t->tlv_table[i].symbol = CL_NIL;
        t->tlv_table[i].value  = CL_NIL;
    }
    t->tlv_entry_count = 0;
    for (i = 0; i < n; i++)
        cl_tlv_set(t, live[i].symbol, live[i].value);
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
    /* CL_NIL is the constant tag (0) — its value/function/plist live on
     * the heap-allocated SYM_NIL storage shadow. */
    if (CL_NULL_P(sym)) sym = SYM_NIL;
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
    if (CL_NULL_P(sym)) sym = SYM_NIL;
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
    if (CL_NULL_P(sym)) sym = SYM_NIL;
    if (t->tlv_entry_count > 0) {
        CL_Obj v = cl_tlv_get(t, sym);
        if (v != CL_TLV_ABSENT) return v != CL_UNBOUND;
    }
    return ((CL_Symbol *)CL_OBJ_TO_PTR(sym))->value != CL_UNBOUND;
}

/* ---- Side tables and worker thread allocation ---- */

CL_Thread *cl_thread_table[CL_MAX_THREADS];
uint32_t cl_thread_table_gen[CL_MAX_THREADS];
void *cl_lock_table[CL_MAX_LOCKS];
void *cl_condvar_table[CL_MAX_CONDVARS];

int cl_thread_table_alloc(CL_Thread *t)
{
    int i, result = -1;
    platform_mutex_lock(cl_thread_list_lock);
    for (i = 0; i < CL_MAX_THREADS; i++) {
        if (!cl_thread_table[i]) {
            cl_thread_table[i] = t;
            /* New occupancy: invalidate every wrapper still pointing at
             * this slot from a previous occupant (see cl_thread_table_gen
             * in thread.h). */
            cl_thread_table_gen[i]++;
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

    /* Allocate VM stack and frames.  On host the CL_WORKER_ sizes equal the
     * main thread's, so a worker can run the same call/NLX depth main can (a
     * smaller budget silently kills the worker — see thread.h).  On AmigaOS they
     * stay at the historical compact sizes; see the CL_WORKER_* rationale. */
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

    /* Allocate saved pending-throw stack */
    t->saved_pending_stack = (CL_SavedPending *)platform_alloc(
        CL_WORKER_SAVED_PENDING * sizeof(CL_SavedPending));
    if (!t->saved_pending_stack) {
        platform_free(t->nlx_stack);
        platform_free(t->vm.frames);
        platform_free(t->vm.stack);
        platform_free(t);
        return NULL;
    }
    t->saved_pending_max = CL_WORKER_SAVED_PENDING;

    /* Default: single-value mode */
    t->mv_count = 1;
    t->status = 0; /* created */

    return t;
}

void cl_thread_free_worker(CL_Thread *t)
{
    if (!t) return;
    if (t->saved_pending_stack) platform_free(t->saved_pending_stack);
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

    /* Allocate saved pending-throw stack */
    cl_main_thread.saved_pending_stack = (CL_SavedPending *)platform_alloc(
        CL_MAX_SAVED_PENDING * sizeof(CL_SavedPending));
    cl_main_thread.saved_pending_max = CL_MAX_SAVED_PENDING;
    cl_main_thread.saved_pending_top = 0;

    /* Default: single-value mode */
    cl_main_thread.mv_count = 1;

    /* Mark as running */
    cl_main_thread.id = 0;
    cl_main_thread.status = 1; /* running */
    cl_main_thread.gc_live = 1; /* main thread always participates in STW */

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

    if (cl_main_thread.saved_pending_stack) {
        platform_free(cl_main_thread.saved_pending_stack);
        cl_main_thread.saved_pending_stack = NULL;
    }
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
