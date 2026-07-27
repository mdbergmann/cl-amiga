; MorphOS hang repro — the FASL compile-file/load section of
; tests/amiga/run-tests.lisp in isolation, with a progress marker around
; every sub-step.
;
; Background: on MorphOS the full suite hangs after "PASS: LTV eq stable"
; (run-tests.lisp:7156) with the main task stuck in state WAIT — the next
; observable output would have been "; Loading T:fasl-ltv3.fasl".  The
; hang window is therefore: writing T:fasl-ltv3.lisp, compile-file-ing
; it, or the front of the subsequent load.  Same place on every run,
; independent of heap (16M/32M) and 68k stack (16K/128K) size.
;
; Run in a FRESH clamiga session on MorphOS:
;
;     clamiga --load tests/morphos-ltv-hang-repro.lisp
;
; Outcome A — hangs at the same step: the bug is local to this sequence;
;   the last "==>" marker names the exact blocking operation.
; Outcome B — runs to "=== repro complete ===": the hang needs the
;   cumulative state of the 7000-line suite; next step is bisecting the
;   suite prefix.

(setq *pass-count* 0)
(setq *fail-count* 0)
(defmacro check (name expected actual)
  (let ((e (gensym "EXPECTED"))
        (a (gensym "ACTUAL"))
        (c (gensym "COND")))
    `(handler-case
         (let ((,e ,expected)
               (,a ,actual))
           (if (equal ,e ,a)
               (progn (setq *pass-count* (+ *pass-count* 1))
                      (format t "PASS: ~A~%" ,name))
               (progn (setq *fail-count* (+ *fail-count* 1))
                      (format t "FAIL: ~A - expected ~S got ~S~%"
                              ,name ,e ,a))))
       (error (,c)
         (setq *fail-count* (+ *fail-count* 1))
         (format t "FAIL: ~A - signaled error: ~A~%" ,name ,c)))))

(defun mark (s) (format t "==> ~A~%" s) (finish-output))

; --- FASL compile-file and load (run-tests.lisp lines ~7070-7186) ---

(mark "write fasl-test1")
(with-open-file (s "T:fasl-test1.lisp" :direction :output :if-exists :supersede)
  (write-string "(defun fasl-fn-1 () 41)" s) (terpri s))
(mark "compile fasl-test1")
(compile-file "T:fasl-test1.lisp" :output-file "T:fasl-test1.fasl")
(check "compile-file creates FASL" t (not (null (probe-file "T:fasl-test1.fasl"))))
(mark "load fasl-test1")
(load "T:fasl-test1.fasl")
(check "FASL function works" 41 (fasl-fn-1))

(mark "recompile fasl-test1 (return value)")
(check "compile-file return type" t
  (pathnamep (compile-file "T:fasl-test1.lisp" :output-file "T:fasl-test1.fasl")))

(mark "write fasl-circ")
(with-open-file (s "T:fasl-circ.lisp" :direction :output :if-exists :supersede)
  (write-string "(defun fasl-circ-fn () '#1=(1 2 3 . #1#))" s) (terpri s))
(mark "compile fasl-circ")
(compile-file "T:fasl-circ.lisp" :output-file "T:fasl-circ.fasl")
(mark "load fasl-circ")
(load "T:fasl-circ.fasl")
(check "FASL circular constant loops back" t
  (let ((c (fasl-circ-fn))) (eq c (nthcdr 3 c))))

(mark "write fasl-share")
(with-open-file (s "T:fasl-share.lisp" :direction :output :if-exists :supersede)
  (write-string "(defun fasl-share-fn () '(#2=(9 9) 1 . #2#))" s) (terpri s))
(mark "compile fasl-share")
(compile-file "T:fasl-share.lisp" :output-file "T:fasl-share.fasl")
(mark "load fasl-share")
(load "T:fasl-share.fasl")
(check "FASL shared sublist preserved" t
  (let ((c (fasl-share-fn))) (eq (car c) (cddr c))))

; --- LOAD-TIME-VALUE under COMPILE-FILE ---

(mark "write fasl-ltv1")
(with-open-file (s "T:fasl-ltv1.lisp" :direction :output :if-exists :supersede)
  (write-string "(defun ltv-gen-fn () (list 4 5 6))" s) (terpri s)
  (write-string "(defun ltv-get-fn () (load-time-value (ltv-gen-fn)))" s) (terpri s))
(mark "compile fasl-ltv1")
(compile-file "T:fasl-ltv1.lisp" :output-file "T:fasl-ltv1.fasl")
(mark "load fasl-ltv1")
(load "T:fasl-ltv1.fasl")
(check "LTV forward function" '(4 5 6) (ltv-get-fn))

(mark "write fasl-ltv2")
(with-open-file (s "T:fasl-ltv2.lisp" :direction :output :if-exists :supersede)
  (write-string "(defun ltv-eq-fn () (load-time-value (cons 1 2)))" s) (terpri s))
(mark "compile fasl-ltv2")
(compile-file "T:fasl-ltv2.lisp" :output-file "T:fasl-ltv2.fasl")
(mark "load fasl-ltv2")
(load "T:fasl-ltv2.fasl")
(check "LTV eq stable" t (eq (ltv-eq-fn) (ltv-eq-fn)))

; === the full suite hangs between here and the ltv3 "; Loading" line ===

(mark "write fasl-ltv3 (open)")
(with-open-file (s "T:fasl-ltv3.lisp" :direction :output :if-exists :supersede)
  (mark "write fasl-ltv3 (line 1)")
  (write-string "(defun ltv-d1 () (load-time-value (cons 'x 'y)))" s) (terpri s)
  (mark "write fasl-ltv3 (line 2)")
  (write-string "(defun ltv-d2 () (load-time-value (cons 'x 'y)))" s) (terpri s)
  (mark "write fasl-ltv3 (close)"))
(mark "compile fasl-ltv3")
(compile-file "T:fasl-ltv3.lisp" :output-file "T:fasl-ltv3.fasl")
(mark "load fasl-ltv3")
(load "T:fasl-ltv3.fasl")
(check "LTV distinct forms" nil (eq (ltv-d1) (ltv-d2)))

; --- keep going past the hang point: MAKE-LOAD-FORM ---

(mark "write fasl-mlf")
(with-open-file (s "T:fasl-mlf.lisp" :direction :output :if-exists :supersede)
  (write-string "(defpackage :mlf-ami (:use :cl))" s) (terpri s)
  (write-string "(in-package :mlf-ami)" s) (terpri s)
  (write-string "(defclass mlfnode () ((label :initarg :label :accessor mlf-label) (n :initarg :n :accessor mlf-n) (self :accessor mlf-self)))" s) (terpri s)
  (write-string "(defmethod make-load-form ((x mlfnode) &optional env) (declare (ignore env)) (make-load-form-saving-slots x))" s) (terpri s)
  (write-string "(defvar *mlf* #.(let ((x (make-instance 'mlfnode :label \"hi\" :n 7))) (setf (mlf-self x) x) x))" s) (terpri s)
  (write-string "(in-package :cl-user)" s) (terpri s))
(mark "compile fasl-mlf")
(compile-file "T:fasl-mlf.lisp" :output-file "T:fasl-mlf.fasl")
(mark "load fasl-mlf")
(load "T:fasl-mlf.fasl")
(check "make-load-form slot values + circular self-ref" '("hi" 7 t)
  (let ((x (symbol-value (find-symbol "*MLF*" "MLF-AMI"))))
    (list (funcall (find-symbol "MLF-LABEL" "MLF-AMI") x)
          (funcall (find-symbol "MLF-N" "MLF-AMI") x)
          (if (eq x (funcall (find-symbol "MLF-SELF" "MLF-AMI") x)) t nil))))

(format t "~%=== repro complete: ~D passed, ~D failed ===~%"
        *pass-count* *fail-count*)
