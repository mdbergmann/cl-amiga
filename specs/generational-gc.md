# Generational GC: nursery + dirty-page tracking (host-only)

Status: DESIGN — implementation in phases (A: quick wins, B: nursery)
Branch: perf/tlab-alloc follow-up
Date: 2026-07-15

## Problem

At actor-workload allocation rates (sento: hundreds of MB/s of short-lived
message envelopes), the mark-sweep(-compact) collector spends most of its
time proving garbage is garbage:

- **Mark is O(live set)** — every cycle re-traverses the entire loaded world
  (quicklisp + sento ≈ tens of MB of symbols/bytecode/CLOS metadata that
  never dies), even though >95% of it is identical to the previous cycle.
- **Sweep is O(arena)** — a linear header walk over the whole bump region.
- **Compaction is O(heap)** — periodic full pauses (sweep-forever escape).

Measured with `ext:%gc-time-stats` (new; see below), reference sento matrix
config (M3 Ultra, `--heap 192M`, 8 workers / 8 load threads, 5s × 6 iters):

| Cell        | GCs | STW-wait | mark | sweep | compact | GC total | share |
|-------------|----:|---------:|-----:|------:|--------:|---------:|------:|
| pinned/tell |  44+5cp | 0.89s | 0.31s | 0.31s | 0.09s | 1.60s | **5.1%** |
| pinned/ask  | 163+1cp | 0.76s | 1.14s | 0.73s | 0.02s | 2.65s | **8.5%** |

Per cycle (ask): mark ≈ 7ms, sweep ≈ 4.5ms, STW-wait ≈ 4.6ms.  Two
conclusions:

1. Mark+sweep per-cycle cost is the bulk — a young-object story must bound
   *both* (mark by live-young, sweep by nursery size).
2. **STW-wait is a co-equal cost** (up to 18ms mean per stop in the tell
   cell).  Suspected major contributor: redundant back-to-back GCs — when N
   threads hit bump-exhaustion simultaneously, each one that loses the
   `gc_mutex` race spins, then runs its own full GC on the just-swept heap.
   Any scheme that collects MORE often (a nursery does) is eaten alive by
   this unless fixed first.

Smaller heaps shift these shares up sharply (GC frequency ∝ 1/free-space);
192M is the *favorable* case.

## Why the free list forces a moving design

The cheap alternative (sticky marks + minor sweep, non-moving) dies on the
allocator's steady state: once the bump front is exhausted, young objects
are recycled into free-list holes scattered across the entire arena.  Then:

- "young" has no address test (watermark broken by free-list allocs);
- pages mixing old objects and young holes become the common case, so
  dirty-page scanning degenerates to O(heap);
- protecting old space faults on every free-list allocation
  (~37k page faults/s at measured alloc rates — instant disqualification).

The fix is to make young allocation **always contiguous bump** and give the
bump space back every minor GC — i.e. a real evacuating nursery.  That is
"major surgery", but the surgery reuses the existing compaction machinery
(forwarding table, `gc_update_*` root updaters, slide, rehash) restricted
to the nursery range.

## Design: sliding nursery with promotion-by-watermark

Arena layout (single arena, CL_Obj stays an arena-relative offset):

```
[0 ......................... old_top) [old_top ............. arena_size)
            OLD SPACE                            NURSERY
   dense after major GC, grows                pure bump, always RW,
   upward by promotion, mprotect-RO           TLABs carve from here
```

- `old_top` (the watermark) = `cl_heap.bump` right after a major GC.
- **All** allocation (TLAB chunks, shared path, single-threaded, large
  objects) bumps inside the nursery.  The free list is unused between
  major GCs.  `alloc_from_free_list` survives only as the non-gen
  (Amiga / CL_NO_GENGC) path.
- Objects are born unmarked.  Old space holds the **sticky-mark invariant**:
  every live old object keeps its mark bit set between GCs, so the existing
  `gc_mark_obj` "skip if marked" check prunes traversal at the old
  generation for free — minor mark needs no new traversal code.

### Minor GC (nursery collection)

Triggered when the nursery bump front is exhausted.  Under STW:

1. Un-protect old space (one `mprotect`), harvest the dirty-page bitmap
   accumulated by the fault handler since the last GC.
