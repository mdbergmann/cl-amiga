# Native Code Backend (m68k)

Future exploration: replacing or supplementing the bytecode VM with native m68k code generation.

## Motivation

- Eliminate VM dispatch overhead (switch/case loop per opcode)
- On 68020 at 14MHz, expect 3-10x speedup for compute-bound code
- Direct use of m68k registers (D0-D7, A0-A6) instead of memory-based value stack

## Tradeoffs

**Gains:**
- Significant performance improvement
- Direct register usage

**Losses:**
- Architecture portability — bytecode currently runs on host and Amiga unchanged
- Simplicity — the VM is ~1000 lines, easy to debug
- Would need separate PPC backend for PPC targets

## Empirical baseline (bouncing-lines)

Measured 2026-05-12 on the high-end FS-UAE config (A4000/68040/Picasso96
JIT, `verify.fs-uae`) using `examples/gfx/bouncing-lines.lisp` — five
2-point lines bouncing in a 592×382 GIMMEZEROZERO window, drawn via
`graphics.library` move/draw. Reference points:

- ACE BASIC (compiled, same algorithm): ~1900 FPS
- CL-Amiga (bytecode VM, current): ~500 FPS
- Hand-written ASM (estimated): higher still

### Frame-time breakdown

Microbenchmarked each phase in isolation for 500 iterations:

| Phase                       | ms total | ms/iter | % of frame |
|-----------------------------|---------:|--------:|-----------:|
| `step-line × 5` (pure Lisp) |      580 |   1.160 |    **63%** |
| `draw-line × 5` (10 FFI)    |      260 |   0.520 |        28% |
| `set-a-pen × 5` (FFI)       |       60 |   0.120 |         7% |
| `rect-fill` clear (FFI)     |       40 |   0.080 |         4% |
| `draw-fps` (FFI×4 + format) |      140 |   0.280 |          — |
| IDCMP poll                  |       60 |   0.120 |          — |
| **Full draw-frame**         |  **920** | **1.84**|  **100%**  |

Sum of measured parts (940 ms) ≈ measured whole (920 ms). The Lisp
portion is ~63% of per-frame cost; graphics.library FFI is the
remaining ~37%. No JIT can shrink the FFI portion — that floor is
shared with hand-written ASM.

### Opcode distribution (1 second of draw-frames)

Counted dispatched bytecodes during 511 frames (requires
`PROFILE_OPCODES` build flag; see "Profiling" below).
Total: 840 765 ops, ~1646/frame.

| Category                       |    Ops |     % | Notes                                  |
|--------------------------------|-------:|------:|----------------------------------------|
| **Stack/local traffic**        |482 466 | **57.4%** | `LOAD` 34.1%, `POP` 16.0%, `STORE` 7.2% |
| Function-call protocol         |100 268 |  12.0% | `FLOAD` 4.2%, `CALL` 3.9%, `RET` 3.8%  |
| Conditionals / short-circuit   | 93 006 |  11.1% | `NIL`+`JNIL` 8.2%, `JTRUE`+`DUP` 2.4%  |
| Struct slot access             | 63 875 |   7.6% | `STRUCT_REF` 5.2%, `STRUCT_SET` 2.4%   |
| Actual arithmetic / compares   | 52 101 |   6.2% | `ADD`/`LT`/`GT`/`LE`/`GE` fast-path    |
| Literals & MV bookkeeping      | 32 696 |   3.9% | `CONST`, `MV_RESET`                    |
| Amiga FFI calls                |  8 687 |   1.0% | `AMIGA_CALL` (17/frame, matches expected)|
| List iteration                 |  7 665 |   0.9% | `CAR`/`CDR`/`TAILCALL` (`dolist`)      |

### What this implies for codegen

- **Already optimal in bytecode**: fixnum arithmetic has inline
  fast paths (no function call), struct slot access compiles to
  dedicated `OP_STRUCT_REF`/`OP_STRUCT_SET` (no accessor frame), and
  Amiga FFI uses `OP_AMIGA_CALL` (single op, no marshalling thunk).
  No new opcode is going to move the needle.
- **57% of dispatched ops are pure stack housekeeping** — `LOAD` /
  `STORE` / `POP` shuffling values between the VM stack and "local"
  slots that *also* live on the VM stack. `step-line`'s 12 locals
  (`sx sy ex ey dx1 dy1 dx2 dy2 w h` + the rebound deltas) fit
  comfortably in 68k registers `D2–D7` + `A2–A3`. A register-allocating
  backend erases this category entirely.
- **Strictly irreducible** (must happen even in hand-tuned ASM):
  arithmetic + struct deref + FFI ≈ ~15% of current bytecode ops.
- **Trivially reducible by a register-allocating JIT**: stack
  housekeeping + most of the call protocol ≈ 65–70%.

### Projected ceilings per approach

If Lisp-portion time shrinks roughly proportionally with reducible
opcodes (a reasonable first-order estimate, since each remaining op
still pays dispatch but not stack traffic):

| Approach                            | Lisp ms | FFI ms | Total | FPS |
|-------------------------------------|--------:|-------:|------:|----:|
| Current bytecode                    |    1.16 |   0.68 |  1.84 | 500 |
| Template JIT (drops dispatch only)  |   ~0.70 |   0.68 | ~1.38 |~720 |
| Register-alloc JIT (drops stack)    |   ~0.35 |   0.68 | ~1.03 |~970 |
| Type-inferring JIT (drops tag chks) |   ~0.20 |   0.68 | ~0.88 |~1100|
| Hand-written ASM (reference)        |    n/a  |   n/a  |   n/a |~1900|

The FFI floor (~0.68 ms, dominated by `graphics.library` itself)
remains. Closing the gap to ACE BASIC is plausible only with a
register-allocating JIT and aggressive small-leaf inlining (`dolist`
body, struct accessors). Anything less leaves us well below ACE.

## Profiling

The opcode counter is in-tree and zero-cost in normal builds.

```
make -f Makefile.cross amiga DEBUG_FLAGS=-DPROFILE_OPCODES
make host DEBUG_FLAGS=-DPROFILE_OPCODES
```

From Lisp (in any build — dumps "not compiled in" when the flag is off):

```lisp
(clamiga::%op-counts-reset)
;; ... do work ...
(clamiga::%op-counts-dump)   ; prints to stdout, sorted by count
```

Implementation: `cl_op_counts_arr[256]` in `src/core/vm.c`, ticked once
per dispatched opcode via the `CL_OPCOUNT_TICK` macro (empty when the
flag is off). Builtins `%OP-COUNTS-RESET` / `%OP-COUNTS-DUMP` are
registered unconditionally.

## Approaches (simplest to hardest)

### 1. Template Compiler (bytecode → m68k) — Recommended

Keep the current S-expr → bytecode compiler. Add a second pass that translates each bytecode op to a fixed m68k instruction sequence ("baseline JIT").

- ~2-3K lines of C
- Each opcode becomes a canned instruction template
- Bytecode remains as fallback on host
- Simplest path, captures most of the speedup

### 2. Direct Native Compiler (S-expr → m68k)

Replace the bytecode compiler with one that emits m68k directly.

- ~5-8K lines of C
- Better code quality — can do register allocation, avoid unnecessary stack traffic
- Loses bytecode portability entirely

### 3. Optimizing Native Compiler

Add an intermediate representation (IR), SSA form, register allocator, instruction selection.

- 20K+ lines — what real Lisp compilers (SBCL, CCL) do
- Massive effort, not justified for this project scope

## Key Technical Challenges

### GC Root Finding

The VM makes this trivial — roots are on `cl_vm.stack[]`. With native code, live `CL_Obj` values sit in CPU registers and the native stack.

Options:
- **Conservative stack scanning**: treat anything that looks like a heap pointer as a root. Simpler but may retain garbage.
- **Precise stack maps**: compiler emits metadata saying which frame slots hold GC refs at each safe point. More work, fully correct.

### Calling Convention

- A7 = stack pointer (standard)
- A5 = arena base pointer (for fast heap access via `CL_OBJ_TO_PTR`)
- D0-D2 = first 3 arguments, rest on stack
- D0 = return value
- Caller-saved: D0-D2, A0-A1
- Callee-saved: D3-D7, A2-A5

### Closures

A closure becomes a code pointer + environment pointer. Accessing captured variables means loading through the environment pointer (in an address register).

### Tail Calls

Reuse the current stack frame. Requires careful frame layout — arguments must be repositioned in-place before the jump.

### Fixnum Arithmetic with Tags

Our tag scheme (bit 0 = 1 for fixnums) works well on m68k:

```
; ADD: (+ a b) — both tagged fixnums
move.l  (sp)+, d0       ; pop b (tagged)
move.l  (sp)+, d1       ; pop a (tagged)
subq.l  #1, d0          ; strip tag from b
add.l   d1, d0          ; add (result keeps a's tag)
move.l  d0, -(sp)       ; push result
```

