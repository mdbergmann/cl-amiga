;; load-and-test-ansi-numbers.lisp
;;
;; Loads the rt framework + aux + universe + the NUMBERS chapter alone.
;; Used to baseline / track ANSI numbers-chapter conformance separately
;; from the cons + symbols runs.
;;
;; Usage (host):
;;   ./build/host/clamiga --heap 96M --load trunk/load-and-test-ansi-numbers.lisp

(defparameter *ansi-test-dir* (truename "third_party/ansi-test/"))
(defparameter *aux-dir* (truename "third_party/ansi-test/auxiliary/"))
(setq *default-pathname-defaults* *ansi-test-dir*)

(format t "~%=== ANSI test bootstrap (rt + aux + universe + numbers) ===~%")
(format t "ansi-test dir: ~A~%" *ansi-test-dir*)

(load (merge-pathnames "compile-and-load.lsp" *ansi-test-dir*))
(load (merge-pathnames "rt-package.lsp" *ansi-test-dir*))
(compile-and-load (merge-pathnames "rt.lsp" *ansi-test-dir*))
(load (merge-pathnames "cl-test-package.lsp" *ansi-test-dir*))

(in-package :cl-test)

(common-lisp-user::compile-and-load
 (common-lisp:merge-pathnames "ansi-aux-macros.lsp"
                              (common-lisp:symbol-value
                               'common-lisp-user::*aux-dir*)))

(common-lisp:format common-lisp:t "~%--- Loading universe.lsp ---~%")
(common-lisp:load
 (common-lisp:merge-pathnames "universe.lsp"
                              (common-lisp:symbol-value
                               'common-lisp-user::*ansi-test-dir*)))

(common-lisp-user::compile-and-load
 (common-lisp:merge-pathnames "ansi-aux.lsp"
                              (common-lisp:symbol-value
                               'common-lisp-user::*aux-dir*)))

(common-lisp-user::compile-and-load
 (common-lisp:merge-pathnames "random-aux.lsp"
                              (common-lisp:symbol-value
                               'common-lisp-user::*aux-dir*)))

(common-lisp:format common-lisp:t "~%--- Loading cl-symbol-names.lsp ---~%")
(common-lisp:load
 (common-lisp:merge-pathnames "cl-symbol-names.lsp"
                              (common-lisp:symbol-value
                               'common-lisp-user::*ansi-test-dir*)))

;; --- Load numbers chapter ---
(common-lisp:format common-lisp:t "~%--- Loading numbers chapter ---~%")
(common-lisp:load
 (common-lisp:merge-pathnames "numbers/load.lsp"
                              (common-lisp:symbol-value
                               'common-lisp-user::*ansi-test-dir*)))

(format t "~%--- Running tests via rt:do-tests ---~%")
(do-tests)
(format t "~%=== Summary ===~%")
(format t "passed: ~A~%" (length regression-test::*passed-tests*))
(format t "failed: ~A~%" (length regression-test::*failed-tests*))
(when regression-test::*failed-tests*
  (format t "~%--- Failed tests ---~%")
  (dolist (n regression-test::*failed-tests*)
    (format t "  ~A~%" n)))