2. **Mark young**: run the existing root enumeration (`gc_mark` — thread
   roots, registered/shared globals, FASL reader/writer hooks, …); sticky
   old marks terminate traversal.  Then scan each dirty old page
   **precisely** (crossing map, below) and `gc_mark_obj` every child
   reference that lands in the nursery.
3. **Forward**: compute forwarding addresses for marked nursery objects,
   sliding them down to `old_top` (fwd table sized to the nursery only).
4. **Update refs**: existing updaters (thread/registered/shared roots,
   srcloc, TLV) — `gc_forward` already returns non-nursery refs unchanged —
   plus: children of dirty old pages, children of the survivors themselves,
   EQ/EQL-hashtable rehash restricted to tables that are young or on dirty
   pages.
5. **Slide** survivors to `old_top`; finalize dead nursery objects
   (`gc_finalize_dead`) during the same walk; survivors keep their mark bit
   (they are old now).  `old_top += survivor_bytes`; extend the crossing
   map over the promoted run (sequential append).
6. Reset nursery bump to `old_top`; drop all TLABs (existing
   `gc_tlab_reset_all`); re-protect `[0, old_top)` RO (one `mprotect`);
   clear the dirty bitmap.

Cost model: O(roots) + O(dirty pages) + O(live young).  With single-digit-%
survival of a ~100MB nursery churn, expect ~2–4ms per minor vs ~11.5ms
mark+sweep today, and the allocator never leaves the bump fast path.

Skipped on minor cycles (documented, bounded): stream outbuf reclaim (the
mark-driven pin only sees traversed streams; old streams aren't traversed
in a minor cycle, so reclaim only runs on major GCs — a dead young stream's
outbuf lingers until the next major).

### Major GC

The existing full mark-sweep-compact, with two adaptations:

- **Clear all mark bits first** (one linear arena walk) — sticky marks
  would otherwise prune the full mark at the roots.
- After compaction: rebuild the crossing map (during the slide walk, free),
  set mark bits on all survivors (sticky invariant), reset `old_top = bump`,
  re-protect old space.

Triggers: nursery survivors don't fit above `old_top` (arena genuinely
filling), old space grown past a threshold (7/8 arena, reuse existing
`live_hwm` logic), minor-GC survival rate persistently high, or explicit
`(ext:gc)` / `(ext:gc-compact)` (always full — semantics unchanged).

### Dirty-page tracking (mprotect, no source-level write barrier)

The load-bearing invariant: *a page not written since the last GC cannot
contain a reference to an object allocated after it.*  Enforced by
hardware, not by auditing hundreds of store sites (the tier-4 audit found
~85 GC bugs in exactly that class of code — a manual write barrier across
the C runtime is not a survivable audit).

- Old space is `mprotect`-ed `PROT_READ` after every GC.  First store to a
  page → SIGSEGV (Linux) / SIGBUS (macOS) → handler validates the fault
  address is inside `[arena, arena+old_top)`, sets the page's dirty bit,
  `mprotect`s that page RW, returns; the store retries and succeeds.
  Non-arena faults chain to the previous handler / default (real crash).
- Dirty bitmap: 1 bit per 4K page (6KB per 192MB heap).  Handler is
  race-safe (idempotent bit set + mprotect; concurrent faults on one page
  are fine).  `__sync` atomics on heap words fault and retry the same way.
- Nursery is never protected → **allocation never faults**.
- The arena must be page-aligned: under CL_GENGC allocate it with `mmap`
  (host) instead of `platform_alloc`.

**EFAULT class (loud, enumerable):** syscalls that write *into* heap
buffers (file `read`, socket `recv`, …) return EFAULT instead of faulting
when the buffer page is RO.  Every such site (platform_* functions handed
pointers derived from heap objects) is preceded by `cl_gc_pretouch(ptr,
len)` (dirty + unprotect the range).  A missed site fails LOUDLY as an I/O
error (never corruption); platform read/recv error paths get an
`errno==EFAULT` hint message pointing at the pretouch audit.

### Crossing map (precise dirty-page scanning)

Per 4K page of old space: the arena offset of the first object that starts
in that page (sentinel for "spans whole page").  Old space is dense after a
major GC and only ever *appended to* (promotion), so:

