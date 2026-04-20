;;;; trivial-cltl2.asd — trivial-cltl2 shim for CL-Amiga
;;;;
;;;; Upstream trivial-cltl2 delegates every CLtL2 name to a platform
;;;; backend package (sb-cltl2 on SBCL, ccl on Clozure, etc.) and has no
;;;; branch for CL-Amiga.  This fork supplies the handful of CLtL2 names
;;;; that downstream libraries (trivia, serapeum) actually call, as
;;;; minimal but well-behaved implementations — see cltl2.lisp for the
;;;; per-symbol semantics and limitations.
;;;;
;;;; ASDF finds this system via lib/local-projects/ being on
;;;; *central-registry* (see lib/quicklisp-compat.lisp).

(asdf:defsystem :trivial-cltl2
  :version "0.1.1"
  :author "Tomohiro Matsuyama (original), CL-Amiga port"
  :description "Compatibility package exporting CLtL2 functionality (CL-Amiga shim)."
  :license "LLGPL"
  :components ((:file "cltl2")))
