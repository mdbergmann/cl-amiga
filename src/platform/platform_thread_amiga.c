/*
 * platform_thread_amiga.c — AmigaOS 3+ threading primitives for CL-Amiga.
 *
 * Thread:   CreateNewProc() (dos.library)
 * Mutex:    struct SignalSemaphore (exec.library)
 * CondVar:  Signal/Wait + SignalSemaphore + waiter list
 * TLS:      FindTask(NULL)->tc_UserData
 * Atomics:  Forbid()/Permit() pairs (safe on 68020, short critical sections)
 */

#include "platform_thread.h"
#include "platform.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/semaphores.h>
#include <exec/tasks.h>
#include <dos/dostags.h>
#include <string.h>

/* ================================================================
 * Thread
 *
 * We use CreateNewProc() which creates a DOS process (superset of
 * Exec Task).  The wrapper struct holds join synchronization state.
 * ================================================================ */

/* Join/exit protocol.  The old protocol had four corruption bugs
 * (GC/threading audit tier 1):
 *
 *  - amiga_thread_entry Signal()ed the parent through `at` AFTER
 *    publishing finished=1, so a joiner that observed the flag via the
 *    semaphore poll freed `at` (and FreeSignal'd the bit) while the
 *    child was still about to read at->parent / at->join_sig — a
 *    use-after-free, plus a stray Signal into whatever task later
 *    reallocated that bit.
 *  - join_sig was allocated in the CREATOR's task at create time.  A
 *    join from any other task Wait()ed on a bit that was never going
 *    to be signalled (hang), and its FreeSignal released a bit number
 *    it never owned in its own task, corrupting that task's signal
 *    allocation while leaking the creator's.
 *  - platform_thread_detach leaked the AmigaThread struct AND the
 *    creator's signal bit.  A task has ~16 free user signal bits, so a
 *    few detached threads exhausted AllocSignal — after which every
 *    platform_condvar_wait hit its sig<0 fallback (see condvar notes).
 *
 * New protocol: no signal bit at create time.  The JOINER allocates
 * its own bit and registers (task, bit) in `at` under Forbid(); the
 * child's exit path extracts them into locals under Forbid() — its
 * very last access to `at` — and Signal()s using only the locals.
 * On the single-core Amiga, Forbid() blocks preemption, so the joiner
 * cannot observe finished=1 (and free `at`) until the child Permit()s.
 * Detach hands `at` to whichever side finishes last. */

typedef struct {
    volatile int            finished;
    volatile int            detached;
    void                   *result;
    void                  *(*func)(void *);
    void                   *arg;
    struct Task            *joiner;    /* registered by platform_thread_join */
    BYTE                    join_sig;  /* signal bit in JOINER's task */
    struct Process         *proc;
} AmigaThread;

/* Entry point for new process — runs user function, signals joiner */
static void amiga_thread_entry(void)
{
    struct Task *me = FindTask(NULL);
    AmigaThread *at;
    struct Task *joiner_local;
    BYTE sig_local;
    int free_self;
    LONG spins = 0;

    /* The parent stores tc_UserData under Forbid() — but CreateNewProc
     * can Wait() internally (DOS packet round-trip), and Wait() BREAKS a
     * Forbid(), so this child may start running before the parent's
     * store.  dos.library zeroes the new process's Task fields, so poll
     * until the parent's non-NULL store becomes visible.  Bounded (~60s
     * of Delay(1) ticks) so a genuinely broken state exits instead of
     * spinning a zombie process forever. */
    while (!(at = (AmigaThread *)me->tc_UserData)) {
        if (++spins > 3000)
            return;
        Delay(1);
    }

    at->result = at->func(at->arg);

    /* Publish completion and take our last look at `at` atomically —
     * after Permit() we touch only locals (the joiner may free `at`
     * the moment it can run and observe finished). */
    Forbid();
    at->finished = 1;
    joiner_local = at->joiner;
    sig_local    = at->join_sig;
    free_self    = at->detached;
    Permit();

    if (free_self) {
        /* Detached: nobody joins; we own the struct now. */
        FreeVec(at);
    } else if (joiner_local && sig_local >= 0) {
        Signal(joiner_local, 1UL << sig_local);
    }
}

