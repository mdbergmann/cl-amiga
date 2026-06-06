---
id: 0022-compile-file-evaluates-load-time-value-forms-at-co
type: bug
status: ready
title: COMPILE-FILE evaluates LOAD-TIME-VALUE forms at compile time, violating CLHS
---

# COMPILE-FILE evaluates LOAD-TIME-VALUE forms at compile time, violating CLHS

  Summary

  make test-plus fails in host-cold-test. While COMPILE-FILE-ing quicklisp/deflate.lisp, the compiler aborts one top-level form with:

  ; Compiling /Users/mbergmann/quicklisp/quicklisp/deflate.lisp
  ERROR: Undefined function: GENERATE-CRC32-TABLE
  Backtrace:
    0: <anonymous> (.../deflate.lisp:156)
    1: PERFORM-LISP-COMPILATION (lib/asdf.lisp:14235)
    ...

  and the cold-load run later dies with Abort trap: 6, so the sento suite never reports a result and the test tier fails.

  Root cause

  LOAD-TIME-VALUE is being evaluated eagerly at compile time, on the assumption that "compile-time IS load-time." That assumption holds for the REPL / LOAD single-pass path, but is false for
  COMPILE-FILE.

  src/core/compiler_extra.c:995 — compile_load_time_value:

  void compile_load_time_value(CL_Compiler *c, CL_Obj form)
  {
      /* (load-time-value form &optional read-only-p)
       * In single-pass compile-and-eval, compile-time IS load-time.
       * Evaluate the form now in the null lexical environment,
       * then emit the result as a constant. */
      CL_Obj value_form = cl_car(cl_cdr(form));
      ...
      bytecode = cl_compile(value_form);
      ...
      result = cl_vm_eval(bytecode);          // <-- runs (generate-crc32-table) NOW
      ...
      cl_emit_const(c, result);               // bakes the value in as a constant
  }

  In COMPILE-FILE, top-level DEFUNs are compiled and serialized to the FASL but are not executed at compile time (only forms flagged by cf_form_needs_compile_time_eval are eval'd — see
  cf_process_toplevel_form, src/core/builtins_io.c:1242). So in deflate.lisp:

  - line 129 (defun generate-crc32-table () ...) is compiled into the FASL, not defined in the compile-time image.
  - line ~146 (defun update-crc32-checksum (crc buffer end) (let ((table (load-time-value (generate-crc32-table)))) ...)) — compiling this hits compile_load_time_value, which calls
  (generate-crc32-table) immediately. The function doesn't exist yet in the compile-time environment → Undefined function: GENERATE-CRC32-TABLE.

  The Abort trap: 6 is a downstream consequence: the erroring form is dropped, so update-crc32-checksum is missing from deflate.fasl, leaving quicklisp's deflate broken for the rest of the cold-load
  run.

  Why this is a conformance bug (HyperSpec)

  CLHS load-time-value (Special Operator), under compile-file semantics:

  ▎ If a load-time-value expression appears within a function compiled with compile-file, the form is evaluated at load time in a null lexical environment. The result is treated as a literal object
  ▎ (coalescable, optionally read-only).

  It is explicitly not evaluated at compile time. The form may reference functions/variables that only become defined when the FASL is loaded — exactly the generate-crc32-table case. CLHS 3.2.3.1
  (processing of top-level forms) and 3.2.2.3 (minimal compilation) further establish that compile-file must not evaluate code except where Table 3-9 / minimal compilation require.

  By contrast, the existing host unit tests in tests/test_vm.c (test_eval_load_time_value_side_effect, :8717) assert eager evaluation — but those run in the REPL/eval path, where eager eval is
  correct. The bug is specifically that the same eager path is taken under COMPILE-FILE, where it must not be.

  Reproduction

  make test-plus          # fails at host-cold-test

  Minimal repro (any file where a load-time-value form references a function defined earlier in the same file):

  ;; ltv-repro.lisp
  (defun make-table () (list 1 2 3))
  (defun get-table () (load-time-value (make-table)))
  clamiga> (compile-file "ltv-repro.lisp")   ; ERROR: Undefined function: MAKE-TABLE

  Required behavior

  In COMPILE-FILE, load-time-value must defer evaluation to load time while preserving the "evaluated once, same object on every reference" guarantee ((eq (get-table) (get-table)) => T), in the null
  lexical environment. The eager path must be kept for the REPL/LOAD single-pass mode so the existing test_eval_load_time_value_* tests still hold.

  Proposed implementation

  1. Add a COMPILE-FILE-active flag. A save/restored global (cl_compiling_to_file, set around the read-compile loop in bi_compile_file, src/core/builtins_io.c). Nesting-safe via save/restore.
  2. In compile_load_time_value, branch on it:
    - Flag off (REPL/LOAD): keep the current eager cl_vm_eval + cl_emit_const (unchanged; preserves current tests/semantics).
    - Flag on (COMPILE-FILE): emit a deferred, memoized evaluation so the body runs at load time, once, with a stable result. Synthesize and compile_expr a form equivalent to:
  (let ((g (quote <fresh-cons-cell>)))   ; cell = (nil . nil), a per-FASL-load constant
    (if (car g)
        (cdr g)
        (progn (rplacd g <value-form>) (rplaca g t) (cdr g))))
    - The quoted cell is serialized into the FASL and reconstructed fresh per load, giving correct per-load "evaluate once, same object thereafter" semantics. The value-form is compiled into the
  function body, so it runs only when the FASL is loaded and the function first executes — by which point generate-crc32-table is defined. (GC note: cell + intermediate conses must be CL_GC_PROTECT'd
  while building, per the project GC-safety rules.)

  Note: this is lazy-once (first-call) rather than strictly at-load-instant. For pure forms it is observationally identical; if strict load-time evaluation is required, the alternative is a per-FASL
  load-time-value table evaluated by the loader before any unit code runs — a larger change requiring a CL_FASL_VERSION bump.
  3. FASL coalescing caveat: ensure constant dedup is by identity (eq), not structural equal. Two distinct load-time-value forms each producing a (nil . nil) memo cell must not be merged, or they'd
  cross-contaminate. Worth an explicit regression test.

  Tests to add (per "tests are our specification")

  - Host (tests/test_vm.c): keep existing eager-path tests; they validate the REPL branch.
  - Compile-file path (new C test driving bi_compile_file, or a shell test under tests/):
    - load-time-value referencing a function defun'd earlier in the same file compiles without error and works after load (the deflate regression).
    - (eq (f) (f)) => T for a load-time-value returning a fresh cons — confirms once-and-stable.
    - Two distinct load-time-value forms return independent objects — confirms no eq-dedup cross-contamination.
  - Amiga (tests/amiga/run-tests.lisp): mirror the above.
  - If a FASL wire-format change is chosen (option-3 alternative), bump CL_FASL_VERSION in src/core/fasl.h and run make fasl.

  Affected files

  - src/core/compiler_extra.c — compile_load_time_value (:995)
  - src/core/builtins_io.c — bi_compile_file (set flag), cf_process_toplevel_form (:1242, context)
  - tests/test_vm.c + new compile-file regression test
  - tests/amiga/run-tests.lisp
