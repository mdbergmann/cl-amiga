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

### 1.4 VM Computed Goto Dispatch ✅ DONE

**Problem**: The VM dispatch loop uses `switch(opcode)` with 100+ cases. On 68020, the compiler generates an indirect jump through a bounds-checked table, adding overhead on every opcode.

**Design**:
- Replace `switch(op)` with GCC computed goto: `static void *dispatch_table[256]` with labels-as-values
- Each opcode handler ends with `VM_BREAK` macro that fetches the next opcode and jumps
- Guard with `#ifdef __GNUC__` — fall back to switch for non-GCC compilers (vbcc)
- Disable with `-DCL_NO_COMPUTED_GOTO` if needed
- Four macros abstract both paths: `VM_CASE(op)`, `VM_BREAK`, `VM_DISPATCH()`, `VM_DEFAULT`

**Implementation**:
- 256-entry dispatch table with designated initializers; unassigned slots → unknown opcode handler
- All 71 opcode handlers use `VM_CASE`/`VM_BREAK` macros — single source for both paths
- Both paths tested: computed goto (default) and switch fallback (`-DCL_NO_COMPUTED_GOTO`)
- All 658+ host tests, fiveam 114/114, FSet 17/17 pass on both paths

**Expected gain**: 5-15% overall VM throughput (eliminates branch through single prediction point).

**Files**: `src/core/vm.c`

---

### 1.5 TLV Bypass (Thread-Local Value Fast Path) ✅ DONE

**Problem**: After threading, every `cl_symbol_value()` call probes the TLV hash table (30-50 cycles average) even when no dynamic bindings are active, which is the common case.

**Design**:
- Add `tlv_entry_count` field to `CL_Thread` — tracks number of active TLV entries
- Incremented in `cl_tlv_set()` on new entry creation, decremented in `cl_tlv_remove()`
- In `cl_symbol_value()`, `cl_set_symbol_value()`, `cl_symbol_boundp()`: check `tlv_entry_count == 0` before probing TLV table — skip entirely when no dynamic bindings active
- `cl_tlv_snapshot()` recomputes count from copied table for thread inheritance

**Expected gain**: Eliminates 30-50 cycle TLV probe on every global variable read when no dynamic bindings are active (the common case). Effectively removes the threading tax from `OP_GLOAD`/`OP_GSTORE`.

**Files**: `src/core/thread.h`, `src/core/thread.c`

---

### 1.6 Hash Table Rehashing + Hash Distribution Fix ✅ DONE (3c58761)

**Problem**: Hash tables used a fixed 16-bucket array with no rehashing. Inserting 100K entries created chains of ~6250 average length — O(n) lookup. Additionally, the EQUAL/EQUALP hash function for fixnums returned raw tagged bits (`(uint32_t)obj`), wasting 50% of buckets for sequential integer keys (all fixnums are odd due to tag bit).

**Design**:
- **Hash mixer**: Extract `hash_mix()` helper (xor-shift-multiply avalanche), apply to all fixnum/identity paths including EQUAL/EQUALP (was only used for EQ)
- **Growable buckets**: Add `bucket_vec` field to `CL_Hashtable` — when `CL_NIL`, use inline `buckets[]` flexible array; after rehash, points to a `CL_Vector` holding the new bucket array
- **Automatic rehashing**: `ht_maybe_rehash()` triggers when load factor > 75%. Allocates new `CL_Vector` with doubled bucket count, redistributes entries by relinking existing cons cells (zero allocation during redistribution). GC-safe: protects `ht_obj` during vector allocation
- **`:size` parameter**: `make-hash-table :size N` now computes initial bucket count as `ceil(N / 0.75)` rounded to power-of-2, preventing immediate rehash after filling
- **Updated all access paths**: `ht_get_buckets()` helper for bucket indirection in gethash, setf-gethash, remhash, maphash, clrhash, hash-table-pairs, and `builtins_struct.c` CLOS class table lookup
- **GC marking**: marks `bucket_vec` field; when non-NIL, vector's own marking traverses bucket contents

**Result**: hash 100K benchmark: **4.129s → 0.037s (112x speedup)**. ECL: 0.025s.

