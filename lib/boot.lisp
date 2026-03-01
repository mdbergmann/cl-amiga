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

;; prog1 / prog2
(defmacro prog1 (first &rest body) (let ((g (gensym))) `(let ((,g ,first)) ,@body ,g)))
(defmacro prog2 (first second &rest body) (let ((g (gensym))) `(progn ,first (let ((,g ,second)) ,@body ,g))))

;; setf modify macros
(defmacro push (item place) `(setf ,place (cons ,item ,place)))
(defmacro pop (place) (let ((g (gensym))) `(let ((,g (car ,place))) (setf ,place (cdr ,place)) ,g)))
(defmacro incf (place &optional (delta 1)) `(setf ,place (+ ,place ,delta)))
(defmacro decf (place &optional (delta 1)) `(setf ,place (- ,place ,delta)))

;; List searching
(defun member (item list &key (test #'eql))
  (do ((l list (cdr l)))
      ((null l) nil)
    (when (funcall test item (car l))
      (return-from member l))))

;; pushnew — push only if not already a member
(defmacro pushnew (item place &key test)
  (let ((g (gensym)))
    (if test
        `(let ((,g ,item))
           (unless (member ,g ,place :test ,test)
             (setf ,place (cons ,g ,place)))
           ,place)
        `(let ((,g ,item))
           (unless (member ,g ,place)
             (setf ,place (cons ,g ,place)))
           ,place))))

;; Set operations
(defun intersection (list1 list2 &key (test #'eql))
  (let ((result nil))
    (dolist (x list1 (nreverse result))
      (when (member x list2 :test test)
        (push x result)))))

(defun union (list1 list2 &key (test #'eql))
  (let ((result (copy-list list1)))
    (dolist (x list2 (nreverse result))
      (unless (member x list1 :test test)
        (push x result)))))

(defun set-difference (list1 list2 &key (test #'eql))
  (let ((result nil))
    (dolist (x list1 (nreverse result))
      (unless (member x list2 :test test)
        (push x result)))))

(defun subsetp (list1 list2 &key (test #'eql))
  (dolist (x list1 t)
    (unless (member x list2 :test test)
      (return-from subsetp nil))))