About 4-5 instructions per fixnum op vs. VM dispatch overhead.

### Executable Memory

On AmigaOS, all memory is executable — no mmap/mprotect issues. Generated code can live in any allocated buffer.

## Implementation Sketch (for approach 1)

### File layout

```
src/
├── core/                       (unchanged callers; minor wiring only)
│   ├── types.h                 +2 fields on CL_Bytecode
│   ├── vm.c                    +1 entry check at function-call boundary
│   └── compiler.c              +1 hook after bytecode emission
└── jit/                        (new subdirectory)
    ├── jit.h                   public API
    ├── jit.c                   orchestration, code-buffer mgmt, entry trampoline
    ├── codegen_m68k.c          one emitter per opcode (the "templates")
    ├── codegen_m68k.h          shared emitter helpers / register conventions
    ├── asm_m68k.c              instruction encoders: emit_move_l, emit_jsr, ...
    ├── asm_m68k.h
    └── runtime.c               C helpers the emitted code calls into
```

Rough LOC budget for a working baseline:

- `asm_m68k.{c,h}`: ~600 — one function per instruction form we emit
- `codegen_m68k.{c,h}`: ~1500 — ~80 opcode templates, each 5–30 instructions
- `jit.{c,h}`: ~400 — code-buffer allocation, native entry, fallback
- `runtime.c`: ~300 — helpers for `CALL`, `MAKE_CELL`, GC safe-point, error throw
- `types.h` / `vm.c` / `compiler.c` deltas: <50 lines total
- Build glue: ~15 lines in `Makefile.cross`

≈ 3 KLOC, matches the original approach (1) estimate. Host build
stays untouched.

### Data-structure additions

```c
/* types.h — CL_Bytecode gets a native code pointer */
typedef struct {
    CL_Header hdr;
    uint8_t  *code;          /* bytecode (unchanged, always present) */
    /* ... existing fields ... */

    /* NEW: native code, NULL if not compiled or non-Amiga */
    uint8_t  *native_code;   /* platform_alloc'd, executable */
    uint32_t  native_len;
} CL_Bytecode;
```

The bytecode is never thrown away — it stays as a fallback (for the
debugger, for disassembly, for the host build, and for code paths the
JIT chooses not to compile such as functions containing `progv` or
unusual `unwind-protect` shapes during early bring-up).

### Integration points (3 of them, each tiny)

**1. Compile-time hook** (`compiler.c`, ~5 lines):

```c
/* end of cl_compile_defun, after bytecode is fully populated */
cl_jit_compile(bc);    /* no-op on host / when JIT disabled */
```

**2. Call-site dispatch** (`vm.c`, in `OP_CALL` handler, ~6 lines):

```c
if (bc->native_code) {
    /* enter native code with the same calling convention as the
     * runtime helper — args already on cl_vm.stack */
    result = cl_jit_invoke(bc, nargs);
    /* unwind frame, push result, dispatch next */
} else {
    /* existing bytecode call path */
}
```

**3. GC safe-point** — every backward branch and every call inside
emitted code emits a 3-instruction check against `gc_compact_pending`,
then a `JSR` to the slow-path runtime helper.

### Calling convention for emitted code

```
A7 = m68k stack pointer (standard)
A5 = arena base (= cl_arena_base; for fast CL_OBJ_TO_PTR)
A4 = pointer to current CL_Frame
A3 = cl_vm.stack + cl_vm.sp (top of VM stack — locals live below)
A2 = scratch / second operand pointer

D7..D5 = stack-cache (top 3 VM stack slots held in regs — see below)
D0     = return value (CL_Obj)
D1..D3 = scratch
```

**Important nuance**: even a "template" JIT can win much more if it
caches the top of the VM stack in registers between adjacent opcodes,
rather than literally emitting push/pop around every template. A
trivial three-slot register cache (D5/D6/D7) eats most of the
`LOAD`/`POP`/`STORE` traffic the opcode trace identified at 57%.
That's the first design choice to make — without it, the template JIT
captures only the dispatch saving (~720 FPS in the projection table),
with it the register-alloc row (~970 FPS) is reachable.

### Per-opcode emitter shape

Each opcode template is a C function that emits m68k bytes for that
op into the codegen buffer:

```c
/* codegen_m68k.c — example: OP_ADD with fixnum fast path */
void emit_op_add(CodeBuf *cb)
{
    /* Pop b, a from VM-stack-cache regs into D0, D1.
     * Assumes top-of-stack cached in D7, second in D6. */

    /* Fixnum fast path: both have tag bit set */
    emit_move_l(cb, REG_D7, REG_D0);      /* b */
    emit_move_l(cb, REG_D6, REG_D1);      /* a */
    emit_and_l_imm(cb, 1, REG_D0);
    emit_and_l_imm(cb, 1, REG_D1);
    emit_and_l(cb, REG_D1, REG_D0);
    Label slow = emit_beq_forward(cb);

    /* Inline fixnum add — strip a's tag, add untagged b */
    emit_move_l(cb, REG_D6, REG_D0);
    emit_subq_l_imm(cb, 1, REG_D0);
    emit_add_l(cb, REG_D7, REG_D0);
    /* overflow check, branch to slow on fixnum range exceeded */
    emit_move_l(cb, REG_D0, REG_D7);      /* result back to TOS-cache */
    Label done = emit_jmp_forward(cb);

    bind_label(cb, slow);
    /* Slow path: call cl_arith_add via runtime helper */
    emit_jsr(cb, (void *)cl_jit_runtime_add);

    bind_label(cb, done);
}
```

There is no dispatch loop. The native code for a function is one
straight-line sequence of these templates concatenated, with branches
between them resolving to native offsets — exactly the 57% of
dispatch overhead the opcode trace identified.

### Runtime helpers (the only thing emitted code calls)

`runtime.c` exposes C functions with stable ABIs that emitted code
calls via `JSR`:

```c
CL_Obj cl_jit_runtime_add(CL_Obj a, CL_Obj b);          /* slow path */
CL_Obj cl_jit_runtime_call(CL_Obj fn, int nargs);       /* generic Lisp call */
void   cl_jit_runtime_signal_type_error(CL_Obj v, ...); /* no return */
void   cl_jit_runtime_safepoint(void);                  /* GC + signals */
CL_Obj cl_jit_runtime_make_cell(CL_Obj v);
```

These are plain C, get the existing `-Os` inlining benefits, and form
the boundary at which generated code "leaves" the JIT world and
re-enters the existing C runtime. Every helper is callable from the
bytecode VM too — there's no duplicated logic.

### GC interaction (the hard part)

The bytecode VM has free GC root finding: everything live is on
`cl_vm.stack`. Native frames don't have that luxury.

**Starting strategy: conservative scanning of m68k registers + native
stack.** At each safe point, JIT-emitted code stashes all
callee-saved address registers (A2–A5 by convention) on the m68k
stack, then calls into the runtime. The collector walks the m68k
stack from current SP up to the saved `sys_stack_base`, and for each
4-byte aligned word checks `CL_HEAP_P(word) && in_arena(word)` — if
so, mark it. False positives retain garbage but never corrupt
anything *under a non-moving collector*.

**Caveat — this codebase has a moving compactor.** `cl_gc_compact()`
(`mem.c`) is a sliding compactor that runs as a fallback when normal
mark-and-sweep doesn't free enough space (`gc_compact_pending`).
Compaction *moves* live objects and rewrites every `CL_Obj` reference
to its new offset. Conservative scanning is unsafe under a moving
collector: if an m68k-stack word happens to be a coincidental integer
whose bit pattern matches a valid arena offset, the compactor would
"forward" that integer to the new offset, silently corrupting it.

Three options for handling this, in increasing implementation cost:

- **A. Suppress compaction while a JIT frame is on the C stack.**
  Increment a thread-local `jit_depth` counter on `cl_jit_invoke`
  entry, decrement on return. While `jit_depth > 0`, `cl_gc` skips
  the compact phase; allocator falls back to OOM if non-moving sweep
  can't free enough. Cheapest (~20 LOC). Correctness-safe. Cost is
  occasional avoidable OOM under fragmentation pressure while inside
  JIT'd code. JIT'd frames are short-lived in practice (graphics
  inner loops, REPL printer), so the window is small.

- **B. Pin conservatively-marked objects.** Add a pinned bit in the
  object header; conservative scan sets it; the compactor leaves
  pinned objects in place and slides unpinned ones around them.
  Lets compaction still happen, but partially defeats it and
  complicates the slide algorithm.

- **C. Precise stack maps.** Codegen emits per-safepoint metadata
  listing exactly which frame slots and which spilled registers hold
  `CL_Obj`. No false positives, compaction stays fully effective.
  Most code, fully correct, what the rest of this section ultimately
  gestures at.

**Landed (2026-05-15): A + offset validation; compaction inhibit removed.**

