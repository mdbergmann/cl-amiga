;;; test_amiga_reaction.lisp — host-side checks of lib/amiga/reaction.lisp
;;; (AMIGA.REACTION), driven by tests/test_amiga_reaction.sh.
;;;
;;; The host has no ReAction (no AmigaOS at all): what can be checked here
;;; is the ReAction-specific portable surface — AVAILABLE-P, NEW-OBJECT's
;;; class-pointer check, the DO-WINDOW-EVENTS expansion — that the
;;; toolkit-neutral half (the pool, WITH-TAGS, DO-METHOD, GET-ATTR ...) is
;;; the very AMIGA.BOOPSI implementation re-exported rather than a copy
;;; (its own checks are tests/test_amiga_boopsi.lisp), and that every
;;; example under examples/amiga/reaction/ loads (all of its code
;;; compiles) and bows out with its "not available" line.
;;; tests/amiga/test-reaction.lisp adds the class half on the Amiga.

(defvar *pass* 0)
(defvar *fail* 0)

(defmacro check (name expected form)
  `(handler-case
       (let ((e ,expected) (a ,form))
         (if (equal e a)
             (progn (incf *pass*) (format t "PASS: ~A~%" ,name))
             (progn (incf *fail*) (format t "FAIL: ~A - expected ~S got ~S~%" ,name e a))))
     (error (c)
       (incf *fail*)
       (format t "FAIL: ~A - signaled error: ~A~%" ,name c))))

(require "amiga/reaction")

(check "host-reaction-not-available" nil (amiga.reaction:available-p))

;; the toolkit-neutral half is AMIGA.BOOPSI's, re-exported: the same
;; symbols (so one implementation, tested in tests/test_amiga_boopsi.lisp),
;; and the %-helpers this module uses itself are imported, not copied
(check "reaction-shares-boopsi-symbols" t
  (let ((ok t))
    (dolist (name '("DO-METHOD" "OBJECT-CLASS" "WITH-FOREIGN-POOL" "POOL-ALLOC"
                    "POOL-STRING" "NEW-LIST" "FREE-LIST-NODES" "WITH-TAGS"
                    "GET-ATTR" "GET-ATTR-POINTER" "SET-ATTRS" "%ULONG" "%WITH-TAGS"))
      (unless (eq (find-symbol name "AMIGA.REACTION") (find-symbol name "AMIGA.BOOPSI"))
        (format t "  ~A differs~%" name)
        (setf ok nil)))
    ok))

;; the existing spelling keeps working: a pooled string through the
;; AMIGA.REACTION names, and the ULONG coercion the tests always reached
;; as amiga.reaction::%ulong
(check "reaction-pool-string-roundtrip" "ReAction"
  (amiga.reaction:with-foreign-pool ()
    (ffi:foreign-to-string (amiga.reaction:pool-string "ReAction"))))

(check "reaction-ulong-coercions" '(0 1 #xFFFFFFFF)
  (list (amiga.reaction::%ulong nil) (amiga.reaction::%ulong t)
        (amiga.reaction::%ulong -1)))

(check "reaction-new-object-rejects-nil-class" t
  (handler-case (progn (amiga.reaction:new-object nil 1 2) nil)
    (error (e) (and (search "class pointer" (format nil "~A" e)) t))))

(check "reaction-do-window-events-is-a-block" :done
  ;; the macro must expand even though it cannot run here
  (progn (macroexpand-1 '(amiga.reaction:do-window-events ((r c) obj) (return)))
         :done))

(check "reaction-event-loop-timeout-default-nil" nil amiga.reaction:*event-loop-timeout*)

;;; --- the examples load on the host and bow out ------------------------

(defparameter *examples*
  '("buttons" "checkbox" "chooser" "clicktab" "fuelgauge" "integer"
    "listbrowser" "requester"))

(dolist (name *examples*)
  (check (format nil "example-~A-loads-and-bows-out" name) t
    (let ((output (with-output-to-string (*standard-output*)
                    (load (format nil "examples/amiga/reaction/~A.lisp" name)))))
      (and (search "not available" output) t))))

(format t "~%~D passed, ~D failed~%" *pass* *fail*)
(if (zerop *fail*)
    (format t "ALL REACTION HOST CHECKS PASSED~%")
    (format t "SOME REACTION HOST CHECKS FAILED~%"))
