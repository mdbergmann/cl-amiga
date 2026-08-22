;;; amiga/ffi.lisp — AmigaOS-specific FFI utilities
;;;
;;; Loaded via (require "amiga/ffi").
;;; Builds on the generic FFI package and the AMIGA package builtins.

;; compile-time too: COMPILE-FILE (the host builds the lib/amiga FASLs) must see
;; these packages at read time, not only LOAD.
(eval-when (:compile-toplevel :load-toplevel :execute)
  (require "ffi"))

(defpackage "AMIGA.FFI"
  (:use "CL" "FFI")
  (:export "WITH-LIBRARY" "WITH-TAG-LIST" "MAKE-TAG-LIST" "DEFCFUN"
           "LIBRARY-VERSION" "OPEN-LIBRARY-OR-DIE"))

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

(defun open-library-or-die (name &optional (version 0))
  "OpenLibrary NAME (at least VERSION) and return its base; signal a
descriptive error if the OS refuses.  Used by the binding modules at
load time, so a missing library fails at REQUIRE with a clear message
instead of at the first call."
  (let ((base (amiga:open-library name version)))
    (when (or (null base) (ffi:null-pointer-p base))
      (error "Cannot open ~A~@[ (version ~D or newer)~] -- is it installed on this system?"
             name (and (plusp version) version)))
    base))

