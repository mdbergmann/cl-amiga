# sento Benchmark Results — 0.8 (speed 3)

Point-in-time measurements of the sento actor pipeline on cl-amiga (host)
for the 0.8 cycle — master ahead of the 0.8 version bump — run at
`(optimize (speed 3))` with the same protocol as the 0.4 entry, and,
because every cell came in below 0.4, a same-session A/B against a 0.4
binary built from the v0.4 bump commit.

**Headline: 0.8 is slower than 0.4 on every cell of the matrix, and the
A/B pins it on the runtime.** The 0.4 binary reproduces its July numbers
today, on the same machine, same sento, same session; the 0.8 binary is
−9% to −27% against it. The async-ask cells lose the most (~+11–13 µs per
message on both dispatchers), the plain `tell` mailbox path the least.
GC is not the cause — the collector's share of wall time is *lower* in 0.8
in every cell. The offending change(s) among the 168 commits between
`cfd2bab` and `5b4bbf6` have not been bisected yet; see Observations.

Companion documents: [sento-bench-results-0.2.md](sento-bench-results-0.2.md)
records the 0.2 baseline and describes the benchmark itself (N sender
threads flooding one receiver actor whose body increments a counter — the
numbers measure framework message-plumbing overhead, not application work);
[sento-bench-results-0.3.md](sento-bench-results-0.3.md) is the speed 1 vs
speed 3 comparison; [sento-bench-results-0.4.md](sento-bench-results-0.4.md)
is the previous matrix this entry's deltas are computed against;
[benchmarks.md](benchmarks.md) has the TLAB / generational-GC entries and
the interleaved-A/B method note this entry follows.

## Environment

- **Commit**: `5b4bbf6` (`master`; HEAD at run time was `860106e`, a
  README-only commit on top of it). The binary still reports `0.7.0`,
  FASL v31 — the 0.8 bump had not happened yet, so the ASDF cache
  directory is the 0.7 one.
- **A/B binary**: 0.4 built from `cfd2bab` (the v0.4 bump) with the same
  `make host` in a detached worktree; reports `0.4.0`, FASL v23
- **Host**: macOS 26.6.2, arm64 (Apple M3 Ultra) — the 0.4 entry ran on
  macOS 26.5.2