int platform_thread_create(void **handle, void *(*func)(void *), void *arg,
                           uint32_t stack_size)
{
    AmigaThread *at;
    struct Process *proc;

    at = (AmigaThread *)AllocVec(sizeof(AmigaThread), MEMF_CLEAR);
    if (!at) return -1;

    at->finished = 0;
    at->detached = 0;
    at->result = NULL;
    at->func = func;
    at->arg = arg;
    at->joiner = NULL;
    at->join_sig = -1;

    if (stack_size == 0) stack_size = 65536;

    /* Forbid() prevents task switching so the child process won't
     * start running before we set tc_UserData.  This is the standard
     * AmigaOS pattern for passing data to a newly created process. */
    Forbid();

    proc = CreateNewProcTags(
        NP_Entry,     (ULONG)amiga_thread_entry,
        NP_StackSize, (ULONG)stack_size,
        NP_Name,      (ULONG)"CL-Thread",
        TAG_DONE
    );

    if (!proc) {
        Permit();
        FreeVec(at);
        return -1;
    }

    at->proc = proc;
    /* Set UserData so the child can find its AmigaThread struct */
    ((struct Task *)proc)->tc_UserData = at;

    Permit();  /* Now child can start and read tc_UserData safely */

    *handle = at;
    return 0;
}

int platform_thread_join(void *handle, void **result)
{
    AmigaThread *at = (AmigaThread *)handle;
    BYTE sig = AllocSignal(-1);
    int need_wait = 0;

    /* Register as the joiner (works from ANY task, not just the
     * creator) and decide atomically whether the child already
     * finished.  If no signal bit is free, degrade to a Delay() poll —
     * correct, just coarser. */
    Forbid();
    if (!at->finished) {
        if (sig < 0) {
            while (!at->finished) {
                Permit();
                Delay(1);      /* give the child CPU time (1/50s tick) */
                Forbid();
            }
        } else {
            at->joiner   = FindTask(NULL);
            at->join_sig = sig;
            need_wait = 1;
        }
    }
    Permit();

    if (need_wait)
        Wait(1UL << sig);   /* child's Signal() uses locals only */
    if (sig >= 0)
        FreeSignal(sig);    /* our own task's bit, consumed or unused */

    if (result) *result = at->result;
    FreeVec(at);
    return 0;
}

/* Detach: hand `at` to whichever side finishes last.  If the child
 * already published finished (its final access to `at`), free the
 * struct here; otherwise mark it detached and the child frees it in
 * its exit path.  Closes the old ~32-bytes + one-signal-bit leak per
 * detached thread (the bit exhaustion starved every later condvar
 * wait into its degraded fallback). */
void platform_thread_detach(void *handle)
{
    AmigaThread *at = (AmigaThread *)handle;
    int free_now;

    Forbid();
    if (at->finished) {
        free_now = 1;
    } else {
        at->detached = 1;
        free_now = 0;
    }
    Permit();

    if (free_now)
        FreeVec(at);
}

void platform_thread_yield(void)
{
    /* Let other tasks of same/higher priority run */
    Forbid();
    Permit();
}

/* ================================================================
 * Mutex (SignalSemaphore)
 * ================================================================ */

int platform_mutex_init(void **handle)
{
    struct SignalSemaphore *sem;
    sem = (struct SignalSemaphore *)AllocVec(sizeof(struct SignalSemaphore), MEMF_CLEAR);
    if (!sem) return -1;
    InitSemaphore(sem);
    *handle = sem;
    return 0;
}

/* SignalSemaphore is naturally recursive: the same task may ObtainSemaphore
 * multiple times and must ReleaseSemaphore the same number of times. So a
 * recursive lock on AmigaOS is just a regular SignalSemaphore. */
int platform_mutex_init_recursive(void **handle)
{
    return platform_mutex_init(handle);
}

void platform_mutex_destroy(void *handle)
{
    FreeVec(handle);
}

void platform_mutex_lock(void *handle)
{
    ObtainSemaphore((struct SignalSemaphore *)handle);
}

void platform_mutex_unlock(void *handle)
{
    ReleaseSemaphore((struct SignalSemaphore *)handle);
}

int platform_mutex_trylock(void *handle)
{
    return AttemptSemaphore((struct SignalSemaphore *)handle) ? 0 : 1;
}

/* ================================================================
 * Read-Write Lock (SignalSemaphore shared/exclusive modes)
 *
 * AmigaOS SignalSemaphore natively supports shared (read) and
 * exclusive (write) access — a perfect fit for rwlock semantics.
 * ================================================================ */

