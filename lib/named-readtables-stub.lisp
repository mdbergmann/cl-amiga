;; named-readtables-stub.lisp — minimal named-readtables for CL-Amiga
;; Provides defreadtable and in-readtable macros needed by fset.

(unless (find-package :named-readtables)
  (defpackage :named-readtables
    (:use :cl)
    (:export #:defreadtable #:in-readtable)))

(in-package :named-readtables)

;; Registry of named readtables
(defvar *named-readtables* (make-hash-table :test 'eq))
(setf (gethash :standard *named-readtables*) (copy-readtable nil))

(defmacro defreadtable (name &body options)
  "Define a named readtable. Supports :merge, :dispatch-macro-char, :macro-char."
  (let ((rt (gensym "RT")))
    `(let ((,rt (copy-readtable nil)))
       ,@(loop for opt in options
               when (and (listp opt) (eq (car opt) :dispatch-macro-char))
               collect `(set-dispatch-macro-character ,(second opt) ,(third opt)
                                                      ,(fourth opt) ,rt))
       ,@(loop for opt in options
               when (and (listp opt) (eq (car opt) :macro-char))
               collect `(set-macro-character ,(second opt) ,(third opt)
                                             ,(fourth opt) ,rt))
       (setf (gethash ',name named-readtables::*named-readtables*) ,rt)
       ',name)))

(defmacro in-readtable (name)
  "Set *readtable* to the named readtable NAME, or standard if :standard."
  `(setf *readtable*
         (or (gethash ',name named-readtables::*named-readtables*)
             (copy-readtable nil))))
