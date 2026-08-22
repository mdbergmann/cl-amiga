;;; ffi.lisp — Platform-independent FFI utilities for CL-Amiga
;;;
;;; Loaded via (require "ffi").
;;; Builds on the C-level FFI package builtins (make-foreign-pointer,
;;; peek/poke, alloc/free, foreign-string, etc.)

(in-package "FFI")

;;; ================================================================
;;; defcstruct — define C struct field accessors
;;; ================================================================

;;; (ffi:defcstruct point
;;;   (x :u16 0)
;;;   (y :u16 2))
;;;
;;; Defines accessor functions:
;;;   (point-x ptr)           → (ffi:peek-u16 ptr 0)
;;;   (setf (point-x ptr) v) → (ffi:poke-u16 ptr v 0)
;;;   *point-size*            → 4 (computed from last field + size)
;;;
;;; Field types:
;;;   :u8 :i8 :u16 :i16 :u32 :i32   integers (peek/poke of that width;
;;;                                 the signed variants sign-extend)
;;;   :pointer                      32-bit address as an INTEGER (legacy
;;;                                 spelling kept for existing code)
;;;   :fptr                         foreign pointer object: the getter
;;;                                 returns NIL for NULL, the setter takes
;;;                                 a foreign pointer, an integer address
;;;                                 or NIL
;;;   :single :double               IEEE floats
;;;   (:struct N)                   embedded sub-struct of N bytes: the
;;;                                 getter returns a foreign pointer to
;;;                                 the field's address; no setter
;;;   (:array T N)                  N inline elements of scalar type T:
;;;                                 getter/setter take an extra index arg
;;;
;;; The name may be (name :size N) to pin the struct size to the
;;; authoritative sizeof instead of deriving it from the last field
;;; (C padding / alignment):
;;;   (ffi:defcstruct (window :size 136) ...)

(defun %cstruct-scalar-size (type)
  "Return byte size for a scalar C type keyword, or NIL if not scalar."
  (case type
    ((:u8 :i8) 1)
    ((:u16 :i16) 2)
    ((:u32 :i32 :pointer :fptr :single) 4)
    (:double 8)
    (otherwise nil)))

(defun %cstruct-type-size (type)
  "Return byte size for a C type spec (scalar keyword, (:struct N) or
\(:array TYPE N))."
  (cond ((keywordp type)
         (or (%cstruct-scalar-size type)
             (error "FFI:DEFCSTRUCT: unknown type ~S" type)))
        ((and (consp type) (eq (first type) :struct)
              (integerp (second type)) (>= (second type) 0))
         (second type))
        ((and (consp type) (eq (first type) :array)
              (%cstruct-scalar-size (second type))
              (integerp (third type)) (> (third type) 0))
         (* (%cstruct-scalar-size (second type)) (third type)))
        (t (error "FFI:DEFCSTRUCT: unknown type ~S" type))))

