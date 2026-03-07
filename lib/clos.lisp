;;;; CLOS Implementation for CL-Amiga
;;;; Practical subset: defclass, defgeneric, defmethod, make-instance,
;;;; slot-value, class-of, with-slots, standard method combination.
;;;; Loaded on demand via (require "clos").

;;; ====================================================================
;;; Step 2: Bootstrap Core Classes
;;; ====================================================================

;;; STANDARD-CLASS is a struct type with 10 slots.
;;; Slot layout:
;;;   0: name                 - symbol
;;;   1: direct-superclasses  - list of class objects
;;;   2: direct-slots         - list of canonical slot specs
;;;   3: cpl                  - class precedence list
;;;   4: effective-slots      - merged slots from CPL
;;;   5: slot-index-table     - hash table: slot-name -> index
;;;   6: direct-subclasses    - list of class objects
;;;   7: direct-methods       - list
;;;   8: prototype            - unused
;;;   9: finalized-p          - t or nil

(%register-struct-type 'standard-class 10 nil
  '((name nil) (direct-superclasses nil) (direct-slots nil)
    (cpl nil) (effective-slots nil) (slot-index-table nil)
    (direct-subclasses nil) (direct-methods nil)
    (prototype nil) (finalized-p nil)))

;; built-in-class: same layout as standard-class, used as metaclass for built-in types
(%register-struct-type 'built-in-class 10 'standard-class
  '((name nil) (direct-superclasses nil) (direct-slots nil)
    (cpl nil) (effective-slots nil) (slot-index-table nil)
    (direct-subclasses nil) (direct-methods nil)
    (prototype nil) (finalized-p nil)))

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
                 name                    ; 0: name
                 direct-superclasses     ; 1: direct-superclasses
                 nil                     ; 2: direct-slots
                 nil                     ; 3: cpl (set below)
                 nil                     ; 4: effective-slots
                 nil                     ; 5: slot-index-table
                 nil                     ; 6: direct-subclasses
                 nil                     ; 7: direct-methods
                 nil                     ; 8: prototype
                 t)))                    ; 9: finalized-p
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

