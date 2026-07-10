# Benchmark Results

Point-in-time performance measurements for future reference. Newest entries
first. Each entry records the commit, environment, exact reproduction
command, and results, so later runs can be compared like-for-like.

Related: [specs/performance.md](../specs/performance.md) is the optimization
*plan*; this file is the *measured results* log.

---

## 2026-07-10 — spec 1.3: optimize support (const folding + dead branches + check elision)

**Commit**: follows `e388fe2`.

`(declaim (optimize ...))` / body `(declare (optimize ...))` now drive the
compiler (lexically scoped per CLHS 3.3.4).  At `speed >= 1` (the default),
calls to pure fixnum builtins with constant arguments fold to a single
`CONST` (`(+ 1 2 3)`: 20 bytes / 4 constants → 9 bytes / 1 constant, tested
via disassembly), and constant `IF` tests compile only the live branch.  At
`(safety 0)`, destructuring-bind arity guards and the `THE` type assert are
elided.  See specs/performance.md § 1.3 for the design and file list.

**Environment**: Apple M3 Ultra, macOS 26.5.2, `make host`, warm FASL cache,
`--heap 64M`, cwd = repo root.  Baseline = the 2026-07-05 bench-opt entry
below (same flags; note the 07-05 numbers predate the CLOS slot-access
specs, so only the rows below are attributable to 1.3).

**Reproduce**: `echo '(quit)' | ./build/host/clamiga --heap 64M --load trunk/bench-opt.lisp`

| Benchmark | before (07-05) | after | delta |
|---|---|---|---|
| opt.const-fold | 39 ms | **20 ms** | 1.95× |
| opt.dead-branch | 49 ms | **35 ms** | 1.4× |
| vm.fixnum-loop | 72 ms | **61 ms** | 1.18× |
| vm.local-shuffle | 63 ms | **51 ms** | 1.24× |
| vm.call-return | 63 ms | 60 ms | ~1.05× |
| safety1.svref-loop | 48 ms | 44 ms | ~1.09× |

All 30 bench-opt checks pass (`fails=0`).  The safety0.* rows are unchanged
because arity/bounds checks live in the VM's call/aref handlers, not at the
emit site — elision for those is deferred to the 1.8-era bytecode work.

---

## 2026-07-10 — writer-GF inline cache: (setf (x obj) v) fast dispatch (host)

**Commit**: follows `25e1fd9`.

