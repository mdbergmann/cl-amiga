# Threading Support for CL-Amiga

## Context

CL-Amiga is a Common Lisp bytecode VM targeting AmigaOS 3+ (68020) and macOS/Linux hosts. The codebase is currently deeply single-threaded with 40+ global mutable variables. We want to add threading support:

- **Host**: POSIX threads (pthreads)
- **Amiga**: AmigaOS Exec Tasks + SignalSemaphore

This enables concurrent Lisp programs and is a prerequisite for networking servers, background compilation, and parallel workloads.

## Architecture Overview

**Key constraint**: All `CL_Obj` values are 32-bit arena-relative byte offsets into a single shared heap. Multiple heaps are impossible without redesigning the entire tagging scheme. Therefore: **single shared heap, per-thread execution state, stop-the-world GC**.

### Platform Mapping

| Concept         | Host (POSIX)            | Amiga (OS3+)                    |
|-----------------|-------------------------|---------------------------------|
| Thread          | `pthread_t`             | Exec `struct Task *`            |
| Mutex           | `pthread_mutex_t`       | `struct SignalSemaphore`        |
| Condition var   | `pthread_cond_t`        | Signal/Wait + semaphore         |
| TLS             | `pthread_key_t`         | `FindTask(NULL)->tc_UserData`   |
| Atomics         | `__sync_*` builtins     | `Forbid()`/`Permit()` pairs    |

---

## Phase 0: Bundle Globals into CL_Thread (No Threading Yet)

**Goal**: Refactor all per-thread global state into a `CL_Thread` struct. Single global instance, zero behavioral change. All tests must pass identically.

### 0.1 Create `src/core/thread.h`

Define `CL_Thread` struct containing all per-thread state:

```c
typedef struct CL_Thread_s {
    /* Thread metadata */
    uint32_t id;
    CL_Obj   name;           /* CL string or NIL */
    uint8_t  status;         /* 0=created, 1=running, 2=finished, 3=aborted */
    CL_Obj   result;

    /* VM instance */
    CL_VM    vm;             /* stack, frames, sp, fp */

    /* Execution stacks */
    CL_DynBinding     dyn_stack[CL_MAX_DYN_BINDINGS];  /* 1024 */
    int               dyn_top;
    CL_NLXFrame      *nlx_stack;    /* heap-allocated, size configurable */
    int               nlx_max;
    int               nlx_top;
    CL_HandlerBinding handler_stack[CL_MAX_HANDLER_BINDINGS]; /* 64 */
    int               handler_top, handler_floor;
    CL_RestartBinding restart_stack[CL_MAX_RESTART_BINDINGS]; /* 64 */
    int               restart_top, restart_floor;

    /* Error frames */
    CL_ErrorFrame error_frames[CL_MAX_ERROR_FRAMES]; /* 16 */
    int           error_frame_top;
    int           error_code;
    char          error_msg[512];
    int           exit_code;

    /* Multiple values */
    CL_Obj mv_values[CL_MAX_MV]; /* 20 */
    int    mv_count;

    /* GC root stack (per-thread) */
    CL_Obj *gc_roots[CL_GC_ROOT_STACK_SIZE]; /* 1024 */
    int     gc_root_count;

    /* Pending throw */
    int    pending_throw;
    CL_Obj pending_tag, pending_value;
    int    pending_error_code;
    char   pending_error_msg[512];

    /* Reader state (was static in reader.c) */
    CL_Obj      reader_stream;
    int         eof_seen, read_suppress, reader_line;
    const char *current_source_file;
    uint16_t    current_file_id;

    /* Printer state (was static in printer.c) */
    int32_t print_depth, print_column;
    int32_t pp_indent_stack[32];
    int32_t pp_indent_top;
    /* circle detection arrays */

    /* Compiler chain */
    CL_Compiler *active_compiler;

    /* Trace & debug */
    int  trace_depth, trace_count;
    char backtrace_buf[CL_BACKTRACE_BUF_SIZE];
    char *c_stack_base;

    /* VM extras */
    CL_Obj vm_extra_args[256];
    int    vm_extra_args_count;

    /* Platform handle (Phase 1+) */
    void *platform_handle;

    /* GC coordination (Phase 2+) */
    volatile uint8_t gc_requested;
    volatile uint8_t gc_stopped;

    /* Thread registry (Phase 2+) */
    struct CL_Thread_s *next;
} CL_Thread;
```

### 0.2 Global access pattern

```c
/* Single global pointer -- becomes TLS in Phase 1 */
extern CL_Thread *cl_current_thread;
#define CT cl_current_thread
```

### 0.3 Replace all global references

Every file that touches per-thread state gets updated. Examples:
- `cl_vm.sp` -> `CT->vm.sp`
- `cl_dyn_stack[i]` -> `CT->dyn_stack[i]`
- `cl_nlx_top` -> `CT->nlx_top`
- `gc_root_count` -> `CT->gc_root_count`
- `cl_mv_values[i]` -> `CT->mv_values[i]`
- `cl_error_frames[i]` -> `CT->error_frames[i]`
- Static reader/printer vars -> `CT->reader_stream`, `CT->print_depth`, etc.

