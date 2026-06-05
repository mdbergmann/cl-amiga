---
id: 0021-fix-compile-file
type: bug
status: ready
title: fix compile-file
---

# fix compile-file

Both clamiga bugs are now flagged and recorded to memory. Here they are.

  🐞 Two clamiga conformance bugs (flagged)

  Both surfaced while making SLY use the generic slynk-loader path instead of the bespoke clamiga-start.lisp. Both are clamiga-side, both have minimal repros, both currently worked around.

  Bug A — compile-file evaluates top-level forms at compile time

  clamiga runs side-effecting top-level forms (e.g. a defvar's make-package value form) during compile-file, not just at load. So compile-file → load the fasl in the same image runs them twice. Per
  CLHS 3.2.3.1 only (eval-when (:compile-toplevel) …) bodies (and defining-macro effects) may run at compile time.

  clamiga --eval '(compile-file "/tmp/pkgtest.lisp" :output-file "/tmp/pkgtest.fasl")' \
          --eval '(load "/tmp/pkgtest.fasl")'
  # pkgtest.lisp = (defvar *p* (make-package :pkgtest-foo))
  # => make-package prints during "; Compiling", then load re-runs it => ERROR: Package already exists
  This broke loading slynk.fasl ("Package already exists" at slynk.lisp:364). C location: bi_compile_file, src/core/builtins_io.c:1103. Note clamiga's own load-of-source auto-cache (builtins_io.c:269)
  does not double-run, and boot.fasl/clos.fasl are unaffected (separate compile/load processes).

  Bug B — load ignores :if-does-not-exist nil

  (load "missing.lisp" :if-does-not-exist nil) must return NIL; clamiga errors.
  clamiga --eval '(load "/tmp/nope.lisp" :if-does-not-exist nil)'   # => ERROR: LOAD: cannot open file
  Root cause: bi_load (builtins_io.c:373) is CL_UNUSED(n) and reads only args[0] — it parses none of its keyword args (the registration comment at :3213 even claims :verbose, :print). On open failure
  it unconditionally errors at :579.
