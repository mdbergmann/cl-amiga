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
void platform_thread_yield(void);

/* ---- Mutex ---- */
int  platform_mutex_init(void **handle);
void platform_mutex_destroy(void *handle);
void platform_mutex_lock(void *handle);
void platform_mutex_unlock(void *handle);
int  platform_mutex_trylock(void *handle);   /* 0 = acquired, non-zero = busy */

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

/* ---- TLS (for cl_current_thread) ---- */
void  platform_tls_init(void);
void  platform_tls_set(void *value);
void *platform_tls_get(void);

#endif /* CL_PLATFORM_THREAD_H */
