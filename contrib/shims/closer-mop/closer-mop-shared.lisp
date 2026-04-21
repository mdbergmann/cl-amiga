;;;; closer-mop-shared.lisp — shared implementation for the CL-Amiga
;;;; closer-mop shim.
;;;;
;;;; On CL-Amiga the canonical closer-mop portable subset is already
;;;; implemented in lib/clos.lisp and exported from the COMMON-LISP
;;;; package.  This file adds the helpers closer-mop ships in its
;;;; shared source file that are NOT part of our base CLOS but are
;;;; expected by serapeum, trivia, and related libraries.
;;;;
;;;; Everything here is guarded by #+clamiga so the file stays
;;;; harmless when it lands on a different Lisp via accidental loading.

#+clamiga
(in-package #:closer-mop)

#+clamiga
(defun required-args (lambda-list &optional (collector #'identity))
  "closer-mop: collect the required positional args of LAMBDA-LIST,
stopping at the first lambda-list keyword.  COLLECTOR maps each arg
before it is collected (identity by default)."
  (loop for arg in lambda-list
        until (member arg lambda-list-keywords)
        collect (funcall collector arg)))

#+clamiga
(defun ensure-finalized (class &optional (errorp t))
  "closer-mop: finalize CLASS if it isn't already.  When CLASS is not a
class metaobject, signal an error (or silently return it when ERRORP
is NIL)."
  (cond
    ((classp class)
     (unless (class-finalized-p class)
       (finalize-inheritance class))
     class)
    (errorp
     (error "~S is not a class." class))
    (t class)))

#+clamiga
(defun subclassp (class superclass)
  "closer-mop: true when CLASS is a (not-necessarily-proper) subclass
of SUPERCLASS.  Accepts class objects or class names — the latter are
resolved via FIND-CLASS."
  (flet ((resolve (c)
           (cond ((classp c) c)
                 ((symbolp c) (find-class c))
                 (t (error "Not a class designator: ~S" c)))))
    (let ((c (resolve class))
          (s (resolve superclass)))
      (if (member s (class-precedence-list c) :test #'eq) t nil))))

#+clamiga
(defun fix-slot-initargs (initargs)
  "closer-mop: on CMUCL/SCL the multiple-occurrence case of standard
slot keys needs canonicalisation.  Every other Lisp — including us —
returns INITARGS unchanged."
  initargs)

#+clamiga
(define-symbol-macro warn-on-defmethod-without-generic-function nil)
