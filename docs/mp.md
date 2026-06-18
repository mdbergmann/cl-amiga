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

| Symbol | Kind | Description |
|--------|------|-------------|
| `make-thread` | function | Spawn a thread running a function; `&key name` |
| `join-thread` | function | Block until a thread finishes, returning its result |
| `current-thread` | function | The thread object for the calling thread |
| `all-threads` | function | List of all live threads |
| `thread-name` | function | A thread's name |
| `thread-alive-p` | function | Whether a thread is still running |
| `threadp` | function | Type predicate for thread objects |
| `thread-yield` | function | Hint the scheduler to run other threads |
| `interrupt-thread` | function | Run a function in the context of another thread |
| `destroy-thread` | function | Forcibly terminate a thread |
| `dump-thread-waits` | function | Debug: print what each thread is waiting on |

## Locks

| Symbol | Kind | Description |
|--------|------|-------------|
| `make-lock` | function | Create a mutex; `&optional name` |
| `make-recursive-lock` | function | Create a recursive (re-entrant) lock |
| `acquire-lock` | function | Acquire a lock; `&optional wait-p` |
| `release-lock` | function | Release a held lock |
| `with-lock-held` | macro | `(with-lock-held (lock) body…)` — acquire/release around a body |
| `with-recursive-lock-held` | macro | As above for a recursive lock |
| `lockp` | function | Type predicate for locks |
| `lock-name` | function | A lock's name |

## Condition variables

| Symbol | Kind | Description |
|--------|------|-------------|
| `make-condition-variable` | function | Create a named condition variable; `&optional name` |
| `condition-wait` | function | Atomically release a lock and wait; `&optional timeout` |
| `condition-notify` | function | Wake one waiter |
| `condition-broadcast` | function | Wake all waiters |
| `condition-variable-p` | function | Type predicate |
| `condition-name` | function | A condition variable's name |

## Memory barriers

| Symbol | Kind | Description |
|--------|------|-------------|
| `read-memory-barrier` | function | Acquire/read fence |
| `write-memory-barrier` | function | Release/write fence |

> `%make-recursive-lock` is the internal primitive behind `make-recursive-lock`.
> Not yet covered: semaphores, atomic integers, `with-timeout`, and `:timeout` on
> `acquire-lock` — see [Known Limitations](../README.md#known-limitations-and-future-work).

## Source of truth

`tests/test_threads.c`, `tests/test_dynbind_threaded.c`,
`tests/test_gc_threaded.c`, and the threading block in
`tests/amiga/run-tests.lisp`.
