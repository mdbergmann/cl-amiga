;;; boot.lisp — Bootstrap macros and functions for CL-Amiga
;;; Loaded at startup to provide common CL conveniences.

;;; NOTE: Macros (defmacro) require runtime macro expansion support,
;;; which is not yet fully implemented. These are provided as
;;; templates for when that support is added.

;;; For now, these functions work immediately:

;; (defun cadr (x) (car (cdr x)))
;; (defun caar (x) (car (car x)))
;; (defun cdar (x) (cdr (car x)))
;; (defun cddr (x) (cdr (cdr x)))
;; (defun caddr (x) (car (cdr (cdr x))))

;; (defun identity (x) x)
;; (defun constantly (x) (lambda (&rest args) x))

;;; Macros (for future use):
;; (defmacro when (test &rest body) (list 'if test (cons 'progn body)))
;; (defmacro unless (test &rest body) (list 'if test nil (cons 'progn body)))
