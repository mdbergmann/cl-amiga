;;;; closer-mop-packages.lisp — package definitions for the CL-Amiga
;;;; closer-mop shim.
;;;;
;;;; On CL-Amiga (#+clamiga), every MOP symbol that canonical
;;;; closer-mop imports from an implementation's internal MOP package
;;;; already lives in COMMON-LISP — see the "Portable MOP shims"
;;;; section at the bottom of lib/clos.lisp for the full list.  The
;;;; defpackage below therefore uses :COMMON-LISP and re-exports those
;;;; names under :CLOSER-MOP (nickname :C2MOP), matching the package
;;;; layout that serapeum / trivia / lisp-namespace expect.
;;;;
;;;; For any non-clamiga host reaching this file, we leave the
;;;; canonical closer-mop package definition in place — callers that
;;;; want the real thing should load upstream closer-mop instead of
;;;; this shim.

(in-package :cl-user)

#+clamiga
(defpackage #:closer-mop
  (:use #:common-lisp)
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

#+clamiga
(defpackage #:closer-common-lisp
  (:nicknames #:c2cl)
  (:use)
  (:import-from :closer-mop
                #:built-in-class #:class #:direct-slot-definition
                #:effective-slot-definition #:eql-specializer
                #:forward-referenced-class #:funcallable-standard-class
                #:funcallable-standard-object #:generic-function
                #:metaobject #:method #:method-combination #:slot-definition
                #:specializer #:standard-accessor-method #:standard-class
                #:standard-generic-function #:standard-direct-slot-definition
                #:standard-effective-slot-definition #:standard-method
                #:standard-object #:standard-reader-method
                #:standard-slot-definition #:standard-writer-method
                #:defclass #:defgeneric #:define-method-combination
                #:defmethod #:classp #:ensure-finalized #:ensure-method
                #:fix-slot-initargs #:required-args #:subclassp
                #:class-default-initargs #:class-direct-default-initargs
                #:class-direct-slots #:class-direct-subclasses
                #:class-direct-superclasses #:class-finalized-p
                #:class-precedence-list #:class-prototype #:class-slots
                #:compute-class-precedence-list #:compute-default-initargs
                #:compute-effective-slot-definition #:compute-slots
                #:direct-slot-definition-class #:effective-slot-definition-class
                #:ensure-class #:ensure-class-using-class
                #:finalize-inheritance #:validate-superclass
                #:slot-definition-allocation #:slot-definition-initargs
                #:slot-definition-initform #:slot-definition-initfunction
                #:slot-definition-location #:slot-definition-name
                #:slot-definition-readers #:slot-definition-writers
                #:slot-definition-type
                #:slot-boundp-using-class #:slot-makunbound-using-class
                #:slot-value-using-class #:standard-instance-access
                #:funcallable-standard-instance-access
                #:eql-specializer-object #:intern-eql-specializer
                #:compute-applicable-methods-using-classes
                #:compute-discriminating-function #:compute-effective-method
                #:ensure-generic-function #:ensure-generic-function-using-class
                #:extract-lambda-list #:extract-specializer-names
                #:generic-function-argument-precedence-order
                #:generic-function-declarations
                #:generic-function-lambda-list #:generic-function-method-class
                #:generic-function-method-combination
                #:generic-function-methods #:generic-function-name
                #:add-method #:make-method-lambda #:method-function
                #:method-generic-function #:method-lambda-list
                #:method-specializers #:remove-method
                #:find-method-combination
                #:accessor-method-slot-definition #:add-direct-method
                #:add-direct-subclass #:reader-method-class
                #:remove-direct-method #:remove-direct-subclass
                #:specializer-direct-generic-functions
                #:specializer-direct-methods #:writer-method-class
                #:add-dependent #:map-dependents #:remove-dependent
                #:update-dependent
                #:subtypep #:typep)
  (:export
   #:built-in-class #:class #:direct-slot-definition
   #:effective-slot-definition #:eql-specializer
   #:forward-referenced-class #:funcallable-standard-class
   #:funcallable-standard-object #:generic-function
   #:metaobject #:method #:method-combination #:slot-definition
   #:specializer #:standard-accessor-method #:standard-class
   #:standard-generic-function #:standard-direct-slot-definition
   #:standard-effective-slot-definition #:standard-method
   #:standard-object #:standard-reader-method
   #:standard-slot-definition #:standard-writer-method
   #:defclass #:defgeneric #:define-method-combination
   #:defmethod #:classp #:ensure-finalized #:ensure-method
   #:fix-slot-initargs #:required-args #:subclassp
   #:class-default-initargs #:class-direct-default-initargs
   #:class-direct-slots #:class-direct-subclasses
   #:class-direct-superclasses #:class-finalized-p
   #:class-precedence-list #:class-prototype #:class-slots
   #:compute-class-precedence-list #:compute-default-initargs
   #:compute-effective-slot-definition #:compute-slots
   #:direct-slot-definition-class #:effective-slot-definition-class
   #:ensure-class #:ensure-class-using-class
   #:finalize-inheritance #:validate-superclass
   #:slot-definition-allocation #:slot-definition-initargs
   #:slot-definition-initform #:slot-definition-initfunction
   #:slot-definition-location #:slot-definition-name
   #:slot-definition-readers #:slot-definition-writers
   #:slot-definition-type
   #:slot-boundp-using-class #:slot-makunbound-using-class
   #:slot-value-using-class #:standard-instance-access
   #:funcallable-standard-instance-access
   #:eql-specializer-object #:intern-eql-specializer
   #:compute-applicable-methods-using-classes
   #:compute-discriminating-function #:compute-effective-method
   #:ensure-generic-function #:ensure-generic-function-using-class
   #:extract-lambda-list #:extract-specializer-names
   #:generic-function-argument-precedence-order
   #:generic-function-declarations
   #:generic-function-lambda-list #:generic-function-method-class
   #:generic-function-method-combination
   #:generic-function-methods #:generic-function-name
   #:add-method #:make-method-lambda #:method-function
   #:method-generic-function #:method-lambda-list
   #:method-specializers #:remove-method
   #:find-method-combination
   #:accessor-method-slot-definition #:add-direct-method
   #:add-direct-subclass #:reader-method-class
   #:remove-direct-method #:remove-direct-subclass
   #:specializer-direct-generic-functions
   #:specializer-direct-methods #:writer-method-class
   #:add-dependent #:map-dependents #:remove-dependent
   #:update-dependent
   #:subtypep #:typep))

#+clamiga
(defpackage #:closer-common-lisp-user
  (:nicknames #:c2cl-user)
  (:use #:closer-common-lisp))
