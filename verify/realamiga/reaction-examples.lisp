;;; reaction-examples.lisp — run every examples/amiga/reaction/ program
;;; unattended, one after the other, in this one clamiga process.
;;;
;;; Each example opens its window, sits in its event loop for
;;; *EVENT-LOOP-TIMEOUT* seconds (nobody clicks in an unattended run),
;;; closes and returns; a failure to load or run is reported per example
;;; and does not stop the others.  The host side (run-reaction-examples.sh)
;;; greps the EXAMPLE-OK / EXAMPLE-FAIL / EXAMPLES-DONE lines.
;;;
;;; Also loaded by the Amiga test suite's test-reaction.lisp? No — the
;;; suite has its own focused checks; this is the examples' smoke run.

(require "amiga/reaction")

(defparameter *examples*
  '("buttons" "checkbox" "chooser" "clicktab" "fuelgauge" "integer"
    "listbrowser" "requester"))

(setf amiga.reaction:*event-loop-timeout* 6)

(unless (amiga.reaction:available-p)
  (format t "~&EXAMPLES-SKIPPED: ReAction classes not available on this system~%")
  (finish-output))

(when (amiga.reaction:available-p)
  (let ((ok 0) (failed 0))
    (dolist (name *examples*)
      (format t "~&=== example: ~A ===~%" name)
      (finish-output)
      (let ((start (get-internal-real-time)))
        (handler-case
            (progn
              (load (format nil "examples/amiga/reaction/~A.lisp" name))
              (incf ok)
              (format t "~&EXAMPLE-OK ~A (~,1F s)~%" name
                      (/ (- (get-internal-real-time) start)
                         internal-time-units-per-second)))
          (error (e)
            (incf failed)
            (format t "~&EXAMPLE-FAIL ~A: ~A~%" name e))))
      (finish-output))
    (format t "~&EXAMPLES-DONE ~D ok, ~D failed~%" ok failed)
    (finish-output)))
