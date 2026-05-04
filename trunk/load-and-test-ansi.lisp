;; load-and-test-ansi.lisp
;;
;; Cautious bootstrap of the ANSI Common Lisp test suite (Paul Dietz / ansi-test).
;; Loads only the rt framework + minimal aux + the CONS chapter so we get a
;; clean baseline before attempting the full ~21k-test run.
;;
;; Usage (host):
;;   ./build/host/clamiga --heap 64M --load trunk/load-and-test-ansi.lisp
;;
;; The bootstrap is split into separate top-level forms (rather than wrapped
;; in a single LET) because (in-package :cl-test) needs to execute *between*
;; LOAD calls — when the whole sequence is bundled in one form, in-package's
;; package lookup happens at compile time, before the LOAD calls have run.
;;
;; We deliberately skip universe.lsp.  It defines *universe* / *mini-universe*
;; (huge condition + structure + array galleries) and triggers a very long
;; load on cl-amiga; cons tests don't depend on those.

(defparameter *ansi-test-dir* (truename "third_party/ansi-test/"))
(defparameter *aux-dir* (truename "third_party/ansi-test/auxiliary/"))
(setq *default-pathname-defaults* *ansi-test-dir*)

(format t "~%=== ANSI test bootstrap (cautious: rt + aux + cons, skip universe) ===~%")
(format t "ansi-test dir: ~A~%" *ansi-test-dir*)

(load "compile-and-load.lsp")
(load "rt-package.lsp")
(compile-and-load "rt.lsp")
(load "cl-test-package.lsp")
(in-package :cl-test)
(compile-and-load* "ansi-aux-macros.lsp")
(compile-and-load* "ansi-aux.lsp")

(format t "~%--- Loading cons chapter ---~%")
(load "cons/load.lsp")

(format t "~%--- Running CONS tests via rt:do-tests ---~%")
(let ((rt-pkg (find-package :regression-test)))
  (cond
    ((not rt-pkg)
     (format t "!! :REGRESSION-TEST package missing — bootstrap failed.~%"))
    (t
     (handler-case (funcall (find-symbol "DO-TESTS" rt-pkg))
       (error (e) (format t "!! do-tests crashed: ~A~%" e)))
     (let ((passed-sym (find-symbol "*PASSED-TESTS*" rt-pkg))
           (failed-sym (find-symbol "*FAILED-TESTS*" rt-pkg)))
       (when (and passed-sym failed-sym)
         (format t "~%=== Summary ===~%")
         (format t "passed: ~A~%" (length (symbol-value passed-sym)))
         (format t "failed: ~A~%" (length (symbol-value failed-sym))))))))
