# Remaining Issues for Loading ASDF 3.3.7

## What Was Fixed (this session)

### Completed
1. **`style-warning` condition type** — Added to C hierarchy (`builtins_condition.c`) and CLOS bootstrap (`clos.lisp`)
2. **Additional condition types** — `cell-error`, `unbound-slot`, `stream-error`, `end-of-file`, `file-error`, `package-error`, `parse-error`, `print-not-readable`, `storage-condition` — added to C hierarchy + CLOS
3. **`documentation` function + `(setf documentation)`** — Implemented in `boot.lisp` using hash table storage
4. **CL stubs** — `compile-file`, `compile-file-pathname`, `with-compilation-unit`, `with-standard-io-syntax`, `print-unreadable-object` — added to `boot.lisp`
5. **ASDF feature check** — Added `cl-amiga` to `#-(or ...)` at line 979 of `asdf.lisp`
6. **Docstrings before `declare`** — Fixed `process_body_declarations()` in `compiler_extra.c` to skip string literals among declarations per CL spec
7. **`#x`, `#b`, `#o`, `#nR` reader dispatch macros** — Added radix number literal reader support in `reader.c`
8. **Loop nested `:if`/`:when`/`:unless` sub-clauses** — Added iterative handling of `:when ... :else :when ...` chains in `%expand-extended-loop` (boot.lisp)

### Files Modified
- `src/core/symbol.h` — new condition type symbol externs
- `src/core/symbol.c` — new condition type symbol definitions + interning
- `src/core/builtins_condition.c` — expanded condition hierarchy
- `src/core/compiler_extra.c` — docstring-before-declare fix
- `src/core/reader.c` — `#x`/`#b`/`#o`/`#nR` radix reader
- `lib/boot.lisp` — `documentation`, stubs, loop conditional chain fix
- `lib/clos.lisp` — CLOS bootstrap classes for new condition types
- `~/.local/share/ocicl/asdf.lisp` — feature check patch (line 979)

## Remaining Blockers

### Blocker 1: Loop with 8+ `:when ... :collect/:append :into` chains crashes
**Status**: FIXED
**Fix**: Doubled bytecode limits — `CL_MAX_CODE_SIZE` 8192→16384, `CL_MAX_CONSTANTS` 256→512 (compiler.h)

### Blocker 2: `return-from` across `labels` boundaries
**Status**: FIXED
**Fix**: Implemented NLX-based blocks for cross-closure return-from:
- New opcodes: `OP_BLOCK_PUSH` (0x99), `OP_BLOCK_POP` (0x9A), `OP_BLOCK_RETURN` (0x9B)
- New NLX type: `CL_NLX_BLOCK` (2) — blocks on the NLX stack, separate from catch/throw
- Outer block names propagated to inner compilers via `CL_Compiler.outer_blocks[]`
- Optimization: only uses NLX when block body contains closure-creating forms (lambda/labels/flet)
- Most blocks (defun without closures) still use efficient local jumps (zero overhead)
- `CL_MAX_NLX_FRAMES` increased from 32 to 64
- 5 new tests: labels, flet, lambda, nested labels, unwind-protect interaction

### Blocker 3: Additional missing CL features deeper in ASDF
**Status**: Not yet investigated
**These will only surface once blockers 1 and 2 are fixed. Known from initial error scan:**
- `symbol-call` — ASDF utility function (defined by ASDF itself, should be OK once `parse-define-package-form` works)
- `with-upgradability` — ASDF macro (also defined by ASDF)
- Various UIOP packages not being created (cascading from `define-package` failing)
- Potential `write-string` with `:start`/`:end` kwargs — verified working

## Test Status
- All 1396 host tests pass (530 VM, 86 CLOS, 16 describe, + others)
- 5 new cross-closure return-from tests added
- New features (condition types, `#x`/`#b`/`#o`, `documentation`, stubs) verified manually in REPL
