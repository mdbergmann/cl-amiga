;; Load and test tuition (https://github.com/atgreen/cl-tuition), a
;; TEA-style terminal UI library.  Exercises the EXT tty primitives
;; (ext:tty-raw-mode / ext:tty-size) through tuition's cl-amiga backend.
;; Works on both host and Amiga.
;;
;; Usage:
;;   ./build/host/clamiga --heap 64M --load trunk/load-and-test-tuition.lisp
;;
;; Expects a cl-tuition checkout in quicklisp's local-projects (where the
;; local-projects searcher resolves it), or set CL_TUITION_DIR to point at
;; a checkout elsewhere.

(require "asdf")

;; Make the quicklisp-managed dependencies resolvable through ASDF.
(load "trunk/load-libs-ql.lisp")

(let ((dir (ext:getenv "CL_TUITION_DIR")))
  (when dir
    (pushnew (pathname (concatenate 'string dir "/"))
             asdf:*central-registry* :test #'equal)))

(format t "~%--- Loading tuition ---~%")
(ensure-ql-lib :bordeaux-threads)
(ensure-ql-lib :trivial-timeout)   ; dep of trivial-channels; ASDF can't fetch it
(ensure-ql-lib :trivial-channels)
(ensure-ql-lib :version-string)
(ensure-ql-lib :alexandria)
(ensure-ql-lib :serapeum)
(ensure-ql-lib :cl-base64)
(asdf:load-system :tuition)

(format t "~%--- Running tuition tests ---~%")
(ensure-ql-lib :fiveam)
(asdf:test-system :tuition)
