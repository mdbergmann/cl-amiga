---
id: 0016-fix-drop-of-multiple-value
type: bug
status: done
title: fix drop of multiple-value
---

# fix drop of multiple-value

a clamiga VM bug

  destructure-case failed: NIL on q traces to this: when a return-from (or any non-local exit) unwinds through an unwind-protect, clamiga drops the secondary multiple values — only the primary
  survives. Minimal repro, no SLY involved:

  (defun cwlh (fn) (unwind-protect (funcall fn) nil))   ; like call-with-lock-held
  (defun recv () (block done (cwlh (lambda () (return-from done (values nil t))))))
  (multiple-value-list (recv))   ; => (NIL)    ← WRONG, should be (NIL T)

This VM bug silently affects any Lisp code returning (values …) through an unwind-protect — the backend workaround only dodges the one spot SLY hit. Worth a pipeline
  ticket: preserve secondary values across an unwind in cl_vm_run's throw/return-from + unwind-protect path (the cl_mv_count/value-stack restore around cleanups).
