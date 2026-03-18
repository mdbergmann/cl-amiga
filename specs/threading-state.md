# Threading Implementation State

## Phase 0: Bundle Globals into CL_Thread — COMPLETE
- `CL_Thread` struct in `src/core/thread.h` holds all per-thread state
- Single global instance, compatibility macros map legacy names
- All 658+ host tests pass, Amiga tests pass
- Committed: `ffb3ff9`

## Phase 1: Platform Threading Primitives — COMPLETE
- **New files**:
  - `src/platform/platform_thread.h` — API header (thread/mutex/condvar/atomic/TLS)
  - `src/platform/platform_thread_posix.c` — pthreads, `__sync_*` atomics, `pthread_key_t` TLS
  - `src/platform/platform_thread_amiga.c` — Exec Tasks (`CreateNewProc`), `SignalSemaphore`, `Forbid/Permit` atomics, `tc_UserData` TLS
- **Modified files**:
  - `src/core/thread.h` — `cl_current_thread` now TLS-backed via `cl_get_current_thread()` / `platform_tls_get()`
  - `src/core/thread.c` — calls `platform_tls_init/set` in `cl_thread_init()`; `cl_main_thread_ptr` exposed for fast access
  - `Makefile` — added `platform_thread_posix.c` to PLATFORM_SRC
  - `Makefile.cross` — added `platform_thread_amiga.c` to PLATFORM_SRC
- **Tests**: `tests/test_thread_platform.c` — 17 tests
- Committed: `76bcbbb`

## Phase 2: GC Coordination — COMPLETE
- **Thread registry**: linked list (`cl_thread_list`) + mutex (`cl_thread_list_lock`) + count
  - `cl_thread_register()`/`cl_thread_unregister()` in `thread.c`
  - Main thread auto-registered in `cl_thread_init()`
- **Allocation mutex**: `alloc_mutex` in `mem.c` protects bump pointer and free list
  - `cl_alloc()` locks/unlocks around allocation; releases before GC so other threads can reach safepoints
- **Safepoints**: `CL_SAFEPOINT()` macro checks `CT->gc_requested`
  - Inserted at: `OP_CALL`, `OP_TAILCALL`, `OP_APPLY`, backward `OP_JMP`, and `cl_alloc()` entry
  - Slow path `cl_gc_safepoint()`: sets `gc_stopped=1`, signals GC initiator, waits until `gc_requested` cleared
- **Stop-the-world protocol**: `cl_gc_stop_the_world()` / `cl_gc_resume_the_world()` in `thread.c`
  - GC initiator: acquires `gc_mutex`, sets `gc_requested` on all other threads, waits for all `gc_stopped`, runs GC, clears flags, broadcasts condvar
  - Skipped when `cl_thread_count == 1` (single-thread fast path)
- **Multi-thread root marking**: `gc_mark()` refactored
  - `gc_mark_thread_roots(CL_Thread *t)` — marks all per-thread roots for one thread
  - Iterates `cl_thread_list` to mark all threads, then marks shared globals
  - `cl_compiler_gc_mark_thread(CL_Thread *t)` and `cl_vm_gc_mark_extra_thread(CL_Thread *t)` — per-thread variants
- **Tests**: `tests/test_gc_threaded.c` — 8 tests (registry, single-thread regression, concurrent alloc 2/4 threads, concurrent alloc with forced GC, safepoint smoke)
- All 658+ existing tests pass; 8 new gc_threaded tests + 17 thread_platform tests pass

## Phase 3: Thread-Local Dynamic Bindings — NEXT
- Per-thread TLV table for special variable isolation
- OP_GLOAD/OP_DYNBIND/OP_DYNUNBIND updated for TLV lookup
- Dynamic variable inheritance on child thread creation

## Phase 4: CL-Level API (MP package)
- TYPE_THREAD, `builtins_thread.c`, Lisp-level mp:make-thread etc.

## Phase 5: Shared State Protection
- Read-write locks for package registry, macro/type tables
- Per-stream mutex, gensym atomic counter
