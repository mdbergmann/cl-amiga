/*
 * platform_thread_posix.c — POSIX threading primitives for CL-Amiga.
 *
 * Thin wrappers around pthreads, GCC __sync_* atomics, and pthread TLS.
 */

#include "platform_thread.h"
#include "platform.h"

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>

/* ================================================================
 * Thread
 * ================================================================ */

int platform_thread_create(void **handle, void *(*func)(void *), void *arg,
                           uint32_t stack_size)
{
    pthread_t *th;
    pthread_attr_t attr;
    int ret;

    th = (pthread_t *)malloc(sizeof(pthread_t));
    if (!th) return -1;

    pthread_attr_init(&attr);
    if (stack_size > 0)
        pthread_attr_setstacksize(&attr, (size_t)stack_size);

    ret = pthread_create(th, &attr, func, arg);
    pthread_attr_destroy(&attr);

    if (ret != 0) {
        free(th);
        return -1;
    }
    *handle = th;
    return 0;
}

int platform_thread_join(void *handle, void **result)
{
    pthread_t *th = (pthread_t *)handle;
    void *retval = NULL;
    int ret;

    ret = pthread_join(*th, &retval);
    free(th);

    if (result) *result = retval;
    return ret == 0 ? 0 : -1;
}

void platform_thread_detach(void *handle)
{
    pthread_t *th = (pthread_t *)handle;
    if (!th) return;
    /* pthread_detach is safe whether the thread is still running or has
     * already terminated: in the running case it flags the kernel to
     * reclaim resources at exit; in the terminated case it reaps now.
     * Either way no one can pthread_join *th afterwards, so freeing our
     * heap-allocated pthread_t wrapper is sound. */
    pthread_detach(*th);
    free(th);
}

void platform_thread_yield(void)
{
    sched_yield();
}

/* ================================================================
 * Mutex
 * ================================================================ */

int platform_mutex_init(void **handle)
{
    pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    if (!m) return -1;

    if (pthread_mutex_init(m, NULL) != 0) {
        free(m);
        return -1;
    }
    *handle = m;
    return 0;
}

int platform_mutex_init_recursive(void **handle)
{
    pthread_mutexattr_t attr;
    pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    if (!m) return -1;

    if (pthread_mutexattr_init(&attr) != 0) {
        free(m);
        return -1;
    }
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (pthread_mutex_init(m, &attr) != 0) {
        pthread_mutexattr_destroy(&attr);
        free(m);
        return -1;
    }
    pthread_mutexattr_destroy(&attr);
    *handle = m;
    return 0;
}

void platform_mutex_destroy(void *handle)
{
    pthread_mutex_t *m = (pthread_mutex_t *)handle;
    pthread_mutex_destroy(m);
    free(m);
}

void platform_mutex_lock(void *handle)
{
    pthread_mutex_lock((pthread_mutex_t *)handle);
}

void platform_mutex_unlock(void *handle)
{
    pthread_mutex_unlock((pthread_mutex_t *)handle);
}

int platform_mutex_trylock(void *handle)
{
    return pthread_mutex_trylock((pthread_mutex_t *)handle) == 0 ? 0 : 1;
}

/* ================================================================
 * Read-Write Lock
 * ================================================================ */

int platform_rwlock_init(void **handle)
{
    pthread_rwlock_t *rw = (pthread_rwlock_t *)malloc(sizeof(pthread_rwlock_t));
    if (!rw) return -1;

    if (pthread_rwlock_init(rw, NULL) != 0) {
        free(rw);
        return -1;
    }
    *handle = rw;
    return 0;
}

void platform_rwlock_destroy(void *handle)
{
    pthread_rwlock_t *rw = (pthread_rwlock_t *)handle;
    pthread_rwlock_destroy(rw);
    free(rw);
}

void platform_rwlock_rdlock(void *handle)
{
    pthread_rwlock_rdlock((pthread_rwlock_t *)handle);
}

void platform_rwlock_wrlock(void *handle)
{
    pthread_rwlock_wrlock((pthread_rwlock_t *)handle);
}

void platform_rwlock_unlock(void *handle)
{
    pthread_rwlock_unlock((pthread_rwlock_t *)handle);
}

/* ================================================================
 * Condition variable
 * ================================================================ */

int platform_condvar_init(void **handle)
{
    pthread_cond_t *cv = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
    if (!cv) return -1;

    if (pthread_cond_init(cv, NULL) != 0) {
        free(cv);
        return -1;
    }
    *handle = cv;
    return 0;
}

void platform_condvar_destroy(void *handle)
{
    pthread_cond_t *cv = (pthread_cond_t *)handle;
    pthread_cond_destroy(cv);
    free(cv);
}

void platform_condvar_wait(void *handle, void *mutex)
{
    pthread_cond_wait((pthread_cond_t *)handle, (pthread_mutex_t *)mutex);
}

int platform_condvar_wait_timeout(void *handle, void *mutex, uint32_t ms)
{
    struct timespec ts;
    struct timeval tv;
    int ret;

    gettimeofday(&tv, NULL);
    ts.tv_sec = tv.tv_sec + (long)(ms / 1000);
    ts.tv_nsec = tv.tv_usec * 1000L + (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }

    ret = pthread_cond_timedwait((pthread_cond_t *)handle,
                                (pthread_mutex_t *)mutex, &ts);
    return ret == ETIMEDOUT ? 1 : 0;  /* 1 = timed out, 0 = signaled */
}

void platform_condvar_signal(void *handle)
{
    pthread_cond_signal((pthread_cond_t *)handle);
}

void platform_condvar_broadcast(void *handle)
{
    pthread_cond_broadcast((pthread_cond_t *)handle);
}

/* ================================================================
 * Atomics (GCC __sync builtins — available on all target GCC versions)
 * ================================================================ */

uint32_t platform_atomic_inc(volatile uint32_t *ptr)
{
    return __sync_add_and_fetch(ptr, 1);
}

uint32_t platform_atomic_dec(volatile uint32_t *ptr)
{
    return __sync_sub_and_fetch(ptr, 1);
}

int platform_atomic_cas(volatile uint32_t *ptr, uint32_t expected,
                        uint32_t desired)
{
    return __sync_bool_compare_and_swap(ptr, expected, desired) ? 1 : 0;
}

/* ================================================================
 * TLS (Thread-Local Storage for cl_current_thread)
 * ================================================================ */

static pthread_key_t tls_key;

void platform_tls_init(void)
{
    pthread_key_create(&tls_key, NULL);
}

void platform_tls_set(void *value)
{
    pthread_setspecific(tls_key, value);
}

void *platform_tls_get(void)
{
    return pthread_getspecific(tls_key);
}
