;;;; closer-mop.asd — closer-mop shim for CL-Amiga
;;;;
;;;; The canonical closer-mop is a compatibility layer for the AMOP
;;;; subset shared by major implementations.  CL-Amiga already exports
;;;; that subset at AMOP names from COMMON-LISP, so this system is a
;;;; thin wrapper that builds the CLOSER-MOP package and re-exports
;;;; the names — downstream libraries that `(:use :closer-mop)` or
;;;; `(:import-from :closer-mop ...)` then resolve against our
;;;; existing bindings.
;;;;
;;;; This file sits in lib/local-projects/closer-mop/ so ASDF finds it
;;;; via the search path wired in lib/quicklisp-compat.lisp (pushed onto
;;;; ASDF:*CENTRAL-REGISTRY* before any (ql:quickload :closer-mop)
;;;; attempts a download).  Users wanting to track upstream closer-mop
;;;; can swap in the real sources here — the #+clamiga branches in
;;;; closer-mop-packages.lisp take over automatically.

(asdf:defsystem #:closer-mop
  :name "Closer to MOP (CL-Amiga shim)"
  :description "Portable MOP shim for CL-Amiga — maps closer-mop symbols onto our built-in MOP exports."
  :author "CL-Amiga"
  :version "1.0.0"
  :licence "MIT-style license"
  :serial t
  :components
  ((:file "closer-mop-packages")
   (:file "closer-mop-shared")))