- **Binary**: `build/host/clamiga`, `--heap 192M`, `CLAMIGA_FORCE_SPEED=3`
- **Collector**: generational (host default), TLABs active — both binaries
- **sento**: 3.4.4, local checkout `~/Development/MySources/cl-gserver` at
  `013ab63` (ASDF resolves it ahead of the quicklisp dist's 3.4.2). 3.4.4
  (2026-08-15) replaced blackbird with sento's own future core; both
  binaries ran this version, so the sento change is not in the A/B delta.
- **atomics**: `~/quicklisp/local-projects/atomics` fork. The 0.8 legs ran
  `d6d72d8` (`cas`/`atomic-incf` mapped onto the new `mp:compare-and-swap`
  primitives, PR #26); the 0.4 binary has no such primitives, so its leg
  ran the fork's previous lock-backend commit `2f5c176` via
  `asdf:*central-registry*`. Either way sento only uses `atomics` in
  `router` and `actor-context` — its message box is `queue-locked` — so
  the backend is off the per-message path.
- **Date**: 2026-08-29

## Reproduction

Run with a **cold ASDF cache** so sento and every dependency compile at
speed 3 — cached FASLs bypass the compiler, so without the wipe the forced
speed changes almost nothing. `trunk/sento-bench-matrix.lisp` is the driver
used here: it loads `trunk/load-sento-bench.lisp`, runs the six cells
back-to-back in one session and prints a `CELL ... STATS` line per cell
with `(ext:%gc-time-stats)` / `(ext:%gengc-stats)` deltas and the derived
GC share.

```
rm -rf ~/.cache/common-lisp/cl-amiga-0.7-fasl31     # version-keyed: adjust
CLAMIGA_FORCE_SPEED=3 ./build/host/clamiga --no-userinit --heap 192M \
    --non-interactive --load trunk/sento-bench-matrix.lisp
```

For the A/B leg, check out the old commit in a worktree, `make host`
there, and run the same command from that directory (its own version-keyed
cache directory stays cold). A binary predating `mp:compare-and-swap`
needs the lock-backend `atomics` checkout — point `ATOMICS_DIR` at it and
the driver pushes it onto `asdf:*central-registry*` (the driver prints
which `atomics.asd` and `sento.asd` actually loaded).

Legs ran strictly sequentially on an otherwise idle machine, in this order:
0.8 cold, 0.8 warm repeat, 0.4 cold. The 0.4 leg ran last, after ~10
minutes of sustained all-core load — session drift (benchmarks.md,
2026-07-11) can only have *understated* its lead.

## Reply-mode / dispatcher matrix

Config: `:num-shared-workers 8`, `:load-threads 8`, `:duration 5`,
`:num-iterations 6`. `AVG` is the throughput figure of record (msg/s) from
the cold-cache run; "repeat" is the warm-cache rerun of the same binary;
GC share = total collector time (STW + phases + minors) / cell wall time;
"0.4 doc" is the 0.4 s3 column from sento-bench-results-0.4.md.

| Dispatcher | Reply mode | AVG 0.8 s3 | Dev | repeat | GC share | 0.4 doc | 0.8 vs 0.4 doc |
| ---------- | ---------- | ---------: | --: | -----: | -------: | ------: | -------------: |
| **PINNED** | tell       | **169,018** | 5,145 | 164,580 | 1.0% | 188,076 | **−10.1%** |
| PINNED     | ask-s      |  94,165 | 2,077 |  94,307 | 2.6% |  98,564 | −4.5% |
| PINNED     | ask        |  24,256 |   416 |  23,870 | 1.8% |  32,367 | **−25.1%** |
| SHARED     | tell       |  29,491 |   260 |  28,973 | 2.9% |  31,996 | −7.8% |
| SHARED     | ask-s      |  43,513 |   286 |  43,427 | 1.3% |  49,666 | **−12.4%** |
| SHARED     | ask        |  19,191 |   286 |  17,571 | 1.5% |  23,101 | **−16.9%** |

The two 0.8 runs agree within 3% on five cells (shared/ask, the noisiest
cell in every entry of this series, within 9%), so the version-over-version
drop is not run-to-run spread.

## Same-session A/B: 0.8 vs 0.4 binary

Same machine, same session, same sento 3.4.4, both cold-compiled at speed
3. "µs/msg" is `1e6 / AVG`; the last column is the per-message cost the
0.8 runtime adds.

| Cell         | 0.8 AVG | 0.4 AVG (today) | 0.4 doc (July) | 0.8 vs 0.4 | 0.8 µs/msg | 0.4 µs/msg | Δ µs/msg |
| ------------ | ------: | --------------: | -------------: | ---------: | ---------: | ---------: | -------: |
| PINNED tell  | 169,018 | **191,254** | 188,076 | −11.6% |  5.92 |  5.23 |  +0.7 |
| PINNED ask-s |  94,165 | **103,441** |  98,564 |  −9.0% | 10.62 |  9.67 |  +1.0 |
| PINNED ask   |  24,256 |  **33,239** |  32,367 | **−27.0%** | 41.23 | 30.09 | **+11.1** |
| SHARED tell  |  29,491 |  **30,204** |  31,996 |  −2.4% | 33.91 | 33.11 |  +0.8 |
| SHARED ask-s |  43,513 |  **49,673** |  49,666 | −12.4% | 22.98 | 20.13 |  +2.9 |
| SHARED ask   |  19,191 |  **25,466** |  23,101 | **−24.6%** | 52.11 | 39.27 | **+12.8** |

The 0.4 binary lands on its July figures (−6% to +10%, five cells within
5%) — the machine, the OS update, and sento 3.4.4 are all excluded as
explanations. The gap is in the runtime.

GC telemetry for the A/B (pinned/ask, the worst cell): 0.8 ran 148 GCs
(141 minors, 7 compactions, 0.55 s total, 1.8% of wall); 0.4 ran 204
(197 minors, 7 compactions, 0.80 s, 2.6%). Mark/sweep time is zero in
every cell of every leg; worst single stop-the-world pause 7.3 ms (0.8) vs
8.0 ms (0.4) on the pinned cells. 0.8 collects *less* — it simply pushes
fewer messages, so it allocates less.

## Observations

- **A runtime regression, concentrated on the async-reply path.** The
  extra per-message cost is ~0.7–1 µs on `tell`/`ask-s` pinned, ~3 µs on
  shared `ask-s`, and **+11–13 µs on `ask`** — nearly the same absolute
  penalty on both dispatchers, which points at what `ask` does that the
  other modes don't: a future per message, completed from the receiver's
  side (lock + condvar + reply delivery), with the sender never waiting on
  it. The plain mailbox handoff (`tell`) is only mildly affected.
- **Not the collector.** GC share is lower in 0.8 than 0.4 in all six
  cells, compaction counts are identical (6–7 per cell), mark/sweep is
  zero, and the worst STW pause is shorter. Whatever got slower is
  mutator-side C on the message path.
- **Not bisected in this run.** 168 commits separate `cfd2bab` from
  `5b4bbf6`; the diff touches `vm.c` (+582 lines), `thread.c`,
  `builtins_thread.c` (per-thread `:stack-size`/`:vm-frames`, worker
  console), `mem.c`, and `package.c`/`symbol.c` (`cl_current_package` made
  per-thread, `7a0bf22`), plus Ctrl-C break-in (`8f9e85f`) and the CAS
  primitives (`5b4bbf6`). No new default-on environment knob or CFLAGS
  change explains it (checked). The pinned/ask cell is the right bisect
  probe: 27% effect against ~2% run-to-run spread, ~31 s per measurement
  after the cold load.
- **Speed 3 is still not the lever** — as in 0.3 and 0.4, the per-message
  path is dominated by C; a bytecode-level change of a few percent cannot
  produce or hide a 25% shift.
- **Cold-load MT soak at speed 3, three times over**: each leg
  cold-compiled sento 3.4.4 and its full dependency stack through the
  peephole at speed 3 and pushed ~10M messages across six heavily
  multi-threaded cells on the generational collector without incident, on
  both binaries.
- SHARED/tell's wall time (50 s for 6×5 s iterations vs ~31 s elsewhere)
  is the same queue-drain overhead the 0.4 entry describes: fire-and-forget
  senders outrun the 8 shared workers and each iteration finishes its
  backlog after the send window closes. It is unchanged across versions
  (50.3 s / 50.2 s / 50.6 s).
