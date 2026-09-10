# sento Benchmark Results — 0.10 (speed 3)

Point-in-time measurements of the sento actor pipeline on cl-amiga (host)
for the 0.10 cycle — master ahead of the 0.10 version bump — run at
`(optimize (speed 3))` with the same protocol as the 0.8 entry, plus a
same-session A/B against binaries built from the 0.9 and 0.8 bump commits.

**Headline: 0.10 is the fastest entry in this series on every cell that
ran, by a wide margin.** Against the same-session 0.8 binary it is +17% to
+99% per cell (pinned/tell 171k → 274k msg/s, shared/ask-s 36k → 72k), and
it is the first runtime since the regression the 0.8 entry documented to
match (pinned/ask) or beat (every other cell that ran) the 0.4 binary.
All of the gain sits between the 0.9 bump and HEAD — the Tier-4 work
(`specs/performance.md` 4.1–4.3: runtime
taxes, the call/unwind protocol, superinstructions); the 0.9 and 0.8
binaries read the same within noise. One cell did not run at its default
configuration: **shared/ask fills clamiga's 16384-slot lock table on the
0.10 binary**, because the faster sender side keeps more asynchronous asks
in flight and each one holds three locks. At a lower queue cap the cell
runs at 28.7k msg/s, +54% over the 0.8 binary at the same cap. See
"shared/ask and the lock table" below.

Companion documents: [sento-bench-results-0.2.md](sento-bench-results-0.2.md)
records the 0.2 baseline and describes the benchmark itself (N sender
threads flooding one receiver actor whose body increments a counter — the
numbers measure framework message-plumbing overhead, not application work);
[sento-bench-results-0.3.md](sento-bench-results-0.3.md) is the speed 1 vs
speed 3 comparison; [sento-bench-results-0.4.md](sento-bench-results-0.4.md)
the 0.4 matrix; [sento-bench-results-0.8.md](sento-bench-results-0.8.md)
the previous matrix, its root-cause bisects and the "after the fixes"
column this entry's deltas are computed against;
[benchmarks.md](benchmarks.md) has the Tier-4 phase entries (per-primitive
costs, opcode profiles, acceptance cells) and the interleaved-A/B method
note this entry follows.

## Environment

- **Commit**: `6989f7aa` (`master` HEAD). The binary still reports
  `0.9.0`, FASL v33 — the 0.10 bump had not happened yet, so the
  version-keyed cache directory is `cl-amiga-0.9-fasl33`.
- **A/B binaries**: 0.9 built from `224340cf` (the v0.9 bump; reports
  `0.9.0`, FASL v31) and 0.8 built from `cabfed45` (the v0.8 bump; reports
  `0.8.0`, FASL v31), each with the same `make host` in a detached
  worktree. Between the two bump commits there is no runtime-performance
  commit (audio, release tooling, FFI diagnostics, the off-heap shutdown
  fixes); between the 0.9 bump and HEAD are the three Tier-4 phases and
  their follow-ups.
- **Host**: macOS 26.6.2, arm64 (Apple M3 Ultra) — the same machine and
  OS build as the 0.8 entry. All three binaries were built today with
  Apple clang 21.0.0 (Xcode 26.6), the Makefile's default `make host`
  (whole-program LTO except `vm.o`, as since the 0.8 fixes); the 0.8
  entry did not record its compiler version.
- **Binary**: `build/host/clamiga` of each worktree, `--heap 192M`,
  `CLAMIGA_FORCE_SPEED=3`.
- **Collector**: generational (host default), TLABs active — all three
  binaries.
- **sento**: 3.4.5, local checkout `~/Development/MySources/cl-gserver` at
  `82cd1e1` (ASDF resolves it ahead of the quicklisp dist). The 0.8 entry
  ran 3.4.4 (`013ab63`). 3.4.5 (2026-09) changed the reply path in
  `message-box.lisp`: a shared-dispatcher `ask-s` now gets its own lock and
  condition variable per message item (it used to share one per box, the
  cross-wired-replies bug), the reply-side notify runs under that lock, and
  the waiter loops on a `done-p` flag instead of trusting the wakeup. Every
  leg of the matrix ran 3.4.5, so the same-session A/B is clean; the
  "0.8 doc" column below folds the sento change in with the runtime change,
  and the "sento 3.4.4 legs" section separates the two.
- **atomics**: `~/quicklisp/local-projects/atomics` fork at `1caed1a`
  (documentation commits on top of the `d6d72d8` the 0.8 legs ran).
  `cas`/`atomic-incf` map onto `mp:compare-and-swap` in all three binaries.
