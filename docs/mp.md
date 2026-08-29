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
> Not yet covered: semaphores, `with-timeout`, and `:timeout` on
> `acquire-lock` — see [Known Limitations](../README.md#known-limitations-and-future-work).

## Atomic operations

| Signature | Kind | Description |
|-----------|------|-------------|
| `(compare-and-swap place old new)` | macro | Atomically store `new` in `place` if it holds `old` (under `eq`). Returns the value `place` held when the comparison was made — `eq` to `old` exactly when the swap happened |
| `(cas place old new)` | macro | Alias for `compare-and-swap` |
| `(atomic-incf place &optional (delta 1))` | macro | Atomically add `delta` to the fixnum in `place`; returns the new value |
| `(atomic-decf place &optional (delta 1))` | macro | Atomically subtract `delta` from the fixnum in `place`; returns the new value |

Supported places: `(car c)` `(cdr c)` `(first c)` `(rest c)`, `(svref v i)` /
`(aref v i)` on a simple-vector, `(symbol-value s)` or a special variable
(the calling thread's dynamic binding when one is in effect), `(slot-value o 's)`
on a standard-object or structure, defstruct slot accessors, and macros,
symbol-macros or `(the type ...)` wrapping one of these.  The place's subforms
are evaluated once, left to right, before `old` and `new` (or `delta`).  An
unsupported place is an error at macroexpansion time.

`atomic-incf` / `atomic-decf` require the current value, the delta and the
result to be fixnums; anything else signals an error and leaves the place
unchanged — a counter never silently turns into a bignum.

The primitive is real on every target: a native compare-exchange on the host,
and a `Forbid()`/`Permit()` window on AmigaOS and MorphOS, where the single
core makes disabling task switching a complete atomicity guarantee.  These
operations are atomic with respect to each other across all threads; a plain
`setf` of the same cell is not part of the protocol.  A library backend maps
onto them directly — e.g. `atomics:cas` as
`(eq old (mp:compare-and-swap place old new))`.

## Source of truth

`tests/test_threads.c`, `tests/test_dynbind_threaded.c`,
`tests/test_gc_threaded.c`, `tests/test_atomics.c`, and the threading and
atomics blocks in `tests/amiga/run-tests.lisp`.
