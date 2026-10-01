# Direct native-to-native calls (m68k JIT)

Status: **in progress** (2026-10-01): phases 1 to 4 done.  Follows the open lever in
`specs/native-backend.md` §"Status (2026-09-16, direct call dispatch from
JIT'd code)": *direct JSR to a native callee from the call site (no helper
at all)*.  The idea is borrowed from Evergreen CL's T1 baseline JIT
(github.com/atgreen/evergreen, `crates/egcl/src/cli/bytecode.rs`,
`DIRECT_CALL_GEN`).  It bakes direct native calls and guards all of them with
**one global generation word** that every invalidating event bumps.

## Problem

Every call a JIT'd function makes goes through a C helper, even when the
callee is native code that the call fits.  On that path,
`cl_jit_runtime_call[_global]` → `jit_dispatch` (`src/jit/runtime.c`) does
the following, every time:

1. classifies the callee (header type, closure → bytecode, `native_code`,
   arity fit);
2. polls `thr->gc_requested` / `thr->interrupt_pending`, and re-resolves
   `bc` afterwards;
3. probes the C stack with `cl_check_c_stack`, which calls
   `platform_stack_headroom` → `FindTask` + bounds on AmigaOS;
4. reverse-copies the arguments onto the VM stack and bumps `vm.sp`;
5. enters `cl_jit_invoke`, which:
   - saves and restores `jit_depth`, `jit_stack_top` and
     `jit_current_nargs`, plus `cl_jit_active_threads` on the outermost
     entry;
   - checks for a shadow frame;
   - switches on arity;
   - re-pushes the arguments in C order for the typed function-pointer
     call.

That is three C frames and two argument copies per call.  Measured
(`trunk/bench-jit-call.lisp`, 200k iterations, µs per iteration including
the 0.4 µs loop):

| Row                   | Vampire V4 bc | Vampire V4 JIT | FS-UAE 040 bc | FS-UAE 040 JIT |
|-----------------------|--------------:|---------------:|--------------:|---------------:|
| call native leaf      | 11.8 | 7.0 | 12.4 | 5.1 |
| FUNCALL native leaf   | 12.9 | 6.8 | 13.1 | 4.9 |
| decode-key mix        | 67.2 | 50.0 | 59.3 | 34.2 |

About 6.5 µs of the native-leaf row on real hardware is the call path
itself.  The callee is a one-instruction leaf.

## Goal

When a call site in native code calls a callee that **has native code with
the positional ABI and an arity the call fits**, the call site JSRs straight
into the callee's code.  The hit path has:
- no C helper;
- no argument copy;
- no `FindTask`;
- no write to shared memory.

Every other case keeps today's helper path, unchanged.  The goal is that
**no observable behavior changes**: values, multiple values, errors and
their messages, TRACE, backtraces with shadow frames, GC safety, and
redefinition semantics.

Target: the native-leaf row at **≤ 2 µs net on the Vampire** (from ~6.5).
That is an estimate to be confirmed, not a promise.

Out of scope for this spec: `&key`, `&optional` and `&rest` callees (they
keep the helper, see §"Later"), generic functions, FFI stubs, builtins, and
any JIT on PPC/MorphOS or host (none exists).

## Design

### 1. One generation word

```c
/* src/core/mem.c (core, so the host builds and tests it too) */
volatile uint32_t cl_call_gen = 1;     /* never 0: 0 = "empty site" */
void cl_call_gen_bump(const char *why); /* ++, skips 0 on wrap; DEBUG_JIT logs `why` */
```

A filled call site is valid **only while its recorded generation equals
`cl_call_gen`**.  Anything that could make a cached `(func, entry)` pair
wrong bumps it:

