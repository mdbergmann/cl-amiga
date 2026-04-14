;; Load and test str via quicklisp
;; Works on both host and Amiga

(require "asdf")

;; Host: ~/quicklisp/setup.lisp, Amiga: S:quicklisp/setup.lisp
#+amigaos (load "S:quicklisp/setup.lisp")
#-amigaos (load (merge-pathnames "quicklisp/setup.lisp" (user-homedir-pathname)))

(load "lib/quicklisp-compat.lisp")

(format t "~%--- Loading str via quicklisp ---~%")
(ql:quickload :str)

(format t "~%--- Running str tests ---~%")
(asdf:test-system :str)
