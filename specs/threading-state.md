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
- **Tests**: `tests/test_thread_platform.c` — 17 tests (mutex, condvar, thread create/join, atomics, TLS isolation, CL_Thread TLS integration)
- All 658+ existing tests pass unchanged; 17 new tests pass

## Phase 2: GC Coordination — NEXT
- Thread registry (linked list + mutex)
- Allocation mutex around `cl_alloc()`
- Safepoints (`CL_SAFEPOINT()`) at OP_CALL, OP_TAILCALL, cl_alloc, backward jumps
- Stop-the-world protocol (set gc_requested on all threads, wait for gc_stopped)
- Multi-thread root marking in `gc_mark()`
- Test: `test_gc_threaded.c` — concurrent allocation stress test

## Phase 3: Thread-Local Dynamic Bindings
- Per-thread TLV table for special variable isolation
- OP_GLOAD/OP_DYNBIND/OP_DYNUNBIND updated for TLV lookup
- Dynamic variable inheritance on child thread creation

## Phase 4: CL-Level API (MP package)
- TYPE_THREAD, `builtins_thread.c`, Lisp-level mp:make-thread etc.

## Phase 5: Shared State Protection
- Read-write locks for package registry, macro/type tables
- Per-stream mutex, gensym atomic counter