- **Date**: 2026-09-10

## Reproduction

Same driver and protocol as the 0.8 entry: `trunk/sento-bench-matrix.lisp`
loads `trunk/load-sento-bench.lisp`, runs the six cells back-to-back in one
session and prints a `CELL ... STATS` line per cell with the
`(ext:%gc-time-stats)` / `(ext:%gengc-stats)` deltas and the derived GC
share. Run with a **cold ASDF cache** so sento and every dependency compile
at speed 3. Rather than wiping `~/.cache/common-lisp/<version dir>` (shared
with any other clamiga session on the machine), point
`CLAMIGA_FASL_CACHE_DIR` at a fresh directory — the runtime namespaces it
by version and FASL format the same way, and the warm repeat reuses it:

```
CLAMIGA_FASL_CACHE_DIR=/path/to/fresh/dir CLAMIGA_FORCE_SPEED=3 \
    ./build/host/clamiga --no-userinit --heap 192M --non-interactive \
    --load trunk/sento-bench-matrix.lisp
```

For the A/B legs, `git worktree add --detach <dir> <bump commit>`, `make
host` there, and run the same command from that directory with its own
cache directory. A different sento checkout goes in through the driver's
`ATOMICS_DIR` hook (it only pushes a directory onto
`asdf:*central-registry*`; the driver prints which `sento.asd` loaded).
`SENTO_QUEUE_CAP=<n>` (added to the driver with this entry) passes `n` as
`:wait-if-queue-larger-than` to every cell. Use it for a shared/ask-only
session on 0.10 and later (see below), not for the whole matrix: a 0.10
run of all six cells at cap 2000 read 202,933 ±32,591 on pinned/tell
against 274,098 at the default cap — the cap throttles the `tell` senders
there — while ask-s, ask and shared/tell were within 6% of their
default-cap figures. The supplementary runs in this entry used a
single-cell copy of the driver with the same arguments.

Legs ran strictly sequentially on an otherwise idle machine, in this
order: 0.10 cold, 0.10 warm repeat, 0.9 cold, 0.8 cold; then the
supplementary shared/ask cell at queue cap 2000 on 0.10, 0.9, 0.8; then
the sento 3.4.4 legs. A cold load (quickload of sento, serapeum,
cl-unicode, log4cl and trivial-benchmark at speed 3 — 284 FASLs written
into the fresh cache directory, 110 of them for those systems) takes
under ten seconds on any of the three binaries, so a leg is the sum of
its cells (~3.5 minutes) and the whole series ran in about 40 minutes.

## Reply-mode / dispatcher matrix

Config: `:num-shared-workers 8`, `:load-threads 8`, `:duration 5`,
`:num-iterations 6`. `AVG` is the throughput figure of record (msg/s) from
the cold-cache run; "repeat" is the warm-cache rerun of the same binary;
GC share = total collector time (STW + phases + minors) / cell wall time;
"0.8 doc" is the "after the fixes" column of sento-bench-results-0.8.md
(sento 3.4.4, 2026-08-30); "0.4" is that entry's same-session 0.4 binary.

| Dispatcher | Reply mode | AVG 0.10 s3 | Dev | repeat | GC share | 0.8 doc | 0.10 vs 0.8 doc | 0.4 | 0.10 vs 0.4 |
| ---------- | ---------- | ----------: | --: | -----: | -------: | ------: | --------------: | --: | ----------: |
| **PINNED** | tell       | **274,098** | 19,692 | 267,170 | 1.4% | 175,227 | **+56.4%** | 191,254 | **+43.3%** |
| PINNED     | ask-s      | **108,575** |  1,863 | 107,836 | 5.2% |  97,733 | +11.1% | 103,441 | +5.0% |
| PINNED     | ask        |  **33,419** |    262 |  32,637 | 3.0% |  28,976 | +15.3% |  33,239 | +0.5% |
| SHARED     | tell       |  **39,056** |    823 |  40,613 | 3.6% |  29,748 | **+31.3%** |  30,204 | **+29.3%** |
| SHARED     | ask-s      |  **72,277** |    313 |  69,207 | 2.4% |  45,849 | **+57.6%** |  49,673 | **+45.5%** |
| SHARED     | ask        | *lock table full* | — | *lock table full* | — | 22,302 | — | 25,466 | — |

