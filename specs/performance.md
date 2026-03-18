# CL-Amiga: Performance Optimization Specification

## Goal

Maximize throughput and minimize latency of the CL-Amiga bytecode VM and runtime, with primary focus on the 68020 @ 14MHz / 8MB RAM target. All optimizations must preserve correctness and CL compliance.

## Constraints

| Constraint | Detail |
|-----------|--------|
| CPU | 68020 — no hardware multiply (microcode, ~70 cycles), no hardware divide, limited branch prediction |
| RAM | 8MB — GC pressure directly impacts usability |
| Compiler | C89/C99, m68k-amigaos-gcc (GCC-based), vbcc as secondary |
| Correctness | All 656+ host tests and Amiga test suite must continue to pass |
| Code size | Binary must remain practical for floppy distribution (~100KB target) |

---

## Tier 1 — High Impact

### 1.1 CLOS Method Dispatch Cache ✅ DONE (b315f2a, enhanced)

**Problem**: Every generic function call performs a full linear scan of all methods, O(n^2) insertion sort by specificity, and repeated CPL walks. No results are cached. For a GF with 50 methods, this is the dominant bottleneck in ASDF, Quicklisp, and FSet.

**Design**: Multi-level dispatch cache with three modes:

**Phase 1: Effective Method Closure (EMF) caching** — cache the fully-built method combination closure, not just the method list. On cache hit: bind `*current-method-args*` and `(apply emf args)` — zero closure allocation per call. Negative caching for no-applicable-method cases.

**Phase 2: Multi-dispatch cache** — nested `eq` hash tables for GFs specializing on 2+ args. `cacheable-p` returns N (number of specialized positions) instead of boolean. For N=1: identical to single-dispatch. For N>1: navigate N-1 intermediate hash tables keyed by `class-of` each arg.

**Phase 3: EQL specializer cache** — mixed EQL/class cache for GFs with `(eql value)` specializers. Each cache level is a `(eql-ht . class-ht)` cons pair. Per-position EQL value sets determine whether to route through the EQL or class hash table.

**Implementation**:
- Expanded `standard-generic-function` from 5→8 slots (`dispatch-cache`, `cacheable-p`, `eql-value-sets`)
- `%compute-gf-cacheable-p`: returns integer N (class positions), `:eql` (has EQL specializers)
- `%build-effective-method`: builds args-independent closure from sorted method list
- `%make-method-chain` / `%make-around-chain`: use `*current-method-args*` dynamic var instead of captured args
- `%gf-dispatch-cached`: nested hash tables for N specialized positions, stores EMF closures
- `%gf-dispatch-eql`: mixed `(eql-ht . class-ht)` cache with per-position EQL value sets
- `%compute-eql-value-sets`: scans methods, builds per-position hash tables of known EQL values
- Invalidation: per-GF on `defmethod` (clears cache + recomputes cacheable-p + EQL sets), all GFs on `defclass`
- 20 new host tests (118 total CLOS) + 30 new Amiga tests; Fiveam 114/114, FSet 17/17 pass

**Expected gain**: 10-100x for dispatch-heavy code paths; eliminates per-call closure allocation.

**Files**: `lib/clos.lisp`, `tests/test_clos.c`, `tests/amiga/run-tests.lisp`

---

### 1.2 Pre-compiled FASL for Boot Files ✅ DONE (7f51164)

**Problem**: `boot.lisp` (1949 lines) and `clos.lisp` (1388 lines) are parsed from source on every startup. The reader, compiler, and macro expander all run at full cost.

**Current behavior**:
- `load_boot_file()` reads `lib/boot.lisp` as source text
- `cl_eval_string("(require \"clos\")")` reads `lib/clos.lisp` as source text
- FASL infrastructure already exists (`fasl.c`, `compile-file`, `.fasl` loading)

**Design**:
- Add a build step that pre-compiles `boot.lisp` and `clos.lisp` to `.fasl`
- Modify `load_boot_file()` to prefer `lib/boot.fasl` over `lib/boot.lisp`
- Modify CLOS require to prefer `lib/clos.fasl` over `lib/clos.lisp`
- Ship `.fasl` files alongside `.lisp` sources
- Add Makefile target: `make fasl` that builds the host binary and uses it to compile the boot files

