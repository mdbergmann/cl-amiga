---
id: 0017-fix-unwind-protect-cleanup-order
type: bug
status: ready
title: fix unwind-protect cleanup order
---

# fix unwind-protect cleanup order

a clamiga VM bug — restart handler runs before unwind-protect cleanups

  When you invoke a restart, CL must unwind the stack first (running every intervening unwind-protect cleanup), and then evaluate the chosen restart-case clause. clamiga does it backwards — it runs
  the handler clause first, then the cleanups.

  I proved it with an 8-line repro, no SLY involved (/tmp/clamiga_restart_uwp_repro.lisp):

  (restart-case
      (unwind-protect
           (progn (tick "before")
                  (invoke-restart (find 'abort (compute-restarts) :key #'restart-name)))
        (tick "CLEANUP"))
    (abort () (tick "HANDLER") :done))

  ;; clamiga prints:  before → HANDLER → CLEANUP     ← WRONG
  ;; correct CL:      before → CLEANUP → HANDLER