The two 0.10 runs agree within 2.5% on four cells and within 4.2% on
shared/tell and shared/ask-s — the tightest pair in this series. The
shared/ask cell aborted in both runs (after 9.7 s cold, 2.2 s warm) with
`MP:MAKE-LOCK: lock table full (max 16384)`; the supplementary
measurement below puts it at **28,678 msg/s** with the bench's queue cap
lowered from 10000 to 2000 and the cell run in a process of its own, a
configuration under which the 0.8 and 0.9 binaries read exactly what
they read at the default cap.

## Same-session A/B: 0.10 vs 0.9 vs 0.8 binaries

Same machine, same session, same sento 3.4.5, all three cold-compiled at
speed 3. "µs/msg" is `1e6 / AVG`; the last column is the per-message cost
the 0.10 runtime removes relative to 0.8.

| Cell         | 0.10 AVG | 0.9 AVG | 0.8 AVG | 0.10 vs 0.8 | 0.10 vs 0.9 | 0.9 vs 0.8 | 0.10 µs/msg | 0.8 µs/msg | Δ µs/msg |
| ------------ | -------: | ------: | ------: | ----------: | ----------: | ---------: | ----------: | ---------: | -------: |
| PINNED tell  | **274,098** | 170,960 | 170,779 | **+60.5%** | +60.3% | +0.1% |  3.65 |  5.86 |  −2.2 |
| PINNED ask-s | **108,575** |  93,393 |  92,692 | **+17.1%** | +16.3% | +0.8% |  9.21 | 10.79 |  −1.6 |
| PINNED ask   |  **33,419** |  24,742 |  23,841 | **+40.2%** | +35.1% | +3.8% | 29.92 | 41.94 | **−12.0** |
| SHARED tell  |  **39,056** |  27,392 |  27,808 | **+40.4%** | +42.6% | −1.5% | 25.60 | 35.96 | **−10.4** |
| SHARED ask-s |  **72,277** |  36,512 |  36,314 | **+99.0%** | +98.0% | +0.5% | 13.84 | 27.54 | **−13.7** |
| SHARED ask   | *lock table full* | 18,635 | 17,745 | — | — | +5.0% | — | 56.35 | — |
| SHARED ask, queue cap 2000 | **28,678** | 18,624 | 18,617 | **+54.0%** | +54.0% | +0.0% | 34.87 | 53.71 | **−18.8** |

0.9 and 0.8 are the same runtime for this workload (six cells within −1.5%
to +5%, the noisy shared/ask cell being the +5%), which is what the commit
range between the two bumps predicts. Everything in the 0.10 column is the
Tier-4 work between the 0.9 bump and HEAD.

GC telemetry for the A/B (pinned/ask, the cell the 0.8 entry watched):
0.10 ran 203 GCs (196 minors, 7 compactions, 0.92 s total, 3.0% of wall);
0.8 ran 146 (139 minors, 7 compactions, 0.53 s, 1.7%). Mark/sweep time is
zero in every cell of every leg; the worst single stop-the-world pause on
the pinned cells is 8.9 ms (0.10) vs 10.4 ms (0.8). 0.10 collects *more*
because it pushes more messages and therefore allocates more per second;
the collector's share of wall time is up in every cell (1.4–5.2% vs
1.0–3.3%), all of it in stop-the-world pauses — see Observations.

## shared/ask and the lock table

`MP:MAKE-LOCK` hands out ids from a fixed 16384-entry table
(`CL_MAX_LOCKS`, `src/core/thread.h`); a slot is reclaimed when the
wrapping `CL_Lock` object is collected, and on a full table `MAKE-LOCK`
runs a minor collection, then a full one, and errors only if both freed
nothing. The error therefore means more than 16384 locks were *reachable*
at once.

The bench's asynchronous `ask` (`sento.actor:ask`, identical in 3.4.4 and
3.4.5) allocates a temporary waiting actor per message — an
`async-waitor-actor` with its own `message-box/dp` (one lock for the box,
one lock and one condition variable for its queue) plus a future (one
lock) — that stays reachable until the reply has been delivered through
the shared dispatcher and the waiter has stopped itself. Three locks per
in-flight ask puts the ceiling at ~5,400 asks in flight. The bench
throttles its 8 senders on the *receiver's* queue depth (sleep when it
exceeds `wait-if-queue-larger-than`, default 10000, checked every 1000
sends per sender), which bounds nothing near that. The 0.8 and 0.9
binaries never hit the ceiling, so their in-flight set (queued asks plus
replies not yet delivered) stayed under ~5,400 — their senders did not
outrun the receiver by more than that. The 0.10 sender side (CLOS
instantiation, dispatch, the submit path) is fast enough to push it past,
and the working set crosses the table. Before the abort the
`MAKE-LOCK` retry path is visible in the telemetry: 85 compactions in the
9.7 s the cold cell lasted (29% of wall in the collector) against 6–7 per
cell everywhere else.

