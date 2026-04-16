;; Load and test log4cl via quicklisp
;; Usage: ./build/host/clamiga --heap 64M --load trunk/load-log4cl.lisp

(require "asdf")

;; Host: ~/quicklisp/setup.lisp
#+amigaos (load "S:quicklisp/setup.lisp")
#-amigaos (load (merge-pathnames "quicklisp/setup.lisp" (user-homedir-pathname)))

(load "lib/quicklisp-compat.lisp")

(format t "~%--- Loading log4cl via quicklisp ---~%")
(ql:quickload :log4cl)
(format t "--- log4cl loaded successfully ---~%")

;; Test basic logging at different levels
(format t "~%--- Testing log4cl ---~%")
(log:info "Hello from CL-Amiga!")
(log:warn "This is a warning")
(log:error "This is an error message")
(log:config :info)
(log:info "After config :info")

(format t "~%--- log4cl tests passed ---~%")
(quit)
