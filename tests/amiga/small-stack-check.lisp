;;;; small-stack-check.lisp -- the main-stack leg of the Amiga suite.
;;;;
;;;; call-on-ustartup runs this after `stack 8000`: the runtime must give
;;;; itself the 128K it needs (platform_run_main's StackSwap) instead of
;;;; overflowing or stopping at the C-stack guard.  The work below is what
;;;; a small stack breaks first: the boot already happened by now, so read
;;;; and compile a deeply nested form, and recurse a few hundred frames.
;;;; 24 levels is about 50K of reader frames (~2K each): far beyond the
;;;; 8000 bytes given, well inside the 128K taken (60 levels overran it).

(defun deep (n) (if (= n 0) 0 (1+ (deep (1- n)))))

(let* ((depth 24)
       (text (with-output-to-string (s)
               (dotimes (i depth) (write-string "(list " s))
               (write-string "1" s)
               (dotimes (i depth) (write-string ")" s))))
       (form (read-from-string text))
       (fn (compile nil `(lambda () ,form)))
       (nested (funcall fn)))
  (format t "SMALL-STACK ~D nested lists, deep ~D~%" depth (deep 500))
  (format t "~A~%" (if (and (consp nested) (= (deep 500) 500))
                       "SMALL-STACK-PASSED"
                       "SMALL-STACK-FAILED")))
