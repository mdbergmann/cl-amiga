;;;; swank.asd --- Minimal SWANK shim system for cl-amiga.
;;;
;;; Several quicklisp libraries (clack, mgl-pax, dref/full, ...) name the
;;; `swank' ASDF system in their :depends-on list purely to reach a handful
;;; of SWANK symbols at read/compile time — most prominently clack, which
;;; references SWANK:CREATE-SERVER / SWANK:STOP-SERVER to optionally stand up
;;; a remote-debug server, but ONLY when its :swank-port argument is non-NIL
;;; (the default is NIL, so the calls never fire).
;;;
;;; cl-amiga has its own native SLY/SLYNK transport and does not ship the
;;; full SWANK backend, so the real swank system is unavailable here.  This
;;; shim provides just enough of the SWANK package for those libraries to
;;; compile and load.  The stubs SIGNAL a clear error if actually invoked,
;;; so the missing functionality surfaces honestly rather than silently.

#-cl-amiga
(error "The cl-amiga swank shim is only intended for cl-amiga.")

(defsystem swank
  :description "Minimal SWANK shim so clack/mgl-pax & co. load on cl-amiga."
  :author "cl-amiga"
  :license "Public Domain"
  :components ((:file "swank")))