The reader fast path left writers untouched: `(setf (x obj) v)` was a plain
2-arg GF call through the full discriminator (EMF inline cache + effective
method funcall + `%ACCESSOR-WRITER-BODY`'s per-store slot resolution).  This
entry adds the mirror machinery: a GF whose whole method set is
DEFCLASS-generated writer methods for one slot is promoted
(`CLAMIGA:*WRITER-GFS*`), and the same probe sites that answer reader calls
(`OP_CALL`, `cl_vm_apply` — the m68k-JIT funnel — `bi_funcall`, `bi_apply`,
the VM's inline `OP_APPLY`) store straight through the cached slot index on
a 2-arg call.  Writer IC entries reuse the reader's `(TYPE-NAME . FIXNUM)`
cons shape with the index encoded negative (`(- -1 idx)`), so the two probes
can never answer from each other's caches (a 1-arg call to a promoted writer
GF still signals its arity error) and every existing slot-8 invalidation
site covers writers for free.

**Environment**: macOS arm64, host bytecode VM, best-of-3.

**Reproduce**: 8 unrolled stores per iteration, 250k iterations, instance in
a local; the poly row alternates two classes whose slot sits at different
indices.

| ns per store | before | after | speedup |
|---|---|---|---|
| `(setf (x obj) v)`, 1 class | 144.5 | **16.0** | 9.0× |
| 2 classes alternating | 390.0 | **17.0** | 23× |
| via `FUNCALL #'(setf x)` | 618.5 | **17.0** | 36× |
| accessor read (control) | 13.5 | 13.5 | — |

Writes now cost the same as promoted reads — the symmetric result the reader
entries below established for the read side.  Real workloads write about as
often as they read (sento actors mutate state constantly), so this closes
the last accessor-shaped gap on the hot path.

Tracked by `clos.accessor-write` in `trunk/bench-opt.lisp` (7 ms, identical
to `clos.accessor`, zero allocation).

Verified: host `make test` + `test_clos_reader` 28/28 (11 new writer tests
incl. arity-error-not-slot-read, `:after`-method demotion, redefinition,
`SET-FUNCALLABLE-INSTANCE-FUNCTION`, latch demotion), gc-stress 391/391
(new writer-GF compact-every-alloc case), FS-UAE suite.

---

## 2026-07-10 — reader-GF inline cache reaches JIT'd callers + call trampolines (Amiga + host)

**Commit**: follows the SLOT-VALUE entry below.

The m68k JIT never executes the bytecode `OP_CALL`, so on the platform this
project exists for, every reader-GF access still unwrapped to the Lisp
discriminating function — the caveat noted in the two entries below.  No
native codegen was needed to close it: every JIT'd call funnels through
`cl_jit_runtime_call → cl_vm_apply`, and `cl_gf_reader_ic_probe` reads
everything it needs (GF slot 8, receiver `type_desc`) fresh at runtime, so
the probe now runs at the top of `cl_vm_apply` — before the GF unwrap
discards the identity the cache is keyed on.  No baked immediates, no
`native_relocs` involvement.  The same probe-before-unwrap was added to the
other C trampolines that lost the GF identity early: `bi_funcall`,
`bi_apply`, the VM's inline `OP_APPLY`, and the sequence helpers' `call_1`.

**Environment (Amiga)**: FS-UAE A4000/68040 + UAE JIT
(`verify/realamiga/verify.fs-uae`), `--heap 8M`, m68k JIT active.  Relative
deltas are what matter; absolute times are emulator-specific.
8-unrolled reader loop inside a JIT'd DEFUN, 240k calls per timing.

| µs per call (JIT'd caller) | before | after | speedup |
|---|---|---|---|
| reader, direct call | 26.17 | **5.83** | 4.5× |
| reader via FUNCALL | 25.75 | **5.67** | 4.5× |
| reader via APPLY | 24.00 | **3.33** | 7.2× |

**Host**: compiled `(apply #'reader (list o))` (inline `OP_APPLY`) halved,
77 → 40 ns/call (the remainder is the harness's per-call arglist consing).
Compiled `(funcall #'reader o)` emits plain `OP_CALL` and was already at the
28.5 ns tier.  `clos.accessor` / `clos.accessor-poly` in bench-opt unchanged
at 7 ms — the interpreter fast path is untouched.

Not covered (follow-up): MAPCAR-family and `:key`/`:test` callers unwrap the
GF at designator-coercion time (`cl_coerce_funcdesig`), so their per-element
calls still take the Lisp discriminator tier (~107 ns/elt host).  Routing
them through the probe means auditing the 28 `cl_coerce_funcdesig` call
sites' "returns one of three flat types" contract.

Verified: host `make test` + `test_clos_reader` 17/17, gc-stress 384/384,
FS-UAE suite 3674/3674 (new checks assert the IC spine stays EQ across
JIT'd-caller/funcall/apply calls, and that demotion by `:around` methods,
`SET-FUNCALLABLE-INSTANCE-FUNCTION`, and the slot-access-protocol latch
disarms every trampoline probe).

---

## 2026-07-10 — SLOT-VALUE compile-time inline + (type, slot) pair index (host)

**Commit**: follows `e0d1ca0`.

`SLOT-VALUE` and `(SETF (SLOT-VALUE ...))` are ordinary DEFUNs, so every
access paid a full Lisp call frame before reaching the fused
`%STRUCT-SLOT-VALUE`/`%STRUCT-SLOT-STORE` builtin — the frame, not the slot
lookup, was the bulk of the cost.  Three changes, no new opcode, no FASL
format change:

1. **Compiler macros** on `SLOT-VALUE` and `%SET-SLOT-VALUE` (clos.lisp)
   splice the DEFUN bodies' fast-path test into every compiled call site —
   the same treatment DEFCLASS accessors and `WITH-SLOTS` already got.
2. **`compile_setf` routes defsetf updaters through `compile_call`**
   (compiler.c) instead of hand-emitting `OP_FLOAD`/`OP_CALL`, so the
   `%SET-SLOT-VALUE` macro can fire at all (and any defsetf setter now
   benefits from compiler macros / builtin opcodes).
3. **A `(type-name, slot-name) → index` pair table** built alongside the
   struct-registry hash index (builtins_struct.c), same lock and same
   dirty/disabled lifecycle, replaces `struct_slot_resolve`'s linear
   specs-list walk — O(1) for any slot of any width class.

**Environment**: macOS arm64, host bytecode VM, best-of-3.

**Reproduce**: 8 unrolled accesses per iteration, 250k iterations, instance
in a local; 12-slot class for the depth rows.

| ns per access | before | after |
|---|---|---|
| `(slot-value p 'x)` read, slot 0 of 2 | 77.5 | 56.0 |
| `(setf (slot-value p 'x) v)` write | 72.5 | 51.0 |
| read, slot 0 of 12 | 59.0¹ | 56.0 |
| read, slot 11 of 12 | 68.0¹ | 56.0 |

¹ measured with the inline already applied, isolating the pair index: the
walk cost ~0.75 ns per slot position; now flat.

Tracked by `clos.slot-value` (18 → 15 ms; the loop harness dilutes the
per-access delta) and the new `clos.slot-value-deep` (15 ms, equal to the
shallow case) in `trunk/bench-opt.lisp`; `struct.slot-value` 19 → 15 ms.
The reader-GF accessor path (17.5 ns) remains faster — it answers in
`OP_CALL` with no call at all — but the "slot-value is 4× a reader" inversion
is now ~2×, and `FUNCALL`/`APPLY` of `#'SLOT-VALUE` still routes through the
DEFUN unchanged.

---

## 2026-07-10 — polymorphic reader-GF inline cache (host)

**Commit**: follows `58f524c`.

The reader IC (GF slot 8) was a single `(TYPE-NAME . SLOT-INDEX)` entry, so a
call site alternating between receiver classes missed on every call — and a
miss is the full slow dispatch plus `%COMPUTE-APPLICABLE-METHODS` plus an IC
rewrite.  Now the IC is a list of up to 4 entries, most-recently-missed
first; `cl_gf_reader_ic_probe` walks it with one word-compare per entry, and
the miss path carries surviving entries over instead of discarding them.

**Environment**: macOS arm64, host bytecode VM, best-of-3.

**Reproduce**: 8 unrolled reader calls per iteration on one GF; receivers
cycle through 1, 2 or 4 classes (subclasses of one base, each with its own
`type_desc`), 250k iterations.

| ns per slot access | before | after |
|---|---|---|
| 1 class (mono) | 16.5 | 17.5 |
| 2 classes alternating | ~1340 | 17.5 |
| 4 classes alternating | ~1340 | 18.5 |

The ~80x alternation penalty is gone; the monomorphic path pays one extra
spine dereference (~1 ns).  Beyond 4 receiver classes the cap evicts the
oldest entry and cycling receivers miss again — bounded, correct, and no
worse than the old behaviour.

Tracked by `clos.accessor-poly` in `trunk/bench-opt.lisp` (7 ms, identical to
the monomorphic `clos.accessor`, zero allocation).  The m68k JIT caveat from
the 2026-07-09 entry still applies: JIT'd callers reach the same probe
through the Lisp reader discriminator, so they get the polymorphic hits too,
at that tier's cost.  *(Resolved 2026-07-10 — see the JIT'd-callers entry
above.)*

---

## 2026-07-09 — reader-GF fast dispatch (host)

**Commits**: `5291e7d` (reader inline cache + Lisp discriminator), then
`05c2aa2` (answer the call in `OP_CALL`).

A GF whose whole method set is the DEFCLASS-generated readers for one slot is
promoted: its inline cache (GF slot 8) holds `(TYPE-NAME . SLOT-INDEX)`, and
`OP_CALL` answers the call by comparing the receiver's `type_desc` and reading
the slot — no unwrap to the discriminating function, no frame, and none of the
per-access `CLASS-OF` + `*CLASS-TABLE*` + slot-index hash probes that
`%STRUCT-SLOT-VALUE` pays.

**Environment**: macOS arm64, host bytecode VM (the JIT is m68k-only, so this
is pure bytecode). Best-of-3, ~1s per timed run.

**Reproduce**: a portable port of the `SLOT-ACCESS/READER` benchmark from
Daniel Kochmański's [*A brief note about slot access cost in Common
Lisp*](https://turtleware.eu/posts/A-brief-note-about-slot-access-cost-in-Common-Lisp.html)
— 100 unrolled reader calls per iteration on a 10-slot class. Cross-checked
against SBCL 2.6.5 and ECL 26.5.5 on the same machine.

| ns per slot access | before | + Lisp discriminator | + `OP_CALL` |
|---|---|---|---|
| reader GF | 148.3 | 72.0 | **28.5** |
| reader ÷ struct-ref (24.7 ns) | 6.00× | 2.91× | **1.20×** |
| reader ÷ plain 1-arg call (44.5 ns) | 3.32× | 1.62× | **0.65×** |
| reader ÷ `slot-value` (103.6 ns) | 1.47× | 0.67× | **0.27×** |

A reader now costs 20% more than a raw constant-index struct slot read, and
*less than an ordinary function call* — because there is no call.

Reference points on the same machine and benchmark (native compilers, so the
absolute times are not comparable to a bytecode VM; the ratio is):

| | reader GF | reader ÷ struct-ref |
|---|---|---|
| SBCL 2.6.5 | 2.61 ns | 5.14× |
| ECL 26.5.5 (`compile-file`) | 13.22 ns | 2.59× |
| clamiga (this entry) | 28.5 ns | **1.20×** |

Note: ECL's `--load` of *source* runs its bytecode interpreter (53 ns/access,
a meaningless 1.05× ratio). The number above is after `compile-file`.

Tracked by `clos.accessor` ÷ `struct.accessor` in `trunk/bench-opt.lisp`.
The m68k JIT does not route through the bytecode `OP_CALL`, so JIT'd callers
fall back to the Lisp reader discriminator (72 ns tier) until the JIT call
sequence learns the same probe.  *(Resolved 2026-07-10 — the probe moved into
`cl_vm_apply`, which the JIT call helper funnels through; see the
JIT'd-callers entry above.)*

---

## 2026-07-05 — fused slot access + C GF inline-cache probe (host)

**Commit**: follows `fd2ecf4`. Second half of spec item 3.1. Three changes:

- New fused `%STRUCT-SLOT-VALUE` / `%STRUCT-SLOT-STORE` builtins: type-name →
  registry entry (O(1) hash probe) → slot index → direct slot read/write in
  ONE non-erroring builtin call. `SLOT-VALUE` / `(SETF SLOT-VALUE)` /
  `SLOT-BOUNDP` front the full protocol with it (the old front paid ~4 Lisp
  calls + ~8 builtin dispatches per access: `class-of`, the index-table
  branch, `%STRUCT-SLOT-INDEX`, a separate `%STRUCT-REF`). Works for both
  DEFSTRUCT and CLOS instances; any non-simple case (`:CLASS` slot, unbound,
  extended protocol, wrong type) falls back to the unchanged full protocol.
- DEFCLASS-generated `:accessor`/`:reader`/`:writer` methods inline the fused
  fast path in the method body instead of calling `SLOT-VALUE`.
- New `%GF-IC-EMF` builtin fuses the 1/2-arg GF discriminators' inline-cache
  hit path (read GF slot 8 + receiver `class-of` + `*CLASS-TABLE*` lookup +
  compare) into one call — this part speeds up ALL 1/2-arg GF dispatch.

| Benchmark | pre (first half only) | post |
|---|---|---|
| clos.slot-value (200k accesses/run) | 55 ms | **18 ms** |
| clos.accessor (GF dispatch + read) | 92 ms | **28 ms** |
| struct.slot-value | 53 ms | **18 ms** |
| struct.accessor (constant-index %STRUCT-REF — control) | 6 ms | 6 ms |
| clos.make-instance (2k instances + accessor reads/run) | 91 ms | **72 ms** |

Isolated probe (1M reads of one slot, bytecode): accessor 243 → 141 ms,
`slot-value` 96 ms. The remaining accessor-vs-struct.accessor gap (28 vs
6 ms) is generic dispatch itself — funcallable-instance unwrap +
discriminator closure + method-function call — not slot resolution.

**Real-world impact** — sento actor smoke benchmark
(`trunk/run-sento-bench.lisp`, 2s x 2 iterations, 2 producers, `:pinned`
`tell`), the workload whose runtime profile motivated spec 3.1:

| | msg/s |
|---|---|
| baseline (2e5f7c4, pre-3.1) | 55,709 |
| after 3.1 both halves | **118,036 (2.1x)** |

**Environment**: Apple M3 Ultra, macOS 26.5.2, `make host`, warm FASL cache,
`--heap 64M`, cwd = repo root.

**Reproduce**:

```
echo '(quit)' | ./build/host/clamiga --heap 64M --load trunk/bench-opt.lisp
```

---

## 2026-07-05 — struct registry hash index + zero-alloc slot resolution (host)

**Commit**: follows `5308c96`. First half of spec item 3.1, driven by the
sento actor-benchmark runtime profile (the largest attackable CPU cluster
after the VM loop was slot-access machinery: `get_slot_specs` 1,028 +
`bi_gethash` 512 + `cl_struct_slot_names` / `bi_class_of` / `bi_struct_ref`
leaf samples). Two fixes:

- `find_struct_entry` now probes an O(1) open-addressing hash index over the
  struct registry instead of walking the prepended alist (early-defined
  types paid a full O(types) walk on every access); the index is marked
  dirty on registration and on compaction, and rebuilt lazily
  (`platform_alloc` only).
- `SLOT-VALUE` / `(SETF SLOT-VALUE)` / `SLOT-BOUNDP` on struct instances
  resolve slots via the new zero-allocation `%STRUCT-SLOT-INDEX` builtin —
  the old path consed a fresh slot-name list per access
  (`cl_struct_slot_names`) and matched it linearly in Lisp.

New `struct.*` benchmarks in bench-opt (the type is buried behind 256 later
registrations, matching real-session registry positions; the pre-existing
`clos.*` benches take the CLOS slot-index-table hash path and never hit the
struct registry):

| Benchmark | pre-fix | post-fix |
|---|---|---|
| struct.slot-value (200k accesses/run) | 143 ms, 6,400,008 bytes | **53 ms, 0 bytes** |
| struct.typep | 50 ms | **12 ms** |
| struct.accessor (constant-index %STRUCT-REF — target/control) | 6 ms | 6 ms |
| clos.slot-value (CLOS hash path — control) | 55 ms | 55 ms |
| compile.file-plain / compile.file-mlf (registry lookups at compile time) | 27 / 27 ms | **20 / 20 ms** |

struct.slot-value is now at parity with the CLOS-instance hash path. The
remaining gap to struct.accessor (53 vs 6 ms) is generic-lookup overhead —
`class-of` + the `%find-struct-slot-index` call chain — which is the second
half of 3.1 (direct-index accessor closures at class finalization).

**Environment**: Apple M3 Ultra, macOS 26.5.2, `make host`, warm FASL cache,
`--heap 64M`, cwd = repo root.

**Reproduce**:

```
echo '(quit)' | ./build/host/clamiga --heap 64M --load trunk/bench-opt.lisp
```

---

## 2026-07-05 — MAKE-LOAD-FORM pre-pass hash index (host)

**Commit**: follows `64d6d8f`. Fix for the profiling finding that
`fasl_mlf_seen_p` / `cl_fasl_mlf_lookup` linear scans made the FASL writer's
make-load-form pre-pass O(n²) — ~85% of all cold-compile CPU (14,713 of ~17k
leaf samples) once any loaded library defines a `make-load-form` method
(cl-ppcre, serapeum, log4cl, local-time, trivia, cffi, ironclad, fset all
do, so the gate is effectively always open in real sessions). See
specs/performance.md item 1.9.

**Environment**: Apple M3 Ultra, macOS 26.5.2, `make host`, warm FASL cache,
cwd = repo root.

| Measurement | pre-fix | post-fix |
|---|---|---|
| `(asdf:load-system "sento" :force :all)` full dep recompile, 192M heap | 18,283 ms | **1,864 ms (9.8x)** |
| bench-opt `compile.file-mlf` (2 compiles of a 20k-cons graph + load) | 123 ms | **23 ms** |
| bench-opt `compile.file-plain` (same, MLF gate closed — control) | 22 ms | 22 ms |
| warm `(ql:quickload "sento")` (unaffected — no compile) | 840 ms | 840 ms |

Post-fix, `cl_fasl_mlf_prepass` no longer appears in the profile at all; the
cold-compile leaf leaders are now the compiler's macro-lookup chain
(`cl_macro_p` 1009 + `cl_get_macro` 184 + `cl_get_compiler_macro` 157
samples) and the VM loop (`cl_vm_run` 874) — the next profiling candidates.

Also measured (context for deprioritizing spec item 2.4): set-operation call
counters during a full sento quickload — `intersection`/`union`/`subsetp`
0 calls; `adjoin` 91 and `set-difference` 20 calls, all on lists ≤ 40
elements.

**Reproduce**:

```
echo '(quit)' | ./build/host/clamiga --heap 64M --load trunk/bench-opt.lisp
# recompile timing: (ql:quickload "sento" :silent t) then
#   (time (asdf:load-system "sento" :force :all))  at --heap 192M
# profile: run the recompile in background, then: sample <pid> 20
```

---

## 2026-07-05 — bench-opt baseline (host, pre-optimization)

**Commit**: `ac39e3c` + new `trunk/bench-opt.lisp`. Baseline for the pending
items in [specs/performance.md](../specs/performance.md) — 1.3 (declaim-speed:
const folding / dead branches / check elision), 1.8 (peephole), 2.4 (set ops
in C), 2.5 (free-list size classes), 3.1 (CLOS slot access), 3.2 (keyword
pre-computation), 3.3 (mv_count writes) — captured **before** any of them
land. Each benchmark maps to a spec item (see the file header); results are
best-of-3 with a warmup run, pure bytecode, deterministic workloads verified
against closed-form expected values (`fails=0`).

**Environment**: Apple M3 Ultra, macOS 26.5.2, `make host`, warm FASL cache,
`--heap 64M`, cwd = repo root.

| Benchmark | ms | bytes consed | gc |
|---|---|---|---|
| vm.fixnum-loop | 72 | 0 | 0 |
| vm.local-shuffle | 63 | 0 | 0 |
| vm.call-return | 63 | 0 | 0 |
| opt.const-fold | 39 | 0 | 0 |
| opt.dead-branch | 49 | 0 | 0 |
| safety1.svref-loop | 48 | 0 | 0 |
| safety0.svref-loop | 48 | 0 | 0 |
| safety1.call-args | 53 | 0 | 0 |
| safety0.call-args | 53 | 0 | 0 |
| set.intersection-small | 36 | 432,000 | 0 |
| set.union-small | 53 | 1,200,000 | 0 |
| set.intersection-large | 67 | 24,080 | 0 |
| set.union-large | 99 | 72,080 | 0 |
| set.difference-large | 68 | 24,080 | 0 |
| set.subsetp-large | 82 | 0 | 0 |
| set.intersection-equal | 45 | 32,320 | 0 |
| set.intersection-key | 59 | 24,160 | 0 |
| kw.call-8keys | 85 | 0 | 0 |
| clos.make-instance | 91 | 12,000,000 | 0 |
| clos.slot-value | 60 | 0 | 0 |
| clos.accessor | 92 | 0 | 0 |
| alloc.mixed-churn | 82 | 57,603,416 | 1 |
| alloc.cons-churn | 53 | 16,000,016 | 0 |
| compile.file-plain * | 22 | 1,635,384 | 0 |
| compile.file-mlf * | 123 | 1,636,736 | 0 |
| **total** | **1460** | | |

\* The `compile.*` pair was added to the suite the same day, just before the
MLF pre-pass fix landed (see the entry above); values here are pre-fix.

Run-to-run stability: a second full run totaled 1452 ms (~0.5% variance);
individual benchmarks repeat within ±2 ms.

Notes for later comparisons:

- `safety0.*` vs `safety1.*` pairs time equal today (only `the`'s
  `OP_ASSERT_TYPE` is safety-gated); item 1.3's check elision should open a
  gap in favor of `safety0.*`.
- The `set.*-large` benchmarks are the O(n*m) → O(n+m) headline for 2.4;
  `set.*-small` guards against the C-builtin version regressing small inputs.
- `alloc.mixed-churn` is sized to cycle the GC (gc ≥ 1) so the free list is
  actually populated and exercised — keep it that way or 2.5 won't show.

**Reproduce**:

```
echo '(quit)' | ./build/host/clamiga --heap 64M --load trunk/bench-opt.lisp
```

On Amiga, scale down the repetition counts and the large-set size first:

```lisp
(defparameter cl-user::*bo-scale* 1/20)
(defparameter cl-user::*bo-set-size* 150)
(load "trunk/bench-opt.lisp")
```

---

## 2026-07-05 — GC mark phase on large live heaps (host, growable mark stack)

**Commit**: growable GC mark stack (follows `72fe7ed`). Found investigating
"`(asdf:load-system :chipi-ui/tests)` takes 21s from a Sly session vs 13s
from a fresh REPL" — the difference was never multi-threading (MT allocator
path, worker-thread eval, and slynk mREPL streaming all measured within
noise of the single-threaded baseline); it was the *live-heap size* of the
long-running Sly image. The fixed 4096-entry mark stack silently overflowed
on any object with more unmarked children, and each full-arena overflow
re-scan pass recovers only ~one-stack-full — quadratic marking.

**Environment**: Apple M3 Ultra, macOS 26.5.2, `make host`, warm FASL
cache, `--heap 512M`, cwd = repo root, chipi-ui dependency closure from
`~/Development/MySources/cl-hab/ocicl`.

**Single mark-sweep `(ext:gc)`, 210MB live** (160 x 65536-element vectors
of fresh conses — worst-case wide fan-out):

| Build | one GC cycle |
|---|---|
| fixed 4096-entry mark stack (pre-fix) | 49,248 ms |
| growable mark stack (this change) | 56 ms |

**`(asdf:load-system :chipi-ui/tests)`, warm FASLs, 512M heap**:

| Scenario | pre-fix | post-fix |
|---|---|---|
| fresh image, main thread | 12,800 ms (3 GCs) | 11,066 ms (3 GCs) |
| + 200MB live ballast (reproduces the user's Sly-session shape: 9 GCs, ~330MB used) | >10 min (killed) | 11,772 ms |

**Reproduce**: build ballast with
`(loop repeat 160 collect (let ((v (make-array 65536))) (dotimes (i 65536) (setf (aref v i) (cons i i))) v))`,
then `(time (ext:gc))`. Growth/fallback observability:
`(ext:%gc-mark-stats)` → `(capacity grows rescan-passes)`; a nonzero third
element means growth failed (cap/OOM) and the quadratic fallback ran.

---

## 2026-07-05 — sento actor throughput (host)

**Commit**: `2e5f7c4` (post tier-4 GC audit + contended acquire-lock
parking fix — contended `mp:acquire-lock` waiters now park on a condvar
broadcast by `release-lock` instead of sleep-polling on a 10ms grid).

**Environment**: Apple M3 Ultra (28 cores), 96GB RAM, macOS 26.5.2,
`make host` build, warm FASL cache, `--heap 192M`.

**Benchmark**: `sento.bench::run-benchmark` from cl-gserver `bench.lisp`
(sento 20260101 quicklisp dist). Producer threads `tell`/`ask-s` a counter
actor as fast as possible for the configured duration; backpressure pauses
producers when the queue exceeds 10k messages. Reported number is messages
processed per second, averaged over iterations.

**Smoke config** (as run by `trunk/run-sento-bench.lisp`: 2s x 2
iterations, 2 producer threads):

| Config | avg msg/s |
|---|---|
| `:pinned`, `tell` | 55,709 |

**Measurement config** (10s x 3 iterations, 8 producer threads):

| Config | avg msg/s | min–max | deviation |
|---|---|---|---|
| `:pinned`, fire-and-forget `tell` | 42,131 | 41.8k–42.5k | ±281 |
| `:shared` (4 workers), `tell` | 14,860 | 14.2k–15.2k | ±482 |
| `:shared` (4 workers), `ask-s` round-trip | 15,460 | 15.0k–16.0k | ±409 |

**Observations**:

- Iteration-to-iteration deviation is under 1–3% — no scheduling jitter
  from the lock layer (the pre-fix 10ms sleep-poll made 8-producer
  configs collapse ~6x and scatter widely).
- Synchronous `ask-s` (send + blocking reply round-trip, the heaviest
  user of the lock/condvar handoff path) matches plain async `tell` on
  the shared dispatcher — the round-trip machinery is not the
  bottleneck; shared-dispatcher fan-out dominates.
- `:pinned` (dedicated actor thread) is ~2.8x faster than `:shared`,
  as expected — shared dispatch adds a second queue hop through the
  worker pool.
- 2 producers outpace 8 (55.7k vs 42.1k pinned): mild fair-contention
  degradation, no collapse.

**Reproduce**:

```
# smoke (as committed):
./build/host/clamiga --heap 192M --load trunk/run-sento-bench.lisp

# measurement configs:
#   (sento.bench::run-benchmark :dispatcher :pinned :duration 10
#                               :num-iterations 3 :load-threads 8)
#   (... :dispatcher :shared :num-shared-workers 4)
#   (... :dispatcher :shared :num-shared-workers 4 :with-reply-p t)
```

---

## 2026-07-05 — JIT call loop (Amiga, FS-UAE)

**Commit**: `2e5f7c4`. Emulated 68020 via FS-UAE (bundled config,
`make -f Makefile.cross test-amiga`), run automatically after the Amiga
test suite (`trunk/bench-jit-loop.lisp`).

| Benchmark | result |
|---|---|
| JIT-BENCH (2,000,000 calls) | 25,120 ms → 79,618 calls/sec |