(defun library-version (base)
  "The lib_Version field (struct Library, offset 20) of an open library
base -- the OS revision that the running system actually provides.
Bindings generated from the OS 3.2 NDK guard functions newer than V39
with this at load time."
  (ffi:peek-u16 base 20))

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
;;;   (:a1 rastport :d0 x :d1 y) :result :void)
;;;
;;; Expands to a call through AMIGA::%FFI-CALL — a special form the
;;; compiler recognizes and emits as the dedicated OP_AMIGA_CALL bytecode
;;; op.  No wrapper-function frame, no list allocation, no plist walk:
;;; the value args go straight onto the VM stack and the trampoline
;;; pulls them into m68k registers.
;;;
;;; (defun move-to (rastport x y)
;;;   (amiga:%ffi-call *gfx-base* -240 #x10000019 rastport x y))
;;;
;;; Regspec layout (positive 31-bit fixnum):
;;;   bits  0..27: 7 nibbles, one register index per value arg
;;;   bits 28..29: result kind (CL_AMIGA_RES_* in builtins.h):
;;;                0 = d0 as unsigned integer, 1 = void (NIL),
;;;                2 = pointer (foreign pointer, NULL -> NIL),
;;;                3 = d0 as signed integer
;;;
;;; :RESULT selects how the d0 value comes back:
;;;   :unsigned (default)  ULONG/UWORD/UBYTE/BPTR/Tag... as an integer
;;;   :void                discard d0, return NIL (most rendering calls)
;;;   :pointer             struct X * / APTR / STRPTR -> foreign pointer,
;;;                        NIL for NULL
;;;   :signed              LONG -> signed integer (-1 stays -1)
;;;   :bool                BOOL -> T/NIL (tests d0.w, the 16-bit ABI width)
;;;   :i16 :u16 :i8 :u8    WORD/UWORD/BYTE/UBYTE results — only the low
;;;                        bits of d0 are defined for these C types
;;; The first four are boxed in C; the rest post-process an :unsigned
;;; result in Lisp.  :VOID T is the legacy spelling of :RESULT :VOID.
;;;
;;; Functions with more than seven register arguments (a dozen in the
;;; whole OS: BltBitMap, ClipBlit, ModifyProp, CreateUpfrontLayer...)
;;; fall back to AMIGA:CALL-LIBRARY's plist path, which has no register
;;; cap.  :A5 can never carry an argument — it is the dispatcher's
;;; scratch register (ffi_dispatch_m68k.s).
;;;
;;; If the AMIGA::%FFI-CALL special form ever runs interpreted (no
;;; compiler hook), CALL-LIBRARY-FAST is the matching runtime fallback —
;;; both honor the same regspec encoding.

(defun %defcfun-reg-index (kw)
  "Return the register index (0..12) for an :Dn or :An keyword, or signal."
  (case kw
    (:d0 0)  (:d1 1)  (:d2 2)  (:d3 3)
    (:d4 4)  (:d5 5)  (:d6 6)  (:d7 7)
    (:a0 8)  (:a1 9)  (:a2 10) (:a3 11)
    (:a4 12)
    (:a5 (error "DEFCFUN: register :A5 is reserved by the call dispatcher and cannot carry an argument (use d0-d7/a0-a4)"))
    (otherwise (error "DEFCFUN: unknown register keyword: ~S" kw))))

(defconstant +defcfun-void-bit+ #x10000000)  ; bit 28 -- kind 1 = void

(defconstant +result-kind-unsigned+ 0)
(defconstant +result-kind-void+     1)
(defconstant +result-kind-pointer+  2)
(defconstant +result-kind-signed+   3)

(defun %defcfun-result-kind (result)
  "Map a :RESULT keyword to (values c-kind lisp-post-processor-or-nil)."
  (ecase result
    (:unsigned (values +result-kind-unsigned+ nil))
    (:void     (values +result-kind-void+ nil))
    (:pointer  (values +result-kind-pointer+ nil))
    (:signed   (values +result-kind-signed+ nil))
    (:bool     (values +result-kind-unsigned+ '%result-bool))
    (:u16      (values +result-kind-unsigned+ '%result-u16))
    (:i16      (values +result-kind-unsigned+ '%result-i16))
    (:u8       (values +result-kind-unsigned+ '%result-u8))
    (:i8       (values +result-kind-unsigned+ '%result-i8))))

;; Post-processors for the sub-32-bit result kinds.  Only the low word /
;; byte of d0 is defined for a BOOL/WORD/BYTE C return type, so mask
;; before interpreting.
(defun %result-bool (v) (if (logtest v #xFFFF) t nil))
(defun %result-u16 (v) (logand v #xFFFF))
(defun %result-i16 (v) (let ((x (logand v #xFFFF))) (if (logbitp 15 x) (- x #x10000) x)))
(defun %result-u8 (v) (logand v #xFF))
(defun %result-i8 (v) (let ((x (logand v #xFF))) (if (logbitp 7 x) (- x #x100) x)))

(defmacro defcfun (name library-base offset (&rest reg-spec)
                   &key void (result :unsigned) doc)
  "Define a Lisp function NAME that calls the AmigaOS library function at
LVO OFFSET of the library whose base is held in the special variable
LIBRARY-BASE.  REG-SPEC is a plist of (:register param-name ...) pairs.
:RESULT (see the table above) chooses how d0 is returned; :VOID T is the
legacy spelling of :RESULT :VOID.  DOC becomes the function's docstring.

In addition to the named function, registers a compiler macro on NAME
so direct call sites -- `(move-to rp x y)` etc. -- compile down to a
bare AMIGA:%FFI-CALL (= OP_AMIGA_CALL) in the caller, skipping the
wrapper's LINK frame and the cl_vm_apply dispatch trip.  Indirect
callers (funcall / sharp-quote) still hit the real wrapper function."
  (let* ((pairs (loop for (reg param) on reg-spec by #'cddr
                      collect (list reg param)))
         (params (mapcar #'second pairs))
         (n-params (length params))
         (result (if void :void result))
         (docs (if doc (list doc) nil)))
    (multiple-value-bind (kind post) (%defcfun-result-kind result)
      (if (> n-params 7)
          ;; Plist path: no register cap, no compiler macro.
          (let ((plist (loop for (reg param) in pairs
                             collect reg collect param)))
            (dolist (pair pairs) (%defcfun-reg-index (first pair)))  ; validate
            `(progn
               (defun ,name ,params
                 ,@docs
                 ,(let ((call `(amiga:call-library ,library-base ,offset
                                                   (list ,@plist) ,kind)))
                    (if post `(,post ,call) call)))
               ',name))
          (let ((regspec 0) (shift 0))
            (dolist (pair pairs)
              (setf regspec (logior regspec
                                    (ash (%defcfun-reg-index (first pair)) shift)))
              (incf shift 4))
            (setf regspec (logior regspec (ash kind 28)))
            `(progn
               (defun ,name ,params
                 ,@docs
                 ,(let ((call `(amiga:%ffi-call ,library-base ,offset ,regspec ,@params)))
                    (if post `(,post ,call) call)))
               ;; Decline expansion on argument-count mismatch so the caller
               ;; gets the wrapper's normal arity error instead of a confusing
               ;; mid-compile diagnostic.  CLHS 3.2.2.1.3: returning the &whole
               ;; form means "no expansion".
               (define-compiler-macro ,name (&whole form &rest args)
                 (if (= (length args) ,n-params)
                     ,(let ((call `(list* 'amiga:%ffi-call ',library-base ,offset ,regspec args)))
                        (if post `(list ',post ,call) call))
                     form))
               ',name))))))

;;; ================================================================
;;; Provide module
;;; ================================================================

(provide "amiga/ffi")