A probe of the allocator itself (`MP:MAKE-LOCK` in a loop, single thread)
reads ≈3 µs per call with 15,000 live locks and under 1 µs on an empty
table — the linear slot scan is not what limits this cell, the cap is.

**Supplementary measurement.** The same cell with the bench's
`:wait-if-queue-larger-than` lowered to 2000 (a documented bench
parameter; nothing in sento or clamiga changed), one run per binary in
the same session, each in a process of its own (warm cache):

| Binary | SHARED ask, cap 2000 | at default cap 10000 | GC share |
| ------ | -------------------: | -------------------: | -------: |
| 0.10   | **28,678** (dev 195) | *lock table full* | 4.2% |
| 0.9    | 18,624 (dev 331) | 18,635 | 1.5% |
| 0.8    | 18,617 (dev 206) | 17,745 | 2.1% |

The older binaries read the same at either cap (their in-flight set stays
under the ceiling either way), so 28,678 is the comparable 0.10 figure:
+54% over 0.8, 34.9 µs per message vs 53.7.

How stable is that? Two more single-cell runs at cap 2000 read 28,222
and 28,712, three at cap 500 read 28,828 / 28,216 / 27,888 — six passes
within ±2%, and the cap itself does not move the number. But the cell
is *not* safe at cap 2000 in a full-matrix session: a 0.10 run of all six
cells with `SENTO_QUEUE_CAP=2000` hit the table again on shared/ask after
the five other cells had run in the same process. The bench's throttle is
racy (each of the 8 senders checks the queue depth only every 1000
sends), so the in-flight working set is bounded in practice, not in
principle, and a cap only shifts the odds. Treat 28.7k as the cell's
figure at this queue cap in a process of its own; the runtime follow-up
below is what makes the cell run at the standard configuration again.

**What to do about it.** This is a runtime limit that a faster runtime
has now exposed, not a sento change: a program that keeps more than ~5k
asynchronous asks in flight on the shared dispatcher hits it on any
clamiga. The table is fixed-size so that lock ids stay stable for the
per-id holder/depth side tables and the image restore (`thread.h`
explains the sizing); the slot scan runs under `cl_thread_list_lock`.
A follow-up should either grow the tables in chunks (never moving live
entries — the id must stay valid without the mutex) or raise the host cap
by an order of magnitude and give the allocator a free list; the Amiga
cap (256) is a separate decision. Until then the shared/ask cell of this
matrix needs the lower queue cap, in a process of its own, on 0.10 and
later.

## sento 3.4.4 legs

To separate the sento upgrade from the runtime change, the 0.8 and 0.10
binaries also ran the full matrix against sento 3.4.4 (`013ab63`, the
checkout the 0.8 entry used) — cold cache, same session, after the legs
above. "0.8 doc" is again the 0.8 entry's "after the fixes" column, which
was measured on 3.4.4.

| Cell         | 0.10 / 3.4.4 | 0.10 / 3.4.5 | 0.8 / 3.4.4 | 0.8 / 3.4.5 | 0.8 doc (3.4.4, Aug) | 0.8 / 3.4.4 today vs doc |
| ------------ | -----------: | -----------: | ----------: | ----------: | -------------------: | -----------------------: |
| PINNED tell  | **279,268** | 274,098 | 176,080 | 170,779 | 175,227 |  +0.5% |
| PINNED ask-s | **108,608** | 108,575 |  97,147 |  92,692 |  97,733 |  −0.6% |
| PINNED ask   |  **33,377** |  33,419 |  24,043 |  23,841 |  28,976 | **−17.0%** |
| SHARED tell  |  **40,248** |  39,056 |  28,419 |  27,808 |  29,748 |  −4.5% |
| SHARED ask-s |  **69,927** |  72,277 |  38,576 |  36,314 |  45,849 | **−15.9%** |
| SHARED ask   | *lock table full* | *lock table full* | 17,431 | 17,745 | 22,302 | **−21.8%** |

Three things follow.

