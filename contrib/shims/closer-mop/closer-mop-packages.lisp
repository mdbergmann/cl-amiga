;;;; closer-mop-packages.lisp — package definitions for the CL-Amiga
;;;; closer-mop shim.
;;;;
;;;; On CL-Amiga (#+clamiga), MOP symbols live in the dedicated MOP
;;;; package (introduced when ANSI symbols were segregated from
;;;; impl-internal helpers).  The defpackage below therefore uses both
;;;; :COMMON-LISP and :MOP and re-exports those names under
;;;; :CLOSER-MOP (nickname :C2MOP), matching the package layout that
;;;; serapeum / trivia / lisp-namespace expect.
;;;;
;;;; For any non-clamiga host reaching this file, we leave the
;;;; canonical closer-mop package definition in place — callers that
;;;; want the real thing should load upstream closer-mop instead of
;;;; this shim.

(in-package :cl-user)

#+clamiga
(defpackage #:closer-mop
  (:use #:common-lisp #:mop)
  (:nicknames #:c2mop)
  (:export
   ;; --- Metaobject classes ---
   #:built-in-class
   #:class
   #:direct-slot-definition
   #:effective-slot-definition
   #:eql-specializer
   #:forward-referenced-class
   #:funcallable-standard-class
   #:funcallable-standard-object
   #:generic-function
   #:metaobject
   #:method
   #:method-combination
   #:slot-definition
   #:specializer
   #:standard-accessor-method
   #:standard-class
   #:standard-generic-function
   #:standard-direct-slot-definition
   #:standard-effective-slot-definition
   #:standard-method
   #:standard-object
   #:standard-reader-method
   #:standard-slot-definition
   #:standard-writer-method

   ;; --- Defining macros (just re-exports — we do not rewrite) ---
   #:defclass
   #:defgeneric
   #:define-method-combination
   #:defmethod

   ;; --- Predicates and convenience ---
   #:classp
   #:ensure-finalized
   #:ensure-method
   #:fix-slot-initargs
   #:required-args
   #:subclassp

   ;; --- Class introspection ---
   #:class-default-initargs
   #:class-direct-default-initargs
   #:class-direct-slots
   #:class-direct-subclasses
   #:class-direct-superclasses
   #:class-finalized-p
   #:class-precedence-list
   #:class-prototype
   #:class-slots

   ;; --- Class finalization protocol ---
   #:compute-class-precedence-list
   #:compute-default-initargs
   #:compute-effective-slot-definition
   #:compute-slots
   #:direct-slot-definition-class
   #:effective-slot-definition-class
   #:ensure-class
   #:ensure-class-using-class
   #:finalize-inheritance
   #:validate-superclass

   ;; --- Slot-definition accessors ---
   #:slot-definition-allocation
   #:slot-definition-initargs
   #:slot-definition-initform
   #:slot-definition-initfunction
   #:slot-definition-location
   #:slot-definition-name
   #:slot-definition-readers
   #:slot-definition-writers
   #:slot-definition-type

   ;; --- Slot-access protocol ---
   #:slot-boundp-using-class
   #:slot-makunbound-using-class
   #:slot-value-using-class
   #:standard-instance-access
   #:funcallable-standard-instance-access

   ;; --- EQL specializer ---
   #:eql-specializer-object
   #:intern-eql-specializer

   ;; --- Generic-function introspection ---
   #:compute-applicable-methods-using-classes
   #:compute-discriminating-function
   #:compute-effective-method
   #:compute-effective-method-function
   #:ensure-generic-function
   #:ensure-generic-function-using-class
   #:extract-lambda-list
   #:extract-specializer-names
   #:generic-function-argument-precedence-order
   #:generic-function-declarations
   #:generic-function-lambda-list
   #:generic-function-method-class
   #:generic-function-method-combination
   #:generic-function-methods
   #:generic-function-name

   ;; --- Method metaobject protocol ---
   #:add-method
   #:make-method-lambda
   #:method-function
   #:method-generic-function
   #:method-lambda-list
   #:method-specializers
   #:remove-method

   ;; --- Method combination ---
   #:find-method-combination

   ;; --- Specializer / direct-method back-links ---
   #:accessor-method-slot-definition
   #:add-direct-method
   #:add-direct-subclass
   #:reader-method-class
   #:remove-direct-method
   #:remove-direct-subclass
   #:specializer-direct-generic-functions
   #:specializer-direct-methods
   #:writer-method-class

   ;; --- Dependents API ---
   #:add-dependent
   #:map-dependents
   #:remove-dependent
   #:update-dependent

   ;; --- Type predicates (re-exported from CL for c2cl alias package) ---
   #:subtypep
   #:typep

   ;; --- Style-warning infrastructure ---
   #:warn-on-defmethod-without-generic-function))

;;; CLOSER-COMMON-LISP (nickname C2CL) — canonical closer-mop defines
;;; this as a superset of COMMON-LISP *plus* the MOP symbols, so that a
;;; package can (:use #:closer-common-lisp) in place of :common-lisp and
;;; pick up both the standard language and the MOP bindings.  Libraries
;;; like CL-MOCK do exactly that, so if c2cl is missing a CL symbol the
;;; downstream :use leaves core forms such as DEFMACRO unresolved.
;;;
;;; Since our :CLOSER-MOP package already uses :COMMON-LISP and :MOP,
;;; its external symbols are the union of CL + MOP — no package-level
;;; conflict when we (:use :common-lisp :closer-mop).  We then
;;; re-export every external of both packages at load time so
;;; downstream :use picks them up.
#+clamiga
(defpackage #:closer-common-lisp
  (:nicknames #:c2cl)
  (:use #:common-lisp #:closer-mop))

#+clamiga
(let ((c2cl (find-package :closer-common-lisp)))
  (do-external-symbols (sym :common-lisp)
    (export sym c2cl))
  (do-external-symbols (sym :closer-mop)
    (export sym c2cl)))

#+clamiga
(defpackage #:closer-common-lisp-user
  (:nicknames #:c2cl-user)
  (:use #:closer-common-lisp))
