;; fset-compat.lisp — compatibility stubs for fset on CL-Amiga
;; Load after fset's defs.lisp has created the FSET package.

(in-package :fset)

;; Single-threaded: no locking needed
(defun make-lock (&optional name)
  (declare (ignore name))
  nil)

(defmacro with-lock ((lock &key (wait? t)) &body body)
  (declare (ignore lock wait?))
  `(progn ,@body))

(defmacro read-memory-barrier ()
  'nil)

(defmacro write-memory-barrier ()
  'nil)

;; deflex — define a lexical-like global variable
;; (Used by fset when defglobal is not available)
(defmacro deflex (name value &optional doc-string)
  (declare (ignore doc-string))
  `(defvar ,name ,value))

;; defglobal — falls back to deflex
(defmacro defglobal (name value &optional doc-string)
  `(deflex ,name ,value ,doc-string))

;; make-char fallback
(defun make-char (code bits)
  (code-char (+ code (ash bits 8))))

;; symbol-hash-value — fset uses this in hash.lisp
(defun symbol-hash-value (sym)
  (sxhash sym))
