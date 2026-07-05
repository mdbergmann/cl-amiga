# Benchmark Results

Point-in-time performance measurements for future reference. Newest entries
first. Each entry records the commit, environment, exact reproduction
command, and results, so later runs can be compared like-for-like.

Related: [specs/performance.md](../specs/performance.md) is the optimization
*plan*; this file is the *measured results* log.

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
| **total** | **1460** | | |

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