Option A as initially landed (suppress compaction while
`cl_jit_active_threads > 0`) hit the documented OOM cliff on the
Amiga test suite — fragmentation accumulated faster than non-moving
sweep could keep up, and 1814 of 2525 tests passed before
`cl_alloc` returned `Heap exhausted (requested 16 bytes)` with the
arena still at ~3.6 MB / 8 MB used.

Refinement: **two-phase conservative scan with header-offset
validation** in `mem.c::gc_scan_jit_native_stack`.

1. *Collect.* Walk the m68k stack window and gather candidate
   offsets (values that pass `CL_HEAP_P` and `< arena_size`) into
   a 256-entry stack-local buffer.  Overflow drops the excess and
   warns once — a correctness gap, not a corruption hazard.
2. *Validate-and-mark.* `qsort` the candidates; walk the arena
   bump-front by header size, and for each real header offset `X`
   binary-search the candidate buffer.  Only matches reach
   `gc_mark_obj`.  Phantom marks at non-object-start bytes are
   impossible.

With phantom marks ruled out, the *mark* phase is safe: a candidate
is either a real heap offset (marked → object retained) or a
coincidental integer (no header match → ignored).

**Superseded (the writeback pass had a corruption bug).**  The
initial landing paired the mark scan with a *forwarding writeback*
(`gc_forward_jit_native_stack`): after the slide, it rewrote every
matching C-stack slot to the object's new offset, so JIT-spilled
operand pointers wouldn't dangle.  The flaw: the writeback could not
tell a real spilled pointer from a **coincidental integer that
happens to equal a live object's arena offset** — a loop counter, a
size, a hash, a bucket index sitting anywhere on the C call stack
between the GC and the JIT entry.  It rewrote those too, corrupting
the value.  This is a *moving*-collector hazard the header-offset
validation does **not** prevent (validation only confirms the value
is a valid object start; a raw integer can be one).  It manifested as
a layout-roulette corruption — e.g. a class metaobject occasionally
failing to register (`(setf (find-class …) …)` silently lost), only
under the JIT, never on host, shifting with unrelated code size.

