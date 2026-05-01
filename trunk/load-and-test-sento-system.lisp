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

#+amigaos (load "S:quicklisp/setup.lisp")
#-amigaos (load (merge-pathnames "quicklisp/setup.lisp" (user-homedir-pathname)))

(load "lib/quicklisp-compat.lisp")

(format t "~%--- Quickload :sento and test dependencies ---~%")
(ql:quickload :sento)
(ql:quickload :fiveam)
(ql:quickload :serapeum)
(ql:quickload :lparallel)
(ql:quickload :cl-mock)

(format t "~%--- Running (asdf:test-system :sento) ---~%")
(asdf:test-system :sento)
