---
id: 0018-fix-sly-debugging-1
type: bug
status: ready
title: fix sly debugging 1
---

# fix sly debugging 1

What's still broken ❌ — a distinct second VM bug

  :debug-return is still never sent, so the SLDB buffer stays open. I traced it on the live server by instrumenting every cleanup + handler on the real path. The log showed:

  LOOP-ENTER level=1            ← entered sly-db-loop
  EVAL-FOR-EMACS-CLEANUP id=99  ← the q rex's cleanup (sends :return :abort)
  ‹nothing after this›          ← no sly-db-loop cleanup, no ABORT handler

  The invoke-restart ran the innermost cleanup and then the transfer was truncated — the handler and all outer cleanups were skipped, and control just resumed locally in sly-db-loop's event loop.

  Root cause (18-line pure-CL repro, no SLY/threads/IO — /tmp/clamiga_sldb_debugreturn_repro.lisp)

  (defun nested-uwp (thunk) (unwind-protect (funcall thunk) nil))  ; like call-with-lock-held

  (restart-case
      (unwind-protect
           (invoke-restart (find 'abort (compute-restarts) :key #'restart-name))
        (nested-uwp (lambda () nil)))        ; ← nested unwind-protect INSIDE the cleanup
    (abort () (setf *handler-ran* t)))
  ;; => handler-ran = NIL          ← BUG, transfer truncated
  ;; plain (progn nil) cleanup     => handler-ran = T   ← works

  A non-local transfer (invoke-restart/throw) is truncated when an intervening unwind-protect cleanup itself runs a nested unwind-protect. The nested UWP, executed mid-transfer, clobbers the saved
  transfer target, so the rest of the stack never unwinds.

  It hits SLDB because on every debugger exit, eval-for-emacs's cleanup calls send-to-emacs → call-with-lock-held (a nested unwind-protect). That swallows the transfer before sly-db-loop's
  :debug-return cleanup or the top-level abort handler can run.

  This is the same family as the mv-through-unwind-protect bug you already fixed — there only the secondary values were dropped; here the whole pending transfer is dropped.

  The fix (clamiga C side)

  When running an unwind-protect cleanup during an in-progress non-local transfer, save and restore the pending transfer target (the catch/restart unwind state) across the cleanup, so a nested
  unwind-protect inside the cleanup can't overwrite it.
