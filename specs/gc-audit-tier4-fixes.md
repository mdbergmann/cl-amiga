# GC Audit Tier 4 — Fix Plan

Status: **IN PROGRESS** — Batches 1-5 and 6 (MT/threading/streams: 6a 8cd41de, 6b 2325548, 6c 561cf93 + condition-wait in-builtin interrupt delivery fixup; Amiga 3566/0) applied on branch fix/tier4-gc-corruption; batch 7 (optional conformance tail) pending.
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

- [x] **T1 [A] wrapper→slot identity checks** — implemented via a per-slot **generation counter** instead of the planned `t->thread_obj == args[0]` compare / reaper wrapper-stamp: both of those dereference-or-compare `t->thread_obj`, which stops being forwarded once the worker unregisters (stale offset — the reaper stamp would even WRITE through it). New `cl_thread_table_gen[]` bumped in cl_thread_table_alloc on every slot claim; `CL_ThreadObj.table_gen` snapshots it at wrapper creation (make-thread, current-thread fallback, main wrapper); join/interrupt/destroy check under cl_thread_list_lock → "thread has already exited"; alive-p → NIL; gc_finalize_dead(TYPE_THREAD) now uses the gen compare as the exact reuse guard (replaces the stale thread_obj compare, which leaked workers after any post-unregister compaction).
- [x] **T2 [A] no allocation under package/tables rwlocks** — all sites converted. Tables side: new `cl_table_prepend_locked(&table, value)` (compiler.c/compiler.h) conses the cell outside cl_tables_wrlock, links with plain stores inside; used by cl_register_macro/compiler_macro/type/setf_function, OP_DEFSETF, compile_defsetf, bi_register_setf_expander, bi_register_struct_type, bi_trace_function, and all four condition-table registrars. Package side: `cl_package_add_symbol_cell` + cell-taking `import_symbol_nolock` (returns conflict code — no cl_error under the wrlock, which would leak it via longjmp); cl_intern_in slow path, cl_export_symbol (pre-cons both cells), cl_import_symbol, cl_shadow_symbol (restructured check→alloc→re-check; also gained the intern-style C-buffer name copy), cl_use_package, cl_add_package_local_nickname, cl_register_package.
  **NEW latent bug exposed+fixed: `CL_T` (types.c) was never registered as a GC root** — a separate global from SYM_T, alive only because boot's allocation layout never moved the T symbol. The pre-consed cells above changed early-boot layout, T moved, and every builtin returning CL_T returned the string "PROGN" (gc-stress: mrs/MLF failures). Fixed with cl_gc_register_root(&CL_T) in symbol.c.
