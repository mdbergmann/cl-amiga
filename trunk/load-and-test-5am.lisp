;; Load and test fiveam via quicklisp
;; Works on both host and Amiga

(require "asdf")

;; Host: ~/quicklisp/setup.lisp, Amiga: S:quicklisp/setup.lisp
(defvar *ql-setup*
  #+amigaos #P"S:quicklisp/setup.lisp"
  #-amigaos (merge-pathnames "quicklisp/setup.lisp" (user-homedir-pathname)))

;; Install quicklisp if missing. Use FIND-SYMBOL to invoke the entry
;; point because the CL-AMIGA-QL package does not exist at read time.
(unless (probe-file *ql-setup*)
  (load "lib/quicklisp-install.lisp")
  (funcall (find-symbol "INSTALL" "CL-AMIGA-QL")))

;; Skip the SETUP load when CL-AMIGA-QL:INSTALL has just initialised
;; quicklisp in this image (re-loading SETUP.LISP triggers a second
;; ASDF:OOS on the QL client, which redefines its CLOS classes and
;; corrupts generic-function dispatch). Same goes for QUICKLISP-COMPAT:
;; re-loading it re-wraps gray-streams and breaks dispatch.
(unless (member :quicklisp *features*)
  (load *ql-setup*))

(unless (member :quicklisp-compat *features*)
  (load "lib/quicklisp-compat.lisp"))

(format t "~%--- Loading fiveam via quicklisp ---~%")
(ql:quickload :fiveam)

(format t "~%--- Running fiveam self-tests ---~%")
(asdf:test-system :fiveam)
