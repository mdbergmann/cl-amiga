;;; test_amiga_asyncio.lisp — host-side checks of lib/amiga/asyncio.lisp
;;; (AMIGA.ASYNCIO), driven by tests/test_amiga_asyncio.sh.
;;;
;;; DOS packets need a filesystem handler process, so the module's real
;;; work only runs on AmigaOS/MorphOS (tests/amiga/test-asyncio.lisp is
;;; the functional specification, run by the FS-UAE suite).  What the
;;; host pins down: the module compiles and loads, AVAILABLE-P answers
;;; NIL, every entry point refuses cleanly instead of crashing into
;;; NULL library bases, and the example loads and bows out.

(defvar *pass* 0)
(defvar *fail* 0)

(defmacro check (name expected form)
  `(handler-case
       (let ((e ,expected) (a ,form))
         (if (equal e a)
             (progn (incf *pass*) (format t "PASS: ~A~%" ,name))
             (progn (incf *fail*) (format t "FAIL: ~A - expected ~S got ~S~%" ,name e a))))
     (error (c)
       (incf *fail*)
       (format t "FAIL: ~A - signaled error: ~A~%" ,name c))))

(require "amiga/asyncio")

(check "host-asyncio-not-available" nil (amiga.asyncio:available-p))

(check "host-asyncio-open-refuses" t
  (handler-case (progn (amiga.asyncio:open-async "T:x" :read) nil)
    (error (e) (and (search "AmigaOS" (format nil "~A" e)) t))))

(check "host-asyncio-open-checks-mode" t
  (handler-case (progn (amiga.asyncio:open-async "T:x" :sideways) nil)
    (error () t)))

;; The accessors on a non-file complain about the type, not about NIL.
(check "host-asyncio-type-errors" '(t t)
  (list (handler-case (progn (amiga.asyncio:read-byte-async 42) nil)
          (error (e) (and (search "ASYNC-FILE" (format nil "~A" e)) t)))
        (handler-case (progn (amiga.asyncio:close-async nil) nil)
          (error (e) (and (search "ASYNC-FILE" (format nil "~A" e)) t)))))

;; The example loads (all of its code compiles) and bows out.
(check "example-copyfile-loads-and-bows-out" t
  (let ((output (with-output-to-string (*standard-output*)
                  (load "examples/amiga/asyncio/copyfile.lisp"))))
    (and (search "not available" output) t)))

(format t "~%~D passed, ~D failed~%" *pass* *fail*)
(if (zerop *fail*)
    (format t "ALL ASYNCIO HOST CHECKS PASSED~%")
    (format t "SOME ASYNCIO HOST CHECKS FAILED~%"))
