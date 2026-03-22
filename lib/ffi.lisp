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

(defun %cstruct-type-size (type)
  "Return byte size for a C type keyword."
  (case type
    (:u8  1)
    (:i8  1)
    (:u16 2)
    (:i16 2)
    (:u32 4)
    (:i32 4)
    (:pointer 4)
    (otherwise (error "FFI:DEFCSTRUCT: unknown type ~S" type))))

(defun %cstruct-peek-fn (type)
  "Return the peek function name for a C type."
  (case type
    ((:u8 :i8)        'ffi:peek-u8)
    ((:u16 :i16)      'ffi:peek-u16)
    ((:u32 :i32 :pointer) 'ffi:peek-u32)
    (otherwise (error "FFI:DEFCSTRUCT: unknown type ~S" type))))

(defun %cstruct-poke-fn (type)
  "Return the poke function name for a C type."
  (case type
    ((:u8 :i8)        'ffi:poke-u8)
    ((:u16 :i16)      'ffi:poke-u16)
    ((:u32 :i32 :pointer) 'ffi:poke-u32)
    (otherwise (error "FFI:DEFCSTRUCT: unknown type ~S" type))))

(defmacro defcstruct (name &body fields)
  "Define a C struct layout with named field accessors.
Each field is (field-name type offset) where type is :u8/:u16/:u32/:pointer etc."
  (let ((size-var (intern (format nil "*~A-SIZE*" name)))
        (forms nil)
        (max-end 0))
    ;; Compute struct size from fields
    (dolist (field fields)
      (destructuring-bind (field-name field-type field-offset) field
        (let* ((end (+ field-offset (%cstruct-type-size field-type)))
               (accessor (intern (format nil "~A-~A" name field-name)))
               (peek-fn (%cstruct-peek-fn field-type))
               (poke-fn (%cstruct-poke-fn field-type)))
          (when (> end max-end) (setf max-end end))
          ;; Getter
          (push `(defun ,accessor (ptr)
                   (,peek-fn ptr ,field-offset))
                forms)
          ;; Setter via defun + defsetf
          (let ((setter-name (intern (format nil "%SET-~A-~A" name field-name))))
            (push `(defun ,setter-name (ptr val)
                     (,poke-fn ptr val ,field-offset)
                     val)
                  forms)
            (push `(defsetf ,accessor ,setter-name)
                  forms)))))
    `(progn
       (defvar ,size-var ,max-end)
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
