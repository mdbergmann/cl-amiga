;; Load and test fiveam via quicklisp
;; Works on both host and Amiga

(require "asdf")

;; Host: ~/quicklisp/setup.lisp, Amiga: S:quicklisp/setup.lisp
#+amigaos (load "S:quicklisp/setup.lisp")
#-amigaos (load (merge-pathnames "quicklisp/setup.lisp" (user-homedir-pathname)))

(load "lib/quicklisp-compat.lisp")

(format t "~%--- Loading fiveam via quicklisp ---~%")
(ql:quickload :fiveam)

(format t "~%--- Running fiveam self-tests ---~%")
(asdf:test-system :fiveam)