### 0.4 Files to modify

- **New**: `src/core/thread.h`
- **Heavy changes**: `vm.h`, `vm.c`, `mem.h`, `mem.c`, `error.h`, `error.c`, `reader.c`, `printer.c`, `compiler.c`
- **Medium changes**: `main.c`, `repl.c`, `debugger.c`
- **Light changes**: All `builtins_*.c` that reference any global (most of them)

### 0.5 Verification

- `make test` -- all 656+ host tests pass
- `make -f Makefile.cross test-amiga` -- Amiga tests pass
- Zero behavioral change

---

## Phase 1: Platform Threading Primitives

### 1.1 New header `src/platform/platform_thread.h`

```c
/* Thread */
int  platform_thread_create(void **handle, void *(*func)(void *), void *arg,
                            uint32_t stack_size);
int  platform_thread_join(void *handle, void **result);
void platform_thread_yield(void);

/* Mutex */
int  platform_mutex_init(void **handle);
void platform_mutex_destroy(void *handle);
void platform_mutex_lock(void *handle);
void platform_mutex_unlock(void *handle);
int  platform_mutex_trylock(void *handle);

/* Condition variable */
int  platform_condvar_init(void **handle);
void platform_condvar_destroy(void *handle);
void platform_condvar_wait(void *handle, void *mutex);
int  platform_condvar_wait_timeout(void *handle, void *mutex, uint32_t ms);
void platform_condvar_signal(void *handle);
void platform_condvar_broadcast(void *handle);

/* Atomics */
uint32_t platform_atomic_inc(volatile uint32_t *ptr);
int      platform_atomic_cas(volatile uint32_t *ptr, uint32_t expected,
                             uint32_t desired);

/* TLS (for cl_current_thread) */
void  platform_tls_init(void);
void  platform_tls_set(void *value);
void *platform_tls_get(void);
```

### 1.2 POSIX implementation (`platform_thread_posix.c`)
- Thin wrappers around pthreads
- TLS via `pthread_key_t`
- Atomics via GCC `__sync_*` builtins

### 1.3 Amiga implementation (`platform_thread_amiga.c`)
- Thread: `CreateNewProc()` (dos.library) or `AddTask()` (exec.library)
- Mutex: `struct SignalSemaphore` (statically embedded, `InitSemaphore`/`ObtainSemaphore`/`ReleaseSemaphore`)
- CondVar: custom via Signal/Wait + waiter list + semaphore
- TLS: `FindTask(NULL)->tc_UserData`
- Atomics: `Forbid()`/`Permit()` pairs (short, safe on 68020)
- Join: finished-signal pattern (joiner allocates signal bit, waits)

### 1.4 Switch `cl_current_thread` to TLS

```c
#define cl_current_thread ((CL_Thread *)platform_tls_get())
#define CT cl_current_thread
```

### 1.5 Tests
- `tests/test_thread_platform.c` -- mutex correctness, condvar signal/broadcast, thread create/join, atomic increment from multiple threads

---

## Phase 2: GC Coordination

### 2.1 Thread registry
```c
static CL_Thread  *cl_thread_list;       /* linked list */
static void       *cl_thread_list_lock;  /* mutex */
static int         cl_thread_count;
```

