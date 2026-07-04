# GC Audit Tier 4 — Fix Plan

Status: **IN PROGRESS** — Batches 1 (compiler criticals), 2 (format/printer), 3 (IO/strings/reader), 4 (sequences/lists/hash/VM) and 5 (mem.c GC-core/allocator hardening, 0c45205, Amiga 3561/0) applied on branch fix/tier4-gc-corruption; batches 6-7 pending.
Findings source: 12-agent audit sweep 2026-07-03 (session 5580131b). ~85 findings, ~25 CRITICAL.
Branch: `fix/tier4-gc-corruption` (create from master at c70b4e4 or later).

Verification key: `[V]` = orchestrator-confirmed against source (10/10 spot checks confirmed agent reports verbatim); `[A]` = agent-reported, high confidence.

## Process rules (apply to every batch)

- Every fix gets a **regression test** that reproduces the bug where host-reachable (discriminating: verify it fails on a scratch worktree at pre-fix HEAD — the tier-2 trick), plus **gc-stress coverage** for the touched allocating paths.
- Gates per batch commit: `make test` + `make test-gc-stress` green. Before merge: `make test-plus` + Amiga FS-UAE suite (`make -f Makefile.cross test-amiga`; `pkill -f fs-uae` first — dup instances corrupt test-results.log).
- **No FASL bump expected** — none of these fixes change serialization. Double-check any fix touching fasl.c/compiler emission doesn't alter FASL bytes.
- Commits with `run_in_background` (auto-review is a synchronous pre-commit hook — never kill in-flight); serialize commits/builds/edits; clean-rebuild when a discriminating run looks wrong (stale-.o gotcha).
- Fix pattern language: "protect X" = `CL_GC_PROTECT(X)`/`CL_GC_UNPROTECT(n)`; "re-derive P" = re-run `CL_OBJ_TO_PTR`/`cl_vector_data` after any allocating call; "re-read from args[i]" = builtin args are rooted VM-stack slices, locals are not.

---

## Batch 1 — Compiler criticals (compiling ordinary code corrupts)

Files: compiler.c, compiler_special.c, compiler_extra.c.

- [x] **C1 [V] compile_tagbody uninit `tb->id` in GC root set — THE dotimes SIGBUS root cause.**
  compiler_special.c:1300-1308: `tb = &c->tagbodies[c->tagbody_count++]` publishes the slot to the mark/update walkers (compiler.c:5085/5168) before `tb->id` is set; the next statement `tb->id = cl_cons(CL_NIL, CL_NIL)` allocates while the slot holds the previous occupant's dead cons offset (slots are reused — count is saved/restored per tagbody). Mark walk hits a dangling offset during that alloc's compaction → SIGBUS in cl_alloc, backtrace `compile_let → compile_dotimes → compile_tagbody → cl_alloc`.
  Fix: set `tb->id = CL_NIL;` (and `n_tags = 0`) **before** the cl_cons; overwrite for the NLX case.
  Test: gc-stress case compiling ≥2 NLX tagbodies in one top-level form: `(progn (dotimes (i 4) (mapc (lambda (x) x) l)) (let ((ok t)) (dotimes (i 20) (let ((v i)) (lambda () v)))))`. Closes old backlog item 6.