| Event | Where | Why it invalidates |
|---|---|---|
| Any function-cell write | the 15 `->function =` sites, funnelled through one setter (below) | DEFUN, `(setf fdefinition)`, FMAKUNBOUND, FFI/Amiga stub installs, package copy |
| **Every garbage collection** (start of `cl_gc`, the compactor, the STW entry) | `mem.c` | The site holds a raw `CL_Obj` that is **not** a GC root and **not** relocated. Bumping on every collection rules out ABA (an object freed and its offset reused) and stale offsets after compaction. Sites need no GC metadata at all. |
| `native_code` freed or replaced | `mem.c:4086` (bytecode swept), `jit.c:3834`/`:4529` (replace), `image.c:1438` (restore) | the cached entry points into freed memory |
| GC requested of a thread (STW) | `thread.c:372`, `:402` | sites must miss so the helper path polls `gc_requested` (see §5) |
| Interrupt posted | `builtins_thread.c:1889`, `:1938`, `:1960` | same, for `interrupt_pending` |
| TRACE / UNTRACE, `cl_trace_count` reset | `builtins.c:1268`, `:1298`, `:1346` | traced calls must go through the trampoline |
| Shadow frames toggled, direct calls toggled, JIT toggled | `cl_jit_set_shadow_frames`, `%jit-set-direct-calls`, `cl_jit_set_active` | the fill rules below change |

**The funnel.**  Add `cl_symbol_set_function(CL_Obj sym, CL_Obj fn)`
(FMAKUNBOUND passes `CL_UNBOUND`).  Every one of today's raw writes is
rewritten to go through it:
`builtins.c:164`, `builtins_amiga.c:836`, `builtins_ffi.c:1267/1294/1303`,
`bindtab.c:949/1069/1097`, `vm.c:2854`, `mem.c:2075/2248`,
`builtins_package.c:978`, `builtins_mutation.c:139/277/284`,
`jit/runtime.c:244`.

`mem.c:2075` is symbol construction and `builtins_package.c:978` fills a
fresh COPY-SYMBOL, where nothing can be cached yet; `mem.c:2248` is a
restart's slot, not a symbol's.  They stay raw, each marked with a
`symfn-raw:` comment the lint accepts.

As built (phase 3): the collection bump sits in `gc_stop_world_timed`,
which every collector (sweep, compaction, gengc minor) enters, right after
the world is stopped.  The image restore needs no bump: it runs before any
native code exists, so no site can hold anything.  Freeing native code in
the sweep is covered by that collection's bump; the JIT's own
replace paths (`jit_compile_impl`, `cl_jit_emit_stub`) bump themselves.

A shell lint in the style of `tests/test_gc_arg_order.sh`,
`tests/test_symfn_funnel.sh`, fails `make test` on any new raw
`->function =` outside the setter and an explicit allowlist.  **A missed
write site is a wrong-function-called bug**, so the lint is not optional.

Bumps are rare (definitions at load time, collections, STW) and the hit path
only reads the word.  That keeps the CLAUDE.md hot-path rule: nothing on the
per-call path writes shared memory.

### 2. The call-site cell

Each `OP_CALL`, `OP_CALL_GLOBAL` (including the fused `LOAD_CALL_GLOBAL` /
`GLOAD_CALL_GLOBAL` heads) and `OP_TAILCALL_GLOBAL` fallback site gets a
12-byte cell:

```c
typedef struct {        /* lives in the function's native code buffer */
    uint32_t gen;       /* cl_call_gen at fill time; 0 = never filled  */
    CL_Obj   func;      /* the callee object (bytecode or closure)      */
    void    *entry;     /* its bc->native_code                          */
} CL_JitCallSite;
```

The cells are appended after the function's code, 4-byte aligned, and
reached PC-relative: `lea (d16,pc),a1`.  The walker records each site's
`lea` and patches its displacement at finalize, the same way as
`BranchPatch`.  The cells are **data**: native code only reads them (data
cache), and only C writes them, on a miss.  No I-cache flush is involved,
and the code buffer is ordinary `platform_alloc` memory, so it is writable.
`native_relocs` must not cover the cells: the generation bump makes
relocation unnecessary.

Cost: 12 bytes per site plus the inline guard (§3), roughly 40 bytes per call
site.  Since the lazy JIT, boot plus the editor compiles to ~35 KB of native
code, so this is small.

### 3. The hit path

`OP_CALL_GLOBAL sym, n`, after `cache_flush`, with the n arguments on the
m68k operand stack (`(a7)` = last argument):

