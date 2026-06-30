;;;; introspect-environment.asd — introspect-environment shim for CL-Amiga.
;;;;
;;;; Upstream introspect-environment only implements working TYPEXPAND /
;;;; TYPEXPAND-1 for implementations that expose a deftype expander; its
;;;; "default" backend (used for unsupported Lisps) leaves every type
;;;; specifier unexpanded.  CL-Amiga exposes its deftype expander table to
;;;; Lisp (CLAMIGA::%TYPE-EXPANDER), so this fork supplies a real TYPEXPAND
;;;; built on top of it — needed by serapeum's EXPLODE-TYPE and any other
;;;; caller that resolves user `deftype' aliases.  See default.lisp for the
;;;; per-symbol semantics; package.lisp / expander.lisp / doc.lisp are
;;;; carried over verbatim from upstream.
;;;;
;;;; ASDF finds this system via lib/local-projects/ being on
;;;; *central-registry* (see lib/quicklisp-compat.lisp).

(asdf:defsystem #:introspect-environment
  :description "Portable but nonstandard introspection of CL environments (CL-Amiga shim)."
  :author "Bike <aeshtaer@gmail.com> (original), CL-Amiga port"
  :license "WTFPL"
  :version "0.1"
  :serial t
  :components ((:file "package")
               (:file "expander")
               (:file "default")
               (:file "doc")))