### 2.2 Allocation mutex
- `cl_alloc()` acquires global allocation mutex before bump/free-list access
- Simple but sufficient (threads are typically I/O-bound; Amiga at 14MHz won't have high contention)

### 2.3 Safepoints (stop-the-world)
Insert `CL_SAFEPOINT()` checks at:
1. `OP_CALL` / `OP_TAILCALL` -- covers all function calls and loop iterations
2. Inside `cl_alloc()` -- before allocation attempt
3. Backward jumps (`OP_JMP` with negative offset) -- loop bodies

```c
#define CL_SAFEPOINT() \
    do { if (CT->gc_requested) cl_gc_safepoint(); } while (0)
```

`cl_gc_safepoint()`: sets `CT->gc_stopped = 1`, waits on condvar until GC complete, clears flags.

### 2.4 Stop-the-world protocol
1. GC initiator acquires `gc_mutex`
2. Sets `gc_requested = 1` on all other threads
3. Waits until all threads have `gc_stopped = 1`
4. Runs `gc_mark()` -- iterates all threads' root sets + shared globals
5. Runs `gc_sweep()`
6. Clears all flags, broadcasts condvar to wake threads
7. Releases `gc_mutex`

### 2.5 Multi-thread root marking
`gc_mark()` iterates `cl_thread_list`, for each thread marks:
- `thread->gc_roots[0..gc_root_count]`
- `thread->vm.stack[0..sp]`, `thread->vm.frames[0..fp].bytecode`
- `thread->dyn_stack`, `thread->nlx_stack`
- `thread->handler_stack`, `thread->restart_stack`
- `thread->mv_values`, `thread->pending_tag/value`
- `thread->reader_stream`
- `thread->active_compiler` chain

Plus shared globals (unchanged): `cl_package_registry`, macro/setf/type tables, etc.

---

## Phase 3: Thread-Local Dynamic Bindings

### The Problem
Currently `OP_DYNBIND` mutates `symbol->value` directly in the shared heap. With threads, two threads binding `*package*` would overwrite each other.

### Solution: Per-Thread TLV (Thread-Local Value) Table

```c
/* In CL_Thread */
#define CL_TLV_TABLE_SIZE 256
typedef struct { CL_Obj symbol; CL_Obj value; } CL_TLVEntry;
CL_TLVEntry tlv_table[CL_TLV_TABLE_SIZE];
```

- **`OP_GLOAD` (read special var)**: Check `CT->tlv_table` first (hash lookup by symbol). If found, return thread-local value. If not, fall back to `symbol->value` (global default).
- **`OP_DYNBIND`**: Save old TLV entry on `dyn_stack`, set new value in `tlv_table`.
- **`OP_DYNUNBIND`**: Restore saved value to `tlv_table` (or remove entry).
- **`OP_GSTORE` on special var**: Write to `tlv_table` if entry exists, else to `symbol->value`.

### Dynamic Variable Inheritance
When creating a child thread, snapshot parent's active dynamic bindings into child's `tlv_table`. This gives the child copies of `*package*`, `*standard-output*`, `*readtable*`, etc.

---

## Phase 4: CL-Level API (MP package)

The internal threading package is `MP` (multiprocessing). A Bordeaux Threads compatibility layer can be built on top later in Lisp.

### 4.1 New type: `TYPE_THREAD`
Add type tag to `types.h`. Thread objects wrap `CL_Thread *`.

### 4.2 New file: `src/core/builtins_thread.c`

```lisp
;; Threads
(mp:make-thread function &key name)    -> thread
(mp:join-thread thread)                -> values
(mp:thread-alive-p thread)             -> bool
(mp:current-thread)                    -> thread
(mp:all-threads)                       -> list
(mp:thread-name thread)                -> string/nil
(mp:thread-yield)                      -> nil

;; Locks (side table of platform mutexes, like file handles)
(mp:make-lock &optional name)          -> lock
(mp:acquire-lock lock &optional wait)  -> bool
(mp:release-lock lock)                 -> nil

;; Condition variables
(mp:make-condition-variable)           -> cv
(mp:condition-wait cv lock)            -> t
(mp:condition-notify cv)               -> nil
```

### 4.3 Boot.lisp additions
```lisp
(defmacro mp:with-lock-held ((lock) &body body)
  (let ((l (gensym)))
    `(let ((,l ,lock))
       (mp:acquire-lock ,l t)
       (unwind-protect (progn ,@body)
         (mp:release-lock ,l)))))
```

### 4.4 Thread entry wrapper
New thread runs: set up TLS -> inherit dynamic bindings -> call user function -> store result -> signal joiner -> clean up.

---

## Phase 5: Shared State Protection

- Package registry, macro/setf/type tables: read-write lock (readers concurrent, writers exclusive)
- Gensym counter: `platform_atomic_inc`
- File/socket handle tables: mutex around open/close (read/write on different handles is independent)
- Stream output: optional per-stream mutex for serialized writes

---

## Memory Budget (Amiga)

| Component | Main thread | Worker thread (compact) |
|-----------|------------|------------------------|
| VM stack  | 64KB       | 16KB                   |
| VM frames | 60KB       | 15KB                   |
| NLX stack | 400KB      | 50KB (256 frames)      |
| Fixed state | 42KB     | 42KB                   |
| **Total** | **~566KB** | **~123KB**             |

With 8MB RAM: main thread + 3 compact workers = ~935KB. Leaves ~7MB for heap.

---

## Verification Plan

After each phase:
1. `make test` -- all 656+ host tests pass
2. `make -f Makefile.cross test-amiga` -- Amiga tests pass
3. Phase-specific tests:
   - Phase 0: No new tests needed (pure refactor)
   - Phase 1: `test_thread_platform.c` -- mutex, condvar, thread create/join
   - Phase 2: `test_gc_threaded.c` -- concurrent allocation stress test
   - Phase 3: `test_dynbind_threaded.c` -- per-thread `*package*` isolation
   - Phase 4: `test_threads.c` -- full MP API from C; Lisp-level tests in `run-tests.lisp`
   - Phase 5: Stress tests with concurrent compilation, package creation

## Implementation Order

**Start with Phase 0** -- it's the largest but safest change (pure refactoring, no new behavior). Every subsequent phase builds on having `CL_Thread` in place. Phases 1-4 can proceed incrementally with each phase independently testable.