;; Other built-in classes
(%make-bootstrap-class 'symbol
  (list (find-class 't)))
(%make-bootstrap-class 'null
  (list (find-class 'symbol)))
(%make-bootstrap-class 'cons
  (list (find-class 't)))
(%make-bootstrap-class 'list
  (list (find-class 't)))
(%make-bootstrap-class 'string
  (list (find-class 't)))
(%make-bootstrap-class 'character
  (list (find-class 't)))
(%make-bootstrap-class 'vector
  (list (find-class 't)))
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
(%make-bootstrap-class 'print-not-readable
  (list (find-class 'error)))

;;; --- Register condition types as CLOS classes (for method dispatch) ---

(defun %register-condition-class (name direct-super-names)
  "Register a condition type as a CLOS class for method dispatch."
  (unless (find-class name nil)
    (let* ((supers (mapcar #'find-class direct-super-names))
           (class (%make-struct 'standard-class
                    name supers nil nil nil nil nil nil nil t)))
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

(defun slot-value (instance slot-name)
  "Return the value of SLOT-NAME in INSTANCE."
  (let* ((class (class-of instance))
         (index-table (class-slot-index-table class)))
    (unless index-table
      (error "~S has no slot-index-table (not a CLOS instance)" instance))
    (multiple-value-bind (index found-p)
        (gethash slot-name index-table)
      (unless found-p
        (error "~S has no slot named ~S" instance slot-name))
      (let ((val (%struct-ref instance index)))
        (if (eq val *slot-unbound-marker*)
            (error "The slot ~S is unbound in ~S" slot-name instance)
            val)))))

(defun %set-slot-value (instance slot-name new-value)
  "Set the value of SLOT-NAME in INSTANCE to NEW-VALUE."
  (let* ((class (class-of instance))
         (index-table (class-slot-index-table class)))
    (unless index-table
      (error "~S has no slot-index-table (not a CLOS instance)" instance))
    (multiple-value-bind (index found-p)
        (gethash slot-name index-table)
      (unless found-p
        (error "~S has no slot named ~S" instance slot-name))
      (%struct-set instance index new-value)
      new-value)))

(defsetf slot-value %set-slot-value)

(defun slot-boundp (instance slot-name)
  "Return T if SLOT-NAME is bound in INSTANCE."
  (let* ((class (class-of instance))
         (index-table (class-slot-index-table class)))
    (unless index-table
      (error "~S has no slot-index-table (not a CLOS instance)" instance))
    (multiple-value-bind (index found-p)
        (gethash slot-name index-table)
      (unless found-p
        (error "~S has no slot named ~S" instance slot-name))
      (not (eq (%struct-ref instance index) *slot-unbound-marker*)))))

(defun slot-makunbound (instance slot-name)
  "Make SLOT-NAME unbound in INSTANCE."
  (let* ((class (class-of instance))
         (index-table (class-slot-index-table class)))
    (unless index-table
      (error "~S has no slot-index-table (not a CLOS instance)" instance))
    (multiple-value-bind (index found-p)
        (gethash slot-name index-table)
      (unless found-p
        (error "~S has no slot named ~S" instance slot-name))
      (%struct-set instance index *slot-unbound-marker*)
      instance)))

(defun slot-exists-p (instance slot-name)
  "Return T if INSTANCE has a slot named SLOT-NAME."
  (let* ((class (class-of instance))
         (index-table (class-slot-index-table class)))
    (if index-table
        (multiple-value-bind (index found-p)
            (gethash slot-name index-table)
          found-p)
        nil)))

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

;;; --- Effective slot computation ---

(defun %merge-slot-option (child-spec parent-spec key)
  "If CHILD-SPEC lacks KEY, inherit it from PARENT-SPEC."
  (if (and (not (%slot-spec-has-option-p child-spec key))
           (%slot-spec-has-option-p parent-spec key))
      (append child-spec (list key (%slot-spec-option parent-spec key)))
      child-spec))

(defun %merge-slot-specs (child-spec parent-spec)
  "Merge parent slot options into child spec (child takes precedence)."
  (let ((result child-spec))
    (dolist (key '(:initarg :initform :type :documentation))
      (setq result (%merge-slot-option result parent-spec key)))
    result))

(defun %compute-effective-slots (cpl)
  "Compute effective slots from class precedence list.
   Walk CPL from most-specific to least-specific, collecting slots.
   If a slot name already exists, merge options (subclass wins)."
  (let ((effective nil))
    (dolist (class cpl)
      (dolist (slot (class-direct-slots class))
        (let* ((name (%slot-spec-name slot))
               (existing (assoc name effective :test #'eq)))
          (if existing
              ;; Merge parent options into existing (child already there)
              (let ((merged (%merge-slot-specs existing slot)))
                (setq effective
                      (mapcar (lambda (s)
                                (if (eq (car s) name) merged s))
                              effective)))
              ;; New slot — add to end
              (setq effective (append effective (list slot)))))))
    effective))

;;; --- Build slot-index-table ---

(defun %build-slot-index-table (effective-slots)
  "Build hash table mapping slot-name -> index from effective slots list."
  (let ((table (make-hash-table :test 'eq))
        (i 0))
    (dolist (slot effective-slots)
      (setf (gethash (%slot-spec-name slot) table) i)
      (setq i (+ i 1)))
    table))

;;; --- Class creation at runtime ---

(defun %ensure-class (name direct-super-names direct-slots initform-alist)
  "Create or update a CLOS class. Called by defclass expansion."
  (let* ((supers (if direct-super-names
                     (mapcar #'find-class direct-super-names)
                     (list (find-class 'standard-object))))
         ;; Create a temporary class to compute CPL
         (class (%make-struct 'standard-class
                  name supers direct-slots
                  nil nil nil nil nil nil nil))
         (cpl (%compute-class-precedence-list class))
         (effective (%compute-effective-slots cpl))
         (n-slots (length effective))
         (index-table (%build-slot-index-table effective))
         ;; Build slot specs for struct registration: ((name default) ...)
         (struct-slot-specs
           (mapcar (lambda (s) (list (%slot-spec-name s) nil))
                   effective)))
    ;; Patch initform functions into direct-slots
    (dolist (pair initform-alist)
      (let* ((slot-name (car pair))
             (initfn (cdr pair))
             (spec (assoc slot-name direct-slots :test #'eq)))
        (when spec
          ;; Store the initform function under :initform-function
          (nconc spec (list :initform-function initfn)))))
    ;; Also propagate initform-functions into effective slots
    ;; Walk effective slots and find initform-functions from CPL
    (dolist (eslot effective)
      (unless (%slot-spec-has-option-p eslot :initform-function)
        ;; Search CPL for a class that defines this slot with an initform
        (let ((slot-name (%slot-spec-name eslot)))
          (dolist (c cpl)
            (let ((dslot (assoc slot-name (class-direct-slots c) :test #'eq)))
              (when (and dslot (%slot-spec-has-option-p dslot :initform-function))
                (nconc eslot (list :initform-function
                                   (%slot-spec-option dslot :initform-function)))
                (return)))))))
    ;; Register struct type (pass first direct super for typep hierarchy)
    (%register-struct-type name n-slots
                           (if direct-super-names (car direct-super-names) nil)
                           struct-slot-specs)
    ;; Fill in class metaobject
    (%set-class-cpl class cpl)
    (%set-class-effective-slots class effective)
    (%set-class-slot-index-table class index-table)
    (%set-class-finalized-p class t)
    ;; Register class
    (setf (find-class name) class)
    ;; Register as subclass of each direct super
    (dolist (super supers)
      (%set-class-direct-subclasses super
        (cons class (class-direct-subclasses super))))
    class))

;;; --- defclass macro ---

(defmacro defclass (name direct-superclasses slot-specifiers &rest class-options)
  "Define a new CLOS class."
  (let ((accessor-defs nil)
        (parsed-slots nil)
        (initform-pairs nil))
    ;; Parse each slot specifier
    (dolist (spec slot-specifiers)
      (let* ((parsed (%parse-slot-spec spec))
             (slot-name (%slot-spec-name parsed))
             (accessor (%slot-spec-option parsed :accessor))
             (reader (%slot-spec-option parsed :reader))
             (writer (%slot-spec-option parsed :writer)))
        (push parsed parsed-slots)
        ;; Collect initform lambdas
        (when (%slot-spec-has-option-p parsed :initform)
          (push `(cons ',slot-name
                       (lambda () ,(%slot-spec-option parsed :initform)))
                initform-pairs))
        ;; Generate accessor functions
        (when accessor
          (let ((setter-name (intern (concatenate 'string
                                      "%SET-" (symbol-name accessor)))))
            (push `(defun ,accessor (obj)
                     (slot-value obj ',slot-name))
                  accessor-defs)
            (push `(defun ,setter-name (obj val)
                     (setf (slot-value obj ',slot-name) val))
                  accessor-defs)
            (push `(defsetf ,accessor ,setter-name) accessor-defs)))
        (when reader
          (push `(defun ,reader (obj)
                   (slot-value obj ',slot-name))
                accessor-defs))
        (when writer
          (push `(defun ,writer (val obj)
                   (setf (slot-value obj ',slot-name) val))
                accessor-defs))))
    (setq parsed-slots (nreverse parsed-slots))
    (setq accessor-defs (nreverse accessor-defs))
    `(progn
       (%ensure-class ',name
                      ',direct-superclasses
                      ',parsed-slots
                      (list ,@(nreverse initform-pairs)))
       ,@accessor-defs
       (find-class ',name))))

;;; ====================================================================
;;; Phase 4: make-instance + Initialization
;;; ====================================================================

(defun allocate-instance (class)
  "Allocate a fresh instance of CLASS with all slots unbound."
  (let* ((name (class-name class))
         (effective (class-effective-slots class))
         (n (length effective))
         (args (make-list n :initial-element *slot-unbound-marker*)))
    (apply #'%make-struct name args)))

(defun %initarg-to-slot-index (class initarg)
  "Find the slot index for INITARG in CLASS, or NIL."
  (let ((effective (class-effective-slots class))
        (i 0))
    (dolist (slot effective)
      (when (eq (%slot-spec-option slot :initarg) initarg)
        (return-from %initarg-to-slot-index i))
      (setq i (+ i 1)))
    nil))

(defun shared-initialize (instance slot-names &rest initargs)
  "Initialize slots of INSTANCE from INITARGS and initforms."
  (let* ((class (class-of instance))
         (effective (class-effective-slots class))
         (i 0))
    (dolist (slot effective)
      (let* ((slot-name (%slot-spec-name slot))
             (initarg (%slot-spec-option slot :initarg))
             (initarg-val nil)
             (initarg-supplied nil))
        ;; Check if initarg was supplied
        (when initarg
          (let ((tail (member initarg initargs)))
            (when tail
              (setq initarg-val (cadr tail))
              (setq initarg-supplied t))))
        (cond
          ;; Initarg supplied — always use it
          (initarg-supplied
           (%struct-set instance i initarg-val))
          ;; Slot is unbound and has initform — apply it
          ((and (or (eq slot-names t)
                    (member slot-name slot-names :test #'eq))
                (eq (%struct-ref instance i) *slot-unbound-marker*)
                (%slot-spec-has-option-p slot :initform-function))
           (%struct-set instance i
                        (funcall (%slot-spec-option slot :initform-function))))))
      (setq i (+ i 1)))
    instance))

(defun initialize-instance (instance &rest initargs)
  "Initialize a newly allocated instance."
  (apply #'shared-initialize instance t initargs))

(defun make-instance (class-or-name &rest initargs)
  "Create a new instance of CLASS-OR-NAME with INITARGS."
  (let* ((class (if (symbolp class-or-name)
                    (find-class class-or-name)
                    class-or-name))
         (instance (allocate-instance class)))
    ;; Uses initialize-instance which is initially a plain function,
    ;; then upgraded to a GF in Phase 7
    (apply #'initialize-instance instance initargs)
    instance))

;;; ====================================================================
;;; Phase 5: defgeneric + defmethod + Dispatch
;;; ====================================================================

;;; --- Generic function and method struct types ---

;;; standard-generic-function: 5 slots
;;;   0: name, 1: lambda-list, 2: methods, 3: discriminating-function,
;;;   4: method-combination
(%register-struct-type 'standard-generic-function 5 nil
  '((name nil) (lambda-list nil) (methods nil)
    (discriminating-function nil) (method-combination nil)))

;;; standard-method: 5 slots
;;;   0: generic-function, 1: specializers, 2: qualifiers, 3: function,
;;;   4: lambda-list
(%register-struct-type 'standard-method 5 nil
  '((generic-function nil) (specializers nil) (qualifiers nil)
    (function nil) (lambda-list nil)))

;;; Register these as classes so dispatch works on them
(%make-bootstrap-class 'standard-generic-function
  (list (find-class 'standard-object)))
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
          ;; EQL specializer: (eql value)
          ((and (consp spec) (eq (car spec) 'eql))
           (unless (eql arg (cadr spec))
             (setq applicable nil)
             (return nil)))
          ;; Class specializer
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
        (unless (eq c1 c2)
          ;; EQL specializers are more specific than class specializers
          (cond
            ((and (consp c1) (eq (car c1) 'eql)) (return t))
            ((and (consp c2) (eq (car c2) 'eql)) (return nil))
            (t
             ;; Compare by position in arg's CPL
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

(defun %call-with-method-combination (methods args)
  "Execute standard method combination on sorted applicable METHODS."
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
    ;; after is already most-specific-first; we want least-specific-first
    ;; (after was pushed most-specific-first from sorted list, so it's reversed)
    ;; Actually: methods are sorted most-specific-first, we push them, so
    ;; after list is reversed (least-specific-first). That's what we want.
    (setq around (nreverse around))
    (unless primary
      (error "No applicable primary method"))
    ;; Build the effective method
    (let* ((primary-chain
             (%make-method-chain primary args))
           (call-primary
             (lambda (&rest args)
               ;; Execute :before methods
               (dolist (m before)
                 (apply (method-function m) args))
               ;; Execute primary chain (preserve multiple values)
               (let ((results (multiple-value-list (apply primary-chain args))))
                 ;; Execute :after methods
                 (dolist (m after)
                   (apply (method-function m) args))
                 (values-list results)))))
      (if around
          ;; Wrap :around methods
          (apply (%make-around-chain around call-primary args) args)
          (apply call-primary args)))))

(defun %make-method-chain (methods args)
  "Build a call-next-method chain from primary methods."
  (if (null methods)
      (lambda (&rest args)
        (error "No next method"))
      (let* ((m (car methods))
             (rest-chain (%make-method-chain (cdr methods) args)))
        (lambda (&rest call-args)
          (let* ((actual-args (or call-args args))
                 (*call-next-method-function* rest-chain)
                 (*call-next-method-args* actual-args)
                 (*next-method-p-function*
                   (lambda () (not (null (cdr methods))))))
            (apply (method-function m) actual-args))))))

(defun %make-around-chain (around-methods inner args)
  "Build an :around chain that wraps INNER."
  (if (null around-methods)
      inner
      (let* ((m (car around-methods))
             (rest-chain (%make-around-chain (cdr around-methods) inner args)))
        (lambda (&rest call-args)
          (let* ((actual-args (or call-args args))
                 (*call-next-method-function* rest-chain)
                 (*call-next-method-args* actual-args)
                 (*next-method-p-function* (lambda () t)))
            (apply (method-function m) actual-args))))))

;;; --- GF dispatch ---

(defun %gf-dispatch (gf args)
  "Dispatch a generic function call."
  (let ((methods (%compute-applicable-methods gf args)))
    (when (null methods)
      (error "No applicable method for ~S with args ~S"
             (gf-name gf) args))
    (%call-with-method-combination methods args)))

;;; --- ensure-generic-function ---

(defun ensure-generic-function (name &key lambda-list)
  "Find or create a generic function named NAME."
  (multiple-value-bind (existing found-p)
      (gethash name *generic-function-table*)
    (if found-p
        existing
        (let* ((gf (%make-struct 'standard-generic-function
                     name lambda-list nil nil nil))
               (dispatch-fn
                 (lambda (&rest args)
                   (%gf-dispatch gf args))))
          (%set-gf-discriminating-function gf dispatch-fn)
          (setf (gethash name *generic-function-table*) gf)
          (cond
            ((symbolp name)
             (setf (symbol-function name) dispatch-fn))
            ;; (setf accessor) — install dispatch fn on hidden symbol
            ((and (consp name) (eq (car name) 'setf) (consp (cdr name)))
             (let* ((accessor (cadr name))
                    (hidden-name (concatenate 'string "%SETF-" (symbol-name accessor)))
                    (hidden-sym (intern hidden-name "COMMON-LISP")))
               (setf (symbol-function hidden-sym) dispatch-fn)
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
  "Resolve specializer names to class objects.
   Symbols -> (find-class sym), (eql val) stays as-is."
  (mapcar (lambda (s)
            (if (and (consp s) (eq (car s) 'eql))
                s
                (find-class s)))
          specializer-names))

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

(defun %add-method-to-gf (gf-name qualifiers specializer-names fn lambda-list)
  "Add a method to the named generic function."
  (let* ((gf (ensure-generic-function gf-name))
         (specializers (%resolve-specializers specializer-names))
         (method (%make-struct 'standard-method
                   gf specializers qualifiers fn lambda-list)))
    ;; Remove existing method with same specializers and qualifiers
    (%set-gf-methods gf
      (remove-if (lambda (m)
                   (and (equal (method-qualifiers m) qualifiers)
                        (equal (method-specializers m) specializers)))
                 (gf-methods gf)))
    ;; Add new method
    (%set-gf-methods gf (cons method (gf-methods gf)))
    method))

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

;;; --- Update defclass to generate GF-based accessors ---
;;; Redefine the defclass macro to use defmethod instead of defun
;;; for :accessor, :reader, :writer

(defmacro defclass (name direct-superclasses slot-specifiers &rest class-options)
  "Define a new CLOS class (with generic function accessors)."
  (let ((accessor-defs nil)
        (parsed-slots nil)
        (initform-pairs nil))
    ;; Parse each slot specifier
    (dolist (spec slot-specifiers)
      (let* ((parsed (%parse-slot-spec spec))
             (slot-name (%slot-spec-name parsed))
             (accessor (%slot-spec-option parsed :accessor))
             (reader (%slot-spec-option parsed :reader))
             (writer (%slot-spec-option parsed :writer)))
        (push parsed parsed-slots)
        ;; Collect initform lambdas
        (when (%slot-spec-has-option-p parsed :initform)
          (push `(cons ',slot-name
                       (lambda () ,(%slot-spec-option parsed :initform)))
                initform-pairs))
        ;; Generate GF-based accessor methods
        (when accessor
          (let ((setter-name (intern (concatenate 'string
                                      "%SET-" (symbol-name accessor)))))
            (push `(defgeneric ,accessor (obj)) accessor-defs)
            (push `(defmethod ,accessor ((obj ,name))
                     (slot-value obj ',slot-name))
                  accessor-defs)
            (push `(defun ,setter-name (obj val)
                     (setf (slot-value obj ',slot-name) val))
                  accessor-defs)
            (push `(defsetf ,accessor ,setter-name) accessor-defs)))
        (when reader
          (push `(defgeneric ,reader (obj)) accessor-defs)
          (push `(defmethod ,reader ((obj ,name))
                   (slot-value obj ',slot-name))
                accessor-defs))
        (when writer
          (push `(defgeneric ,writer (val obj)) accessor-defs)
          (push `(defmethod ,writer (val (obj ,name))
                   (setf (slot-value obj ',slot-name) val))
                accessor-defs))))
    (setq parsed-slots (nreverse parsed-slots))
    (setq accessor-defs (nreverse accessor-defs))
    `(progn
       (%ensure-class ',name
                      ',direct-superclasses
                      ',parsed-slots
                      (list ,@(nreverse initform-pairs)))
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
         (new-n-slots (length new-slots))
         (new-type-name (class-name new-class-obj))
         (new-index-table (class-slot-index-table new-class-obj)))
    ;; Save old slot values BEFORE changing class
    (let ((saved-values nil))
      (let ((old-i 0))
        (dolist (old-slot old-slots)
          (let ((name (%slot-spec-name old-slot)))
            (multiple-value-bind (new-i found-p)
                (gethash name new-index-table)
              (when (and found-p
                         (not (eq (%struct-ref instance old-i)
                                  *slot-unbound-marker*)))
                (push (cons new-i (%struct-ref instance old-i)) saved-values))))
          (setq old-i (+ old-i 1))))
      ;; Try in-place modification
      (if (%struct-change-class instance new-type-name new-n-slots)
          (progn
            ;; Clear all slots to unbound
            (dotimes (i new-n-slots)
              (%struct-set instance i *slot-unbound-marker*))
            ;; Restore saved shared slot values
            (dolist (pair saved-values)
              (%struct-set instance (car pair) (cdr pair)))
            ;; Initialize remaining slots from initargs and initforms
            (apply #'shared-initialize instance t initargs)
            instance)
        ;; Fallback: allocate new instance
        (let ((new-instance (allocate-instance new-class-obj))
              (old-i 0))
          (dolist (old-slot old-slots)
            (let ((name (%slot-spec-name old-slot)))
              (multiple-value-bind (new-i found-p)
                  (gethash name new-index-table)
                (when (and found-p
                           (not (eq (%struct-ref instance old-i)
                                    *slot-unbound-marker*)))
                  (%struct-set new-instance new-i
                               (%struct-ref instance old-i)))))
            (setq old-i (+ old-i 1)))
          (apply #'shared-initialize new-instance t initargs)
          new-instance)))))

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

(defmethod print-object ((object standard-method) stream)
  (let* ((gf (method-function object))
         (specs (method-specializers object))
         (quals (method-qualifiers object))
         (spec-names (mapcar (lambda (s)
                               (if (and (structurep s)
                                        (eq (%struct-type-name s)
                                            'standard-class))
                                   (symbol-name (class-name s))
                                   (if (consp s)
                                       (format nil "(EQL ~S)" (cadr s))
                                       "?")))
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

;;; --- Provide module ---
(provide "clos")
