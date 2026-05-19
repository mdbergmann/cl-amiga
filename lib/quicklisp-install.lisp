;;; quicklisp-install.lisp — tiny wrapper around lib/quicklisp.lisp
;;;
;;; Just enough to install quicklisp on a fresh CL-Amiga system. After
;;; this finishes, use quicklisp the normal way:
;;;
;;;   (load #P"~/quicklisp/setup.lisp")
;;;   (load "lib/quicklisp-compat.lisp")
;;;
;;; Usage on a fresh system (where ~/quicklisp/ does not yet exist):
;;;
;;;   (require :asdf)
;;;   (load "lib/quicklisp-install.lisp")
;;;   (cl-amiga-ql:install)
;;;
;;; Mechanism: QUICKLISP-QUICKSTART:INSTALL downloads the QL client
;;; into ~/quicklisp/ and finishes by calling (QUICKLISP:SETUP), which
;;; tries to fetch the default dist over the network and fails because
;;; CL-Amiga isn't a registered ql-impl. We catch that error, load
;;; lib/quicklisp-compat.lisp (its IN-PACKAGE forms now resolve, since
;;; the ql-* packages were loaded by ASDF before the failure), and
;;; retry (QUICKLISP:SETUP) — which is idempotent and proceeds with
;;; networking patched in.

(defpackage #:cl-amiga-ql
  (:use #:cl)
  (:export #:install))

(in-package #:cl-amiga-ql)

(defvar *here*
  (make-pathname :name nil :type nil
                 :defaults (or *load-truename* *load-pathname*)))

(defun install ()
  "Install quicklisp under ~/quicklisp/ on a fresh CL-Amiga system."
  (load (merge-pathnames "quicklisp.lisp" *here*))
  (let ((qq-install (find-symbol "INSTALL" "QUICKLISP-QUICKSTART"))
        (compat (merge-pathnames "quicklisp-compat.lisp" *here*)))
    (handler-case
        (funcall qq-install)
      (error (c)
        (let ((ql (find-package "QUICKLISP"))
              (ql-dist (find-package "QL-DIST")))
          (unless (and ql ql-dist)
            (error c))
          (format t "~&;; cl-amiga-ql: ~A~%~
                     ;;             loading compat shim + retrying dist install~%"
                  c)
          (load compat)
          ;; The failed setup left ~/quicklisp/dists/quicklisp/
          ;; as an empty directory, so QUICKLISP:SETUP's
          ;; idempotency check (DISTS-INITIALIZED-P) now thinks
          ;; the dist is already on disk and skips
          ;; MAYBE-INITIAL-SETUP. Install the dist ourselves with
          ;; :REPLACE T to overwrite the stub, then run SETUP
          ;; again to finish the rest of the init.
          (funcall (find-symbol "INSTALL-DIST" ql-dist)
                   (symbol-value (find-symbol "*INITIAL-DIST-URL*" ql))
                   :prompt nil :replace t)
          (funcall (find-symbol "SETUP" ql))))))
  (format t "~&;; cl-amiga-ql: install complete. To use:~%~
             ;;   (load #P\"~~/quicklisp/setup.lisp\")~%~
             ;;   (load \"lib/quicklisp-compat.lisp\")~%")
  t)