```
        lea     site(pc),a1
        move.l  (a1)+,d0            ; site.gen
        move.l  (a1)+,d1            ; site.func
        movea.l (a1),a0             ; site.entry
        cmp.l   cl_call_gen,d0      ; abs.l
        bne.w   .miss
        cmpa.l  JIT_FLOOR(a3),a7    ; per-thread C-stack floor, see §4
        bls.w   .miss
        move.l  d1,-(a7)            ; push site.func  (callee's 8(a6))
        jsr     (a0)
        lea     4+4n(a7),a7         ; drop func + args
        bra.w   .done
.miss:  move.l  a7,a0               ; operand_top
        pea     -8(a1)              ; the cell
        move.l  #sym,-(a7)
        move.l  #n,-(a7)
        move.l  a0,-(a7)
        jsr     cl_jit_runtime_call_global_site
        lea     16+4n(a7),a7
.done:  ; D0 -> stack cache, as today
```

That is 12 instructions on a hit, all reads.  All three words are read
**before** gen is compared (as built; the first draft compared first): see
§5 for why that order is what keeps a preempted reader from pairing one
fill's `func` with another's `entry`.  `OP_CALL` is the same, plus
`cmp.l 4n(a7),d1` against the function slot under the arguments.  It falls
through to `.miss` on a mismatch, so FUNCALL of a different function at the
same site is just a miss.  As built, `OP_TAILCALL`'s fallback goes through a
site too, so all four call opcodes share one emitter (`emit_call_site`) and
the old `cl_jit_runtime_call[_global]` helpers are gone.

`OP_TAILCALL_GLOBAL`'s fallback is "call, then UNLK/RTS", as it is today;
only the call part changes.  The self-tail-call `bra` stays first and is
not affected.

Multiple values need no extra work: the callee sets `mv_count` /
`mv_values` exactly as it does under `cl_jit_invoke`, and the caller reads
them the same way.

### 4. Argument order and the thread register (prerequisites)

**Argument order.**  The positional native ABI today is the C order:
`native(func, a0, …, an-1)`, so parameter i is at `12+4i(a6)`.  The
operand stack holds the arguments the other way round: `a(n-1)` is at the
lowest address.  A direct JSR would therefore have to re-copy n words.
Instead, **flip the native positional ABI to operand-stack order**:
parameter i at `12 + 4*(arity-1-i)(a6)`.  A call site then pushes only
`func` and JSRs; the arguments are already in place.

Places that change:
- `slot_disp` for the non-kw arguments;
- the two self-TCO argument rewrites (`jit.c` ~2301, ~2454), which become
  index-identity copies;
- `matches_passthrough`'s displacement (only for arity ≥ 2);
- `cl_jit_invoke`, which passes the arguments reversed.

`cl_jit_invoke` is the only C caller of `native_code` (verified: there is
no other `native_code)(` call).  The `&key` ABI is untouched.

**Thread register.**  The C-stack check must stay on the direct path:
native frames nest on the m68k stack, and runaway native recursion must
still hit the clean "C stack nearly exhausted" error.  The test
`jit-direct-…` "runaway native recursion reaching the C-stack guard" pins
this.  `FindTask` per call is what we are removing, so:

- **A3 holds `CL_Thread *` inside native code.**  The walker never uses
  A2–A5, and m68k SysV makes A3 callee-saved, so every C helper
  preserves it.
- `CL_Thread` gains `char *jit_c_floor`.  The outermost `cl_jit_invoke`
  (`prev_depth == 0`) computes it once from `platform_stack_headroom()`:
  floor = current SP − headroom + 16 KB margin, the same margin
  `cl_check_c_stack` uses.  If headroom is unknown (-1), the floor is NULL
  and the compare never fires; the `cl_check_c_stack` budget check on the
  miss path still applies.
