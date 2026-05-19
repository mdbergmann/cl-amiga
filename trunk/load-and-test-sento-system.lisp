;; Load and test Sento via asdf:test-system (matches the documented
;; baseline of 511/513 passing — the 2 fails are timing-flaky
;; ACTOR-OF--LIMIT-QUEUE-SIZE cases).
;;
;; Sibling script `load-and-test-sento.lisp` runs suites one-by-one
;; with a custom runner; this one delegates to the system definition
;; and matches the convention of the other `load-and-test-*` scripts.
;;
;; Usage (host, cold cache):
;;   rm -rf ~/.cache/common-lisp/cl-amiga-*
;;   ./build/host/clamiga --heap 192M --load trunk/load-and-test-sento-system.lisp
;;
;; Usage (host, warm cache):
;;   ./build/host/clamiga --heap 128M --load trunk/load-and-test-sento-system.lisp
;;
;; On Amiga: --heap >= 96M and a large stack (e.g. `stack 800000`).

(setq *load-verbose* nil)
(require "asdf")

(defvar *ql-setup*
  #+amigaos #P"S:quicklisp/setup.lisp"
  #-amigaos (merge-pathnames "quicklisp/setup.lisp" (user-homedir-pathname)))

(unless (probe-file *ql-setup*)
  (load "lib/quicklisp-install.lisp")
  (funcall (find-symbol "INSTALL" "CL-AMIGA-QL")))

(unless (member :quicklisp *features*)
  (load *ql-setup*))

(unless (member :quicklisp-compat *features*)
  (load "lib/quicklisp-compat.lisp"))

;; No (ql:quickload ...) here: that would compile sento + its deps and
;; populate the FASL cache before the test, defeating the "cold cache"
;; mode advertised in the usage comment.  asdf:test-system pulls every
;; dep in via the system definitions, so the cache state at start is
;; exactly what the caller arranged (cold after `rm -rf`, warm
;; otherwise).
(format t "~%--- Running (asdf:test-system :sento) ---~%")
(asdf:test-system :sento)
