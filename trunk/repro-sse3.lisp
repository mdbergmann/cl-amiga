;; Isolate make-sse-manager called from a WORKER thread (no hunchentoot).
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
  (hab:defconfig "chipi")
  (format t "~&[MAIN] spawning worker thread to call make-sse-manager...~%")
  (finish-output)
  (let ((done nil))
    (mp:make-thread
     (lambda ()
       (unwind-protect
            (handler-case
                (progn
                  (format t "~&[WRK] in worker, calling make-sse-manager~%")
                  (finish-output)
                  (let ((mgr (sse-manager::make-sse-manager)))
                    (format t "~&[WRK] make-sse-manager OK: running=~A~%"
                            (ignore-errors (act-cell:running-p mgr)))
                    (finish-output)))
              (condition (c)
                (format t "~&[WRK] CONDITION type=~A: ~A~%" (type-of c) c)
                (finish-output)))
         (format t "~&[WRK] unwind-protect cleanup reached~%") (finish-output)
         (setf done t)))
     :name "worker")
    ;; wait for worker
    (loop repeat 100 until done do (sleep 0.1))
    (format t "~&[MAIN] worker done flag=~A~%" done) (finish-output))
  (format t "~&[MAIN] done~%") (finish-output)
  (quit 0))

(run)
