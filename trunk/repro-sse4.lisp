;; Does ac:actor-of (ensure-sse-manager) hang from a PLAIN worker thread while
;; the full chipi system is running — no hunchentoot/HTTP involved?
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
  (api-env:init :apikey-store apikey-store:*memory-backend*
                :apikey-lifetime (ltd:duration :day 1))
  (api:start)                      ; full system running (timers, dispatcher, etc.)
  (hab:defitem 'test-sensor "Test Sensor" 'float :initial-value 20.0)
  (sleep 1)
  (let ((done nil) (result nil))
    (format t "~&[MAIN] spawning PLAIN worker to call ensure-sse-manager...~%") (finish-output)
    (mp:make-thread
     (lambda ()
       (handler-case
           (progn
             (format t "~&[WRK] calling ensure-sse-manager...~%") (finish-output)
             (setf result (chipi-api.sse-manager::ensure-sse-manager))
             (format t "~&[WRK] ensure-sse-manager RETURNED: ~A~%" result) (finish-output))
         (serious-condition (c)
           (format t "~&[WRK] ERROR ~A: ~A~%" (type-of c) c) (finish-output)))
       (setf done t))
     :name "plain-worker")
    (loop repeat 150 until done do (sleep 0.1))   ; wait up to 15s
    (format t "~&[MAIN] done=~A result=~A~%" done result) (finish-output)
    (if done
        (format t "~&RESULT: PLAIN-WORKER-OK (actor-of works off a plain worker)~%")
        (format t "~&RESULT: PLAIN-WORKER-HANGS (actor-of blocks off a plain worker too)~%")))
  (finish-output)
  (quit 0))

(run)
