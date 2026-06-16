;; Isolate make-sse-manager on the MAIN thread.
(setq *load-verbose* nil)
(require "asdf")
(load "trunk/load-libs-ql.lisp")
(ensure-ql-lib :babel) (asdf:load-system :babel)
(dolist (s '(:chipi :ironclad :cl-base64 :marshal :snooze :fiveam :cl-mock :split-sequence))
  (ensure-ql-lib s))
(asdf:load-system :chipi-api/tests)
(load "trunk/hunchentoot-clamiga.lisp")

(in-package :cl-user)

(defun run ()
  ;; minimal env: need isys (actor system) + config
  (hab:defconfig "chipi")
  (format t "~&[T] calling make-sse-manager on MAIN thread...~%") (finish-output)
  (handler-case
      (let ((mgr (sse-manager::make-sse-manager)))
        (format t "~&[T] make-sse-manager OK: ~A~%" mgr) (finish-output)
        (sleep 1)
        (format t "~&[T] running-p=~A~%"
                (ignore-errors (act-cell:running-p mgr))) (finish-output))
    (serious-condition (c)
      (format t "~&[T] make-sse-manager ERROR type=~A: ~A~%" (type-of c) c)
      (finish-output)
      (format t "~&[T] backtrace:~%") (finish-output)
      (ignore-errors (ext:backtrace))))
  (format t "~&[T] done~%") (finish-output)
  (hab:shutdown)
  (quit 0))

(run)
