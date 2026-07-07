# Deduplication & Simplification Audit

Date: 2026-07-06. Tree-wide sweep for code duplication and simplification
opportunities (six parallel exploration passes covering builtins, compiler,
VM/mem/fasl, IO/printer/format, platform/JIT/thread, and type/condition/misc).

All findings are **behavior-preserving**. Hot-path items are flagged and use
macros / X-macros so generated code is identical (zero per-iteration cost).
Nothing here has been implemented yet — this is a backlog, ordered by
impact ÷ risk.

Rough tree-wide reduction if fully done: **~2,000+ lines**, plus retirement of
several documented "must-fix-in-N-places" bug classes (GC lockstep switches,
FASL/LOAD path fragmentation, GC-protect idiom copies).

---

## Tier 1 — biggest wins, lowest risk (start here)

### STATUS (2026-07-06)
- **T1.1 DONE** — shared `defun` in builtins.h; 20 copies removed; debugger.c's
  exporting variant renamed `defun_exported`. Net −240 lines. `make test` green.
- **T1.4 DONE** — generic `FaslRegistry` + `fasl_reg_*` helpers; 8 wrappers +
  GC walkers use it. `make test` + `make test-gc-stress` (366/366) green.
- **T1.2 DONE** — `GC_WALK_OBJ_CHILDREN` X-macro shared by gc_mark_children /
  gc_update_children; two tail hooks (BYTECODE reloc / STREAM outbuf pin).
  Binary byte-size unchanged (identical codegen). host + gc-stress 366/366;
  Amiga FS-UAE 3625/0 (incl. JIT loop bench exercising the native_relocs arm).
- **T1.3 DEFERRED** — thread-root mirror is NOT a clean 1:1 (debug breadcrumbs,
  JIT-stack scan, compiler/VM external hooks, gc_roots skip, mv_values/pending
  exclusion rules). Higher risk, smaller payoff. Do as its own change WITH
  Amiga FS-UAE validation (past Amiga-only GC bugs lived in thread-root/JIT
  forwarding). Not started.

### T1.1 `defun` copy-pasted in 20 files (~200 lines) — VERIFIED — DONE
Every `builtins_*.c` (plus `debugger.c`, `float_math.c`) carries the identical
11-line GC-protected `static void defun(name, func, min, max)`:
```c
static void defun(const char *name, CL_CFunc func, int min, int max) {
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_cl);
    CL_Obj fn; CL_Symbol *s;
    CL_GC_PROTECT(sym);
    fn = cl_make_function(func, sym, min, max);
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    CL_GC_UNPROTECT(1);
}
```
`builtins.c:62` already has the one-line wrapper over the shared
`cl_register_builtin(name, func, min, max, cl_package_cl)`.
21 files match `static void defun(`: builtins.c, builtins_describe.c,
builtins_stream.c, builtins_inspect.c, builtins_type.c, builtins_io.c,
builtins_struct.c, builtins_array.c, debugger.c, builtins_package.c,
builtins_strings.c, builtins_mutation.c, builtins_lists.c, builtins_pathname.c,
builtins_sequence2.c, builtins_random.c, builtins_bitvector.c,
builtins_hashtable.c, builtins_condition.c, float_math.c, builtins_sequence.c.
`ffi_defun` (builtins_ffi.c:88) differs only by an extra `cl_export_symbol`.
**Fix:** expose one `cl_defun()` in `builtins.h`, delete the 20 copies; make
`ffi_defun` a 3-line wrapper. Init-time only, zero perf risk.

