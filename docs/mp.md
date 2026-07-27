# `MP` — Multiprocessing (Threads)

Kernel threads with per-thread dynamic bindings, locks, and condition variables.
On POSIX this is pthreads (`pthread_rwlock`, `__sync_*` atomics); on AmigaOS it is
`CreateNewProc()`, shared/exclusive `SignalSemaphore`s, custom condition variables
over signal bits, and `Forbid()`/`Permit()` for atomicity. GC is stop-the-world
with safepoints across all live threads.

- **Package:** `MP` (uses `CL`)
- **Inherited by:** `COMMON-LISP-USER`.
- This package backs the **bordeaux-threads** CL-Amiga fork (BT v1 and v2), which
  is what most concurrent libraries (Sento, lparallel, …) build on.

```lisp
(let ((lock (mp:make-lock "counter"))
      (n 0))
  (let ((threads (loop repeat 4 collect
                   (mp:make-thread
                     (lambda ()
                       (dotimes (i 1000)
                         (mp:with-lock-held (lock) (incf n))))))))
    (mapc #'mp:join-thread threads)
    n))            ; => 4000
```

## Threads

| Signature | Kind | Description |
|-----------|------|-------------|
| `(make-thread function &key name)` | function | Spawn a thread running a function |
| `(join-thread thread)` | function | Block until a thread finishes, returning its result |
| `(current-thread)` | function | The thread object for the calling thread |
| `(all-threads)` | function | List of all live threads |
| `(thread-name thread)` | function | A thread's name |
| `(thread-alive-p thread)` | function | Whether a thread is still running |
| `(threadp object)` | function | Type predicate for thread objects |
| `(thread-yield)` | function | Hint the scheduler to run other threads |
| `(interrupt-thread thread function)` | function | Run a function in the context of another thread |
| `(destroy-thread thread)` | function | Forcibly terminate a thread |
| `(dump-thread-waits)` | function | Debug: print what each thread is waiting on |

## Locks

| Signature | Kind | Description |
|-----------|------|-------------|
| `(make-lock &optional name)` | function | Create a mutex |
| `(make-recursive-lock &optional name)` | function | Create a recursive (re-entrant) lock |
| `(acquire-lock lock &optional wait-p)` | function | Acquire a lock; with `wait-p` `nil`, try without blocking |
| `(release-lock lock)` | function | Release a held lock |
| `(with-lock-held (lock) &body body)` | macro | Acquire/release around a body |
| `(with-recursive-lock-held (lock) &body body)` | macro | As above for a recursive lock |
| `(lockp object)` | function | Type predicate for locks |
| `(lock-name lock)` | function | A lock's name |

## Condition variables

| Signature | Kind | Description |
|-----------|------|-------------|
| `(make-condition-variable &optional name)` | function | Create a named condition variable |
| `(condition-wait condition-variable lock &optional timeout)` | function | Atomically release the lock and wait; `timeout` in seconds |
| `(condition-notify condition-variable)` | function | Wake one waiter |
| `(condition-broadcast condition-variable)` | function | Wake all waiters |
| `(condition-variable-p object)` | function | Type predicate |
| `(condition-name condition-variable)` | function | A condition variable's name |

## Memory barriers

| Signature | Kind | Description |
|-----------|------|-------------|
| `(read-memory-barrier)` | function | Acquire/read fence |
| `(write-memory-barrier)` | function | Release/write fence |

> `%make-recursive-lock` is the internal primitive behind `make-recursive-lock`.
> Not yet covered: semaphores, atomic integers, `with-timeout`, and `:timeout` on
> `acquire-lock` — see [Known Limitations](../README.md#known-limitations-and-future-work).

## Source of truth

`tests/test_threads.c`, `tests/test_dynbind_threaded.c`,
`tests/test_gc_threaded.c`, and the threading block in
`tests/amiga/run-tests.lisp`.
