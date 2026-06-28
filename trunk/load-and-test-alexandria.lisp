;; Load and test Alexandria.
;; Works on both host and Amiga.
;;
;; Usage:
;;   ./build/host/clamiga --heap 24M --load trunk/load-and-test-alexandria.lisp

(require "asdf")

;; Make the quicklisp-managed dependencies resolvable through ASDF.
;; All quicklisp handling lives in this helper; this script only uses
;; asdf:load-system.
(load "trunk/load-libs-ql.lisp")

(format t "~%--- Loading Alexandria ---~%")
(ensure-ql-lib :alexandria)      ; fetch from dist only if not in local repo
(asdf:load-system :alexandria)

(format t "~%--- Running Alexandria tests ---~%")
(asdf:test-system :alexandria)