### T1.2 GC twin switches `gc_mark_children` / `gc_update_children` (~170 lines) — VERIFIED
`mem.c` — two ~190-line `switch(type)` blocks over ~22 object types
(gc_mark_children @1414, gc_update_children @2760, comment says "mirrors
gc_mark_children"). Identical slot lists; differ only in
`gc_mark_push(x)` vs `gc_update_slot(&x)`. Only two arms genuinely differ:
TYPE_BYTECODE (update patches native_relocs), TYPE_STREAM (mark pins outbuf).
Already visible drift: TYPE_STRING listed in update's no-op arm, omitted in mark.
**Fix:** define slot layout once via X-macro
`CL_OBJ_SLOTS(ptr, type, VISIT)`; instantiate with `#define VISIT(x)
gc_mark_push(x)` and `#define VISIT(x) gc_update_slot(&(x))`; keep the two
special arms hand-written. HOT PATH but macro expansion = identical codegen.
Do as its own commit; run `make test-gc-stress`.

### T1.3 GC thread-root mirror `gc_mark_thread_roots` / `gc_update_thread_roots` (~100 lines)
`mem.c` @2023 / @2958. Same X-macro technique as T1.2. Warm (once per thread
per GC), not per-object. Subtle mv_values / pending-throw exclusion rules are
currently commented in BOTH copies — consolidating removes the drift risk.

### T1.4 FASL reader/writer registry — 8 mirror functions (~110 lines)
`fasl.c` — `cl_fasl_{reader,writer}_{register,unregister,save_count,
restore_count}` (1851-2072) byte-identical except token `reader`↔`writer`.
**Fix:** one generic `FaslRegistry { void **items; uint32_t *owners; int count,
cap; }` + 4 helpers; 8 public fns become one-liners. Cold path.
NOTE: pure refactor of in-memory tables, no on-disk format change → no
`CL_FASL_VERSION` bump needed. (Bump only if wire bytes change.)

---

## Tier 2 — mechanical extractions, clear wins

### STATUS (2026-07-07)
- **T2.1 DONE** — `compile_do_impl(c, form, sequential)` merges do/do*; plus
  shared `compile_implicit_tagbody_body` / `push_loop_nil_block` /
  `emit_loop_epilogue` used by do/do*/dolist/dotimes (4 copies → 1 each).
  compiler_special.c −255 lines net. host + gc-stress 366/366; do/do*/dolist/
  dotimes semantics (parallel vs sequential, boxing capture, return, declare
  strip) verified. Amiga: pending.

- **T2.2 DONE (core)** — `fmt_stage_list(list, &base, who)` replaces the
  "stage sublist on VM stack + CALL-ARGS-LIMIT guard" copy at 3 clean sites
  (~:@{, ~:{, ~?); the function-control apply paths (~@?/~? with a FORMATTER
  function) push the stream first + use the result and stay hand-written.
  builtins_format.c −28 net. host + gc-stress 366/366; ~{}/~:{}/~:@{}/~@{}/~?/
  ~@?/~^ verified. `fmt_skip_params` "3 copies" was a mis-count — the other two
  are trivial `while(*sp==':'||*sp=='@')` 1-liners, not the full scanner; the
  substring→sub-context bracket dance varies too much per site to share
  cleanly (deferred, low value). Amiga: pending.

- **T2.3 DONE (core)** — `cl_resolve_input_namestring(input, out, outsz, who)`
  replaces the parse/merge/coerce/expand-~ block copy-pasted in bi_load,
  bi_compile_file, bi_compile_file_pathname (the f52cb57 truncation-bug
  fragmentation). Also collapsed `cf_process_toplevel_eval_when`'s 4
  near-identical situation loops → one loop + `do_eval`/`do_collect` bools
  (CT+LT / CT / LT / EX; EX≡CT here). builtins_io.c net large reduction.
  host + gc-stress 366/366; load/compile-file/compile-file-pathname round-trip,
  ~-expansion, :if-does-not-exist nil, and all eval-when situations verified
  behavior-identical. Deferred (lower value): the `*load-pathname*` bind/restore
  FASL-cache-vs-source twin inside bi_load, and `fasl_read_magic` — both have
  branch-specific differences; left hand-written. Amiga: pending.

- **T2.4 PARTIAL** — DONE the three biggest, host-testable, GC-safe bignum
  dedups: (1) `bignum_bitop(a,b,op,name)` for LOGAND/LOGIOR/LOGXOR cold tails
  (fixnum fast path kept inline; op loop-invariant so -O3 unswitches it); (2)
  the three 2-limb constructors collapsed onto one static `bignum_from_uint32`
  (int32/uint32 now thin wrappers); (3) `bignum_addsub(a,b,subtract)` shares
  the entire cold add/sub tail (sub = add with b's sign flipped). GC-safety
  preserved: al/bl consumed by non-allocating mag ops before bignum_from_limbs.
  bignum.c ~−170 net. test_bignum 98/98, test_ratio 71/71, host + gc-stress;
  large-operand add/sub sign/cancellation + logand/ior/xor on bignums verified.
  NOT DONE (smaller / more paths, deferred): `limb_alloc`/`limb_free` stack-or-
  heap boilerplate (mul/truncate/mod/gcd/ash), `bignum_remainder` (gcd's inlined
  divmod), `complex_binop`, `cl_bignum_from_u64` covering FFI's 4-limb path.
- **T2.5 DONE (2026-07-07)** — JIT/runtime.c NLX families, two Amiga-validated
  commits (batch 1 `0ef0c7d`, batch 2 `43f867b`; net ~−350 lines across
  jit.c/runtime.c). `jit.c`/`runtime.c` are **m68k-only** (not compiled for the
  host — only codebuf.o is), so every change was validated *only* via
  cross-compile + FS-UAE (3625/0 each). All five bullet groups below are done.
  - **Batch 1 DONE** — runtime.c NLX families + jit.c Family 1 (helper-call
    templates). runtime.c: `nlx_alloc_common(type,tag)` (block/catch/tagbody
    share it; uwprot calls it then overwrites code/constants/bytecode from the
    current VM frame), `nlx_pop_type(type)` (uwprot wrapper appends
    `cl_pending_throw=0`), `nlx_restore_core(nlx)` (SP/FP + 8 marks, shared by
    all four *_post_longjmp) + `nlx_restore_common()` (block/catch = core + full
    MV set + return result; tagbody/uwprot written longhand on top of core),
    `real_cmp(a,b,kind,op)` behind the 4 lt/gt/le/ge JSR entry points. jit.c:
    `emit_helper_call_2` (9 sites: MUL/DIV/PROGV_BIND/PROGV_UNBIND/APPLY/RPLACA/
    RPLACD/NTH_VALUE/CONS) + `emit_helper_call_1(...,flush)` (MV_TO_LIST/CAR/CDR/
    MAKE_CELL flush=1, CELL_REF flush=0) — emitted m68k is byte-identical.
    net −155 lines (committed −314/+171). Amiga FS-UAE 3625/0. host +
    gc-stress green.
  - **Batch 2 DONE** — jit.c Family 3 (`compute_landing_ip`) + Family 2
    (`emit_nlx_setjmp_tail`), both emit-time.
    `compute_landing_ip(offset, base, code_len, &ok)` folds the 10 copies of
    the "signed offset → landing IP" idiom (5 prescan pass `base = ip+operand
    width` since ip still points at the opcode; 5 emit pass plain `ip` since
    already advanced — the `>ip+5`/`>ip+7`/`>ip` variance was correct, not a
    bug, and is now expressed once). `emit_nlx_setjmp_tail(...,push_result,...)`
    folds the byte-identical setjmp→TST/BEQ→post_longjmp→[push D0]→BRA-landing→
    BEQ-patch→commit tail of BLOCK/CATCH/TAGBODY/UWPROT (alloc arg-shape stays
    hand-written per site; `push_result=0` for UWPROT's void post_longjmp).
    Emitted m68k byte-identical. Amiga FS-UAE 3625/0.

### T2.1 Compiler: merge `compile_do` / `compile_do_star` (~200 lines)
`compiler_special.c` @2276 / @2538 — two ~260-line near-duplicates differing
only in parallel-vs-sequential init/step. `compile_let(c, form, int sequential)`
(compiler.c:1989) already demonstrates this exact merge in-tree. Keep
do/do* as one-line wrappers over `compile_do_impl(c, form, int sequential)`.
Plus shared loop building blocks across do/do*/dolist/dotimes (~145 more):
- strip declares → wrap body in tagbody (4 copies) → `compile_implicit_tagbody_body`
- push implicit NIL block (4 copies) → `push_local_nil_block`
- loop epilogue (4 copies) → `emit_loop_epilogue`

### T2.2 FORMAT iteration/body helpers (~130 lines)
`builtins_format.c` — `~{ ~}` "stage list on VM stack with CL_CALL_ARGS_LIMIT
guard" block copy-pasted 5× (fmt_iteration ×2, fmt_recursive,
bi__formatter_inner); "copy substring → run sub-context" bracket dance 5×
(fmt_case_convert, fmt_conditional, fmt_iteration, fmt_justify). Both repeat
VM-stack staging + GC rooting — the bug-prone part.
**Fix:** `fmt_stage_list(list, &base, who)` and
`fmt_run_body(parent, sub, start, end, stream)`. Also `fmt_skip_params()`
scanner (3 copies).

### T2.3 LOAD / COMPILE-FILE path resolution (~140 lines)
`builtins_io.c` — ~40-line parse/merge/coerce/expand-~ input-path block
copy-pasted in `bi_load` (@433), `bi_compile_file` (@1527),
`bi_compile_file_pathname` (@1945); plus `*load-pathname*`/`*load-truename*`
bind + FASL-header + restore twins inside `bi_load` (FASL-cache branch vs
source branch). File's own comments (@264-287) note this fragmentation caused
the `f52cb57` truncation bug → consolidation is a correctness win.
**Fix:** `cl_resolve_input_namestring()`, `load_bind/restore_pathnames()`,
`fasl_read_magic()`. Also `cf_process_toplevel_eval_when` 4 near-identical
situation loops → one loop with `do_eval`/`do_collect` bools (~55).

### T2.4 bignum.c arithmetic families (~200 lines) — HOT paths, care needed
Keep fixnum fast paths inline; only factor the COLD large-operand tails.
- `logand/logior/logxor` byte-identical but for operator → `bignum_bitop` (~60)
- `add`/`sub` share entire cold bignum tail; sub only flips b_sign → shared
  `bignum_addsub` tail (~65)
- stack-or-heap limb-buffer alloc/free boilerplate repeats 7× (add/sub/mul/
  truncate/mod/gcd/ash) → `limb_alloc`/`limb_free` (~45)
- `mod`'s divmod re-inlined inside `gcd` → `bignum_remainder` (~25)
- three 2-limb constructors (`bignum_from_int32`/`from_uint32`×2) + FFI's
  `ffi_u64/i64_to_obj` → one `cl_bignum_from_u64(v, sign)` (~30)
- complex add/sub/mul/div/negate/abs scaffolding → `complex_binop(a,b,real_op)`

### T2.5 JIT / runtime NLX families (~330 lines) — all emit-time/cold, zero runtime cost
`jit.c` / `runtime.c`:
- N-arg helper-call emitter template (pop→flush→predec-push→JSR→drop→push)
  repeated ~15× → `emit_helper_call_1/2` (~150). MUL/DIV, RPLACA/RPLACD,
  PROGV_BIND/UNBIND are literally byte-identical but for the helper addr.
- inline-setjmp NLX emitter copy-pasted 4× (BLOCK/CATCH/TAGBODY/UWPROT) →
  `emit_nlx_setjmp_frame` (~120)
- signed-offset → landing_ip computation duplicated ~7× →
  `resolve_branch_target` (~45), normalizes a subtle `> ip+5` vs `> ip` variance
- `runtime.c`: four `*_alloc` ~90% identical → `nlx_alloc_common` (~65);
  four `*_pop` identical but type constant → `nlx_pop_type` (~30);
  three `*_post_longjmp` byte-identical → `nlx_restore_common` (~50)
- comparison helpers lt/gt/le/ge bodies → shared `real_cmp` (each stays a
  distinct ABI entry point / JSR target, just 1-line wrappers)

---

## Tier 3 — repeated guard/predicate boilerplate (adds up, ~900 lines)

### Builtins guards
- **Char comparators**: 12 near-identical fns (`builtins_strings.c`
  104-175 + 1830-1901). String side already solved via `string_cmp_op` +
  STR_CMP_* enum; char side never got it. Use inline `switch(mode)` + inline
  case-fold, NOT a per-element function pointer (char=/char< are hot). (~110)
- **Type-check-extract resolvers**: `require_hash_table` (11× in
  builtins_hashtable.c), `require_fp_vector` (4×, builtins_array.c),
  `require_char` (13×, builtins_strings.c), `require_symbol` (11×,
  builtins_mutation.c). Each collapses 3 lines → 1.
- **Condition slot accessors**: 10 identical templates (builtins_condition.c
  592-710) → one `bi_cond_slot(args,n,key,name)` + `{name,key}` table (~55)
- **Type predicates**: `{ CL_UNUSED(n); return PRED(args[0]) ? SYM_T : CL_NIL; }`
  ×~20 → `DEFINE_TYPE_PREDICATE(fn, PRED)` macro, identical codegen (~80)
- **arith**: `bi_lt/gt/le/ge` + `bi_max/min` copy-paste →
  `real_chain_compare`/`minmax`; `bi_logand/ior/xor` → `logfold` (~55)

### VM opcodes — HOT, use inline macros only
- `OP_LT/GT/LE/GE` copy-paste (vm.c 1969-2019) → `VM_ORDER_CMP(op,name)` (~30)
- `if(!constants)` guard ×5 (OP_CONST/GLOAD/GSTORE/FLOAD) →
  `VM_REQUIRE_CONSTANTS(op)` macro, or hoist to one frame-entry check (~20)

### Cross-file dedup
- `classify_general_elt_code` (builtins_array.c:112) ==
  `coerce_general_elt_code` (builtins_type.c:984) — comment admits the mirror.
  Move to shared TU (~40)
- `seq_elt`/`seq_length` in both builtins_array.c (uint32) and
  builtins_sequence.c (int32); `call_test` in both builtins_lists.c and
  builtins_sequence.c. Promote canonical copies to a shared sequence-helpers
  TU. HOT (element access in scan loops) — keep as plain statics, no extra
  indirection (~50)
- `resolve_input_stream`/`resolve_output_stream` verbatim in builtins_io.c and
  builtins_stream.c; `bi_read`/`bi_read_delimited_list` inline the same logic
  by hand → promote to stream.h/builtins.h (~25)

### package.c
- `if (cl_package_rwlock) platform_rwlock_*` guard repeated 38× →
  `pkg_lock_read/write/unlock` inlines (~30)
- three identical list-splice loops (unexport/unuse/remove-local-nickname) →
  `list_remove_if(&head, pred)` (~30)
- `export_all_present_symbols` and `cl_package_export_defined_cl_symbols` are the
  same GC-safe counted-advance double loop → parameterize with predicate (~60)

### thread / builtins_thread
- three table alloc/free pairs (thread/lock/condvar) structurally identical →
  generic `slot_table_alloc/free` (~40)
- per-builtin lock/condvar arg-validation preamble ×10 → `resolve_lock`/
  `resolve_condvar` (~50)
- timed vs untimed `condition-wait` large near-duplicate → factor pre/post
  interrupt choreography (~40)

### printer / misc
- radix prefix switch (#b/#o/#x/#Nr) copy-pasted 3× (out_integer, bignum, ratio)
  → `out_radix_prefix` (~30)
- `print_single_float`/`print_double_float` same fn, swapped markers →
  `print_float(v, is_double)` (~15)
- temp string-output-stream "get string + free outbuf" 3-liner ×~11 across
  io/format/printer/condition → `cl_finish_string_output_stream` (~30),
  also prevents outbuf-slot leaks
- `*print-object-hook*` dispatch duplicated STRUCT vs CONDITION →
  `try_print_object_hook` (~35)
- FASL: double-float endian swap read+write twin → `fasl_double_to/from_be`
  (~15); RATIO/COMPLEX/PATHNAME read blocks → `fasl_read_n` helper (~20);
  gc mark/update writers+readers = 2 impls of 1 loop → visitor fn ptr (~25)
- error.c per-frame restore sequence duplicated across
  `cl_error_frame_longjmp` / CL_ERR_EXIT branch / outermost-frame — the EXIT
  path restores a DIFFERENT subset (latent inconsistency this would surface)

---

## Implementation notes
- Land Tier 1 items as separate focused commits; each is independently testable
  against `make test`.
- T1.2/T1.3 (GC X-macros) are the most sensitive — own commit, run
  `make test-gc-stress` and the Amiga suite (`make -f Makefile.cross test-amiga`).
- No item here changes FASL wire bytes, so no `CL_FASL_VERSION` bump is required
  (T1.4 refactors in-memory tables only). If any implementation ends up altering
  serialized bytes, bump per fasl.h rules.
- Hot-path items (T1.2/1.3, T2.4 fast paths, T3 VM opcodes, char comparators,
  seq_elt/seq_length) must stay as macros / inline statics — no per-element
  function-pointer indirection.
