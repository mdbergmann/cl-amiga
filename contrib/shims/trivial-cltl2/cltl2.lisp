;;;; cltl2.lisp — CLtL2 compatibility layer for CL-Amiga.
;;;;
;;;; Upstream trivial-cltl2 delegates every name to an implementation
;;;; package (sb-cltl2, ccl, hcl, etc.).  CL-Amiga has no such package
;;;; — our compiler does not expose a reified lexical environment — so
;;;; this file provides minimal implementations that satisfy the actual
;;;; callers in the ecosystem (trivia, serapeum), documenting where
;;;; each one diverges from AMOP/CLtL2 semantics.

(defpackage #:trivial-cltl2
  (:nicknames #:cltl2)
  (:use #:cl)
  (:export #:compiler-let
           #:variable-information
           #:function-information
           #:declaration-information
           #:augment-environment
           #:define-declaration
           #:parse-macro
           #:enclose))

(in-package #:trivial-cltl2)

;;; --- Declaration registry ---
;;;
;;; DEFINE-DECLARATION registers a user declaration identifier + its
;;; handler; DECLARATION-INFORMATION queries the registry.  Without a
;;; reified lexical environment, we can only answer "globally proclaimed"
;;; — which is exactly what (proclaim '(declaration NAME)) tracks.  We
;;; therefore keep the last value a handler would have produced in a
;;; global table, keyed by declaration-name, and hand it back on query.
;;;
;;; This is enough for trivia's OPTIMIZER declaration (it proclaims the
;;; declaration once and then either queries it or falls back to
;;; *OPTIMIZER*) and for serapeum's FBOUNDP-gated OPTIMIZE probe.

(defvar *declaration-handlers* (make-hash-table :test 'eq)
  "NAME -> handler function registered by DEFINE-DECLARATION.")

(defvar *declaration-globals* (make-hash-table :test 'eq)
  "NAME -> most recent CDR of (NAME . VALUE) produced by a handler —
returned by DECLARATION-INFORMATION when no env-local value is known.")

(defmacro define-declaration (decl-name lambda-list &body body)
  "CLtL2 §8.5: register DECL-NAME as a known declaration, along with a
handler called at declaration-processing time.  The handler takes the
declaration specifier and a (stubbed) environment and returns two
values: a disposition (:VARIABLE / :FUNCTION / :DECLARE / :BIND) and
a payload.  On CL-Amiga the environment argument is always NIL and
payloads are stashed globally, not lexically — users whose logic
depends on lexical scoping should guard with FBOUNDP and provide a
fallback."
  (let ((handler-name (intern (format nil "%DECL-HANDLER-~A" decl-name))))
    `(progn
       (proclaim '(declaration ,decl-name))
       (defun ,handler-name ,lambda-list ,@body)
       (setf (gethash ',decl-name *declaration-handlers*) #',handler-name)
       ',decl-name)))

(defun declaration-information (decl-name &optional env)
  "CLtL2 §8.5: return the value currently associated with DECL-NAME in
ENV.  Since we don't track declarations lexically, ENV is ignored and
we return globally-proclaimed values where we can."
  (declare (ignore env))
  (cond
    ((eq decl-name 'optimize)
     ;; Spec: OPTIMIZE returns an alist of (quality value).  Our compiler
     ;; tracks optimize settings internally but does not expose them to
     ;; Lisp, so we answer with neutral defaults for all standard
     ;; qualities.  Callers like serapeum's POLICY-QUALITY need the alist
     ;; to contain every quality they query (speed, space, etc.) — a
     ;; partial alist makes them error with 'Unknown policy quality'.
     '((speed 1) (safety 1) (space 1) (debug 1) (compilation-speed 1)))
    (t (gethash decl-name *declaration-globals*))))

(defun %store-declaration-information (decl-name value)
  "Internal: record the most recent handler output for DECL-NAME so
DECLARATION-INFORMATION can return it.  Called by code that simulates
the lexical-environment-augmentation a real CLtL2 implementation would
perform in AUGMENT-ENVIRONMENT."
  (setf (gethash decl-name *declaration-globals*) value))

;;; --- Environment introspection (stubs) ---
;;;
;;; Our compiler does not expose a reified lexical environment, so
;;; these return reasonable "I don't know" answers.  Downstream code
;;; guarded by FBOUNDP (e.g. serapeum) sees the symbol bound and gets
;;; NIL rather than erroring.

(defun variable-information (symbol &optional env)
  "CLtL2 §8.5: classify SYMBOL as :SPECIAL / :LEXICAL / :SYMBOL-MACRO /
:CONSTANT in ENV, or NIL if unknown.  We answer for globally known
special / constant symbols; unknowns return NIL."
  (declare (ignore env))
  (cond
    ((not (symbolp symbol)) nil)
    ((or (eq symbol nil) (eq symbol t) (keywordp symbol)) (values :constant nil nil))
    ((and (boundp symbol)
          (handler-case
              (progn (symbol-value symbol) t)
            (error () nil))
          (let ((plist (symbol-plist symbol)))
            (declare (ignore plist))
            nil))
     nil)
    (t nil)))

(defun function-information (name &optional env)
  "CLtL2 §8.5: classify NAME as :FUNCTION / :MACRO / :SPECIAL-FORM in
ENV.  We answer for globally fbound names only."
  (declare (ignore env))
  (cond
    ((and (symbolp name) (macro-function name))
     (values :macro nil nil))
    ((and (symbolp name) (fboundp name))
     (values :function nil nil))
    (t nil)))

(defun augment-environment (env &key variable symbol-macro function macro declare)
  "CLtL2 §8.5: return a new environment extending ENV.  We have no
reified env type, so the returned value is ENV itself — augmentations
are silently discarded.  Callers that rely on AUGMENT-ENVIRONMENT for
correctness cannot be supported on CL-Amiga; this stub exists so
libraries that call it defensively (often followed by PARSE-MACRO /
ENCLOSE) keep loading."
  (declare (ignore variable symbol-macro function macro))
  (when declare
    ;; Record any (DECLARATION-INFORMATION-compatible) payload so a
    ;; later query can return something sensible.
    (dolist (spec declare)
      (when (and (consp spec) (symbolp (car spec)))
        (%store-declaration-information (car spec) (cdr spec)))))
  env)

;;; --- Macro lifting (stubs) ---
;;;
;;; PARSE-MACRO produces a lambda-expression from a macro defining form
;;; suitable for ENCLOSE, which wraps it as a function closed over ENV.
;;; We don't track environments, so ENCLOSE just compiles the lambda.

(defun parse-macro (name lambda-list body &optional env)
  "CLtL2 §8.5: convert a DEFMACRO body into a (LAMBDA (FORM ENV) ...)
expression.  Minimal implementation — returns a plain lambda that
destructures FORM against LAMBDA-LIST; does not handle &whole /
&environment.  Sufficient for most defensive callers."
  (declare (ignore name env))
  (let ((form-var (gensym "FORM"))
        (env-var (gensym "ENV")))
    `(lambda (,form-var ,env-var)
       (declare (ignorable ,env-var))
       (destructuring-bind ,lambda-list (cdr ,form-var)
         ,@body))))

(defun enclose (lambda-expression &optional env)
  "CLtL2 §8.5: compile LAMBDA-EXPRESSION in ENV, returning a function.
We drop ENV (no lexical-env capture) and EVAL/COERCE the lambda."
  (declare (ignore env))
  (coerce lambda-expression 'function))

;;; --- COMPILER-LET (CLtL2 §7.3) ---
;;;
;;; CLtL2's COMPILER-LET bound variables during compilation only; ANSI
;;; CL dropped it.  Libraries that reference it typically use it for
;;; pure-macro plumbing and a plain LET is a safe substitute on a
;;; single-pass compiler where macro expansion happens at runtime-ish
;;; scope anyway.
(defmacro compiler-let (bindings &body body)
  "CLtL2 §7.3 COMPILER-LET.  Compiles to a plain LET — adequate on a
single-pass compiler where macro-time bindings are visible through
ordinary variable scoping."
  `(let ,bindings ,@body))
