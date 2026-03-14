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

### Step 4: Load named-readtables through ASDF

Remove `lib/named-readtables-stub.lisp` from the load sequence. Let ASDF load the real `named-readtables` from quicklisp dists. If loading fails due to missing CL features, implement those features:

- `set-syntax-from-char` — if needed by named-readtables (check first)
- Any other missing readtable operations

If named-readtables loads without issues, no changes needed. If it requires features too complex to implement now, keep the stub but register it as a proper ASDF system so ASDF's dependency resolution works transparently.

### Step 5: Remove fset-compat.lisp — DONE

`lib/fset-compat.lisp` removed (replaced by port.lisp `#+cl-amiga` patch + EXT threading primitives). `lib/named-readtables-stub.lisp` still needed until named-readtables loads through ASDF (step 4).

### Step 6: End-to-end test

Create `tests/amiga/test-fset-asdf.lisp`:

```lisp
(load "lib/asdf.lisp")
(load quicklisp-setup)
(ql:quickload :fset)

(let ((s (fset:set 1 2 3 4 5)))
  (assert (fset:contains? s 3))
  (assert (not (fset:contains? s 9))))

(let ((m (fset:map ("a" 1) ("b" 2))))
  (assert (= (fset:lookup m "b") 2)))

(format t "~%ALL TESTS PASSED~%")
```

Test on both host and Amiga via FS-UAE.

## Success Criteria

- `(ql:quickload :fset)` completes without errors on host and Amiga
- No compat shim files needed — fset loads through standard ASDF/quicklisp
- Threading primitives available in EXT for any future library that needs them
- `named-readtables` loads through ASDF (real library or properly registered stub)

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
| `lib/named-readtables-stub.lisp` | Still needed until step 4 |
| `tests/amiga/test-fset-asdf.lisp` | End-to-end quickload test |

## Priority Order

1. ~~Fix the crash (blocker — nothing works without this)~~ DONE
2. Threading primitives in EXT (enables fset and other libraries)
3. Port.lisp patch (enables fset specifically)
4. Named-readtables via ASDF (cleanup)
5. Remove compat shims (cleanup)
6. Amiga test (validation)