- **The sento version is worth a few percent at most.** 3.4.5 costs the
  0.8 binary 2–6% on the tell and ask-s cells (the per-item lock and
  condition variable plus the notify-under-lock on the reply path) and
  the 0.10 binary 2–3% on the tell cells only (pinned/tell −1.9%,
  shared/tell −3.0%: every popped item now runs a finalize call in an
  `unwind-protect`, waiter or not). The reply cells are flat on 0.10, and
  shared/ask-s reads 3.4% *higher* on 3.4.5 — the caller now waits on a
  condition variable after an asynchronous dispatch instead of 3.4.4's
  synchronous hop through the worker's own reply path, and on 0.10 the
  per-item lock and condvar cost less than that hop did. On 3.4.4 the
  0.10-vs-0.8 deltas are +59%, +12%, +39%, +42%, +81% — the same picture
  as the A/B table.
- **shared/ask fills the lock table on 0.10 with either sento** (4.2 s
  into the cell on 3.4.4). The asynchronous-ask path is identical in the
  two versions; the cap is a runtime effect, as the section above says.
- **The 0.8 binary reproduces its August figures on pinned/tell and
  pinned/ask-s (+0.5%, −0.6%) but reads 16–22% below them on pinned/ask,
  shared/ask-s and shared/ask, with either sento.** The runtime is the
  same (between the fix tree the August column was measured on and the
  bump commit: docs, MUI, NTH, the banner, the inspector — nothing on the
  message path), the atomics fork differs by documentation commits only,
  the OS build is the same; the compiler version was not recorded in
  August. Whatever moved is outside the same-session A/B, which is why
  this entry's figures of record are the A/B columns and not the
  doc-to-doc deltas: the "+11%" and "+15%" against the 0.8 doc on
  pinned/ask-s and pinned/ask are the conservative reading, and the
  same-session numbers say +17% and +40%.

## Observations

- **Where the gain is.** Per message, pinned/tell dropped from 5.9 µs to
  3.6 µs and pinned/ask-s from 10.8 to 9.2 — the plain mailbox handoff and
  the synchronous reply, where the 0.8 entry had found ~1 µs of runtime
  regression, now sit well under the 0.4 binary (5.2 and 9.7 µs). The
  async cells and the shared dispatcher gained the most in absolute terms:
  −10 to −19 µs per message on shared/tell, pinned/ask, shared/ask-s and
  shared/ask (cap 2000). Those are the paths with the most Lisp-side work
  per message — CLOS instantiation of the waiting actor, the dispatcher's
  `ask-s` hop to a worker, `handler-case` around every handler invocation,
  `unwind-protect` cleanup, slot access on the message items — which is
  exactly what Tier-4 targeted (CALL_GLOBAL, `%handler-case` as a special
  form, the multiple-value save stack, the per-thread slot cache, the
  superinstructions). The 0.8 entry's conclusion that "the per-message
  path is dominated by C" was true of the runtime taxes it measured; once
  those were removed, the bytecode side turned out to be worth 40–100% on
  the heavier cells.
- **Speed 3 vs the default speed.** Not measured in this entry. Since
  phase 3 the peephole pass runs at every speed above 0, so the
  superinstruction gain does not depend on the forced speed; the 0.3
  entry's finding that speed 3 adds little on top holds a fortiori.
- **The collector's share went up, in stop-the-world time.** 0.10 spends
  1.4–5.2% of wall in the collector against 1.0–3.3% for 0.8, and the
  difference is entirely `stw=` — pinned/ask-s went from 0.28 s across
  320 stops (0.8) to 1.09 s across 694 stops (0.10) in 31 s of wall, with
  the epoch `skips` count more than tripling (138 → 481). Mark, sweep and
  minor times are flat. Twice as many stop-the-world rendezvous for 17% more messages
  says the coordination cost per collection rose, not the collection
  itself; at 5% of wall it is not what limits the cell, but it is the one
  telemetry line that moved the wrong way and the first thing to look at
  if the collector is ever the bottleneck again.
- **Cold-load MT soak at speed 3, five times over** (0.10 twice, 0.9,
  0.8 twice; two sento versions): each leg cold-compiled sento and its full dependency
  stack through the peephole at speed 3 and pushed 11–16M messages across
  the multi-threaded cells on the generational collector; the only
  failure in the series is the lock-table cap above.
- SHARED/tell's wall time (47 s for 6×5 s iterations vs ~31 s elsewhere)
  is the queue-drain overhead the 0.4 entry describes — fire-and-forget
  senders outrun the 8 shared workers and each iteration finishes its
  backlog after the send window closes. It shrank from 50–51 s on the 0.8
  and 0.9 binaries to 45–47 s on 0.10: the workers drain faster.