**Fallback**: If `.fasl` is missing or load fails, fall back to source loading (current behavior).

**Expected gain**: 2-5x faster startup on Amiga.

**Files**: `src/core/builtins_io.c` (load paths), `Makefile`, `Makefile.cross`

---

### 1.3 Compiler Constant Folding

**Problem**: No compile-time evaluation — `(+ 1 2)` emits `CONST 1, CONST 2, ADD` (3 opcodes, 2 constant pool entries) instead of `CONST 3` (1 opcode, 1 entry).

**Design**:
- After compiling arguments of arithmetic/logic calls, check if all arguments are compile-time constants
- Supported operations for folding: `+`, `-`, `*`, `ash`, `logand`, `logior`, `logxor`, `not`, `1+`, `1-`
- For constant conditional tests (`(if nil ...)`, `(if t ...)`), emit only the live branch (dead code elimination)
- Implementation: in `compile_call()`, detect known pure functions with all-constant args, evaluate at compile time, emit single `CONST` with result

**Scope**: Only fold fixnum arithmetic and boolean constants initially. Bignum/float/ratio folding can follow later.

**Expected gain**: 10-15% smaller bytecode, fewer VM dispatch cycles.

**Files**: `src/core/compiler.c`

---

### 1.4 VM Computed Goto Dispatch

**Problem**: The VM dispatch loop uses `switch(opcode)` with 100+ cases. On 68020, the compiler generates an indirect jump through a bounds-checked table, adding overhead on every opcode.

**Design**:
- Replace `switch(op)` with GCC computed goto: `static void *dispatch_table[] = {&&op_nop, &&op_load, ...}; goto *dispatch_table[op];`
- Each opcode handler ends with `DISPATCH_NEXT;` macro that fetches the next opcode and jumps
- Guard with `#ifdef __GNUC__` — fall back to switch for non-GCC compilers (vbcc)
- Define macros:
  ```c
  #ifdef __GNUC__
  #define DISPATCH_NEXT  do { op = *ip++; goto *dispatch_table[op]; } while(0)
  #define CASE(label)    op_##label:
  #else
  #define DISPATCH_NEXT  break
  #define CASE(label)    case OP_##label:
  #endif
  ```

**Expected gain**: 5-15% overall VM throughput (eliminates branch through single prediction point).

**Risk**: Large refactor of vm.c (2654 lines). Must be done carefully with full test coverage.

**Files**: `src/core/vm.c`

---

## Tier 2 — Medium Impact

### 2.1 VM Debug Instrumentation Gating ✅ DONE (7f51164)

**Problem**: `dbg_last_op`, `dbg_last_ip`, and trace buffer writes execute on every opcode dispatch in production builds.

**Design**:
- Move all `dbg_*` variable updates behind `#ifdef DEBUG_VM`
- Move trace buffer writes behind `#ifdef DEBUG_VM`
- Keep the zero-opcode trap (`__builtin_expect(op == 0x00, 0)`) — it's already branch-predicted away

**Expected gain**: 3-5% (removes unnecessary memory writes per dispatch cycle).

**Effort**: Very low — mechanical `#ifdef` wrapping.

**Files**: `src/core/vm.c`

---

### 2.2 Rotate-XOR Hash Function ✅ DONE (7f51164)

**Problem**: FNV-1a hash uses `hash *= 16777619`, which takes ~70 cycles on 68020 (microcode multiply). Symbol interning and hash table operations are pervasive.

**Current code** (`src/core/symbol.c`):
```c
hash = 2166136261u;
for (...) { hash ^= byte; hash *= 16777619u; }
```

**Design**:
- Replace with rotate-XOR hash:
  ```c
  hash = 0;
  for (...) { hash = ((hash << 5) | (hash >> 27)) ^ byte; }
  ```
