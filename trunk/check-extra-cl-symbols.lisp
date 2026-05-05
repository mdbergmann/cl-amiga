;; Diagnostic: list every external CL symbol whose name is NOT in the
;; ANSI *cl-symbol-names* list.  Mirrors the no-extra-symbols-exported test.
(defparameter *ansi-test-dir* (truename "third_party/ansi-test/"))
(load (merge-pathnames "compile-and-load.lsp" *ansi-test-dir*))
(load (merge-pathnames "rt-package.lsp" *ansi-test-dir*))
(compile-and-load (merge-pathnames "rt.lsp" *ansi-test-dir*))
(load (merge-pathnames "cl-test-package.lsp" *ansi-test-dir*))
(in-package :cl-test)
(common-lisp:load
 (common-lisp:merge-pathnames "cl-symbol-names.lsp"
                              (common-lisp:symbol-value
                               'common-lisp-user::*ansi-test-dir*)))

(let ((ht (make-hash-table :test 'equal))
      (extras nil))
  (dolist (n *cl-symbol-names*) (setf (gethash n ht) t))
  (do-external-symbols (s "CL")
    (unless (gethash (symbol-name s) ht)
      (push (symbol-name s) extras)))
  (setq extras (sort extras #'string<))
  (format t "~%~D extras exported from COMMON-LISP:~%" (length extras))
  (dolist (n extras) (format t "  ~A~%" n)))
