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

(defun find-class (name &optional (errorp t))
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

;;; --- class-of ---

(defun class-of (object)
  "Return the class of OBJECT."
  (let ((type-name (%class-of object)))
    (or (gethash type-name *class-table*)
        (find-class 't))))

;;; --- Provide module ---
(provide "clos")
