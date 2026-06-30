;;;; default.lisp — CL-Amiga port of introspect-environment's default backend.
;;;;
;;;; Upstream introspect-environment provides working TYPEXPAND/TYPEXPAND-1
;;;; only for implementations that expose a deftype expander (SBCL, ECL,
;;;; ABCL, …); for everything else its "default" backend leaves every type
;;;; unexpanded.  CL-Amiga DOES expose its deftype expander table to Lisp
;;;; via CLAMIGA::%TYPE-EXPANDER, so this fork supplies a real TYPEXPAND
;;;; built on top of it.  This matters for callers such as serapeum's
;;;; EXPLODE-TYPE (control-flow.lisp), which relies on TYPEXPAND to resolve
;;;; a user `deftype' alias to its underlying disjunction.
;;;;
;;;; Everything else is carried over verbatim from the upstream default
;;;; backend (the unsupported-implementation stubs), so this remains a
;;;; faithful drop-in replacement.

(in-package #:introspect-environment)

(defun specialp (name &optional env)
  "This implementation is not supported; this function always returns NIL."
  (declare (ignore name env))
  nil)

(defun variable-type (name &optional env)
  "This implementation is not supported; this function doesn't know how to query an environment for type declaration information, and so always returns T."
  (declare (ignore env))
  (check-type name symbol)
  't)

(defun function-type (name &optional env)
  "This implementation is not supported; this function doesn't know how to query an environment for ftype declaration information, and so always returns (FUNCTION * *)."
  (declare (ignore env))
  (check-type name (or symbol (cons (eql setf) (cons symbol null))) "a function name")
  '(function * *))

(defun constant-form-value (form &optional env)
  "This implementation is not supported; if there is more environment dependence than macroexpansion this function will not work."
  (eval (macroexpand form env)))

(defun policy-quality (quality &optional env)
  "This implementation is not supported; this function doesn't know how to query an environment for optimize declaration information, and so returns 1 for all qualities for all environments."
  (declare (ignore env))
  (unless (member quality '(speed safety space debug compilation-speed))
    (error "Unknown policy quality ~s" quality))
  1)

(defmacro policy (expr &optional env)
  "This implementation is not supported; this macro treats all optimization qualities as being 1 at all times."
  (declare (ignore env))
  `(symbol-macrolet ((speed 1) (safety 1) (space 1) (debug 1) (compilation-speed 1))
     ,expr))

;;; --- Type expansion (CL-Amiga) ---
;;;
;;; CLAMIGA::%TYPE-EXPANDER returns the closure `deftype' registered for a
;;; symbol, or NIL when the symbol names no user deftype.  The closure takes
;;; the compound type's arguments — i.e. the expander for
;;; `(deftype foo (a b) ...)' is applied to (a b); an atom deftype's expander
;;; takes no arguments.  Built-in/primitive type names (INTEGER, STRING, …)
;;; have no expander and are therefore returned unchanged, which is the
;;; behaviour callers expect.

(defun typexpand-1 (type &optional env)
  "Expand the type specifier TYPE by one step if it names a user `deftype',
returning two values: the (possibly) expanded specifier and a boolean that
is true iff an expansion was performed."
  (declare (ignore env))
  (check-type type (or symbol cons) "a type specifier")
  (multiple-value-bind (head args)
      (if (consp type)
          (values (car type) (cdr type))
          (values type nil))
    (if (symbolp head)
        (let ((expander (clamiga::%type-expander head)))
          (if expander
              (values (apply expander args) t)
              (values type nil)))
        (values type nil))))

(defun typexpand (type &optional env)
  "Repeatedly TYPEXPAND-1 the type specifier TYPE until it no longer names a
user `deftype', returning two values: the fully expanded specifier and a
boolean that is true iff any expansion was performed."
  (declare (ignore env))
  (check-type type (or symbol cons) "a type specifier")
  (let ((ever nil))
    (loop
      (multiple-value-bind (new expanded) (typexpand-1 type)
        (if expanded
            (setf type new ever t)
            (return (values type ever)))))))

;;; --- Macro lifting (unsupported-implementation defaults) ---

(defun function-name->block-name (name)
  (if (consp name)
      (second name)
      name))

(defun %parse-macro (name lambda-list body cm-p)
  (check-type name (or symbol (cons (eql setf) (cons symbol null)))
	      "a function name")
  (let ((whole (gensym "WHOLE"))
	(env (gensym "ENV"))
	(rebind-whole nil)
	(rebind-env nil)
	(doc nil))
    (let (res)
      (loop (cond ((and (stringp (first body)) (rest body))
		   (setf doc (first body)
			 body (nconc (nreverse res) (rest body)))
		   (return))
		  ((null body)
		   (setf body (nreverse res))
		   (return))
		  ((and (consp (first body)) (eql (first (first body)) 'declare))
		   (push (first body) res)
		   (setf body (rest body)))
		  (t
		   (setf body (nconc (nreverse res) body))
		   (return)))))
    (when (eql (first lambda-list) '&whole)
      (setf whole (second lambda-list)
	    rebind-whole t
	    lambda-list (cddr lambda-list)))
    (let (res)
      (loop (cond ((atom lambda-list)
		   (setf lambda-list (nconc (nreverse res) lambda-list))
		   (return))
		  ((eql (first lambda-list) '&environment)
		   (setf env (second lambda-list)
			 rebind-env t
			 lambda-list (nconc (nreverse res) (cddr lambda-list)))
		   (return))
		  (t
		   (push (first lambda-list) res)
		   (setf lambda-list (cdr lambda-list))))))
    (when rebind-whole (setf lambda-list (cons whole lambda-list)))
    (when rebind-env (setf lambda-list (cons env lambda-list)))
    `(lambda (,whole ,env)
       ,@(when doc (list doc))
       (declare (ignorable ,whole ,env))
       (block ,(function-name->block-name name)
	 (destructuring-bind (,@lambda-list)
	     (list* ,@(when rebind-env (list env))
		    ,@(when rebind-whole (list whole))
		    ,(if cm-p
			 `(if (eq (first ,whole) 'funcall) (cddr ,whole) (cdr ,whole))
			 `(cdr ,whole)))
	   ,@body)))))

(defun parse-macro (name lambda-list body &optional env)
  "This implementation is not supported; this function works as defined, but performs minimal error checking."
  (declare (ignore env))
  (%parse-macro name lambda-list body nil))

(defun parse-compiler-macro (name lambda-list body &optional env)
  "This implementation is not supported; this function works as defined, but performs minimal error checking."
  (declare (ignore env))
  (%parse-macro name lambda-list body t))
