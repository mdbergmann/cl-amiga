;;; boot.lisp — Bootstrap functions for CL-Amiga
;;; Loaded at startup to provide common CL conveniences.

;; Composite CAR/CDR accessors
(defun cadr (x) (car (cdr x)))
(defun caar (x) (car (car x)))
(defun cdar (x) (cdr (car x)))
(defun cddr (x) (cdr (cdr x)))
(defun caddr (x) (car (cdr (cdr x))))
(defun cadar (x) (car (cdr (car x))))

;; Utility functions
(defun identity (x) x)
(defun endp (x) (null x))
