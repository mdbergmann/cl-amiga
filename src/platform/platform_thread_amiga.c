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

typedef struct {
    struct SignalSemaphore  join_sem;
    volatile int            finished;
    void                   *result;
    void                  *(*func)(void *);
    void                   *arg;
    struct Task            *parent;    /* for join signaling */
    BYTE                    join_sig;  /* signal bit in parent */
    struct Process         *proc;
} AmigaThread;

/* Entry point for new process — runs user function, signals joiner */
static void amiga_thread_entry(void)
{
    struct Task *me = FindTask(NULL);
    AmigaThread *at = (AmigaThread *)me->tc_UserData;

    at->result = at->func(at->arg);

    ObtainSemaphore(&at->join_sem);
    at->finished = 1;
    ReleaseSemaphore(&at->join_sem);

    /* Wake parent if waiting in join */
    if (at->parent && at->join_sig >= 0)
        Signal(at->parent, 1UL << at->join_sig);
}

int platform_thread_create(void **handle, void *(*func)(void *), void *arg,
                           uint32_t stack_size)
{
    AmigaThread *at;
    struct Process *proc;

    at = (AmigaThread *)AllocVec(sizeof(AmigaThread), MEMF_CLEAR);
    if (!at) return -1;

    InitSemaphore(&at->join_sem);
    at->finished = 0;
    at->result = NULL;
    at->func = func;
    at->arg = arg;
    at->parent = FindTask(NULL);
    at->join_sig = AllocSignal(-1);

    if (at->join_sig < 0) {
        FreeVec(at);
        return -1;
    }

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
        FreeSignal(at->join_sig);
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

    /* Wait until child signals completion */
    for (;;) {
        ObtainSemaphore(&at->join_sem);
        if (at->finished) {
            ReleaseSemaphore(&at->join_sem);
            break;
        }
        ReleaseSemaphore(&at->join_sem);
        Wait(1UL << at->join_sig);
    }

    if (result) *result = at->result;

    FreeSignal(at->join_sig);
    FreeVec(at);
    return 0;
}

/* AmigaOS detach: caller guarantees the worker is finished (status >= 2),
 * so amiga_thread_entry has already executed the post-Signal section by
 * the time the OS scheduler completes the process exit.  However, we have
 * no portable way to wait specifically for the process structure to be
 * reclaimed without joining, so we conservatively leak the AmigaThread
 * struct (small — ~32 bytes per detached thread) and the join signal.
 * This avoids any race where amiga_thread_entry's epilogue is still
 * touching `at` after we'd freed it. */
void platform_thread_detach(void *handle)
{
    (void)handle;
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
    int idx;

    if (sig < 0) return;  /* no signal bits available — degrade gracefully */

    /* Register as waiter */
    ObtainSemaphore(&cv->lock);
    idx = cv->count;
    if (idx < AMIGA_CV_MAX_WAITERS) {
        cv->waiters[idx] = me;
        cv->signals[idx] = sig;
        cv->count++;
    }
    ReleaseSemaphore(&cv->lock);

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
    BYTE timer_sig;
    ULONG sigs;
    int idx, timed_out = 0;
    int i;

    if (sig < 0) return 1;

    /* Register as waiter */
    ObtainSemaphore(&cv->lock);
    idx = cv->count;
    if (idx < AMIGA_CV_MAX_WAITERS) {
        cv->waiters[idx] = me;
        cv->signals[idx] = sig;
        cv->count++;
    }
    ReleaseSemaphore(&cv->lock);

    platform_mutex_unlock(mutex);

    /* Simple timed wait: use Delay() approximation.
     * Proper timer.device usage would be more precise but much more code. */
    timer_sig = AllocSignal(-1);
    if (timer_sig >= 0) {
        /* Poll with short delays — crude but functional */
        uint32_t elapsed = 0;
        uint32_t step = ms < 50 ? ms : 50;  /* 50ms steps */
        while (elapsed < ms) {
            sigs = SetSignal(0, 1UL << sig);
            if (sigs & (1UL << sig)) break;
            Delay(step / 20 + 1);  /* Delay() uses 1/50s ticks */
            elapsed += step;
        }
        if (!(SetSignal(0, 1UL << sig) & (1UL << sig)))
            timed_out = 1;
        FreeSignal(timer_sig);
    } else {
        /* Fallback: just wait without timeout */
        Wait(1UL << sig);
    }

    /* Remove ourselves from waiter list if timed out */
    if (timed_out) {
        ObtainSemaphore(&cv->lock);
        for (i = 0; i < cv->count; i++) {
            if (cv->waiters[i] == me && cv->signals[i] == sig) {
                cv->count--;
                cv->waiters[i] = cv->waiters[cv->count];
                cv->signals[i] = cv->signals[cv->count];
                break;
            }
        }
        ReleaseSemaphore(&cv->lock);
    }

    platform_mutex_lock(mutex);
    FreeSignal(sig);
    return timed_out;
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
