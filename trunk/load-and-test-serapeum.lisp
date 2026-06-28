;; Load and test Serapeum.
;; Works on both host and Amiga.
;;
;; Usage:
;;   ./build/host/clamiga --heap 48M --load trunk/load-and-test-serapeum.lisp

(require "asdf")

;; Make the quicklisp-managed dependencies resolvable through ASDF.
;; All quicklisp handling lives in this helper; this script only uses
;; asdf:load-system.
(load "trunk/load-libs-ql.lisp")

(format t "~%--- Loading Serapeum ---~%")
(ensure-ql-lib :serapeum)        ; fetch from dist only if not in local repo
(asdf:load-system :serapeum)

(format t "~%--- Running Serapeum tests ---~%")
;; Serapeum's TEST-OP calls (serapeum.tests:run-tests), which is
;; (5am:run! 'serapeum): it prints the per-test status and a summary, and
;; signals nothing on failure.  We additionally tally an explicit,
;; machine-readable pass/fail count from FiveAM's result objects below so
;; the trunk/run-load-and-test-all.sh aggregator can recognize the outcome.
(asdf:load-system :serapeum/tests)

(let* ((five (find-package "IT.BESE.FIVEAM"))
       (run-sym (and five (find-symbol "RUN" five)))
       (results (and run-sym (funcall run-sym (read-from-string "serapeum")))))
  (flet ((status-name (r)
           ;; FiveAM result classes: TEST-PASSED / TEST-FAILURE /
           ;; TEST-SKIPPED / UNEXPECTED-TEST-FAILURE etc.  Classify by the
           ;; class name so we carry no read-time dependency on the symbols.
           (let ((cn (symbol-name (class-name (class-of r)))))
             (cond ((search "PASSED" cn) :pass)
                   ((search "SKIP" cn) :skip)
                   (t :fail)))))
    (let ((passed 0) (failed 0) (skipped 0))
      (dolist (r results)
        (case (status-name r)
          (:pass (incf passed))
          (:skip (incf skipped))
          (t (incf failed))))
      (format t "~%=== Serapeum Summary ===~%")
      (format t "passed: ~A~%" passed)
      (format t "failed: ~A~%" failed)
      (format t "skipped: ~A~%" skipped))))

(format t "~%=== Serapeum done ===~%")