**Files**: `src/core/builtins_hashtable.c`, `src/core/types.h`, `src/core/mem.c`, `src/core/builtins_struct.c`

---

### 1.7 Bignum 32-bit Limb Multiplication on 64-bit Hosts ✅ DONE (fbb7e29)

**Problem**: Bignum multiplication used 16-bit limbs with `uint32_t` intermediates (designed for 68020). On 64-bit hosts, this wastes the processor's native word size — `1000!` requires ~534 16-bit limbs, giving ~285K inner-loop iterations for schoolbook O(n²) multiply.

**Design**:
- **`CL_Bignum` heap format unchanged**: 16-bit limbs for Amiga compatibility, FASL serialization, GC
- **Pack/unpack at computation boundary**: `pack_16_to_32()` pairs 16-bit limbs into 32-bit limbs; `unpack_32_to_16()` converts back after computation
- **`bignum_mul_mag32()`**: schoolbook multiplication with `uint32_t` limbs and `uint64_t` intermediates — halves limb count, giving ~4x fewer inner-loop iterations
- **Stack buffers for small operands** (≤256 32-bit limbs), heap-allocated for larger
- **Amiga path unchanged**: `#else` branch keeps 16-bit limbs with `uint32_t` products
- Guarded by `#ifdef PLATFORM_POSIX`

**Result**: factorial 1000 x5000 benchmark: **5.908s → 2.677s (2.2x speedup)**. ECL (GMP): 1.348s. Remaining gap is algorithmic (schoolbook O(n²) vs GMP's Karatsuba O(n^1.585)).

**Files**: `src/core/bignum.c`

---

## Benchmark Summary (vs ECL 24.5.10)

Benchmark suite: `bench.lisp` — factorial, fibonacci, Takeuchi, list-ops, hash-table.

| Benchmark | CL-Amiga (before) | CL-Amiga (after) | ECL | vs ECL |
|-----------|-------------------|------------------|-----|--------|
| factorial 1000 x5000 | 5.908s | **2.677s** | 1.348s | 2.0x slower |
| fib-iter 1000 x5000 | 0.590s | 0.595s | 0.919s | **1.5x faster** |
| tak 18 12 6 x100 | 0.469s | 0.473s | 0.853s | **1.8x faster** |
| list-ops 10000 | 0.032s | 0.034s | 0.062s | **1.8x faster** |
| hash 100000 | 4.129s | **0.037s** | 0.025s | 1.5x slower |

CL-Amiga beats ECL on 3/5 benchmarks. The two remaining gaps are in bignum-heavy arithmetic (schoolbook vs Karatsuba) and hash table implementation (cons-chain vs open addressing).

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

### 3.1 CLOS Slot Access Optimization

**Problem**: `slot-value` performs a hash table lookup on every access. Defstruct already compiles direct `%struct-ref` with known offset.

**Design**:
- At class finalization, generate optimized accessor closures that capture the slot index
- `(slot-value obj 'x)` with a finalized class → `(%struct-ref obj 3)` (direct offset)
- For generic `slot-value` calls where class is not known at compile time, keep hash table fallback
- Accessor methods generated by `defclass` `:accessor`/`:reader`/`:writer` should use direct offset

**Expected gain**: 1.5-2x per slot access.

**Files**: `lib/clos.lisp`

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
| 4 | 1.4 (computed goto), 1.5 (TLV bypass) | ✅ DONE |
| 5 | 1.6 (HT rehash + hash fix), 1.7 (32-bit limb bignum mul) | ✅ DONE (3c58761, fbb7e29) |
| 6 | 1.3 (constant folding), 2.4 (set ops in C) | Pending |
| 7 | 2.5 (free-list segregation) | Pending |
| 8 | 3.1 (slot access), 3.2 (keyword pre-comp) | Pending |

## Validation

- All 656+ host tests must pass after each phase
- Amiga test suite must pass via FS-UAE after each phase
- Integration tests: `load-and-test-5am.lisp` (57/57), `load-and-test-fset.lisp` (17/17)
- Quicklisp install + `(ql:quickload :alexandria)` must succeed at 24M heap
- No increase in heap usage beyond 5% for equivalent workloads
- Binary size must stay under 150KB for Amiga target
