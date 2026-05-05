#ifndef CL_PLATFORM_THREAD_H
#define CL_PLATFORM_THREAD_H

/*
 * Platform threading abstraction for CL-Amiga.
 *
 * Host (POSIX): pthreads, __sync_* atomics, pthread_key_t TLS
 * Amiga (OS3+): Exec Tasks, SignalSemaphore, Forbid/Permit atomics, tc_UserData TLS
 *
 * Implementations: platform_thread_posix.c, platform_thread_amiga.c
 */

#include <stdint.h>

/* ---- Thread ---- */
int  platform_thread_create(void **handle, void *(*func)(void *), void *arg,
                            uint32_t stack_size);
int  platform_thread_join(void *handle, void **result);
/* Release OS-side resources without joining.  Caller must guarantee the
 * worker has already finished (status >= 2) so no further use of `handle`
 * will occur.  After this call, `handle` is invalid. */
void platform_thread_detach(void *handle);
void platform_thread_yield(void);

/* ---- Mutex ---- */
int  platform_mutex_init(void **handle);
int  platform_mutex_init_recursive(void **handle);
void platform_mutex_destroy(void *handle);
void platform_mutex_lock(void *handle);
void platform_mutex_unlock(void *handle);
int  platform_mutex_trylock(void *handle);   /* 0 = acquired, non-zero = busy */

/* ---- Read-Write Lock ---- */
int  platform_rwlock_init(void **handle);
void platform_rwlock_destroy(void *handle);
void platform_rwlock_rdlock(void *handle);
void platform_rwlock_wrlock(void *handle);
void platform_rwlock_unlock(void *handle);

/* ---- Condition variable ---- */
int  platform_condvar_init(void **handle);
void platform_condvar_destroy(void *handle);
void platform_condvar_wait(void *handle, void *mutex);
int  platform_condvar_wait_timeout(void *handle, void *mutex, uint32_t ms);
void platform_condvar_signal(void *handle);
void platform_condvar_broadcast(void *handle);

/* ---- Atomics ---- */
uint32_t platform_atomic_inc(volatile uint32_t *ptr);
uint32_t platform_atomic_dec(volatile uint32_t *ptr);
int      platform_atomic_cas(volatile uint32_t *ptr, uint32_t expected,
                             uint32_t desired);  /* 1 = swapped, 0 = failed */

/* ---- TLS (for cl_current_thread) ----
 *
 * On POSIX, the per-thread CL_Thread* is held in a `__thread` variable so
 * the hot `cl_get_current_thread()` path is a single TLS load (e.g. an
 * `mrs tpidr_el0`-based access on ARM64) instead of a `pthread_getspecific`
 * libcall.  Profiling sento showed `pthread_getspecific` dominating the real
 * CPU work in multi-threaded mode (every `call_builtin`, `cl_gc_push_root`,
 * `cl_gc_pop_roots`, `cl_dynbind_restore_to`, `cl_alloc`, ... pays it).
 *
 * On Amiga, `tc_UserData` already serves this role with zero overhead, so
 * there `platform_tls_get` stays a tiny function call.
 */
void  platform_tls_init(void);
void  platform_tls_set(void *value);

#ifdef PLATFORM_POSIX
extern __thread void *cl_tls_thread_ptr;
static inline void *platform_tls_get(void) { return cl_tls_thread_ptr; }
#else
void *platform_tls_get(void);
#endif

#endif /* CL_PLATFORM_THREAD_H */
