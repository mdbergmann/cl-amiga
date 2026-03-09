;; Load and test fiveam via quicklisp
;; Works on both host and Amiga

(require "asdf")

;; Host: ~/quicklisp/setup.lisp, Amiga: local quicklisp/setup.lisp
#+amigaos (load "quicklisp/setup.lisp")
#-amigaos (load (merge-pathnames "quicklisp/setup.lisp" (user-homedir-pathname)))

#+amigaos (load "lib/quicklisp-compat.lisp")

(format t "~%--- Loading fiveam via quicklisp ---~%")
(ql:quickload :fiveam)

(format t "~%--- Running fiveam self-tests ---~%")
(asdf:test-system :fiveam)
