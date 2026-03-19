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
- **Tests**: `tests/test_gc_threaded.c` — 8 tests
- Committed: `3d851d1`

## Phase 3: Thread-Local Dynamic Bindings — COMPLETE
- Per-thread TLV table (256-slot open-addressing hash) for special variable isolation
- OP_GLOAD/OP_GSTORE/OP_DYNBIND/OP_PROGV_BIND/OP_DYNUNBIND use TLV
- `cl_symbol_value()`/`cl_set_symbol_value()`/`cl_symbol_boundp()` TLV-aware accessors
- Dynamic variable inheritance via `cl_tlv_snapshot()`
- All printer/stream/reader/repl/debugger vars migrated to TLV accessors
- **Tests**: `tests/test_dynbind_threaded.c` — 16 tests
- Committed: `e1dadb6`

## Phase 4: CL-Level API (MP package) — COMPLETE
- **New types**: `TYPE_THREAD`, `TYPE_LOCK`, `TYPE_CONDVAR` in `types.h`
  - `CL_ThreadObj` (thread_id + name), `CL_Lock` (lock_id + name), `CL_CondVar` (condvar_id)
  - Side tables in `thread.c`: `cl_thread_table[32]`, `cl_lock_table[64]`, `cl_condvar_table[32]`
- **New file**: `src/core/builtins_thread.c` — MP package builtins
  - Threads: `make-thread`, `join-thread`, `thread-alive-p`, `current-thread`, `all-threads`, `thread-name`, `thread-yield`
  - Locks: `make-lock`, `acquire-lock`, `release-lock`
  - Condvars: `make-condition-variable`, `condition-wait`, `condition-notify`
- **Thread entry wrapper**: alloc worker CL_Thread (4K VM stack, 256 frames, 256 NLX), TLV snapshot from parent, register before OS thread create, CL_CATCH/CL_UNCATCH error handling, clean up on exit
- **MP package**: created in `package.c`, CL-USER uses MP
- **boot.lisp**: `mp:with-lock-held`, `mp:make-recursive-lock`, `mp:with-recursive-lock-held`, `mp:read-memory-barrier`, `mp:write-memory-barrier` (threading stubs moved from EXT to MP; EXT retains only `defglobal`)
- **GC support**: `gc_mark_children` cases for new types; `main_thread_obj` GC-rooted
- **Printer**: `#<THREAD name>`, `#<LOCK name>`, `#<CONDITION-VARIABLE>`
- **Modified files**: `types.h`, `types.c`, `mem.c`, `printer.c`, `package.h`, `package.c`, `builtins.c`, `thread.h`, `thread.c`, `Makefile`, `Makefile.cross`, `lib/boot.lisp`
- **Tests**: `tests/test_threads.c` — 21 tests (create/join, alive-p, current-thread, all-threads, yield, locks, lock contention, condvar, TLV inheritance, error in thread, multiple concurrent, GC stress)
- All 679+ host tests pass (658 existing + 21 new)

## Phase 5: Shared State Protection — NEXT
- Read-write locks for package registry, macro/type tables
- Per-stream mutex, gensym atomic counter