- A small asm entry, `src/jit/jit_enter_m68k.s`, replaces the typed
  arity switch in `cl_jit_invoke`:
  `cl_jit_enter(entry, thread, func, argv, nargs)`.  It:
  - saves A3 and loads `thread`;
  - pushes `argv[0..n-1]` in operand-stack order, then `func`;
  - `jsr (entry)`, pops, restores A3, and returns D0.

  The keyword ABI enters through it as well, with argv = `{args, nargs,
  bc}`, so A3 is valid in every native function, keyword or not.
  `cl_jit_restore_depth` clears the floor with `jit_stack_top` when an
  unwind leaves native code entirely.  `(clamiga::%jit-c-floor)` returns
  `(floor sp)` from inside native code (NIL outside), for the tests.

  The precedents are `stack_swap_m68k.s` and `cpu_store_probe_m68k.s`.  A
  side effect: the `CL_JIT_PASSTHROUGH_MAX_ARITY` cap (6) no longer comes
  from the invoke switch.  Lifting it is a separate change.

**Non-local exits.**  A longjmp lands in a C frame whose own setjmp saved its
own A3, and that frame restores the native caller's A3 on return.  This is
plain C callee-saved semantics, so nothing new is needed.  A Lisp callback
on a foreign task (MUI hooks on `input.device`) enters through
`cl_jit_invoke` → `cl_jit_enter`, which loads that thread's pointer.

### 5. The miss path and filling

`cl_jit_runtime_call_global_site(operand_top, nargs, sym, site)` and
`cl_jit_runtime_call_site(operand_top, nargs, site)` do this:

1. Poll `gc_requested` / `interrupt_pending` and handle them, as
   `jit_dispatch` does today.
2. Read `g = cl_call_gen` **before** resolving the callee.  If a racing
   bump lands after this, the site records the old `g` and misses again.
   That is correct, only slower.
3. Resolve the callee and apply the fill rule.  Fill only if **all** hold:
   - direct calls enabled, `jit_shadow_frames == 0`,
     `thr->trace_count == 0`;
   - type is `TYPE_BYTECODE` or `TYPE_CLOSURE`, with
     `bc->native_code != NULL`;
   - positional ABI: `bc->flags == 0`, `n_optional == 0`, no `&rest`,
     `arity == nargs` (exactly the `fits` rule of `jit_dispatch`'s native
     arm, restricted to non-kw);
   - the C-stack check `cl_check_c_stack("a native call")` passes (it
     signals otherwise, as today).
4. If it fills, write `func` and `entry` first and `gen = g` last.  The
   m68k JIT is single-CPU and has no reordering, so a preempted half-fill
   leaves `gen` stale, which reads as a miss.

   As built, three more rules close the races a preemptive scheduler
   leaves (runtime.c, above `jit_site_try_fill`):
   - fillers serialize through a try-lock (`platform_atomic_cas`); a
     contended fill is skipped, and the next miss retries;
   - a site is rewritten only while `site->gen != cl_call_gen`: a live
     site is never touched, and since `cl_call_gen` only grows, a reader
     that read the old gen before the rewrite compares it after its three
     reads and misses;
   - every input of the fill rule changes **before** its bump.  Replacing
     native code therefore unhooks `bc->native_code`, bumps, and only then
     frees the old code (`jit_compile_impl`, `cl_jit_emit_stub`).
   - TRACE is process-wide for the fill rule: a new counter,
     `cl_traced_function_count`, refuses fills while any symbol is traced
     (`cl_trace_count` is per thread, so a site filled by an untracing
     thread would otherwise let a tracing one skip the trace).
5. Dispatch this call through `jit_dispatch`, unchanged.  The next call
   through the site hits.

Callees that are still counting toward the lazy-JIT threshold do not fill.
They go through the stub frame, where `OP_CALL` counts them, exactly as
today.  Once compiled they fill on the next miss.  No bump is needed when
`native_code` goes from NULL to code, because nothing could have cached the
NULL.

