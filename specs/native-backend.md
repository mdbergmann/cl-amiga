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

## Implementation Plan (for approach 1)

1. Add `codegen_m68k.c` — walks `CL_Bytecode`, emits m68k for each opcode
2. Allocate code buffers via `platform_alloc`
3. Extend `CL_Bytecode` or `CL_Closure` with a native code pointer
4. VM checks for native code and calls it directly; falls back to interpretation if absent
5. Conservative GC for root scanning (simplest starting point)
6. Runtime helpers for complex operations (function calls, GC triggers, error signaling)

## Decision

Not started. Continue with Phase 5-6 (standard library, condition system) first. The bytecode VM is correct and sufficient for now. Native codegen is a performance optimization for later — possibly Phase 10 or beyond.
