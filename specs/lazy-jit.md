# Lazy JIT (compile-on-hot)

Status: **proposed** (2026-06-04). Not yet implemented.

## Problem

Boot is ~5× slower with the JIT enabled than without it:

- `--no-jit`: ~0.8 s boot
- default (JIT on): ~3.8 s boot

The JIT is **eager / ahead-of-time, not lazy**. There is no hot-loop
threshold — `cl_jit_compile()` runs on *every* `CL_Bytecode` the moment
it is created:

- `src/core/fasl.c:1925` — every bytecode deserialized from a FASL
- `src/core/compiler.c:922` and `src/core/compiler.c:4094` — every
  function compiled from source

At boot we load `lib/boot.fasl` then `(require "clos")`. That is ~186
defuns/macros in `boot.lisp` + ~295 in `clos.lisp`, and once nested
lambdas, closures, every `defmethod` and macro body are counted it is
well over a thousand bytecode objects — each fully compiled to m68k
native code *before the REPL is reached*. With `--no-jit`,
`cl_jit_compile()` short-circuits at `jit.c:2877` (`if (!jit_active)
return;`), so all of that work is skipped. The extra ~3 s is whole-
program native compilation done up front.

### Cost amplifiers (per compiled function)

These make each eager compile expensive on real hardware, and are paid
hundreds–thousands of times during boot:

1. **Full cache flush per function** — `jit.c:2922` →
   `platform_amiga.c:1424` calls `CacheClearU()`, which flushes the
   *entire* I and D cache. Worse than its own cost: it dumps the data
   cache too, so the surrounding boot work (FASL parsing, GC, the next
   compile) then runs cold. Cheap on a bare 68020 (no write-back I-cache),
   genuinely expensive on 68030/040/060 — so the magnitude of the
   slowdown is CPU-dependent.
2. **Allocator churn** — `codebuf.c` starts the code buffer at 8 bytes
   (`cb_init(&cb, 8)` at `jit.c:2881`) and doubles, each step an
   `AllocVec` + `memcpy` + `FreeVec`. `walker_compile` additionally
   `platform_alloc`s two scratch arrays (`bc_to_native`, `is_target`,
   `jit.c:962-967`) and a `BranchPatch` list per function. On AmigaOS
   each `AllocVec`/`FreeVec` is a `Forbid/Permit`-bracketed memory-list
   operation.

## Goal

Compile a function only once it has proven hot, so boot compiles almost
nothing (most boot/CLOS functions are called only a handful of times
during load), while genuinely hot user code still reaches native speed
after a short warm-up. Target: boot back near the 0.8 s figure with the
JIT still enabled.

## Why the core change is small

The dispatch already keys off a single field — `vm.c:1948`,
`if (callee_bc->native_code && !is_tail && !is_func_traced(func_obj))`.
Native code is a sidecar pointer on `CL_Bytecode`; it is **never freed on
sweep** (bytecode objects are effectively permanent, and the GC
mark/update paths at `mem.c:848` / `mem.c:1734` do not touch
`native_code`), and `cl_jit_compile` only uses `platform_alloc` (system
memory, not the arena) so it **never triggers GC or moves `bc`**. That
makes calling it lazily from inside the VM loop safe with respect to GC —
the property that makes this tractable.

## Design

### Data-structure additions (`src/core/types.h`, `CL_Bytecode`)

Add two runtime-only fields. **Not FASL-serialized, so no
`CL_FASL_VERSION` bump.** Struct stays 32-bit clean.

- `uint16_t call_count;` — invocations observed while still interpreted.
- `uint8_t  jit_state;`  — one of:
  - `JIT_UNTRIED` (0) — not yet attempted; keep counting.
  - `JIT_COMPILING` (1) — a thread is compiling it now (race guard).
  - `JIT_COMPILED` (2) — `native_code != NULL`, fast path live.
  - `JIT_REJECTED` (3) — walker bailed; never retry.

Initialize `call_count = 0`, `jit_state = JIT_UNTRIED` in the bytecode
allocator alongside the existing `native_code = NULL`.

