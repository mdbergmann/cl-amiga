;;;; CLOS Implementation for CL-Amiga
;;;; Practical subset: defclass, defgeneric, defmethod, make-instance,
;;;; slot-value, class-of, with-slots, standard method combination.
;;;; Loaded on demand via (require "clos").

;;; ====================================================================
;;; Step 2: Bootstrap Core Classes
;;; ====================================================================

;;; STANDARD-CLASS is a struct type with 12 slots.
;;; Slot layout:
;;;   0:  name                     - symbol
;;;   1:  direct-superclasses      - list of class objects
;;;   2:  direct-slots             - list of canonical slot specs
;;;   3:  cpl                      - class precedence list
;;;   4:  effective-slots          - merged slots from CPL
;;;   5:  slot-index-table         - hash table: slot-name -> effective-slot-def
;;;   6:  direct-subclasses        - list of class objects
;;;   7:  direct-methods           - list
;;;   8:  prototype                - lazily created by CLASS-PROTOTYPE
;;;   9:  finalized-p              - t or nil
;;;   10: default-initargs         - effective (CPL-merged) default initargs
;;;   11: direct-default-initargs  - as supplied to DEFCLASS

(%register-struct-type 'standard-class 12 nil
  '((name nil) (direct-superclasses nil) (direct-slots nil)
    (cpl nil) (effective-slots nil) (slot-index-table nil)
    (direct-subclasses nil) (direct-methods nil)
    (prototype nil) (finalized-p nil) (default-initargs nil)
    (direct-default-initargs nil)))

;; built-in-class: same layout as standard-class, used as metaclass for built-in types
(%register-struct-type 'built-in-class 12 'standard-class
  '((name nil) (direct-superclasses nil) (direct-slots nil)
    (cpl nil) (effective-slots nil) (slot-index-table nil)
    (direct-subclasses nil) (direct-methods nil)
    (prototype nil) (finalized-p nil) (default-initargs nil)
    (direct-default-initargs nil)))

;;; --- Class metaobject accessors ---

(defun class-name (class)
  (%struct-ref class 0))

(defun class-direct-superclasses (class)
  (%struct-ref class 1))

(defun class-direct-slots (class)
  (%struct-ref class 2))

(defun class-precedence-list (class)
  (%struct-ref class 3))

(defun class-effective-slots (class)
  (%struct-ref class 4))

(defun class-slot-index-table (class)
  (%struct-ref class 5))

(defun class-direct-subclasses (class)
  (%struct-ref class 6))

(defun class-direct-methods (class)
  (%struct-ref class 7))

;;; Internal setters (not exported, used during bootstrap)

(defun %set-class-direct-slots (class val)
  (%struct-set class 2 val))

(defun %set-class-cpl (class val)
  (%struct-set class 3 val))

(defun %set-class-effective-slots (class val)
  (%struct-set class 4 val))

(defun %set-class-slot-index-table (class val)
  (%struct-set class 5 val))

(defun %set-class-direct-subclasses (class val)
  (%struct-set class 6 val))

(defun %set-class-direct-methods (class val)
  (%struct-set class 7 val))

(defun %set-class-finalized-p (class val)
  (%struct-set class 9 val))

(defun class-default-initargs (class)
  (%struct-ref class 10))

(defun %set-class-default-initargs (class val)
  (%struct-set class 10 val))

(defun class-direct-default-initargs (class)
  (%struct-ref class 11))

(defun %set-class-direct-default-initargs (class val)
  (%struct-set class 11 val))

;;; MOP public reader for the finalized flag.
(defun class-finalized-p (class)
  (%struct-ref class 9))

;;; --- Class registry ---