- [x] **ST2 [A] cl_stream_close TOCTOU** — re-check-and-clear OPEN under the iolock (FILE/SOCKET); no-iolock types claim the flag bit via platform_atomic_cas loop (single-threaded fallback plain store).
- [x] **ST3 [A] cl_stream_free_outbuf** — `.data` occupancy check moved inside the table mutex.
- [x] **T3 [A] POSIX+Amiga file_table locking** — `file_table_mutex` on both platforms (mirrors socket_table_mutex; init in file_table_ensure_init, first use single-threaded). Open claims under the lock; close DETACHES under the lock and runs fclose/Close (+ Amiga wbuf flush via captured handles) outside it.
- [x] **ST1 [A] cl_stream_peek_char** — stream protected across the blocking read; st derived after.
- [x] **ST4 [A] bi_listen** — handle captured before the probe, stream protected, st re-derived before the EOF-flag RMW (Amiga probe parks in the reactor safe region; POSIX probe never blocks but the guard is platform-neutral).
- [x] **ST5 [A] resolve_synonym type check** — requires CL_STREAM_P after each symbol-value hop.
- [x] **T4 [A] Amiga CreateNewProc/Forbid race** — amiga_thread_entry bounded-polls tc_UserData (Delay(1), ~60s cap) before touching the AmigaThread struct; dos zeroes new-process Task fields so poll-for-non-NULL is sound.
- [x] **I8 [A] lock finalize owner tracking** — `cl_lock_held[CL_MAX_LOCKS]` (thread.c/thread.h) set after every successful acquire (blocking + trylock, also single-threaded — the finalize hazard doesn't need MT), cleared before release, re-claimed after a condition-wait's internal re-acquire. gc_finalize_dead(TYPE_LOCK): held → NULL the table slot + clear the entry + LEAK the OS mutex (one-shot warning). Deviation from plan: dying threads do NOT clear their entries — a mutex still locked by a dead thread must be leaked, not destroyed (destroy of a locked mutex is the very UB this fix removes); the dangling CL_Thread* is only ever NULL-compared.
- [x] **I5 [A] interrupt delivery to parked threads** — all three layers, plus an **Amiga-found fixup**: the pre-park/post-wake checks must CONSUME the interrupt via cl_thread_handle_interrupt inside bi_condition_wait itself, not defer to "the next safepoint" — Amiga JIT'd caller loops contain NO safepoints (JIT code polls nothing; builtin calls go cl_jit_runtime_call → cl_vm_apply without one), so a returned spurious-wakeup spun `(loop (condition-wait ...))` forever and the first FS-UAE run hung exactly there (diagnosed with in-runtime printf markers: wake+broadcast worked, waiter spun in the pre-park check). Layers: (1) bi_condition_wait (timed + untimed) checks interrupt_pending after publishing its wait registration (barrier-paired with the publisher) and handles it in place; (2) publishers capture the target's wait ids under cl_thread_list_lock (barrier after the pending store) and, after unlocking, wake via `wake_interrupted_waiter`: bounded trylock of the USER mutex then broadcast — during the target's pre-park window the target itself holds that mutex, so acquiring it proves the target has atomically parked; a third-party long hold means the target can't be pre-park and an unlocked broadcast suffices; (3) blocking acquire-lock is an escalating trylock loop (256 yield-spins for fast contended handoffs — no sento latency regression — then 10ms sleeps) with an interrupt check per round, handler run outside the safe region. NEW prerequisite: **safe regions now NEST** (CL_Thread.safe_region_depth; only the outermost enter/leave touches gc_mutex/in_safe_region) — needed because platform_sleep_ms brackets internally and is called from inside bracketed loops.
- [x] **T5 [A] shutdown teardown** — cl_shutting_down set in cl_thread_shutdown; with cl_thread_count > 0 after main unregisters, the GC/registry primitives are deliberately LEAKED (one-shot stderr note) instead of destroyed under live workers; cl_thread_unregister null-guards the registry lock.
- [x] **G6 [A] safe-region bracketing (selective)** — bracketed with C-memory copies first: POSIX+Amiga file open (path copy, ≤1023 bytes else unbracketed), flush, seek/set-position/length, platform_sleep_ms, platform_system (malloc copy of the command — bi_system_command passed s->data arena ptr), platform_directory (pattern copy; Amiga brackets the whole Match walk); Amiga additionally brackets the 4KB IOBuf refill Read / flush Write and routes the no-IOBuf direct-write fallback through bracketed 512B C-stack chunks. Per-char getchar/write-char and sub-4KB buffered writes stay UNBRACKETED by design — enter/leave costs a gc_mutex lock + broadcast each, which would dwarf hot LOAD/print paths; documented as accepted short STW stalls (mirrors the FFI decision). >chunk-size platform write callers must pass C memory — holds for all current callers (FASL buffers are platform memory; the stream layer chunks arena strings through rooted CL_Objs).
- [x] **P-out_str [A] out_str_lisp conversion** — new `out_str_lisp(CL_Obj)` (chunked 256B C-buffer copy, rooted obj, per-chunk re-derive with the m68k volatile workaround; column tracking via out_str on the C copy), `out_wide_str_lisp` (per-code-point re-derive) and `out_symbol_name_lisp` (whole C copy first — the *print-case* logic scans the full name up front). Converted: print_string + the wide-string case (rooted obj, per-char re-derive), symbol case (all 5 out_symbol_name sites + package prefix + post-write sym->package re-derive), pprint-dispatch splice, package/function/closure/bytecode/struct(#S name + slot keywords)/condition/restart printers — including every `out_str(cl_symbol_name(...))` (kept no-*print-case* semantics via out_str_lisp of the name string) and post-blocking-write re-derives of obj-relative pointers in those cases. Closes backlog item 1. MT-only (out path doesn't allocate — single-threaded behavior unchanged; gc-stress printer suite still green).
- [x] **T6-T9 + F7/F8 hardening (LOW)** — T6: late joiner spins on the owner's wrapper stamp (`tobj->thread_id == -1` via rooted args[0], read OUTSIDE the safe region, enter/leave per round) instead of the ABA-prone freed-CL_Thread pointer compare; T7: leave_safe_region captures jit_park_sp AFTER acquiring gc_mutex; F7: interrupt closure local protected across cl_vm_apply in cl_thread_handle_interrupt; T9: condition-wait park guard aborts on rdlock_tables_held (rdlock_package_held is NOT checked here — nothing increments that counter yet, since package.c/symbol.c take cl_package_rwlock directly rather than through a counted wrapper; the check is deferred until such a wrapper exists — see review log 2026-07-04); V10: GF-type-table GC roots registered deterministically from cl_vm_init (racing the old fully-lazy init double-registered roots; the remaining lazy intern of slot 0 is benign to race). V9 (computed-goto dispatch table): no fix needed — the `&&label` initializer is materialized at link time, not lazily at first entry.

Batch tests: extend tests/test_gc_threaded.c + test_mt_join_race.sh style shell tests (join-identity, close-race); Amiga FS-UAE MT checks; I5 gets a host test (destroy a thread parked on a never-signaled condvar → must terminate); gc-stress unaffected paths re-run.

Batch 6a tests (registered in the fast tier): tests/test_mt_thread_identity.sh (deterministic — pre-fix at 7b83059: T1-ALIVE=T + T1-INT=SENT on the innocent occupant of the reused slot; post-fix all refused), tests/test_mt_intern_stw.sh + tests/test_mt_stream_close_race.sh (smoke guards — the pre-fix races did not reproduce on the macOS scheduler, even under CLAMIGA_GC_STRESS; kept for regression coverage and Amiga runs). gc-stress 326/326 post-fix (the 4 initial failures were the CL_T latent root, above).

Batch 6b tests: tests/test_mt_interrupt_parked.sh (deterministic — pre-fix at 8cd41de the parked destroy hangs to the 60s timeout; post-fix clean) covering I5 condwait/lockwait destroy + parked interrupt + I8 held-lock finalize + T5 exit-with-workers; Amiga t4b6 mirror block in tests/amiga/run-tests.lisp. Batch 6c tests: tests/test_mt_print_stress.sh (smoke — concurrent printers on one contended file stream + allocator churn; byte-set check catches freed-arena garbage). All registered in the fast tier.

---

## Batch 7 — Conformance / diagnostics tail (optional, after corruption fixes)

- [x] **bi_apply C path silent 64-arg truncation (7a)** — spreads onto the VM stack (rooted, no cap up to CALL-ARGUMENTS-LIMIT); sp restored on every exit path.
- [x] **FS5 (7a)** — ~? function-control parent-arg snapshot is now an uncapped GC vector (copy-back after apply); call args staged on the VM stack. Same treatment for the ~? string-control sub_args[64], %formatter-inner fmt_buf[66], and both ~:{/~:@{ sublist sub_args[64] caps (all silently dropped elements past 64). FMT_MAX_ARGS deleted.
- [x] **FS6 (7a)** — fmt_case_convert branches on CL_WIDE_STRING_P: per-code-point ASCII case ops + per-code-point output (cl_stream_write_lisp_string is a base-string byte writer — feeding it a wide string emitted the raw UTF-32 low bytes).
- [ ] AH5 hashtable hash/equality contract: content-hash wide strings + EQUALP vectors; add keys_equal bit-vector branch (CLHS EQUAL).
- [x] **S9 + FS13 (7a)** — remove_from_string stages kept chars in a GC vector (no 1023 truncation, no (char) wide mangling, width-preserving result); concatenate string path stages in a GC vector (no 4096 truncation, no 20KB wide stack frame). Bonus find while testing: COERCE char/list/vector→string narrowed wide chars the same way — now promotes to a wide string when any code point > 0x7F.
- [ ] Reader token caps should signal reader errors instead of silently splitting/truncating (symbol >255 → two tokens; 300-digit bignum parses as two numbers!; strings >4095; #* bits; #\ names) — per the diagnostics policy.
- [ ] R-srcloc: cons-offset-keyed srcloc table gives wrong file/line after compaction + unlocked MT writes (cosmetic).
- [ ] ST7 file-position/length bignum instead of fixnum truncation (>~512MB files); ST8 finalize-close leaked fds (design decision: warn vs auto-close); ST9 register stream-layer roots immediately after assignment.
- [ ] mapl/mapcon missing cl_coerce_funcdesig; mapcar-family silent 16-list cap; every/some/map/map-into 16-seq caps → error or document.
- [x] **FS7 + S-LOW (7a)** — format's platform_alloc sites route through fmt_alloc (loud CL_ERR_STORAGE instead of NULL-deref / silent ""), and the sequence-family buffers held across user :test/:key calls are GC objects now (KEEP_BV_* bit-vectors in remove_from_vector/bitvector + remove-duplicates, GC-vector snapshot in bi_replace, SORT_TMP GC vector in array_seq_insertion_sort) — a user-fn longjmp can no longer leak them. Note: CL_CATCH-based cleanup was rejected — a Lisp handler-case catch longjmps PAST intervening C error frames, so only GC staging is leak-proof.
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
