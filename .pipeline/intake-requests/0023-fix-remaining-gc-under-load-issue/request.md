---
id: 0023-fix-remaining-gc-under-load-issue
type: bug
status: ready
title: fix remaining gc under load issue
---

# fix remaining gc under load issue

What's actually broken

  handler-case (and other gensym-binding macros) compiled under CLAMIGA_GC_STRESS=1 produce Unbound variable: BOX<n> — or, at other sites, Undefined function: <name> (a whole defun dropped).

  Key fact that scopes the search: it's compile-time only. A clean-compiled FASL loads and runs fine under stress:
  compile clean → load under stress → RESULT:caught: boom   ✓
  compile under stress                → Unbound variable: BOX2  ✗
  So ignore the runtime/FASL/load paths entirely. The bug is an unprotected CL_Obj in the compiler held across an allocating recursive call — the exact same class as the PROGN cursor I just fixed,
  just a different site (and there are probably several).

  Two mechanisms, two symptoms

  - Dropped form → "Undefined/unbound <name>": a list cursor x = cl_cdr(x) walks a body/binding list, an allocating compile_expr/macroexpand in the loop triggers compaction, x goes stale, later
  elements are skipped. (This was the PROGN bug.)
  - Diverged gensym → "Unbound variable: BOX<n>": the lexical env records the binding symbol, then a compaction relocates that gensym; the binding side and the use side end up with different offsets,
  so eq fails and the reference compiles as a free/special variable that's unbound.

  Where to look (ranked)

  compile_let's bindings cursor is already protected (compiler.c:1561), so it's not there. Audit, in order:
  1. Boxing pre-scan — determine_boxed_vars / scan_body_for_boxing (compiler.c ~941–1100). It re-enters the VM for macroexpansion and walks binding/body lists; prime suspect for gensym divergence.
  2. Lambda/defun body walk and the lexical-env construction (cl_build_lex_env, env.c) — where binding symbols are recorded vs. resolved.
  3. typecase / handler-bind clause-list compilation in compiler_special.c.
  4. Quasiquote builder in compiler_extra.c — dozens of nested cl_cons(A, cl_cons(...)) where A is movable (same unspecified-arg-eval-order hazard as the condition fix).

  Mechanical audit rule: grep the compiler for any loop doing x = cl_cdr(x) whose body calls an allocating function (compile_expr, cl_macroexpand_1*, cl_vm_apply, cl_cons, cl_build_lex_env). The
  cursor — and any CL_Obj derived from it used after the call — must be CL_GC_PROTECT'd. Same for nested cl_cons with a movable outer arg.

  Diagnostic recipe that cracked the last one (reuse it)

  1. Build make host BUILDDIR=build/host-gcdbg DEBUG_FLAGS="-DDEBUG_GC_STRESS -DDEBUG_GC".
  2. Wire gc_verify_marked() into cl_gc_compact() right after gc_mark(). It currently only runs in cl_gc() — which is why this whole bug class went undetected. It prints marked @X.field -> unmarked @Y
  at the precise corrupting compaction.
  3. If the bad value is 0x800000, that's CL_HDR_MARK_BIT set on an interior pointer — add a per-gc_roots[i] "is this an object start?" check + file:line tagging on CL_GC_PROTECT (a debug
  cl_gc_push_root_dbg(&v, __FILE__, __LINE__)) to name the exact holder. That's how cl_cons's arg at mem.c:378 got fingered.
  4. Track one symbol's field across compactions (find it by name in the arena each GC, print transitions + last_builtin_name) to see which compaction flips it.

  Minimal repros to bisect with

  ;; gensym divergence:
  (handler-case (error "x") (error (c) (format nil "~a" c)))   ; → Unbound variable: BOX<n>
  ;; dropped form:
  (defmacro with-g (&body b) (let ((g (gensym))) `(let ((,g (list 1 2 3))) ,@b)))
  (defun f () (with-g 42))                                      ; → Undefined function: F
  Bisect: clean-compile+load-under-stress first (compile vs runtime), then which construct (progn / let / defun / typecase) by shrinking the macro body.