Safepoint latency is unchanged.  A requester sets the target's flag, then
bumps.  The target's next call site misses and polls.  Today the target
polls at its next call as well.  Call-free native loops do not poll, today
or after this change.  (As built, the miss path polls before every
dispatch, so a call from native code to a builtin now polls too, as the
interpreter's OP_CALL does.)

**Ctrl-C needs no bump.**  The break poll (`VM_POLL_BREAK`) lives only in
`cl_vm_run`; neither `jit_dispatch` nor native code polls it.  A
native-to-native call chain did not see Ctrl-C before this change either,
and every path back into the interpreter (an interpreted callee, a
builtin that applies Lisp) still runs it.

**A nested entry on a foreign stack.**  `jit_c_floor` is measured on the
outermost entry's stack.  A nested `cl_jit_invoke` whose SP lies outside
`(jit_c_floor, jit_stack_top]` is a callback on another task's stack (or
already below the floor), so it parks the floor at the top of the address
space until it returns: every site misses there, and the miss path's
`cl_check_c_stack` measures the real stack.

### 6. What the direct path skips, and why that is safe

| Skipped (vs. `cl_jit_invoke`) | Why it is safe |
|---|---|
| `jit_depth` / `jit_stack_top` / `cl_jit_active_threads` | The caller is native, so `jit_depth > 0` already.  The scan window runs from the outermost entry's `jit_stack_top` down to the current SP, which covers nested direct frames.  Error/NLX unwind restores depth from snapshots taken in C, which direct calls never enter. |
| `jit_current_nargs` | Only `OP_ARGC` reads it, and the walker rejects `&optional`, the only shape that emits it.  `walker_compile` gets an assert-style comment at its `&optional` gate; see §"Later". |
| VM-stack argument copy | The positional native ABI reads its arguments from the m68k stack.  Arguments are conservatively scanned and pinned there, the same as every value in a native frame today. |
| Shadow `CL_Frame` | Fill refuses while shadow frames are on; toggling bumps. |
| `jit_invoke_count++` | Diagnostic only.  Test `jit-direct-native-callee-invokes` counts it and is rewritten against the new site statistics. |

### 7. Diagnostics

- `(clamiga::%jit-direct-call-stats)` → plist `:fills :misses :refused-trace
  :refused-shadow :refused-abi :refused-not-native :gen :enabled`.  The counters are
  bumped **only on the miss path** (C), never on a hit.
- `(clamiga::%jit-set-direct-calls nil|t)` and `CLAMIGA_JIT_DIRECT=0`: the
  kill switch for A/B runs and bisection.  It bumps, and fill then refuses.
  This mirrors `%jit-set-active`.
- `DEBUG_JIT` logs every bump with its `why` string, so an unexpected
  bump storm (a site that never stays filled) is visible.
- `%jit-disassemble` decodes the new instruction forms (`lea (d16,pc)`,
  `cmp.l abs.l`, `cmpa.l d16(a3)`, `jsr (a0)`) and prints each site's cell
  as `site #k: gen=… func=… entry=…`.

## Phases

Each phase lands on its own commit.  `make test` and
`make -f Makefile.cross test-amiga` must be green before the next one
starts.

1. **ABI flip only** (§4, argument order).  There is no behavior change, so
   the whole Amiga suite and `test-jit.lisp` are the check.  Re-run
   `bench-jit-call.lisp` to confirm it is neutral.
2. **`cl_jit_enter` + A3 + `jit_c_floor`.**  `cl_jit_invoke` uses the
   trampoline; nothing reads A3 yet.  Gate: the suite, plus the runaway
   recursion test, plus the `make test-memleak` scenario if the floor or
   the trampoline holds anything off-heap (it should not).
3. **`cl_call_gen` + the setter funnel + the lint**, in core.  Host-testable;
   see §Tests.
4. **Sites, cells, fill, hit path** behind `%jit-set-direct-calls`,
   default **on**.
5. **Measure and document.**  `bench-jit-call.lisp` on FS-UAE 040 and the
   Vampire, the decode-key mix, and the Clamacs per-key path.  Add a
   "Status" section to `specs/native-backend.md`, update `docs/benchmarks.md`
   and the README JIT paragraph (one user-facing sentence plus a
   benchmark-table refresh, no internals).

## Tests

**Host** (core parts only: the generation word and the funnel):
- `tests/test_call_gen.c`: every bump source moves `cl_call_gen`.  This
  covers DEFUN, `(setf fdefinition)`, FMAKUNBOUND, `(setf symbol-function)`,
  `cl_gc()`, TRACE/UNTRACE, a stub install, and wraparound skipping 0.  It
  is exposed to Lisp as `clamiga::%call-gen` so the same checks run from
  the shell tests.
