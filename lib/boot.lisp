;;; boot.lisp — Bootstrap functions for CL-Amiga
;;; Loaded at startup to provide common CL conveniences.

;; Standard CL variables (set early, before any macro expansion)
(setq *macroexpand-hook* #'funcall)

;; CLHS REPL/condition variables that the C bootstrap interns without a
;; value: / // /// hold the value-lists of the three most recent forms,
;; *break-on-signals* gates whether a signalled condition enters the
;; debugger.  (+ ++ +++ - * ** *** are initialised by the C REPL bootstrap;
;; these four are not.)  Give them their standard initial value NIL and
;; make them special so they are BOUNDP and dynamically bindable
;; (ANSI cl-variable-symbols.1).
(proclaim '(special / // /// *break-on-signals*))
(setq / nil // nil /// nil *break-on-signals* nil)

;; Composite CAR/CDR accessors (all 28 from CL spec)
(defun cadr (x) (car (cdr x)))
(defun caar (x) (car (car x)))
(defun cdar (x) (cdr (car x)))
(defun cddr (x) (cdr (cdr x)))
(defun caaar (x) (car (car (car x))))
(defun cdaar (x) (cdr (car (car x))))
(defun cddar (x) (cdr (cdr (car x))))
(defun caddr (x) (car (cdr (cdr x))))
(defun cadar (x) (car (cdr (car x))))
(defun caadr (x) (car (car (cdr x))))
(defun cdadr (x) (cdr (car (cdr x))))
(defun cdddr (x) (cdr (cdr (cdr x))))
(defun caaaar (x) (car (car (car (car x)))))
(defun cadaar (x) (car (cdr (car (car x)))))
(defun caddar (x) (car (cdr (cdr (car x)))))
(defun cadddr (x) (car (cdr (cdr (cdr x)))))
(defun cdaaar (x) (cdr (car (car (car x)))))
(defun cddaar (x) (cdr (cdr (car (car x)))))
(defun cdddar (x) (cdr (cdr (cdr (car x)))))
(defun cddddr (x) (cdr (cdr (cdr (cdr x)))))
(defun caadar (x) (car (car (cdr (car x)))))
(defun caaddr (x) (car (car (cdr (cdr x)))))
(defun cdadar (x) (cdr (car (cdr (car x)))))
(defun cdaddr (x) (cdr (car (cdr (cdr x)))))
(defun caaadr (x) (car (car (car (cdr x)))))
(defun cadadr (x) (car (cdr (car (cdr x)))))
(defun cdaadr (x) (cdr (car (car (cdr x)))))
(defun cddadr (x) (cdr (cdr (car (cdr x)))))

;; Utility functions
(defun identity (x) x)
(defun endp (x)
  (cond ((null x) t)
        ((consp x) nil)
        (t (error 'type-error :datum x :expected-type 'list))))

;; prog1 / prog2
(defmacro prog1 (first &rest body) (let ((g (gensym))) `(let ((,g ,first)) ,@body ,g)))
(defmacro prog2 (first second &rest body) (let ((g (gensym))) `(progn ,first (let ((,g ,second)) ,@body ,g))))

;; ----------------------------------------------------------------------
;; Real macro expansions for the control-flow operators that the compiler
;; ALSO inlines as special forms (compile_cond / compile_and / compile_or /
;; compile_case / compile_typecase).  The C bootstrap registers an *identity*
;; macro stub for each so that MACRO-FUNCTION and FBOUNDP return non-NIL; but
;; that stub makes (macroexpand-1 '(cond ...)) return the form UNCHANGED.
;;
;; Code walkers (e.g. iterate's clause walker) treat "macro-function is
;; non-NIL but macroexpand makes no progress" as "a macro that won't expand"
;; and refuse to descend into it — so iterate clauses such as COLLECT hidden
;; inside a COND/CASE were never seen, and reached run time as undefined
;; function calls.  Per the HyperSpec these operators ARE macros, so we give
;; them genuine expansions here.  The compiler dispatches them by symbol
;; BEFORE consulting the macro table (see compile_expr), so these definitions
;; never change how the forms compile — they only make MACROEXPAND conformant.
;; These defmacros shadow the C identity stubs (macro table is prepend-wins).
(defmacro cond (&rest clauses)
  (if (null clauses)
      nil
      (let* ((clause (car clauses))
             (test (car clause))
             (forms (cdr clause)))
        (cond ((eq test t) `(progn ,@forms))
              ((null forms)
               (let ((g (gensym)))
                 `(let ((,g ,test)) (if ,g ,g (cond ,@(cdr clauses))))))
              (t `(if ,test (progn ,@forms) (cond ,@(cdr clauses))))))))

(defmacro and (&rest forms)
  (cond ((null forms) t)
        ((null (cdr forms)) (car forms))
        (t `(if ,(car forms) (and ,@(cdr forms))))))

(defmacro or (&rest forms)
  (cond ((null forms) nil)
        ((null (cdr forms)) (car forms))
        (t (let ((g (gensym)))
             `(let ((,g ,(car forms))) (if ,g ,g (or ,@(cdr forms))))))))

;; The clause bodies below are wrapped in (progn ...) so a matched clause
;; with an EMPTY body yields NIL — (cond (test)) would otherwise return the
;; test value, which is the wrong CASE/TYPECASE semantics (CLHS 5.3).
(defmacro case (keyform &rest clauses)
  (let ((g (gensym)))
    `(let ((,g ,keyform))
       (cond ,@(mapcar
                (lambda (clause)
                  (let ((keys (car clause)) (body (cdr clause)))
                    (cond ((or (eq keys t) (eq keys 'otherwise)) `(t (progn ,@body)))
                          ((listp keys) `((member ,g ',keys) (progn ,@body)))
                          (t `((eql ,g ',keys) (progn ,@body))))))
                clauses)))))

(defmacro ecase (keyform &rest clauses)
  ;; In ECASE the symbols T and OTHERWISE are NOT special — they are literal
  ;; keys (CLHS 5.3) — so every clause is matched, then a fall-through error.
  (let ((g (gensym)))
    `(let ((,g ,keyform))
       (cond ,@(mapcar
                (lambda (clause)
                  (let ((keys (car clause)) (body (cdr clause)))
                    (if (listp keys) `((member ,g ',keys) (progn ,@body))
                        `((eql ,g ',keys) (progn ,@body)))))
                clauses)
             (t (error 'type-error :datum ,g :expected-type 'symbol))))))

(defmacro typecase (keyform &rest clauses)
  (let ((g (gensym)))
    `(let ((,g ,keyform))
       (cond ,@(mapcar
                (lambda (clause)
                  (let ((type (car clause)) (body (cdr clause)))
                    (if (or (eq type t) (eq type 'otherwise)) `(t (progn ,@body))
                        `((typep ,g ',type) (progn ,@body)))))
                clauses)))))

(defmacro etypecase (keyform &rest clauses)
  (let ((g (gensym)))
    `(let ((,g ,keyform))
       (cond ,@(mapcar
                (lambda (clause)
                  `((typep ,g ',(car clause)) (progn ,@(cdr clause))))
                clauses)
             (t (error 'type-error :datum ,g :expected-type t))))))

;; multiple-value-setq — assign multiple values to variables
(defmacro multiple-value-setq (vars form)
  (let ((tmps (mapcar (lambda (v) (declare (ignore v)) (gensym)) vars)))
    `(multiple-value-bind ,tmps ,form
       ,@(mapcar (lambda (var tmp) `(setq ,var ,tmp)) vars tmps)
       ,(car tmps))))

;; prog — block nil + let + tagbody
(defmacro prog (bindings &body body)
  `(block nil (let ,bindings (tagbody ,@body))))
(defmacro prog* (bindings &body body)
  `(block nil (let* ,bindings (tagbody ,@body))))

;; constantp — returns T for self-evaluating forms and (quote ...) forms
(defun constantp (form &optional environment)
  (declare (ignore environment))
  (cond ((null form) t)              ; NIL
        ((eq form t) t)              ; T
        ((keywordp form) t)          ; keywords
        ((symbolp form)              ; check defconstant flag
         (%symbol-constant-p form))
        ((consp form)                ; (quote ...) is constant
         (eq (car form) 'quote))
        (t t)))                      ; numbers, chars, strings, etc.

;; setf modify macros — evaluate all subforms left-to-right exactly once
;; per CLHS 5.1.1.1.1.  For non-symbol places we bind each subform of the
;; place to a temp, then reuse those temps in both the SETF target and
;; the value form so that the place's subforms are seen only once.
;; Operators where (setf (op subforms) val) mutates one of the subforms in
;; place rather than rewriting outward to (setf <subform> ...).  For these,
;; we can safely bind the subforms to gensyms once and reuse them in both
;; the SETF target and the value form.  Operators NOT in this set (getf,
;; ldb, mask-field, etc.) propagate the assignment to their first argument
;; via SETF expansion, so subform-binding would lose the update — we must
;; let SETF see the original PLACE.
(defparameter %place-direct-mutators
  '(car cdr first second third fourth fifth sixth seventh eighth ninth tenth
    rest nth caar cadr cdar cddr caaar caadr cadar caddr cdaar cdadr cddar
    cdddr caaaar caaadr caadar caaddr cadaar cadadr caddar cadddr cdaaar
    cdaadr cdadar cdaddr cddaar cddadr cdddar cddddr aref svref elt char
    schar bit sbit gethash get slot-value row-major-aref symbol-value
    symbol-function fdefinition))

(defun %place-direct-mutator-p (op)
  (and (symbolp op) (member op %place-direct-mutators :test #'eq)))

(defmacro push (item place)
  (cond
    ((symbolp place)
     (let ((g (gensym "ITEM")))
       `(let ((,g ,item)) (setq ,place (cons ,g ,place)))))
    ((and (consp place) (%place-direct-mutator-p (car place)))
     (let* ((op (car place))
            (subs (cdr place))
            (item-t (gensym "ITEM"))
            (sub-ts (mapcar (lambda (x) (declare (ignore x)) (gensym "T"))
                            subs)))
       `(let* ((,item-t ,item)
               ,@(mapcar #'list sub-ts subs))
          (setf (,op ,@sub-ts) (cons ,item-t (,op ,@sub-ts))))))
    (t `(setf ,place (cons ,item ,place)))))

(defmacro pop (place)
  (cond
    ((symbolp place)
     (let ((g (gensym)))
       `(let ((,g (car ,place)))
          (setq ,place (cdr ,place))
          ,g)))
    ((and (consp place) (%place-direct-mutator-p (car place)))
     (let* ((op (car place))
            (subs (cdr place))
            (sub-ts (mapcar (lambda (x) (declare (ignore x)) (gensym "T"))
                            subs))
            (g (gensym "OLD")))
       `(let* (,@(mapcar #'list sub-ts subs))
          (let ((,g (car (,op ,@sub-ts))))
            (setf (,op ,@sub-ts) (cdr (,op ,@sub-ts)))
            ,g))))
    (t (let ((g (gensym)))
         `(let ((,g (car ,place)))
            (setf ,place (cdr ,place))
            ,g)))))

;; CLtS 5.1.3 demands DELTA be evaluated before PLACE is read, so that
;; e.g. (incf x (setf x 1)) returns 2 and leaves x = 2 (not 1) — the
;; setf-of-x is the delta, then the modified x is what we add to.
(defmacro incf (place &optional (delta 1) &environment env)
  (multiple-value-bind (temps vals stores set-form access-form)
      (get-setf-expansion place env)
    (let ((d (gensym "DELTA")) (store (car stores)))
      `(let* (,@(mapcar #'list temps vals)
              (,d ,delta)
              (,store (+ ,access-form ,d)))
         ,set-form
         ,store))))

(defmacro decf (place &optional (delta 1) &environment env)
  (multiple-value-bind (temps vals stores set-form access-form)
      (get-setf-expansion place env)
    (let ((d (gensym "DELTA")) (store (car stores)))
      `(let* (,@(mapcar #'list temps vals)
              (,d ,delta)
              (,store (- ,access-form ,d)))
         ,set-form
         ,store))))

(defmacro remf (place indicator)
  ;; CLHS 5.1.2: remf modifies the place.  %remf returns the new list as
  ;; primary value and T/NIL as second; we setf the place and return the
  ;; second value as a generalized boolean.  Per CLHS 5.1.1.1.1, place
  ;; subforms must be evaluated once before INDICATOR.
  (cond
    ((symbolp place)
     (let ((new (gensym)) (found (gensym)) (ind (gensym)))
       `(let ((,ind ,indicator))
          (multiple-value-bind (,new ,found) (clamiga::%remf ,place ,ind)
            (setq ,place ,new)
            ,found))))
    ((and (consp place) (%place-direct-mutator-p (car place)))
     (let* ((op (car place))
            (subs (cdr place))
            (sub-ts (mapcar (lambda (x) (declare (ignore x)) (gensym "T"))
                            subs))
            (ind (gensym "IND")) (new (gensym)) (found (gensym)))
       `(let* (,@(mapcar #'list sub-ts subs)
               (,ind ,indicator))
          (multiple-value-bind (,new ,found)
              (clamiga::%remf (,op ,@sub-ts) ,ind)
            (setf (,op ,@sub-ts) ,new)
            ,found))))
    (t (let ((new (gensym)) (found (gensym)))
         `(multiple-value-bind (,new ,found) (clamiga::%remf ,place ,indicator)
            (setf ,place ,new)
            ,found)))))
(defsetf elt %setf-elt)
(defsetf readtable-case %setf-readtable-case)

;; rotatef — rotate values among places
(defmacro rotatef (&rest places)
  (if (< (length places) 2)
      nil
      (let ((temps (mapcar (lambda (x) (declare (ignore x)) (gensym)) places)))
        `(let ,(mapcar #'list temps places)
           ,@(mapcar (lambda (p v) `(setf ,p ,v))
                     places
                     (append (cdr temps) (list (car temps))))
           nil))))

;; shiftf — shift values among places, return first
(defmacro shiftf (&rest places-and-newval)
  (let* ((places (butlast places-and-newval))
         (newval (car (last places-and-newval)))
         (temps (mapcar (lambda (x) (declare (ignore x)) (gensym)) places))
         (tval (gensym)))
    `(let (,@(mapcar #'list temps places)
           (,tval ,newval))
       ,@(mapcar (lambda (p v) `(setf ,p ,v))
                 places
                 (append (cdr temps) (list tval)))
       ,(car temps))))

;; define-modify-macro — define a macro that modifies a place
(defmacro define-modify-macro (name lambda-list function &optional documentation)
  (let ((place-var (gensym "PLACE"))
        (has-rest (member '&rest lambda-list))
        (plain-args (remove '&rest (remove '&optional lambda-list))))
    `(defmacro ,name (,place-var ,@lambda-list)
       ,@(when documentation (list documentation))
       (list 'setf ,place-var
             ,(if has-rest
                  `(list* ',function ,place-var ,@plain-args)
                  `(list ',function ,place-var ,@plain-args))))))

;; Compiler macros (CLHS 3.2.2.1).  The expander runs at compile time
;; on the call form; returning the original form (eq) means "decline"
;; and the compiler proceeds with the regular call path.  See
;; cl_get_compiler_macro / compile_call in compiler.c.
;;
;; Lambda list supports &whole (must be first) for the decline pattern and
;; &environment (any position); &environment's var is bound to the macro
;; environment the compiler passes as the expander's 2nd argument.
(defun %dcm-split-whole (ll)
  "Return (cons WHOLE-VAR CLEANED-LL).  WHOLE-VAR is NIL when no &whole."
  (if (and (consp ll) (eq (car ll) '&whole))
      (cons (cadr ll) (cddr ll))
      (cons nil ll)))

;; Remove a single `&environment <var>` pair from anywhere in a
;; compiler-macro lambda list (CLHS 3.4.4: &environment may appear at any
;; position).  Returns (cons ENV-VAR CLEANED-LL); ENV-VAR is NIL when absent.
;; Compared by symbol-name so a wrong-package &ENVIRONMENT is still caught.
;; Without this strip, &environment + its var flow into the inner
;; DESTRUCTURING-BIND as bogus required params → spurious "too few elements"
;; (e.g. alexandria's `(define-compiler-macro of-type (&whole form type
;; &environment env) ...)`).
(defun %dcm-split-env (ll)
  (cond ((not (consp ll)) (cons nil ll))
        ((and (symbolp (car ll))
              (string= (symbol-name (car ll)) "&ENVIRONMENT"))
         (cons (cadr ll) (cddr ll)))
        (t (let ((sub (%dcm-split-env (cdr ll))))
             (cons (car sub) (cons (car ll) (cdr sub)))))))

;; CLHS 3.4.11: the body is enclosed in an implicit block named NAME,
;; same rule as defun (CLHS 3.4.10).  Libraries (e.g. serapeum's fnil)
;; rely on (return-from <name> <form>) to short-circuit expansion.
;;
;; (setf <name>) function-names are accepted but registration is skipped
;; — compile_call only consults compiler-macros by symbol, and compile_setf
;; never does, so a compiler macro on (setf foo) could never fire.  This
;; lets libraries that try to install one (e.g. serapeum's (setf href))
;; load without erroring; the regular (setf <name>) function still works.
;; Block name for setf function-names is the symbol per CLHS 5.3.2.
(defmacro define-compiler-macro (name lambda-list &body body)
  (let* ((setf-p   (and (consp name) (eq (car name) 'setf)))
         (block-name (if setf-p (cadr name) name))
         (form-var (gensym "FORM"))
         (env-var  (gensym "ENV"))
         (env-split (%dcm-split-env lambda-list))
         (user-env (car env-split))
         (split    (%dcm-split-whole (cdr env-split)))
         (whole    (car split))
         (clean    (cdr split))
         (inner    `(destructuring-bind ,clean (cdr ,form-var)
                      (block ,block-name ,@body)))
         (with-whole (if whole `(let ((,whole ,form-var)) ,inner) inner)))
    (if setf-p
        `',name
        `(progn
           (clamiga::%setf-compiler-macro-function
            (lambda (,form-var ,env-var)
              ,@(if user-env
                    ;; Expose the macro environment to the user's
                    ;; &environment var (the compiler passes the env as the
                    ;; expander's 2nd arg).
                    `((let ((,user-env ,env-var))
                        (declare (ignorable ,user-env))
                        ,with-whole))
                    `((declare (ignore ,env-var))
                      ,with-whole)))
            ',name)
           ',name))))

;; define-symbol-macro — stores expansion on the symbol's plist under
;; %SYMBOL-MACRO-EXPANSION; the compiler consults this when compiling a
;; variable reference (see cl_lookup_global_symbol_macro in compiler.c).
(defmacro define-symbol-macro (symbol expansion)
  `(eval-when (:compile-toplevel :load-toplevel :execute)
     (setf (get ',symbol '%symbol-macro-expansion) ',expansion)
     ',symbol))

;; Setf expanders — define-setf-expander registers a 5-values expander
;; per CLHS 5.1.4.  The expander takes the place's argument forms and
;; returns (temps vals stores store-form access-form).  Two parallel
;; tables: %setf-expansion-fns is the 5-values registry consulted by
;; get-setf-expansion; the C-side setf_expander_table holds a single-form
;; wrapper consulted by the compiler.  define-setf-expander populates both
;; so place-subform-once semantics propagate through nested setf forms.
(defparameter clamiga::*setf-expansion-fns* nil)

(defun clamiga::%register-setf-expansion (name fn)
  (let ((existing (assoc name clamiga::*setf-expansion-fns*)))
    (if existing
        (rplacd existing fn)
        (setq clamiga::*setf-expansion-fns*
              (cons (cons name fn) clamiga::*setf-expansion-fns*))))
  name)

(defun clamiga::%get-setf-expansion-fn (name)
  (cdr (assoc name clamiga::*setf-expansion-fns*)))

;; CLHS 5.1.2 GET-SETF-EXPANSION — returns 5 values bracketing a place
;; so that subforms are evaluated exactly once.  CLHS 5.1.2.7: when the
;; place is a macro call, the place is macroexpanded first.  Order of
;; preference after expansion: (1) explicit define-setf-expander
;; registry, (2) defsetf setter, (3) compiler-handled fallback.
(defun get-setf-expansion (place &optional env)
  ;; CLHS 5.1.2.9: an operator's own setf expander takes precedence over
  ;; macroexpansion, even when the operator is ALSO a macro (e.g. FSet's
  ;; @, which is both a macro and a define-setf-expander).  Consult the
  ;; expander registry BEFORE MACROEXPAND — otherwise the place is expanded
  ;; as an ordinary read form and the expander is lost, yielding a bogus
  ;; generic expansion ((setf (progn #:t) ...)).  This mirrors the
  ;; precedence compile_setf_place already applies on the compiler side.
  (let ((head-expander
          (and (consp place) (symbolp (car place))
               (clamiga::%get-setf-expansion-fn (car place)))))
    (when head-expander
      (return-from get-setf-expansion (apply head-expander (cdr place)))))
  (let ((place (macroexpand place env)))
    (cond
      ((symbolp place)
       (let ((store (gensym "NEW")))
         (values nil nil (list store) (list 'setq place store) place)))
      ;; CLHS 5.1.2.4: (the type place) as a place — the place is the
      ;; inner form; the type spec wraps the access form.  Without this
      ;; the default branch would treat TYPE as a value subform and try
      ;; to evaluate it (e.g. unbound-variable on FIXNUM).
      ((and (consp place) (eq (car place) 'the))
       (multiple-value-bind (temps vals stores set-form access-form)
           (get-setf-expansion (third place) env)
         (values temps vals stores set-form
                 (list 'the (second place) access-form))))
      ((and (consp place) (symbolp (car place)))
       (let ((expander (clamiga::%get-setf-expansion-fn (car place))))
         (cond
           (expander
            (apply expander (cdr place)))
           (t
            (let ((temps (mapcar (lambda (x) (declare (ignore x)) (gensym "T"))
                                 (cdr place)))
                  (store (gensym "NEW"))
                  (setter (%get-defsetf-setter (car place))))
              (cond
                (setter
                 ;; defsetf: setter receives place args... new-value
                 (values temps (cdr place) (list store)
                         (append (list* setter temps) (list store))
                         (cons (car place) temps)))
                (t
                 ;; Default: emit (setf (head temps...) store) and let the
                 ;; compiler dispatch — covers AREF, GETHASH, CAR, etc.
                 (values temps (cdr place) (list store)
                         (list 'setf (cons (car place) temps) store)
                         (cons (car place) temps)))))))))
      (t (error "GET-SETF-EXPANSION: cannot expand ~S" place)))))

(defmacro define-setf-expander (access-fn lambda-list &body body)
  ;; Strip &environment from lambda-list, bind it to nil.  The body
  ;; must return 5 values (CLHS 5.1.4); we register it for both
  ;; get-setf-expansion (5-values) and the compiler (single form wrapper).
  (let ((env-pos (position-if (lambda (x) (and (symbolp x)
                                                (string= (symbol-name x) "&ENVIRONMENT")))
                              lambda-list))
        (clean-ll lambda-list)
        (env-var nil))
    (when env-pos
      (setq env-var (nth (1+ env-pos) lambda-list))
      (setq clean-ll (append (subseq lambda-list 0 env-pos)
                             (subseq lambda-list (+ env-pos 2)))))
    `(progn
       (clamiga::%register-setf-expansion
        ',access-fn
        (lambda ,clean-ll
          ,@(if env-var
                `((let ((,env-var nil))
                    (declare (ignorable ,env-var))
                    ,@body))
                body)))
       (clamiga::%register-setf-expander
        ',access-fn
        (lambda (place-form value-form)
          (multiple-value-bind (temps vals stores store-form access-form)
              (apply (clamiga::%get-setf-expansion-fn ',access-fn)
                     (cdr place-form))
            (declare (ignore access-form))
            (let ((bindings (append (mapcar #'list temps vals)
                                    (list (list (car stores) value-form)))))
              (list 'let* bindings store-form)))))
       ',access-fn)))

;; Long-form DEFSETF (CLHS 5.5.5):
;;   (defsetf access-fn defsetf-lambda-list (store-variable*) [decl|doc]* form*)
;; The C compiler (compile_defsetf) rewrites the long form to a call to this
;; helper macro, which expands it into a DEFINE-SETF-EXPANDER.  Semantics: the
;; access-fn's argument subforms become temp gensyms (evaluated once, in
;; order); the defsetf-lambda-list variables are bound to those temps; the
;; store-variables are bound to fresh store gensyms; BODY is then evaluated as
;; a macro body to produce the storing form.  Using a real lambda + APPLY to
;; bind the lambda-list against the temps lets &optional/&key defaults work.
(defmacro clamiga::%defsetf-long (access-fn lambda-list store-vars &body body)
  ;; Strip a trailing &environment var from the access lambda list (CLHS
  ;; permits it); bind it to NIL since the compiler-side expander has no
  ;; environment to thread here.
  (let ((env-pos (position-if (lambda (x)
                                (and (symbolp x)
                                     (string= (symbol-name x) "&ENVIRONMENT")))
                              lambda-list))
        (clean-ll lambda-list)
        (env-var nil)
        (args (gensym "ARGS")))
    (when env-pos
      (setq env-var (nth (1+ env-pos) lambda-list))
      (setq clean-ll (append (subseq lambda-list 0 env-pos)
                             (subseq lambda-list (+ env-pos 2)))))
    `(define-setf-expander ,access-fn (&rest ,args)
       (let* ((temps (mapcar (lambda (a)
                               (declare (ignore a))
                               (gensym "TMP"))
                             ,args))
              ,@(mapcar (lambda (sv) `(,sv (gensym "STORE"))) store-vars)
              (stores (list ,@store-vars)))
         (values
          temps
          ,args
          stores
          (apply (lambda ,clean-ll
                   ,@(if env-var
                         `((let ((,env-var nil))
                             (declare (ignorable ,env-var))
                             ,@body))
                         body))
                 temps)
          (cons ',access-fn temps))))))

;; (setf (ldb bytespec int-place) new-byte) — CLHS 5.1.4 expander.
;; Composes with int-place's own setf expansion so subforms (e.g. of
;; (aref ...) under the LDB) are evaluated exactly once.
(define-setf-expander ldb (bytespec int-place &environment env)
  (multiple-value-bind (temps vals stores set-form access-form)
      (get-setf-expansion int-place env)
    (let ((bs (gensym "BS")) (val (gensym "VAL")) (store (car stores)))
      (values
       (cons bs temps)
       (cons bytespec vals)
       (list val)
       `(let ((,store (dpb ,val ,bs ,access-form)))
          ,set-form
          ,val)
       `(ldb ,bs ,access-form)))))

(define-setf-expander mask-field (bytespec int-place &environment env)
  (multiple-value-bind (temps vals stores set-form access-form)
      (get-setf-expansion int-place env)
    (let ((bs (gensym "BS")) (val (gensym "VAL")) (store (car stores)))
      (values
       (cons bs temps)
       (cons bytespec vals)
       (list val)
       `(let ((,store (deposit-field ,val ,bs ,access-form)))
          ,set-form
          ,val)
       `(mask-field ,bs ,access-form)))))

;; (setf (getf place indicator [default]) new) — CLHS 5.1.2.4 expander.
;; Composes with PLACE's own setf-expansion so adding a property (which
;; conses a fresh (indicator value) pair onto the front, changing the
;; list head) reassigns PLACE itself rather than a temp.  Without this,
;; GET-SETF-EXPANSION fell through to the generic branch that binds PLACE
;; to a temp, so INCF/DECF/PUSH on an absent key silently dropped the new
;; pair (ANSI INCF-GETF.2).  DEFAULT, when supplied, is evaluated once and
;; read by the access form; its value is ignored on store, per the spec.
;; %SETF-GETF mutates in place when the key is present and returns the
;; (possibly new) list head otherwise.
(define-setf-expander getf (place indicator &optional default &environment env)
  (multiple-value-bind (temps vals stores set-form access-form)
      (get-setf-expansion place env)
    (let ((itemp (gensym "IND"))
          (dtemp (gensym "DEFAULT"))
          (store (gensym "VAL"))
          (ptemp (car stores)))
      (values
       (append temps (list itemp dtemp))
       (append vals (list indicator default))
       (list store)
       `(let ((,ptemp (clamiga::%setf-getf ,access-form ,itemp ,store)))
          ,set-form
          ,store)
       `(getf ,access-form ,itemp ,dtemp)))))

;; List searching — CLHS MEMBER accepts :test, :test-not, and :key.
;; :test defaults to EQL; :key, if supplied, is applied to each list
;; element before testing (item itself is NOT keyed, per spec).
(defun member (item list &key (test nil test-p)
                              (test-not nil test-not-p)
                              (key nil))
  (when (and test-p test-not-p)
    (error ":TEST and :TEST-NOT are mutually exclusive"))
  (do ((l list (cdr l)))
      ((null l) nil)
    (let* ((elem (if key (funcall key (car l)) (car l)))
           (match (cond (test-not-p (not (funcall test-not item elem)))
                        (test-p (funcall test item elem))
                        (t (eql item elem)))))
      (when match
        (return-from member l)))))

;; CLHS 14.2 MEMBER-IF / MEMBER-IF-NOT — return the tail whose car
;; satisfies (or fails) PREDICATE.  :KEY is applied to the element, not
;; to the predicate's result.  :KEY = NIL means use #'identity.
(defun member-if (predicate list &key (key nil))
  (do ((l list (cdr l)))
      ((null l) nil)
    (let ((elem (if key (funcall key (car l)) (car l))))
      (when (funcall predicate elem)
        (return-from member-if l)))))

(defun member-if-not (predicate list &key (key nil))
  (do ((l list (cdr l)))
      ((null l) nil)
    (let ((elem (if key (funcall key (car l)) (car l))))
      (unless (funcall predicate elem)
        (return-from member-if-not l)))))

;; ADJOIN — override C version with full :test/:test-not/:key support.
(defun adjoin (item list &key (test nil test-p) (test-not nil test-not-p)
                              (key nil))
  (when (and test-p test-not-p)
    (error "ADJOIN: cannot supply both :TEST and :TEST-NOT"))
  (let ((pred (cond (test-not-p (complement test-not))
                    (test-p test)
                    (t #'eql)))
        (item-key (if key (funcall key item) item)))
    (dolist (x list)
      (when (funcall pred item-key (if key (funcall key x) x))
        (return-from adjoin list)))
    (cons item list)))

;; pushnew — push via ADJOIN so :test/:test-not/:key are honored.
;; Per CLHS 5.1.1.1.1, evaluate item, then place subforms, then keys
;; (each subform exactly once).
(defmacro pushnew (item place &rest keys)
  (cond
    ((symbolp place)
     (let ((g (gensym "ITEM")))
       `(let ((,g ,item)) (setq ,place (adjoin ,g ,place ,@keys)))))
    ((and (consp place) (%place-direct-mutator-p (car place)))
     (let* ((op (car place))
            (subs (cdr place))
            (item-t (gensym "ITEM"))
            (sub-ts (mapcar (lambda (x) (declare (ignore x)) (gensym "T"))
                            subs)))
       `(let* ((,item-t ,item)
               ,@(mapcar #'list sub-ts subs))
          (setf (,op ,@sub-ts)
                (adjoin ,item-t (,op ,@sub-ts) ,@keys)))))
    (t (let ((g (gensym)))
         `(let ((,g ,item))
            (setf ,place (adjoin ,g ,place ,@keys)))))))

;; Set operations — support :test, :test-not, :key per CL spec.
;; Per CLHS 17.2.1, the :test function is invoked with a list1 element
;; first and a list2 element second.  Caller specifies which side
;; ITEM came from via REVERSE-ARGS: NIL means ITEM is from list1 (the
;; default — used by intersection / set-difference's outer loop, and
;; the first set-exclusive-or loop) and the test is called as
;; (test item list-elem); T means ITEM is from list2 (used by union's
;; loop and the second set-exclusive-or loop) and the test is invoked
;; as (test list-elem item) so list1 is still the first argument.
(defun %set-member (keyed-item list test key reverse-args)
  (dolist (y list nil)
    (let ((y-key (if key (funcall key y) y)))
      (when (if reverse-args
                (funcall test y-key keyed-item)
                (funcall test keyed-item y-key))
        (return t)))))

(defun intersection (list1 list2 &key (test nil test-p) (test-not nil test-not-p) (key nil))
  (when (and test-p test-not-p)
    (error "Cannot supply both :TEST and :TEST-NOT"))
  (let ((result nil)
        (actual-test (cond (test-not-p (complement test-not))
                           (test-p test)
                           (t #'eql))))
    (dolist (x list1 (nreverse result))
      (when (%set-member (if key (funcall key x) x) list2 actual-test key nil)
        (push x result)))))

(defun union (list1 list2 &key (test nil test-p) (test-not nil test-not-p) (key nil))
  (when (and test-p test-not-p)
    (error "Cannot supply both :TEST and :TEST-NOT"))
  (let ((result (copy-list list1))
        (actual-test (cond (test-not-p (complement test-not))
                           (test-p test)
                           (t #'eql))))
    (dolist (x list2 (nreverse result))
      (unless (%set-member (if key (funcall key x) x) list1 actual-test key t)
        (push x result)))))

(defun set-difference (list1 list2 &key (test nil test-p) (test-not nil test-not-p) (key nil))
  (when (and test-p test-not-p)
    (error "Cannot supply both :TEST and :TEST-NOT"))
  (let ((result nil)
        (actual-test (cond (test-not-p (complement test-not))
                           (test-p test)
                           (t #'eql))))
    (dolist (x list1 (nreverse result))
      (unless (%set-member (if key (funcall key x) x) list2 actual-test key nil)
        (push x result)))))

;; ASSOC / RASSOC — override the C builtins to add :test-not and :key.
(defun assoc (item alist &key (test nil test-p) (test-not nil test-not-p)
                              (key nil))
  (when (and test-p test-not-p)
    (error "ASSOC: cannot supply both :TEST and :TEST-NOT"))
  (let ((pred (cond (test-not-p (complement test-not))
                    (test-p test)
                    (t #'eql))))
    (dolist (pair alist nil)
      (cond ((null pair))
            ((consp pair)
             (let ((k (car pair)))
               (when (funcall pred item (if key (funcall key k) k))
                 (return-from assoc pair))))
            (t (error 'type-error :datum pair :expected-type 'list))))))

(defun rassoc (item alist &key (test nil test-p) (test-not nil test-not-p)
                              (key nil))
  (when (and test-p test-not-p)
    (error "RASSOC: cannot supply both :TEST and :TEST-NOT"))
  (let ((pred (cond (test-not-p (complement test-not))
                    (test-p test)
                    (t #'eql))))
    (dolist (pair alist nil)
      (cond ((null pair))
            ((consp pair)
             (let ((v (cdr pair)))
               (when (funcall pred item (if key (funcall key v) v))
                 (return-from rassoc pair))))
            (t (error 'type-error :datum pair :expected-type 'list))))))

;; SUBLIS / SUBST / NSUBST — override C builtins for full :test/:test-not/:key.
(defun sublis (alist tree &key (test nil test-p) (test-not nil test-not-p)
                               (key nil))
  (when (and test-p test-not-p)
    (error "SUBLIS: cannot supply both :TEST and :TEST-NOT"))
  (let ((pred (cond (test-not-p (complement test-not))
                    (test-p test)
                    (t #'eql))))
    (labels ((walk (tree)
               (let ((tk (if key (funcall key tree) tree)))
                 (dolist (pair alist)
                   (when (consp pair)
                     (when (funcall pred tk (car pair))
                       (return-from walk (cdr pair))))))
               (if (consp tree)
                   (let ((car-r (walk (car tree)))
                         (cdr-r (walk (cdr tree))))
                     (if (and (eq car-r (car tree)) (eq cdr-r (cdr tree)))
                         tree
                         (cons car-r cdr-r)))
                   tree)))
      (walk tree))))

(defun subst (new old tree &key (test nil test-p) (test-not nil test-not-p)
                                (key nil))
  (when (and test-p test-not-p)
    (error "SUBST: cannot supply both :TEST and :TEST-NOT"))
  (let ((pred (cond (test-not-p (complement test-not))
                    (test-p test)
                    (t #'eql))))
    (labels ((walk (tree)
               (cond
                 ((funcall pred old (if key (funcall key tree) tree)) new)
                 ((consp tree)
                  (let ((car-r (walk (car tree)))
                        (cdr-r (walk (cdr tree))))
                    (if (and (eq car-r (car tree)) (eq cdr-r (cdr tree)))
                        tree
                        (cons car-r cdr-r))))
                 (t tree))))
      (walk tree))))

(defun nsubst (new old tree &rest keys)
  (apply #'subst new old tree keys))

;; COPY-ALIST / MAKE-LIST :initial-element
(defun copy-alist (alist)
  "Return a copy of ALIST in which each pair is itself copied."
  (let ((result nil))
    (dolist (pair alist (nreverse result))
      (push (if (consp pair) (cons (car pair) (cdr pair)) pair) result))))

(defun subst-if (new test tree &key (key nil))
  "Substitute NEW for every subtree of TREE for which TEST returns true."
  (cond
    ((funcall test (if key (funcall key tree) tree)) new)
    ((consp tree)
     (let ((car-r (subst-if new test (car tree) :key key))
           (cdr-r (subst-if new test (cdr tree) :key key)))
       (if (and (eq car-r (car tree)) (eq cdr-r (cdr tree)))
           tree
           (cons car-r cdr-r))))
    (t tree)))

(defun subst-if-not (new test tree &key (key nil))
  "Substitute NEW for every subtree of TREE for which TEST returns false."
  (subst-if new (complement test) tree :key key))

(defun nsubst-if (new test tree &key (key nil))
  (subst-if new test tree :key key))

(defun nsubst-if-not (new test tree &key (key nil))
  (subst-if-not new test tree :key key))

(defun nsublis (alist tree &rest keys)
  (apply #'sublis alist tree keys))

(defun set-exclusive-or (list1 list2 &key (test nil test-p) (test-not nil test-not-p) (key nil))
  "Return a list of elements appearing in exactly one of LIST1, LIST2."
  (when (and test-p test-not-p)
    (error "Cannot supply both :TEST and :TEST-NOT"))
  (let ((actual-test (cond (test-not-p (complement test-not))
                           (test-p test)
                           (t #'eql)))
        (result nil))
    (dolist (x list1)
      (unless (%set-member (if key (funcall key x) x) list2 actual-test key nil)
        (push x result)))
    (dolist (y list2)
      (unless (%set-member (if key (funcall key y) y) list1 actual-test key t)
        (push y result)))
    (nreverse result)))

;; CLHS: the N-prefixed variants may destructively modify their list
;; arguments but are not required to. We delegate to the non-destructive
;; versions — correctness wins, optimization can come later.
(defun nunion (list1 list2 &rest keys)
  (apply #'union list1 list2 keys))

(defun nintersection (list1 list2 &rest keys)
  (apply #'intersection list1 list2 keys))

(defun nset-difference (list1 list2 &rest keys)
  (apply #'set-difference list1 list2 keys))

(defun nset-exclusive-or (list1 list2 &rest keys)
  (apply #'set-exclusive-or list1 list2 keys))

(defun subsetp (list1 list2 &key (test nil test-p) (test-not nil test-not-p) (key nil))
  (when (and test-p test-not-p)
    (error "Cannot supply both :TEST and :TEST-NOT"))
  (let ((actual-test (cond (test-not-p (complement test-not))
                           (test-p test)
                           (t #'eql))))
    (dolist (x list1 t)
      (unless (%set-member (if key (funcall key x) x) list2 actual-test key nil)
        (return-from subsetp nil)))))

;; documentation — CL standard documentation function
(defvar *documentation-table* (make-hash-table :test 'equal))

(defun documentation (obj doc-type)
  (gethash (cons obj doc-type) *documentation-table*))

(defun %set-documentation (obj doc-type string)
  (setf (gethash (cons obj doc-type) *documentation-table*) string))

(defsetf documentation %set-documentation)

;; with-open-file — defined early so compile-file can use it.
;; Dependencies: open, close (C builtins), unwind-protect/locally/when (special forms).
(defmacro with-open-file (spec &body body)
  (let ((var (car spec))
        (open-args (cdr spec)))
    `(let ((,var (open ,@open-args)))
       (unwind-protect
         (locally ,@body)
         (when ,var (close ,var))))))

(defmacro with-open-stream ((var stream) &body body)
  `(let ((,var ,stream))
     (unwind-protect
       (locally ,@body)
       (when ,var (close ,var)))))

;; compile-file and compile-file-pathname are C builtins (builtins_io.c).
;; compile-file produces real FASL binary files that load without reparsing.

(defmacro with-compilation-unit ((&rest options) &body body)
  (declare (ignore options))
  `(progn ,@body))

;; with-hash-table-iterator — CLHS 18.2.
;; Snapshots the table as a list of (key . value) pairs and exposes a
;; local iterator function that returns three values per call:
;;   (values t key value)  — for each remaining pair
;;   (values nil nil nil)  — once exhausted
(defmacro with-hash-table-iterator ((iter-name hash-table) &body body)
  (let ((pairs (gensym "PAIRS"))
        (pair (gensym "PAIR")))
    `(let ((,pairs (%hash-table-pairs ,hash-table)))
       (flet ((,iter-name ()
                (if ,pairs
                    (let ((,pair (pop ,pairs)))
                      (values t (car ,pair) (cdr ,pair)))
                    (values nil nil nil))))
         ,@body))))

(defmacro with-standard-io-syntax (&body body)
  `(let ((*package* (find-package :common-lisp-user))
         (*print-base* 10)
         (*print-radix* nil)
         (*print-circle* nil)
         (*print-escape* t)
         (*print-readably* nil)
         (*read-base* 10)
         (*read-eval* t))
     ,@body))

(defmacro print-unreadable-object ((object stream &key type identity) &body body)
  (let ((obj-var (gensym)))
    `(let ((,obj-var ,object))
       (write-string "#<" ,stream)
       ,@(when type `((write-string (symbol-name (type-of ,obj-var)) ,stream)
                       ,(when body `(write-char #\Space ,stream))))
       ,@body
       (write-string ">" ,stream)
       nil)))

;; handler-case — run clause bodies after unwinding
;; Uses catch/throw + cons box because:
;;   - handlers run in separate VM context (cl_vm_apply), so return-from can't
;;     reach the establishing block
;;   - closures use value capture, so setq on outer variables doesn't propagate;
;;     rplaca on a shared cons cell does propagate
(defmacro handler-case (form &rest clauses)
  ;; Normal-return path uses (return-from BLK (handler-bind ... FORM)) so ALL
  ;; values of FORM are returned (CLHS: handler-case yields the values of its
  ;; body when no condition is handled).  An earlier version bound the CATCH
  ;; result to a single variable, which collapsed multiple values to the
  ;; primary one — e.g. (handler-case (values a b) ...) lost b, breaking
  ;; callers like rfc2388/str-based code that destructure a (values ...).
  ;; On a handled condition the handler throws TAG, the RETURN-FROM is skipped,
  ;; CATCH returns normally, and control falls through to the clause dispatch.
  (let ((tag (gensym "HC"))
        (box (gensym "BOX"))
        (blk (gensym "HCBLK")))
    `(let ((,box (cons nil nil)))
       (block ,blk
         (catch ',tag
           (return-from ,blk
             (handler-bind
               ,(mapcar (lambda (clause)
                          `(,(car clause) (lambda (c)
                                            (rplaca ,box c)
                                            (throw ',tag ',tag))))
                        clauses)
               ,form)))
         (typecase (car ,box)
           ,@(mapcar (lambda (clause)
                       (let ((type (car clause))
                             (arglist (cadr clause))
                             (body (cddr clause)))
                         `(,type (let ((,(if arglist (car arglist) (gensym)) (car ,box)))
                                   ,@body))))
                     clauses))))))

;; ignore-errors — catch errors, return (values nil condition)
(defmacro ignore-errors (&rest body)
  `(handler-case (progn ,@body)
     (error (c) (values nil c))))

;; with-simple-restart — establish a named restart that returns (values nil t).
;; Per CLHS the spec is (name format-control format-argument*); format-control
;; is a format control evaluated at print time, so the restart's :report must
;; be a function (a bare symbol/variable there would be read as a function
;; name) that formats the control with its arguments.
(defmacro with-simple-restart (restart-spec &rest body)
  (let ((name (car restart-spec))
        (format-control (cadr restart-spec))
        (format-args (cddr restart-spec))
        (s (gensym "STREAM")))
    `(restart-case (progn ,@body)
       (,name ()
         :report (lambda (,s) (format ,s ,format-control ,@format-args))
         (values nil t)))))

;; restart-bind — like restart-case but the body is *not* wrapped in a
;; non-local exit; if a restart handler is invoked it is called as a regular
;; function and execution continues after restart-bind returns its value.
;; CL-Amiga has no interactive debugger driver so we cannot really invoke
;; user-provided restart handlers; the pragmatic stub just runs the body.
(defmacro restart-bind (bindings &rest body)
  (declare (ignore bindings))
  `(progn ,@body))

;; cerror — continuable error: establish CONTINUE restart around error.
;; The CONTINUE restart's report formats format-control with args at print
;; time, so it must be a report function (a bare variable would be read as a
;; function name by restart-case).
(defun cerror (format-control datum &rest args)
  (restart-case (apply #'error datum args)
    (continue () :report (lambda (s) (apply #'format s format-control args)) nil)))

;; %set-condition-default-initargs is a CLAMIGA builtin (builtins_condition.c):
;; it records a condition type's :default-initargs so make-condition / error /
;; signal can fill in initargs the caller omitted.

;; define-condition — define a user condition type with slots and readers
(defmacro define-condition (name parent-types slot-specs &rest options)
  (let ((parent (cond ((consp parent-types) (car parent-types))
                      ((null parent-types) 'condition)
                      (t parent-types)))
        (parents-list (cond ((consp parent-types) parent-types)
                            ((null parent-types) '(condition))
                            (t (list parent-types))))
        (slot-pairs (mapcar (lambda (spec) (cons (car spec) (getf (cdr spec) :initarg))) slot-specs))
        (report nil)
        (default-initargs nil)
        (clos-available (fboundp '%add-method-to-gf)))
    ;; Parse options
    (dolist (opt options)
      (case (car opt)
        (:report (setq report (cadr opt)))
        (:default-initargs (setq default-initargs (cdr opt)))))
    `(progn
       (%register-condition-type ',name ',parent ',slot-pairs)
       (when (fboundp '%register-condition-class)
         (%register-condition-class ',name ',parents-list))
       ,@(mapcan (lambda (slot-spec)
                   (let* ((slot-name (car slot-spec))
                          (opts (cdr slot-spec))
                          (reader (or (getf opts :reader)
                                      (getf opts :accessor)))
                          (accessor (getf opts :accessor)))
                     (append
                       (when reader
                         (if clos-available
                             (list `(defmethod ,reader ((c ,name)) (condition-slot-value c ',slot-name)))
                             (list `(defun ,reader (c) (condition-slot-value c ',slot-name)))))
                       (when accessor
                         (let ((setter-name (intern (concatenate 'string
                                                     "%SETF-" (symbol-name accessor))
                                                   (find-package :clamiga))))
                           (list `(defun ,setter-name (val c)
                                    (%set-condition-slot-value c ',slot-name val))))))))
                 slot-specs)
       ,@(when report
           (cond
             ((stringp report)
              `((defmethod print-object ((c ,name) stream)
                  (write-string ,report stream))))
             ;; A SYMBOL :report arg names a function of (condition stream)
             ;; (CLHS define-condition).  Splicing it bare into funcall's first
             ;; (evaluated) slot would read it as a VARIABLE — "Unbound variable:
             ;; FOO" — so quote it as a function designator.  A lambda
             ;; expression already evaluates to a function, so pass it through.
             ((symbolp report)
              `((defmethod print-object ((c ,name) stream)
                  (funcall ',report c stream))))
             (t
              `((defmethod print-object ((c ,name) stream)
                  (funcall ,report c stream))))))
       ,@(when default-initargs
           `((%set-condition-default-initargs ',name ',default-initargs)))
       ',name)))

;; check-type — signal type-error if place is not of type
(defmacro check-type (place type &optional type-string)
  (let ((val (gensym)))
    `(let ((,val ,place))
       (unless (typep ,val ',type)
         (error 'type-error :datum ,val :expected-type ',type)))))

;; ccase — like ecase (signals an error if no clause matches), but the error
;; is continuable: in a real implementation a STORE-VALUE restart lets the user
;; supply a new keyform value and retry.  We don't have a debugger that can
;; offer restarts interactively, so we treat CCASE as ECASE (one-shot type
;; error) — the host-incompatible difference is the restart presentation, not
;; the value-on-success or value-on-mismatch behaviour.
(defmacro ccase (keyplace &rest clauses)
  `(ecase ,keyplace ,@clauses))

;; ctypecase — same relationship to etypecase.
(defmacro ctypecase (keyplace &rest clauses)
  `(etypecase ,keyplace ,@clauses))

;; assert — signal error if test-form is false
(defmacro assert (test-form &optional places string &rest args)
  (declare (ignore places))
  (if (and string args)
      `(unless ,test-form
         (error 'simple-error
                :format-control ,string
                :format-arguments (list ,@args)))
      `(unless ,test-form
         (error 'simple-error
                :format-control ,(or string "Assertion failed: ~S")
                :format-arguments (list ',test-form)))))

;; defpackage — define a package with :use, :export, :nicknames, :local-nicknames,
;;   :import-from, :shadow, :shadowing-import-from, :intern options
(defmacro defpackage (name &rest options)
  (let ((pkg-name (if (symbolp name) (symbol-name name) name))
        (uses nil)
        (exports nil)
        (nicknames nil)
        (local-nicks nil)
        (import-froms nil)
        (shadows nil)
        (shadowing-import-froms nil)
        (interns nil))
    ;; Parse options
    (dolist (opt options)
      (case (car opt)
        (:use (setq uses (append uses (cdr opt))))
        (:export (setq exports (append exports (cdr opt))))
        (:nicknames (setq nicknames (append nicknames (cdr opt))))
        (:local-nicknames (setq local-nicks (cdr opt)))
        (:import-from (setq import-froms (cons (cdr opt) import-froms)))
        (:shadow (setq shadows (append shadows (cdr opt))))
        (:shadowing-import-from (setq shadowing-import-froms (cons (cdr opt) shadowing-import-froms)))
        (:intern (setq interns (append interns (cdr opt))))))
    `(eval-when (:compile-toplevel :load-toplevel :execute)
       (let ((pkg (or (find-package ,pkg-name)
                      (make-package ,pkg-name :nicknames ',nicknames))))
         ,@(when shadows
             `((dolist (s ',shadows)
                 (shadow (if (symbolp s) (symbol-name s) s) pkg))))
         ,@(when uses
             `((dolist (u ',uses)
                 (use-package (or (find-package (if (symbolp u) (symbol-name u) u))
                                  (error "Package ~A not found" u))
                              pkg))))
         ,@(mapcar (lambda (import-spec)
                     (let ((from-pkg (car import-spec))
                           (syms (cdr import-spec)))
                       `(let ((src-pkg (or (find-package ,(if (symbolp from-pkg)
                                                             (symbol-name from-pkg)
                                                             from-pkg))
                                           (error "Package ~A not found" ',from-pkg))))
                          (dolist (s ',syms)
                            (let ((sym (find-symbol (if (symbolp s) (symbol-name s) s) src-pkg)))
                              (when sym (import sym pkg)))))))
                   import-froms)
         ,@(mapcar (lambda (import-spec)
                     (let ((from-pkg (car import-spec))
                           (syms (cdr import-spec)))
                       `(let ((src-pkg (or (find-package ,(if (symbolp from-pkg)
                                                             (symbol-name from-pkg)
                                                             from-pkg))
                                           (error "Package ~A not found" ',from-pkg))))
                          (dolist (s ',syms)
                            (let ((sym (find-symbol (if (symbolp s) (symbol-name s) s) src-pkg)))
                              (when sym (shadowing-import sym pkg)))))))
                   shadowing-import-froms)
         ,@(when interns
             `((dolist (s ',interns)
                 (intern (if (symbolp s) (symbol-name s) s) pkg))))
         ,@(when exports
             `((dolist (e ',exports)
                 (export (intern (if (symbolp e) (symbol-name e) e) pkg) pkg))))
         ,@(when local-nicks
             `((dolist (ln ',local-nicks)
                 (add-package-local-nickname
                  (car ln)
                  (or (find-package (if (symbolp (cadr ln)) (symbol-name (cadr ln)) (cadr ln)))
                      (error "Package ~A not found for local nickname" (cadr ln)))
                  pkg))))
         pkg))))

;; do-symbols — iterate over all symbols in a package
(defmacro do-symbols (spec &body body)
  (let ((var (car spec))
        (package (cadr spec))
        (result (caddr spec))
        (pkg (gensym)) (syms (gensym)) (s (gensym)))
    `(let* ((,pkg ,(if package `(find-package ,package) '*package*))
            (,syms (%package-symbols ,pkg)))
       (dolist (,s ,syms ,result)
         (let ((,var ,s))
           ,@body)))))

;; do-external-symbols — iterate over exported symbols in a package
(defmacro do-external-symbols (spec &body body)
  (let ((var (car spec))
        (package (cadr spec))
        (result (caddr spec))
        (pkg (gensym)) (syms (gensym)) (s (gensym)))
    `(let* ((,pkg ,(if package `(find-package ,package) '*package*))
            (,syms (%package-external-symbols ,pkg)))
       (dolist (,s ,syms ,result)
         (let ((,var ,s))
           ,@body)))))

;; do-all-symbols — iterate over the symbols of all registered packages.
;; CLHS: spec is (var [result-form]) — no package argument.  A symbol that is
;; present in more than one package is processed only once (we track visited
;; symbols by identity); result-form is evaluated with var bound to NIL.
(defmacro do-all-symbols (spec &body body)
  ;; Use tagbody/go instead of dolist so the inner loops don't create their
  ;; own implicit (block nil ...) that would intercept (return ...) from body.
  (let ((var (car spec))
        (result (cadr spec))
        (pkgs (gensym)) (pkg (gensym))
        (syms (gensym)) (s (gensym))
        (seen (gensym))
        (pkg-loop (gensym)) (sym-loop (gensym)) (done (gensym)))
    `(block nil
       (let ((,pkgs (list-all-packages))
             (,pkg nil)
             (,syms nil)
             (,s nil)
             (,seen (make-hash-table :test 'eq)))
         (tagbody
          ,pkg-loop
          (when (null ,pkgs) (go ,done))
          (setq ,pkg (car ,pkgs))
          (setq ,pkgs (cdr ,pkgs))
          (setq ,syms (%package-symbols ,pkg))
          ,sym-loop
          (when (null ,syms) (go ,pkg-loop))
          (setq ,s (car ,syms))
          (setq ,syms (cdr ,syms))
          (unless (gethash ,s ,seen)
            (setf (gethash ,s ,seen) t)
            (let ((,var ,s))
              ,@body))
          (go ,sym-loop)
          ,done))
       (let ((,var nil)) ,result))))

;; defstruct — define a named structure type
;; Supports options: :conc-name, :constructor, :predicate, :copier, :include
(defun %defstruct-parse-slot (spec)
  "Parse a slot spec into (name default). Accepts NAME or (NAME DEFAULT)."
  (if (consp spec)
      (list (car spec) (cadr spec))
      (list spec nil)))

(defun %boa-patch-defaults (boa-lambda-list all-slots)
  "Patch &optional and &key params in a BOA lambda list to use slot defaults
when the param has no explicit default.  CL spec 3.4.6 requires this."
  (let ((state :required)
        (result nil))
    (dolist (p boa-lambda-list)
      (cond
        ((member p '(&optional &key &rest &aux &allow-other-keys))
         (setq state p)
         (push p result))
        ((or (eq state :required) (eq state '&rest))
         (push p result))
        ((or (eq state '&optional) (eq state '&key))
         (if (and (symbolp p)
                  ;; bare symbol &optional/&key param — inject slot default
                  (not (member p '(&optional &key &rest &aux &allow-other-keys))))
             (let ((slot (assoc p all-slots)))
               (if (and slot (cadr slot))
                   ;; Slot has a default form, wrap param with it
                   (push (list p (cadr slot)) result)
                   (push p result)))
             ;; Already a list (has explicit default) — keep as-is
             (push p result)))
        (t (push p result))))
    (reverse result)))

(defmacro defstruct (name-and-options &rest slot-specs)
  (let* ((name (if (consp name-and-options) (car name-and-options) name-and-options))
         (options (if (consp name-and-options) (cdr name-and-options) nil))
         (name-str (symbol-name name))
         ;; Parse options
         (conc-name-opt nil) (conc-name-set nil)
         ;; CLHS 3.4.6.6: a struct may declare MANY (:constructor ...) options.
         ;; CONSTRUCTORS collects (NAME HAS-BOA-P BOA-LAMBDA-LIST) per option;
         ;; (:constructor nil) suppresses (adds nothing).
         (constructors nil) (constructor-set nil)
         (predicate-opt nil) (predicate-set nil)
         (copier-opt nil) (copier-set nil)
         (include-name nil) (include-slots nil)
         (type-opt nil)
         (print-function-opt nil) (print-object-opt nil))
    ;; Process options
    (dolist (opt options)
      (cond
        ((and (consp opt) (eq (car opt) :conc-name))
         (setq conc-name-set t)
         (setq conc-name-opt (cadr opt)))
        ((eq opt :conc-name)
         (setq conc-name-set t)
         (setq conc-name-opt nil))
        ((and (consp opt) (eq (car opt) :constructor))
         (setq constructor-set t)
         ;; (:constructor name)          => keyword constructor
         ;; (:constructor name arglist)  => BOA constructor
         ;; (:constructor nil)           => suppress (record nothing)
         (when (cadr opt)
           (setq constructors
                 (cons (list (cadr opt) (consp (cddr opt)) (caddr opt))
                       constructors))))
        ((eq opt :constructor)
         (setq constructor-set t))
        ((and (consp opt) (eq (car opt) :predicate))
         (setq predicate-set t)
         (setq predicate-opt (cadr opt)))
        ((eq opt :predicate)
         (setq predicate-set t)
         (setq predicate-opt nil))
        ((and (consp opt) (eq (car opt) :copier))
         (setq copier-set t)
         (setq copier-opt (cadr opt)))
        ((eq opt :copier)
         (setq copier-set t)
         (setq copier-opt nil))
        ((and (consp opt) (eq (car opt) :include))
         (setq include-name (cadr opt)))
        ((and (consp opt) (eq (car opt) :type))
         (setq type-opt (cadr opt)))
        ((and (consp opt) (eq (car opt) :print-function))
         (setq print-function-opt (cadr opt)))
        ((and (consp opt) (eq (car opt) :print-object))
         (setq print-object-opt (cadr opt)))))
    ;; Compute inherited slots from :include (with defaults)
    (when include-name
      (let ((parent-specs (%struct-slot-specs include-name)))
        (dolist (spec parent-specs)
          (push spec include-slots))
        (setq include-slots (reverse include-slots))))
    ;; Skip docstring if first element is a string
    (when (and slot-specs (stringp (car slot-specs)))
      (pop slot-specs))
    ;; Build full slot list: inherited + own
    (let* ((own-parsed (mapcar #'%defstruct-parse-slot slot-specs))
           (all-slots (append include-slots own-parsed))
           (n-slots (length all-slots))
           (slot-names (mapcar #'car all-slots))
           ;; Compute conc-name prefix (user supplies the full prefix incl. separator)
           (prefix (cond
                     ((not conc-name-set) (concatenate 'string name-str "-"))
                     ((null conc-name-opt) "")
                     (t (symbol-name conc-name-opt))))
           ;; Constructor specs (NAME HAS-BOA-P BOA-LL).  With no (:constructor
           ;; ...) option, default to a single keyword constructor MAKE-<name>.
           (ctor-specs (reverse
                        (if constructor-set
                            constructors
                            (list (list (intern (concatenate 'string "MAKE-" name-str))
                                        nil nil)))))
           ;; Predicate name
           (pred-name (cond
                        ((not predicate-set) (intern (concatenate 'string name-str "-P")))
                        ((null predicate-opt) nil)
                        (t predicate-opt)))
           ;; Copier name
           (copy-name (cond
                        ((not copier-set) (intern (concatenate 'string "COPY-" name-str)))
                        ((null copier-opt) nil)
                        (t copier-opt)))
           (forms nil))
      ;; A typed-sequence struct: (:type vector), (:type (vector ELT [SIZE]))
      ;; — e.g. ironclad's (:type (vector (unsigned-byte 32))) — or (:type
      ;; list) — e.g. rfc2388's (defstruct (header (:type list) ...)).  The
      ;; declared element type/size are advisory and ignored; the value is a
      ;; real CL:VECTOR / CL:LIST so REPLACE/AREF/SVREF/NTH work on it, and
      ;; snooze's (second (parse-header ...)) sees an actual list.
      (if (or (eq type-opt 'vector)
              (eq type-opt 'list)
              (and (consp type-opt) (eq (car type-opt) 'vector)))
          ;; --- (:type vector) / (:type list) mode: represent as a typed
          ;; sequence (CL:VECTOR or CL:LIST), not a real struct ---
          (let* ((seq-kind (cond
                             ((or (eq type-opt 'vector)
                                  (and (consp type-opt)
                                       (eq (car type-opt) 'vector)))
                              'vector)
                             (t 'list)))
                 (builder (if (eq seq-kind 'vector) 'vector 'list)))
            ;; Constructors (one per (:constructor ...) option)
            (dolist (cspec ctor-specs)
              (let ((ctor-name (car cspec))
                    (has-boa (cadr cspec))
                    (boa-lambda-list (caddr cspec)))
                (if has-boa
                    (let ((slot-inits (mapcar (lambda (s)
                                                (let* ((sname (car s))
                                                       (sdefault (cadr s)))
                                                  (if (member sname boa-lambda-list
                                                              :test (lambda (name p)
                                                                      (cond ((symbolp p)
                                                                             (and (not (member p '(&optional &key &rest &aux)))
                                                                                  (eq name p)))
                                                                            ((consp p) (eq name (car p)))
                                                                            (t nil))))
                                                      sname
                                                      sdefault)))
                                              all-slots)))
                      (push `(defun ,ctor-name ,(%boa-patch-defaults boa-lambda-list all-slots)
                               (,builder ,@slot-inits))
                            forms))
                    (let ((key-params (mapcar (lambda (s) (list (car s) (cadr s)))
                                              all-slots)))
                      (push `(defun ,ctor-name (&key ,@key-params)
                               (,builder ,@(mapcar #'car all-slots)))
                            forms)))))
            ;; No predicate or copier for typed-sequence structs — no type
            ;; discrimination.  Accessors index by position (svref / nth).
            (let ((idx 0))
              (dolist (sname slot-names)
                (let* ((acc-name (intern (concatenate 'string prefix (symbol-name sname))))
                       (setter-name (intern (concatenate 'string "%SET-" (symbol-name acc-name))))
                       (getter-form (if (eq seq-kind 'vector)
                                        `(svref obj ,idx)
                                        `(nth ,idx obj)))
                       (set-place   (if (eq seq-kind 'vector)
                                        `(svref obj ,idx)
                                        `(nth ,idx obj))))
                  (push `(defun ,acc-name (obj) ,getter-form) forms)
                  (push `(defun ,setter-name (obj val) (setf ,set-place val)) forms)
                  (push `(defsetf ,acc-name ,setter-name) forms))
                (setq idx (+ idx 1))))
            ;; EVAL-WHEN (not PROGN): a top-level DEFSTRUCT must register its
            ;; slots/accessors at COMPILE time so a later same-file DEFSTRUCT
            ;; with (:INCLUDE this) can resolve inherited slots when COMPILE-FILE
            ;; macroexpands it (CLHS 3.2.3.1).  With PROGN the registration only
            ;; ran at load time, so compiled (:include ...) constructors dropped
            ;; their inherited-slot keyword args.
            `(eval-when (:compile-toplevel :load-toplevel :execute)
               ,@(reverse forms) ',name))
        ;; --- Normal struct mode ---
        (progn
          ;; Register the struct type (store slot specs with defaults for :include)
          (push `(%register-struct-type ',name ,n-slots
                   ,(if include-name `',include-name nil)
                   ',all-slots)
                forms)
          ;; Constructors (one per (:constructor ...) option; CLHS 3.4.6.6)
          (dolist (cspec ctor-specs)
            (let ((ctor-name (car cspec))
                  (has-boa (cadr cspec))
                  (boa-lambda-list (caddr cspec)))
              (if has-boa
                  ;; BOA constructor: positional args mapped to slots by name
                  (let ((slot-inits (mapcar (lambda (s)
                                              ;; For each slot, use the BOA param if named,
                                              ;; otherwise use the slot default
                                              (let* ((sname (car s))
                                                     (sdefault (cadr s)))
                                                ;; Check if slot name appears in BOA params
                                                (if (member sname boa-lambda-list
                                                            :test (lambda (name p)
                                                                    (cond ((symbolp p)
                                                                           (and (not (member p '(&optional &key &rest &aux)))
                                                                                (eq name p)))
                                                                          ((consp p) (eq name (car p)))
                                                                          (t nil))))
                                                    sname
                                                    sdefault)))
                                            all-slots)))
                    (push `(defun ,ctor-name ,(%boa-patch-defaults boa-lambda-list all-slots)
                             (%make-struct ',name ,@slot-inits))
                          forms))
                  ;; Standard keyword constructor
                  (let ((key-params (mapcar (lambda (s)
                                              (list (car s) (cadr s)))
                                            all-slots)))
                    (push `(defun ,ctor-name (&key ,@key-params)
                             (%make-struct ',name ,@(mapcar #'car all-slots)))
                          forms)))))
          ;; Predicate
          (when pred-name
            (push `(defun ,pred-name (obj) (typep obj ',name))
                  forms))
          ;; Copier
          (when copy-name
            (push `(defun ,copy-name (obj) (%copy-struct obj))
                  forms))
          ;; Accessors and setf writers.  We emit both a defun (so #'line-sx
          ;; works for funcall/mapcar) AND a compiler macro that inlines
          ;; the call into (clamiga::%struct-ref obj <idx>) — which the
          ;; compiler then lowers to OP_STRUCT_REF, skipping the wrapper
          ;; frame entirely on direct calls.  Same for the setter.
          (let ((idx 0))
            (dolist (sname slot-names)
              (let* ((acc-name (intern (concatenate 'string prefix (symbol-name sname))))
                     (setter-name (intern (concatenate 'string "%SET-" (symbol-name acc-name)))))
                (push `(defun ,acc-name (obj) (clamiga::%struct-ref obj ,idx)) forms)
                (push `(define-compiler-macro ,acc-name (obj)
                         (list 'clamiga::%struct-ref obj ,idx))
                      forms)
                (push `(defun ,setter-name (obj val) (clamiga::%struct-set obj ,idx val)) forms)
                (push `(define-compiler-macro ,setter-name (obj val)
                         (list 'clamiga::%struct-set obj ,idx val))
                      forms)
                (push `(defsetf ,acc-name ,setter-name) forms))
              (setq idx (+ idx 1))))
          ;; Register struct as CLOS class if CLOS is loaded
          (push `(when (fboundp '%make-bootstrap-class)
                   (%make-bootstrap-class ',name
                     (list (find-class ',(or include-name 'structure-object)))))
                forms)
          ;; :print-function — defmethod print-object calling (fn obj stream depth)
          (when print-function-opt
            (push `(when (fboundp 'print-object)
                     (defmethod print-object ((object ,name) stream)
                       (,print-function-opt object stream 0)))
                  forms))
          ;; :print-object — defmethod print-object calling (fn obj stream)
          (when print-object-opt
            (push `(when (fboundp 'print-object)
                     (defmethod print-object ((object ,name) stream)
                       (,print-object-opt object stream)))
                  forms))
          ;; EVAL-WHEN (not PROGN) — see the typed-struct branch above: the
          ;; struct's %register-struct-type / accessors must take effect at
          ;; COMPILE time so a later same-file (:include this) resolves its
          ;; inherited slots during COMPILE-FILE macroexpansion (CLHS 3.2.3.1).
          `(eval-when (:compile-toplevel :load-toplevel :execute)
             ,@(reverse forms) ',name))))))

;; CL bitwise functions not in C builtins
(defun logandc1 (integer-1 integer-2) (logand (lognot integer-1) integer-2))
(defun logandc2 (integer-1 integer-2) (logand integer-1 (lognot integer-2)))
(defun logorc1  (integer-1 integer-2) (logior (lognot integer-1) integer-2))
(defun logorc2  (integer-1 integer-2) (logior integer-1 (lognot integer-2)))
(defun lognand  (integer-1 integer-2) (lognot (logand integer-1 integer-2)))
(defun lognor   (integer-1 integer-2) (lognot (logior integer-1 integer-2)))
(defun logeqv   (&rest integers)
  (dolist (x integers)
    (unless (integerp x) (error 'type-error :datum x :expected-type 'integer)))
  (if (null integers) -1
    (reduce (lambda (a b) (lognot (logxor a b))) integers)))

;; psetq — parallel setq: evaluate all values, then assign
(defmacro psetq (&rest pairs)
  (let ((temps nil) (sets nil) (p pairs))
    (do () ((null p))
      (let ((var (car p))
            (val (cadr p))
            (tmp (gensym)))
        (push (list tmp val) temps)
        (push `(setq ,var ,tmp) sets)
        (setq p (cddr p))))
    `(let ,(reverse temps) ,@(reverse sets) nil)))

;; psetf — parallel setf: evaluate all values AND place subforms, then assign.
;; For compound places like (cdr x), captures x before any assignments so
;; that cross-referencing pairs work correctly.
(defmacro psetf (&rest pairs)
  (let ((bindings nil) (sets nil) (p pairs))
    (do () ((null p))
      (let ((place (car p))
            (val (cadr p))
            (val-tmp (gensym "PVAL")))
        (push (list val-tmp val) bindings)
        (if (symbolp place)
            ;; Simple variable — just assign the temp
            (push `(setf ,place ,val-tmp) sets)
            ;; Compound place — capture subforms in temps
            (let ((head (car place))
                  (args (cdr place))
                  (arg-temps nil))
              (dolist (arg args)
                (let ((at (gensym "PARG")))
                  (push (list at arg) bindings)
                  (push at arg-temps)))
              (push `(setf (,head ,@(reverse arg-temps)) ,val-tmp) sets)))
        (setq p (cddr p))))
    `(let ,(reverse bindings) ,@(reverse sets) nil)))

;; read-from-string — read an S-expression from a string
(defun read-from-string (string &optional eof-error-p eof-value)
  (let ((s (make-string-input-stream string)))
    (read s eof-error-p eof-value)))

;; read-preserving-whitespace — same as read for our implementation
(defun read-preserving-whitespace (&optional stream (eof-error-p t) eof-value recursive-p)
  (declare (ignore recursive-p))
  (read stream eof-error-p eof-value))

;; break — enter debugger with CONTINUE restart
(defun break (&optional (format-control "Break"))
  (restart-case
    (invoke-debugger (make-condition 'simple-condition
                                     :format-control format-control))
    (continue () nil)))

;; --- String stream macros ---

(defmacro with-output-to-string (spec &body body)
  (let ((var (car spec)))
    `(let ((,var (make-string-output-stream)))
       (unwind-protect
         (locally ,@body (get-output-stream-string ,var))
         (close ,var)))))

(defmacro with-input-from-string (spec &body body)
  (let ((var (car spec))
        (string (cadr spec)))
    `(let ((,var (make-string-input-stream ,string)))
       (unwind-protect
         (locally ,@body)
         (close ,var)))))

;; with-open-file — open a file stream, execute body, ensure close
;; with-open-file defined earlier in boot.lisp (before compile-file)

;; --- Stream output control ---
;; No-ops: console/file output is unbuffered, socket send() is immediate.

;; finish-output, force-output, and clear-output are C builtins — do not redefine here

;; --- Byte and sequence I/O ---
;; read-byte and write-byte are C builtins (builtins_stream.c)
;; They use raw byte I/O without UTF-8 encoding/decoding.

(defun write-sequence (sequence stream &key (start 0) end)
  (let ((e (or end (length sequence))))
    (do ((i start (1+ i)))
        ((>= i e) sequence)
      (let ((elt (elt sequence i)))
        (if (characterp elt)
            (write-char elt stream)
            (write-byte elt stream))))))

(defun read-sequence (sequence stream &key (start 0) end)
  (let ((e (or end (length sequence)))
        (i start)
        (use-char (and (arrayp sequence)
                       (subtypep (array-element-type sequence) 'character))))
    ;; Cannot use loop here — loop macro is defined later in boot.lisp
    (block nil
      (tagbody
       loop-top
       (when (>= i e) (go loop-end))
       (let ((elem (if use-char
                       (read-char stream nil nil)
                       (read-byte stream nil nil))))
         (if elem
             (progn (setf (aref sequence i) elem) (setf i (1+ i)))
             (return i)))
       (go loop-top)
       loop-end))
    i))

;; with-standard-io-syntax — bind I/O variables to standard values
;; Minimal implementation: only binds *package* to CL-USER for now.
;; Future: bind *readtable*, *print-readably*, etc. when implemented.
(defmacro with-standard-io-syntax (&body body)
  `(let ((*package* (find-package "CL-USER")))
     ,@body))

;; --- Printer-to-string functions ---

(defun prin1-to-string (object)
  (with-output-to-string (s) (prin1 object s)))

(defun princ-to-string (object)
  (with-output-to-string (s) (princ object s)))


;; --- Time functions (Step 10) ---

(defun decode-universal-time (universal-time &optional time-zone)
  "Decode universal time into 9 values: sec min hour date month year dow dst tz"
  (let* ((tz (or time-zone 0))
         (adjusted (- universal-time (* tz 3600)))
         ;; Seconds within the day
         (day-seconds (mod adjusted 86400))
         (total-days (truncate adjusted 86400))
         ;; Time components
         (second (mod day-seconds 60))
         (minute (mod (truncate day-seconds 60) 60))
         (hour (truncate day-seconds 3600))
         ;; Date computation — days since 1900-01-01 (Monday)
         ;; Day of week: 0=Monday, 6=Sunday
         (dow (mod total-days 7))
         ;; Year calculation with leap year handling
         (remaining-days total-days)
         (year 1900)
         (leap nil)
         (month 1)
         (date 1))
    ;; Find year using tagbody/go
    (block find-year
      (tagbody
       year-loop
        (setq leap (and (zerop (mod year 4))
                        (or (not (zerop (mod year 100)))
                            (zerop (mod year 400)))))
        (let ((days-in-year (if leap 366 365)))
          (when (< remaining-days days-in-year)
            (return-from find-year))
          (setq remaining-days (- remaining-days days-in-year))
          (setq year (+ year 1)))
        (go year-loop)))
    ;; Find month (remaining-days is 0-based day within year)
    (let ((month-days (if leap
                          '(31 29 31 30 31 30 31 31 30 31 30 31)
                          '(31 28 31 30 31 30 31 31 30 31 30 31))))
      (dolist (md month-days)
        (when (< remaining-days md)
          (setq date (+ remaining-days 1))
          (return))
        (setq remaining-days (- remaining-days md))
        (setq month (+ month 1))))
    (values second minute hour date month year dow nil tz)))

(defun encode-universal-time (second minute hour date month year &optional time-zone)
  "Encode time components into universal time."
  (let* ((tz (or time-zone 0))
         ;; Count days from 1900-01-01 to year-01-01
         (y (- year 1900))
         (total-days (+ (* y 365)
                        (truncate (+ y 3) 4)        ; leap years (every 4)
                        (- (truncate (+ y 99) 100)) ; minus century years
                        (truncate (+ y 399) 400)))   ; plus 400-year cycles
         ;; Add days for months within the year
         (leap (and (zerop (mod year 4))
                    (or (not (zerop (mod year 100)))
                        (zerop (mod year 400)))))
         (month-days (if leap
                         '(0 31 60 91 121 152 182 213 244 274 305 335)
                         '(0 31 59 90 120 151 181 212 243 273 304 334))))
    (setq total-days (+ total-days (nth (- month 1) month-days) (- date 1)))
    (+ (* total-days 86400) (* hour 3600) (* minute 60) second (* tz 3600))))

(defun get-decoded-time ()
  "Return the current time decoded into 9 values."
  (decode-universal-time (get-universal-time)))

;; --- Phase 8 Step 4: Higher-order functions ---
(defun complement (fn)
  "Return a function that returns the opposite of FN."
  (lambda (&rest args) (not (apply fn args))))

(defun constantly (value)
  "Return a function that always returns VALUE."
  (lambda (&rest args) value))

;; --- Phase 8 Step 1: Missing list operations ---

(defun tree-equal (a b &key (test #'eql test-supplied-p)
                              (test-not nil test-not-supplied-p))
  "Compare two trees recursively using TEST (or TEST-NOT).
Per CLHS, x and y are tree-equal iff: both are conses with tree-equal
car and cdr, OR both are atoms and the test is satisfied."
  (when (and test-supplied-p test-not-supplied-p)
    (error "TREE-EQUAL: cannot supply both :TEST and :TEST-NOT"))
  (let ((pred (if test-not-supplied-p
                  (lambda (x y) (not (funcall test-not x y)))
                  test)))
    (labels ((walk (a b)
               (cond
                 ((and (consp a) (consp b))
                  (and (walk (car a) (car b))
                       (walk (cdr a) (cdr b))))
                 ((or (consp a) (consp b)) nil)
                 (t (and (funcall pred a b) t)))))
      (walk a b))))

(defun list-length (list)
  "Return the length of LIST, or NIL if circular (tortoise-and-hare)."
  (do ((n 0 (+ n 2))
       (slow list (cdr slow))
       (fast list))
      (nil)
    (when (null fast) (return n))
    (setq fast (cdr fast))
    (when (null fast) (return (+ n 1)))
    (setq fast (cdr fast))
    (when (eq slow fast) (return nil))))

(defun tailp (object list)
  "Return true if OBJECT is EQL to LIST or any CDR of LIST."
  (do ((l list (cdr l)))
      ((atom l) (eql object l))
    (when (eql object l) (return t))))

(defun ldiff (list object)
  "Return a copy of LIST up to but not including the tail OBJECT.
If OBJECT is not EQL to any tail of LIST, a copy of LIST is returned —
including any dotted-list terminator (so the result is also dotted)."
  (unless (listp list)
    (error 'type-error :datum list :expected-type 'list))
  (do ((l list (cdr l))
       (result nil))
      (nil)
    (cond ((atom l)
           (return (if (eql l object)
                       (nreverse result)
                       (nreconc result l))))
          ((eql l object)
           (return (nreverse result)))
          (t (push (car l) result)))))

(defun revappend (list tail)
  "Non-destructively reverse LIST and append TAIL."
  (do ((l list (cdr l))
       (result tail (cons (car l) result)))
      ((null l) result)))

(defun nreconc (list tail)
  "Destructively reverse LIST and append TAIL."
  (do ((l list)
       (next nil))
      ((null l) tail)
    (setq next (cdr l))
    (rplacd l tail)
    (setq tail l)
    (setq l next)))

(defun assoc-if (predicate alist &key key)
  "Return first pair in ALIST where PREDICATE is true of the key."
  (dolist (pair alist nil)
    (cond ((null pair))
          ((consp pair)
           (when (funcall predicate
                          (if key (funcall key (car pair)) (car pair)))
             (return pair)))
          (t (error 'type-error :datum pair :expected-type 'list)))))

(defun assoc-if-not (predicate alist &key key)
  "Return first pair in ALIST where PREDICATE is false of the key."
  (dolist (pair alist nil)
    (cond ((null pair))
          ((consp pair)
           (unless (funcall predicate
                            (if key (funcall key (car pair)) (car pair)))
             (return pair)))
          (t (error 'type-error :datum pair :expected-type 'list)))))

(defun rassoc-if (predicate alist &key key)
  "Return first pair in ALIST where PREDICATE is true of the value."
  (dolist (pair alist nil)
    (cond ((null pair))
          ((consp pair)
           (when (funcall predicate
                          (if key (funcall key (cdr pair)) (cdr pair)))
             (return pair)))
          (t (error 'type-error :datum pair :expected-type 'list)))))

(defun rassoc-if-not (predicate alist &key key)
  "Return first pair in ALIST where PREDICATE is false of the value."
  (dolist (pair alist nil)
    (cond ((null pair))
          ((consp pair)
           (unless (funcall predicate
                            (if key (funcall key (cdr pair)) (cdr pair)))
             (return pair)))
          (t (error 'type-error :datum pair :expected-type 'list)))))

;; --- make-sequence ---
(defun make-sequence (type size &key (initial-element nil ie-p))
  "Create a sequence of the given TYPE and SIZE."
  (let ((tname (if (symbolp type) (symbol-name type) "")))
    (cond
      ((or (string= tname "LIST"))
       (if ie-p
           (make-list size :initial-element initial-element)
           (make-list size)))
      ((or (string= tname "VECTOR")
           (string= tname "SIMPLE-VECTOR")
           (string= tname "ARRAY"))
       (if ie-p
           (make-array size :initial-element initial-element)
           (make-array size)))
      ((or (string= tname "STRING")
           (string= tname "SIMPLE-STRING")
           (string= tname "BASE-STRING")
           (string= tname "SIMPLE-BASE-STRING"))
       (if ie-p
           (make-string size :initial-element initial-element)
           (make-string size)))
      ;; Handle (vector element-type) and (simple-array element-type (size)) compound specs
      ((and (consp type)
            (let ((head (symbol-name (car type))))
              (or (string= head "VECTOR") (string= head "SIMPLE-VECTOR")
                  (string= head "ARRAY") (string= head "SIMPLE-ARRAY"))))
       (if ie-p
           (make-array size :initial-element initial-element)
           (make-array size)))
      (t (error "MAKE-SEQUENCE: unsupported type ~S" type)))))

;; --- Pathname functions (Step 10) ---
;; Core pathname functions (pathname, pathnamep, parse-namestring, namestring,
;; make-pathname, merge-pathnames, pathname-host/device/directory/name/type/version,
;; file-namestring, directory-namestring, enough-namestring) are C builtins.
;; truename is now a C builtin (resolves symlinks via platform_realpath).

(defun translate-logical-pathname (pathspec &rest keys)
  (declare (ignore keys))
  (pathname pathspec))

(defun logical-pathname-translations (host)
  (declare (ignore host))
  nil)

(defun %ensure-dirs-helper (dir pos len created)
  "Helper for ensure-directories-exist. Walks dir string creating directories."
  (if (>= pos len)
      created
      (let ((next-sep nil))
        (do ((i pos (+ i 1)))
            ((>= i len))
          (when (or (char= (char dir i) #\/)
                    (char= (char dir i) #\:))
            (setq next-sep (+ i 1))
            (return)))
        (if next-sep
            (progn
              (when (char= (char dir (- next-sep 1)) #\/)
                (let ((sub (subseq dir 0 next-sep)))
                  (when (%mkdir sub) (setq created t))))
              (%ensure-dirs-helper dir next-sep len created))
            created))))

(defun ensure-directories-exist (pathname)
  "Create all directories in the path of PATHNAME. Returns pathname and created-p."
  (let* ((dir (directory-namestring pathname))
         (created (%ensure-dirs-helper dir 0 (length dir) nil)))
    (values pathname created)))

;; --- LOOP macro (Phase 8) ---

(defun %loop-keyword-p (sym name)
  "Check if SYM is a symbol whose name matches NAME."
  (and (symbolp sym)
       (string= (symbol-name sym) name)))

(defun %loop-keyword-sym-p (sym)
  "Check if SYM is any recognized loop keyword."
  (and (symbolp sym)
       (let ((n (symbol-name sym)))
         (or (string= n "FOR") (string= n "AS")
             (string= n "WITH") (string= n "DO") (string= n "DOING")
             (string= n "COLLECT") (string= n "COLLECTING")
             (string= n "APPEND") (string= n "APPENDING")
             (string= n "NCONC") (string= n "NCONCING")
             (string= n "SUM") (string= n "SUMMING")
             (string= n "COUNT") (string= n "COUNTING")
             (string= n "MAXIMIZE") (string= n "MAXIMIZING")
             (string= n "MINIMIZE") (string= n "MINIMIZING")
             (string= n "WHILE") (string= n "UNTIL")
             (string= n "REPEAT")
             (string= n "ALWAYS") (string= n "NEVER") (string= n "THEREIS")
             (string= n "WHEN") (string= n "IF") (string= n "UNLESS")
             (string= n "RETURN")
             (string= n "NAMED") (string= n "INITIALLY") (string= n "FINALLY")
             (string= n "IN") (string= n "ON") (string= n "BY")
             (string= n "FROM") (string= n "DOWNFROM") (string= n "UPFROM")
             (string= n "TO") (string= n "BELOW") (string= n "ABOVE")
             (string= n "DOWNTO") (string= n "UPTO")
             (string= n "ACROSS") (string= n "THEN")
             (string= n "INTO") (string= n "BEING") (string= n "EACH")
             (string= n "THE") (string= n "OF") (string= n "USING")
             (string= n "HASH-KEY") (string= n "HASH-KEYS")
             (string= n "HASH-VALUE") (string= n "HASH-VALUES")
             (string= n "SYMBOL") (string= n "SYMBOLS")
             (string= n "PRESENT-SYMBOL") (string= n "PRESENT-SYMBOLS")
             (string= n "EXTERNAL-SYMBOL") (string= n "EXTERNAL-SYMBOLS")
             (string= n "AND") (string= n "ELSE") (string= n "END")))))

(defun %loop-simple-type-spec-p (x)
  "CLHS 6.1.1.7: a bare (no OF-TYPE) LOOP type-spec must be one of the
simple type specifiers FIXNUM, FLOAT, T or NIL."
  (and (symbolp x)
       (let ((n (symbol-name x)))
         (or (string= n "FIXNUM") (string= n "FLOAT")
             (string= n "T") (string= n "NIL")))))

(defun %loop-skip-type-spec (rest)
  "Consume an optional LOOP type-spec at the head of REST and return the
remaining list.  A type-spec is either OF-TYPE followed by a single
d-type-spec form, or a bare simple-type-spec (FIXNUM/FLOAT/T/NIL).  The
type is purely advisory here, so it is discarded once parsed."
  (cond ((null rest) rest)
        ((%loop-keyword-p (car rest) "OF-TYPE") (cddr rest))
        ((%loop-simple-type-spec-p (car rest)) (cdr rest))
        (t rest)))

(defun %loop-list-accum-p (kn)
  "True if accumulation kind KN builds a list (COLLECT/NCONC/APPEND)."
  (or (string= kn "COLLECT") (string= kn "COLLECTING")
      (string= kn "NCONC") (string= kn "NCONCING")
      (string= kn "APPEND") (string= kn "APPENDING")))

(defun %loop-accum-body (kn expr accum-var)
  "Generate the body form for accumulation kind KN.

List accumulators (COLLECT/NCONC/APPEND) all build the accumulator in
REVERSED order (newest element first) so that a single NREVERSE at loop
end yields the correct order.  This is what lets COLLECT, NCONC and
APPEND be freely mixed into the same accumulation (CLHS 6.1.3) — e.g.
`(loop ... if x nconc l else collect e)`.  COLLECT pushes one element;
NCONC reverse-splices the list destructively (NRECONC — the list may be
destroyed, like NCONC); APPEND reverse-copies it (REVAPPEND — no
destruction, like APPEND).  The finalizer (%loop-list-accum-p) emits the
matching NREVERSE."
  (cond
    ((or (string= kn "COLLECT") (string= kn "COLLECTING"))
     `(push ,expr ,accum-var))
    ((or (string= kn "SUM") (string= kn "SUMMING"))
     `(setq ,accum-var (+ ,accum-var ,expr)))
    ((or (string= kn "COUNT") (string= kn "COUNTING"))
     `(when ,expr (setq ,accum-var (+ ,accum-var 1))))
    ((or (string= kn "MAXIMIZE") (string= kn "MAXIMIZING"))
     (let ((tmp (gensym "V")))
       `(let ((,tmp ,expr))
          (when (or (null ,accum-var) (> ,tmp ,accum-var))
            (setq ,accum-var ,tmp)))))
    ((or (string= kn "MINIMIZE") (string= kn "MINIMIZING"))
     (let ((tmp (gensym "V")))
       `(let ((,tmp ,expr))
          (when (or (null ,accum-var) (< ,tmp ,accum-var))
            (setq ,accum-var ,tmp)))))
    ((or (string= kn "APPEND") (string= kn "APPENDING"))
     `(setq ,accum-var (revappend ,expr ,accum-var)))
    (t
     `(setq ,accum-var (nreconc ,expr ,accum-var)))))

(defun %loop-accum-keyword-p (sym)
  "Check if SYM is an accumulation keyword."
  (and (symbolp sym)
       (let ((n (symbol-name sym)))
         (or (string= n "COLLECT") (string= n "COLLECTING")
             (string= n "APPEND") (string= n "APPENDING")
             (string= n "NCONC") (string= n "NCONCING")
             (string= n "SUM") (string= n "SUMMING")
             (string= n "COUNT") (string= n "COUNTING")
             (string= n "MAXIMIZE") (string= n "MAXIMIZING")
             (string= n "MINIMIZE") (string= n "MINIMIZING")))))

(defun %expand-simple-loop (forms)
  "Expand simple (loop body...) into block/tagbody."
  (let ((tag (gensym "LOOP")))
    `(block nil
       (tagbody
         ,tag
         ,@forms
         (go ,tag)))))

(defun %loop-destructure-vars (pattern)
  "Extract all variable names from a destructuring pattern."
  (cond
    ((null pattern) nil)
    ((symbolp pattern) (list pattern))
    ((consp pattern)
     (append (%loop-destructure-vars (car pattern))
             (%loop-destructure-vars (cdr pattern))))
    (t nil)))

(defun %loop-destructure-assigns (pattern source)
  "Generate setq forms to destructure SOURCE into PATTERN variables."
  (cond
    ((null pattern) nil)
    ((symbolp pattern) (list `(setq ,pattern ,source)))
    ((consp pattern)
     (append (%loop-destructure-assigns (car pattern) `(car ,source))
             (%loop-destructure-assigns (cdr pattern) `(cdr ,source))))
    (t nil)))

(defun %loop-destructure-bindings (pattern source)
  "Generate (var init) binding forms to destructure SOURCE into PATTERN variables."
  (cond
    ((null pattern) nil)
    ((symbolp pattern) (list (list pattern source)))
    ((consp pattern)
     (append (%loop-destructure-bindings (car pattern) `(car ,source))
             (%loop-destructure-bindings (cdr pattern) `(cdr ,source))))
    (t nil)))

(defun %loop-parse-for (rest var sub-kw end-tag)
  "Parse a FOR/AS clause. Returns (new-rest bindings end-tests pre-body steps)."
  (cond
    ;; FOR var IN list [BY step-fn]
    ((%loop-keyword-p sub-kw "IN")
     (let ((list-expr (car rest))
           (iter-gs (gensym "ITER"))
           (by-fn nil))
       (setq rest (cdr rest))
       (when (and rest (%loop-keyword-p (car rest) "BY"))
         (setq rest (cdr rest))
         (setq by-fn (car rest))
         (setq rest (cdr rest)))
       (list rest
             (list (list iter-gs list-expr) (list var nil))
             (list `(when (endp ,iter-gs) (go ,end-tag)))
             (list `(setq ,var (car ,iter-gs)))
             (list (if by-fn
                       `(setq ,iter-gs (funcall ,by-fn ,iter-gs))
                       `(setq ,iter-gs (cdr ,iter-gs)))))))
    ;; FOR var ON list [BY step-fn]
    ((%loop-keyword-p sub-kw "ON")
     (let ((list-expr (car rest))
           (by-fn nil))
       (setq rest (cdr rest))
       (when (and rest (%loop-keyword-p (car rest) "BY"))
         (setq rest (cdr rest))
         (setq by-fn (car rest))
         (setq rest (cdr rest)))
       (list rest
             (list (list var list-expr))
             (list `(when (endp ,var) (go ,end-tag)))
             nil
             (list (if by-fn
                       `(setq ,var (funcall ,by-fn ,var))
                       `(setq ,var (cdr ,var)))))))
    ;; FOR var ACROSS vector
    ((%loop-keyword-p sub-kw "ACROSS")
     (let ((vec-expr (car rest))
           (vec-gs (gensym "VEC"))
           (len-gs (gensym "LEN"))
           (idx-gs (gensym "IDX")))
       (setq rest (cdr rest))
       (list rest
             (list (list vec-gs vec-expr) (list len-gs `(length ,vec-gs))
                   (list idx-gs 0) (list var nil))
             (list `(when (>= ,idx-gs ,len-gs) (go ,end-tag)))
             (list `(setq ,var (elt ,vec-gs ,idx-gs)))
             (list `(setq ,idx-gs (+ ,idx-gs 1))))))
    ;; FOR var = expr [THEN step-expr]
    ((and (symbolp sub-kw) (string= (symbol-name sub-kw) "="))
     (let ((init-expr (car rest))
           (step-expr nil)
           (has-then nil))
       (setq rest (cdr rest))
       (when (and rest (%loop-keyword-p (car rest) "THEN"))
         (setq rest (cdr rest))
         (setq step-expr (car rest))
         (setq has-then t)
         (setq rest (cdr rest)))
       (if has-then
           ;; WITH THEN: first iteration uses init-expr, subsequent use step-expr
           ;; Assignment must be in preamble (before body), not steps (after body)
           (let ((flag (gensym "FIRST")))
             (list rest
                   (list (list var nil) (list flag t))
                   (list `(if ,flag
                              (progn (setq ,var ,init-expr) (setq ,flag nil))
                              (setq ,var ,step-expr)))
                   nil
                   nil))
           ;; WITHOUT THEN: re-evaluate expr each iteration
           ;; Assignment in preamble so variable is set before body runs
           (list rest
                 (list (list var nil))
                 (list `(setq ,var ,init-expr))
                 nil
                 nil))))
    ;; FOR var FROM/UPFROM/DOWNFROM start ...
    ((or (%loop-keyword-p sub-kw "FROM")
         (%loop-keyword-p sub-kw "UPFROM")
         (%loop-keyword-p sub-kw "DOWNFROM"))
     (let ((start-expr (car rest))
           (dir (if (%loop-keyword-p sub-kw "DOWNFROM") :down :up))
           (end-expr nil) (end-gs nil) (cmp-fn nil)
           (step-val 1))
       (setq rest (cdr rest))
       (block parse-num-opts
         (tagbody
           num-opt-next
           (when (null rest) (return-from parse-num-opts))
           (cond
             ((or (%loop-keyword-p (car rest) "TO")
                  (%loop-keyword-p (car rest) "UPTO"))
              (setq rest (cdr rest))
              (setq end-expr (car rest))
              (setq rest (cdr rest))
              (setq cmp-fn (if (eq dir :up) '> '<)))
             ((%loop-keyword-p (car rest) "DOWNTO")
              (setq rest (cdr rest))
              (setq end-expr (car rest))
              (setq rest (cdr rest))
              (setq dir :down)
              (setq cmp-fn '<))
             ((%loop-keyword-p (car rest) "BELOW")
              (setq rest (cdr rest))
              (setq end-expr (car rest))
              (setq rest (cdr rest))
              (setq cmp-fn '>=))
             ((%loop-keyword-p (car rest) "ABOVE")
              (setq rest (cdr rest))
              (setq end-expr (car rest))
              (setq rest (cdr rest))
              (setq dir :down)
              (setq cmp-fn '<=))
             ((%loop-keyword-p (car rest) "BY")
              (setq rest (cdr rest))
              (setq step-val (car rest))
              (setq rest (cdr rest)))
             (t (return-from parse-num-opts)))
           (go num-opt-next)))
       (let ((binds (list (list var start-expr)))
             (tests nil))
         (when end-expr
           (setq end-gs (gensym "END"))
           (push (list end-gs end-expr) binds)
           (setq tests (list `(when (,cmp-fn ,var ,end-gs) (go ,end-tag)))))
         (list rest binds tests nil
               (list (if (eq dir :down)
                         `(setq ,var (- ,var ,step-val))
                         `(setq ,var (+ ,var ,step-val))))))))
    ;; FOR var BEING {the|each} {hash-key[s]|hash-value[s]} {of|in} ht [using (...)]
    ;; FOR var BEING {the|each} {symbol[s]|present-symbol[s]|external-symbol[s]} {of|in} pkg
    ((%loop-keyword-p sub-kw "BEING")
     (let ((what nil)
           (using-var nil))
       ;; Skip optional the/each
       (when (and rest (symbolp (car rest))
                  (let ((n (symbol-name (car rest))))
                    (or (string= n "THE") (string= n "EACH"))))
         (setq rest (cdr rest)))
       ;; Parse iteration kind
       (when (null rest) (error "LOOP: missing BEING iteration kind"))
       (let ((n (if (symbolp (car rest)) (symbol-name (car rest)) "")))
         (cond
           ((or (string= n "HASH-KEY") (string= n "HASH-KEYS"))
            (setq what "HASH-KEYS"))
           ((or (string= n "HASH-VALUE") (string= n "HASH-VALUES"))
            (setq what "HASH-VALUES"))
           ((or (string= n "SYMBOL") (string= n "SYMBOLS"))
            (setq what "SYMBOLS"))
           ((or (string= n "PRESENT-SYMBOL") (string= n "PRESENT-SYMBOLS"))
            (setq what "PRESENT-SYMBOLS"))
           ((or (string= n "EXTERNAL-SYMBOL") (string= n "EXTERNAL-SYMBOLS"))
            (setq what "EXTERNAL-SYMBOLS"))
           (t (error "LOOP: unrecognized BEING type ~S" (car rest)))))
       (setq rest (cdr rest))
       ;; Skip of/in
       (when (and rest (symbolp (car rest))
                  (let ((n (symbol-name (car rest))))
                    (or (string= n "OF") (string= n "IN"))))
         (setq rest (cdr rest)))
       ;; Source expression
       (let ((src-expr (car rest)))
         (setq rest (cdr rest))
         ;; Check for USING clause (hash tables only)
         (when (and rest (%loop-keyword-p (car rest) "USING"))
           (setq rest (cdr rest))
           (let ((spec (car rest)))
             (setq rest (cdr rest))
             (setq using-var (cadr spec))))
         ;; Generate expansion
         (cond
           ;; Hash table iteration
           ((or (string= what "HASH-KEYS") (string= what "HASH-VALUES"))
            (let* ((iter-gs (gensym "ITER"))
                   (is-keys (string= what "HASH-KEYS"))
                   (binds (list (list iter-gs `(%hash-table-pairs ,src-expr))
                                (list var nil)))
                   (pre (list `(setq ,var (,(if is-keys 'caar 'cdar) ,iter-gs)))))
              (when using-var
                (setq binds (append binds (list (list using-var nil))))
                (setq pre (append pre
                                  (list `(setq ,using-var
                                               (,(if is-keys 'cdar 'caar) ,iter-gs))))))
              (list rest binds
                    (list `(when (endp ,iter-gs) (go ,end-tag)))
                    pre
                    (list `(setq ,iter-gs (cdr ,iter-gs))))))
           ;; Package symbol iteration
           (t
            (let* ((iter-gs (gensym "ITER"))
                   (fn (if (string= what "EXTERNAL-SYMBOLS")
                           '%package-external-symbols
                           '%package-symbols)))
              (list rest
                    (list (list iter-gs `(,fn (find-package ,src-expr)))
                          (list var nil))
                    (list `(when (endp ,iter-gs) (go ,end-tag)))
                    (list `(setq ,var (car ,iter-gs)))
                    (list `(setq ,iter-gs (cdr ,iter-gs))))))))))
    ;; FOR var BELOW limit — shorthand for FROM 0 BELOW limit
    ((%loop-keyword-p sub-kw "BELOW")
     (let ((end-expr (car rest))
           (end-gs (gensym "END")))
       (setq rest (cdr rest))
       (let ((step-val 1))
         (when (and rest (%loop-keyword-p (car rest) "BY"))
           (setq rest (cdr rest))
           (setq step-val (car rest))
           (setq rest (cdr rest)))
         (list rest
               (list (list var 0) (list end-gs end-expr))
               (list `(when (>= ,var ,end-gs) (go ,end-tag)))
               nil
               (list `(setq ,var (+ ,var ,step-val)))))))
    ;; FOR var ABOVE limit — shorthand for downward from some start
    ((%loop-keyword-p sub-kw "ABOVE")
     (let ((end-expr (car rest))
           (end-gs (gensym "END")))
       (setq rest (cdr rest))
       (let ((step-val 1))
         (when (and rest (%loop-keyword-p (car rest) "BY"))
           (setq rest (cdr rest))
           (setq step-val (car rest))
           (setq rest (cdr rest)))
         (list rest
               (list (list var 0) (list end-gs end-expr))
               (list `(when (<= ,var ,end-gs) (go ,end-tag)))
               nil
               (list `(setq ,var (- ,var ,step-val)))))))
    ;; FOR var TO/UPTO limit — shorthand for FROM 0 TO limit (inclusive)
    ;; Extension: ANSI LOOP (HyperSpec 6.1.2.1.1.2) requires FROM/UPFROM/DOWNFROM as the
    ;; mandatory start keyword; TO/UPTO/DOWNTO as the *first* keyword is an
    ;; implementation-defined extension (also accepted by SBCL and CCL).
    ((or (%loop-keyword-p sub-kw "TO")
         (%loop-keyword-p sub-kw "UPTO"))
     (let ((end-expr (car rest))
           (end-gs (gensym "END")))
       (setq rest (cdr rest))
       (let ((step-val 1))
         (when (and rest (%loop-keyword-p (car rest) "BY"))
           (setq rest (cdr rest))
           (setq step-val (car rest))
           (setq rest (cdr rest)))
         (list rest
               (list (list var 0) (list end-gs end-expr))
               (list `(when (> ,var ,end-gs) (go ,end-tag)))
               nil
               (list `(setq ,var (+ ,var ,step-val)))))))
    ;; FOR var DOWNTO limit — shorthand for FROM 0 DOWNTO limit (inclusive, downward)
    ;; Extension: same implementation-defined extension as TO/UPTO above.
    ((%loop-keyword-p sub-kw "DOWNTO")
     (let ((end-expr (car rest))
           (end-gs (gensym "END")))
       (setq rest (cdr rest))
       (let ((step-val 1))
         (when (and rest (%loop-keyword-p (car rest) "BY"))
           (setq rest (cdr rest))
           (setq step-val (car rest))
           (setq rest (cdr rest)))
         (list rest
               (list (list var 0) (list end-gs end-expr))
               (list `(when (< ,var ,end-gs) (go ,end-tag)))
               nil
               (list `(setq ,var (- ,var ,step-val)))))))
    ;; FOR var OF-TYPE type-spec <sub-clause> — skip type declaration, recurse
    ((%loop-keyword-p sub-kw "OF-TYPE")
     ;; rest = (type-spec real-sub-kw . rest-of-clause)
     ;; Skip the type-spec, delegate to real sub-clause
     (let ((real-sub-kw (cadr rest))
           (real-rest (cddr rest)))
       (%loop-parse-for real-rest var real-sub-kw end-tag)))
    (t
     ;; sub-kw might be an inline type declaration (e.g., FIXNUM in
     ;; "FOR I FIXNUM FROM 0").  Per CL spec, a type-spec can appear
     ;; between the variable and the sub-clause keyword.  Skip it and
     ;; use the next element as the real sub-keyword.
     (let ((next (car rest)))
       (if (and rest (symbolp next)
                (or (%loop-keyword-p next "IN")
                    (%loop-keyword-p next "ON")
                    (%loop-keyword-p next "ACROSS")
                    (%loop-keyword-p next "FROM")
                    (%loop-keyword-p next "UPFROM")
                    (%loop-keyword-p next "DOWNFROM")
                    (%loop-keyword-p next "TO")
                    (%loop-keyword-p next "UPTO")
                    (%loop-keyword-p next "DOWNTO")
                    (%loop-keyword-p next "BELOW")
                    (%loop-keyword-p next "ABOVE")
                    (%loop-keyword-p next "BEING")
                    (and (symbolp next) (string= (symbol-name next) "="))))
           (%loop-parse-for (cdr rest) var next end-tag)
           (error "LOOP: unrecognized FOR sub-clause ~S" sub-kw))))))

(defun %loop-parse-cond-subclause (sub rest block-name
                                    bindings into-vars default-accum
                                    result-form epilogue)
  "Parse one conditional sub-clause. Returns (form rest bindings into-vars
   default-accum result-form epilogue)."
  (let ((form nil))
    (cond
      ;; DO — consume forms until keyword
      ((or (%loop-keyword-p sub "DO") (%loop-keyword-p sub "DOING"))
       (let ((do-forms nil))
         (block consume-cond-do
           (tagbody
             cond-do-next
             (when (null rest) (return-from consume-cond-do))
             (when (%loop-keyword-sym-p (car rest)) (return-from consume-cond-do))
             (push (car rest) do-forms)
             (setq rest (cdr rest))
             (go cond-do-next)))
         (setq form `(progn ,@(nreverse do-forms)))))
      ;; RETURN
      ((%loop-keyword-p sub "RETURN")
       (setq form `(return-from ,block-name ,(car rest)))
       (setq rest (cdr rest)))
      ;; Accumulation
      ((%loop-accum-keyword-p sub)
       (let* ((kn (symbol-name sub))
              (is-list-accum (%loop-list-accum-p kn))
              (is-sum (or (string= kn "SUM") (string= kn "SUMMING")))
              (is-count (or (string= kn "COUNT") (string= kn "COUNTING")))
              (init-val (if (or is-sum is-count) 0 nil))
              (expr (car rest))
              (accum-var nil))
         (setq rest (cdr rest))
         (if (and rest (%loop-keyword-p (car rest) "INTO"))
             (progn
               (setq rest (cdr rest))
               (setq accum-var (car rest))
               (setq rest (cdr rest))
               ;; CLHS 6.1.3.1: INTO var may be followed by OF-TYPE type-spec.
               (when (and rest (%loop-keyword-p (car rest) "OF-TYPE"))
                 (setq rest (cddr rest)))
               (unless (member accum-var into-vars)
                 (push accum-var into-vars)
                 (push (list accum-var init-val) bindings)
                 (when is-list-accum
                   (push `(setq ,accum-var (nreverse ,accum-var)) epilogue))))
             (progn
               (unless default-accum
                 (setq default-accum (gensym "ACC"))
                 (push (list default-accum init-val) bindings)
                 (setq result-form (if is-list-accum `(nreverse ,default-accum)
                                       default-accum)))
               (setq accum-var default-accum)))
         (setq form (%loop-accum-body kn expr accum-var))))
      ;; Nested IF/WHEN/UNLESS as sub-clause (standard LOOP syntax)
      ((or (%loop-keyword-p sub "IF") (%loop-keyword-p sub "WHEN")
           (%loop-keyword-p sub "UNLESS"))
       (let* ((inner-test (car rest))
              (negate (%loop-keyword-p sub "UNLESS"))
              (then-forms nil)
              (else-forms nil))
         (setq rest (cdr rest))
         ;; Parse then-branch sub-clause(s) connected by AND
         (block parse-inner-then
           (tagbody
             inner-then-sub
             (let* ((r (%loop-parse-cond-subclause
                        (car rest) (cdr rest) block-name
                        bindings into-vars default-accum
                        result-form epilogue)))
               (push (car r) then-forms)
               (setq rest (cadr r))
               (setq bindings (caddr r))
               (setq into-vars (car (cdddr r)))
               (setq default-accum (cadr (cdddr r)))
               (setq result-form (caddr (cdddr r)))
               (setq epilogue (car (cdddr (cdddr r)))))
             (when (and rest (%loop-keyword-p (car rest) "AND"))
               (setq rest (cdr rest))
               (go inner-then-sub))))
         ;; Check for ELSE branch
         (when (and rest (%loop-keyword-p (car rest) "ELSE"))
           (setq rest (cdr rest))
           (block parse-inner-else
             (tagbody
               inner-else-sub
               (let* ((r (%loop-parse-cond-subclause
                          (car rest) (cdr rest) block-name
                          bindings into-vars default-accum
                          result-form epilogue)))
                 (push (car r) else-forms)
                 (setq rest (cadr r))
                 (setq bindings (caddr r))
                 (setq into-vars (car (cdddr r)))
                 (setq default-accum (cadr (cdddr r)))
                 (setq result-form (caddr (cdddr r)))
                 (setq epilogue (car (cdddr (cdddr r)))))
               (when (and rest (%loop-keyword-p (car rest) "AND"))
                 (setq rest (cdr rest))
                 (go inner-else-sub)))))
         ;; Consume optional END
         (when (and rest (%loop-keyword-p (car rest) "END"))
           (setq rest (cdr rest)))
         ;; Build the form
         (when negate (setq inner-test `(not ,inner-test)))
         (let ((then-body (if (cdr then-forms)
                              `(progn ,@(nreverse then-forms))
                              (car then-forms)))
               (else-body (when else-forms
                            (if (cdr else-forms)
                                `(progn ,@(nreverse else-forms))
                                (car else-forms)))))
           (setq form (if else-body
                          `(if ,inner-test ,then-body ,else-body)
                          `(when ,inner-test ,then-body))))))
      (t (error "LOOP: invalid WHEN/IF sub-clause ~S" sub)))
    (list form rest bindings into-vars default-accum result-form epilogue)))

(defun %expand-extended-loop (forms)
  "Expand extended loop with keywords into block/let*/tagbody."
  (let* ((rest forms)
         (block-name nil)
         (bindings nil)
         (preamble nil)
         (body nil)
         (steps nil)
         (epilogue nil)
         (finally-forms nil)
         (initially-forms nil)
         (result-form nil)
         (default-accum nil)
         (into-vars nil)
         (loop-tag (gensym "LOOP"))
         (end-tag (gensym "END")))
    ;; Parser loop
    (block parse-done
      (tagbody
        parse-next
        (when (null rest) (return-from parse-done))
        (let ((kw (car rest)))
          (setq rest (cdr rest))
          (cond
            ;; WHILE — termination test at the point where it appears (CLHS 6.1.4)
            ((%loop-keyword-p kw "WHILE")
             (push `(unless ,(car rest) (go ,end-tag)) body)
             (setq rest (cdr rest)))
            ;; UNTIL — termination test at the point where it appears (CLHS 6.1.4)
            ((%loop-keyword-p kw "UNTIL")
             (push `(when ,(car rest) (go ,end-tag)) body)
             (setq rest (cdr rest)))
            ;; DO / DOING
            ((or (%loop-keyword-p kw "DO") (%loop-keyword-p kw "DOING"))
             (block consume-do
               (tagbody
                 do-next
                 (when (null rest) (return-from consume-do))
                 (when (%loop-keyword-sym-p (car rest)) (return-from consume-do))
                 (push (car rest) body)
                 (setq rest (cdr rest))
                 (go do-next))))
            ;; REPEAT
            ((%loop-keyword-p kw "REPEAT")
             (let ((ctr (gensym "REP")))
               (push (list ctr (car rest)) bindings)
               (setq rest (cdr rest))
               (push `(when (<= ,ctr 0) (go ,end-tag)) preamble)
               (push `(setq ,ctr (- ,ctr 1)) steps)))
            ;; FOR / AS [var spec] { AND [var spec] }* — parallel iteration.
            ;; CLHS 6.1.2.1: AND introduces another variable whose binding,
            ;; end-test, and stepping happen in parallel with the preceding
            ;; FOR.  For our simple expansion model each sub-clause's
            ;; bindings / preamble / steps are merged at the same level,
            ;; which matches parallel semantics for the iteration forms we
            ;; support (IN, ON, FROM/TO/BY, =, = THEN).
            ((or (%loop-keyword-p kw "FOR") (%loop-keyword-p kw "AS"))
             (block parse-for-and
               (tagbody
                 for-and-next
                 (let* ((raw-var (car rest))
                        (destructuring (consp raw-var))
                        (var (if destructuring (gensym "DVAR") raw-var))
                        ;; CLHS 6.1.2.1: an optional type-spec may follow
                        ;; the variable, e.g. (loop for i fixnum from 0 ...).
                        (after-var (%loop-skip-type-spec (cdr rest)))
                        (sub-kw (car after-var))
                        (r (%loop-parse-for (cdr after-var) var sub-kw end-tag)))
                   (setq rest (car r))
                   (dolist (b (cadr r)) (push b bindings))
                   (when destructuring
                     (dolist (v (%loop-destructure-vars raw-var))
                       (push (list v nil) bindings)))
                   (dolist (e (caddr r)) (push e preamble))
                   (dolist (p (car (cdddr r))) (push p preamble))
                   (when destructuring
                     (dolist (a (%loop-destructure-assigns raw-var var))
                       (push a preamble)))
                   (dolist (s (cadr (cdddr r))) (push s steps)))
                 (when (and rest (%loop-keyword-p (car rest) "AND"))
                   (setq rest (cdr rest))
                   (go for-and-next)))))
            ;; RETURN
            ((%loop-keyword-p kw "RETURN")
             (push `(return-from ,block-name ,(car rest)) body)
             (setq rest (cdr rest)))
            ;; Accumulation (unified)
            ((%loop-accum-keyword-p kw)
             (let* ((kn (symbol-name kw))
                    (is-list-accum (%loop-list-accum-p kn))
                    (is-sum (or (string= kn "SUM") (string= kn "SUMMING")))
                    (is-count (or (string= kn "COUNT") (string= kn "COUNTING")))
                    (init-val (if (or is-sum is-count) 0 nil))
                    (expr (car rest))
                    (accum-var nil))
               (setq rest (cdr rest))
               (if (and rest (%loop-keyword-p (car rest) "INTO"))
                   (progn
                     (setq rest (cdr rest))
                     (setq accum-var (car rest))
                     (setq rest (cdr rest))
                     ;; CLHS 6.1.3.1: accumulation clauses accept an
                     ;; optional OF-TYPE type-spec after INTO var.  We
                     ;; don't use the type for optimization — just
                     ;; consume it so the clause parses.
                     (when (and rest (%loop-keyword-p (car rest) "OF-TYPE"))
                       (setq rest (cddr rest)))
                     (unless (member accum-var into-vars)
                       (push accum-var into-vars)
                       (push (list accum-var init-val) bindings)
                       (when is-list-accum
                         (push `(setq ,accum-var (nreverse ,accum-var)) epilogue))))
                   (progn
                     (unless default-accum
                       (setq default-accum (gensym "ACC"))
                       (push (list default-accum init-val) bindings)
                       (setq result-form (if is-list-accum `(nreverse ,default-accum)
                                             default-accum)))
                     (setq accum-var default-accum)))
               (push (%loop-accum-body kn expr accum-var) body)))
            ;; NAMED
            ((%loop-keyword-p kw "NAMED")
             (setq block-name (car rest))
             (setq rest (cdr rest)))
            ;; WITH var [= expr] {AND var [= expr]}*
            ((%loop-keyword-p kw "WITH")
             (block parse-with
               (tagbody
                 with-next
                 (let* ((raw-wvar (car rest))
                        (destructuring-w (consp raw-wvar))
                        (wvar (if destructuring-w (gensym "WDVAR") raw-wvar)))
                   (setq rest (cdr rest))
                   ;; CLHS 6.1.2.4: skip an optional type-spec
                   ;; (OF-TYPE ... or a bare FIXNUM/FLOAT/T/NIL) such as
                   ;; (loop with noctets fixnum = 0 ...).
                   (setq rest (%loop-skip-type-spec rest))
                   (if (and rest (symbolp (car rest))
                            (string= (symbol-name (car rest)) "="))
                       (progn
                         (setq rest (cdr rest))
                         (push (list wvar (car rest)) bindings)
                         (setq rest (cdr rest)))
                       (push (list wvar nil) bindings))
                   (when destructuring-w
                     (dolist (b (%loop-destructure-bindings raw-wvar wvar))
                       (push b bindings))))
                 (when (and rest (%loop-keyword-p (car rest) "AND"))
                   (setq rest (cdr rest))
                   (go with-next)))))
            ;; INITIALLY
            ((%loop-keyword-p kw "INITIALLY")
             (block consume-ini
               (tagbody
                 ini-next
                 (when (null rest) (return-from consume-ini))
                 (when (%loop-keyword-sym-p (car rest)) (return-from consume-ini))
                 (push (car rest) initially-forms)
                 (setq rest (cdr rest))
                 (go ini-next))))
            ;; FINALLY
            ((%loop-keyword-p kw "FINALLY")
             (block consume-fin
               (tagbody
                 fin-next
                 (when (null rest) (return-from consume-fin))
                 (when (%loop-keyword-sym-p (car rest)) (return-from consume-fin))
                 (push (car rest) finally-forms)
                 (setq rest (cdr rest))
                 (go fin-next))))
            ;; ALWAYS
            ((%loop-keyword-p kw "ALWAYS")
             (push `(unless ,(car rest) (return-from ,block-name nil)) body)
             (setq rest (cdr rest))
             (unless result-form (setq result-form t)))
            ;; NEVER
            ((%loop-keyword-p kw "NEVER")
             (push `(when ,(car rest) (return-from ,block-name nil)) body)
             (setq rest (cdr rest))
             (unless result-form (setq result-form t)))
            ;; THEREIS
            ((%loop-keyword-p kw "THEREIS")
             (let ((tmp (gensym "V")))
               (push `(let ((,tmp ,(car rest)))
                        (when ,tmp (return-from ,block-name ,tmp)))
                     body)
               (setq rest (cdr rest))))
            ;; WHEN / IF / UNLESS — conditional with sub-clauses
            ;; Handles flat :when ... :else :when ... chains iteratively
            ((or (%loop-keyword-p kw "WHEN") (%loop-keyword-p kw "IF")
                 (%loop-keyword-p kw "UNLESS"))
             (let ((cond-clauses nil)  ;; list of (test . then-forms) -- built in reverse
                   (final-else nil)
                   (cur-test (car rest))
                   (cur-negate (%loop-keyword-p kw "UNLESS"))
                   (cur-then nil)
                   (it-var (gensym "IT"))   ;; gensym for LOOP IT variable
                   (it-raw-test (car rest))) ;; save raw test before negation
               (setq rest (cdr rest))
               (block parse-cond-chain
                 (tagbody
                   parse-cond-sub
                   ;; Parse one sub-clause
                   (let* ((r (%loop-parse-cond-subclause
                              (car rest) (cdr rest) block-name
                              bindings into-vars default-accum
                              result-form epilogue)))
                     (push (car r) cur-then)
                     (setq rest (cadr r))
                     (setq bindings (caddr r))
                     (setq into-vars (car (cdddr r)))
                     (setq default-accum (cadr (cdddr r)))
                     (setq result-form (caddr (cdddr r)))
                     (setq epilogue (car (cdddr (cdddr r)))))
                   (cond
                     ;; AND — more sub-clauses for same branch
                     ((and rest (%loop-keyword-p (car rest) "AND"))
                      (setq rest (cdr rest))
                      (go parse-cond-sub))
                     ;; ELSE followed by WHEN/IF/UNLESS — new cond clause (iterative)
                     ((and rest (%loop-keyword-p (car rest) "ELSE")
                           (cdr rest)
                           (or (%loop-keyword-p (cadr rest) "WHEN")
                               (%loop-keyword-p (cadr rest) "IF")
                               (%loop-keyword-p (cadr rest) "UNLESS")))
                      ;; Save current clause
                      (when cur-negate (setq cur-test `(not ,cur-test)))
                      (push (cons cur-test (nreverse cur-then)) cond-clauses)
                      ;; Start new clause
                      (setq rest (cdr rest))  ;; skip :else
                      (setq cur-negate (%loop-keyword-p (car rest) "UNLESS"))
                      (setq rest (cdr rest))  ;; skip :when/:if/:unless
                      (setq cur-test (car rest))
                      (setq rest (cdr rest))  ;; skip test-expr
                      (setq cur-then nil)
                      (go parse-cond-sub))
                     ;; ELSE followed by non-conditional — final else branch
                     ((and rest (%loop-keyword-p (car rest) "ELSE"))
                      (setq rest (cdr rest))
                      ;; Save current clause
                      (when cur-negate (setq cur-test `(not ,cur-test)))
                      (push (cons cur-test (nreverse cur-then)) cond-clauses)
                      ;; Parse else sub-clauses
                      (setq cur-then nil)
                      (block parse-final-else
                        (tagbody
                          parse-else-sub
                          (let* ((r (%loop-parse-cond-subclause
                                     (car rest) (cdr rest) block-name
                                     bindings into-vars default-accum
                                     result-form epilogue)))
                            (push (car r) final-else)
                            (setq rest (cadr r))
                            (setq bindings (caddr r))
                            (setq into-vars (car (cdddr r)))
                            (setq default-accum (cadr (cdddr r)))
                            (setq result-form (caddr (cdddr r)))
                            (setq epilogue (car (cdddr (cdddr r)))))
                          (when (and rest (%loop-keyword-p (car rest) "AND"))
                            (setq rest (cdr rest))
                            (go parse-else-sub))))
                      (when (and rest (%loop-keyword-p (car rest) "END"))
                        (setq rest (cdr rest))))
                     ;; END — done
                     ((and rest (%loop-keyword-p (car rest) "END"))
                      (setq rest (cdr rest))
                      ;; Save current clause
                      (when cur-negate (setq cur-test `(not ,cur-test)))
                      (push (cons cur-test (nreverse cur-then)) cond-clauses))
                     ;; No more conditional tokens — done
                     (t
                      (when cur-negate (setq cur-test `(not ,cur-test)))
                      (push (cons cur-test (nreverse cur-then)) cond-clauses)))))
               ;; Build nested if/else from cond-clauses (reverse order) + final-else
               ;; Bind IT to the when-test value per CL spec (LOOP IT variable).
               ;; IT refers to the value of the test-form; we bind IT first, then
               ;; use it as the condition to avoid double evaluation.
               (let ((result (if final-else
                                 `(progn ,@(nreverse final-else))
                                 nil)))
                 (dolist (clause cond-clauses)
                   (let ((test (car clause))
                         (forms (cdr clause)))
                     (if result
                         (setq result `(if ,test (progn ,@forms) ,result))
                         (setq result `(when ,test ,@forms)))))
                 ;; Bind IT to the when-test value per CL spec (LOOP IT
                 ;; variable).  We use a gensym and substitute any reference
                 ;; to the symbol IT (by name) in the body forms.
                 (labels ((%subst-it (form)
                            (cond
                              ((and (symbolp form)
                                    (string= (symbol-name form) "IT"))
                               it-var)
                              ((consp form)
                               (cons (%subst-it (car form))
                                     (%subst-it (cdr form))))
                              (t form))))
                   (if (and (= (length cond-clauses) 1) (null final-else))
                       (let ((forms (%subst-it (cdar cond-clauses))))
                         (push `(let ((,it-var ,it-raw-test))
                                  ,(if cur-negate
                                       `(unless ,it-var ,@forms)
                                       `(when ,it-var ,@forms)))
                               body))
                       ;; Multi-clause: substitute IT and wrap
                       (push `(let ((,it-var ,it-raw-test))
                                ,(%subst-it result))
                             body))))))
            (t
             (error "LOOP: unrecognized clause ~S" kw))))
        (go parse-next)))
    ;; Build the expansion
    `(block ,block-name
       (let* ,(nreverse bindings)
         ,@(nreverse initially-forms)
         (macrolet ((loop-finish () (list 'go ',end-tag)))
           (tagbody
             ,loop-tag
             ,@(nreverse preamble)
             ,@(nreverse body)
             ,@(nreverse steps)
             (go ,loop-tag)
             ,end-tag))
         ,@(nreverse epilogue)
         ,@(nreverse finally-forms)
         ,result-form))))

(defmacro loop (&rest forms)
  "CL LOOP macro -- supports simple and extended forms."
  (if (or (null forms)
          (%loop-keyword-sym-p (car forms)))
      ;; Extended loop (starts with keyword or is empty)
      (%expand-extended-loop forms)
      ;; Simple loop (body forms only)
      (%expand-simple-loop forms)))

;; pprint-logical-block — structured pretty printing
(defmacro pprint-logical-block (header &body body)
  (let* ((stream-sym (car header))
         (list-form (cadr header))
         (keys (cddr header))
         (prefix (getf keys :prefix))
         (per-line-prefix (getf keys :per-line-prefix))
         (suffix (getf keys :suffix))
         (pfx (or prefix per-line-prefix))
         (glist (gensym "LIST")))
    `(let ((,glist ,list-form))
       (%pp-push-block)
       ,(when pfx `(write-string ,pfx ,stream-sym))
       (macrolet ((pprint-pop ()
                    (list 'prog1 (list 'car ',glist)
                          (list 'setq ',glist (list 'cdr ',glist))))
                  (pprint-exit-if-list-exhausted ()
                    (list 'when (list 'null ',glist) '(return nil))))
         (block nil ,@body))
       ,(when suffix `(write-string ,suffix ,stream-sym))
       (%pp-pop-block)
       nil)))

;; pprint-fill — print list elements separated by spaces (fill style)
(defun pprint-fill (stream list &optional (colon-p t) atsign-p)
  (declare (ignore atsign-p))
  (let ((s (or stream *standard-output*)))
    (when colon-p (write-char #\( s))
    (loop for tail on list
          do (write (car tail) :stream s)
          when (cdr tail) do (write-char #\Space s))
    (when colon-p (write-char #\) s))))

;; pprint-linear — print list elements separated by newlines (linear style)
(defun pprint-linear (stream list &optional (colon-p t) atsign-p)
  (declare (ignore atsign-p))
  (let ((s (or stream *standard-output*)))
    (when colon-p (write-char #\( s))
    (loop for tail on list
          do (write (car tail) :stream s)
          when (cdr tail) do (terpri s))
    (when colon-p (write-char #\) s))))

;;; ============================================================
;;; EXT package: defglobal
;;; ============================================================

(in-package :ext)

(defmacro defglobal (name value &optional doc)
  (declare (ignore doc))
  `(defvar ,name ,value))

(export '(defglobal))

;; Make socket read/write timeouts settable:
;;   (setf (ext:socket-stream-timeout stream :input)  seconds)
;;   (setf (ext:socket-stream-timeout stream :output) seconds)
;; The setter receives (stream direction new-value) per the defsetf short form.
(defsetf socket-stream-timeout %set-socket-stream-timeout)

;;; ============================================================
;;; MP package: threading macros and stubs
;;; ============================================================
;;; Real threading primitives are C builtins (make-thread, make-lock,
;;; acquire-lock, release-lock, etc.).  Convenience macros and stubs
;;; for recursive locks and memory barriers live here.

(in-package :mp)

;; Shadow these names so they are interned in MP, not inherited from CL
;; (cl_package_export_defined_cl_symbols would otherwise claim them)
(shadow '(with-lock-held make-recursive-lock with-recursive-lock-held
          read-memory-barrier write-memory-barrier))

(defmacro with-lock-held ((lock) &body body)
  (let ((l (gensym "LOCK")))
    `(let ((,l ,lock))
       (acquire-lock ,l t)
       (unwind-protect (progn ,@body)
         (release-lock ,l)))))

(defun make-recursive-lock (&optional name)
  (%make-recursive-lock name))

(defmacro with-recursive-lock-held ((lock) &body body)
  `(with-lock-held (,lock) ,@body))

(defun read-memory-barrier ()
  nil)

(defun write-memory-barrier ()
  nil)

(export '(with-lock-held make-recursive-lock
          with-recursive-lock-held
          read-memory-barrier write-memory-barrier))

(in-package :cl-user)

;; JITEXPAND — show the m68k assembly the JIT emitted for a form.
;;
;; Three accepted shapes:
;;
;;   (jitexpand (defun foo (x) x))     ; defines foo, then disassembles
;;   (jitexpand (lambda (x) x))        ; disassembles the lambda
;;   (jitexpand (+ x 1))               ; wraps in (lambda () ...) and
;;                                       disassembles — useful for
;;                                       inspecting an expression in
;;                                       isolation
;;
;; A leading QUOTE is unwrapped so all three also work as:
;;   (jitexpand '(defun foo ...))
;;   (jitexpand '(lambda ...))
;;   (jitexpand '(+ x 1))
;;
;; On host (no JIT_M68K) or for functions/forms the JIT didn't compile,
;; prints "(no native code ...)" and returns NIL.
;;
;; Note: this macro deliberately avoids CADR / SECOND / etc. — at the
;; time boot.lisp compiles, defining a top-level macro that references
;; CADR somehow poisons the CADR function binding for later code (a
;; compiler-macro interaction worth chasing separately).  CAR/CDR
;; combinations are safe.
(defmacro jitexpand (form)
  (let ((f form))
    (if (and (consp f) (eq (car f) 'quote) (consp (cdr f)))
        (setq f (car (cdr f))))
    (cond
      ;; (defun NAME ...) — install the function and disassemble by name.
      ((and (consp f) (eq (car f) 'defun)
            (consp (cdr f)) (symbolp (car (cdr f))))
       (let ((name (car (cdr f))))
         `(progn
            ,f
            (format t "; JIT disassembly of ~A:~%" ',name)
            (clamiga::%jit-disassemble (symbol-function ',name)))))
      ;; (lambda ...) — disassemble the lambda directly.
      ((and (consp f) (eq (car f) 'lambda))
       `(progn
          (format t "; JIT disassembly of ~S:~%" ',f)
          (clamiga::%jit-disassemble ,f)))
      ;; Any other expression — wrap as a thunk and disassemble.  The
      ;; thunk is never invoked, so free variables in F don't have to
      ;; be bound for the inspection to work.
      (t
       `(progn
          (format t "; JIT disassembly of (lambda () ~S):~%" ',f)
          (clamiga::%jit-disassemble (lambda () ,f)))))))