int platform_rwlock_init(void **handle)
{
    struct SignalSemaphore *sem;
    sem = (struct SignalSemaphore *)AllocVec(sizeof(struct SignalSemaphore), MEMF_CLEAR);
    if (!sem) return -1;
    InitSemaphore(sem);
    *handle = sem;
    return 0;
}

void platform_rwlock_destroy(void *handle)
{
    FreeVec(handle);
}

void platform_rwlock_rdlock(void *handle)
{
    ObtainSemaphoreShared((struct SignalSemaphore *)handle);
}

void platform_rwlock_wrlock(void *handle)
{
    ObtainSemaphore((struct SignalSemaphore *)handle);
}

void platform_rwlock_unlock(void *handle)
{
    ReleaseSemaphore((struct SignalSemaphore *)handle);
}

/* ================================================================
 * Condition variable
 *
 * AmigaOS has no native condvar.  We implement one using a
 * SignalSemaphore (protects waiter list) + per-waiter signal bits.
 * ================================================================ */

#define AMIGA_CV_MAX_WAITERS 32

typedef struct {
    struct SignalSemaphore  lock;
    struct Task            *waiters[AMIGA_CV_MAX_WAITERS];
    BYTE                    signals[AMIGA_CV_MAX_WAITERS];
    int                     count;
} AmigaCondVar;

int platform_condvar_init(void **handle)
{
    AmigaCondVar *cv = (AmigaCondVar *)AllocVec(sizeof(AmigaCondVar), MEMF_CLEAR);
    if (!cv) return -1;
    InitSemaphore(&cv->lock);
    cv->count = 0;
    *handle = cv;
    return 0;
}

void platform_condvar_destroy(void *handle)
{
    FreeVec(handle);
}

void platform_condvar_wait(void *handle, void *mutex)
{
    AmigaCondVar *cv = (AmigaCondVar *)handle;
    struct Task *me = FindTask(NULL);
    BYTE sig = AllocSignal(-1);
    int registered = 0;

    /* Degraded paths (no free signal bit / waiter table full) must
     * behave like a SPURIOUS WAKEUP: release the mutex so the
     * signalling side can make progress, yield, re-acquire, return.
     * The old code returned WITHOUT releasing the mutex (sig < 0) or
     * Wait()ed on a signal nobody would ever send (table full) —
     * either way the caller's predicate loop then held the mutex
     * forever and every signaller deadlocked (observed as a gc_mutex
     * deadlock once detached threads had exhausted the signal bits). */
    if (sig >= 0) {
        ObtainSemaphore(&cv->lock);
        if (cv->count < AMIGA_CV_MAX_WAITERS) {
            cv->waiters[cv->count] = me;
            cv->signals[cv->count] = sig;
            cv->count++;
            registered = 1;
        }
        ReleaseSemaphore(&cv->lock);
    }

    if (!registered) {
        if (sig >= 0) FreeSignal(sig);
        platform_mutex_unlock(mutex);
        Delay(1);                      /* spurious wakeup, 1/50s backoff */
        platform_mutex_lock(mutex);
        return;
    }

    /* Release the mutex, wait for signal, re-acquire mutex */
    platform_mutex_unlock(mutex);
    Wait(1UL << sig);
    platform_mutex_lock(mutex);

    FreeSignal(sig);
}

