# Type Enhancements Feasibility

## 1. Runtime Type Assertions (Feasible — ~300-500 lines)

### `the` special form

`(the fixnum (+ x y))` — asserts the result is of the given type at runtime.

- Add `SYM_THE` well-known symbol
- `compile_the()` in compiler_extra.c: compile expr, emit type-check
- Two modes controlled by `(optimize (safety ...))`:
  - safety >= 1: emit `OP_ASSERT_TYPE` (new opcode) — signals `type-error` on mismatch
  - safety = 0: no-op (trust the programmer, zero overhead)

### `(declare (type <type> <var>...))` declarations

Currently parsed but ignored. Wire up to emit runtime checks.

- Store type declarations in `CL_CompEnv` (new `type_decls[]` array)
- After binding a variable in `let`/`let*`, emit `OP_ASSERT_TYPE` if safety > 0
- Works with compound type specifiers: `(declare (type (or fixnum null) x))`

### New opcode: `OP_ASSERT_TYPE`

- Encoding: `OP_ASSERT_TYPE u16` (constant pool index of type specifier)
- Peeks at TOS (does not pop), calls `typep_check()`, signals `type-error` on failure
- Zero overhead when safety = 0 (not emitted)

### Estimated changes

- `opcodes.h`: +1 opcode
- `vm.c`: +15 lines (opcode handler)
- `compiler_extra.c`: +100 lines (`compile_the`, type decl processing)
- `compiler_internal.h`: +5 lines (type decl storage in CompEnv)
- `builtins_type.c`: minor (reuse `typep_check`)

## 2. Compile-Time Type Checking (Hard — ~2000+ lines)

### What it would require

- Type environment in `CL_CompEnv` mapping variables → known types
- Type propagation through `let`/`let*` bindings, function returns
- Flow-sensitive narrowing after `typep`/`typecase` branches
- Warning emission (not errors — CL is dynamically typed)

### Obstacles

- **Single-pass compiler**: no IR to analyze, code emitted as expressions are visited
- **Macros are runtime**: macro expanders run via `cl_vm_apply()`, can't be analyzed statically
- **Dynamic typing is fundamental**: CL spec doesn't require compile-time type errors

### Possible limited version (~800 lines)

Track "definitely fixnum" vs "unknown" through simple cases:
- Literal integers → fixnum
- `(+ fixnum fixnum)` → fixnum
- `(the fixnum ...)` → fixnum
- Warn when passing known-wrong types to builtins (e.g., `(+ "hello" 1)`)

## 3. Dialyzer-Style Static Analysis (Not Practical — ~5000-8000 lines)

Dialyzer uses whole-program success typing: finds code guaranteed to crash without annotations.

### Why it doesn't fit

- Requires AST/IR representation (~800 lines) — compiler currently emits bytecode directly
- Requires constraint solver (~1000 lines) and type inference engine (~1500-2000 lines)
- Memory-hungry: target is 68020 @ 14MHz, 8MB RAM
- Macro expansion depends on runtime VM — can't analyze unexpanded code
- Would fundamentally change the compiler architecture

### Alternative: separate offline tool

A standalone type checker that reads .lisp source files outside the compiler.
Does not affect runtime performance or compiler complexity.
Still significant effort but architecturally clean.

## Recommendation

**Phase 6 (now):** Implement `the` + `(declare (type ...))` with `OP_ASSERT_TYPE` — runtime checks gated by safety level. Low effort, high value for catching bugs.

**Later:** Consider basic compile-time warnings for obviously wrong types (string to arithmetic, etc.) as a compiler enhancement. No need for full Dialyzer-style analysis.