- [x] **C2 [A] compile_case** (compiler_extra.c:721/740/748): `clauses` read from form at 721, `compile_expr(c, keyform)` at 740 (macroexpands/allocates), `CL_GC_PROTECT(clauses)` only at 748 — protects a stale value. Fix: re-derive `clauses = cl_cdr(cl_cdr(form))` after compile_expr (form is caller-protected), or protect before. Trigger: `(case (my-macro …) …)`.
- [x] **C3 [A] compile_typecase** (compiler_extra.c:907/928/940): identical shape. Also covers etypecase.
- [x] **C4 [A] compile_lambda &key defaults** (compiler.c:718-742): `saved_key_locals[CL_MAX_LOCALS]` C array saves the hidden key-param symbols, `compile_expr(default)` compacts, restore writes pre-compaction symbol offsets back into `env->locals` → later key params become "Unbound variable" + non-idempotent forward corruption. Fix: restore from GC-forwarded `inner->ll.key_names[j]` (or protect each saved entry). Trigger: `(defun f (&key (a (some-macro)) b) … b …)`.
- [x] **C5 [A] compile_let LET* boxing pre-scan** (compiler.c:1985-2004): cursors `b`/`cur` unprotected across `scan_body_for_boxing` (macroexpands via cl_vm_apply). `determine_boxed_vars` (parallel LET) protects its cursors; the LET* duplicate doesn't. Fix: protect both cursors. Very common trigger: any LET* with a macro in an init form.
- [x] **C6 [A] compile_call %struct-set inline hook** (compiler.c:3828-3844): `val_form` read before `compile_expr(obj_form)`, compiled after. `args` IS protected → fix: re-derive `val_form = cl_car(cl_cdr(cl_cdr(args)))` after obj_form compile. Trigger: `(setf (point-x p) v)` where the object expr's compilation allocates.
- [x] **C7 [A] scan_body_for_boxing SETQ/SETF handler** (compiler.c:1146-1319): `pairs` cursor + `val` + the `(setf (progn …))` `pforms` walk unprotected across allocating recursive scans/setf-expanders. Siblings (LET/CASE/DO/FLET/handler-bind) were fixed in prior tiers; SETQ/SETF missed (nlx_scan's SETQ handler protects). Fix: protect cursor, re-read val after place processing.
- [x] **C8 [A] compile_multiple_value_prog1** (compiler_extra.c:1177-1207): `rest_forms` stale before its protect. Fix: re-derive after `compile_expr(first_form)`.
- [x] **C9 [A] parse_lambda_list &key intern window** (compiler.c:458-553): `cl_intern_keyword` (new keyword) allocates; (a) walk cursor `p` unprotected; (b) `key_*[n_keys]` written before `n_keys++` → invisible to compiler mark/update walkers during the intern. Fix: bump `n_keys` with a NIL keyword before interning, then store; protect `p` (mirror the destructure twin at compiler_special.c:311-331). Deterministic trigger: first compile of a lambda with a never-interned `&key` name.
- [x] **C10 [A] compile_nth_value dynamic-index** (compiler_extra.c:1162-1166): `values_form` stale across `compile_expr(n_form)`. Re-derive.
- [x] **C11 [A] scan_qq_for_boxing** (compiler.c:1004-1041): raw `cl_vector_data` pointer + list cursor held across allocating recursion. Re-derive per iteration / protect cursor.
- [x] **C12 [A] nlx_scan_qq** (compiler_special.c:656-678): same shape as C11.
- [x] **C13 [A] cl_compile_defun** (compiler.c:5062-5068): `name` stale across nested cl_cons. Dead code (no in-tree callers) — fix or delete.
- [x] **C-bounds (LOW)**: parse_lambda_list writes `opt_*`/`key_*[n]` before the `>= CL_MAX_LOCALS` check (off-by-one at cap); `compile_dolist/dotimes/do/do*` do `c->blocks[c->block_count++]` without the CL_MAX_BLOCKS guard compile_block has; `tb->tags[tb->n_tags++]` unbounded vs CL_MAX_TAGBODY_TAGS. Add guards + clean cl_error.

Batch tests: extend gc-stress LOOPCOMPILE family with case/typecase/&key-default/LET*-setf/struct-setf/mv-prog1/nth-value/qq shapes.

---

## Batch 2 — Format stack smash + printer element loops — **DONE**

Files: builtins_format.c, printer.c (+ error.h/error.c, vm.h/vm.c, jit/runtime.c, builtins_condition.c for P10; builtins_io.c for IO5; mem.c for V12 + diagnostics).

- [x] **FS1 [V] fmt_padded_integer STACK SMASH** — dead first loop deleted, single bounded grouping loop, `with_commas` sized 256 (worst case 254 for the 127-digit raw[] cap) + loud cl_error backstop. Host regression tests in test_format.c.
- [x] **FS3 [A] fmt_goto plain `~n*`** — clamped both ends (negative param no longer reads `ctx->args[-3]`). The `~n@*` +2 sub-context bias remains FS8 (deferred).
- [x] **FS2 [A] fmt_recursive `~?` string-control** — sub_args slots rooted across fmt_run (mirrors the fixed `~{~}` pattern).
- [x] **P1 [V] TYPE_STRUCT slot_names window** — obj protected before cl_struct_slot_names, st re-derived after; slot loop keeps obj+cursor rooted.
- [x] **P2 [V] TYPE_VECTOR 1-D** — obj protected, data pointer re-derived per element.
- [x] **P3 [A] print_array_slice** — takes the vector CL_Obj; each recursion level roots its copy and re-derives per element.
- [x] **P4 [A] try_pprint_dispatch** — takes `CL_Obj *obj_p` (caller's slot forwards); cur/best_fn rooted across cl_typep; best_fn re-derived from the rooted cursor. PLUS: buffer-mode prints (write-to-string) previously handed the dispatch fn a NIL stream and silently dropped its output — now captured via a string stream and spliced (restart-report pattern). PLUS: the recursion guard is held across the SCAN so a cl_typep that signals can't recurse through condition printing.
- [x] **P5 [A] TYPE_RESTART fallback** — obj recovered from pr_inprog + kept rooted across get-output-stream-string; r re-derived for the fallback; wide-string report text spliced per code point (conformance).
- [x] **P6 [A] saved printer_stream restore** — `prev` protected in cl_write_to_stream / write_to_buffer_internal (+ pre-init branches); prev_e/prev_r protected in all four prin1/princ entry points.
- [x] **FS4 [A] render_integer** — prev_b/prev_x protected across cl_princ_to_string.
- [x] **P7 [A] TYPE_COMPLEX/TYPE_RATIO** — components read up front; second component protected across the first's print.
- [x] **P10 [A] printer NLX leaks** — new `CL_PrinterState` snapshot (pr_depth, pr_inprog_top, pprint_dispatch_active, circle_active; printer.h) embedded in CL_ErrorFrame (`saved_printer`) and CL_NLXFrame (`printer_mark`); restored on every longjmp landing (error.c nested+outermost+EXIT paths, vm.c 4 NLX landings, jit/runtime.c 4 twins, builtins_condition.c muffle landing).
- [x] **IO5 (pulled forward from batch 3): bi_set_pprint_dispatch** — removal + rebuild loops rooted (cursor/result/table), type-spec/function re-read from rooted args[]. Was corrupting the dispatch table on the 2nd set-pprint-dispatch under stress (dead-closure "Unknown opcode 0x00" / CAR-type-error → unbounded error recursion). IO6/IO7 remain in batch 3.
- [x] **V12 (NEW, found by batch-2 tests): OP_UWPROT parks stale pending_tag/value** (vm.c arming block): with no throw in flight, cl_pending_tag/value still hold the last completed throw's objects; the parking slot is GC-skipped while pending_throw==0, so the cleanup body's compactions stale the parked copies and UWRETHROW restored stale offsets into the always-marked globals — the next mark walk follows them into object interiors and ORs mark bits mid-object (observed: GF dispatch-cache bucket array → circular chain → gc_rehash_table spins forever). Fix: park NILs when no throw is armed. Pre-existing (reproduced at batch-1 HEAD); THE deterministic hang behind the tier4-printer stress case.
- [x] **IO1 pulled forward too** (see batch 3 entry) — the badmark guard exposed it as the cause of 6 pre-existing gc-stress case failures (nested `(load ...)`).
- [x] **GC diagnostics (permanent, DEBUG_GC/DEBUG_GC_STRESS-guarded)**: gc_mark_obj/gc_mark_push plausibility guards ("badmark": abort on implausible object start, with root-category + parent-object + CL_GC_PROTECT file:line provenance); hashtable chain-cycle verify in gc_rehash_table + post-rehash arena sweep; root-stack-overflow histogram of pusher sites. These made V12 findable in minutes and make the whole interior-mark class deterministic in CI (immediately caught IO1 in existing cases).

Batch tests: `tier4-printer.lisp` block in test_gc_stress_regression.sh (16 checks: P1/P2/P3/P4/P5/P5W/P6/P7/FS1/FS2/FS4/P10×4/IO5 — verified failing on a pre-fix worktree: P4 empty, P5W degraded, P10B `#<...>`, P10C root-stack overflow, then deterministic GC hang); host format regressions in tests/test_format.c (FS1 grouped 101-digit bignum, grouping correctness, FS3 clamps, FS2 behavior).

---

## Batch 3 — IO / strings / reader (restore class + cursor cluster) — **DONE**

Files: builtins_io.c, builtins_strings.c, reader.c, printer.c/printer.h (helper).

- [x] **IO1 [V] bi_load *LOAD-PATHNAME* staleness** — **done in batch 2** (both blocks: source path and FASL-cache twin; load_pathname_obj protected immediately after the first parse). Pulled forward because the new gc_mark_obj badmark guard turned it into a deterministic abort in 6 existing gc-stress cases (any nested `(load ...)` under stress).
- [x] **Shared fix: print-control save/restore helper** — `cl_print_controls_save/restore` (printer.c, CL_PRINT_CTRL_COUNT=13): snapshots all *PRINT-* controls into a caller array with every slot pushed as a GC root; restore writes back the forwarded values and pops. Used by bi_write (also now returns `args[0]`, dead CL_Symbol* locals deleted), bi_write_to_string; bi_pprint protects its 2 saved values + `stream` directly (restoring all 13 would clobber user setqs of unrelated controls from inside print-object methods). TLV dynamic binds remain the long-term follow-up (FS16).
- [x] **IO4 [A] bi_read_delimited_list** — `stream` protected alongside result/tail.
- [x] **IO5/IO6/IO7 [A] pprint-dispatch cluster** — IO5 done in batch 2. IO6 copy-pprint-dispatch `cur` protected; IO7 bi_pprint_dispatch: cur/best_fn rooted, object read from args[0], winning fn re-derived through the forwarded cursor AFTER cl_typep (dead typep_args removed).
- [x] **IO8 [A] bi_require** — `pathnames` cursor + all three `load_args[0]` C slots registered as roots (bi_load treats args[0] as a rooted, forwarded slot).
- [x] **IO9 [A] bi_macroexpand** — form/env protected across the expander loop; expanded-p tracked as a C int (no stale SYM_T copy). bi_macroexpand_1 compares against rooted args[0].
- [x] **IO10 [A] disassemble** — disasm_bytecode now takes the CL_Obj (rooted); code/constants/scalars hoisted ONCE at entry (platform memory — stable, constants slots forwarded in place); bc re-derived for the name print; OP_CLOSURE n_upvalues hoisted; builtin branch roots fn->name before the first write.
- [x] **IO11 [A] extfun** — sym protected across cl_make_function, s derived after (mirrors defun).
- [x] **IO12 [A]** — merge_args[0] + path_pn protected in bi_load and bi_compile_file_pathname (mirrors bi_compile_file).
- [x] **FS10 [A] bi_string_trim/left/right** — `set = args[0]` re-read after the allocating coerce (all three).
- [x] **FS11 [A] bi_concatenate compound result-type** — result_type protected across the is_char/is_bit deftype-expander checks; rest re-derived between them.
- [x] **R1 [V] reader #nA flatten** — work[0..7] + inner lst cursor all registered as roots for the flatten loop.
- [x] **R2 [V] cl_read_from_stream saved_stream** — protected (4th member of the save set).
- [x] **R3 [A] cl_read_from_string** — saved_stream protected.
- [x] **R4 [A] cl_read saved_uninterned** — protected.
- [x] **R5 [A] ensure_feature_keywords** — each kw root registered immediately after its intern; eval_feature_expr calls ensure BEFORE reading head/rest **with expr protected across the ensure** (the naive reorder introduced a deterministic `#-(or)` CAR-type-error under stress — first-call interns compact while expr is live; caught by the tier-2 T2-FORMS gc-stress case).
- [x] **R6 [A] CL_READER_SKIP sentinel leak** — quote/backquote/unquote(+splicing)/#'/dotted-cdr/#nA all loop past CL_READER_SKIP (CLHS 2.4.8.17: `'#+nope foo bar` reads as `'BAR`). The pre-fix binary crashed outright on this case under stress (the 0x06 sentinel embedded in structure) — the discriminating check for the batch.

Batch tests: `tier4-io.lisp` block in test_gc_stress_regression.sh (18 checks: IO2/IO3/IO4/IO6/IO7/IO8/IO9/IO10/IO12/FS10/FS11/FS12/R1/R2/R5/R6 + completion + corruption-absent; pre-fix worktree run confirmed R6 crashes pre-fix and all T4C checks pass post-fix); Amiga t4 batch-3 mirror checks in tests/amiga/run-tests.lisp.

---

## Batch 4 — Sequences, lists, hashtable, VM opcodes — **DONE**

Files: builtins_sequence.c, builtins_sequence2.c, builtins.c, builtins_lists.c, builtins_hashtable.c, builtins_array.c, vm.c.

- [x] **S4 [V] list_merge_sort** — pred/key_fn protected alongside list/mid (UNPROTECT 4).
- [x] **S5 [A] bi_map string/vector result** — `seqs[i] = orig_seqs[i] = args[i+2]` re-read after the result alloc, before the cursor PROTECTs.
- [x] **S1 [A] remove_from_list stale elem** — elem re-read from the forwarded `cur` after the match test (both from-end and forward passes). PLUS: `seq` itself now protected (UNPROTECT 7) — the from-end second pass re-seeds `cur = seq` after the counting pass has already run allocating tests.
- [x] **S2/S3 [A] bi_reduce list paths** — cursor seeded from `args[1]` in both the :from-end vector-collect and forward paths, AND the CL_CONS_P branch test reads args[1] (a stale offset can misclassify the sequence).
- [x] **S6 [A] array_seq_insertion_sort** — val/kval hoisted + rooted for the loop (mirrors vector_insertion_sort; kval is heap when :key returns e.g. a string).
- [x] **S7 [A] bi_map result-type** — `result_type = args[0]` re-read immediately after seq_result_type_class (covers the (or ...) walk, the non-OR type-error, and the else-branch re-classification).
- [x] **S8 [A] bi_merge pre-classification** — result-type classified FIRST from args[0]; pred rooted before the :key coerce; key_fn/seq1/seq2 read+rooted after all setup allocs; general path's inner protects reduced to a1/a2/out (outer 4 + inner 3 = the existing UNPROTECT(7)s).
- [x] **L1 [V] bi_nsubst** — refactored to `nsubst_rec(new, old, tree, test_fn)` mirroring subst_rec; per-frame roots; destructive stores re-derive the cons pointer from the rooted tree AFTER each recursion.
- [x] **L2 [V] bi_reverse vector + bit-vector branches** — seq protected across the result alloc; re-fetch now goes through the forwarded value.
- [x] **L3 [V] bi_make_list** — init_elem protected alongside result.
- [x] **L4 [A] bi_jit_dump_bytes** — native_code/native_len hoisted to C locals before the cons loop (bc is an arena pointer; the buffer is platform memory). Not host-reachable (native_code NULL without JIT) — no host regression test; covered by Amiga JIT usage.
- [x] **AH1 [A] bi_hash_table_pairs** — chain cursor rooted (mirror bi_maphash).
- [x] **AH2 [A] bi_make_array keyword parse** — parse loop now records args[] INDICES only; dim_arg/initial_element/initial_contents/displaced_to/element_type materialized from the rooted slots after the loop (classify runs in-loop on fresh reads as before).
- [x] **AH3 [A] bi_adjust_array size checks** — negative dims (both fixnum and list spelling) and dims > CL_ARRAY_DIMENSION_LIMIT are catchable type-errors before any allocation.
- [x] **AH4 [A] bi_vector_push_extend extension arg** — negative extension → type-error; extension that would exceed ARRAY-DIMENSION-LIMIT → type-error (extension is a CLHS minimum — clamping would silently violate it); doubling path clamped; old_len at the limit → error.
- [x] **V2/V3/V4 [V] OP_DEFMACRO/OP_DEFTYPE/OP_DEFSETF** — push `constants[idx]` re-read after the allocating registrar (pool is platform memory, entries forwarded in place). Note: OP_DEFSETF's cons-under-wrlock remains a batch-6 item (T2).
- [x] **V1 [A] OP_ASSERT_TYPE** — type_spec/val re-read after cl_typep, val re-read between the two condition-slot conses, both re-read again for the fall-through prin1s (signal handlers ran arbitrary code).
- [x] **V5-V8 [A] TRACE paths** — trace_print_entry roots name_sym, trace_print_exit roots name_sym+result (writes/prin1 allocate); OP_CALL builtin branch re-derives f from the rooted fn stack slot after entry print and after call_builtin, result rooted across exit print; tailcall + normal-call traced paths re-read func_obj from the still-live stack slot and re-derive callee_bc after the entry print; OP_RET roots result across the exit print; OP_APPLY roots apply_func for the whole opcode (its slot is overwritten by the spread), re-derives f, roots result.
- [x] **V11 [A]** OP_APPLY frame push sets `new_frame->nlx_level = cl_nlx_top` (mirrors OP_CALL; field kept).

Batch tests: `tier4-seq.lisp` block in test_gc_stress_regression.sh (21 checks: S4/S6/S5/S7/S8/S1/S2-S3/L1/L2/L3/AH1/AH2/V2/V3/V4/V1/V5/V5b/V7 + completion + corruption-absent — pre-fix worktree run at d9ba80d: crashes at S6, 18/21 fail; post-fix 326/326); host CLHS error tests in tests/test_array.c (adjust_array_negative_dimension, adjust_array_dimension_limit, vector_push_extend_bad_extension); Amiga t4 batch-4 mirror block (30 checks) in tests/amiga/run-tests.lisp.

---

## Batch 5 — GC core & allocator hardening (mem.c)

- [x] **M1 [A] allocator size caps** — per-type CL_MAX_* element caps in mem.h (derived from CL_HDR_SIZE_MASK), checked BEFORE alloc_size in cl_make_string/wide_string/vector/array/struct/bignum/bit_vector/hashtable (hashtable also fixes the power-of-two round-up infinite loop for counts > 2^31). FASL reader: fasl_check_count() (count vs remaining-input/min-bytes AND type cap) on STRING/WIDE_STRING/BIGNUM/VECTOR/MD_ARRAY/BIT_VECTOR/STRUCT, new FASL_ERR_BAD_LENGTH; MD_ARRAY additionally validates len == dims product (overflow-guarded). Pre-fix probe: cl_make_vector(2^30) silently allocated a 16-byte block with length 2^30; FASL huge string wrapped platform_alloc(len+1) AND the read_bytes truncation check → ~4GB memcpy.
- [x] **M2 [A] JIT pin/scan OOM branches** — jit_pin_record OOM sets gc_jit_pin_oom (reset in gc_mark); cl_gc_compact checks it after marking and degrades to a non-moving mark+sweep (+ gc_sweeps_since_compact reset). Scan-buffer OOM no longer SKIPS the conservative scan (that swept JIT-only-reachable objects — worse than the pin case): it degrades to chunked scanning through a 256-slot static buffer (one extra arena walk per chunk; gc_compute_forwarding already sorts the aggregate pins). Misleading "never corrupts" comments corrected; one-shot loud warnings on both paths.
- [x] **M3 [A] fwd-table OOM pathology** — fwd-alloc failure branch now resets gc_sweeps_since_compact and latches gc_fwd_fail_bump = bump; cl_alloc's trigger 2 (sweep-forever escape) skips re-attempts while bump >= that latch (table size ∝ bump); trigger 1 (last resort) always tries; latch cleared on successful compaction. Chunked/two-pass table remains a follow-up spec item.
- [x] **M4 [A] root-dedup skip gap** — root_slot_on_vm_stack → root_slot_independently_forwarded: exact-bounded ranges for vm.stack/mv_values/pending_mv_values (gated on pending_throw + pending_mv_count)/dyn_stack/handler_stack/restart_stack/vm_extra_args/saved_pending_stack[].{pending_tag,pending_value,pending_mv_values[]} (gated per-entry on that entry's pending_throw + pending_mv_count, mirroring gc_update_thread_roots), and the compiler chain probed by reusing cl_compiler_gc_update_thread itself as the membership test (can't drift). cl_gc_audit_roots gained an informational aliasing report (not counted as violations — post-fix they're harmless-by-construction).
- [x] **M7 [A] cl_make_string/wide arena-source OOM fallback** — copy-buffer platform_alloc failure now signals cl_storage_error instead of keeping the arena pointer across the alloc.
- [x] **M6 [A]** — pre_call_mv_values deliberate non-mark documented in gc_mark_thread_roots (+ pointer in gc_update_thread_roots) incl. the regression hazard.
- [x] **M8/M9/M10 [A] LOW** — cl_bump_fits() wrap-safe check (non-static, unit-tested); gc_is_freed bounds-checks the poison word against bump and returns not-freed for blocks <= sizeof(CL_FreeBlock) (8-byte pin-gap chunks have no poison room); gc_reset_transient_state() called from cl_mem_init clears gc_compact_pending/gc_mark_overflow/gc_fwd_fail_bump/gc_jit_pin_oom/jit_pinned_count/jit_scan_free_valid+count.
- [x] **M11 [A]** — explicit rank>1 error before the in-place displacement writes in bi_vector_push_extend and bi_adjust_array (GC data[CL_DISP_BASE_IDX] contract backstop). Both builtins made non-static so the guard can be exercised directly (a rank>1 fill-pointer/adjustable vector isn't reachable from Lisp via MAKE-ARRAY).
- [x] **M4 follow-up [A]** — extended `root_slot_independently_forwarded`'s exemption list to `saved_pending_stack[i].{pending_tag,pending_value,pending_mv_values[]}` (per-entry gated on that entry's `pending_throw` + `pending_mv_count`, mirroring `gc_update_thread_roots`), closing the one region M4 left uncovered.

Batch tests: tests/test_gc_mem_hardening.c (23 checks: M1 caps incl. hashtable-hang + bump-untouched, FASL BAD_LENGTH rejections + well-formed still load, M4 mv_values/vm_extra/saved_pending_stack/dyn_stack aliases + beyond-bound-still-forwarded, M8 wrap, M11 rank>1 backstop in bi_vector_push_extend/bi_adjust_array, M10 reinit). Discriminating pre-fix run at c48cf1c: vector cap FAILS (16-byte wrapped alloc, no error), FASL huge vector FATAL-aborts, mv_values alias reads 777 instead of 888 (double-forward onto neighbor). gc-stress + DEBUG_GC boot as gates for M2-M4/M9. M11 is a Lisp-unreachable defensive backstop (both call sites are gated by earlier checks), so it is covered only by the direct host unit test, not gc-stress.

---

## Batch 6 — MT / threading / streams

Files: thread.c, builtins_thread.c, platform_posix.c, platform_amiga.c, platform_thread_amiga.c, stream.c, builtins_stream.c, printer.c (out_str), symbol.c, package.c, compiler.c (registrars).

Order within batch: correctness races first, then the known backlog items, then out_str conversion.

- [ ] **T1 [A] wrapper→slot identity checks** (builtins_thread.c join :574-607, interrupt :1210-1222, destroy :1268-1279, alive-p :668-676): add `t->thread_obj == args[0]` check under cl_thread_list_lock (gc_finalize_dead already has it, mem.c:2034); error "thread has already exited" (alive-p → NIL). Zombie reaper stamps reaped wrapper's `thread_id = -1` under the lock. Fixes join-frees-wrong-worker double-free + interrupt/destroy of innocent threads.
- [ ] **T2 [A] no allocation under package/tables rwlocks** (symbol.c:311-319 cl_intern slow path; package.c:380-404 export, :450-479 shadow, import/use/nickname siblings; compiler.c:4622/4684/~4739 macro/setf/type registrars; vm.c OP_DEFSETF): pre-cons the new cell OUTSIDE the lock, re-check + link with a plain store inside (re-derive raw ptrs after the outside-alloc). Kills the STW-vs-rwlock-wait circular hang (cross-confirmed by two agents; plausible "random MT hangs under load").
- [ ] **ST2 [A] cl_stream_close TOCTOU** (stream.c:1046-1112): re-check-and-clear OPEN after acquiring the iolock; bail if already closed. For STRING streams (no iolock) use an atomic flag-claim. Kills double platform-close (fd reuse → killing unrelated connections).
- [ ] **ST3 [A] cl_stream_free_outbuf** (stream.c:304-327): move the `.data` check inside the table mutex.
- [ ] **T3 [A] POSIX+Amiga file_table locking** (platform_posix.c:135-226, platform_amiga.c:183-233): claim/release under a static mutex (mirror socket_table_lock); init in platform_init.
- [ ] **ST1 [A] cl_stream_peek_char** (stream.c:1023-1034): protect `stream` across the blocking read; re-derive st for the unread_char store.
- [ ] **ST4 [A] bi_listen Amiga** (builtins_stream.c:1655-1676): capture handle_id first; protect stream; re-derive st before the EOF-flag RMW after the sock_call safe region.
- [ ] **ST5 [A] resolve_synonym type check** (stream.c:449-460): require `CL_STREAM_P` like resolve_stream — `(close (make-synonym-stream '*standard-output*))` with a Gray-stream binding currently treats a CL_Struct as CL_Stream. (Single-threaded! Could also ride in batch 3.)
- [ ] **T4 [A] Amiga CreateNewProc/Forbid race** (platform_thread_amiga.c:112-138): CreateNewProc Wait()s internally, breaking the Forbid → child may read tc_UserData before the parent stores it. Fix: child bounded-polls `while (!me->tc_UserData) Delay(1);` at entry.
- [ ] **I8 [A] lock finalize owner tracking** (mem.c:1964-1980): `cl_lock_held[CL_MAX_LOCKS]` set by owner after acquire / cleared before release (owner-only mutation while holding; finalize reads under STW = race-free). If held at finalize: NULL the table slot and deliberately LEAK the OS mutex + one-shot warning (destroy is never sound). Clear entries owned by a dying thread in thread_entry's exit path.
- [ ] **I5 [A] interrupt delivery to parked threads** (builtins_thread.c:1107-1125 condition-wait, :968-970 acquire-lock; publishers :1238/:1283): layered fix — (1) pre-park `interrupt_pending` check after publishing wait_cv_id (barrier-ordered), (2) publisher acquires the USER mutex then broadcasts the target's cv (guarantees pre-park-sees-flag or parked-gets-broadcast), (3) acquire-lock becomes trylock+sleep(10ms)+interrupt-check loop inside the safe region (timedlock on POSIX). Spurious wakeups are permitted by bordeaux-threads semantics; runtime loops re-test predicates.
- [ ] **T5 [A] shutdown teardown** (thread.c:821-853, :78): set `cl_shutting_down`; never destroy gc_mutex/gc_condvar/cl_thread_list_lock while `cl_thread_count > 1` (leak at exit); null-guard in cl_thread_unregister.
- [ ] **G6 [A] safe-region bracketing (selective)**: bracket platform file open/getchar/write/flush/seek + platform_sleep_ms + platform_directory + platform_system on both platforms — ONLY together with copying arena-backed buffers/paths to C memory first (direct-write fallbacks route through IOBuf chunks; file_open copies path; bi_system_command copies `s->data` BEFORE bracketing — today it passes an arena ptr). FFI (`platform_ffi_call`/`platform_amiga_call`): do NOT blanket-bracket; add an opt-in `:blocking` flag later (document as accepted STW-stall for now). Amiga reactor_ensure boot: accept (bounded).
- [ ] **P-out_str [A] out_str_lisp conversion** (printer.c — 20 enumerated sites: 927, 932, 935, 941, 947, 953, 1000, 1012, 1023, 1128, 1191, 1211, 1226, 1282, 1290, 1296, 1302, 1318, 1344, 1353 + per-char loops at 678, 963, 1036, 1117, 474-524, 919-954): add `out_str_lisp(CL_Obj)` / `out_symbol_name_lisp(CL_Obj sym, int pcase)` — chunked copy to a C buffer with per-chunk re-derive (the cl_stream_write_lisp_string pattern incl. m68k volatile workaround), column tracking on the C chunk. Convert all sites; closes backlog item 1. All MT-only (verified: out path doesn't allocate; park windows = contended iolock + socket writes).
- [ ] **T6-T9 + F7/F8 hardening (LOW)**: late-joiner spins on `tobj->thread_id == -1` (rooted args[0], re-derived) instead of table-ptr ABA; jit_park_sp stored after acquiring gc_mutex in leave_safe_region; protect interrupt closure local around cl_vm_apply; condition-wait park guard also checks rdlock_package_held; V9/V10 vm lazy-init races (dispatch table barrier, gf-types under lock).

Batch tests: extend tests/test_gc_threaded.c + test_mt_join_race.sh style shell tests (join-identity, close-race); Amiga FS-UAE MT checks; I5 gets a host test (destroy a thread parked on a never-signaled condvar → must terminate); gc-stress unaffected paths re-run.

---

## Batch 7 — Conformance / diagnostics tail (optional, after corruption fixes)

- [ ] bi_apply C path silent 64-arg truncation (builtins.c:838-861) — route through the VM-stack spread like OP_APPLY or error loudly.
- [ ] FS5 FMT_MAX_ARGS=64 vs CAL 4096 (~? function-control clobbers args[64..]); %formatter-inner 64 cap.
- [ ] FS6 fmt_case_convert wide-string type confusion (garbled ~( ~) output on non-ASCII).
- [ ] AH5 hashtable hash/equality contract: content-hash wide strings + EQUALP vectors; add keys_equal bit-vector branch (CLHS EQUAL).
- [ ] S9 remove_from_string 1024-char truncation + wide-char mangling; FS13 concatenate-string 4096 truncation (+ ~20KB stack frame in wide builds — heap-allocate).
- [ ] Reader token caps should signal reader errors instead of silently splitting/truncating (symbol >255 → two tokens; 300-digit bignum parses as two numbers!; strings >4095; #* bits; #\ names) — per the diagnostics policy.
- [ ] R-srcloc: cons-offset-keyed srcloc table gives wrong file/line after compaction + unlocked MT writes (cosmetic).
- [ ] ST7 file-position/length bignum instead of fixnum truncation (>~512MB files); ST8 finalize-close leaked fds (design decision: warn vs auto-close); ST9 register stream-layer roots immediately after assignment.
- [ ] mapl/mapcon missing cl_coerce_funcdesig; mapcar-family silent 16-list cap; every/some/map/map-into 16-seq caps → error or document.
- [ ] FS7 platform_alloc NULL checks in format; S-LOW platform_alloc leaks on user-fn longjmp (use GC vectors or error-frame cleanup).
- [ ] repl.c REPL_BUF_SIZE silent truncation → error message; bi_trace_function RMW under wrlock.
- [ ] FS16/print-control globals → real TLV dynamic binds (removes MT value races; supersedes the batch-3 snapshot helper).

---

## Deferred / documented (no code change this round)

- FMT/FFI blanket safe-regions (G6 FFI part) — needs `:blocking` opt-in design.
- fwd-table chunked allocation (M3 follow-up) — separate spec if Amiga hits it in practice.
- pre_call_mv_values redesign (M6) — comment only.
- Amiga worker-VM sizing retest (old backlog item 7) — after batches 1-5 land, re-try enabling full worker VM sizes on Amiga; tiers 1-4 have now killed several members of the suspected latent-GC-bug class (esp. C1).
- ~n@* sub-context bias (FS8) — semantic, needs CLHS-check + tests.

## Suggested execution order & rationale

1. **Batch 1** (compiler) — highest blast radius: every user compile path; contains the one-line SIGBUS fix. 
2. **Batch 2** (format/printer) — contains the only stack smash; printer bugs corrupt on `print` of everyday data.
3. **Batch 3** (io/strings/reader) — ASDF-critical (bi_load) + ubiquitous write-to-string.
4. **Batch 4** (sequences/lists/hash/vm) — sort/map/remove/hash-iteration hot paths.
5. **Batch 5** (mem.c hardening) — lower urgency (no confirmed critical), do before MT so M2/M4 land under the same stress gates.
6. **Batch 6** (MT) — largest and riskiest; needs Amiga runs per item group. Split into 3+ commits: (a) races T1/T2/ST2/ST3/T3, (b) backlog I5/I8/G6/T4/T5, (c) out_str_lisp sweep.
7. **Batch 7** — opportunistic; each item small and independent.

Each batch = one commit (or a few for batch 6), gc-stress + host tests green per commit, full Amiga suite at batch boundaries, merge to master when all batches land (or merge batches 1-4 early if MT work stretches).