- Rotate + XOR uses shift and OR — 2-3 cycles on 68020 vs ~70 for multiply
- Verify hash quality: run collision test on CL symbol corpus (2000+ symbols)
- Apply to all hash functions: symbol hash, string hash, `sxhash`, hash table key hash

**Expected gain**: 2-5x faster hashing. Affects every symbol intern, hash table lookup, and `sxhash` call.

**Risk**: Hash distribution change could affect hash table load balance. Validate with collision statistics.

**Files**: `src/core/symbol.c`, `src/core/builtins_hashtable.c`

---

### 2.3 Hash Table Power-of-2 Buckets ✅ DONE (7f51164)

**Problem**: Bucket index computed as `hash % bucket_count` — division is extremely slow on 68020 (~140 cycles for 32-bit divide).

**Design**:
- Ensure all hash table bucket counts are powers of 2
- Replace `hash % bucket_count` with `hash & (bucket_count - 1)`
- Adjust growth strategy: double bucket count on resize (already natural for power-of-2)
- Store `bucket_mask = bucket_count - 1` in hash table header to avoid recomputing
- Also store hash value alongside each key in chain entries — compare hash before calling equality test (avoids expensive EQUAL/EQUALP on hash mismatch)

**Expected gain**: 5-10% on hash-table-heavy code (CLOS slot lookup, symbol tables, ASDF).

**Files**: `src/core/builtins_hashtable.c`, possibly `src/core/mem.c` (hash table struct layout)

---

### 2.4 Set Operations as C Builtins

**Problem**: `union`, `intersection`, `set-difference`, `subsetp` are pure Lisp with O(n*m) nested `dolist` + `member`. Hot path in ASDF package operations.

**Current code** (`lib/boot.lisp`):
```lisp
(defun intersection (list1 list2 &key key test test-not)
  (let ((result nil))
    (dolist (x list1 (nreverse result))
      (when (%set-member x list2 key test test-not)
        (push x result)))))
```

**Design**:
- Implement `intersection`, `union`, `set-difference`, `set-exclusive-or`, `subsetp` as C builtins
- For small lists (< 32 elements): use current O(n*m) algorithm (cache-friendly, no allocation)
- For large lists: build temporary hash set from shorter list, probe with longer list — O(n+m)
- Support `:key`, `:test`, `:test-not` keyword arguments
- Remove Lisp implementations from `boot.lisp` (or keep as fallback behind feature flag)

**Expected gain**: 5-20x for large sets; modest gain for small sets due to eliminated interpreter overhead.

**Files**: new `src/core/builtins_set.c` or add to `src/core/builtins_sequence.c`, `lib/boot.lisp`

---

### 2.5 Free-List Size Class Segregation

**Problem**: Free-list is a single linked list scanned linearly. After heap fragmentation, allocation degrades to O(n) where n = number of free blocks.

**Design**:
- Segregate free list into size classes: 8, 16, 32, 64, 128, 256, 512, 1024, 2048+ bytes
- On free: insert block into appropriate size class list
- On alloc: check exact size class first, then next larger class (split if needed)
- On GC sweep: coalesce adjacent free blocks, then re-segregate
- Data structure: array of 9 list heads (one per size class) — 36 bytes overhead

**Expected gain**: O(1) allocation for common sizes (cons cells = 8 bytes, small vectors = 16-32 bytes). Eliminates pathological O(n) scans.

**Files**: `src/core/mem.c`

---

### 2.6 Build Flags: `-O3` and `-flto` ✅ DONE (7f51164)

**Problem**: Cross-compile uses `-O2`. LTO would allow GCC to inline across translation units — critical for small helper functions called from the VM loop (`cl_vm_push`, `cl_vm_pop`, type checks).

**Design**:
- Change `Makefile.cross` CFLAGS from `-O2` to `-O3`
- Add `-flto` to both CFLAGS and LDFLAGS
- Verify binary size stays reasonable (< 150KB)
- Verify all tests still pass (aggressive optimization can expose UB)
- If binary size is a concern, use `-Os -flto` as alternative

**Expected gain**: 5-10% from cross-TU inlining and loop optimizations.

