(require "asdf")
(format t "~%ASDF loaded.~%")
(load "quicklisp.lisp")
(format t "~%Quicklisp loaded, attempting install...~%")
;; Set install path explicitly
(setf quicklisp-quickstart::*home*
      (if (member :amigaos *features*)
          (pathname "quicklisp/")   ;; relative to current dir (CLAmiga:)
          (merge-pathnames (make-pathname :directory '(:relative "quicklisp"))
                           (user-homedir-pathname))))
(handler-case
  (quicklisp-quickstart:install)
  (error (e)
    (format t "~%ERROR: ~A~%" e)))
(format t "~%Done.~%")
