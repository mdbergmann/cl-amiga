# GC Audit Tier 4 — Fix Plan

Status: **IN PROGRESS** — Batches 1 (compiler criticals) and 2 (format/printer) applied on branch fix/tier4-gc-corruption; batches 3-7 pending.
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

## Batch 3 — IO / strings / reader (restore class + cursor cluster)

Files: builtins_io.c, builtins_strings.c, reader.c.

- [x] **IO1 [V] bi_load *LOAD-PATHNAME* staleness** — **done in batch 2** (both blocks: source path and FASL-cache twin; load_pathname_obj protected immediately after the first parse). Pulled forward because the new gc_mark_obj badmark guard turned it into a deterministic abort in 6 existing gc-stress cases (any nested `(load ...)` under stress).
- [ ] **Shared fix: print-control save/restore helper.** IO2 bi_write (:117-207, 13 saved values), IO3 bi_pprint (:231-243), FS12 bi_write_to_string (builtins_strings.c:1518-1565, 12 saved values) all stale-restore *PRINT-* specials across allocating prints. Fix once: a helper that snapshots the print controls into a protected `CL_Obj[16]` (loop-protect each slot / GC-vector) and restores from the forwarded copies; bi_write also must `return args[0]` not the stale `obj`. (Longer term these should be real TLV dynamic binds — also fixes the FS16 MT value-race; do the protected-snapshot now, note TLV as follow-up.)
- [ ] **IO4 [A] bi_read_delimited_list** (builtins_io.c:2034-2087): `stream` unprotected across reader+cons loop. Protect.
- [ ] **IO5/IO6/IO7 [A] pprint-dispatch cluster** (builtins_io.c): ~~IO5 set-pprint-dispatch~~ **done in batch 2** (needed by its tests). Remaining: IO6 copy-pprint-dispatch `cur`; IO7 bi_pprint_dispatch walk across allocating cl_typep (protect cursors, re-read locals from args after allocs).
- [ ] **IO8 [A] bi_require** (builtins_io.c:3412-3427): `pathnames` cursor walked across bi_load. Protect.
- [ ] **IO9 [A] bi_macroexpand** (builtins_io.c:2164-2197): `env` stale across expander iterations (walked as alist!); stale `form` EQ-test; `any_expansion` stale SYM_T copy. Protect env+form, re-read. bi_macroexpand_1: stale form compare → wrong secondary value.
- [ ] **IO10 [A] disassemble** (builtins_io.c:2635-2832): raw `bc`/`code`/`constants` held across allocating output (Gray/string streams — SLY). Fix: protect the bytecode CL_Obj, re-derive per instruction; constants/code are platform memory once re-derived from the moved object.
- [ ] **IO11 [A] extfun** (builtins_io.c:57-64): `sym` unprotected across cl_make_function (sibling `defun` fixed in tier 3). Protect + re-derive s.
- [ ] **IO12 [A]** (builtins_io.c:469, 1964): protect `merge_args[0]` across bi_merge_pathnames (match bi_compile_file's commented fix). Latent.
- [ ] **FS10 [A] bi_string_trim/left/right** (builtins_strings.c:664-701): `set = args[0]` copied before `coerce_to_string_obj(args[1])` which allocates for char designators (conforming: `(string-trim "x" #\a)`). Fix: re-read `set = args[0]` after the coerce (all three).
- [ ] **FS11 [A] bi_concatenate compound result-type** (builtins_strings.c:1160-1190): `rest` stale after `concat_elt_type_is_char` runs a deftype expander (`(concatenate '(vector octet) …)` — ironclad/babel). Fix: protect result_type, re-derive rest after each is_char/is_bit call.
- [ ] **R1 [V] reader #nA flatten** (reader.c:1483-1520): `lst` cursor + `work[8]` array unprotected across per-element cl_cons. Fix: register `&work[i]` roots (rank ≤ 8) for the loop + keep cursor in a protected local re-read per iteration.
- [ ] **R2 [V] cl_read_from_stream saved_stream** (reader.c:1636/1666): the one unprotected member of the save/restore set; restore writes a stale offset into CT->rd_stream on nested reads. Protect (mirror cl_read_standard_string_from_stream). 
- [ ] **R3 [A] cl_read_from_string** (reader.c:1678/1741): same, incl. setup allocs before reassignment. Protect.
- [ ] **R4 [A] cl_read saved_uninterned** (reader.c:1615/1622): same class, nested-stdin-read only. Protect.
- [ ] **R5 [A] ensure_feature_keywords** (reader.c:164-175 + eval_feature_expr 185-213): kw_and assigned → later interns can compact before `cl_gc_register_root(&kw_and)` registers a stale offset (one-shot, permanent). Fix: register each root immediately after its assignment; call ensure_feature_keywords() BEFORE reading head/rest in eval_feature_expr.
- [ ] **R6 [A] CL_READER_SKIP sentinel leak** (reader.c:953-974, 1018-1021, 879-881, 1437): quote/backquote/#'/dotted-cdr/#nA don't filter `#+nope`-skips → 0x06 embeds in user structure (`'#+removed-feature foo bar`). Not GC-unsafe; CLHS 2.4.8.17 conformance. Fix: re-read on skip in each handler.

Batch tests: gc-stress cases for load-under-stress *LOAD-PATHNAME* identity, write/pprint restore integrity, string-trim char designator, concatenate deftype, #2A reading, nested read-from-string; Amiga t4 checks mirroring.

---

## Batch 4 — Sequences, lists, hashtable, VM opcodes

Files: builtins_sequence.c, builtins_sequence2.c, builtins.c, builtins_lists.c, builtins_hashtable.c, builtins_array.c, vm.c.

- [ ] **S4 [V] list_merge_sort** (builtins_sequence2.c:988-995): protect `pred`/`key_fn` alongside list/mid (UNPROTECT 4). Trigger: `(sort list pred)` len ≥ 3 with any allocating predicate.
- [ ] **S5 [A] bi_map string/vector result** (builtins_sequence2.c:619-623/640-644): seqs[]/orig_seqs[] rooted only AFTER `cl_make_string`/`cl_make_vector`. Fix: re-read `seqs[i] = orig_seqs[i] = args[i+2]` after the result alloc (cursors haven't advanced yet).
- [ ] **S1 [A] remove_from_list stale elem** (builtins_sequence.c:1044→1064, 1012→1034): re-read `elem = cl_car(cur)` after the match test (cur is a forwarded root). Covers remove/remove-if/-not on lists.
- [ ] **S2 [A] bi_reduce list :from-end** (builtins_sequence.c:1994-1995): seed cursor from `args[1]` (not stale `seq`) after cl_make_vector; also the CL_CONS_P test. No user fn needed to trigger.
- [ ] **S3 [A] bi_reduce forward list** (builtins_sequence.c:2024→2033): same via apply_key window — use args[1].
- [ ] **S6 [A] array_seq_insertion_sort kval** (builtins_sequence2.c:1261-1266): protect `kval` across the inner apply_key/call_test loop (mirror vector_insertion_sort fix).
- [ ] **S7 [A] bi_map result-type staleness** (builtins_sequence2.c:517-576): re-read `result_type = args[0]` after every classify that can run a deftype expander.
- [ ] **S8 [A] bi_merge pre-classification** (builtins_sequence2.c:1062-1088): classify result-type FIRST, then read seq1/seq2/pred/key_fn from args.
- [ ] **L1 [V] bi_nsubst** (builtins_lists.c:597-628): zero protection; destructive stores through stale `tree` after user :test; `sub_args[5]` C array passed as recursion args[] (not a rooted VM slice). Fix: refactor to `nsubst_rec(new, old, tree, test_fn)` mirroring subst_rec — protect params per frame, re-read tree after each call_test/recursion before the `->car/->cdr` stores.
- [ ] **L2 [V] bi_reverse vector + bit-vector branches** (builtins.c:253-274): protect `seq` (cons/string branches do); the existing "re-fetch after GC" comment re-derives from the stale local — fix it to re-derive from the protected value.
- [ ] **L3 [V] bi_make_list** (builtins_lists.c:1076-1079): protect `init_elem` alongside result.
- [ ] **L4 [A] bi_jit_dump_bytes** (builtins.c:1060-1067): hoist `native_code`/len into C locals before the cons loop (platform memory is stable; the `bc` re-read isn't).
- [ ] **AH1 [A] bi_hash_table_pairs** (builtins_hashtable.c:942-948): protect the `chain` cursor (mirror bi_maphash fix). Backs with-hash-table-iterator AND LOOP hash iteration — the single highest-traffic finding in this batch.
- [ ] **AH2 [A] bi_make_array keyword parse** (builtins_array.c:231-333): after each classify (deftype expander via cl_vm_apply), re-read `dim_arg = args[0]` and initial_element/initial_contents/displaced_to from their remembered args[] indices (only element_type is re-read today; later PROTECTs root already-stale values).
- [ ] **AH3 [A] bi_adjust_array size checks** (builtins_array.c:1671-1724): reject negative fixnums and dims > CL_ARRAY_DIMENSION_LIMIT before `cl_make_array` (list-dims branch too). `(adjust-array v -1)` currently wraps alloc_size → heap smash.
- [ ] **AH4 [A] bi_vector_push_extend extension arg** (builtins_array.c:1589-1623): cap `new_cap` against CL_ARRAY_DIMENSION_LIMIT / overflow-check `old_len + ext`.
- [ ] **V2/V3/V4 [V] OP_DEFMACRO/OP_DEFTYPE/OP_DEFSETF stale push** (vm.c:2739-2771): push `constants[idx]` (forwarded in place) instead of the stale local after cl_register_macro/type/setf-cons.
- [ ] **V1 [A] OP_ASSERT_TYPE** (vm.c:2867-2884): re-read `val = cl_vm.stack[cl_vm.sp-1]` and `type_spec = constants[idx]` after cl_typep AND between the two condition-slot cons lines.
- [ ] **V5-V8 [A] TRACE paths** (vm.c:2069-2080, 2200-2209/2308/2346/2463, 3310-3321, 615-645): protect `func_obj` whenever traced and re-derive `f`/`callee_bc` after every trace print (OP_APPLY's `apply_func` isn't rooted at all — can be swept); inside trace_print_entry/exit protect `name_sym`/`result` across their own allocating writes.
- [ ] **V11 [A]** OP_APPLY frame push: set `new_frame->nlx_level` like OP_CALL does (or delete the write-only field).

Batch tests: gc-stress for sort/map/reduce/remove with allocating :test/:key/pred; hash-iteration under stress; nsubst with allocating test; reverse/make-list; deftype-driven make-array/adjust-array error tests (host, CLHS-verified); traced-builtin under stress; top-level defmacro/deftype/defsetf under stress.

---

## Batch 5 — GC core & allocator hardening (mem.c)

- [ ] **M1 [A] allocator size caps** (mem.c cl_make_vector/array/struct/bignum/hashtable/bit_vector): reject `length > CL_HDR_SIZE_MASK/sizeof(elt)` BEFORE computing alloc_size (uint32 `length*4` wraps for len ≥ 2^30 and sails past the 23-bit guard). Primary reachable path: FASL reader passes raw u32 (fasl.c:2434 vector len, :2514 struct n_slots) — corrupted/truncated .fasl caches reach it. Also add sanity bounds in the FASL reader itself (nice error > wrapped alloc).
- [ ] **M2 [A] JIT pin/scan OOM branches** (mem.c:1352-1369, 1474-1482): both currently degrade to real corruption (unpinned object moves under a JIT frame's spilled offset; whole conservative scan skipped → JIT-only-reachable objects swept). On the 8MB Amiga target these are plausible. Fix: on OOM, **suppress compaction for this cycle** (mark-only sweep) instead of proceeding without pins/scan; loud DEBUG_GC warning. Correct the misleading comments.
- [ ] **M3 [A] fwd-table OOM pathology** (mem.c:2122-2130/3048-3057): when the bump-sized platform_alloc fails, compaction silently never happens AND `gc_sweeps_since_compact` never resets → every 8th alloc runs a doomed full mark+sweep forever. Fix: reset the counter on fwd-alloc failure + rate-limit retries (e.g. only re-attempt when bump shrank); consider a chunked/two-pass table as a follow-up spec item, not this batch.
- [ ] **M4 [A] root-dedup skip gap** (mem.c:2593-2662): `root_slot_on_vm_stack` only exempts VM-stack aliases; gc_roots entries aliasing mv_values/pending_mv/dyn_stack/handler/restart/vm_extra_args/compiler-constants (all independently forwarded) still double-forward, and `cl_gc_audit_roots` can't see it. Fix: extend the skip to all forwarded thread regions (table of [start,end) ranges per thread) + extend the audit diagnostic likewise.
- [ ] **M7 [A] cl_make_string/wide arena-source OOM fallback** (mem.c:585-599/628-639): on copy-alloc failure, signal storage-condition instead of keeping the arena pointer across the alloc (silent garbage string).
- [ ] **M6 [A]**: comment `pre_call_mv_values` in the thread-roots walkers documenting the verified zero-alloc snapshot→bi_throw window (deliberate non-mark, regression hazard).
- [ ] **M8/M9/M10 [A] LOW**: bump+size wrap guard (`size <= arena_size && bump <= arena_size - size`); gc_is_freed vs 8-byte gap chunks (DEBUG_GC false positives); cl_mem_init resets for gc_compact_pending/gc_mark_overflow/jit_pinned_count/jit_scan_free_valid.
- [ ] **M11 [A]**: assert rank==1 at the two in-place displacement sites (builtins_array.c:1620/1748) so the GC's `data[CL_DISP_BASE_IDX]` contract can't be silently violated for rank>1.

Batch tests: test_alloc-style unit tests for M1 (host, includes FASL-reader bound), M8; gc-stress remains the gate for M2-M4 (plus a DEBUG_GC run for M9).

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
