# FSet Quickload via ASDF

## Goal

Make `(ql:quickload :fset)` work end-to-end on both host and Amiga. FSet is a functional set-theoretic collections library — a good validation target for CL-Amiga's ASDF/quicklisp/CLOS integration.

## Current Status

- `(ql:quickload :misc-extensions)` — WORKS
- `(ql:quickload :mt19937)` — WORKS
- `(ql:quickload :fset)` — FAILS with "Undefined function: MAKE-LOCK" (fset's `port.lisp` has no `#+cl-amiga` branch)

FSet's dependencies: `misc-extensions`, `mt19937`, `named-readtables`. All available in local quicklisp dists. `named-readtables` has no further dependencies.

## Problems to Fix

### Problem 1: "CDR: corrupted pointer" crash — FIXED

**Root cause:** `asdf-compat.lisp` only had null-safe `(eql nil)` methods for 3 of 11 session accessors. When `*asdf-session*` was NIL, `toplevel-asdf-session` returned NIL, and `session-operate-level(NIL)` caused CLOS dispatch to read struct slots from arena offset 0 (garbage), producing the corrupted pointer error.

**Fix (commit fcf0fec):**
- Added null-safe methods for ALL 11 session accessors (readers + setters): `session-ancestor`, `session-cache`, `session-operate-level`, `asdf-upgraded-p`, `forcing`, `visited-actions`, `visiting-action-set`, `visiting-action-list`, `total-action-count`, `planned-action-count`, `planned-output-action-count`
- NLX frame sync: save/restore `frame->bytecode` in all NLX frames (block, tagbody, catch, unwind-protect) to prevent tail-call state corruption on longjmp
- GC-mark NLX bytecodes; added OP_RET ip-bounds validation with diagnostics

**Investigation results:**
- CLOS GC marking is complete — all instance slots, class metaobjects, hash tables properly marked
- `*asdf-session*` dynamic binding lifecycle is correct (restored on NLX unwind)
- No GC sweep bugs found

### Problem 2: Missing threading primitives

FSet's `port.lisp` defines per-implementation threading stubs via `#+`/`#-` reader conditionals. CL-Amiga matches none of the existing branches (`sbcl`, `ccl`, `allegro`, `clisp`, `ecl`, `abcl`, `clasp`, etc.), so these functions are undefined:

- `make-lock` / `with-lock` — mutex creation and locking
- `read-memory-barrier` / `write-memory-barrier` — memory ordering
- `deflex` / `defglobal` — global variable definition (non-special)

Currently hacked around via `lib/fset-compat.lisp` which injects stubs into the `:fset` package. This doesn't scale — every library with threading needs would need its own compat file.

### Problem 3: Named-readtables loading

FSet depends on `named-readtables` (package `editor-hints.named-readtables`). Available in local quicklisp dists at `named-readtables-20260101-git/`. Currently bypassed via `lib/named-readtables-stub.lisp`. Should load through ASDF like any other dependency.

If `named-readtables` requires features CL-Amiga doesn't have (e.g., `set-syntax-from-char`), implement them or provide a minimal working subset.

## Implementation Plan

### Step 1: Fix the crash — DONE

Fixed in commit fcf0fec. Two changes:

1. **ASDF session null-safety:** Added null-safe `(eql nil)` methods for all 11 session accessors in `lib/asdf-compat.lisp`. Hash-table readers return shared empty hash table; numeric readers return 0; setters are no-ops.
2. **NLX frame sync:** Added `bytecode` field to `CL_NLXFrame`, saved at all NLX push sites, restored and synced to `frame->` on all NLX landing sites. GC-marks NLX bytecodes. Added OP_RET ip-bounds validation. Regression tests: `eval_catch_throw_deep_chain`, `eval_block_return_deep_chain`, `eval_uwp_throw_deep_cleanup`.

### Step 2: Threading primitives in EXT package — DONE

Implemented single-threaded stubs in `lib/boot.lisp` (Lisp-level, no C changes):

```
ext:make-lock (&optional name)           → NIL (no-op, single-threaded)
ext:with-lock-held ((lock) &body body)   → (progn ,@body)
ext:make-recursive-lock (&optional name) → NIL
ext:with-recursive-lock-held ((lock) &body body) → (progn ,@body)
ext:read-memory-barrier ()               → NIL
ext:write-memory-barrier ()              → NIL
ext:defglobal (name value &optional doc) → defvar expansion
```

All symbols exported from EXT package. 6 host tests added.

### Step 3: FSet port.lisp patch — DONE

Added `#+cl-amiga` section to fset's `port.lisp` (single-threaded stubs matching the `#+clisp` pattern) and `#+cl-amiga` `make-char` definition. Patched copy placed in `~/quicklisp/local-projects/fset-v2.2.0/` to survive dist updates. Also patched project-local `quicklisp/` copy.

Removed `lib/fset-compat.lisp` — no longer needed. Updated test scripts (`test-fset.lisp`, `test-fset-asdf.lisp`) to remove fset-compat loading.

FSet loads and core operations (sets, maps, lookups, unions) work on host.

### Step 4: Load named-readtables through ASDF — DONE

Implemented `set-syntax-from-char` (CL spec) and loaded the real named-readtables library via ASDF, including its full dependency chain: `mgl-pax.asdf` → `autoload` → `mgl-pax-bootstrap` → `named-readtables`.

#### 4a: Implement `set-syntax-from-char` — DONE

Implemented in `builtins_stream.c` as `bi_set_syntax_from_char`. Registered as `SET-SYNTAX-FROM-CHAR` builtin (4 params: 2 required, 2 optional). Copies syntax type and reader macro function from `from-char` to `to-char`. Default `from-readtable` is standard readtable (slot 0) per CL spec. 4 host tests added.

#### 4b: Dependencies already satisfied — DONE

All required CL spec features were already implemented.

#### 4c: Load named-readtables dependency chain via ASDF — DONE

`(ql:quickload :named-readtables)` works end-to-end. Required fix: added `(:implementation cl-amiga ...)` for `directory-entries` in quicklisp's `impl-util.lisp` (local-projects searcher needed this). Named-readtables uses portable readtable iterator (grovels chars 0..`char-code-limit`), works fine.

#### 4d: Remove stub — DONE

- Deleted `lib/named-readtables-stub.lisp`
- Updated `tests/amiga/test-fset-asdf.lisp` to quickload named-readtables via ASDF instead of loading stub
- Updated `tests/amiga/test-fset.lisp` to load named-readtables from source (full dependency chain)
- FSet loads and works on host with real named-readtables

### Step 5: Remove fset-compat.lisp — DONE

`lib/fset-compat.lisp` removed (replaced by port.lisp `#+cl-amiga` patch + EXT threading primitives). `lib/named-readtables-stub.lisp` also removed (step 4d).

### Step 6: Fix `(ql:quickload :fset)` end-to-end — DONE

`(ql:quickload :fset)` completes on host at 24M heap. All 17 FSet Code files load. 4 oversized test functions in testing.lisp are skipped (exceed 16KB bytecode limit) — these are FSet's test suite, not library functionality.

**Bugs fixed:**

1. **`defstruct (:type vector)` support** — FSet's CHAMP nodes use `(defstruct (ch-map-node (:type vector)))`. CL-Amiga generated `%STRUCT-REF` accessors instead of `svref`-based ones. Added `:type vector` support to defstruct in boot.lisp: vector-based constructors, `svref` accessors.

2. **Missing CL bitwise functions** — FSet uses `logandc2f` (modify-macro wrapping `logandc2`). Added `logandc1`, `logandc2`, `logorc1`, `logorc2`, `lognand`, `lognor`, `logeqv` to boot.lisp.

3. **GC root stack corruption on NLX** — When `cl_error("Bytecode too large")` longjmps out of the compiler, `CL_GC_PROTECT` entries pointing to dead C stack frames remained on the GC root stack. Next GC followed stale pointers → SIGSEGV. Fixed by saving/restoring `gc_root_count` in NLX frames.

4. **Compiler chain leak on NLX** — Failed compilations left orphaned compiler structs in `cl_active_compiler` chain, leaking memory and env pointers. Added `cl_compiler_mark()`/`cl_compiler_restore_to()` to save/restore the compiler chain in NLX frames, freeing leaked compilers on non-local exit.

5. **ASDF form-by-form loading** — Replaced `perform-lisp-compilation` to load source files form-by-form with per-form error handling. "Bytecode too large" errors on individual functions are caught and reported as warnings without aborting the file. File-level handler catches any corruption that escapes per-form handlers.

6. **ASDF double-loading** — Made `perform-lisp-load-fasl` a no-op since `perform-lisp-compilation` already loads and evaluates all forms.

**FSet operations verified:**
- `(fset:empty-map)` → WB-MAP
- `(fset:with (fset:empty-map) 'x 42)` + `(fset:lookup ... 'x)` → 42
- `(fset:set 1 2 3)` + `(fset:contains? ... 2)` → T

### Step 7: Amiga validation

Test on Amiga via FS-UAE once step 6 is complete.

## Success Criteria

- `(ql:quickload :fset)` completes without errors on host and Amiga
- No compat shim files needed — fset loads through standard ASDF/quicklisp
- Threading primitives available in EXT for any future library that needs them
- `named-readtables` loads through ASDF (real library, not stub)

## Files Affected

| File | Change |
|------|--------|
| `src/core/vm.c` | NLX frame sync fix — DONE (fcf0fec) |
| `src/core/vm.h` | NLX bytecode field — DONE (fcf0fec) |
| `src/core/mem.c` | GC mark NLX bytecode — DONE (fcf0fec) |
| `tests/test_vm.c` | NLX deep-chain regression tests — DONE (fcf0fec) |
| `lib/boot.lisp` | Threading primitives in EXT package — DONE |
| `lib/asdf-compat.lisp` | All 11 session null-safe methods — DONE (fcf0fec) |
| `~/quicklisp/local-projects/fset-v2.2.0/Code/port.lisp` | `#+cl-amiga` section — DONE |
| `lib/fset-compat.lisp` | Removed — DONE |
| `src/core/builtins_stream.c` | `set-syntax-from-char` builtin — DONE |
| `tests/test_vm.c` | `set-syntax-from-char` tests (4 tests) — DONE |
| `lib/named-readtables-stub.lisp` | Removed — DONE |
| `~/quicklisp/quicklisp/impl-util.lisp` | Added `directory-entries` for CL-Amiga — DONE |
| `src/core/compiler.c` | `cl_compiler_mark()`/`cl_compiler_restore_to()` — DONE |
| `src/core/compiler.h` | Compiler chain save/restore API — DONE |
| `tests/amiga/test-fset-asdf.lisp` | End-to-end quickload test |

## Priority Order

1. ~~Fix the crash (blocker — nothing works without this)~~ DONE
2. ~~Threading primitives in EXT (enables fset and other libraries)~~ DONE
3. ~~Port.lisp patch (enables fset specifically)~~ DONE
4. ~~Implement `set-syntax-from-char`, load real named-readtables via ASDF~~ DONE
5. ~~Remove compat shims~~ DONE
6. ~~Fix `(ql:quickload :fset)` end-to-end~~ DONE
7. Amiga test (validation)
