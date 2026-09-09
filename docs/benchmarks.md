# Benchmark Results

Point-in-time performance measurements for future reference. Newest entries
first. Each entry records the commit, environment, exact reproduction
command, and results, so later runs can be compared like-for-like.

Related: [specs/performance.md](../specs/performance.md) is the optimization
*plan*; this file is the *measured results* log.

## 2026-09-09 — General workloads: clamiga vs ECL vs SBCL

**Context**: the first comparison that is not sento-shaped.
`trunk/bench-general.lisp` times 31 WORKLOADS — the Gabriel shapes (tak,
fib, deriv, n-queens), sorting and list processing, hash tables, strings and
string streams, the reader, the printer, FORMAT, fixnum / float / bignum
arithmetic, arrays, CLOS, structs, conditions, LOOP, allocation churn — and
runs unchanged on clamiga, ECL and SBCL.  Every row returns an integer that
depends on all of its work, printed as `val=`; the three implementations
must agree on it (they do, except for the bignum rows — see finding 5).
Master at `8d8e89b3` (0.9 + Tier 4 phases 1–3), `make host`.  ECL 26.5.5
(Homebrew, native `compile-file` output) and SBCL 2.6.8 (Homebrew, `--load`
compiles natively).  All three at `(optimize (speed 1) (safety 1) (debug
1))`, the setting third-party code is compiled with by default.

