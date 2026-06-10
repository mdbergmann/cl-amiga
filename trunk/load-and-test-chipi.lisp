;; Load and test chipi (home-automation lib, system "chipi") via ASDF.
;;
;; chipi resolves through ASDF's source registry the same way it does
;; under quicklisp — on the host its .asd is symlinked into ~/common-lisp,
;; so no central-registry push or path hard-coding is needed here.  Like
;; the sibling load-and-test-*.lisp scripts this file contains NO quicklisp
;; references: load-libs-ql.lisp sets up the dist + local-projects searchers
;; (and the :drakma-no-ssl feature cl-amiga needs), ENSURE-QL-LIB fetches
;; each system from the dist on a cold cache, and ASDF does the rest.
;;
;; Usage (host):
;;   ./build/host/clamiga --heap 256M --load trunk/load-and-test-chipi.lisp
;;
;; On Amiga: --heap >= 96M and a large stack (e.g. `stack 800000`).

(setq *load-verbose* nil)
(require "asdf")
(load "trunk/load-libs-ql.lisp")

;; babel is normally pulled in transitively via the cffi chain (cl+ssl);
;; since cl-amiga skips SSL it must be fetched + loaded explicitly.  It is
;; pure Lisp (alexandria + trivial-features) so it loads fine here.
;; chipi's persistence-influx.lisp uses babel:octets-to-string.
(ensure-ql-lib :babel)
(asdf:load-system :babel)

;; Make chipi and its test-only deps resolvable from the quicklisp dist
;; (download-only; ASDF performs the actual load/compile below).
(ensure-ql-lib :chipi)
(ensure-ql-lib :fiveam)
(ensure-ql-lib :cl-mock)

(format t "~%--- (asdf:test-system :chipi) ---~%")
(asdf:test-system :chipi)