`JIT_REJECTED` is **mandatory**: today a walker bail and "not yet tried"
are both `native_code == NULL` and indistinguishable. Without a distinct
rejected state, a non-compilable function would be re-attempted (and
re-`CacheClearU`'d) on *every* call forever — a worse regression than the
boot cost.

### Remove the eager call sites

Drop (or gate behind a debug/eager build flag) the three eager
`cl_jit_compile()` calls: `fasl.c:1925`, `compiler.c:922`,
`compiler.c:4094`. After this nothing compiles at load.

### VM dispatch (`src/core/vm.c`, around 1948)

When `native_code == NULL` and `jit_state == JIT_UNTRIED`, increment
`call_count`; on crossing the threshold attempt one compile:

```c
if (callee_bc->native_code) {
    /* existing native fast path (vm.c:1948) */
} else if (callee_bc->jit_state == JIT_UNTRIED &&
           ++callee_bc->call_count >= CL_JIT_HOT_THRESHOLD) {
    cl_jit_try_compile(callee_bc);   /* sets state COMPILED or REJECTED */
}
```

The increment+compare is in the hottest interpreter path, so it must stay
minimal and only run while `JIT_UNTRIED`. Counting happens at the
non-tail dispatch site only; tail calls already never take the native
path (`!is_tail` gate), so tail-only functions stay interpreted with no
regression.

Threshold: small, **4–16** (start at 8, tune). Lower = closer to eager;
higher = longer warm-up but cheaper boot.

### Thread safety (the real wrinkle)

The VM is multi-threaded (MP package, per-thread VM, stop-the-world GC),
and `CL_Bytecode` is shared across threads. Eager-at-load sidesteps races
entirely — compilation happens once before any worker runs. Lazy
compile-on-hot means two threads can race to compile the same bytecode.

`cl_jit_try_compile` must:

1. CAS `jit_state` `JIT_UNTRIED → JIT_COMPILING` (`__sync_*`, already used
   on Amiga per the threading model). Only the winner compiles; losers
   skip and keep interpreting until `native_code` is published.
2. Compile into a local buffer, then **publish `native_code` last** (it is
   the field the reader gates on at `vm.c:1948`); set `native_len` before
   the pointer, with a write barrier, so a half-written state is never
   observed.
3. Set `jit_state` to `JIT_COMPILED` (on success) or `JIT_REJECTED` (walker
   bailed → free the buffer, leave `native_code == NULL`).

`cl_jit_active_threads` (`jit.c:3261`) guards native *execution* vs.
stop-the-world GC and is unrelated to this compilation race.

## What this does not change

- **No `CL_FASL_VERSION` bump** — new fields are runtime-only.
- **No GC interaction** — `cl_jit_compile` allocates only system memory;
  `bc` cannot move during compile.
- **No new free path** — `native_code` was never swept before and still
  is not; lifecycle is unchanged.
- The `CacheClearU()` per function (`jit.c:2922`) is still emitted, just
  called far fewer times. See "Related cheap fix" below.

## Behavioural change to be aware of

Warm-up: the first `CL_JIT_HOT_THRESHOLD` calls of every function
interpret. Correct for boot (functions called a few times never compile).
For a tight benchmark, peak speed now arrives after warm-up rather than
from the first call.

## Host testability

On host, `cl_jit_compile` is an inline no-op (`jit.h:82`) — `native_code`
never gets set. The counter would climb forever unless `JIT_REJECTED`
fires after the one attempt, which it does. This makes the
threshold/reject state machine **unit-testable on host** without m68k:
drive a function past the threshold, assert `jit_state == JIT_REJECTED`
and that no further compile attempts occur.

## Touch list

- `src/core/types.h` — two fields on `CL_Bytecode`.
- bytecode allocator — initialize the two fields.
- `src/core/vm.c` — lazy counter + try-compile at dispatch (~15 lines).
- `src/core/compiler.c` (×2), `src/core/fasl.c` — remove eager calls.
- `src/jit/jit.c` — `cl_jit_try_compile` wrapper with the CAS publish
  protocol.
- `tests/test_*.c` — host test for the state machine (threshold →
  attempt → `REJECTED`, no re-attempt).
- `tests/amiga/run-tests.lisp` — a hot function ends up native after
  enough calls (introspection hook exists: `builtins.c:870` / `965`).
- `specs/native-backend.md` — cross-reference; CLAUDE.md boot-time note.

## Effort estimate

- **Single-threaded-correct version:** ~half a day. Straightforward,
  testable on host.
- **Thread-safe version (required to ship, given the MP package):** ~1 day
  including contention testing. Risk is concentrated almost entirely in
  the CAS publish protocol, not in volume of code.

## Related cheap fix (independent, do regardless)

Batch the cache flush: drop the per-function `CacheClearU()` and flush
once after the boot FASL load completes, or switch to per-range
`CacheClearE` (V37+) on just the emitted buffer (`platform_amiga.c:1417`).
~20 minutes; may recover a large fraction of the boot cost on 040/060 on
its own. **Measure first** to apportion the 3 s between compilation volume
and the per-function full-cache flush before committing to the larger
lazy-JIT change.