int platform_condvar_wait_timeout(void *handle, void *mutex, uint32_t ms)
{
    AmigaCondVar *cv = (AmigaCondVar *)handle;
    struct Task *me = FindTask(NULL);
    BYTE sig = AllocSignal(-1);
    int registered = 0;
    int got = 0;
    int i;

    if (sig >= 0) {
        ObtainSemaphore(&cv->lock);
        if (cv->count < AMIGA_CV_MAX_WAITERS) {
            cv->waiters[cv->count] = me;
            cv->signals[cv->count] = sig;
            cv->count++;
            registered = 1;
        }
        ReleaseSemaphore(&cv->lock);
    }

    if (!registered) {
        /* Same spurious-wakeup degradation as platform_condvar_wait. */
        if (sig >= 0) FreeSignal(sig);
        platform_mutex_unlock(mutex);
        Delay(ms / 20 + 1);
        platform_mutex_lock(mutex);
        return 1;
    }

    platform_mutex_unlock(mutex);

    /* Timed wait via a Delay() poll — crude but functional.  Track the
     * signal in `got`: the old code re-tested SetSignal AFTER the loop,
     * but the in-loop SetSignal had already CONSUMED the bit, so every
     * wait that was signalled during the poll was misreported as a
     * timeout. */
    {
        uint32_t elapsed = 0;
        uint32_t step = ms < 50 ? ms : 50;  /* 50ms steps */
        for (;;) {
            if (SetSignal(0, 1UL << sig) & (1UL << sig)) {
                got = 1;
                break;
            }
            if (elapsed >= ms) break;
            Delay(step / 20 + 1);  /* Delay() uses 1/50s ticks */
            elapsed += step;
        }
    }

    if (!got) {
        /* Timed out — remove ourselves from the waiter list.  If we are
         * NOT in the list, a signaller already popped us and Signal()ed
         * (inside cv->lock, so the send completed before we got the
         * lock): that wakeup targeted us and must not be dropped —
         * consume it and report "signalled". */
        int found = 0;
        ObtainSemaphore(&cv->lock);
        for (i = 0; i < cv->count; i++) {
            if (cv->waiters[i] == me && cv->signals[i] == sig) {
                cv->count--;
                cv->waiters[i] = cv->waiters[cv->count];
                cv->signals[i] = cv->signals[cv->count];
                found = 1;
                break;
            }
        }
        ReleaseSemaphore(&cv->lock);
        if (!found) {
            Wait(1UL << sig);   /* delivered or in flight — consume it */
            got = 1;
        }
    }

    platform_mutex_lock(mutex);
    FreeSignal(sig);
    return got ? 0 : 1;
}

void platform_condvar_signal(void *handle)
{
    AmigaCondVar *cv = (AmigaCondVar *)handle;

    ObtainSemaphore(&cv->lock);
    if (cv->count > 0) {
        cv->count--;
        Signal(cv->waiters[cv->count], 1UL << cv->signals[cv->count]);
    }
    ReleaseSemaphore(&cv->lock);
}

void platform_condvar_broadcast(void *handle)
{
    AmigaCondVar *cv = (AmigaCondVar *)handle;
    int i;

    ObtainSemaphore(&cv->lock);
    for (i = 0; i < cv->count; i++)
        Signal(cv->waiters[i], 1UL << cv->signals[i]);
    cv->count = 0;
    ReleaseSemaphore(&cv->lock);
}

/* ================================================================
 * Atomics (Forbid/Permit — safe on 68020, very short critical sections)
 * ================================================================ */

uint32_t platform_atomic_inc(volatile uint32_t *ptr)
{
    uint32_t val;
    Forbid();
    val = ++(*ptr);
    Permit();
    return val;
}

uint32_t platform_atomic_dec(volatile uint32_t *ptr)
{
    uint32_t val;
    Forbid();
    val = --(*ptr);
    Permit();
    return val;
}

int platform_atomic_cas(volatile uint32_t *ptr, uint32_t expected,
                        uint32_t desired)
{
    int ok;
    Forbid();
    ok = (*ptr == expected);
    if (ok) *ptr = desired;
    Permit();
    return ok;
}

void platform_memory_barrier(void)
{
    /* Single-core 68k: no CPU reordering; only compiler reordering must
     * be prevented.  GCC: an empty asm with a memory clobber.  Other
     * compilers (vbcc): Forbid/Permit — opaque libcalls the compiler
     * cannot reorder stores across. */
#if defined(__GNUC__)
    __asm__ __volatile__("" ::: "memory");
#else
    Forbid();
    Permit();
#endif
}

/* ================================================================
 * TLS (Thread-Local Storage via tc_UserData)
 *
 * Note: tc_UserData is also used by AmigaThread for join sync.
 * Phase 1 uses a simple scheme: the main thread sets tc_UserData
 * directly.  For child threads (Phase 4+), the thread entry wrapper
 * will set tc_UserData to the CL_Thread* after reading the
 * AmigaThread* from it.
 * ================================================================ */

void platform_tls_init(void)
{
    /* Nothing needed — tc_UserData exists in every Task struct */
}

void platform_tls_set(void *value)
{
    FindTask(NULL)->tc_UserData = value;
}

void *platform_tls_get(void)
{
    return FindTask(NULL)->tc_UserData;
}
