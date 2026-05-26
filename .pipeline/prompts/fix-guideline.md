# CL-Amiga — project rules for AI review/fix

CL-Amiga is a Common Lisp environment for AmigaOS 3+, written in C89/C99, with a
single-pass bytecode compiler, a stack-based VM, and a **compacting (moving)**
mark-and-sweep GC. Heap pointers are **arena-relative byte offsets**, not raw
pointers. Enforce these rules when reviewing or fixing code:

- **GC safety (critical).** Any `CL_Obj` C local held across an *allocating*
  call (`cl_alloc`, `cl_cons`, `cl_make_string`, `cl_make_vector`,
  `cl_make_struct`, `cl_make_symbol`, `cl_vm_apply`, or anything that calls
  these) MUST be protected with `CL_GC_PROTECT(var)` before and
  `CL_GC_UNPROTECT(n)` after. The classic bug is an iterative list-building
  loop with `result`/`tail` locals and `cl_cons()` inside — the partial list
  is invisible to the GC and will be swept or left holding a stale offset after
  compaction. Values already on the VM stack (`cl_vm.stack`) or in builtin
  `args[]` are GC-rooted — do not double-protect those.

- **32-bit-clean heap structs.** No `size_t` or pointer-sized fields in heap
  objects; everything sized must be explicit `uint32_t`/`int32_t`. The target
  is 32-bit.

- **C89/C99 only.** No C11+ features.

- **HyperSpec conformance.** Common Lisp semantics — and the tests that assert
  them — must match the ANSI HyperSpec, not merely observed behavior.

- **Tests are mandatory.** Every feature needs tests; every bug fix needs a
  *regression* test that would fail before the fix.

- **Efficiency matters.** Target is a 68020 @ 14 MHz with 8 MB RAM — flag
  needless allocation, copying, or per-call overhead on hot paths.

- **Debug output** must be guarded by preprocessor flags (`#ifdef DEBUG_GC`,
  `DEBUG_COMPILER`, `DEBUG_VM`) — never left unconditional.

Report only substantive problems (bugs, GC-safety, memory/32-bit violations,
C89/C99 violations, HyperSpec deviations, missing/incorrect tests, security).
No style nits, no speculation.
