# Benchmark Results

Point-in-time performance measurements for future reference. Newest entries
first. Each entry records the commit, environment, exact reproduction
command, and results, so later runs can be compared like-for-like.

Related: [specs/performance.md](../specs/performance.md) is the optimization
*plan*; this file is the *measured results* log.

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