- rebuilt once per major GC during the slide walk (no extra pass);
- appended incrementally as survivors are promoted (sequential);
- never touched by the mutator (old space doesn't get holes between majors
  — the free list is unused in gen mode).

Scanning a dirty page = start at the crossing-map entry (or walk back to
the previous page's entry for spanners), header-walk the overlapping
objects, visit each CL_Obj child slot via the existing
`GC_WALK_OBJ_CHILDREN` X-macro.  Precise, so it feeds both minor mark
(step 2) and minor ref-update (step 4).

### Interactions audit (each gets explicit handling + a test)

| Area | Handling |
|------|----------|
| TLABs | carve from nursery bump only; reset each GC (existing hook) |
| gc-stress (`CLAMIGA_GC_STRESS`) | forces a *minor* GC before every alloc + periodic major — unprotected-local bugs for young objects surface identically (moving nursery); existing stress suite must stay green |
| EQ/EQL hashtables | minor rehash restricted to young tables + tables on dirty pages (a table that gained a young key was written → dirty) |
| srcloc / TLV / outbuf side tables | forward via existing per-table hooks, ranged to nursery; outbuf reclaim majors-only |
| FASL reader/writer dedup tables | marked via existing hooks (young entries traced); forwarded on minor like registered roots |
| Weak semantics | unchanged (all existing weak behavior is mark-driven and works under minor marks) |
| `cl_mem_init` re-init | unprotect everything before teardown; reset bitmap/crossing map/old_top/handler state (documented stale-static bug class) |
| DEBUG_GC verify passes | adapted: minor-cycle variants check the nursery range; full-heap verifies run on majors |
| Amiga / m68k JIT | **compiled out entirely** (`CL_GENGC` requires POSIX; Amiga keeps mark-sweep-compact + JIT pinning untouched) |
| Kill switch | `CLAMIGA_GENGC=0` env → boot in classic mode (A/B benchmarking, field escape hatch) |
| Fallback | mmap/mprotect/sigaction failure at boot → classic mode with a loud one-line notice |

## Phase A — quick wins (independent, land first)

1. **GC-epoch dedup**: `cl_alloc` records `cl_heap.gc_count` before calling
   `cl_gc`; a thread that becomes STW initiator after the epoch advanced
   returns without re-collecting (re-attempts allocation first).  Explicit
   `(ext:gc)` still always collects.  Collapses the redundant-GC bursts
   behind the 18ms mean STW-wait.
2. **STW diagnostics**: extend `%gc-time-stats` era with per-stop max/mean
   STW-wait (already have total) to confirm/refute straggler theories.
3. Already landed with the profiling work: `ext:%gc-time-stats` (gc-count,
   compact-count, stw/mark/sweep/compact seconds) + `platform_time_us`.

## Phase B — the nursery (order of implementation)

1. mmap'd page-aligned arena + fault handler + dirty bitmap + pretouch API
   (+ unit tests: fault-dirties-page, pretouch, EFAULT paths, re-init).
2. Nursery bump allocation + `old_top` watermark + TLAB rebase (classic
   free-list path compiled out under gen mode); major-GC sticky-mark +
   clear-marks-first adaptations; crossing map build/append.
3. Minor GC passes (mark/forward/update/slide) + side-table forwarding +
   restricted rehash.
4. Policy: triggers, survival-rate accounting, major escalation.
5. Tests: unit (each pass), gc-stress minor mode, MT stability (10×),
   host fast tier + test-plus + test-extra, Amiga suite (feature absent
   but shared code paths touched), sento matrix A/B.

## Expected impact (honest)

- GC share of wall: 8.5% → ~2–3% on pinned/ask; 5.1% → ~2% on pinned/tell
  (plus whatever the epoch dedup recovers of the 2.8% STW-wait).  Net
  sento throughput: **+5–10%** on the reference matrix — bigger on
  allocation-heavy cells, much bigger on memory-constrained heaps
  (24–64M), where today's GC share is far above 8.5%.
- Allocation stays on the bump fast path permanently (no free-list probes,
  no sweep-forever regime, compaction pauses become rare majors).
- This is the foundation the "GC cost at allocation rate" problem actually
  needs; TLABs already removed the lock, this removes the per-cycle
  O(live)+O(arena) tax.