**Landed (option B — pinning):** the mark-phase scan now *pins* every
validated conservatively-referenced object (records its offset in the
sorted `jit_pinned[]` set), and `gc_compute_forwarding` keeps each
pinned object at its current offset (forwarding = identity).  Pinned
objects never move, so the JIT's spilled C-stack offsets stay valid
**with no writeback** — `gc_forward_jit_native_stack` was removed
entirely.  A coincidental integer that equals a live object's offset
now merely *pins* that object (harmless over-pinning, the
forwarding-side analogue of conservative over-retention) and is never
itself rewritten.  Because this is a Lisp2 sliding compactor, pinning
is cheap: when the address-ordered forwarding walk reaches a pinned
object the free pointer is always ≤ its offset (all live data below it
compacts into the space below it), so the pin stays put, leaving a gap
between the compacted run and the pin.  `gc_slide` fills each such gap
with valid free-block header(s) (`gc_make_free_gap`, mirroring
`gc_sweep`'s format) and threads them onto the free list — this is
*mandatory*: the arena's linear "walk by header size" would otherwise
desync on the stale bytes in the gap (observed as a `CAR: not a LIST`
corruption ~mid-suite).  The reclaimed gaps are immediately
reusable, so a pinned compaction lands in the same free-list + bump
state as a sweep.  Pins exist only while a JIT frame is live
(`jit_depth > 0`) and number only a handful (the live operand spills),
so the effect is small — unlike Option A (suppress compaction), which
hit the OOM cliff above.

`cl_jit_active_threads` survives as an informational counter; it no
longer gates compaction.  Host coverage:
`tests/test_jit_gc_scan.c` (`pinned_obj_does_not_move`,
`unpinned_obj_moves`).

Option C (precise stack maps) remains a future path if conservative
*retention* (not corruption) ever becomes a measurable problem —
typically it doesn't for this workload.

### Build wiring

```makefile
# Makefile.cross — JIT only built for m68k
JIT_SRC = $(SRCDIR)/jit/jit.c \
          $(SRCDIR)/jit/codegen_m68k.c \
          $(SRCDIR)/jit/asm_m68k.c \
          $(SRCDIR)/jit/runtime.c

ALL_SRCS = $(MAIN_SRC) $(PLATFORM_SRC) $(CORE_SRC) $(JIT_SRC)
CFLAGS  += -DJIT_M68K
```

Host `Makefile` stays as-is — `cl_jit_compile` becomes a one-line stub
in `compiler.c` guarded by `#ifndef JIT_M68K`. Lisp code never has to
care whether the JIT is present.

### Runtime opt-in

A command-line flag — `clamiga --no-jit` or env var `CLAMIGA_JIT=0` —
forces bytecode interpretation. Useful for:

- Debugging: compare bytecode vs JIT output of the same function
- Bisecting miscompiles: run the test suite both ways
- Profiling: see how much each phase actually wins

### What's deliberately not in this design

- **No IR, no SSA, no register allocator.** Each opcode template is
  independent and emits canned m68k. The only "allocation" is the
  three-slot stack cache, which is implicit in the templates.
- **No inlining across function boundaries.** A `CALL` is a real
  `JSR` to the callee's native entry. Inlining + leaf-fusion is what
  the *next* approach (direct native compiler) would add.
- **No tail-call optimization beyond the existing bytecode `TAILCALL`
  template.** That op already does the work; the template just emits
  the m68k equivalent.
- **No multi-arch generality.** The interface is m68k-shaped. A PPC
  port would mean writing a parallel `codegen_ppc.c` + `asm_ppc.c`
  pair, but the existing `jit.c` / `runtime.c` and integration points
  stay.

### Suggested staging

1. **Skeleton + 10 opcodes** (`CONST`, `LOAD`, `STORE`, `POP`, `RET`,
   `ADD`, `LT`, `JNIL`, `JMP`, `HALT`). Get one trivial function to
   round-trip through native code. ~2 weeks.
2. **Coverage**: add the remaining ~70 opcodes, one or two at a
   sitting. `tests/amiga` gets a `--jit` run alongside the existing
   run.
3. **GC**: precise enough conservative scan, safe-point check. Run
   the full Lisp test suite under JIT.
4. **Polish**: stack cache (D5/D6/D7), fixnum overflow templates,
   AmigaOS cache flush (`CacheClearU` after emit), native
   disassembler for debugging.

## Binary & memory footprint

The JIT has two distinct costs: a fixed-size hit to the executable,
and a variable-size hit to runtime memory that scales with how much
Lisp code gets compiled.

### Binary growth: modest (~+7%)

The JIT subsystem is ~3 KLOC of C, compiled once into `clamiga`.
With `-Os` for m68k (≈ 10–14 bytes per source line):

| Component             |  LOC | Est. `.text` |
|-----------------------|-----:|-------------:|
| `asm_m68k.c`          |  600 |     ~7–8 KB  |
| `codegen_m68k.c`      | 1500 |    ~18–22 KB |
| `jit.c`               |  400 |     ~5 KB    |
| `runtime.c`           |  300 |     ~4 KB    |
| **Total added**       | 2800 | **~35–40 KB**|

Current binary is 536 KB → ~+7%. Fixed cost regardless of how much
Lisp is JIT'd at runtime. Realistic upper bound is closer to 50–60 KB
once edge-case templates accumulate (`OP_CALL`, `OP_TAGBODY_GO`, and
NLX-related ops can each grow to 50+ lines). Still well under 10%.

### Runtime memory growth: this is the real concern

Native code is **4–8× larger than the bytecode it replaces**, because
each bytecode op (1–3 bytes) expands to 5–30 m68k instructions
(20–100+ bytes).

Projected hit if the JIT compiles everything on the standard
clamiga boot:

| What's loaded               | Bytecode size | Native (×6 avg) |
|-----------------------------|--------------:|----------------:|
| boot.lisp / CLOS / lib core |    ~60–100 KB |    ~360–600 KB  |
| Test suite / user code      |   ~100–300 KB |   ~600 KB–1.8 MB|
| Quicklisp + FSet            |   ~200–500 KB |     ~1.2–3 MB   |

On the 8 MB / 68020 target — the project's documented baseline —
compiling everything would consume a significant fraction of usable
RAM. On the 64 MB JIT FS-UAE target or a real 68060 with a RAM card
it's a non-issue. The 8 MB baseline is where the design has to be
careful.

### Mitigations (design in from day one)

1. **JIT opt-in, not opt-out.** Default to bytecode; require
   `(declaim (optimize (speed 3)))` or a `--jit-all` flag to compile
   aggressively. Inverts the `--no-jit` flag mentioned in the
   implementation sketch.
2. **Hot-function detection.** Add a simple call-count threshold in
   `OP_CALL` — only invoke `cl_jit_compile` after N invocations
   (typical: N=10..100). Cost: one `uint16_t` counter per
   `CL_Bytecode`. First-N calls stay bytecode (some warmup latency),
   but the working set of *actually hot* functions is usually a tiny
   fraction of loaded code.
3. **Native-code reclamation under GC pressure.** Native code lives
   in `platform_alloc`'d buffers attached to `CL_Bytecode`. If the
   arena gets tight, drop native code (re-emittable from bytecode
   anytime) — same idea as a code cache in HotSpot. Adds ~50 lines
   to `mem.c`. The `native_code` pointer just goes back to NULL and
   the next call falls through to the bytecode path.
4. **Size-bounded compilation.** Skip large functions
   (`bc->code_len > N`) — they're usually setup/init code that runs
   once, not hot loops. Caps worst-case bloat per compiled function.

With (1) + (2) enabled by default, the runtime memory hit on the 8 MB
baseline drops to roughly **+50–150 KB** (just the genuinely hot
functions: graphics inner loop, REPL printer, test harness driver).
That's the regime where the JIT becomes a net win even on the low-end
target.

## Status (2026-05-14, post stack-cache + OP_TAILCALL)

Approach (1) shipping.  Two codegen paths, both fired on
source-compile and FASL-load.  Per-opcode design details live in the
matching commit messages — `git log --oneline -- src/jit/jit.c` is
the journal.

**Matchers (whole-function templates).**  Fixed shapes, hand-written
4–8 byte bodies.

| Shape                              | Native code                       | Size |
|------------------------------------|-----------------------------------|------|
| `(defun f () <literal>)`           | `moveq #imm,d0 ; rts`             | 4 B  |
| (literal too big for moveq)        | `move.l #imm32,d0 ; rts`          | 8 B  |
| `(defun f (x1..xk) xj)`, k ≤ 6     | `move.l (4+4*j)(a7),d0 ; rts`     | 6 B  |

**Walker (per-opcode fallback).**  LINK frame at A6 for locals, m68k
SP as the operand stack, 3-slot rotating register cache on top
(D5/D6/D7 — callee-saved, stashed below the LINK frame).  Supported
opcodes:

```
OP_NIL OP_T OP_CONST OP_LOAD OP_STORE OP_POP OP_DUP
OP_JMP OP_JNIL OP_JTRUE
OP_ADD OP_SUB OP_MUL OP_LT OP_GT OP_LE OP_GE OP_NUMEQ OP_EQ OP_NOT
OP_CAR OP_CDR OP_CONS
OP_GLOAD OP_GSTORE OP_FLOAD OP_CALL OP_TAILCALL
OP_STRUCT_REF OP_STRUCT_SET OP_DYNBIND OP_DYNUNBIND
OP_MV_RESET OP_MV_TO_LIST
OP_BLOCK_PUSH OP_BLOCK_POP OP_BLOCK_RETURN
OP_UWPROT OP_UWPOP OP_UWRETHROW
OP_AMIGA_CALL
OP_RET
```

Recurring shape: arith ops have an inline fixnum fast path (BTST tag
check → ADD/SUB/CMP, BVS to slow path) with a JSR helper for
non-fixnum / overflow; non-arith ops are JSR-only via a
`cl_jit_runtime_*` helper.  JSR-using emitters flush the cache before
the call (helpers read args off `(a7)` at fixed displacements); branch
targets flush, so `cache_depth = 0` at every join point.  Unrecognized
opcode → walker bails, `native_code` stays NULL, function runs
interpreted.

**`OP_TAILCALL` self-recursive native TCO (landed 2026-05-15).**
When `nargs == arity` and `bc->name` is a symbol, the emitter prefixes
the standard helper sequence with a runtime guard: compare the func
on the operand stack against this bytecode's CL_Obj; on match, copy
the N args into the A6 frame slots, drop the operand stack, and
`bra.w` back to entry-after-prologue (the same LINK frame is
reused — zero m68k-stack growth on tail-recursive loops).  On
mismatch (the symbol resolves elsewhere because of redefinition, or
compaction moved the bytecode), fall through to the existing
`cl_jit_runtime_call → cl_vm_apply` path, semantics preserved.
Cross-function tail calls (foo→bar tail) still use the helper path,
so they grow the m68k C stack at ~1 KB/level — option (b) from
§"Open design choices" (trampolining in `cl_jit_invoke`) is the
follow-up there.  The compiler-emitted OP_RET after OP_TAILCALL is
dead code on the self-TCO branch, still emitted by the walker as
unreachable trailing bytes.

**GC safety across JIT helper calls.**  Operand-stack values that
live on the m68k stack are reached by the conservative scan with
header-offset validation (`mem.c::gc_scan_jit_native_stack`, landed
2026-05-15).  Values held in the three-slot cache regs D5/D6/D7 are
*not* scanned (registers, not memory).  Rule (2026-05-15): every
helper-calling emitter flushes the cache before its JSR.  In
practice helpers can almost always allocate — even "non-allocating"
helpers throw via `cl_signal_type_error` / `cl_error` on bad inputs,
which allocates a condition object — so universal flushing is the
safest and simplest invariant.  Cost: ~1–6 extra bytes of native
code per slow path (the predec spills for whatever cache regs held
values at compile time).  `OP_CALL` / `OP_TAILCALL` / `OP_CONS` plus
the arith, compare, `OP_CAR`/`OP_CDR`, `OP_FLOAD`, `OP_GLOAD` /
`OP_GSTORE`, `OP_DYNBIND` / `OP_DYNUNBIND`, and `OP_STRUCT_REF` /
`OP_STRUCT_SET` paths all conform.

**Baked heap-CL_Obj immediates are relocated across compaction
(landed 2026-06-16).**  The conservative stack scan above only reaches
operand values *spilled onto the m68k stack*.  It does **not** see the
CL_Obj literals the emitters bake directly into the instruction stream
as 32-bit immediate operands — `OP_FLOAD`/`OP_GLOAD`/`OP_GSTORE`/
`OP_FSTORE`/`OP_DYNBIND` symbols, `OP_BLOCK_PUSH`/`OP_BLOCK_RETURN`
tags, `OP_TAGBODY_PUSH`/`OP_TAGBODY_GO` ids, `OP_ASSERT_TYPE` /
`OP_HANDLER_PUSH` / `OP_RESTART_PUSH` type/name symbols, the
`OP_CLOSURE` template, the `OP_AMIGA_CALL` library-base symbol, the
`OP_TAILCALL` self-bytecode, `OP_CONST` quoted constants, the boolean
`CL_T` materialised by the compares / `OP_EQ` / `OP_NOT`, and the
trivial-leaf matcher's literal.  Those immediates are arena-relative
offsets; the moving compactor relocates the referenced objects but
cannot see offsets buried in the platform_alloc'd code buffer, so
without help they go stale after a compaction and the next execution
dereferences a moved object (observed symptom: "`OP_FLOAD: JIT call
site has non-symbol constant 0x........`" when a GC fired mid-loop in
the ratio/float arithmetic stress test).

Fix: each emitter that bakes a *heap* CL_Obj routes through
`emit_obj_imm_predec` / `emit_obj_imm_d0` / `cache_push_obj`
(`jit.c`), which (a) force the 32-bit `MOVE.L #imm` form so the field
is always 4 bytes and (b) record the immediate's byte offset in a
per-function `JitRelocs` table.  `cl_jit_compile` copies the finished
table onto the bytecode (`bc->native_relocs` / `native_reloc_count`,
parallel to `native_code`; rebuilt on FASL load, not serialised).  The
compactor's reference-update pass forwards every entry in place
(`mem.c` `gc_update_children`, `TYPE_BYTECODE`) and, if any immediate
moved, flushes the CPU caches once at the end of `cl_gc_compact` so
68040/060 re-fetch the patched bytes.  Raw-integer immediates (arg
counts, slot/constant indices, regspecs, FFI offsets, `NIL`/fixnum
constants) are *not* heap references and are deliberately not recorded
(a `CL_HEAP_P` guard trims them — and never misclassifies a small
integer that happens to be 4-byte-aligned, since those emitters keep
calling the bare `m68k_emit_*` encoders).  Host coverage:
`tests/test_gc_jit_reloc.c` synthesises the native_code + reloc table a
JIT'd `OP_FLOAD` produces and checks the compactor rewrites the baked
offset; end-to-end coverage is `tests/amiga/run-tests.lisp`'s
"rational/float compare gc-safe" and `tests/amiga/test-jit.lisp`.

> **RESOLVED — was NOT a reloc gap.** This previously-open layout-roulette
> corruption (under the JIT, a class metaobject occasionally failing to
> register — `(setf (find-class …) …)` silently lost → later `make-instance`
> signals "No class named …", surfacing on `tests/amiga/run-tests.lisp`'s
> `sv-amiga-default` checks, 2 failures) was originally hypothesised to be a
> missing `jit_reloc_record`.  That hypothesis was **disproven**: a host audit
> that runs the real `walker_compile` under `-DJIT_M68K` over the whole boot +
> the failing test found the reloc table is *complete* — no opcode bakes an
> un-relocated heap offset.  The actual cause was the conservative JIT-stack
> **forwarding writeback** (`gc_forward_jit_native_stack`) corrupting a
> coincidental-integer C-stack word during a compaction fired while a JIT frame
> was live — see "Superseded (the writeback pass had a corruption bug)" and the
> option-B pinning landing in §"GC interaction" above.  Fixed by pinning
> conservatively-referenced objects and removing the writeback; the full Amiga
> suite passes with the JIT enabled.

**Branches** use a single-pass label table (`bc_to_native[ip]`):
forward branches emit a `Bcc.W` with a placeholder displacement plus
a patch record; `OP_RET` resolves them all in one pass.  Out-of-range
displacement bails (32-bit `Bcc.L` exists on 68020+ but isn't needed
at current function sizes).

**Layout**

- `src/jit/codebuf.{c,h}` — growable byte buffer (portable, 11 host
  unit tests).
- `src/jit/asm_m68k.{c,h}` — m68k encoders (`JIT_M68K`-only).
- `src/jit/jit.{c,h}` — matchers + walker + `cl_jit_invoke` dispatch
  + `%JIT-*` introspection builtins.
- `src/jit/runtime.{c,h}` — C helpers JIT'd code calls.
- `vm.c` OP_CALL has a native fast-path when `bc->native_code &&
  !is_tail && !traced`; `fasl.c` calls `cl_jit_compile` after each
  bytecode deserialize.

**Verification**

- `make host` + `make test`: green (JIT entry points are inline no-ops
  on host).
- `make -f Makefile.cross amiga`: green.
- `make -f Makefile.cross test-amiga` (A4000/68040/JIT FS-UAE config):
  **2525/2525** Amiga tests pass.  Per-opcode coverage lives in
  `tests/amiga/test-jit.lisp` (~280 checks: every supported opcode has
  a counter-bump assertion, value-correctness sweep, and where
  applicable a type-error / unwind-recovery path).
- `test-amiga-lowend` (68020 baseline, no JIT) still available;
  numbers will fall closer to the conservative 1.5–2× template-JIT
  estimate.

### Headline benchmarks (`trunk/bench-jit-loop.lisp`)

A/B variants flipped via `(clamiga::%jit-set-active …)`; identical
bodies, dispatch path differs.

| Bench         | Shape                              | N      | Bytecode | JIT     | Speedup |
|---------------|------------------------------------|-------:|---------:|--------:|--------:|
| sum-to        | tagbody+go fixnum loop             | 40 000 |   400 ms |   20 ms | 20.00×  |
| arith-chain   | binary ops on cached operands      | 20 000 |   300 ms |   40 ms |  7.50×  |
| call-loop     | OP_CALL inside a loop body         | 20 000 |   340 ms |  240 ms |  1.42×  |
| struct-loop   | 2× OP_STRUCT_REF per iteration     | 20 000 |   260 ms |   20 ms | 13.00×  |

`call-loop` is the limit case: every iteration flushes the cache for
the JSR, so the walker is essentially bottlenecked on the same helper
round-trip the bytecode interpreter has.  Real-TCO on self-recursion
would lift this further (the loop is shaped to exercise OP_CALL, not
OP_TAILCALL).

**Open design choices not yet committed to code**

- *Memory policy.*  JIT compiles every recognized shape
  unconditionally today.  With the walker at ~30 opcodes and bodies
  averaging 80–250 B of native code, a coarse
  "compile-on-Nth-invocation" gate is the next likely lever — see
  §"Mitigations".
- *Real tail-call optimization.*  (a) *Self-recursion landed
  2026-05-15* — see §"`OP_TAILCALL` self-recursive native TCO"
  above.  The guard form differs slightly from this section's
  original sketch: rather than pattern-matching `OP_FLOAD <self>;
  OP_TAILCALL` at compile time (hard because arbitrary arg
  evaluation sits between them), the emitter unconditionally
  compares the runtime func value against `bc`'s CL_Obj at every
  arity-matching tail-call site.  Cost: ~14 extra bytes per such
  site (load + cmp + bne + copy/lea/bra inside the taken arm).  In
  return: redefinition correctness is automatic.  (b)
  Cross-function tail-call trampolining in `cl_jit_invoke` remains
  open and is the next lever for non-self deep tail chains.
- *GC root finding for the operand stack.*  See §"GC interaction".
  Prerequisite for JIT'ing CONS-heavy / mixed-type code by default.
  **Direction chosen (2026-05-15): Option A — conservative m68k
  stack scan, with compaction suppressed while `jit_depth > 0`.**
  Plan:
    1. `cl_jit_invoke` captures entry SP into `t->jit_stack_top` and
       bumps `t->jit_depth`; restores both on return. Nested JIT
       calls only matter for the outermost `jit_stack_top`.
    2. `gc_mark_thread_roots` adds a native-stack scan when
       `t->jit_depth > 0`: 4-byte-aligned words from current SP up
       to `t->jit_stack_top`; for each, if `CL_HEAP_P(w) && w <
       cl_heap.arena_size` call `gc_mark_obj(w)` (already handles
       in-bounds + already-marked).
    3. `cl_gc` honors a no-compact inhibit when any thread has
       `jit_depth > 0`. Allocator falls back to existing OOM path.
    4. Portable SP capture via `__builtin_frame_address(0)` / a
       small `cl_capture_sp()` helper. On host (no `JIT_M68K`) the
       fields exist for layout symmetry but stay zero — scan is a
       no-op there.
    5. Tests: host unit test for the scanner (place a known
       `CL_Obj` on the C stack, scan a buffer, confirm marked); on
       Amiga, an FS-UAE test once an allocating opcode is JIT'd
       (today the walker bails on those, so this test waits on the
       first allocating-opcode lift).
  **OP_CONS lift (2026-05-15)**: first allocating opcode handled
  directly by the walker.  Emitter pops `cdr`/`car` from the cache,
  flushes the remaining cache so residual heap pointers land on the
  m68k stack within the scan window, then JSRs
  `cl_jit_runtime_cons` (a thin wrapper around `cl_cons`).  The
  conservative scan + offset validation is what makes this safe;
  without those, a GC inside `cl_cons` could either miss live
  operand-stack references or phantom-mark coincidental integers.
  Generic-arith bignum fallback (OP_ADD slow path on overflow) and
  every other helper-calling emitter were brought under the same
  rule in the same change — see §"GC safety across JIT helper
  calls" above.
- *MV_RESET and `mv_count` propagation.*  Investigated 2026-05-14
  and reverted.  Bytecode VM resets `cl_mv_count = 1` as a side
  effect of every value-producing opcode (OP_CONST, OP_NIL, OP_LOAD,
  …).  The walker doesn't emit per-opcode resets, so a multi-value
  producer's mv_count leaks through a JIT'd body's OP_RET to the
  caller.  Two tried-and-reverted fixes:  (i) reset in
  `cl_jit_invoke` after the native call — broke CLOS dispatch (CLOS
  internals route MVs through JIT'd intermediates and need them
  propagated);  (ii) reset in walker's OP_RET emitter — same CLOS
  damage.  Adding a standalone `OP_MV_RESET` emitter that compiles
  the bytecode opcode is safe in isolation but doesn't fix the
  per-opcode-reset gap (the compiler emits OP_MV_RESET sparsely, only
  between `or`/`and` arms; intra-body single-value sequences still
  leak).  The only general fix is a real per-opcode reset (one
  store per value-producing opcode), prohibitively expensive at the
  current "JSR per write" cost.  An inlined direct write — `MOVE.L
  #1, mv_count_offset(thread_ptr)` — would be cheap if we cached the
  current-thread pointer in an A-register across the JIT'd
  function's prologue (single-threaded fast path: `cl_main_thread_ptr`
  load once at LINK time; multi-threaded: skip JIT or call
  `platform_tls_get`).  Until that's in, JIT'd functions inherit
  whatever mv_count their callers were last left with — fine for the
  current test suite and bouncing-lines hot path (nothing reads
  mv_count from JIT'd code), but a latent correctness gap.

## Status (2026-05-17, post OP_AMIGA_CALL walker + defcfun inlining)

Two changes targeted the FFI hot path identified in the
empirical-baseline section.  Both landed in the same session; bench
numbers below verify on the high-end FS-UAE config running
`examples/gfx/bouncing-lines.lisp`.

**`OP_AMIGA_CALL` in the walker (commit 25336f5).**  Was the last
defcfun-emitted opcode that bailed the walker, forcing every FFI
wrapper (`move-to`, `draw-to`, `rect-fill`, `set-a-pen`, …) to run
through the bytecode interpreter — and every JIT'd caller of such a
wrapper to bounce native→bytecode→native per FFI hop.  Walker now
emits cache_flush + 5-arg JSR to
`cl_jit_runtime_amiga_call(base_sym, offset, regspec, n_args,
operand_top)`; helper resolves the library-base symbol (matching
`vm.c`'s `OP_AMIGA_CALL` errors), reverse-copies n_args from the m68k
operand stack into a stack-local `CL_Obj[8]`, and delegates to
`cl_amiga_ffi_call_dispatch`.  Prescan accounts the 10-byte encoding
(u16 sym_idx, i16 offset, i32 regspec, u8 n_args); the base symbol is
baked into the call site as a CL_Obj literal from `constants[idx]`.

**`defcfun` compiler-macro (commit 7091844).**  After the walker fix,
every direct `(move-to rp x y)` call site was still paying for
`OP_FLOAD <move-to>` + `OP_CALL 3` → `cl_jit_runtime_call` →
`cl_vm_apply` → wrapper entry/exit (LINK frame, save+restore of
D5–D7) around the single `OP_AMIGA_CALL` *inside* the wrapper.  The
wrapper adds zero semantic value at direct call sites.  `defcfun` now
expands a `define-compiler-macro` alongside the `defun`, rewriting
direct call sites to `(amiga:%ffi-call <library-base> <offset>
<regspec> <args…>)` — the same form the compiler's FFI fast-path
already lowers to `OP_AMIGA_CALL`.  The compiler-macro returns the
`&whole` form on argument-count mismatch (CLHS 3.2.2.1.3) so wrong-
arity calls still get the wrapper's normal error.  Indirect callers
(`funcall`/`#'`) still hit the named wrapper.

**FASL-cache caveat for the compiler-macro path.**  The wrappers'
compiler-macros are registered at `defcfun`-expand time, so a FASL
built with the *old* `defcfun` macro will not carry them.  The FASL
infrastructure only mtime-checks the consuming file (e.g.
`graphics.lisp` → `graphics.fasl`), not transitive macro sources
(`ffi.lisp`).  When the `defcfun` macro changes, the consuming FASLs
under `verify/realamiga/aos3/S/cl-amiga/faslcache/.../lib/amiga/*.fasl`
(`graphics.fasl`, `intuition.fasl`, `gadtools.fasl`) must be deleted
by hand.  Symptom of missing this: bench delta is ~0 because the
wrappers still go through `cl_vm_apply` even though the new defcfun
macro registers compiler-macros at the symbol level.

### Bouncing-lines bench progression (high-end FS-UAE)

| Build state                                        |    FPS | Δ vs. prev |
|----------------------------------------------------|-------:|-----------:|
| post fast-path cache_flush skip (prev status, 5/16)| 467–470|          — |
| + walker `OP_AMIGA_CALL`                           |   ~525 |    +55–58  |
| + `defcfun` compiler-macro inlining                |   ~615 |       +90  |

Net **+148 FPS** (+32%) over the prior status snapshot.  The remaining
gap to ACE BASIC's ~1900 FPS (cited in the empirical-baseline section)
is **not** mostly graphics-library cost: ACE and CL-Amiga share the
same ROM calls.  The 3× headroom is the structural cost of being a
dynamic, GC'd, tagged-value language vs a static one — value
unboxing per arg, closure/bytecode/builtin dispatch per call, symbol
lookup at `OP_FLOAD`, GC safepoints.  It is not closable to ACE
levels by JIT codegen alone, and likely not closable past the
register-alloc-JIT projection in §"Projected ceilings" without a
structurally different design.

### Investigated and shelved: collapsing the FFI helper chain

Per-call FFI overhead today, on top of the ROM library call itself,
for a 3-arg `(move-to rp x y)`-shape call:

| Stage                                              | ~cycles |
|----------------------------------------------------|--------:|
| `cl_jit_runtime_amiga_call` frame entry/exit       |    ~30  |
| `cl_symbol_value(base_sym)` + unbound + FOREIGN_POINTER_P + `bfp->address` | ~25 |
| reverse-copy 3 args into `CL_Obj[8]`               |    ~30  |
| call to `cl_amiga_ffi_call_dispatch` (JSR + 5 args)|    ~10  |
| `memset(regs, 0, 56)`                              |    ~30  |
| regspec-decode loop + `ffi_arg_to_u32` × 3         |    ~50  |
| `platform_amiga_call` trampoline (movem save/load) |    ~50  |
| result handling (void_p → CL_NIL, or fixnum box)   |     ~5  |

Two options for compressing this were measured and shelved:

- **(3) Collapse to one helper.**  Replace `cl_jit_runtime_amiga_call`
  with a tiny `cl_jit_runtime_amiga_resolve_base(sym)` returning the
  resolved library address; emit the reverse-copy inline in m68k;
  JSR `cl_amiga_ffi_call_dispatch` directly.  Saves the outer helper
  frame (~30 cycles) and inlines the copy (~15 cycles cheaper than
  the C loop) ≈ **~30 cycles/call**.  At ~20 FFI calls/frame × 615 FPS
  ≈ 370k cycles/sec saved ≈ **~+1–2 FPS**.  Refactor cost not worth
  the gain.
- **(2+3) Inline regspec decode + register marshalling.**  Walker
  knows regspec and n_args at compile time → can lay each arg
  directly into its `regs[reg_idx]` slot, zero-fill unused slots
  (the trampoline `movem.l (a5),d0-d7/a0-a4` loads all 13
  unconditionally), inline a fixnum-fast-path for `ffi_arg_to_u32`
  with a helper bailout for bignum/foreign-pointer args, and JSR
  `platform_amiga_call` directly.  Saves helper frame, memset, and
  regspec-decode loop ≈ **~90 cycles/call** in the typical
  fixnum-args case.  At ~20 FFI calls/frame ≈ ~+15–20 FPS to
  ~630–635.  ~100–150 lines of walker emit; needs the m68k FFI
  register layout encoded into the JIT, and result-boxing emitted
  for non-void wrappers.  Real complexity for modest gain;
  diminishing-returns territory.

**Conclusion.**  Neither option pursued.  The next codegen lever
likely worth the work is a walker fast-path for `OP_FLOAD <sym>; …;
OP_CALL n` when the resolved callee carries `native_code`: emit a
direct JSR into the callee's native entry (guarded against
redefinition the way `OP_TAILCALL` self-TCO is — runtime compare of
the symbol's current function value against the captured callee),
bypassing the `cl_vm_apply` dispatch trip.  That benefits **every**
direct JIT'd-to-JIT'd call site in the codebase, not just FFI ones.
Not investigated this session.

## Status (2026-05-18, post closure + mutation walker pass)

Three independent walker landings extend coverage from "top-level
defuns with no captured variables" to "inner closures-within-closures
that mutate their captures, plus the common list-building / mutation
opcodes."  No bench delta on bouncing-lines (closure-heavy paths
weren't the hot loop), but the *fraction of loaded code that
JIT-compiles* shifts substantially — closures pervade CLOS, the
condition system, and most macroexpansion-heavy library code, and
those previously bailed the walker entirely.

**Walker opcodes added since 2026-05-17:**

```
OP_CLOSURE OP_MAKE_CELL OP_CELL_REF OP_CELL_SET_LOCAL    (0ed45f9, phase A)
OP_UPVAL OP_CELL_SET_UPVAL                               (7f7bb1a, phase B)
OP_FSTORE OP_LIST OP_RPLACA OP_ASET                      (33d956f)
```

**Phase A — outer-function closure construction (commit 0ed45f9).**
Drops the `OP_CLOSURE`-rejection in the walker.  Emitter loads each
capture descriptor onto the m68k stack, snapshots `&values[0]` into
A0, and JSRs `cl_jit_runtime_make_closure(tmpl, n_upvals, values)`.
Operand drop is via `LEA` (not `addq.l`, which silently truncates
immediates > 8).  `OP_MAKE_CELL` / `OP_CELL_REF` / `OP_CELL_SET_LOCAL`
each get one helper; `OP_CELL_SET_LOCAL` is peek-only (TOS unchanged,
mirroring the VM).  Outer-function gate stays: walker still requires
`bc->n_upvalues == 0`, so this phase only covers top-level defuns
that *contain* inner lambdas, not closures whose own body captures.
Prescan validates the entire `OP_CLOSURE` descriptor stream up front
so a malformed sequence bails the whole compile cleanly rather than
miscompiling mid-emit.

*Bug uncovered and fixed in the same change.*
`cl_jit_runtime_block_post_longjmp` and `cl_jit_runtime_uwprot_post_longjmp`
captured `cl_vm.sp` / `cl_vm.fp` at setup but never restored them
after a longjmp.  Latent because the pre-phase-A walker rejected any
function containing `OP_CLOSURE`, so a `(block tag (mapc (lambda (x)
(return-from tag x)) list))` ran entirely in the bytecode VM where
`OP_BLOCK_RETURN`'s longjmp arm already restores sp/fp.  With the
walker now accepting `OP_CLOSURE`, the longjmp lands back in JIT'd
code via the helper, which left `cl_vm.sp` pointing into the inner
lambda's deepest VM frame.  The next VM action (`cl_jit_invoke`'s
caller doing `sp -= nargs+1; push result`) then overwrote whatever
the caller had pushed before invoking the JIT'd function.  Symptom in
failing tests: a literal pushed as the CHECK macro's expected value
got clobbered with the closure object the JIT'd function had just
allocated.  Fix mirrors `vm.c`'s `OP_BLOCK_RETURN` longjmp arm.

**Phase B — inner closures that capture (commit 7f7bb1a).**  Drops
the `bc->n_upvalues != 0` walker gate.  Adds `OP_UPVAL` and
`OP_CELL_SET_UPVAL` emitters routing through
`cl_jit_runtime_upval_ref(func, index)` and
`cl_jit_runtime_cell_set_upval(func, idx, v)`.  Both helpers
non-allocating; upval_ref falls back to CL_NIL on
closure-or-raw-bytecode mismatch matching the VM's `OP_UPVAL`
semantics, cell_set_upval errors on non-cell upvalue (the VM's
"shouldn't happen" diagnostic).

*ABI shift required.*  `cl_jit_invoke` now passes the function-object
CL_Obj (the closure or raw bytecode) as the first C argument, so JIT'd
code can read it from `8(a6)` when an `OP_UPVAL` /
`OP_CELL_SET_UPVAL` needs the closure's `upvalues[]` array.  User
args shift to `12(a6)` onward for the non-kw path, and
`12/16/20(a6)` for the kw-prologue's `(bc, nargs, args)` tuple.
`slot_disp()`, the passthrough matcher's load displacement, and the
kw-prologue emit all bumped by the same +4 in lockstep.  The
trivial-leaf matcher reads no args, so it stays as-is.  Existing
matcher byte-count tests adjusted for the shift (slot j now at
`(8+4*j)(a7)` instead of `(4+4*j)(a7)`).  `OP_CLOSURE` also drops
its `is_local == 0` reject: captures from the parent's upvalues now
route through `cl_jit_runtime_upval_ref`, so nested-closures-within-
closures compile.

**Mutation / list-building (commit 33d956f).**  Four independent
opcodes with no ABI changes.

- `OP_FSTORE` — peek-and-store into `sym->function`, same shape as
  `OP_GSTORE`; `cl_jit_runtime_fstore` non-allocating.
- `OP_LIST` — build a list from N operand-stack values.  Helper
  iterates `operand_top[0..n-1]` (TOS = last in list),
  `CL_GC_PROTECT`s the partial list across the n `cl_cons`
  allocations.  The JIT-stack forwarding pass rewrites
  `operand_top` slots in place so reads stay valid across
  collections.
- `OP_RPLACA` — pop new_car + cons, type-check, write, push
  new_car.  Mirrors `OP_STRUCT_SET`'s two-arg shape.
- `OP_ASET` — pop val/idx/vec; dispatch across vector / string /
  bit-vector with the same value type-checks the VM uses.
  Three-arg helper called via `disp(a0)` push to avoid spending a
  third scratch register.

Prescan accepts the new opcodes with the correct step sizes
(`FSTORE=3`, `LIST=2`, `RPLACA=1`, `ASET=1`).  23 new behavioral
tests in `tests/amiga/test-jit.lisp`: nested defun via `OP_FSTORE`,
list-building via `(untrace)` for n=1/3/7, setf-of-car via
`OP_RPLACA` (returns, mutates, type-error), setf-of-aref via
`OP_ASET` on vector / string / bit-vector (returns, mutates, bounds,
value-type errors).

**Verification (2026-05-18 high-end FS-UAE config).**
`test-amiga`: 2624/2624 pass — 99 new tests since 2026-05-17 across
the three landings (5 closure-mutate + 71 closure / cell shape
sweeps, plus 23 mutation).  Host `make test`: green.  The three
pre-existing `amiga-defcfun-regspec-*` failures noted in the
2026-05-17 closure-phase verifications were fixed in commit e7f21b4
(progn-wrapped defcfun expansion → tests descend `(second expanded)`
to reach the inner defun).

### Updated walker opcode list (cumulative)

```
OP_NIL OP_T OP_CONST OP_LOAD OP_STORE OP_POP OP_DUP
OP_JMP OP_JNIL OP_JTRUE
OP_ADD OP_SUB OP_MUL OP_LT OP_GT OP_LE OP_GE OP_NUMEQ OP_EQ OP_NOT
OP_CAR OP_CDR OP_CONS OP_LIST OP_RPLACA OP_ASET
OP_GLOAD OP_GSTORE OP_FLOAD OP_FSTORE OP_CALL OP_TAILCALL
OP_STRUCT_REF OP_STRUCT_SET OP_DYNBIND OP_DYNUNBIND
OP_MV_RESET OP_MV_TO_LIST
OP_BLOCK_PUSH OP_BLOCK_POP OP_BLOCK_RETURN
OP_UWPROT OP_UWPOP OP_UWRETHROW
OP_CLOSURE OP_MAKE_CELL OP_CELL_REF OP_CELL_SET_LOCAL
OP_UPVAL OP_CELL_SET_UPVAL
OP_AMIGA_CALL
OP_RET
```

Outer-function gate is now "bytecode validates prescan" — `n_upvalues
!= 0` no longer disqualifies.  The remaining bail-causers are the
opcodes the walker hasn't grown emitters for (most of `OP_TAGBODY_*`,
`OP_CATCH` / `OP_THROW`, `OP_PROGV`, `OP_HANDLER_*`, and the
specialized-vector accessors), plus the &rest / &optional prologue
shapes and self-TCO for kw entries — all called out as deferred in
prior status sections.

**Open levers, unchanged from 2026-05-17:**

- Memory policy / hot-function gate.
- Cross-function TCO trampolining in `cl_jit_invoke`.
- Direct JSR `OP_FLOAD <sym>; …; OP_CALL n` fast-path when the
  resolved callee carries `native_code` (the "JIT-to-JIT direct
  call" lever flagged at the end of the FFI section).
- Per-opcode `mv_count` reset via cached thread pointer.

## Status (2026-05-19, post NLX / handler / mutation completeness pass)

Five back-to-back walker landings (2026-05-18 → 2026-05-19) close
every common-form bail flagged in the prior status sections.  The
walker now spans the full "ordinary defun" surface: arithmetic, list
mutation, closures, conditions, restarts, tagbody/catch/block/uwprot
NLX, dynamic binding (including progv), and apply.  No
bouncing-lines bench delta — these opcodes are absent from the
inner draw loop — but the fraction of loaded user / library code
that JIT-compiles is now near-total for non-`&rest`/`&optional`
shapes.

**Walker opcodes added since 2026-05-18:**

```
OP_RPLACD OP_ARGC OP_MV_LOAD OP_NTH_VALUE OP_ASSERT_TYPE   (0a8a8fc)
OP_HANDLER_PUSH OP_HANDLER_POP
  OP_RESTART_PUSH OP_RESTART_POP                           (fced077)
OP_TAGBODY_PUSH OP_TAGBODY_POP OP_TAGBODY_GO               (7130bf0)
OP_CATCH OP_UNCATCH                                        (2341edf)
OP_DIV OP_APPLY OP_PROGV_BIND OP_PROGV_UNBIND              (9ad741c)
```

All five landings follow the established shapes:

- *Linear opcodes* (RPLACD, ARGC, MV_LOAD, NTH_VALUE, ASSERT_TYPE,
  DIV, APPLY, PROGV_BIND, PROGV_UNBIND, HANDLER_POP, RESTART_POP):
  cache-aware pop + JSR through a one-purpose `cl_jit_runtime_*`
  helper that mirrors the matching `vm.c::OP_*` case byte-for-byte.
  Cache flush before every JSR (the "helpers may allocate even on
  the success path via cl_signal_type_error / cl_error condition
  objects" invariant established with the closure landing).

- *NLX setjmp shapes* (CATCH, TAGBODY_PUSH): same JIT-inline-JSR-
  setjmp protocol as BLOCK_PUSH / UWPROT.  TAGBODY_PUSH adds a
  re-arm step (cl_nlx_top++) on the longjmp arm so the same frame
  stays usable across repeated GO until the matching TAGBODY_POP.
  TAGBODY_GO emits the dispatch shim emitted by compile_tagbody —
  the longjmp helper returns the tag-index fixnum, JTRUE per-tag
  branches route to the matching body, and the closing OP_JMP wraps
  the dispatcher back to the GO.  Prescan marks the dispatcher
  landing IP as a branch target so the cache flushes at arrival.

- *Handler / restart push* (HANDLER_PUSH, RESTART_PUSH): plain
  push/pop on the per-thread handler/restart stacks — no setjmp
  needed (handlers run as ordinary calls dispatched by
  `cl_signal_condition`).  Same OP_DYNBIND-style emitter shape:
  pop runtime values, cache-flush, push C args with a baked-in
  symbol literal, JSR, drop.

- *OP_DIV*: present for completeness but not reachable from Lisp
  source today.  The compiler routes `/` through OP_FLOAD+OP_CALL
  (not the inline-op path the way `+`, `-`, `*` are), so the
  emitter waits on a future inliner change to engage.  Helper is in
  place to avoid a latent walker bail if that change ever lands.
  No inline fixnum fast path (same as OP_MUL): m68k DIVS.L is ~88
  cycles on 68020, so the JSR overhead is in the noise, and the
  result is a ratio whenever the division isn't exact — bailing to
  the helper is the common case anyway.  Inlining would buy
  ~5 cycles for ~70 lines of walker emit + a remainder-check + a
  FIXNUM_MIN/-1 overflow guard + a new asm encoder.  Not worth it.

- *OP_APPLY*: helper flattens the arglist into a stack-local
  CL_Obj[64] (matching the VM's cap), resolves a SYMBOL callee
  through `s->function`, then delegates to `cl_vm_apply`.  The
  VM's OP_APPLY inlines dispatch to avoid C-stack growth on deep
  `(apply f (apply g (apply h …)))` chains; the JIT accepts the
  extra cl_vm_apply round-trip since apply chains are not on a
  measured hot path.

- *OP_PROGV_BIND / OP_PROGV_UNBIND*: PROGV_BIND walks the symbols
  + values lists in lockstep, NIL-paired symbols bind to
  CL_UNBOUND (per CLHS), returns the saved `cl_dyn_top` as a
  CL_MAKE_FIXNUM mark on the operand stack.  PROGV_UNBIND untags
  the mark, calls `cl_dynbind_restore_to`, returns the body
  result so the walker's cache_push leaves it as the new TOS.

### Updated walker opcode list (cumulative)

```
OP_NIL OP_T OP_CONST OP_LOAD OP_STORE OP_POP OP_DUP
OP_JMP OP_JNIL OP_JTRUE
OP_ADD OP_SUB OP_MUL OP_DIV
OP_LT OP_GT OP_LE OP_GE OP_NUMEQ OP_EQ OP_NOT
OP_CAR OP_CDR OP_CONS OP_LIST OP_RPLACA OP_RPLACD OP_ASET
OP_GLOAD OP_GSTORE OP_FLOAD OP_FSTORE
OP_CALL OP_TAILCALL OP_APPLY
OP_STRUCT_REF OP_STRUCT_SET
OP_DYNBIND OP_DYNUNBIND OP_PROGV_BIND OP_PROGV_UNBIND
OP_MV_RESET OP_MV_LOAD OP_MV_TO_LIST OP_NTH_VALUE
OP_ARGC OP_ASSERT_TYPE
OP_BLOCK_PUSH OP_BLOCK_POP OP_BLOCK_RETURN
OP_CATCH OP_UNCATCH
OP_TAGBODY_PUSH OP_TAGBODY_POP OP_TAGBODY_GO
OP_UWPROT OP_UWPOP OP_UWRETHROW
OP_HANDLER_PUSH OP_HANDLER_POP
OP_RESTART_PUSH OP_RESTART_POP
OP_CLOSURE OP_MAKE_CELL OP_CELL_REF OP_CELL_SET_LOCAL
OP_UPVAL OP_CELL_SET_UPVAL
OP_AMIGA_CALL
OP_RET
```

**Remaining walker bail-causers** (opcodes that still disqualify a
function from native compile):

```
OP_DEFMACRO OP_DEFTYPE OP_DEFSETF OP_DEFVAR
```

All four are top-level definers — rare in hot code, low leverage.
The walker is otherwise complete for ordinary defun bodies.

Plus the non-opcode gates still in place:

- `&rest` / `&optional` argument shapes (kw landed 2026-05-16;
  rest/optional still bail).
- Self-TCO for keyworded entries (the cross-function trampoline
  lever covers this; positional self-TCO landed 2026-05-15).

### GC native-stack scan cap removal (commit 84d62d3)

Tangential but worth recording: the 256-entry stack-local candidate
buffer in `mem.c::gc_scan_jit_native_stack` was sized for typical
operand-stack depths but capped retention at deep CLOS / serapeum
workloads.  Now grows on demand via `platform_alloc`, so the
conservative scan never silently drops candidates.  Doesn't affect
walker coverage but removes a quiet "may retain extra garbage"
disclaimer from the GC interaction story.

### Verification (2026-05-19, high-end FS-UAE config)

- `make host` + `make test`: green.
- `make -f Makefile.cross test-amiga`: **2667 / 2667** pass.
  Net +43 tests across the five landings:
  - +28 across the RPLACD / ARGC / MV_LOAD / NTH_VALUE / ASSERT_TYPE
    landing
  - + handler/restart/tagbody/catch coverage in
    `tests/amiga/test-jit.lisp`
  - +12 from this commit's apply (6: builtin / user-fn / symbol-fn /
    leading-args / empty arglist / counter bump) and progv (6:
    single / multi / restore-on-exit / short-values / non-symbol
    type-error / counter bump) sweeps.
- No bench delta on `bouncing-lines` (~615 FPS) — the new opcodes
  aren't on the inner draw loop.  General-purpose JIT speed-up
  remains the 20× / 13× / 7× pattern from `trunk/bench.lisp`
  (commit 15befb6).

**Open levers, unchanged from 2026-05-18:**

- Memory policy / hot-function gate.
- Cross-function TCO trampolining in `cl_jit_invoke` (also unblocks
  self-TCO for keyworded entries).
- Direct JSR `OP_FLOAD <sym>; …; OP_CALL n` fast-path when the
  resolved callee carries `native_code`.
- Per-opcode `mv_count` reset via cached thread pointer.
- `&rest` / `&optional` prologue shapes for the walker.

## Backtrace / frame introspection under JIT (opt-in shadow frames, 2026-05-22)

`EXT:BACKTRACE` and `EXT:FRAME-LOCALS` (the Sly/SLDB backend) walk
`cl_vm.frames`. JIT'd functions run native code via `cl_jit_invoke` and
**do not push a `CL_Frame`** — so by default they are invisible to the
backtrace, and the `cl_vm_apply` trampolines that drive JIT→JIT calls show
up as anonymous frames. On the interpreter (host, `--no-jit`) ordinary
calls push real frames, so backtraces are complete there.

### Opt-in shadow frame

`cl_jit_invoke` can push a lightweight shadow `CL_Frame` around the native
call: `{bytecode = func_obj, bp = sp - nargs, n_locals = nargs}`, popped on
return. That makes the function visible (name + source line) and exposes its
**arguments** to `frame-locals` (the arg vector is still live on the
GC-rooted `cl_vm.stack` at `sp - nargs`). Interior `let`-bound locals live on
the m68k operand stack inside the native code and remain **not**
introspectable — only arguments are recoverable for JIT'd frames.

This costs a few percent on call-heavy code (~8.5% on a pure call-dispatch
micro-bench; ~5% / ~30 FPS on `bouncing-lines`, whose graphics primitives go
through `OP_AMIGA_CALL` and bypass `cl_jit_invoke`). Since the frame is only
ever *read* by a backtrace (an error, the SLDB debugger, an explicit
`ext:backtrace`), it is **off by default** and gated on a flag, so a normal
run pays only one not-taken branch per call:

```
src/jit/jit.c:
  static int jit_shadow_frames = 0;                 /* default off       */
  void cl_jit_set_shadow_frames(int on);            /* toggle            */
  int  cl_jit_shadow_frames_enabled(void);          /* read              */
  ... in cl_jit_invoke:
  if (jit_shadow_frames && cl_vm.fp < cl_vm.frame_size) { /* push shadow */ }
```

Lisp-visible switch (CLAMIGA package):

- `(clamiga::%jit-set-frames t|nil)` — enable / disable, returns new state
- `(clamiga::%jit-frames-p)` — current state
- `(clamiga::%jit-active-p)` — whether the JIT itself is compiled in + active

A debug session (Sly/SLDB connect, or a developer at the REPL) turns frames
on while introspecting and off afterwards. The Amiga test suite enables them
around its `EXT:BACKTRACE`/`EXT:FRAME-LOCALS` section and disables them again.

On a non-local exit (`longjmp`) the shadow-frame pop is skipped, but the
error/NLX unwind resets `cl_vm.fp` wholesale to a pre-call snapshot, so the
frame cannot leak.

### Two supporting fixes

- **Stub-frame skipping** (`src/core/vm.c`): the backtrace readers
  (`cl_vm_backtrace_list`, `cl_vm_frame_locals`, `cl_capture_backtrace`) skip
  `cl_vm_apply` trampoline frames (`f->code == f->stub_code`) so the JIT call
  path's trampolines don't pollute the listing. No-op on host.
- **Stale `cl_debug_base_fp`** (`src/core/repl.c`): the error-time frame
  snapshot is set on *every* error and was never cleared, so an ad-hoc
  `ext:backtrace` deep in a long session could report a truncated frame
  window. It is now cleared per top-level form in the load/REPL loop — still
  valid within the form that errors (the debugger runs before the next form),
  but no longer leaks across forms.
