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
anything.

This is enough to start; precise stack maps can come later if
retention turns out to be a real problem (typically isn't for this
kind of workload).

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

## Decision

Not started. The bytecode VM is correct and well-tuned for an
interpreter — the opcode trace above shows fixnum arith / struct
access / FFI are already inlined opcodes, so there are no remaining
"cheap" wins inside the bytecode design.

A native backend is the next real performance lever, and approach (1)
— the template JIT — is the recommended starting point: ~65% of
dispatched ops are stack housekeeping that disappears for free once
locals stay in registers between ops. Defer until library / condition
system work is settled; the projected ~2× FPS on graphics workloads
won't change the project's day-to-day usability before then.

When work does begin, the 8 MB / 68020 baseline forces an opt-in,
hot-function-only design (see "Mitigations" above) — JIT'ing
everything would blow the runtime memory budget. The high-end JIT
config has no such constraint.
