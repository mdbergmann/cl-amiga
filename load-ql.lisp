(require "asdf")
(format t "~%ASDF loaded.~%")
(load "quicklisp.lisp")
(format t "~%Quicklisp loaded, attempting install...~%")
;; Override *home* directly — the (:path *home*) keyword default bug
;; causes *home* to be NIL inside install when not passed explicitly
(setf quicklisp-quickstart::*home*
      (if (member :amigaos *features*)
          (pathname "S:quicklisp/")
          (merge-pathnames (make-pathname :directory '(:relative "quicklisp"))
                           (user-homedir-pathname))))
(handler-case
  (quicklisp-quickstart:install)
  (error (e)
    (format t "~%ERROR: ~A~%" e)))
(format t "~%Done.~%")