**Risk**: `-O3` may expose latent undefined behavior. Run full test suite. `-flto` may not be supported by all m68k-amigaos-gcc versions — verify toolchain support.

**Files**: `Makefile.cross`

---

## Tier 3 — Lower Impact / Quick Wins

### 3.1 CLOS Slot Access Optimization ✅ DONE

**Problem**: `slot-value` performs a hash table lookup on every access. Defstruct already compiles direct `%struct-ref` with known offset.

**Design**:
- At class finalization, `%install-optimized-accessors` (CLOS) and `%install-bootstrap-accessors` (pre-CLOS) generate accessor closures that capture the slot index as a fixnum
- Accessor body: `(%struct-ref obj idx)` — direct array access, no hash table lookup
- `%compute-effective-slots` reordered to process CPL from least-specific to most-specific, ensuring parent slots occupy stable indices across all subclasses
- Reader closures include `*slot-unbound-marker*` check and `slot-unbound` GF call
- Direct `slot-value` calls remain hash-table-based (unchanged)
- User `:before`/`:after`/`:around` methods on accessors still work (accessor is a regular GF)

**Expected gain**: 1.5-2x per slot access.

**Files**: `lib/clos.lisp`, `tests/test_clos.c`, `tests/amiga/run-tests.lisp`

---

### 3.2 Keyword Argument Pre-computation

**Problem**: Keyword argument matching in `OP_CALL` is O(n_keys * n_supplied) — nested loop scanning supplied args for each defined keyword.

**Design**:
- At function definition time, build a small hash map or sorted array of `(keyword-symbol → local-slot-index)` pairs
- At call site, iterate supplied keyword args once, looking up each in the map — O(n_supplied) total
- For functions with <= 4 keyword args, use linear scan (faster than hash for small n)
- Store the keyword map in the bytecode function object

**Expected gain**: Noticeable for functions with many keyword args (e.g., `make-instance`, `format`).

**Files**: `src/core/vm.c`, `src/core/compiler.c`

---

### 3.3 `cl_mv_count` Write Reduction

**Problem**: Many opcodes unconditionally set `cl_mv_count = 1` even when it is already 1. This is a memory write on every arithmetic op, load, store, etc.

**Design**:
- Only set `cl_mv_count = 1` in opcodes that may follow a multiple-values-producing opcode (e.g., after `OP_CALL`, `OP_VALUES`)
- Alternatively, set `cl_mv_count = 1` once at the top of the dispatch loop and only change it in `OP_VALUES` / `OP_CALL` that produce multiple values
- Audit all opcodes to determine which ones actually need to reset mv_count

**Expected gain**: Small (1-2%) — eliminates unnecessary store on hot path.

**Files**: `src/core/vm.c`

---

## Implementation Order

Recommended sequence balancing impact vs. risk:

| Phase | Items | Status |
|-------|-------|--------|
| 1 | 2.1 (debug gating), 2.2 (hash function), 2.6 (build flags) | ✅ DONE (7f51164) |
| 2 | 2.3 (HT power-of-2), 1.2 (pre-FASL boot), 3.3 (mv_count) | ✅ 2.3+1.2 DONE (7f51164), 3.3 pending |
| 3 | 1.1 (CLOS dispatch cache) | ✅ DONE (b315f2a) |
| 4 | 1.3 (constant folding), 2.4 (set ops in C) | Pending |
| 5 | 1.4 (computed goto), 2.5 (free-list segregation) | Pending |
| 6 | 3.1 (slot access), 3.2 (keyword pre-comp) | ✅ 3.1 DONE, 3.2 pending |

## Validation

- All 656+ host tests must pass after each phase
- Amiga test suite must pass via FS-UAE after each phase
- Integration tests: `load-and-test-5am.lisp` (57/57), `load-and-test-fset.lisp` (17/17)
- Quicklisp install + `(ql:quickload :alexandria)` must succeed at 24M heap
- No increase in heap usage beyond 5% for equivalent workloads
- Binary size must stay under 150KB for Amiga target
