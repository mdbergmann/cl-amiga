;;; amiga/ffi.lisp — AmigaOS-specific FFI utilities
;;;
;;; Loaded via (require "amiga/ffi").
;;; Builds on the generic FFI package and the AMIGA package builtins.

(require "ffi")

(defpackage "AMIGA.FFI"
  (:use "CL" "FFI")
  (:export "WITH-LIBRARY" "WITH-TAG-LIST" "MAKE-TAG-LIST" "DEFCFUN"))

(in-package "AMIGA.FFI")

;;; ================================================================
;;; with-library — open/close AmigaOS library with cleanup
;;; ================================================================

(defmacro with-library ((var name &optional (version 0)) &body body)
  "Open an AmigaOS shared library, bind base to VAR, close on exit.
Signals an error if the library cannot be opened."
  `(let ((,var (amiga:open-library ,name ,version)))
     (when (or (null ,var) (ffi:null-pointer-p ,var))
       (error "Cannot open library ~A version ~D" ,name ,version))
     (unwind-protect
       (progn ,@body)
       (amiga:close-library ,var))))

;;; ================================================================
;;; Tag list support
;;; ================================================================

;;; AmigaOS tag lists are arrays of (uint32 tag, uint32 data) pairs,
;;; terminated by TAG_DONE (0).

(defconstant +tag-done+ 0)
(defconstant +tag-ignore+ 1)
(defconstant +tag-more+ 2)
(defconstant +tag-skip+ 3)

(defun make-tag-list (pairs)
  "Build a foreign TagItem array from a flat list of (tag value ...) pairs.
Returns a foreign pointer to the allocated array.  Caller must free it."
  (let* ((n (floor (length pairs) 2))
         (size (* (1+ n) 8))  ; n pairs + TAG_DONE, each 8 bytes
         (ptr (ffi:alloc-foreign size)))
    (do ((rest pairs (cddr rest))
         (i 0 (1+ i)))
        ((null rest))
      (let ((tag (car rest))
            (val (cadr rest))
            (offset (* i 8)))
        ;; tag at offset, data at offset+4
        (ffi:poke-u32 ptr tag offset)
        (ffi:poke-u32 ptr
                      (if (ffi:foreign-pointer-p val)
                          (ffi:foreign-pointer-address val)
                          val)
                      (+ offset 4))))
    ;; Terminate with TAG_DONE
    (ffi:poke-u32 ptr +tag-done+ (* n 8))
    (ffi:poke-u32 ptr 0 (+ (* n 8) 4))
    ptr))

(defmacro with-tag-list ((var &rest pairs) &body body)
  "Build a TagItem array from pairs, bind to VAR, free on exit.
Each pair is (tag-constant value).  String values are automatically
copied to foreign memory and freed."
  (let ((tag-pairs (gensym "PAIRS"))
        (strings (gensym "STRINGS")))
    `(let ((,strings nil)
           (,tag-pairs (list ,@(loop for (tag val) on pairs by #'cddr
                                     collect tag
                                     collect (if (stringp val)
                                                 ;; Defer to runtime
                                                 val
                                                 val)))))
       ;; Convert string values to foreign strings
       (do ((rest ,tag-pairs (cddr rest)))
           ((null rest))
         (when (stringp (cadr rest))
           (let ((fstr (ffi:foreign-string (cadr rest))))
             (push fstr ,strings)
             (setf (cadr rest) fstr))))
       (let ((,var (make-tag-list ,tag-pairs)))
         (unwind-protect
           (progn ,@body)
           (ffi:free-foreign ,var)
           (dolist (s ,strings)
             (ffi:free-foreign s)))))))

;;; ================================================================
;;; defcfun — define a named wrapper for an Amiga library function
;;; ================================================================

;;; (amiga.ffi:defcfun move-to *gfx-base* -240
;;;   (:a1 rastport :d0 x :d1 y))
;;;
;;; Expands to a call through CALL-LIBRARY-FAST with the register layout
;;; pre-encoded as a fixnum at macro-expansion time, eliminating the
;;; per-call list allocation:
;;;
;;; (defun move-to (rastport x y)
;;;   (amiga:call-library-fast *gfx-base* -240 #x019 rastport x y))
;;;
;;; The regspec fixnum packs one nibble per argument: :a1=9 (low nibble),
;;; :d0=0, :d1=1 -> #x109. Up to 7 register args (28 bits) are supported,
;;; matching the CALL-LIBRARY-FAST builtin's contract.

(defun %defcfun-reg-index (kw)
  "Return the register index (0..13) for an :Dn or :An keyword, or signal."
  (case kw
    (:d0 0)  (:d1 1)  (:d2 2)  (:d3 3)
    (:d4 4)  (:d5 5)  (:d6 6)  (:d7 7)
    (:a0 8)  (:a1 9)  (:a2 10) (:a3 11)
    (:a4 12) (:a5 13)
    (otherwise (error "DEFCFUN: unknown register keyword: ~S" kw))))

(defmacro defcfun (name library-base offset (&rest reg-spec))
  "Define a Lisp function that calls an AmigaOS library function.
REG-SPEC is a plist of (:register param-name ...) pairs."
  (let* ((pairs (loop for (reg param) on reg-spec by #'cddr
                      collect (list reg param)))
         (params (mapcar #'second pairs))
         (regspec 0)
         (shift 0))
    (dolist (pair pairs)
      (setf regspec (logior regspec
                            (ash (%defcfun-reg-index (first pair)) shift)))
      (incf shift 4))
    (when (> (length params) 7)
      (error "DEFCFUN: too many register args (max 7): ~S" reg-spec))
    `(defun ,name ,params
       (amiga:call-library-fast ,library-base ,offset ,regspec ,@params))))

;;; ================================================================
;;; Provide module
;;; ================================================================

(provide "amiga/ffi")