- `tests/test_symfn_funnel.sh`: the lint described in §1.
- gc-stress: `make test-gc-stress` runs the `%call-gen` checks with
  compaction forced on every allocation (every collection must bump).

**Amiga** (`tests/amiga/test-jit.lisp`, new `jit-site-*` checks, eager JIT
bound as the existing `jit-direct-*` block does):
- **hit:** a native caller calls a native leaf 1000 times.  The value is
  right, and `:fills` grows by exactly 1 and `:misses` by 1.
- **redefinition:** redefine the callee between two calls of the same
  native caller.  The second call returns the new definition's value.
  Repeat with `(setf fdefinition)` and with FMAKUNBOUND (the second call
  signals UNDEFINED-FUNCTION with today's message).
- **FUNCALL:** an `OP_CALL` site called alternately with two different
  native closures.  Both values are right (a guard mismatch is a miss).
- **GC between calls:** a caller allocates enough to force collections and
  compaction between calls through the same site.  The value is right, and
  `:fills` grows once per collection, not per call.  Also run under
  `CLAMIGA_GC_STRESS=1` in the Amiga gc-stress leg.  (As built there is
  no Amiga gc-stress leg; the check is a `stress-check`, and the host
  gc-stress run covers the `%call-gen` side.)
- **ABA:** create a closure, call it through a site, drop it, collect,
  allocate a different closure of the same size, call it through the same
  site.  The result is the new closure's.
- **TRACE:** trace the callee after the site has filled.  The trace output
  appears; after untrace the site fills again.
- **shadow frames:** with them on, the callee appears in `EXT:BACKTRACE`.
- **multiple values** through a filled site, plus `(values)` → 0 values.
- **arity mismatch** at a filled site's callee after redefinition with a
  different arity.  The error message is identical to today's `OP_CALL`
  diagnostic.
- **runaway recursion** through direct calls reaches the C-stack guard
  cleanly, at 64K and at 128K stack.
- **interrupt:** `mp:interrupt-thread` on a thread looping on direct calls
  is delivered.
- **STW GC:** a second thread forcing collections while the first loops on
  direct calls.  No hang, and values are right.
- **ABI flip:** arity 0–6 callees reached from the VM (`cl_jit_invoke`),
  from native code (direct) and through the self-TCO path, all returning
  every argument in order.  This is the phase 1 test and must exist before
  the flip.
- **kill switch:** with `%jit-set-direct-calls nil`, `:fills` stays 0 and
  every value is identical.

## Risks

- **A missed function-cell write** calls a stale function.  Mitigation:
  the funnel lint, and the redefinition tests through every public path.
- **A3 clobbered** by future walker code or a hand-written helper.
  Mitigation: a comment block next to the register table in `jit.c`, and a
  `DEBUG_JIT` check in a few helpers that A3 equals the current thread.
- **Bump storms**, for example a program collecting very often on a tiny
  heap.  The cost per bump is one refill per active site, which is roughly
  today's per-call cost once.  `%jit-direct-call-stats` shows it, and the
  kill switch bounds it.
- **Cache-layout effects** on the Vampire (see the `jit_dispatch_apply`
  note in `specs/native-backend.md`).  Use interleaved A/B pairs with the
  kill switch, not before/after binaries.

## Later (not in this spec)

- **`&optional` prologues** (the other open lever).  When the walker takes
  them, a direct-called callee needs `nargs`.  Pass it in D1 (caller-saved,
  free at entry) instead of `jit_current_nargs`, and let fill accept
  `nargs` in `[arity, arity+n_optional]`.
- **`&key` callees**: the kw ABI wants an `args` pointer in natural order,
  so it needs either a site-side reverse or a reversed-args variant of
  `cl_jit_runtime_kw_prologue`.
- **Lifting the 6-argument cap** once `cl_jit_enter` takes an argument
  vector.
- **Using A3 elsewhere**: inline `mv_count` resets and inline TLV-free
  `GLOAD` (`thr->tlv_entry_count == 0`), now that the thread pointer is a
  register.