(defvar *class-table* (make-hash-table :test 'eq))

;; Register with C for typep to check CLOS class-precedence-lists
(%set-clos-class-table *class-table*)

(defun find-class (name &optional (errorp t) environment)
  "Find the class named NAME. Signal an error if not found and ERRORP is true."
  (multiple-value-bind (class found-p)
      (gethash name *class-table*)
    (if found-p
        class
        (if errorp
            (error "No class named ~S" name)
            nil))))

(defun %set-find-class (name class)
  "Register CLASS under NAME in the class table."
  (setf (gethash name *class-table*) class)
  class)

(defsetf find-class %set-find-class)

;;; --- CPL computation for bootstrap classes ---

(defun %compute-builtin-cpl (class)
  "Compute class precedence list for a built-in class.
   Supers must already have their CPLs set."
  (let ((result (list class)))
    (dolist (super (class-direct-superclasses class))
      (let ((super-cpl (class-precedence-list super)))
        (if super-cpl
            (setq result (append result super-cpl))
            (setq result (append result (list super))))))
    ;; Remove duplicates keeping first occurrence
    (let ((seen nil)
          (out nil))
      (dolist (c result)
        (unless (member c seen :test #'eq)
          (push c seen)
          (push c out)))
      (nreverse out))))

;;; --- Bootstrap helper ---
;;; Classes are created in dependency order (supers before subs),
;;; so CPL and subclass links are computed inline.

(defun %make-bootstrap-class (name direct-superclasses)
  "Create a class metaobject during bootstrap. Supers must already exist."
  (let ((class (%make-struct 'standard-class
                 name                    ; 0:  name
                 direct-superclasses     ; 1:  direct-superclasses
                 nil                     ; 2:  direct-slots
                 nil                     ; 3:  cpl (set below)
                 nil                     ; 4:  effective-slots
                 nil                     ; 5:  slot-index-table
                 nil                     ; 6:  direct-subclasses
                 nil                     ; 7:  direct-methods
                 nil                     ; 8:  prototype
                 t                       ; 9:  finalized-p
                 nil                     ; 10: default-initargs
                 nil)))                  ; 11: direct-default-initargs
    ;; Register in class table
    (setf (find-class name) class)
    ;; Compute CPL (supers already have theirs)
    (%set-class-cpl class (%compute-builtin-cpl class))
    ;; Register as subclass of each direct super
    (dolist (super direct-superclasses)
      (%set-class-direct-subclasses super
        (cons class (class-direct-subclasses super))))
    class))

;;; --- Bootstrap built-in classes ---
;;; Order matters: superclasses must be created before subclasses.

;; T is the root of the class hierarchy
(%make-bootstrap-class 't '())

;; Standard base classes
(%make-bootstrap-class 'standard-object
  (list (find-class 't)))
(%make-bootstrap-class 'standard-class
  (list (find-class 'standard-object)))
(%make-bootstrap-class 'structure-object
  (list (find-class 't)))

;; Numeric tower
(%make-bootstrap-class 'number
  (list (find-class 't)))
(%make-bootstrap-class 'real
  (list (find-class 'number)))
(%make-bootstrap-class 'rational
  (list (find-class 'real)))
(%make-bootstrap-class 'integer
  (list (find-class 'rational)))
(%make-bootstrap-class 'fixnum
  (list (find-class 'integer)))
(%make-bootstrap-class 'bignum
  (list (find-class 'integer)))
(%make-bootstrap-class 'ratio
  (list (find-class 'rational)))
(%make-bootstrap-class 'float
  (list (find-class 'real)))
(%make-bootstrap-class 'single-float
  (list (find-class 'float)))
(%make-bootstrap-class 'double-float
  (list (find-class 'float)))

;; Other built-in classes — CL type hierarchy
(%make-bootstrap-class 'sequence
  (list (find-class 't)))
(%make-bootstrap-class 'list
  (list (find-class 'sequence)))
(%make-bootstrap-class 'symbol
  (list (find-class 't)))
(%make-bootstrap-class 'null
  (list (find-class 'symbol) (find-class 'list)))
(%make-bootstrap-class 'cons
  (list (find-class 'list)))
(%make-bootstrap-class 'character
  (list (find-class 't)))
(%make-bootstrap-class 'array
  (list (find-class 't)))
(%make-bootstrap-class 'vector
  (list (find-class 'array) (find-class 'sequence)))
(%make-bootstrap-class 'string
  (list (find-class 'vector)))
(%make-bootstrap-class 'bit-vector
  (list (find-class 'vector)))
(%make-bootstrap-class 'function
  (list (find-class 't)))
(%make-bootstrap-class 'compiled-function
  (list (find-class 'function)))
(%make-bootstrap-class 'hash-table
  (list (find-class 't)))
(%make-bootstrap-class 'package
  (list (find-class 't)))
(%make-bootstrap-class 'stream
  (list (find-class 't)))
(%make-bootstrap-class 'pathname
  (list (find-class 't)))
(%make-bootstrap-class 'random-state
  (list (find-class 't)))
(%make-bootstrap-class 'readtable
  (list (find-class 't)))
(%make-bootstrap-class 'complex
  (list (find-class 'number)))
(%make-bootstrap-class 'class
  (list (find-class 'standard-object)))
(%make-bootstrap-class 'condition
  (list (find-class 't)))

;; Condition type hierarchy (for method dispatch on condition types)
(%make-bootstrap-class 'serious-condition
  (list (find-class 'condition)))
(%make-bootstrap-class 'warning
  (list (find-class 'condition)))
(%make-bootstrap-class 'simple-condition
  (list (find-class 'condition)))
(%make-bootstrap-class 'error
  (list (find-class 'serious-condition)))
(%make-bootstrap-class 'type-error
  (list (find-class 'error)))
(%make-bootstrap-class 'arithmetic-error
  (list (find-class 'error)))
(%make-bootstrap-class 'division-by-zero
  (list (find-class 'arithmetic-error)))
(%make-bootstrap-class 'control-error
  (list (find-class 'error)))
(%make-bootstrap-class 'program-error
  (list (find-class 'error)))
(%make-bootstrap-class 'undefined-function
  (list (find-class 'error)))
(%make-bootstrap-class 'unbound-variable
  (list (find-class 'error)))
(%make-bootstrap-class 'simple-error
  (list (find-class 'error) (find-class 'simple-condition)))
(%make-bootstrap-class 'simple-warning
  (list (find-class 'warning) (find-class 'simple-condition)))
(%make-bootstrap-class 'style-warning
  (list (find-class 'warning)))
(%make-bootstrap-class 'storage-condition
  (list (find-class 'serious-condition)))
(%make-bootstrap-class 'cell-error
  (list (find-class 'error)))
(%make-bootstrap-class 'unbound-slot
  (list (find-class 'cell-error)))
(%make-bootstrap-class 'stream-error
  (list (find-class 'error)))
(%make-bootstrap-class 'end-of-file
  (list (find-class 'stream-error)))
(%make-bootstrap-class 'file-error
  (list (find-class 'error)))
(%make-bootstrap-class 'package-error
  (list (find-class 'error)))
(%make-bootstrap-class 'parse-error
  (list (find-class 'error)))
(%make-bootstrap-class 'reader-error
  (list (find-class 'parse-error) (find-class 'stream-error)))
(%make-bootstrap-class 'print-not-readable
  (list (find-class 'error)))

;;; ====================================================================
;;; Slot Definition Metaobjects (MOP)
;;; ====================================================================
;;;
;;; Reified slot definitions: struct instances rather than cons+plist.
;;; STANDARD-DIRECT-SLOT-DEFINITION — one per slot declared in defclass.
;;; STANDARD-EFFECTIVE-SLOT-DEFINITION — one per merged slot in a class.

;;; standard-direct-slot-definition layout (9 slots):
;;;   0: name           1: initargs      2: initform
;;;   3: initfunction   4: type          5: allocation
;;;   6: readers        7: writers       8: documentation
(%register-struct-type 'standard-direct-slot-definition 9 nil
  '((name nil) (initargs nil) (initform nil) (initfunction nil)
    (type nil) (allocation :instance)
    (readers nil) (writers nil) (documentation nil)))

;;; standard-effective-slot-definition layout (5 slots) — compact form
;;; sharing indices 0/1/3 with direct-slot-definition so that the
;;; runtime hot path (shared-initialize) needs no type dispatch:
;;;   0: name           1: initargs      2: location
;;;   3: initfunction   4: allocation
;;;
;;; Introspection accessors for type/initform/documentation return NIL
;;; on effective slot defs (the source form lives on the primary direct
;;; slot reachable via class-direct-slots).
(%register-struct-type 'standard-effective-slot-definition 5 nil
  '((name nil) (initargs nil) (location nil)
    (initfunction nil) (allocation :instance)))

;;; Class hierarchy for slot-definition metaobjects
(%make-bootstrap-class 'slot-definition
  (list (find-class 'standard-object)))
(%make-bootstrap-class 'standard-slot-definition
  (list (find-class 'slot-definition)))
(%make-bootstrap-class 'standard-direct-slot-definition
  (list (find-class 'standard-slot-definition)))
(%make-bootstrap-class 'standard-effective-slot-definition
  (list (find-class 'standard-slot-definition)))

;;; --- Accessors (AMOP / closer-mop names) ---
;;; Slots 0, 1, 3 share indices between direct and effective so name,
;;; initargs, and initfunction reads avoid any per-call dispatch.

(defun slot-definition-name         (sd) (%struct-ref sd 0))
(defun slot-definition-initargs     (sd) (%struct-ref sd 1))
(defun slot-definition-initfunction (sd) (%struct-ref sd 3))

(defun %effective-slot-def-p (sd)
  (eq (%struct-type-name sd) 'standard-effective-slot-definition))

(defun slot-definition-initform (sd)
  (if (%effective-slot-def-p sd) nil (%struct-ref sd 2)))

(defun slot-definition-type (sd)
  (if (%effective-slot-def-p sd) nil (%struct-ref sd 4)))

(defun slot-definition-allocation (sd)
  (if (%effective-slot-def-p sd) (%struct-ref sd 4) (%struct-ref sd 5)))

(defun slot-definition-documentation (sd)
  (if (%effective-slot-def-p sd) nil (%struct-ref sd 8)))

;; Direct-only: readers, writers
(defun slot-definition-readers (dsd) (%struct-ref dsd 6))
(defun slot-definition-writers (dsd) (%struct-ref dsd 7))

;; Effective-only: location
(defun slot-definition-location (esd) (%struct-ref esd 2))

;;; Internal setters (used by the effective-slot builder and class
;;; finalization; not part of the MOP API).
(defun %set-slot-definition-initargs (sd v) (%struct-set sd 1 v))
(defun %set-slot-definition-initfunction (sd v) (%struct-set sd 3 v))

(defun %set-slot-definition-initform (sd v)
  (unless (%effective-slot-def-p sd) (%struct-set sd 2 v)))

(defun %set-slot-definition-type (sd v)
  (unless (%effective-slot-def-p sd) (%struct-set sd 4 v)))

(defun %set-slot-definition-location (esd v) (%struct-set esd 2 v))

(defun %set-slot-definition-documentation (sd v)
  (unless (%effective-slot-def-p sd) (%struct-set sd 8 v)))

;;; --- Constructors ---
;;; Positional args (no &key) so defclass expansion stays small and the
;;; call site is a plain %make-struct with no keyword-argument dispatch.

(defun %make-direct-slot-def (name initargs initform initfunction
                              type allocation readers writers documentation)
  (%make-struct 'standard-direct-slot-definition
    name initargs initform initfunction type allocation
    readers writers documentation))

(defun %make-effective-slot-def (name initargs initfunction allocation location)
  (%make-struct 'standard-effective-slot-definition
    name initargs location initfunction allocation))

;;; --- Slot-definition-class protocol ---
;;;
;;; Plain functions for now; upgraded to GFs at the bottom of clos.lisp
;;; once defgeneric/defmethod are available. User-defined metaclasses
;;; are out of scope, so the customization point is an :around method
;;; on the standard-class specializer.
(defun direct-slot-definition-class (class &rest initargs)
  (declare (ignore class initargs))
  (find-class 'standard-direct-slot-definition))

(defun effective-slot-definition-class (class &rest initargs)
  (declare (ignore class initargs))
  (find-class 'standard-effective-slot-definition))

;;; ====================================================================
;;; EQL specializer metaobjects (MOP)
;;; ====================================================================
;;;
;;; AMOP §5.4 reifies (eql value) specializers as metaobjects interned
;;; per value (by EQL on the object). Callers that previously built a
;;; cons (eql value) now call INTERN-EQL-SPECIALIZER so that
;;;   (eq (intern-eql-specializer 42) (intern-eql-specializer 42)) => T
;;; and user code can reach them via the MOP accessor.
;;;
;;; One slot: OBJECT. The intern table never shrinks — we have no weak
;;; hash tables and the leak is bounded by the set of literal values
;;; used in DEFMETHOD forms.

(%register-struct-type 'eql-specializer 1 nil
  '((object nil)))

(%make-bootstrap-class 'eql-specializer
  (list (find-class 'standard-object)))

(defvar *eql-specializer-table* (make-hash-table :test 'eql))

(defun eql-specializer-p (object)
  "Return T if OBJECT is an EQL-SPECIALIZER metaobject."
  (and (structurep object)
       (eq (%struct-type-name object) 'eql-specializer)))

(defun eql-specializer-object (spec)
  "AMOP: return the object associated with an EQL specializer metaobject."
  (%struct-ref spec 0))

(defun intern-eql-specializer (object)
  "AMOP: return the canonical EQL-SPECIALIZER for OBJECT. Repeated calls
   with EQL-identical OBJECTs return the same metaobject so that method
   equality and dispatch caching can use EQ."
  (multiple-value-bind (existing found-p)
      (gethash object *eql-specializer-table*)
    (if found-p
        existing
        (let ((spec (%make-struct 'eql-specializer object)))
          (setf (gethash object *eql-specializer-table*) spec)
          spec))))

;;; --- Register condition types as CLOS classes (for method dispatch) ---

(defun %register-condition-class (name direct-super-names)
  "Register a condition type as a CLOS class for method dispatch."
  (unless (find-class name nil)
    (let* ((supers (mapcar #'find-class direct-super-names))
           (class (%make-struct 'standard-class
                    name supers nil nil nil nil nil nil nil t nil nil)))
      (%set-class-cpl class (%compute-builtin-cpl class))
      (setf (find-class name) class)
      (dolist (super supers)
        (%set-class-direct-subclasses super
          (cons class (class-direct-subclasses super))))
      class)))

;;; --- class-of ---

(defun class-of (object)
  "Return the class of OBJECT."
  (let ((type-name (%class-of object)))
    (or (gethash type-name *class-table*)
        (find-class 't))))

;;; ====================================================================
;;; Phase 1: Slot Access Infrastructure
;;; ====================================================================

;;; Sentinel value for unbound slots — uninterned so it can't collide
(defvar *slot-unbound-marker* (make-symbol "SLOT-UNBOUND"))

;;; When NIL, SLOT-VALUE / SLOT-BOUNDP / SLOT-MAKUNBOUND / (SETF SLOT-VALUE)
;;; bypass generic dispatch and go straight to %STRUCT-REF / %STRUCT-SET —
;;; preserving the tight loop we rely on for fiveam, fset, etc.
;;;
;;; Flipped to T the first time a user installs a non-default method on any
;;; of the four SLOT-*-USING-CLASS GFs. Once set it stays set — tracking
;;; method removals would cost more than it saves, and the slow path only
;;; adds one GF call per slot access (still cache-friendly).
(defvar *slot-access-protocol-extended-p* nil)

;;; Structure slot lookup: when CLASS-OF returns a class without a
;;; slot-index-table (i.e. a DEFSTRUCT instance), resolve slot names
;;; against the struct's slot list so SLOT-VALUE / WITH-SLOTS work on
;;; structures as they do in SBCL/CCL.
(defun %find-struct-slot-index (instance slot-name)
  "Return the slot index in INSTANCE for SLOT-NAME, or NIL if not found.
   INSTANCE must be a structure."
  (let ((names (%struct-slot-names (%struct-type-name instance)))
        (idx 0))
    (block found
      (dolist (n names nil)
        (when (eq n slot-name)
          (return-from found idx))
        (incf idx)))))

(defun slot-value (instance slot-name)
  "Return the value of SLOT-NAME in INSTANCE."
  (let* ((class (class-of instance))
         (index-table (class-slot-index-table class)))
    (cond
      (index-table
       (let ((esd (gethash slot-name index-table)))
         (unless esd
           (error "~S has no slot named ~S" instance slot-name))
         (if *slot-access-protocol-extended-p*
             (slot-value-using-class class instance esd)
             (let* ((location (slot-definition-location esd))
                    (val (if (consp location)
                             (cdr location)
                             (%struct-ref instance location))))
               (if (eq val *slot-unbound-marker*)
                   (slot-unbound class instance slot-name)
                   val)))))
      ((structurep instance)
       (let ((idx (%find-struct-slot-index instance slot-name)))
         (unless idx
           (error "~S has no slot named ~S" instance slot-name))
         (%struct-ref instance idx)))
      (t
       (error "~S has no slot-index-table (not a CLOS instance)" instance)))))

(defun slot-unbound (class instance slot-name)
  "Called when an unbound slot is accessed. Default signals an error.
Specialize via defmethod to provide lazy initialization."
  (declare (ignore class))
  (error "The slot ~S is unbound in ~S" slot-name instance))

(defun %set-slot-value (instance slot-name new-value)
  "Set the value of SLOT-NAME in INSTANCE to NEW-VALUE."
  (let* ((class (class-of instance))
         (index-table (class-slot-index-table class)))
    (cond
      (index-table
       (let ((esd (gethash slot-name index-table)))
         (unless esd
           (error "~S has no slot named ~S" instance slot-name))
         (if *slot-access-protocol-extended-p*
             (setf (slot-value-using-class class instance esd) new-value)
             (let ((location (slot-definition-location esd)))
               (if (consp location)
                   (rplacd location new-value)
                   (%struct-set instance location new-value))
               new-value))))
      ((structurep instance)
       (let ((idx (%find-struct-slot-index instance slot-name)))
         (unless idx
           (error "~S has no slot named ~S" instance slot-name))
         (%struct-set instance idx new-value)
         new-value))
      (t
       (error "~S has no slot-index-table (not a CLOS instance)" instance)))))

(defsetf slot-value %set-slot-value)

(defun slot-boundp (instance slot-name)
  "Return T if SLOT-NAME is bound in INSTANCE.
   Structures are always considered bound (DEFSTRUCT slots have initial values)."
  (let* ((class (class-of instance))
         (index-table (class-slot-index-table class)))
    (cond
      (index-table
       (let ((esd (gethash slot-name index-table)))
         (unless esd
           (error "~S has no slot named ~S" instance slot-name))
         (if *slot-access-protocol-extended-p*
             (slot-boundp-using-class class instance esd)
             (let ((location (slot-definition-location esd)))
               (not (eq (if (consp location)
                            (cdr location)
                            (%struct-ref instance location))
                        *slot-unbound-marker*))))))
      ((structurep instance)
       (unless (%find-struct-slot-index instance slot-name)
         (error "~S has no slot named ~S" instance slot-name))
       t)
      (t
       (error "~S has no slot-index-table (not a CLOS instance)" instance)))))

(defun slot-makunbound (instance slot-name)
  "Make SLOT-NAME unbound in INSTANCE.
   Signals an error for structures per CLHS (undefined behavior)."
  (let* ((class (class-of instance))
         (index-table (class-slot-index-table class)))
    (cond
      (index-table
       (let ((esd (gethash slot-name index-table)))
         (unless esd
           (error "~S has no slot named ~S" instance slot-name))
         (if *slot-access-protocol-extended-p*
             (slot-makunbound-using-class class instance esd)
             (let ((location (slot-definition-location esd)))
               (if (consp location)
                   (rplacd location *slot-unbound-marker*)
                   (%struct-set instance location *slot-unbound-marker*))
               instance))))
      ((structurep instance)
       (error "SLOT-MAKUNBOUND is not supported for structures: ~S" instance))
      (t
       (error "~S has no slot-index-table (not a CLOS instance)" instance)))))

(defun slot-exists-p (instance slot-name)
  "Return T if INSTANCE has a slot named SLOT-NAME."
  (let* ((class (class-of instance))
         (index-table (class-slot-index-table class)))
    (cond
      (index-table
       (multiple-value-bind (esd found-p)
           (gethash slot-name index-table)
         (declare (ignore esd))
         found-p))
      ((structurep instance)
       (if (%find-struct-slot-index instance slot-name) t nil))
      (t nil))))

;;; ====================================================================
;;; Phase 2: C3 Linearization
;;; ====================================================================

(defun %c3-merge (lists)
  "Merge step of C3 linearization. LISTS is a list of lists."
  (let ((result nil))
    (loop
      ;; Remove empty lists
      (setq lists (remove nil lists))
      (when (null lists)
        (return (nreverse result)))
      ;; Find a candidate: head of some list that doesn't appear in
      ;; the tail of any other list
      (let ((candidate nil))
        (dolist (l lists)
          (let ((head (car l))
                (dominated nil))
            ;; Check if head appears in tail of any list
            (dolist (l2 lists)
              (when (member head (cdr l2) :test #'eq)
                (setq dominated t)
                (return)))
            (unless dominated
              (setq candidate head)
              (return))))
        (unless candidate
          (error "Inconsistent class precedence list — C3 linearization failed"))
        ;; Remove candidate from all lists
        (push candidate result)
        (setq lists
              (mapcar (lambda (l)
                        (if (eq (car l) candidate)
                            (cdr l)
                            l))
                      lists))))))

(defun %compute-class-precedence-list (class)
  "Compute CPL using C3 linearization. Supers must already have CPLs."
  (let ((supers (class-direct-superclasses class)))
    (if (null supers)
        (list class)
        (%c3-merge
         (cons (list class)
               (append
                (mapcar #'class-precedence-list supers)
                (list supers)))))))

;;; ====================================================================
;;; Phase 3: defclass
;;; ====================================================================

;;; --- Slot spec helpers ---

(defun %parse-slot-spec (spec)
  "Parse a defclass slot specifier into (name . plist) form."
  (if (symbolp spec)
      (list spec)
      (cons (car spec) (cdr spec))))

(defun %slot-spec-name (spec)
  (car spec))

(defun %slot-spec-option (spec key)
  "Get option value from slot spec plist, or NIL if absent."
  (let ((tail (member key (cdr spec))))
    (if tail (cadr tail) nil)))

(defun %slot-spec-has-option-p (spec key)
  "Return T if slot spec contains KEY."
  (if (member key (cdr spec)) t nil))

(defun %slot-spec-all-options (spec key)
  "Get all values for KEY from slot spec plist (supports multiple occurrences)."
  (let ((result nil)
        (tail (cdr spec)))
    (loop
      (setq tail (member key tail))
      (unless tail (return (nreverse result)))
      (push (cadr tail) result)
      (setq tail (cddr tail)))))

;;; --- Effective slot computation ---
;;;
;;; Direct slots arrive as STANDARD-DIRECT-SLOT-DEFINITION structs already
;;; built by the defclass expansion. Effective slots are fresh
;;; STANDARD-EFFECTIVE-SLOT-DEFINITION structs, merged per AMOP §5.5:
;;; most-specific direct slot supplies initform/type/allocation; initargs
;;; are unioned across all direct slots with that name.

(defun %find-slot-def (name slot-defs)
  "Return the first slot-definition in SLOT-DEFS with the given NAME, or NIL."
  (dolist (sd slot-defs nil)
    (when (eq (slot-definition-name sd) name)
      (return sd))))

(defun %direct-to-effective (dsd)
  "Build a fresh effective-slot-definition from a direct-slot-definition.
   Only runtime-critical fields are stored on the effective slot; type,
   initform source, and documentation are introspected via the primary
   direct slot (reachable from class-direct-slots)."
  (%make-effective-slot-def (slot-definition-name dsd)
    (copy-list (slot-definition-initargs dsd))
    (slot-definition-initfunction dsd)
    (slot-definition-allocation dsd)
    nil))

(defun %fold-parent-into-effective (eff parent-dsd)
  "Fold inherited options from PARENT-DSD into effective slot EFF.
   Most-specific already wins (EFF was seeded first); we union initargs
   and fill in initfunction if the child left it blank."
  ;; Union initargs
  (let ((parent-initargs (slot-definition-initargs parent-dsd)))
    (when parent-initargs
      (let ((combined (slot-definition-initargs eff)))
        (dolist (ia parent-initargs)
          (unless (member ia combined :test #'eq)
            (setq combined (append combined (list ia)))))
        (%set-slot-definition-initargs eff combined))))
  ;; initfunction: inherit only if child had none
  (unless (slot-definition-initfunction eff)
    (let ((pfn (slot-definition-initfunction parent-dsd)))
      (when pfn
        (%set-slot-definition-initfunction eff pfn))))
  eff)

(defun %compute-effective-slots (cpl)
  "Compute effective slots from a class precedence list of classes."
  (let ((effective nil))
    (dolist (class cpl)
      (dolist (dsd (class-direct-slots class))
        (let* ((name (slot-definition-name dsd))
               (existing (%find-slot-def name effective)))
          (if existing
              (%fold-parent-into-effective existing dsd)
              (setq effective
                    (append effective (list (%direct-to-effective dsd))))))))
    effective))

;;; --- Build slot-index-table ---

(defun %build-slot-index-table (effective-slots)
  "Build a hash table mapping slot-name -> effective-slot-definition.
   Callers reach the actual storage via SLOT-DEFINITION-LOCATION on the
   returned slot-def — an integer struct index for :INSTANCE allocation,
   or a cons cell (NAME . VALUE) for :CLASS allocation."
  (let ((table (make-hash-table :test 'eq)))
    (dolist (slot effective-slots)
      (setf (gethash (slot-definition-name slot) table) slot))
    table))

;;; --- Class creation at runtime ---

(defun %ensure-class (name direct-super-names direct-slot-defs
                      &optional direct-default-initargs)
  "Create or update a CLOS class. Called by defclass expansion (and by
   the ENSURE-CLASS GF after keyword args have been destructured).
   DIRECT-SLOT-DEFS is a list of STANDARD-DIRECT-SLOT-DEFINITION instances
   (already constructed by the defclass macro with initfunctions closed
   over the lexical environment).

   The class struct is allocated here; the CPL, effective slots, and
   effective default-initargs are computed by FINALIZE-INHERITANCE so
   that MOP :around methods on COMPUTE-SLOTS, COMPUTE-CLASS-PRECEDENCE-LIST,
   and COMPUTE-DEFAULT-INITARGS get a chance to run."
  (let* ((old-class (find-class name nil))
         (supers (if direct-super-names
                     (mapcar #'find-class direct-super-names)
                     (list (find-class 'standard-object))))
         (class (%make-struct 'standard-class
                  name supers direct-slot-defs
                  nil nil nil nil nil nil nil nil nil)))
    (%set-class-direct-default-initargs class direct-default-initargs)
    ;; On redefinition, drop the old class from each former super's
    ;; direct-subclasses so the old metaobject and its slot-defs are
    ;; reachable only from find-class — which we're about to overwrite.
    (when old-class
      (dolist (old-super (class-direct-superclasses old-class))
        (%set-class-direct-subclasses old-super
          (remove old-class (class-direct-subclasses old-super) :test #'eq))))
    ;; Run the finalization protocol. Routes through the GF if available
    ;; (always the case once clos.lisp finishes loading), otherwise falls
    ;; back to the internal body.
    (if (fboundp 'finalize-inheritance)
        (finalize-inheritance class)
        (%finalize-inheritance-body class))
    ;; Register struct type with the finalized slot layout. The struct
    ;; only holds :INSTANCE-allocated slots — :CLASS slots live on the
    ;; class itself via the cons cell returned by SLOT-DEFINITION-LOCATION.
    (let* ((instance-esds nil))
      (dolist (esd (class-effective-slots class))
        (when (eq (slot-definition-allocation esd) :instance)
          (push esd instance-esds)))
      (setq instance-esds (nreverse instance-esds))
      (let ((struct-slot-specs
              (mapcar (lambda (esd) (list (slot-definition-name esd) nil))
                      instance-esds)))
        (%register-struct-type name (length instance-esds)
                               (if direct-super-names (car direct-super-names) nil)
                               struct-slot-specs)))
    ;; Register class
    (setf (find-class name) class)
    ;; Register as subclass of each direct super
    (dolist (super supers)
      (%set-class-direct-subclasses super
        (cons class (class-direct-subclasses super))))
    ;; Invalidate all GF dispatch caches (class hierarchy changed)
    (when (fboundp '%invalidate-all-gf-caches)
      (%invalidate-all-gf-caches))
    class))

;;; --- Class finalization body ---
;;; Shared between the FINALIZE-INHERITANCE GF default method and the
;;; bootstrap path used by %ENSURE-CLASS before the GFs are defined.
;;; Calling twice is idempotent — the class struct is replaced on
;;; redefinition, so a fresh class always starts with FINALIZED-P = NIL.

(defun %find-inherited-class-slot-cell (class slot-name)
  "Walk CLASS's direct-superclasses' effective slots looking for a
   class-allocated slot with SLOT-NAME. Return its location cons cell
   so the subclass can share the same storage — per AMOP §5.5,
   subclasses inherit :class storage unless they redefine the slot."
  (dolist (super (class-direct-superclasses class) nil)
    (when (class-finalized-p super)
      (let ((super-esd (%find-slot-def slot-name
                                       (class-effective-slots super))))
        (when (and super-esd
                   (eq (slot-definition-allocation super-esd) :class))
          (let ((super-loc (slot-definition-location super-esd)))
            (when (consp super-loc)
              (return super-loc))))))))

(defun %assign-slot-locations (class effective-slots)
  "Fill in SLOT-DEFINITION-LOCATION for each effective slot. Instance
   slots get the next integer struct index. Class-allocated slots get
   a cons (NAME . VALUE) — inherited from a superclass when the class
   does not provide its own direct definition for the same name."
  (let ((instance-i 0))
    (dolist (esd effective-slots)
      (let ((name (slot-definition-name esd)))
        (cond
          ((eq (slot-definition-allocation esd) :class)
           (let ((own-direct (%find-slot-def name (class-direct-slots class))))
             (%set-slot-definition-location esd
               (if own-direct
                   (cons name *slot-unbound-marker*)
                   (or (%find-inherited-class-slot-cell class name)
                       (cons name *slot-unbound-marker*))))))
          (t
           (%set-slot-definition-location esd instance-i)
           (setq instance-i (+ instance-i 1))))))))

(defun %finalize-inheritance-body (class)
  (unless (class-finalized-p class)
    (%set-class-cpl class (%compute-class-precedence-list class))
    (let ((effective (%compute-slots-default class)))
      (%set-class-effective-slots class effective)
      (%set-class-slot-index-table class (%build-slot-index-table effective))
      (%assign-slot-locations class effective))
    (%set-class-default-initargs class
      (%compute-default-initargs-default class))
    (%set-class-finalized-p class t))
  class)

;;; --- compute-slots default body ---
;;; AMOP §5.5: collect direct slots across the CPL (most-specific first)
;;; grouped by name, then call COMPUTE-EFFECTIVE-SLOT-DEFINITION for each
;;; name. User methods on COMPUTE-EFFECTIVE-SLOT-DEFINITION see the full
;;; per-name slot group.

(defun %compute-slots-default (class)
  (let* ((cpl (class-precedence-list class))
         (order nil)
         (groups (make-hash-table :test 'eq)))
    (dolist (c cpl)
      (dolist (dsd (class-direct-slots c))
        (let ((name (slot-definition-name dsd)))
          (unless (gethash name groups)
            (push name order))
          (setf (gethash name groups)
                (append (gethash name groups) (list dsd))))))
    (setq order (nreverse order))
    (mapcar (lambda (name)
              (%compute-effective-slot-definition-default
                 class name (gethash name groups)))
            order)))

(defun %compute-effective-slot-definition-default (class name direct-slots)
  "AMOP: build an effective slot def from a list of direct slot defs
   (most-specific first). The most-specific supplies initform/type/
   allocation; initargs are unioned and initfunction is inherited only
   when the most-specific leaves it blank."
  (declare (ignore class name))
  (let ((primary (first direct-slots))
        (parents (rest direct-slots)))
    (let ((eff (%direct-to-effective primary)))
      (dolist (p parents)
        (%fold-parent-into-effective eff p))
      eff)))

(defun %compute-default-initargs-default (class)
  "Collect effective default-initargs from the CPL, most-specific wins."
  (let ((result nil)
        (keys-seen nil))
    (dolist (c (class-precedence-list class))
      (let ((directs (class-direct-default-initargs c)))
        (dolist (entry directs)
          (let ((key (first entry)))
            (unless (member key keys-seen :test #'eq)
              (push key keys-seen)
              (setq result (append result (list entry))))))))
    result))

;;; --- defclass macro ---
;;;
;;; Emits a list of runtime %MAKE-DIRECT-SLOT-DEF calls. The initfunction
;;; slot is a lambda closing over the defclass-site lexical environment,
;;; so initforms can reference surrounding bindings.

(defun %slot-spec->direct-def-form (spec)
  "Turn a raw slot specifier into a form that builds a direct-slot-def."
  (let* ((parsed (%parse-slot-spec spec))
         (slot-name (%slot-spec-name parsed))
         (initargs (%slot-spec-all-options parsed :initarg))
         (has-initform (%slot-spec-has-option-p parsed :initform))
         (initform (%slot-spec-option parsed :initform))
         (type (%slot-spec-option parsed :type))
         (allocation (or (%slot-spec-option parsed :allocation) :instance))
         (readers (%slot-spec-all-options parsed :reader))
         (writers (%slot-spec-all-options parsed :writer))
         (accessors (%slot-spec-all-options parsed :accessor))
         (documentation (%slot-spec-option parsed :documentation)))
    `(%make-direct-slot-def ',slot-name
       ',initargs
       ',initform
       ,(if has-initform `(lambda () ,initform) nil)
       ',type
       ',allocation
       ',(append readers accessors)
       ',(append writers (mapcar (lambda (a) `(setf ,a)) accessors))
       ',documentation)))

(defmacro defclass (name direct-superclasses slot-specifiers &rest class-options)
  "Define a new CLOS class."
  (let ((accessor-defs nil)
        (slot-def-forms nil)
        (default-initarg-forms nil))
    ;; Parse class options
    (dolist (opt class-options)
      (when (and (consp opt) (eq (car opt) :default-initargs))
        (let ((args (cdr opt)))
          (loop while args
                do (let ((key (pop args))
                         (val (pop args)))
                     (push `(list ',key (lambda () ,val)) default-initarg-forms))))))
    ;; Parse each slot specifier
    (dolist (spec slot-specifiers)
      (let* ((parsed (%parse-slot-spec spec))
             (slot-name (%slot-spec-name parsed))
             (accessors (%slot-spec-all-options parsed :accessor))
             (readers (%slot-spec-all-options parsed :reader))
             (writers (%slot-spec-all-options parsed :writer)))
        (push (%slot-spec->direct-def-form spec) slot-def-forms)
        ;; Generate accessor functions
        (dolist (accessor accessors)
          (let ((setter-name (intern (concatenate 'string
                                      "%SETF-" (symbol-name accessor))
                                    (or (symbol-package accessor) *package*))))
            (push `(defun ,accessor (obj)
                     (slot-value obj ',slot-name))
                  accessor-defs)
            (push `(defun ,setter-name (val obj)
                     (setf (slot-value obj ',slot-name) val))
                  accessor-defs)))
        (dolist (reader readers)
          (push `(defun ,reader (obj)
                   (slot-value obj ',slot-name))
                accessor-defs))
        (dolist (writer writers)
          (push `(defun ,writer (val obj)
                   (setf (slot-value obj ',slot-name) val))
                accessor-defs))))
    (setq slot-def-forms (nreverse slot-def-forms))
    (setq accessor-defs (nreverse accessor-defs))
    `(progn
       (%ensure-class ',name
                      ',direct-superclasses
                      (list ,@slot-def-forms)
                      (list ,@(nreverse default-initarg-forms)))
       ,@accessor-defs
       (find-class ',name))))

;;; ====================================================================
;;; Phase 4: make-instance + Initialization
;;; ====================================================================

(defun allocate-instance (class)
  "Allocate a fresh instance of CLASS with all instance slots unbound.
   Class-allocated slots don't take struct storage — they live in the
   cons cell attached to their effective-slot-definition."
  (let ((name (class-name class))
        (n 0))
    (dolist (esd (class-effective-slots class))
      (when (eq (slot-definition-allocation esd) :instance)
        (setq n (+ n 1))))
    (apply #'%make-struct name
           (make-list n :initial-element *slot-unbound-marker*))))

(defun %initarg-to-slot-index (class initarg)
  "Find the slot index for INITARG in CLASS, or NIL."
  (let ((effective (class-effective-slots class))
        (i 0))
    (dolist (slot effective)
      (when (member initarg (slot-definition-initargs slot) :test #'eq)
        (return-from %initarg-to-slot-index i))
      (setq i (+ i 1)))
    nil))

(defun %find-initarg-value (initargs slot-initargs)
  "Scan INITARGS (plist) for the first key that appears in SLOT-INITARGS.
   Return (values value supplied-p)."
  (let ((tail initargs))
    (loop while tail
          do (let ((key (car tail)))
               (when (member key slot-initargs :test #'eq)
                 (return-from %find-initarg-value (values (cadr tail) t)))
               (setq tail (cddr tail)))))
  (values nil nil))

(defun shared-initialize (instance slot-names &rest initargs)
  "Initialize slots of INSTANCE from INITARGS and initforms.
   Routes writes through SLOT-DEFINITION-LOCATION so that :CLASS slots
   update their shared cons cell while :INSTANCE slots write into the
   struct directly."
  (let* ((class (class-of instance))
         (effective (class-effective-slots class)))
    (dolist (slot effective)
      (let* ((slot-name (slot-definition-name slot))
             (slot-initargs (slot-definition-initargs slot))
             (location (slot-definition-location slot)))
        (multiple-value-bind (initarg-val initarg-supplied)
            (%find-initarg-value initargs slot-initargs)
          (cond
            (initarg-supplied
             (if (consp location)
                 (rplacd location initarg-val)
                 (%struct-set instance location initarg-val)))
            ((and (or (eq slot-names t)
                      (member slot-name slot-names :test #'eq))
                  (eq (if (consp location)
                          (cdr location)
                          (%struct-ref instance location))
                      *slot-unbound-marker*)
                  (slot-definition-initfunction slot))
             (let ((val (funcall (slot-definition-initfunction slot))))
               (if (consp location)
                   (rplacd location val)
                   (%struct-set instance location val))))))))
    instance))

(defun initialize-instance (instance &rest initargs)
  "Initialize a newly allocated instance."
  (apply #'shared-initialize instance t initargs))

(defun make-instance (class-or-name &rest initargs)
  "Create a new instance of CLASS-OR-NAME with INITARGS."
  (let* ((class (if (symbolp class-or-name)
                    (find-class class-or-name)
                    class-or-name))
         ;; Apply default-initargs: for each default not already in initargs,
         ;; evaluate the initform function and prepend to initargs
         (defaults (class-default-initargs class))
         (effective-initargs initargs))
    (when defaults
      (dolist (default defaults)
        (let ((key (first default))
              (initfn (second default)))
          (unless (member key initargs :test #'eq)
            (setq effective-initargs
                  (append effective-initargs (list key (funcall initfn)))))))
      (setq initargs effective-initargs))
    (let ((instance (allocate-instance class)))
      ;; Uses initialize-instance which is initially a plain function,
      ;; then upgraded to a GF in Phase 7
      (apply #'initialize-instance instance initargs)
      instance)))

;;; ====================================================================
;;; Phase 5: defgeneric + defmethod + Dispatch
;;; ====================================================================

;;; --- Generic function and method struct types ---

;;; standard-generic-function: 8 slots
;;;   0: name, 1: lambda-list, 2: methods, 3: discriminating-function,
;;;   4: method-combination, 5: dispatch-cache, 6: cacheable-p,
;;;   7: eql-value-sets
(%register-struct-type 'standard-generic-function 8 nil
  '((name nil) (lambda-list nil) (methods nil)
    (discriminating-function nil) (method-combination nil)
    (dispatch-cache nil) (cacheable-p nil) (eql-value-sets nil)))

;;; standard-method: 5 slots
;;;   0: generic-function, 1: specializers, 2: qualifiers, 3: function,
;;;   4: lambda-list
(%register-struct-type 'standard-method 5 nil
  '((generic-function nil) (specializers nil) (qualifiers nil)
    (function nil) (lambda-list nil)))

;;; Funcallable metaclass hierarchy (AMOP §5.5).
;;;
;;; A funcallable instance is a metaobject that can be invoked as a
;;; function; in our implementation, the C-side call path recognises a
;;; STANDARD-GENERIC-FUNCTION struct and dispatches through its
;;; discriminating-function slot (see cl_unwrap_funcallable in vm.c).
;;;
;;; User-defined metaclasses are out of scope (see specs/mop.md), so
;;; FUNCALLABLE-STANDARD-CLASS exists as a placeholder for API
;;; completeness — DEFCLASS ignores `:metaclass`. FUNCALLABLE-STANDARD-OBJECT
;;; is a real superclass of STANDARD-GENERIC-FUNCTION so that
;;;   (typep #'foo 'funcallable-standard-object) => T
;;; and typep on 'function succeeds too (function is in the CPL).
(%make-bootstrap-class 'funcallable-standard-class
  (list (find-class 'standard-class)))
(%make-bootstrap-class 'funcallable-standard-object
  (list (find-class 'function) (find-class 'standard-object)))

;;; Register these as classes so dispatch works on them
(%make-bootstrap-class 'standard-generic-function
  (list (find-class 'funcallable-standard-object)))
(%make-bootstrap-class 'standard-method
  (list (find-class 'standard-object)))
;; standard-class is already registered from the bootstrap section

;;; --- GF/method accessors ---

(defun gf-name (gf) (%struct-ref gf 0))
(defun gf-lambda-list (gf) (%struct-ref gf 1))
(defun gf-methods (gf) (%struct-ref gf 2))
(defun %set-gf-methods (gf val) (%struct-set gf 2 val))
(defun gf-discriminating-function (gf) (%struct-ref gf 3))
(defun %set-gf-discriminating-function (gf val) (%struct-set gf 3 val))
(defun gf-dispatch-cache (gf) (%struct-ref gf 5))
(defun %set-gf-dispatch-cache (gf val) (%struct-set gf 5 val))
(defun gf-cacheable-p (gf) (%struct-ref gf 6))
(defun %set-gf-cacheable-p (gf val) (%struct-set gf 6 val))
(defun gf-eql-value-sets (gf) (%struct-ref gf 7))
(defun %set-gf-eql-value-sets (gf val) (%struct-set gf 7 val))

(defun method-generic-function (m) (%struct-ref m 0))
(defun %set-method-generic-function (m gf) (%struct-set m 0 gf))
(defun method-specializers (m) (%struct-ref m 1))
(defun method-qualifiers (m) (%struct-ref m 2))
(defun method-function (m) (%struct-ref m 3))
(defun method-lambda-list (m) (%struct-ref m 4))

;;; --- GF table ---
(defvar *generic-function-table* (make-hash-table :test 'equal))

;;; --- call-next-method support ---
(defvar *call-next-method-function* nil)
(defvar *call-next-method-args* nil)
(defvar *next-method-p-function* nil)
(defvar *current-method-args* nil)

(defun call-next-method (&rest args)
  "Call the next most-specific method.
When called with no arguments, passes the original method arguments."
  (if *call-next-method-function*
      (apply *call-next-method-function*
             (if args args *call-next-method-args*))
      (error "No next method")))

(defun next-method-p ()
  "Return T if there is a next method."
  (if *next-method-p-function*
      (funcall *next-method-p-function*)
      nil))

;;; --- Subclass predicate ---

(defun %subclassp (sub super)
  "Return T if SUB is a subclass of (or eq to) SUPER."
  (member super (class-precedence-list sub) :test #'eq))

;;; --- Applicability and specificity ---

(defun %method-applicable-p (method args)
  "Return T if METHOD is applicable to ARGS."
  (let ((specializers (method-specializers method))
        (applicable t))
    (do ((specs specializers (cdr specs))
         (as args (cdr as)))
        ((or (null specs) (null as)) applicable)
      (let ((spec (car specs))
            (arg (car as)))
        (cond
          ((eql-specializer-p spec)
           (unless (eql arg (eql-specializer-object spec))
             (setq applicable nil)
             (return nil)))
          (t
           (let ((arg-class (class-of arg)))
             (unless (%subclassp arg-class spec)
               (setq applicable nil)
               (return nil)))))))))

(defun %method-more-specific-p (m1 m2 args)
  "Return T if M1 is more specific than M2 for ARGS."
  (let ((s1 (method-specializers m1))
        (s2 (method-specializers m2)))
    (do ((sp1 s1 (cdr sp1))
         (sp2 s2 (cdr sp2))
         (as args (cdr as)))
        ((or (null sp1) (null sp2)) nil)
      (let ((c1 (car sp1))
            (c2 (car sp2)))
        ;; EQ handles the interned-EQL-specializer case as well as class
        ;; identity; only unequal specializers need ordering.
        (unless (eq c1 c2)
          (cond
            ((eql-specializer-p c1) (return t))
            ((eql-specializer-p c2) (return nil))
            (t
             (let* ((arg-class (class-of (car as)))
                    (cpl (class-precedence-list arg-class)))
               (dolist (c cpl)
                 (when (eq c c1) (return-from %method-more-specific-p t))
                 (when (eq c c2) (return-from %method-more-specific-p nil)))))))))))

(defun %compute-applicable-methods (gf args)
  "Return applicable methods sorted most-specific-first."
  (let ((applicable nil))
    (dolist (m (gf-methods gf))
      (when (%method-applicable-p m args)
        (push m applicable)))
    ;; Sort by specificity (insertion sort — small lists)
    (let ((sorted nil))
      (dolist (m applicable)
        (if (null sorted)
            (setq sorted (list m))
            (let ((inserted nil)
                  (result nil)
                  (rest sorted))
              (loop
                (when (null rest)
                  (push m result)
                  (setq inserted t)
                  (return))
                (if (and (not inserted)
                         (%method-more-specific-p m (car rest) args))
                    (progn
                      (push m result)
                      (setq inserted t)
                      (dolist (r rest) (push r result))
                      (return))
                    (progn
                      (push (car rest) result)
                      (setq rest (cdr rest)))))
              (setq sorted (nreverse result)))))
      sorted)))

;;; --- Standard method combination ---

(defun %build-effective-method (methods)
  "Build a cacheable effective method closure (args-independent)."
  (let ((around nil)
        (before nil)
        (primary nil)
        (after nil))
    ;; Separate by qualifier
    (dolist (m methods)
      (let ((q (method-qualifiers m)))
        (cond
          ((null q) (push m primary))
          ((equal q '(:around)) (push m around))
          ((equal q '(:before)) (push m before))
          ((equal q '(:after)) (push m after)))))
    (setq primary (nreverse primary))
    (setq before (nreverse before))
    ;; after list is reversed (least-specific-first) — that's what we want
    (setq around (nreverse around))
    (unless primary
      (error "No applicable primary method"))
    ;; Build the effective method
    (let* ((primary-chain (%make-method-chain primary))
           (call-primary
             (if (and (null before) (null after))
                 ;; Optimization: no before/after, use primary chain directly
                 primary-chain
                 (lambda (&rest call-args)
                   (let ((args (if call-args call-args *current-method-args*)))
                     ;; Execute :before methods
                     (dolist (m before)
                       (apply (method-function m) args))
                     ;; Execute primary chain (preserve multiple values)
                     (let ((results (multiple-value-list (apply primary-chain args))))
                       ;; Execute :after methods
                       (dolist (m after)
                         (apply (method-function m) args))
                       (values-list results)))))))
      (if around
          (%make-around-chain around call-primary)
          call-primary))))

(defun %call-with-method-combination (methods args)
  "Execute standard method combination on sorted applicable METHODS."
  (let ((*current-method-args* args))
    (apply (%build-effective-method methods) args)))

(defun %make-method-chain (methods)
  "Build a call-next-method chain from primary methods."
  (if (null methods)
      (lambda (&rest call-args)
        (declare (ignore call-args))
        (error "No next method"))
      (let* ((m (car methods))
             (rest-chain (%make-method-chain (cdr methods)))
             (has-next (not (null (cdr methods)))))
        (lambda (&rest call-args)
          (let* ((actual-args (if call-args call-args *current-method-args*))
                 (*call-next-method-function* rest-chain)
                 (*call-next-method-args* actual-args)
                 (*next-method-p-function* (lambda () has-next)))
            (apply (method-function m) actual-args))))))

(defun %make-around-chain (around-methods inner)
  "Build an :around chain that wraps INNER."
  (if (null around-methods)
      inner
      (let* ((m (car around-methods))
             (rest-chain (%make-around-chain (cdr around-methods) inner)))
        (lambda (&rest call-args)
          (let* ((actual-args (if call-args call-args *current-method-args*))
                 (*call-next-method-function* rest-chain)
                 (*call-next-method-args* actual-args)
                 (*next-method-p-function* (lambda () t)))
            (apply (method-function m) actual-args))))))

;;; --- GF dispatch cache ---

(defun %compute-gf-cacheable-p (gf)
  "Return dispatch mode for GF:
   N (integer) = number of specialized arg positions (class cache)
   :EQL = has EQL specializers (EQL cache)
   Always returns at least 1 for class-only dispatch."
  (let ((t-class (find-class 't))
        (has-eql nil)
        (max-specialized 1))
    (dolist (m (gf-methods gf))
      (let ((pos 0))
        (dolist (s (method-specializers m))
          (cond
            ((eql-specializer-p s)
             (setq has-eql t))
            ((not (eq s t-class))
             (when (> (1+ pos) max-specialized)
               (setq max-specialized (1+ pos)))))
          (setq pos (1+ pos)))))
    (if has-eql :eql max-specialized)))

(defun %invalidate-all-gf-caches ()
  "Clear dispatch caches of all generic functions."
  (maphash (lambda (name gf)
             (declare (ignore name))
             (%set-gf-dispatch-cache gf nil))
           *generic-function-table*))

(defun %compute-eql-value-sets (gf)
  "Compute per-position EQL value sets for GF.
   Returns a list: NIL for positions without EQL specializers,
   or a hash table mapping known EQL values to T."
  (let ((max-pos 0))
    ;; Find max specializer position
    (dolist (m (gf-methods gf))
      (let ((len (length (method-specializers m))))
        (when (> len max-pos) (setq max-pos len))))
    ;; Build per-position hash tables
    (let ((result nil))
      (dotimes (pos max-pos)
        (let ((ht nil))
          (dolist (m (gf-methods gf))
            (let ((specs (method-specializers m)))
              (when (> (length specs) pos)
                (let ((s (nth pos specs)))
                  (when (eql-specializer-p s)
                    (unless ht
                      (setq ht (make-hash-table :test 'eql)))
                    (setf (gethash (eql-specializer-object s) ht) t))))))
          (push ht result)))
      (nreverse result))))

(defun %gf-dispatch-eql (gf args)
  "Dispatch a GF with EQL specializers using mixed EQL/class cache.
   Cache structure at each level: (eql-ht . class-ht) cons.
   EQL-HT keys are EQL values, CLASS-HT keys are class objects.
   Values at intermediate levels are (eql-ht . class-ht) conses.
   Values at final level are EMF closures (or NIL for negative cache)."
  (let* ((eql-sets (gf-eql-value-sets gf))
         (cache (gf-dispatch-cache gf)))
    (unless cache
      ;; Initialize: (eql-ht . class-ht) cons
      (setq cache (cons (make-hash-table :test 'eql)
                        (make-hash-table :test 'eq)))
      (%set-gf-dispatch-cache gf cache))
    ;; Navigate/create nested levels for each position with EQL/class specializers
    (let ((node cache)
          (a args)
          (sets eql-sets)
          (depth 0)
          (n-levels (length eql-sets)))
      ;; Navigate intermediate levels (0..n-levels-2)
      (loop
        (when (>= depth (1- n-levels))
          (return))
        (let* ((eql-set (car sets))
               (arg (car a))
               (use-eql (and eql-set (gethash arg eql-set)))
               (ht (if use-eql (car node) (cdr node)))
               (key (if use-eql arg (class-of arg)))
               (next (gethash key ht)))
          (unless next
            (setq next (cons (make-hash-table :test 'eql)
                             (make-hash-table :test 'eq)))
            (setf (gethash key ht) next))
          (setq node next)
          (setq a (cdr a))
          (setq sets (cdr sets))
          (setq depth (1+ depth))))
      ;; Final level: lookup EMF
      (let* ((eql-set (car sets))
             (arg (car a))
             (use-eql (and eql-set (gethash arg eql-set)))
             (ht (if use-eql (car node) (cdr node)))
             (key (if use-eql arg (class-of arg)))
             (*current-method-args* args))
        (multiple-value-bind (emf found)
            (gethash key ht)
          (if found
              (if emf
                  (apply emf args)
                  (error "No applicable method for ~S with args of types ~S"
                         (gf-name gf) (mapcar #'type-of args)))
              (let ((methods (%compute-applicable-methods gf args)))
                (if methods
                    (let ((new-emf (%build-effective-method methods)))
                      (setf (gethash key ht) new-emf)
                      (apply new-emf args))
                    (progn
                      (setf (gethash key ht) nil)
                      (error "No applicable method for ~S with args of types ~S"
                             (gf-name gf) (mapcar #'type-of args)))))))))))

(defun %gf-dispatch-cached (gf args n-specialized)
  "Look up or compute effective method using nested class cache.
   N-SPECIALIZED is the number of specialized arg positions."
  (let ((cache (gf-dispatch-cache gf)))
    (unless cache
      (setq cache (make-hash-table :test 'eq))
      (%set-gf-dispatch-cache gf cache))
    ;; Navigate/create nested hash tables for positions 0..n-specialized-2
    (let ((table cache)
          (a args)
          (depth 0))
      ;; Navigate intermediate levels
      (loop
        (when (>= depth (1- n-specialized))
          (return))
        (let* ((class (class-of (car a)))
               (next (gethash class table)))
          (unless next
            (setq next (make-hash-table :test 'eq))
            (setf (gethash class table) next))
          (setq table next)
          (setq a (cdr a))
          (setq depth (1+ depth))))
      ;; Final level: lookup EMF by class of last specialized arg
      (let* ((class (class-of (car a)))
             (*current-method-args* args))
        (multiple-value-bind (emf found)
            (gethash class table)
          (if found
              (if emf
                  (apply emf args)
                  (error "No applicable method for ~S with args of types ~S"
                         (gf-name gf) (mapcar #'type-of args)))
              (let ((methods (%compute-applicable-methods gf args)))
                (if methods
                    (let ((new-emf (%build-effective-method methods)))
                      (setf (gethash class table) new-emf)
                      (apply new-emf args))
                    (progn
                      (setf (gethash class table) nil)
                      (error "No applicable method for ~S with args of types ~S"
                             (gf-name gf) (mapcar #'type-of args)))))))))))

;;; --- GF dispatch ---

(defun %gf-dispatch (gf args)
  "Dispatch a generic function call."
  (let ((mode (gf-cacheable-p gf)))
    (cond
      ((integerp mode)
       (%gf-dispatch-cached gf args mode))
      ((eq mode :eql)
       (%gf-dispatch-eql gf args))
      (t
       (let ((methods (%compute-applicable-methods gf args)))
         (when (null methods)
           (error "No applicable method for ~S with args of types ~S"
                  (gf-name gf) (mapcar #'type-of args)))
         (let ((*current-method-args* args))
           (apply (%build-effective-method methods) args)))))))

;;; --- ensure-generic-function ---

(defun ensure-generic-function (name &key lambda-list)
  "Find or create a generic function named NAME.
Installs the GF metaobject itself in the symbol-function cell; the VM
transparently unwraps funcallable instances to their discriminating
function, so (FOO ...) and (FUNCALL #'FOO ...) both dispatch through
the GF's slot 3. SET-FUNCALLABLE-INSTANCE-FUNCTION can retarget that
slot without touching the symbol-function cell."
  (multiple-value-bind (existing found-p)
      (gethash name *generic-function-table*)
    (if found-p
        (progn
          ;; Re-install the GF object in the function cell, in case a
          ;; (defun ...) reload overwrote it.
          (cond
            ((symbolp name)
             (setf (symbol-function name) existing))
            ((and (consp name) (eq (car name) 'setf) (consp (cdr name)))
             (let* ((accessor (cadr name))
                    (hidden-name (concatenate 'string "%SETF-" (symbol-name accessor)))
                    (hidden-sym (intern hidden-name (or (symbol-package accessor) "COMMON-LISP"))))
               (setf (symbol-function hidden-sym) existing))))
          existing)
        (let* ((gf (%make-struct 'standard-generic-function
                     name lambda-list nil nil nil nil nil nil))
               (dispatch-fn
                 (named-lambda %gf-dispatch-entry (&rest args)
                   (%gf-dispatch gf args))))
          (%set-gf-discriminating-function gf dispatch-fn)
          (setf (gethash name *generic-function-table*) gf)
          (cond
            ((symbolp name)
             (setf (symbol-function name) gf))
            ;; (setf accessor) — install GF on hidden symbol
            ((and (consp name) (eq (car name) 'setf) (consp (cdr name)))
             (let* ((accessor (cadr name))
                    (hidden-name (concatenate 'string "%SETF-" (symbol-name accessor)))
                    (hidden-sym (intern hidden-name (or (symbol-package accessor) "COMMON-LISP"))))
               (setf (symbol-function hidden-sym) gf)
               (%register-setf-function accessor hidden-sym))))
          gf))))

;;; --- defgeneric macro ---

(defmacro defgeneric (name lambda-list &rest options)
  "Define a generic function."
  (let ((method-defs nil))
    (dolist (opt options)
      (when (and (consp opt) (eq (car opt) :method))
        ;; (:method [qualifiers...] specialized-lambda-list &body body)
        (let ((rest (cdr opt))
              (qualifiers nil))
          ;; Collect qualifiers (keywords before the lambda-list)
          (loop
            (if (and rest (keywordp (car rest)))
                (progn (push (car rest) qualifiers)
                       (setq rest (cdr rest)))
                (return)))
          (setq qualifiers (nreverse qualifiers))
          (let ((spec-ll (car rest))
                (body (cdr rest)))
            (if qualifiers
                (push `(defmethod ,name ,@qualifiers ,spec-ll ,@body) method-defs)
                (push `(defmethod ,name ,spec-ll ,@body) method-defs))))))
    (setq method-defs (nreverse method-defs))
    (if method-defs
        `(progn
           (ensure-generic-function ',name :lambda-list ',lambda-list)
           ,@method-defs)
        `(ensure-generic-function ',name :lambda-list ',lambda-list))))

;;; --- defmethod helpers ---

(defun %parse-specialized-lambda-list (spec-ll)
  "Parse a specialized lambda-list into (unspec-ll . specializer-names).
   E.g. ((x point) y) -> ((x y) . (point t))"
  (let ((unspec nil)
        (specs nil)
        (in-required t))
    (dolist (param spec-ll)
      (cond
        ;; Lambda-list keyword — stop specializing
        ((member param '(&rest &optional &key &body &allow-other-keys) :test #'eq)
         (setq in-required nil)
         (push param unspec))
        ;; Specialized parameter: (var class-name) or (var (eql val))
        ((and in-required (consp param))
         (push (car param) unspec)
         (push (cadr param) specs))
        ;; Unspecialized required parameter
        (in-required
         (push param unspec)
         (push 't specs))
        ;; Non-required parameter
        (t (push param unspec))))
    (cons (nreverse unspec) (nreverse specs))))

(defun %resolve-specializers (specializer-names)
  "Resolve specializer names to specializer metaobjects.
   Symbols -> (find-class sym), (eql val) -> interned EQL-SPECIALIZER
   for the evaluated value."
  (mapcar (lambda (s)
            (if (and (consp s) (eq (car s) 'eql))
                (intern-eql-specializer (eval (cadr s)))
                (find-class s)))
          specializer-names))

(defun extract-specializer-names (specialized-lambda-list)
  "AMOP: given a specialized lambda list, return the list of specializer
   names — class-name symbols or (EQL value) forms, padded with T for
   unspecialized required parameters.  Non-required parameters are
   skipped."
  (let ((names nil))
    (dolist (p specialized-lambda-list)
      (cond
        ((member p '(&optional &rest &key &body &allow-other-keys &aux) :test #'eq)
         (return))
        ((consp p)
         (push (cadr p) names))
        (t (push 't names))))
    (nreverse names)))

(defun extract-lambda-list (specialized-lambda-list)
  "AMOP: given a specialized lambda list, return the corresponding plain
   lambda list — specialized required parameters are replaced by their
   variable names; non-required parameters are preserved verbatim."
  (let ((unspec nil)
        (in-required t))
    (dolist (p specialized-lambda-list)
      (cond
        ((member p '(&optional &rest &key &body &allow-other-keys &aux) :test #'eq)
         (setq in-required nil)
         (push p unspec))
        ((and in-required (consp p))
         (push (car p) unspec))
        (t (push p unspec))))
    (nreverse unspec)))

;;; --- defmethod macro ---

(defmacro defmethod (name &rest args)
  "Define a method on generic function NAME."
  ;; Parse qualifiers (keywords before the lambda-list)
  (let ((qualifiers nil)
        (rest args))
    (loop
      (if (and rest (keywordp (car rest)))
          (progn
            (push (car rest) qualifiers)
            (setq rest (cdr rest)))
          (return)))
    (setq qualifiers (nreverse qualifiers))
    (let* ((spec-ll (car rest))
           (body (cdr rest))
           (parsed (%parse-specialized-lambda-list spec-ll))
           (unspec-ll (car parsed))
           (spec-names (cdr parsed))
           ;; Block name must be a symbol; (setf foo) -> foo
           (block-name (if (and (consp name) (eq (car name) 'setf))
                           (cadr name)
                           name)))
      ;; Per CL spec, keyword checking for GFs is based on the union of
      ;; all applicable methods' keywords. Add &allow-other-keys so the
      ;; VM doesn't reject keywords accepted by other methods.
      (let ((effective-ll (if (and (member '&key unspec-ll)
                                   (not (member '&allow-other-keys unspec-ll)))
                              (append unspec-ll '(&allow-other-keys))
                              unspec-ll)))
        `(%add-method-to-gf
           ',name ',qualifiers ',spec-names
           (lambda ,effective-ll (block ,block-name ,@body))
           ',unspec-ll)))))

(defun %slot-access-protocol-gf-p (gf-name)
  "True when GF-NAME names one of the four slot-access protocol GFs."
  (or (eq gf-name 'slot-value-using-class)
      (eq gf-name 'slot-boundp-using-class)
      (eq gf-name 'slot-makunbound-using-class)
      (and (consp gf-name)
           (eq (car gf-name) 'setf)
           (eq (cadr gf-name) 'slot-value-using-class))))

(defun %install-method-in-gf (gf method)
  "Low-level install: put METHOD into GF's method list, replacing any
   method with matching qualifiers and specializers.  Invalidates the
   dispatch cache and recomputes cacheability.  Returns METHOD.
   Primitive; does not dispatch through the ADD-METHOD GF so it is safe
   during bootstrap and from DEFMETHOD expansion."
  (let ((qualifiers (method-qualifiers method))
        (specializers (method-specializers method))
        (gf-name (gf-name gf)))
    (%set-method-generic-function method gf)
    (%set-gf-methods gf
      (remove-if (lambda (m)
                   (and (equal (method-qualifiers m) qualifiers)
                        (equal (method-specializers m) specializers)))
                 (gf-methods gf)))
    (%set-gf-methods gf (cons method (gf-methods gf)))
    (%set-gf-dispatch-cache gf nil)
    (let ((mode (%compute-gf-cacheable-p gf)))
      (%set-gf-cacheable-p gf mode)
      (if (eq mode :eql)
          (%set-gf-eql-value-sets gf (%compute-eql-value-sets gf))
          (%set-gf-eql-value-sets gf nil)))
    (when (and (%slot-access-protocol-gf-p gf-name)
               (> (length (gf-methods gf)) 1))
      (setq *slot-access-protocol-extended-p* t))
    method))

(defun %uninstall-method-from-gf (gf method)
  "Low-level remove: drop METHOD (by EQ identity) from GF and refresh
   dispatch state.  Returns GF."
  (%set-gf-methods gf
    (remove method (gf-methods gf) :test #'eq))
  (%set-gf-dispatch-cache gf nil)
  (let ((mode (%compute-gf-cacheable-p gf)))
    (%set-gf-cacheable-p gf mode)
    (if (eq mode :eql)
        (%set-gf-eql-value-sets gf (%compute-eql-value-sets gf))
        (%set-gf-eql-value-sets gf nil)))
  ;; Clear the back-link so the method object no longer claims membership.
  (%set-method-generic-function method nil)
  gf)

(defun %add-method-to-gf (gf-name qualifiers specializer-names fn lambda-list)
  "Bridge used by DEFMETHOD expansion — construct the method struct and
   install it via the primitive install helper (bypasses the ADD-METHOD
   GF dispatch that is itself built on this path during bootstrap)."
  (let* ((gf (ensure-generic-function gf-name))
         (specializers (%resolve-specializers specializer-names))
         (method (%make-struct 'standard-method
                   gf specializers qualifiers fn lambda-list)))
    (%install-method-in-gf gf method)))

;;; ====================================================================
;;; Phase 6: with-slots
;;; ====================================================================

(defmacro with-slots (slot-entries instance-form &body body)
  "Evaluate BODY with slot names bound via symbol-macrolet.
   SLOT-ENTRIES is ((var slot) ...) or (slot-name ...).
   Each slot-name expands to (slot-value instance 'slot-name)."
  (let ((instance-var (gensym "INSTANCE"))
        (bindings nil))
    (dolist (entry slot-entries)
      (if (consp entry)
          ;; (variable-name slot-name)
          (push (list (car entry)
                      `(slot-value ,instance-var ',(cadr entry)))
                bindings)
          ;; plain slot-name — variable and slot are the same
          (push (list entry
                      `(slot-value ,instance-var ',entry))
                bindings)))
    `(let ((,instance-var ,instance-form))
       (symbol-macrolet ,(nreverse bindings)
         ,@body))))

(defmacro with-accessors (slot-entries instance-form &body body)
  "Evaluate BODY with accessor-based bindings via symbol-macrolet.
   SLOT-ENTRIES is ((var accessor) ...).
   Each var expands to (accessor instance)."
  (let ((instance-var (gensym "INSTANCE"))
        (bindings nil))
    (dolist (entry slot-entries)
      (let ((var (car entry))
            (accessor (cadr entry)))
        (push (list var `(,accessor ,instance-var))
              bindings)))
    `(let ((,instance-var ,instance-form))
       (symbol-macrolet ,(nreverse bindings)
         ,@body))))

;;; ====================================================================
;;; Phase 7: Convert to Generic Functions
;;; ====================================================================

;;; --- Convert initialize-instance and shared-initialize to GFs ---
;;; Save the original plain functions before defgeneric overwrites them.
;;; defun stores in the value cell; #' reads via OP_FLOAD (function cell
;;; first, then value cell). Capture via #' before defgeneric sets the
;;; function cell.

(let ((%si-fn #'shared-initialize))
  (defgeneric shared-initialize (instance slot-names &rest initargs))
  (defmethod shared-initialize ((instance t) slot-names &rest initargs)
    (apply %si-fn instance slot-names initargs)))

(defgeneric initialize-instance (instance &rest initargs))
(defmethod initialize-instance ((instance t) &rest initargs)
  (apply #'shared-initialize instance t initargs))

;;; --- slot-unbound as a generic function ---
;;; Upgrade the plain function to a GF so that classes can specialize
;;; it (e.g. for lazy slot initialization).
(let ((%su-fn #'slot-unbound))
  (defgeneric slot-unbound (class instance slot-name))
  (defmethod slot-unbound ((class t) instance slot-name)
    (funcall %su-fn class instance slot-name)))

;;; ====================================================================
;;; Slot-access protocol (MOP)
;;; ====================================================================
;;;
;;; AMOP §5.6 exposes raw slot access as a set of generic functions
;;; dispatching on (class instance effective-slot-definition). Libraries
;;; hook these for lazy slots, change tracking, persistence, memoization,
;;; and so on.
;;;
;;; The default methods encode the existing %STRUCT-REF / %STRUCT-SET
;;; behavior. SLOT-VALUE etc. normally bypass this dispatch (see
;;; *SLOT-ACCESS-PROTOCOL-EXTENDED-P*); the first user-installed method
;;; flips the flag and the slot-access wrappers start routing through
;;; the GFs. Struct slot access (non-CLOS) continues to bypass the
;;; protocol entirely — it has no effective-slot-definition to pass.

(defgeneric slot-value-using-class (class instance slot))
(defmethod slot-value-using-class ((class t) instance slot)
  (let* ((location (slot-definition-location slot))
         (val (if (consp location)
                  (cdr location)
                  (%struct-ref instance location))))
    (if (eq val *slot-unbound-marker*)
        (slot-unbound class instance (slot-definition-name slot))
        val)))

(defgeneric (setf slot-value-using-class) (new-value class instance slot))
(defmethod (setf slot-value-using-class) (new-value (class t) instance slot)
  (declare (ignore class))
  (let ((location (slot-definition-location slot)))
    (if (consp location)
        (rplacd location new-value)
        (%struct-set instance location new-value)))
  new-value)

(defgeneric slot-boundp-using-class (class instance slot))
(defmethod slot-boundp-using-class ((class t) instance slot)
  (declare (ignore class))
  (let ((location (slot-definition-location slot)))
    (not (eq (if (consp location)
                 (cdr location)
                 (%struct-ref instance location))
             *slot-unbound-marker*))))

(defgeneric slot-makunbound-using-class (class instance slot))
(defmethod slot-makunbound-using-class ((class t) instance slot)
  (declare (ignore class))
  (let ((location (slot-definition-location slot)))
    (if (consp location)
        (rplacd location *slot-unbound-marker*)
        (%struct-set instance location *slot-unbound-marker*)))
  instance)

;;; --- Upgrade slot-definition-class protocol to GFs ---
;;; So that libraries can customize the slot-definition class. User
;;; metaclasses are out of scope, so specialization is via :around on
;;; the standard-class specializer.
(defgeneric direct-slot-definition-class (class &rest initargs))
(defmethod direct-slot-definition-class ((class t) &rest initargs)
  (declare (ignore initargs))
  (find-class 'standard-direct-slot-definition))

(defgeneric effective-slot-definition-class (class &rest initargs))
(defmethod effective-slot-definition-class ((class t) &rest initargs)
  (declare (ignore initargs))
  (find-class 'standard-effective-slot-definition))

;;; --- class-slots is the AMOP name for the effective-slot accessor ---
(defun class-slots (class)
  "AMOP: return the list of effective slot definitions of CLASS."
  (class-effective-slots class))

;;; ====================================================================
;;; Class finalization protocol (MOP)
;;; ====================================================================
;;;
;;; AMOP exposes class finalization as a set of GFs so that user code
;;; (typically closer-mop callers — serapeum, trivia, …) can intercept
;;; CPL computation, effective-slot merging, and effective default-initargs.
;;;
;;; All defaults delegate to the internal helpers already used during
;;; %ENSURE-CLASS, so the happy path is unchanged. Specialization is on
;;; the class argument; since user-defined metaclasses are out of scope
;;; (see specs/mop.md), practical customization is via :around methods.

(defgeneric validate-superclass (class superclass))
(defmethod validate-superclass ((class t) (super t))
  "Accept all metaclass pairs — single-metaclass world."
  t)

(defgeneric compute-class-precedence-list (class))
(defmethod compute-class-precedence-list ((class t))
  (%compute-class-precedence-list class))

(defgeneric compute-effective-slot-definition (class name direct-slots))
(defmethod compute-effective-slot-definition ((class t) name direct-slots)
  (%compute-effective-slot-definition-default class name direct-slots))

(defgeneric compute-slots (class))
(defmethod compute-slots ((class t))
  ;; Re-implement the default here (rather than call %compute-slots-default)
  ;; so that the GF dispatches to COMPUTE-EFFECTIVE-SLOT-DEFINITION and user
  ;; methods on that GF are visible.
  (let* ((cpl (class-precedence-list class))
         (order nil)
         (groups (make-hash-table :test 'eq)))
    (dolist (c cpl)
      (dolist (dsd (class-direct-slots c))
        (let ((name (slot-definition-name dsd)))
          (unless (gethash name groups)
            (push name order))
          (setf (gethash name groups)
                (append (gethash name groups) (list dsd))))))
    (setq order (nreverse order))
    (mapcar (lambda (name)
              (compute-effective-slot-definition class name (gethash name groups)))
            order)))

(defgeneric compute-default-initargs (class))
(defmethod compute-default-initargs ((class t))
  (%compute-default-initargs-default class))

(defgeneric finalize-inheritance (class))
(defmethod finalize-inheritance ((class t))
  "Compute CPL, effective slots, and default initargs via the MOP GFs.
   Idempotent: already-finalized classes return immediately (AMOP §3.4.2)."
  (if (class-finalized-p class)
      class
      (progn
        (%set-class-cpl class (compute-class-precedence-list class))
        (let ((effective (compute-slots class)))
          (%set-class-effective-slots class effective)
          (%set-class-slot-index-table class (%build-slot-index-table effective))
          (%assign-slot-locations class effective))
        (%set-class-default-initargs class (compute-default-initargs class))
        (%set-class-finalized-p class t)
        class)))

(defgeneric class-prototype (class))
(defmethod class-prototype ((class t))
  "Return a (lazily allocated) prototype instance of CLASS. For classes
   that cannot be instantiated (built-ins without a slot-index-table),
   return NIL rather than signal — callers treat it as advisory."
  (or (%struct-ref class 8)
      (when (class-slot-index-table class)
        (let ((proto (allocate-instance class)))
          (%struct-set class 8 proto)
          proto))))

;;; --- ensure-class / ensure-class-using-class ---
;;; AMOP-style entry point. DEFCLASS expands into a call to ENSURE-CLASS
;;; so that :around methods on ENSURE-CLASS-USING-CLASS can observe the
;;; keyword args.

(defgeneric ensure-class-using-class (class name &rest keys))
(defmethod ensure-class-using-class ((class t) name &rest keys)
  (declare (ignore class))
  (let ((direct-supers (getf keys :direct-superclasses))
        (direct-slots  (getf keys :direct-slots))
        (direct-inits  (getf keys :direct-default-initargs)))
    (%ensure-class name direct-supers direct-slots direct-inits)))

(defun ensure-class (name &rest keys)
  "AMOP: look up the existing class (or NIL) and dispatch to
   ENSURE-CLASS-USING-CLASS."
  (apply #'ensure-class-using-class (find-class name nil) name keys))

;;; --- Update defclass to generate GF-based accessors ---
;;; Redefine the defclass macro to use defmethod instead of defun
;;; for :accessor, :reader, :writer

(defmacro defclass (name direct-superclasses slot-specifiers &rest class-options)
  "Define a new CLOS class (with generic function accessors)."
  (let ((accessor-defs nil)
        (slot-def-forms nil)
        (default-initarg-forms nil))
    ;; Parse class options
    (dolist (opt class-options)
      (when (and (consp opt) (eq (car opt) :default-initargs))
        ;; (:default-initargs :key1 val1 :key2 val2 ...)
        (let ((args (cdr opt)))
          (loop while args
                do (let ((key (pop args))
                         (val (pop args)))
                     (push `(list ',key (lambda () ,val)) default-initarg-forms))))))
    ;; Parse each slot specifier
    (dolist (spec slot-specifiers)
      (let* ((parsed (%parse-slot-spec spec))
             (slot-name (%slot-spec-name parsed))
             (accessors (%slot-spec-all-options parsed :accessor))
             (readers (%slot-spec-all-options parsed :reader))
             (writers (%slot-spec-all-options parsed :writer)))
        (push (%slot-spec->direct-def-form spec) slot-def-forms)
        ;; Generate GF-based accessor methods
        (dolist (accessor accessors)
          (push `(defgeneric ,accessor (obj)) accessor-defs)
          (push `(defmethod ,accessor ((obj ,name))
                   (slot-value obj ',slot-name))
                accessor-defs)
          ;; Writer: use defgeneric + defmethod for (setf accessor)
          ;; so additional methods can be added without replacing the original
          (push `(defgeneric (setf ,accessor) (val obj)) accessor-defs)
          (push `(defmethod (setf ,accessor) (val (obj ,name))
                   (setf (slot-value obj ',slot-name) val))
                accessor-defs))
        (dolist (reader readers)
          (push `(defgeneric ,reader (obj)) accessor-defs)
          (push `(defmethod ,reader ((obj ,name))
                   (slot-value obj ',slot-name))
                accessor-defs))
        (dolist (writer writers)
          (push `(defgeneric ,writer (val obj)) accessor-defs)
          (push `(defmethod ,writer (val (obj ,name))
                   (setf (slot-value obj ',slot-name) val))
                accessor-defs))))
    (setq slot-def-forms (nreverse slot-def-forms))
    (setq accessor-defs (nreverse accessor-defs))
    `(progn
       (ensure-class ',name
         :direct-superclasses ',direct-superclasses
         :direct-slots (list ,@slot-def-forms)
         :direct-default-initargs (list ,@(nreverse default-initarg-forms)))
       ,@accessor-defs
       (find-class ',name))))

;;; ====================================================================
;;; Phase 8: change-class + reinitialize-instance
;;; ====================================================================

(defgeneric reinitialize-instance (instance &rest initargs))
(defmethod reinitialize-instance ((instance t) &rest initargs)
  "Re-apply initargs to an existing instance."
  (apply #'shared-initialize instance nil initargs)
  instance)

(defgeneric change-class (instance new-class &rest initargs))
(defmethod change-class ((instance t) new-class &rest initargs)
  "Change INSTANCE to be an instance of NEW-CLASS.
   Modifies the instance in-place when the allocation has enough space,
   otherwise allocates a new struct."
  (let* ((new-class-obj (if (symbolp new-class)
                            (find-class new-class)
                            new-class))
         (old-class (class-of instance))
         (old-slots (class-effective-slots old-class))
         (new-slots (class-effective-slots new-class-obj))
         (new-type-name (class-name new-class-obj))
         (new-index-table (class-slot-index-table new-class-obj))
         (new-n-slots 0))
    (dolist (esd new-slots)
      (when (eq (slot-definition-allocation esd) :instance)
        (setq new-n-slots (+ new-n-slots 1))))
    ;; Capture surviving slot values as (new-esd . value) pairs.
    (let ((saved-values nil))
      (dolist (old-esd old-slots)
        (let* ((name (slot-definition-name old-esd))
               (old-loc (slot-definition-location old-esd))
               (old-val (if (consp old-loc)
                            (cdr old-loc)
                            (%struct-ref instance old-loc))))
          (unless (eq old-val *slot-unbound-marker*)
            (let ((new-esd (gethash name new-index-table)))
              (when new-esd
                (push (cons new-esd old-val) saved-values))))))
      (flet ((restore-to (target-instance)
               (dolist (pair saved-values)
                 (let ((loc (slot-definition-location (car pair))))
                   (if (consp loc)
                       (rplacd loc (cdr pair))
                       (%struct-set target-instance loc (cdr pair)))))))
        ;; Try in-place modification
        (if (%struct-change-class instance new-type-name new-n-slots)
            (progn
              (dotimes (i new-n-slots)
                (%struct-set instance i *slot-unbound-marker*))
              (restore-to instance)
              (apply #'shared-initialize instance t initargs)
              instance)
            (let ((new-instance (allocate-instance new-class-obj)))
              (restore-to new-instance)
              (apply #'shared-initialize new-instance t initargs)
              new-instance))))))

;;; ====================================================================
;;; Phase 9: print-object generic function
;;; ====================================================================

;;; print-object is a generic function. The *print-object-hook* in C
;;; calls it for struct objects. It must return a string (or NIL to
;;; fall through to default #S(...) printing).

(defgeneric print-object (object stream))

;;; Default method — return NIL to let the C printer handle it
(defmethod print-object ((object t) stream)
  nil)

;;; CLOS metaobject printing
(defmethod print-object ((object standard-class) stream)
  (concatenate 'string "#<STANDARD-CLASS "
               (symbol-name (class-name object)) ">"))

(defmethod print-object ((object standard-generic-function) stream)
  (concatenate 'string "#<STANDARD-GENERIC-FUNCTION "
               (symbol-name (gf-name object)) ">"))

(defmethod print-object ((object standard-direct-slot-definition) stream)
  (concatenate 'string "#<STANDARD-DIRECT-SLOT-DEFINITION "
               (symbol-name (slot-definition-name object)) ">"))

(defmethod print-object ((object standard-effective-slot-definition) stream)
  (concatenate 'string "#<STANDARD-EFFECTIVE-SLOT-DEFINITION "
               (symbol-name (slot-definition-name object)) ">"))

(defmethod print-object ((object standard-method) stream)
  (let* ((gf (method-function object))
         (specs (method-specializers object))
         (quals (method-qualifiers object))
         (spec-names (mapcar (lambda (s)
                               (cond
                                 ((eql-specializer-p s)
                                  (format nil "(EQL ~S)" (eql-specializer-object s)))
                                 ((and (structurep s)
                                       (eq (%struct-type-name s)
                                           'standard-class))
                                  (symbol-name (class-name s)))
                                 (t "?")))
                             specs)))
    (concatenate 'string "#<STANDARD-METHOD"
                 (if quals
                     (concatenate 'string " " (symbol-name (car quals)))
                     "")
                 " (" (format nil "~{~A~^ ~}" spec-names) ")>")))

;;; Wire up the C hook to call print-object.
;;; The hook must return a string (or NIL to fall through to default #S(...)).
;;; Uses with-output-to-string to capture stream-writing methods (CL protocol).
;;; Also checks the return value for methods that return a string directly.
(setq *print-object-hook*
  (lambda (obj)
    (let* ((result nil)
           (s (with-output-to-string (stream)
                (setq result (print-object obj stream)))))
      (cond
        ((and (stringp s) (> (length s) 0)) s)  ; stream-writing method
        ((stringp result) result)                 ; string-returning method
        (t nil)))))

;;; ====================================================================
;;; Funcallable instance protocol (MOP)
;;; ====================================================================
;;;
;;; AMOP §5.5 treats generic functions as callable metaobjects — the
;;; "funcallable instance" concept.  Our STANDARD-GENERIC-FUNCTION struct
;;; serves that role: its slot 3 (discriminating-function) is what the
;;; VM reaches when it sees a call whose operator is a GF metaobject
;;; (see cl_unwrap_funcallable in src/core/vm.c).

(defun set-funcallable-instance-function (gf fn)
  "AMOP: install FN as the discriminating function of GF.  Future calls
to GF invoke FN in place of the standard dispatch.  Returns GF."
  (%set-gf-discriminating-function gf fn)
  gf)

(defun standard-instance-access (instance location)
  "AMOP: unchecked slot access by integer location.  No bound check,
no GF dispatch — useful inside SLOT-VALUE-USING-CLASS methods that want
to sidestep the protocol they are implementing."
  (%struct-ref instance location))

(defun %set-standard-instance-access (instance location new-value)
  (%struct-set instance location new-value)
  new-value)

(defsetf standard-instance-access %set-standard-instance-access)

(defun funcallable-standard-instance-access (instance location)
  "AMOP: same as STANDARD-INSTANCE-ACCESS but typed for funcallable
metaobjects (e.g. generic functions)."
  (%struct-ref instance location))

(defsetf funcallable-standard-instance-access %set-standard-instance-access)

(defgeneric compute-discriminating-function (gf))
(defmethod compute-discriminating-function ((gf standard-generic-function))
  "AMOP: return the function that will be called when GF is invoked.
Default method returns the currently cached discriminating function;
user methods may specialise to return a customised dispatcher, which
can then be installed via SET-FUNCALLABLE-INSTANCE-FUNCTION."
  (gf-discriminating-function gf))

;;; ====================================================================
;;; Method metaobject protocol (MOP)
;;; ====================================================================
;;;
;;; AMOP §5.4 exposes the pieces of DEFMETHOD expansion as public hooks:
;;; ADD-METHOD / REMOVE-METHOD operate on already-constructed methods,
;;; EXTRACT-LAMBDA-LIST and EXTRACT-SPECIALIZER-NAMES pull a specialized
;;; lambda list apart, ENSURE-METHOD is the closer-mop convenience that
;;; ties them together, and MAKE-METHOD-LAMBDA is the hook a metaclass
;;; uses to rewrite the body of a method during code generation.
;;;
;;; DEFMETHOD itself does not dispatch through ADD-METHOD — it calls the
;;; same primitive (%INSTALL-METHOD-IN-GF) directly.  That keeps
;;; bootstrap simple (no chicken-and-egg when defining the GF), keeps
;;; DEFMETHOD fast, and matches what SBCL and CCL do.  User-installed
;;; (ADD-METHOD ...) still routes through the protocol so :around
;;; methods on ADD-METHOD take effect.

(defgeneric add-method (generic-function method))
(defmethod add-method ((gf standard-generic-function) (method standard-method))
  "AMOP: install METHOD in GF.  Returns GF."
  (%install-method-in-gf gf method)
  gf)

(defgeneric remove-method (generic-function method))
(defmethod remove-method ((gf standard-generic-function) (method standard-method))
  "AMOP: uninstall METHOD from GF.  Returns GF.  Uses EQ identity — a
method that is not installed is silently ignored, matching AMOP."
  (%uninstall-method-from-gf gf method))

(defgeneric find-method (generic-function qualifiers specializers &optional errorp))
(defmethod find-method ((gf standard-generic-function) qualifiers specializers
                        &optional (errorp t))
  "AMOP: locate the method on GF with the given QUALIFIERS list and
SPECIALIZERS list.  SPECIALIZERS may contain class objects, class
names, or (EQL value) / EQL-SPECIALIZER metaobjects — names are
resolved to metaobjects via %RESOLVE-SPECIALIZERS before comparison."
  (let* ((resolved (mapcar (lambda (s)
                             (cond
                               ((symbolp s) (find-class s))
                               ((and (consp s) (eq (car s) 'eql))
                                (intern-eql-specializer (cadr s)))
                               (t s)))
                           specializers))
         (match (find-if (lambda (m)
                           (and (equal (method-qualifiers m) qualifiers)
                                (equal (method-specializers m) resolved)))
                         (gf-methods gf))))
    (cond
      (match match)
      (errorp
       (error "No method on ~S with qualifiers ~S and specializers ~S"
              (gf-name gf) qualifiers specializers))
      (t nil))))

(defgeneric make-method-lambda (generic-function method lambda-expression environment))
(defmethod make-method-lambda ((gf standard-generic-function) (method standard-method)
                                lambda-expression environment)
  "AMOP: rewrite a method LAMBDA-EXPRESSION before it is compiled.
Default method returns LAMBDA-EXPRESSION and NIL — DEFMETHOD builds
the lambda directly and does not consult this GF unless a user method
overrides it.  Returns two values: the possibly-transformed lambda
expression and a list of extra initargs for MAKE-METHOD-LAMBDA callers."
  (declare (ignore gf method environment))
  (values lambda-expression nil))

(defun %gf-or-name (gf-or-name)
  "Resolve a GF designator to a standard-generic-function metaobject —
accepts a GF struct or a function-name (symbol or (SETF name))."
  (cond
    ((and (structurep gf-or-name)
          (eq (%struct-type-name gf-or-name) 'standard-generic-function))
     gf-or-name)
    (t (ensure-generic-function gf-or-name))))

(defun ensure-method (gf-or-name lambda-expression
                      &key (qualifiers '())
                           (lambda-list nil lambda-list-p)
                           (specializers nil specializers-p)
                           (method-class (find-class 'standard-method)))
  "closer-mop: construct a method from LAMBDA-EXPRESSION and install it
on GF-OR-NAME.  LAMBDA-EXPRESSION is either a lambda form or a
specialized lambda method form — if :lambda-list and :specializers are
not supplied, they are parsed from LAMBDA-EXPRESSION's first argument
list.  METHOD-CLASS is accepted for API completeness but is not used
since user-defined method classes are out of scope."
  (declare (ignore method-class))
  (let* ((gf (%gf-or-name gf-or-name))
         (ll (cond
               (lambda-list-p lambda-list)
               ((and (consp lambda-expression)
                     (eq (car lambda-expression) 'lambda))
                (cadr lambda-expression))
               (t (error "ENSURE-METHOD: :LAMBDA-LIST not supplied and ~S ~
                          is not a lambda expression" lambda-expression))))
         (specs (cond
                  (specializers-p specializers)
                  (t (make-list (length (extract-specializer-names ll))
                                :initial-element 't))))
         (resolved-specs (%resolve-specializers specs))
         (fn (cond
               ((functionp lambda-expression) lambda-expression)
               ((and (consp lambda-expression)
                     (eq (car lambda-expression) 'lambda))
                (eval lambda-expression))
               (t (error "ENSURE-METHOD: cannot coerce ~S to a method function"
                         lambda-expression))))
         (method (%make-struct 'standard-method
                   gf resolved-specs qualifiers fn ll)))
    (add-method gf method)
    method))

;;; --- Provide module ---
(provide "clos")