(defun %cstruct-peek-fn (type)
  "Return the peek function name for a scalar C type."
  (case type
    (:u8 'ffi:peek-u8)   (:i8 'ffi:peek-i8)
    (:u16 'ffi:peek-u16) (:i16 'ffi:peek-i16)
    (:u32 'ffi:peek-u32) (:i32 'ffi:peek-i32)
    (:pointer 'ffi:peek-u32)
    (:fptr 'ffi::%peek-fptr)
    (:single 'ffi:peek-single) (:double 'ffi:peek-double)
    (otherwise (error "FFI:DEFCSTRUCT: unknown type ~S" type))))

(defun %cstruct-poke-fn (type)
  "Return the poke function name for a scalar C type."
  (case type
    (:u8 'ffi:poke-u8)   (:i8 'ffi:poke-i8)
    (:u16 'ffi:poke-u16) (:i16 'ffi:poke-i16)
    (:u32 'ffi:poke-u32) (:i32 'ffi:poke-i32)
    (:pointer 'ffi:poke-u32)
    (:fptr 'ffi::%poke-fptr)
    (:single 'ffi:poke-single) (:double 'ffi:poke-double)
    (otherwise (error "FFI:DEFCSTRUCT: unknown type ~S" type))))

(defun %peek-fptr (ptr offset)
  "Read a 32-bit address at PTR+OFFSET as a foreign pointer; NIL for NULL."
  (let ((addr (ffi:peek-u32 ptr offset)))
    (if (zerop addr) nil (ffi:make-foreign-pointer addr))))

(defun %poke-fptr (ptr val offset)
  "Store VAL — a foreign pointer, an integer address or NIL — at PTR+OFFSET."
  (ffi:poke-u32 ptr
                (cond ((null val) 0)
                      ((ffi:foreign-pointer-p val)
                       (ffi:foreign-pointer-address val))
                      ((integerp val) val)
                      (t (error "FFI: pointer field value must be a foreign pointer, an integer address or NIL, got ~S" val)))
                offset)
  val)

(defmacro defcstruct (name-spec &body fields)
  "Define a C struct layout with named field accessors.
NAME-SPEC is a symbol or (NAME :size BYTES).  Each field is
\(field-name type offset); see the type table above."
  (let* ((name (if (consp name-spec) (first name-spec) name-spec))
         (explicit-size (and (consp name-spec) (getf (rest name-spec) :size)))
         (size-var (intern (format nil "*~A-SIZE*" name)))
         (forms nil)
         (max-end 0))
    (dolist (field fields)
      (destructuring-bind (field-name field-type field-offset) field
        (let* ((end (+ field-offset (%cstruct-type-size field-type)))
               (accessor (intern (format nil "~A-~A" name field-name)))
               (setter-name (intern (format nil "%SET-~A-~A" name field-name))))
          (when (> end max-end) (setf max-end end))
          (cond
            ;; Embedded struct: pointer to the field, no setter.
            ((and (consp field-type) (eq (first field-type) :struct))
             (push `(defun ,accessor (ptr)
                      (ffi:make-foreign-pointer
                       (+ (ffi:foreign-pointer-address ptr) ,field-offset)))
                   forms))
            ;; Inline array of scalars: indexed getter/setter.
            ((and (consp field-type) (eq (first field-type) :array))
             (let* ((elt-type (second field-type))
                    (elt-size (%cstruct-scalar-size elt-type))
                    (peek-fn (%cstruct-peek-fn elt-type))
                    (poke-fn (%cstruct-poke-fn elt-type)))
               (push `(defun ,accessor (ptr index)
                        (,peek-fn ptr (+ ,field-offset (* index ,elt-size))))
                     forms)
               (push `(defun ,setter-name (ptr index val)
                        (,poke-fn ptr val (+ ,field-offset (* index ,elt-size)))
                        val)
                     forms)
               (push `(defsetf ,accessor ,setter-name) forms)))
            ;; Scalar field.
            (t
             (let ((peek-fn (%cstruct-peek-fn field-type))
                   (poke-fn (%cstruct-poke-fn field-type)))
               (push `(defun ,accessor (ptr)
                        (,peek-fn ptr ,field-offset))
                     forms)
               (push `(defun ,setter-name (ptr val)
                        (,poke-fn ptr val ,field-offset)
                        val)
                     forms)
               (push `(defsetf ,accessor ,setter-name) forms)))))))
    `(progn
       (defvar ,size-var ,(or explicit-size max-end))
       ,@(nreverse forms)
       ',name)))

(export '(defcstruct))

;;; ================================================================
;;; with-foreign-alloc — scoped foreign memory
;;; ================================================================

(defmacro with-foreign-alloc ((var size) &body body)
  "Allocate SIZE bytes of foreign memory, bind to VAR, free on exit."
  `(let ((,var (ffi:alloc-foreign ,size)))
     (unwind-protect
       (progn ,@body)
       (ffi:free-foreign ,var))))

(export '(with-foreign-alloc))

;;; ================================================================
;;; with-foreign-string — scoped foreign string
;;; ================================================================

(defmacro with-foreign-string ((var string) &body body)
  "Copy STRING to null-terminated foreign memory, bind to VAR, free on exit."
  `(let ((,var (ffi:foreign-string ,string)))
     (unwind-protect
       (progn ,@body)
       (ffi:free-foreign ,var))))

(export '(with-foreign-string))

;;; ================================================================
;;; Provide module
;;; ================================================================

(provide "ffi")