**Environment**: Apple M3 Ultra, macOS 26.6.2, `--heap 64M`; clamiga compiled
the file from source into a scratch FASL cache each run
(`CLAMIGA_FASL_CACHE_DIR`).  Each row is the minimum of 5 in-process
repetitions, and each implementation ran twice in separate processes, one
after the other on a quiet machine (the table takes the lower of the two;
clamiga's runs agree within 2% on every row).  A `CLAMIGA_FORCE_SPEED=3` run
matched speed 1 on every row.

| row | clamiga | ECL | SBCL | ECL/clamiga | SBCL/clamiga |
| --- | ---: | ---: | ---: | ---: | ---: |
| tak | 122.0 | 33.0 | 16.3 | 3.70× | 7.48× |
| fib | 87.0 | 44.2 | 15.3 | 1.97× | 5.69× |
| nqueens | 71.0 | 20.8 | 14.1 | 3.41× | 5.04× |
| deriv | 115.0 | 34.3 | 8.6 | 3.35× | 13.37× |
| list-sort | 108.0 | 123.2 | 68.2 | 0.88× | 1.58× |
| list-ops | 80.0 | 44.2 | 9.2 | 1.81× | 8.70× |
| hof | 71.0 | 35.3 | 8.2 | 2.01× | 8.66× |
| assoc | 213.0 | 7.8 | 12.4 | 27.31× | 17.18× |
| getf | 11.0 | 7.6 | 5.9 | 1.45× | 1.86× |
| vector-sort | 1054.0 | 6.4 | 3.0 | 164.69× | 351.33× |
| insertion-sort | 99.0 | 29.5 | 11.9 | 3.36× | 8.32× |
| matmul | 62.0 | 34.7 | 12.4 | 1.79× | 5.00× |
| mandel | 49.0 | 69.1 | 8.8 | 0.71× | 5.57× |
| float-vector | 59.0 | 49.7 | 0.7 | 1.19× | 84.29× |
| bignum-fact | 53.0 | 20.4 | 9.0 | 2.60× | 5.89× |
| bignum-arith | 33.0 † | 6.4 | 14.3 | (5.16×) | (2.31×) |
| hash-fixnum | 45.0 | 36.2 | 12.0 | 1.24× | 3.75× |
| hash-string | 77.0 | 9.0 | 2.9 | 8.56× | 26.55× |
| string-ops | 64.0 | 86.2 | 10.3 | 0.74× | 6.21× |
| string-stream | 65.0 | 89.9 | 16.9 | 0.72× | 3.85× |
| char-loop | 103.0 | 3.9 | 2.0 | 26.41× | 51.50× |
| format | 52.0 | 685.5 | 46.4 | 0.08× | 1.12× |
| reader | 84.0 | 53.7 | 32.5 | 1.56× | 2.58× |
| printer | 58.0 | 100.9 | 76.9 | 0.57× | 0.75× |
| clos-dispatch | 73.0 | 33.0 | 6.1 | 2.21× | 11.97× |
| make-instance | 84.0 | 61.7 | 0.4 | 1.36× | 210.00× |
| struct-bst | 59.0 | 34.1 | 8.5 | 1.73× | 6.94× |
| fixnum-loop | 170.0 | 85.0 | 12.9 | 2.00× | 13.18× |
| loop-collect | 84.0 | 26.9 | 10.9 | 3.12× | 7.71× |
| conditions | 61.0 | 339.7 | 10.2 | 0.18× | 5.98× |
| alloc-churn | 79.0 | 54.2 | 12.4 | 1.46× | 6.37× |
| **total** | **3445** | **2266** | **480** | **1.52×** | **7.18×** |
| **geomean** |  |  |  | **2.10×** | **8.16×** |

Milliseconds; a ratio above 1 means clamiga is slower.  † clamiga's
bignum-arith cell is not comparable (finding 5).

**Headline**: ECL is 1.52× faster in total and 2.10× by geometric mean, but
the mean is carried by three pathological rows.  Without vector-sort, assoc
and char-loop the geometric mean is 1.50× and the totals are at parity
(clamiga 2,075 ms vs ECL 2,248 ms), because ECL's FORMAT and condition
signalling are so slow.  The plain interpretive tax of the VM against native
code is 2–3.7× (fixnum-loop 2.0, fib 2.0, tak 3.7, nqueens 3.4, deriv 3.4,
insertion-sort 3.4, loop-collect 3.1).  clamiga beats ECL on seven rows:
format 13×, conditions 5.6×, printer 1.7×, mandel 1.4×, string-stream 1.4×,
string-ops 1.35×, list-sort 1.14×.  SBCL is 7–8× ahead overall; its
make-instance (210×) and float-vector (84×) rows are its constructor
optimisation and unboxed double-float arrays.

**Findings** (ranked by what a fix would buy):

1. **vector-sort 165×** — `bi_sort` (`builtins_sequence2.c`) sorts lists
   with a merge sort but vectors with `vector_insertion_sort`, O(n²) with a
   predicate call per comparison: 20,000 elements take 1,054 ms as a vector
   and 108 ms as a list.  `stable-sort` shares the path.  A merge sort for
   vectors (or sort-through-a-list) is the fix.
2. **assoc 27× while getf is 1.45×** — `lib/boot.lisp` overrides the C
   `ASSOC` with a Lisp function (keyword parsing, then `funcall #'eql` per
   pair) to add `:test-not`/`:key`; every ASSOC in user code pays ~34 ns per
   list element.  Same shape for RASSOC, SUBLIS, SUBST, NSUBST.  Delegating
   to the C builtin when no keyword is supplied (or a compiler macro) would
   recover the 27×.
3. **char-loop 26×** — `char` and `char=` are builtin calls, not opcodes;
   the loop body is six calls per character.
4. **hash-string 8.6×** — EQUAL hash tables with string keys, 10,000 keys,
   50,000 operations.
5. **bignum division is WRONG above 4,095 bits** — `bignum.c:238` divides
   through fixed 16-bit-limb scratch buffers (`u_buf[256], v_buf[128]`,
   commented "should be enough for practical use"); a dividend of 4,096 bits
   (128 32-bit words, `(expt 2 4095)` and up) or a divisor above 2,048 bits
   silently yields quotient 0 and remainder = dividend, and `gcd` inherits
   it.  `logand`/`logior`/`logxor` (`bignum.c:1864`) truncate at 2,048 bits
   the same way.  `(mod (factorial 600) 1000003)` is 0 instead of 471663;
   400! is still right.  Multiplication is unaffected (`integer-length` of
   1000! is the correct 8,530), so bignum-fact's timing is the intended
   workload and only its `val` is wrong; bignum-arith's `floor` bails out
   early, so that clamiga cell is not a measurement.
6. **bignum-fact 2.6×**, hash-fixnum 1.24×, alloc-churn 1.46×, matmul 1.8×,
   struct-bst 1.7×, clos-dispatch 2.2×: the runtime's C paths sit close to
   ECL; the rows dominated by bytecode do not.

**Reproduce**:

```
./build/host/clamiga --no-userinit --heap 64M --non-interactive --load trunk/bench-general.lisp
CLAMIGA_FORCE_SPEED=3 ./build/host/clamiga --no-userinit --heap 64M --non-interactive --load trunk/bench-general.lisp
ecl --norc --eval '(progn (load (compile-file "trunk/bench-general.lisp" :output-file "/tmp/bench-general.fas")) (quit))'
sbcl --non-interactive --no-userinit --no-sysinit --load trunk/bench-general.lisp
# Amiga: (defparameter cl-user::*bg-scale* 1/20) before loading
```

---

## 2026-09-09 — Tier 4 phase 3 landed: superinstructions, the peephole at every speed

**Context**: results for [specs/performance.md](../specs/performance.md) 4.3.
The peephole pass runs at every speed above 0 now (it was `speed >= 2`),
fuses fourteen adjacent opcode pairs and triples into one dispatch each,
and gained three rewrites on the way (the dead result-slot store before a
RET, a JMP whose target is a RET, a RETURN-FROM site returning in place).
Three binaries measured in the SAME session on a quiet machine: the item-5
tree (`17a20059`, a `git worktree`), the phase-3 tree with
`CLAMIGA_NO_FUSE=1` (the pass at speed 1 with its rewrites, no fusion —
"nofuse"), and the phase-3 tree.

**Environment**: Apple M3 Ultra, macOS 26.6.2, `--heap 64M` for the
microbench, `--heap 192M` for sento; every bench-prims run compiled the
file from source (the FASL cache entry was deleted before each run),
minimum of 5 interleaved runs per binary.

**Which opcode pairs to fuse** came from an adjacent-pair profile
(`PROFILE_OPCODES` builds count pairs now; `(clamiga::%op-counts-dump)`
prints the top 48), taken on the sento pinned/tell path at one producer,
4 s, before any fusion: 856.8M dispatches, 1,245 per message.  The top
fusable pairs — STORE→POP 10.8%, POP→LOAD 8.4%, LOAD→LOAD 5.2%,
LOAD→CALL_GLOBAL 4.8%, LOAD→RET 2.8%, LOAD→STRUCT_REF 2.7%,
LOAD→MV_RESET 2.6%, EQ→JNIL 1.9%, GLOAD→JNIL 1.6% — became the first nine
opcodes; `CONST; EQ; JNIL` from the plan does not occur on this path
(`GLOAD; EQ; JNIL`, the slot-protocol marker test, does).  A second
profile of the fused stream (1,044 per message) chose the five of round
two: LOAD→CONST 2.3%, GLOAD→CALL_GLOBAL 2.4%, GLOAD→EQ→JNIL 2.1%,
LOAD→STORE→POP 2.0%, POP→LOAD 1.8%.  After round two: 854.4M dispatches for 877k messages, **974 per message** (−22%).  The top remaining pairs are dynamic successors (JNIL→NIL, GLOAD_EQ_JNIL→LOAD_MV_RESET, a builtin call's result stored) or headed by a peeked STORE (STORE→GLOAD_EQ_JNIL 2.3%, STORE→CONST 1.7%) — the next round, if one is ever worth it, starts there.

**Per-primitive** (`trunk/bench-prims.lisp`, ABSOLUTE ns = net + the
empty-loop baseline).  The baseline itself moved: `(dotimes (i n) (setq
acc i))` is 25.0 → 22.0 (nofuse) → 16.0 ns per iteration, because its
body `LOAD i; STORE acc; POP` is one `LOAD_STORE_POP` now (the separate
7-run probe agrees: 25.8 → 17.3 ns), so about 9 ns of every row's delta
is loop overhead and the rest is the row's own.

| row | item 5 | nofuse | phase 3 | delta |
| --- | ---: | ---: | ---: | ---: |
| fixnum-add | 28 | 25 | 24 | −4 |
| call-1arg | 41 | 35 | 27 | −14 |
| call-3arg | 48 | 43 | 33 | −15 |
| call-&key-2of3 | 67 | 60 | 49 | −18 |
| call-&rest-2 | 67 | 60 | 51 | −16 |
| call-mvbind | 65 | 59 | 47 | −18 |
| flet-call | 50 | 43 | 42 | −8 |
| funcall-closure | 40 | 38 | 33 | −7 |
| closure-cell-incf | 59 | 53 | 45 | −14 |
| apply-3list | 59 | 53 | 45 | −14 |
| gf-1arg-1method | 76 | 64 | 57 | −19 |
| gf-2arg-2methods | 86 | 73 | 65 | −21 |
| gf-around+primary | 478 | 391 | 379 | **−99** |
| gf-call-next-method | 486 | 389 | 381 | **−105** |
| accessor-read | 31 | 28 | 23 | −8 |
| accessor-write | 35 | 33 | 28 | −7 |
| slot-value-read | 62 | 56 | 39 | −23 |
| slot-value-write | 61 | 59 | 48 | −13 |
| with-slots-incf | 136 | 127 | 94 | **−42** |
| struct-read | 27 | 24 | 22 | −5 |
| struct-write | 28 | 26 | 26 | −2 |
| struct-push-pop | 61 | 61 | 56 | −5 |
| make-instance-2init | 2,752 | 2,200 | 2,180 | **−572** |
| make-struct-2init | 52 | 51 | 37 | −15 |
| cons | 32 | 28 | 24 | −8 |
| list-4 | 65 | 60 | 55 | −10 |
| closure-alloc | 41 | 38 | 28 | −13 |
| special-read | 27 | 23 | 20 | −7 |
| special-bind | 37 | 34 | 28 | −9 |
| handler-case | 54 | 46 | 37 | −17 |
| handler-bind | 51 | 41 | 32 | −19 |
| unwind-protect | 80 | 68 | 61 | −19 |
| catch-throw | 55 | 49 | 43 | −12 |
| typep-class | 51 | 48 | 39 | −12 |
| case-keyword | 66 | 57 | 44 | −22 |
| gethash-eq | 39 | 36 | 29 | −10 |
| svref | 47 | 44 | 37 | −10 |
| lock-acquire-release | 85 | 80 | 70 | −15 |
| lock+condvar-notify | 99 | 92 | 81 | −18 |

No row regressed.  The "nofuse" column separates the two halves: the
rewrites alone (and the old peephole finally running on default-speed
code) take the CLOS rows most — `gf-around+primary` 478 → 391 and
`make-instance-2init` 2,752 → 2,200 are all rewrites, the dispatch and
CLOS code being full of RETURN-FROM sites and block results that used to
cost a store, a jump and a reload each — while the fusion takes the
loop-shaped rows: `slot-value-read` 56 → 39, `with-slots-incf` 127 → 94,
`case-keyword` 57 → 44, `closure-alloc` 38 → 28.

**Acceptance cells** — sento pinned/tell.  The single-producer cell
(`trunk/sento-bench-loadthreads.lisp`, `SENTO_LOAD_THREADS="1"`,
`:num-shared-workers 8`, `:duration 5`, `:num-iterations 6`), the
consumer's own number per the item-5 entry, two runs per binary,
alternating:

| | AVG msg/s | MEDIAN | MIN | MAX | GC share |
| --- | ---: | ---: | ---: | ---: | ---: |
| item 5, run 1 | 278,237 | 259,190 | 255,166 | 363,462 | 0.9% |
| item 5, run 2 | 285,595 | 269,744 | 264,015 | 368,289 | 0.9% |
| phase 3, run 1 | 368,352 | 355,421 | 333,897 | 444,975 | 0.8% |
| phase 3, run 2 | 370,798 | 358,162 | 329,632 | 455,591 | 0.8% |
| | **+30%** | **+33%** | +28% | +23% | |

Against the item-5 entry's median of record (271k): 357k, +32%.  The
4-producer cell of the phase-1/phase-2 entries (`trunk/profile-sento-bench.lisp`:
`:load-threads 4`, `:duration 15`, `:num-iterations 8`), the tier's gate
since 4.0 (175k there), one run per binary:

| | AVG msg/s | MEDIAN | MIN | MAX |
| --- | ---: | ---: | ---: | ---: |
| item 5 | 219,969 | 198,458 | 187,940 | 259,727 |
| phase 3 | 234,646 | 211,342 | 202,075 | 310,292 |
| | **+6.7%** | +6.5% | +7.5% | +19% |

That cell measures the message-box hand-off between several producers
and one consumer (see the item-5 entry), so it moves far less than the
consumer-bound cell above; it is reported for continuity with the earlier
phases, not as the phase-3 gate.

**Gates**: `make test` (incl. `tests/test_tier4_phase3.sh`, 78 checks,
and the 47 cases of `tests/test_peephole.c`), `make test-gc-stress` (which
found the latent C-frame NLX bug — see the commit — and now carries its
regression case), `make test-memleak`, `make -f Makefile.cross test-amiga` (fresh boot 4615/4616 — the one miss is the known FS-UAE-only `audio-short-sample-completes` — and restored image 4616/4616; a `--no-jit` run of the suite through the boot-override hook passes everything but the 74 checks that expect native code, so the fused VM is right on m68k independently of the walker).

**Reproduce**: `git worktree add --detach /tmp/base 17a20059 && (cd
/tmp/base && make host)`; per run `find ~/.cache/common-lisp -name
bench-prims.fasl -delete` then `clamiga --no-userinit --heap 64M
--non-interactive --load trunk/bench-prims.lisp`, alternating the three
binaries (`CLAMIGA_NO_FUSE=1` for the middle column), minimum per row;
the sento cells as named above; the pair profile with `make host
BUILDDIR=build/host-opprof DEBUG_FLAGS=-DPROFILE_OPCODES`, a warm-up
iteration, `(clamiga::%op-counts-reset)`, one 4 s iteration at one
producer, `(clamiga::%op-counts-dump)`.

---

## 2026-09-08 — Tier 4 phase 2, item 5: non-escaping local functions inlined

**Context**: the follow-up to the phase-2 commit closes its open items
([specs/performance.md](../specs/performance.md) 4.2): a `flet`/`labels`
function the compiler proves non-escaping is compiled into each call site
instead of a closure; the m68k JIT gets a template for the handler-case
opcodes; the `OP_CALL_GLOBAL` constant-pool check moves under `DEBUG_VM`.
Three binaries measured in the SAME session on a quiet machine: the
phase-2 tree (`563983cc`, a `git worktree`), that tree plus every change
except the `vm.c` gating ("novm"), and the full tree.

**Environment**: Apple M3 Ultra, macOS 26.6.2, `--heap 64M`; every run
compiled `trunk/bench-prims.lisp` from source (the FASL cache entry was
deleted before each run), minimum of 5 interleaved runs per binary,
absolute ns (baseline 25.0 ns in all three).  Rows that moved by 3 ns or
more:

| row | phase 2 | novm | item 5 | delta |
| --- | ---: | ---: | ---: | ---: |
| flet-call | 73 | 49 | 49 | **−24** |
| gf-around+primary | 476 | 464 | 462 | −14 (spread ±15) |
| gf-call-next-method | 484 | 472 | 466 | −18 (spread ±15) |
| make-instance-2init | 2,716 | 2,779 | 2,704 | −12 (spread ±60) |

Every other row is within ±2 ns; the call rows (`call-1arg` 40, `call-3arg`
48, `funcall-closure` 40) are identical across the three binaries, i.e. the
`DEBUG_VM` gating of the constant-pool check is not measurable on the host
(the 68020 saves three compares per interpreted global call).  `flet-call`
— `(flet ((h (x) (if x i x))) (h i))` — is now the body alone: no closure
allocation on entry, no frame, no `OP_RET`.  The three heavy CLOS rows
moved less than their run-to-run spread and nothing on those paths is a
`flet` (`defmethod` expands to a `named-lambda`; `call-next-method` is a
global function), so they are not attributed to this change.

**A measurement trap worth recording**: a first pass of this table showed
every row +5–6 ns except the empty loop.  It was contention — a unit-test
binary was being rebuilt and run on the same machine during runs 2–5 —
and it looked exactly like a layout regression.  A source-compiled and a
FASL-loaded run of the same file also time the same (checked as part of
the rerun), so "FASL-loaded code is slower" is not a thing either.

**What the sento pinned/tell cell is bound by** (the question the phase-2
entry below left open).  `trunk/sento-bench-loadthreads.lisp` runs the
cell over a producer-count sweep, everything else as in the matrix
(`:num-shared-workers 8`, `:duration 5`, `:num-iterations 6`, warm cache,
`--heap 192M`, this binary, quiet machine):

| producers (`:load-threads`) | AVG msg/s | MEDIAN | MIN | MAX | GC share |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 286,970 | 271,495 | 263,098 | 366,989 | 1.0% |
| 2 | 191,665 | 190,193 | 180,042 | 201,220 | 1.4% |
| 4 | 188,315 | 186,248 | 179,958 | 195,913 | 1.4% |
| 8 | 266,679 | 257,826 | 253,606 | 290,000 | 1.2% |

The rate does not grow with the producers — one producer already reaches
the highest rate, so the producer side is not the limit; the collector
is not either (1–1.4% of wall time, stop-the-world 0–52 ms per 31 s).
The consumer is: at one producer the actor thread handles a message every
~3.5 µs, which is what the phase-1 profile's ~1,450 bytecodes per message
cost at ~2.4 ns each — the message path is VM-bound, so phase 3's
superinstructions will move this number.  The dip at 2–4 producers (−35%)
is a queue hand-off effect, not VM work: with `:wait-if-queue-larger-than
10000` several producers alternately overfill and drain the one message
box, so the consumer sleeps and wakes on the condition variable per
burst; with one producer the queue stays populated, with eight it is
always full, and in both cases the consumer never sleeps.  This is
why the 4-producer configuration used as the phase-2 gate stayed flat
while the CPU time per run fell 10%: that cell measures the hand-off
pattern, not the consumer.  **Use `:load-threads 1` (or 8) as the phase-3
acceptance cell**; the 1-producer median (271k) is the consumer's number.

**Gates**: `make test` (incl. `tests/test_local_inline.sh`, 72 checks),
`make test-gc-stress`, `make test-memleak`, `make -f Makefile.cross
test-amiga` (see the follow-up commit message for the counts).

**Reproduce**: `git worktree add --detach /tmp/base 563983cc && (cd
/tmp/base && make host)`; then per run `find ~/.cache/common-lisp -name
bench-prims.fasl -delete` before `clamiga --no-userinit --heap 64M
--non-interactive --load trunk/bench-prims.lisp`, alternating binaries,
and take the minimum per row.

---

## 2026-09-08 — Tier 4 phase 2 landed: call and unwind protocol

**Context**: results for [specs/performance.md](../specs/performance.md) 4.2
(items 1, 2, 3, 4, 6, 7; item 5 — direct calls for non-escaping local
functions — is not in this commit).  Both binaries built with `make host`
from the same tree state; the "before" is a `git worktree` of the phase-1
tree (`80e4b838`), built and measured in the SAME session.

**Environment**: Apple M3 Ultra, macOS 26.6.2. `--heap 192M` for sento,
`--heap 64M` for the microbench.

**Per-primitive** (`trunk/bench-prims.lisp`, ABSOLUTE ns — the empty-loop
baseline included, since it moved by 2 ns between the two runs — minimum
of 5 runs per row).  Rows that moved by 3 ns or more:

| row | before | after | delta |
| --- | ---: | ---: | ---: |
| call-1arg | 51 | 43 | −8 |
| call-3arg | 58 | 51 | −7 |
| call-&key-2of3 | 79 | 71 | −8 |
| call-&rest-2 | 77 | 67 | −10 |
| call-mvbind | 78 | 70 | −8 |
| flet-call | 84 | 78 | −6 |
| gf-1arg-1method | 91 | 78 | −13 |
| gf-2arg-2methods | 99 | 87 | −12 |
| gf-around+primary | 539 | 485 | **−54** |
| gf-call-next-method | 541 | 490 | **−51** |
| accessor-read | 35 | 31 | −4 |
| slot-value-write | 67 | 64 | −3 |
| with-slots-incf | 149 | 142 | −7 |
| make-instance-2init | 3,284 | 2,844 | −440 |
| make-struct-2init | 98 | 55 | **−43** |
| list-4 | 70 | 64 | −6 |
| closure-alloc | 46 | 43 | −3 |
| handler-case | 118 | 55 | **−63** |
| handler-bind | 57 | 51 | −6 |
| unwind-protect | 123 | 81 | **−42** |
| catch-throw | 64 | 55 | −9 |
| typep-class | 54 | 51 | −3 |
| svref | 55 | 48 | −7 |
| lock-acquire-release | 134 | 89 | **−45** |
| lock+condvar-notify | 151 | 102 | **−49** |

No row regressed by 3 ns or more (the largest move up is `case-keyword`,
+2).  The empty loop is 26.0 ns before and after (`(dotimes (i n) (setq
acc i))`, 7 process runs each).

**Which item paid**: the fused `CALL_GLOBAL` and the OP_CALL slimming took
every call row 6–10 ns and the two plain gf rows 12–13; the setjmp-free
NLX frames and the value save stack took `unwind-protect` 123 → 81 and
both lock rows −45/−49 (`with-lock-held` is an unwind-protect); the
special-form `handler-case` took its row 118 → 55 (that row used to cons a
box and a closure and push three NLX frames per entry); the constructor
compiler macro took `make-struct-2init` 98 → 55; the list-based
`call-next-method` chains took the two `call-next-method` rows −51/−54.

**A layout trap, again**: the first build of the NLX work lost ~0.5 ns on
*every* dispatch (empty loop 26 → 31 ns) with no change to any hot handler.
The disassembly showed why: clang merges every `VM_DISPATCH` into one
shared indirect branch and materializes `thr + <offset>` there for each
per-thread field the handlers touch whose offset exceeds the load
immediate range; the 32 KB `dyn_stack` sat near the top of `CL_Thread`, so
every scalar after it (the NLX/handler/restart tops, `mv_count`, the
printer state) cost a stack reload plus an add per dispatch, and the NLX
work added two more.  Moving every large table to the end of the struct
recovered all of it (and is the reorder that ships).  See the comment on
the "Large per-thread tables" section of `thread.h`.

**Acceptance cell** — sento pinned/tell (`trunk/profile-sento-bench.lisp`:
`:load-threads 4`, `:duration 15`, `:num-iterations 8`, warm ASDF cache):

| | AVG msg/s | MEDIAN | MIN | CPU s per 122 s wall |
| --- | ---: | ---: | ---: | ---: |
| before (phase-1 tree) | 207,293 | 192,906 | 177,940 | 206.6 |
| after (phase 2) | 208,088 | 193,614 | 180,772 | 186.3 |
| | **+0.4%** | +0.4% | +1.6% | **−10%** |

The throughput did not move, while the CPU time burned per run dropped
10%.  A per-thread `sample` at steady state (10 s, after "BENCH STEADY
STATE BEGIN") shows no lock contention (`__psynch_mutexwait` ≈ 0 on every
thread) and several threads saturated in `cl_vm_run` — the consumer is
not the only busy thread, and the bench creates a fresh actor system per
iteration, so the busy set changes over the run.  What limits the rate
with this configuration (the producer side, allocation and stop-the-world
collections across 20+ threads, or the consumer) was not resolved here;
the per-thread breakdown is the place to start before gating phase 3 on
this cell.  A `:load-threads 1` variant would isolate the consumer.

**Gates**: `make test` (incl. `tests/test_tier4_phase2.sh`, 141 checks),
`make test-gc-stress`, `make test-memleak`, `make -f Makefile.cross
test-amiga` (fresh boot 4555/4556 — the one miss is the known FS-UAE-only
`audio-short-sample-completes` — and restored image 4556/4556).

**Reproduce**: as for phase 1 below (`git worktree add --detach /tmp/base
80e4b838`, min-of-5 bench-prims, the sento cell).

---

## 2026-09-08 — Tier 4 phase 1 landed: runtime taxes removed (+37.6% sento)

**Context**: results for [specs/performance.md](../specs/performance.md) 4.1
(the six runtime-tax items).  Both binaries built with `make host` from the
same tree state — the "before" column is a `git worktree` of the unmodified
`1e76a19d`, built and measured in the SAME session as the "after" column,
because this host drifts by more between sessions than several of these
items are worth.

**Environment**: Apple M3 Ultra, macOS 26.6.2. `--heap 192M` for sento,
`--heap 64M` for the microbench.

**Acceptance cell** — sento pinned/tell (`trunk/profile-sento-bench.lisp`:
`:load-threads 4`, `:duration 15`, `:num-iterations 8`, warm ASDF cache):

| | AVG msg/s | MEDIAN | MIN | µs per message (avg) |
| --- | ---: | ---: | ---: | ---: |
| before (`1e77a16d` tree) | 155,204 | 150,540 | 148,684 | 6.4 |
| after (phase 1) | **213,543** | 202,691 | 185,854 | 4.7 |
| | **+37.6%** | +34.6% | +25.0% | |

That is above the 20–25% the phase was scoped for.  ECL 26.5.5 remains the
reference at ~512k msg/s (2026-07-15 entry below); the gap is now ~2.4×
rather than ~2.9×.

**Per-primitive** (`trunk/bench-prims.lisp`, ns net of the empty-loop
baseline, **minimum of 5 runs** per row — a single run on this host varies by
up to 20% on the short rows, enough to hide or invent a 5ns change).  Only
rows that moved beyond ±2ns are listed; the full output is reproducible with
the command below.

| row | before | after | delta |
| --- | ---: | ---: | ---: |
| typep-class | 89 | 27 | **−62** |
| gf-2arg-2methods | 105 | 68 | **−37** |
| gf-call-next-method | 525 | 490 | −35 |
| with-slots-incf | 134 | 115 | −19 |
| gf-around+primary | 512 | 499 | −13 |
| lock+condvar-notify | 127 | 116 | −11 |
| call-&rest-2 | 55 | 47 | −8 |
| slot-value-write | 46 | 38 | −8 |
| lock-acquire-release | 109 | 101 | −8 |
| slot-value-read | 45 | 38 | −7 |
| list-4 | 49 | 42 | −7 |
| gf-1arg-1method | 69 | 63 | −6 |
| struct-push-pop | 39 | 33 | −6 |
| svref | 29 | 25 | −4 |
| cons | 8 | 5 | −3 |
| handler-case | 97 | 94 | −3 |
| make-instance-2init | 3,461 | 3,119 | −342 |

`typep-class` is now *below* ECL's figure for that row (51 net / 71 absolute).
The empty-loop baseline was 26 before and 25 after, so the absolute cost of a
row is `net + 25`.

**Which item paid**, from the incremental measurements taken as each landed:
`call_builtin` slimming moved every builtin-heavy row at once (svref 30→22,
gethash-eq 16→11, catch-throw 38→31, unwind-protect 98→87); the rooted cons
took `cons` 10→3 and `&rest` 58→47; the type code took `typep-class` 93→26;
the GF cache took `gf-2arg` 105→68; the slot cache took the `slot-value` and
`with-slots` rows.

**A layout trap worth recording**: the first version of the per-thread slot
cache sat in the middle of `CL_Thread` and cost a uniform ~3ns on *every*
call row — the VM's hot fields had been pushed onto different cache lines.
Moving the 1KB table to the end of the struct recovered all of it.  Same
class of sensitivity as the `vm.o`/LTO note in CLAUDE.md; the bench-prims
call rows are what catches it.

**Gates**: `make test`, `make test-gc-stress` (516/516), `make test-plus`
(host-cold-test 585/585), `make test-memleak` (5/5), and
`tests/test_tier4_phase1.sh` (34 checks, run in the fast tier and again under
`CLAMIGA_GC_STRESS=1`).

**Reproduce**:

```
# before/after microbench, minimum of N runs (single runs are too noisy)
for i in 1 2 3 4 5; do ./build/host/clamiga --no-userinit --heap 64M \
    --non-interactive --load trunk/bench-prims.lisp; done   # take the min per row
# acceptance cell
./build/host/clamiga --heap 192M --load trunk/profile-sento-bench.lisp
# a fair "before": build the unmodified commit in a worktree and measure it
# in the same session, not against a number from another day
git worktree add --detach /tmp/base <commit> && make -C /tmp/base host
```

---

## 2026-09-08 — Tier 4 baseline: sento vs ECL, profile shares, per-primitive costs

**Context**: baseline for [specs/performance.md](../specs/performance.md) Tier 4
("2× vs ECL").  Master at `1e76a19d` (0.9), `make host`.  ECL 26.5.5 from
Homebrew, sento 3.4.4 local checkout in both.

**Environment**: Apple M3 Ultra, macOS 26.6.2, `--heap 192M` for sento,
`--heap 64M` for the microbench.

**sento pinned/tell** (`trunk/profile-sento-bench.lisp`: `:load-threads 4`,
`:duration 15`, `:num-iterations 8`, warm ASDF cache, default speed):

| | AVG msg/s | µs per message |
| --- | ---: | ---: |
| clamiga | **174,597** (dev 13,258) | 5.7 |
| ECL (2026-07-15 entry below, 8 load threads) | 512k | 2.0 |

ECL could not be re-run today: its `ql:quickload` of the bench dependencies
fails compiling the current serapeum (`level0/hash-tables`), so the July figure
stands.  sento itself loads and compiles on ECL.

**Recompile** `(asdf:load-system "sento" :force t)`, warm dependency cache:
clamiga **0.39 s**, ECL **15.06 s**.

**Profile** (`sample` 25 s at steady state, on-CPU samples ≈ 38.7k): `cl_vm_run`
self 26,646 (**69%**); `call_builtin` self 1,880 + its `cl_symbol_name` 323 +
`platform_stack_headroom` 190 + TLS 120 (~7%); `bi_gethash` inclusive 2,302
(6%); `struct_slot_resolve` inclusive 2,179 of which `pthread_rwlock_rdlock`
729 (6%); `cl_symbol_value` 617; `typep_symbol` → `strcmp` 596; `cl_alloc`
496; `cl_gc_push_root`/`pop_roots` 719 (of which 223 under `cl_cons`);
`bi_gf_ic_emf` → `cl_intern_in` 73.

**Opcode counts** (`make host BUILDDIR=build/host-opprof
DEBUG_FLAGS=-DPROFILE_OPCODES`, pinned, 2 load threads, 4 s, 501,405 messages):
727,953,971 ops = **1,452 per message**, 138 calls per message
(CALL 54.9M, TAILCALL 13.1M, APPLY 1.5M).  Top rows: LOAD 18.9%, POP 12.4%,
STORE 10.8%, FLOAD 8.7%, CALL 7.5%, JNIL 5.7%, GLOAD 5.0% (72/message), CONST
4.6%, MV_RESET 3.3%, RET 3.0%, STRUCT_REF 2.5%, ASSERT_TYPE 0.74%
(10.8/message).

**Per-primitive cost** (`trunk/bench-prims.lisp`, 1M iterations per row, ns
net of the empty-loop baseline: clamiga speed 1 = 30.0, speed 3 = 24.0,
ECL = 20.1; ECL rows are native `compile-file` output):

| row | clamiga s1 | clamiga s3 | ECL |
| --- | ---: | ---: | ---: |
| fixnum-add | -1 | 3 | -5 |
| call-1arg | 18 | 20 | -7 |
| call-3arg | 27 | 31 | -8 |
| call-&key-2of3 | 44 | 48 | 3 |
| call-&rest-2 | 56 | 59 | 13 |
| call-mvbind | 50 | 54 | -9 |
| flet-call | 54 | 55 | -10 |
| funcall-closure | 14 | 20 | -6 |
| closure-cell-incf | 32 | 36 | -7 |
| apply-3list | 30 | 34 | 5 |
| gf-1arg-1method | 68 | 70 | 24 |
| gf-2arg-2methods | 104 | 111 | 31 |
| gf-around+primary | 529 | 551 | 79 |
| gf-call-next-method | 521 | 534 | 79 |
| accessor-read | 4 | 10 | -4 |
| accessor-write | 5 | 11 | -3 |
| slot-value-read | 45 | 47 | 1 |
| slot-value-write | 43 | 49 | 5 |
| with-slots-incf | 134 | 131 | 22 |
| struct-read | -1 | 1 | -8 |
| struct-write | 1 | 2 | -11 |
| struct-push-pop | 39 | 41 | 10 |
| make-instance-2init | 3,526 | 3,580 | 1,564 |
| make-struct-2init | 72 | 70 | 34 |
| cons | 8 | 13 | -4 |
| list-4 | 52 | 54 | 25 |
| closure-alloc | 20 | 24 | 21 |
| special-read | -1 | 1 | -12 |
| special-bind | 8 | 12 | -9 |
| handler-case | 104 | 98 | 52 |
| handler-bind | 29 | 30 | 20 |
| unwind-protect | 93 | 94 | 2 |
| catch-throw | 37 | 41 | 1 |
| typep-class | 88 | 95 | 51 |
| case-keyword | 40 | 40 | -10 |
| gethash-eq | 14 | 18 | -11 |
| svref | 30 | 32 | -11 |
| lock-acquire-release | 117 | 115 | 44 |
| lock+condvar-notify | 131 | 139 | 43 |

Negative values are loop-overhead noise (the row is free).  Speed 3 changes no
row beyond noise.

**Reproduce**:

```
./build/host/clamiga --no-userinit --heap 64M --non-interactive --load trunk/bench-prims.lisp
CLAMIGA_FORCE_SPEED=3 ./build/host/clamiga --no-userinit --heap 64M --non-interactive --load trunk/bench-prims.lisp
ecl --norc --eval '(progn (load (compile-file "trunk/bench-prims.lisp" :output-file "/tmp/bench-prims.fas")) (quit))'
# profile: run trunk/profile-sento-bench.lisp in the background, wait for
#   "BENCH STEADY STATE BEGIN", then: sample <pid> 25 -file out.sample
# opcode counts: PROFILE_OPCODES build, (clamiga::%op-counts-reset) before and
#   (clamiga::%op-counts-dump) after one run-benchmark call
```

---

## 2026-08-30 — 0.8 regression root causes: per-thread break-poll counter + vm.o without LTO

**Context**: [sento-bench-results-0.8.md](sento-bench-results-0.8.md) found
master 9–27% below a same-session 0.4 binary on every sento cell. Three
bisects (all on deterministic micro probes, not on the noisy sento cell):

1. `8f9e85f` — the Ctrl-C poll counter was a process-wide `static` bumped on
   every `OP_CALL`/backward `OP_JMP` → all threads bounce one cache line.
   **Fixed**: counter in `CL_Thread`.
2. `d467727` — an LTO code-generation artifact in the giant `cl_vm_run`:
   every opcode 15–25% slower single-threaded with identical bytecode.
   **Mitigated**: `vm.o` built with `-fno-lto` (Makefile), rest of the
   runtime keeps LTO.
3. `d467727` — a residual layout effect on the local-variable loops that
   survives no-LTO; structural (one giant function), left as is.

**Environment**: Apple M3 Ultra, macOS 26.6.2, `make host`.  Probes: 8
threads each running a call loop / special-bind loop (wall ns per op);
bench-opt rows (ms, best of 3, `--heap 64M`).

| Probe                      | 0.4 (`cfd2bab`) | master before | + counter fix | + vm.o no-LTO |
| -------------------------- | --------------: | ------------: | ------------: | ------------: |
| 8-thread call loop (ns/op) |  6.8 |  27–29 | **7.5** | 6.8 |
| 8-thread special-bind      |  7.5 |  25–27 | **8.1** | 7.6 |
| vm.local-shuffle (ms)      |   47 |     56 |    56 | **52** |
| vm.fixnum-loop             |   58 |     66 |    66 | **60** |
| vm.call-return             |   56 |     61 |    61 | **54** |
| mt.call-x8 (new row)       |    — |      — |    64 | **57** |
| mt.dynbind-x8 (new row)    |    — |      — |    48 | **44** |

sento matrix (msg/s, cold speed-3 cache): pinned/ask 24,256 → **28,976**
(0.4: 33,239), shared/ask 19,191 → **22,302** (25,466), pinned/tell
169,018 → 175,227 (191,254); full table in the 0.8 results doc.

**Guards**: the `mt.*` rows are the 8-thread twins of `vm.call-return` /
a dynamic-binding loop — a shared write on the call path shows as 3–4×
the single-thread figure (healthy: within ~1.5×). After touching `vm.c`,
compare the `vm.*` rows against this table.

**Reproduce**: `echo '(quit)' | ./build/host/clamiga --heap 64M --load
trunk/bench-opt.lisp`; sento: `trunk/sento-bench-matrix.lisp`.

---

## 2026-07-15 — Generational GC (host): sliding nursery + dirty-page tracking

**Branch**: `perf/gengc` (on top of the TLAB branch).  Design:
[specs/generational-gc.md](../specs/generational-gc.md).  Host collections
are now mostly MINOR cycles that trace only live young objects (survivor
list collected during mark — dead nursery space is never walked) plus the
old-space pages actually written since the last GC (mprotect write-watch);
survivors slide onto the old-space watermark.  Majors are full compactions.
Also in this round: GC-epoch dedup of redundant back-to-back stop-the-world
collections, and minor-first reclamation for the bounded lock/condvar/
thread handle tables (their exhaustion path used to force a full
collection — the dominant GC cost of the ask benchmark).

**Environment**: Apple M3 Ultra, macOS 26.5.2, `make host`, `--heap 192M`.
Benchmark: `sento.bench::run-benchmark` (`:num-shared-workers 8,
:load-threads 8, :duration 5, :num-iterations 6`).  A/B on the SAME binary
via `CLAMIGA_GENGC=0` (classic collector, incl. the epoch dedup).

Full 6-cell matrix (single session per collector, cells back-to-back;
GC share = total collector time / cell wall time from the telemetry):

| Cell (avg msg/s) | classic | GC share | gengc | GC share | delta |
| ---------------- | ------: | -------: | ----: | -------: | ----: |
| PINNED tell      | 178,503 |  2.2% | 180,859 | 1.3% | +1.3% |
| PINNED ask-s     |  76,904 | 21.5% | **91,230** | 3.6% | **+18.6%** |
| PINNED ask       |  28,081 | 11.0% | **31,238** | 2.0% | **+11.2%** |
| SHARED tell      |  26,949 |  2.5% | **31,531** | 3.8% | **+17.0%** |
| SHARED ask-s     |  45,690 |  6.2% | **51,233** | 1.7% | **+12.1%** |
| SHARED ask       |  18,305 |  6.2% | **24,601** | 1.4% | **+34.4%** |

(An earlier 2-cell run measured pinned/tell 164.2k→186.1k (+13%) and
pinned/ask 25.8k→32.5k (+26%) — run-to-run variance on the tell cell is
substantial; the paired A/B within one session is the meaningful signal.)

Shape of the win: minors run at ~1.8–6ms (126 minors vs 139 full sweeps in
the pinned/ask cell; full compactions dropped 128 → 6 once the lock/condvar
table exhaustion stopped forcing them).  Worst single stop-the-world pause
dropped 52ms → 8.6ms.  The reply-mode cells gain the most — they cons a
lock+condvar per future, which both churns the handle tables (classic: a
full sweep every ~0.25s, up to 21.5% of wall) and produces exactly the
short-lived garbage a nursery reclaims for free.

**Reproduce**: load `trunk/load-sento-bench.lisp`, then run
`sento.bench::run-benchmark` with the config above (per cell:
`:with-reply-p`/`:async-ask-p`); read `(ext:%gc-time-stats)` /
`(ext:%gengc-stats)` around the run; A/B with `CLAMIGA_GENGC=0`.

---

## 2026-07-14 — condition-hierarchy + deftype table hash indexes (TYPEP)

**Commit**: on top of `fd52e4a`. **Env**: macOS host, `make host` (-O3).

TYPEP on a symbol type spec probes the struct registry (indexed since
spec 3.1), then `cl_is_condition_type` and `cl_get_type_expander` — both
were linear alist walks, so every symbol TYPEP paid O(registered
conditions + deftypes) even when the answer was "no".  Real-world impact
(eta-hab on linux-arm64, diagnosed via `CLAMIGA_LOCK_DIAG` + in-container
gdb): the print-object hook runs one TYPEP per printed node, so a single
log4cl log line printing an actor/queue graph held the serialized appender
lock for **~60s per appender**, stalling main/watcher/timer threads 1–2
minutes per log event during the item-definition phase.  Both tables now
use the generic `CL_AlistIndex` open-addressing index (compiler.h), same
protocol as the struct registry index.

Repro: `./build/host/clamiga --heap 64M --load trunk/bench-opt.lisp`
(bench registers 100 conditions + 100 deftypes first).

| Benchmark (ms, best of runs) | before | after | delta |
|---|---|---|---|
| type.typep-cond-deftype | 40 | **15** | 2.7× |
| struct.typep (control, already indexed) | 16 | 16 | — |

The micro-bench understates the field case: eta-hab registers several
hundred condition types (every loaded library's `define-condition`s), and
the walk cost scales with that count while the indexed probe stays O(1).

## 2026-07-11 — MT publication barrier: no sento throughput cost (A/B)

**Commit**: `f45a91a` (barrier `17dee11` + CLOS registry sync `f45a91a`).

Regression check after the arm64 publication-barrier fix, which issues
`platform_memory_barrier()` before every struct-slot store when threads are
live — sento's message path is the most barrier-dense MT workload we have.
Controlled A/B: two binaries identical except the four barrier sites
(`17dee11` applied vs reverted), interleaved runs (barrier / no-barrier /
barrier / no-barrier) so thermal and session drift cancel, warm speed-3
ASDF cache, otherwise the docs/sento-bench-results-0.3.md long-run config
at 30 iterations (`:dispatcher :pinned`, `:with-reply-p nil`,
`:load-threads 8`, `:duration 10`).

**Environment**: Apple M3 Ultra, macOS 26.5.2, `--heap 192M`,
`CLAMIGA_FORCE_SPEED=3`.

| Leg (msg/s AVG)   | barrier | no-barrier |
| ----------------- | ------: | ---------: |
| Round 1           | 144,378 |    143,076 |
| Round 2           | 144,910 |    146,141 |
| **Mean**          | **144,644** | **144,608** |

Dead even (+0.02%, standard error ≈ 1.1k), and both variants sit at the
0.3-documented speed-3 ceiling (145,592) — the sync-table CLOS registries
(loaded by both binaries) cost nothing either, as expected (registries are
not on the per-message path).

Method note: a plain sequential rerun of the full 0.3 protocol first showed
an apparent −5–9% across the board; the interleaved A/B exposed it as
session drift (the suspect legs ran after ~45 min of sustained all-core
load). For future regression checks, interleave the builds under test —
same-session sequential runs of this benchmark drift by more than the
effect sizes of interest.

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

---

## 2026-07-10 — Spec 1.8: bytecode peephole post-pass (speed >= 2)

**Branch**: `feat/peephole-speed2`. Host, Apple M3 Ultra, `make host`,
`--heap 64M`, `trunk/bench-opt.lisp` best-of-3, pure bytecode, `fails=0`
in both runs. Comparison: default (`speed 1`, pass off) vs
`CLAMIGA_FORCE_SPEED=3` (pass on for every compile). The vm.* rows are
the pass's target workloads (store/load traffic, call-return around
tight loops); the set./clos./struct./alloc. rows are dominated by C
builtins and move little, as expected.

| Benchmark | speed 1 | forced speed 3 | delta |
|---|---|---|---|
| vm.fixnum-loop | 58 ms | 53 ms | -8.6% |
| vm.local-shuffle | 49 ms | 45 ms | -8.2% |
| vm.call-return | 57 ms | 50 ms | -12.3% |
| opt.dead-branch | 35 ms | 32 ms | -8.6% |
| safety1.svref-loop | 44 ms | 39 ms | -11.4% |
| safety1.call-args | 51 ms | 46 ms | -9.8% |

**Reproduce**:

```
./build/host/clamiga --no-userinit --heap 64M --non-interactive --load trunk/bench-opt.lisp
CLAMIGA_FORCE_SPEED=3 ./build/host/clamiga --no-userinit --heap 64M --non-interactive --load trunk/bench-opt.lisp
```

---

## 2026-07-15 — TLAB: per-thread allocation buffers (host, MT)

**Branch**: `perf/tlab-alloc`.  With more than one thread registered,
every `cl_alloc` used to serialize on the global `alloc_mutex`; at actor
message rates the mutex handoff dominated the per-message cost.  Each
thread now cuts objects lock-free from a private chunk (TLAB, default
32K, `CLAMIGA_TLAB_CHUNK` overrides, compiled out on Amiga) refilled
from the shared bump front / free list under the mutex once per chunk.
Uncut chunk remainders stay formatted as walkable free-block holes;
every GC cycle drops all TLABs during stop-the-world.

**Environment**: Apple M3 Ultra, macOS 26.5.2, `make host`,
`--heap 192M`, default speed.  Benchmark: `sento.bench::run-benchmark`
matrix as in [sento-bench-results-0.3.md](sento-bench-results-0.3.md)
(`:num-shared-workers 8, :load-threads 8, :duration 5,
:num-iterations 6`).  Baseline = the speed-1 column measured there
(commit `360abd4`).

| Dispatcher | Reply mode | baseline | TLAB | delta |
| ---------- | ---------- | -------: | ---: | ----: |
| PINNED     | tell       | 142,836 | **188,711** | +32% |
| PINNED     | ask-s      |  62,644 |  78,259 | +25% |
| PINNED     | ask        |  13,387 | **27,308** | +104% |
| SHARED     | tell       |  22,249 |  27,420 | +23% |
| SHARED     | ask-s      |  25,073 | **46,758** | +86% |
| SHARED     | ask        |  10,754 |  14,848 | +38% |

- The allocation-heaviest cells gained the most: `ask` allocates a
  future/promise per message and doubled on the pinned dispatcher;
  `shared/ask-s` (+86%) pays queue-handoff plus reply allocation.
- SHARED/`ask` is the noisiest cell (dev ±5.6k on 14.8k); treat its
  delta as indicative.
- Cross-implementation context (same machine, same config, measured
  2026-07-15): SBCL 2.6.5 pinned/tell 1.44M msg/s, ECL 26.5.5 512k.
  The TLAB moves clamiga from 1/10 to ~1/7.6 of SBCL on that cell.

**Reproduce**: run the 6-cell matrix via
`sento.bench::run-benchmark` after `(load "trunk/load-sento-bench.lisp")`;
A/B by setting `CLAMIGA_TLAB_CHUNK=0` (disables TLABs at runtime).

## 2026-08-23 — Raw OS binding modules: heap per `require` (host + FS-UAE)

`specs/raw-bindings-footprint.md`: a generated binding module
(`lib/amiga/raw/*`) used to materialise every function, constant and
struct field at load; Phase 1 replaced the wrappers with 20-byte FFI
stubs, Phase 2 ships the module as one packed binding table and builds a
name on first reference.  Heap delta of `(require m)` after `(ext:gc)`
(`ROOM`), FASL-loaded, no docstrings:

| module           | eager   | Phase 1 (stubs) | **Phase 2 (table)** | FS-UAE P1 | **FS-UAE P2** | FASL P1 → P2 |
|------------------|--------:|----------------:|--------------------:|----------:|--------------:|-------------:|
| raw/exec         | 216 KB  |   106 KB        |   **37 KB**         |  90 KB    |   **24 KB**   | 151 → 25 KB  |
| raw/dos          | 306 KB  |   140 KB        |   **34 KB**         | 135 KB    |   **34 KB**   | 197 → 35 KB  |
| raw/intuition    | 424 KB  |   218 KB        |   **50 KB**         | 210 KB    |   **50 KB**   | 301 → 53 KB  |
| raw/graphics     | 477 KB  |   232 KB        |   **55 KB**         | 223 KB    |   **55 KB**   | 294 → 58 KB  |
| the four         | 1.42 MB |   0.71 MB       |   **0.18 MB**       |           |               |              |

FS-UAE (68040/JIT config, `make -f Makefile.cross test-amiga`, the
`; raw-bindings:` lines of the suite log): 140–160 ms per module from
FASL; smaller modules (utility 5 KB, layers 8, gadtools 8, timer 2,
gadgets/button 4, classes/window 6).  Touching 150 intuition names on
the host adds 14 KB (~95 B per name).  `(clamiga::%binding-table-info
"AMIGA.RAW.INTUITION")` → 1485 entries in 47 KB, 23 symbols right after
load.

**Reproduce**: the appendix script of the spec on the host; on the
target the suite prints the per-module line itself
(`tests/amiga/test-raw-bindings.lisp`, `%raw-require`).
