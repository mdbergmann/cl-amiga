---
id: 0019-sly-clamiga-fix-2
type: bug
status: ready
title: sly-clamiga fix-2
---

# sly-clamiga fix-2

Progress ✅

  The transfer truncation is fixed. The original repro now passes (handler-ran = T), and live tracing confirms sly-db-loop's cleanup and the top-level abort handler now run:
  LOOP-ENTER → EVAL-FOR-EMACS-CLEANUP id=99 → SLYDB-CLEANUP-ENTER → TOPLEVEL-ABORT-HANDLER

  Still broken ❌ — premature resume (new facet of the same machinery)

  q still doesn't deliver :debug-return. The trace shows SLYDB-CLEANUP-ENTER fires, but then control jumps straight to TOPLEVEL-ABORT-HANDLER — without logging SENT and without any caught error. So
  send-to-emacs exited non-locally mid-call.

  Minimal repro (/tmp/clamiga_sldb_premature_resume_repro.lisp):
  (defun nested-uwp (thunk) (unwind-protect (funcall thunk) nil))  ; like send-to-emacs's lock
  (restart-case
      (unwind-protect (invoke-restart (find 'abort (compute-restarts) :key #'restart-name))
        (progn (mark :cleanup-start)
               (nested-uwp (lambda () (mark :inside-nested-uwp)))
               (mark :cleanup-after-nested)))          ; ← SKIPPED
    (abort () (mark :handler)))
  ;; got:      (:CLEANUP-START :INSIDE-NESTED-UWP :HANDLER)
  ;; expected: (:CLEANUP-START :INSIDE-NESTED-UWP :CLEANUP-AFTER-NESTED :HANDLER)

  The bug: when a nested unwind-protect completes inside a cleanup that's running during an in-progress non-local transfer, clamiga resumes the pending transfer immediately, skipping the cleanup code
  after the nested unwind-protect. In sly-db-loop's cleanup, the :debug-return send sits "after" send-to-emacs's internal lock — so it never lands on the wire.

  Fix direction

  Completing a nested unwind-protect inside a cleanup must not continue the outer pending transfer. The cleanup has to run to completion first, then the transfer resumes. Looks like the
  transfer-target state is being restored/re-armed at the end of the nested unwind-protect instead of being kept suspended until the enclosing cleanup finishes.

  Server is back up clean on 127.0.0.1:4005 (instrumentation reverted via git checkout; verified clean). Memory updated. Re-test with /tmp/clamiga_sldb_premature_resume_repro.lisp — fixed when the
  result includes :CLEANUP-AFTER-NESTED.
