;; repro-sse8 — same as repro-sse7 but flips the C-level NLX trace
;; (clamiga::%nlx-trace) ON only around the make-instance that spuriously
;; unwinds on the hunchentoot worker, so we capture EXACTLY which
;; return-from / throw / go fires and where it lands — the non-condition
;; non-local exit that is invisible to cl_error/handler-case instrumentation.
(setq *load-verbose* nil)
(require "asdf")
(load "trunk/load-libs-ql.lisp")
(ensure-ql-lib :babel) (asdf:load-system :babel)
(dolist (s '(:chipi :ironclad :cl-base64 :marshal :snooze :fiveam :cl-mock :split-sequence))
  (ensure-ql-lib s))
(asdf:load-system :chipi-api/tests)
(load "trunk/hunchentoot-clamiga.lisp")

(in-package :cl-user)
(defun elog (fmt &rest args)
  (format *error-output* "~&[~A] ~A~%"
          (or (ignore-errors (mp:thread-name (mp:current-thread))) "?")
          (apply #'format nil fmt args))
  (finish-output *error-output*))

(in-package :sento.actor-context)
(defun %message-box-for-dispatcher-id (context dispatcher-id queue-size mbox-type)
  (cl-user::elog "  %mbox: enter id=~A" dispatcher-id)
  (case dispatcher-id
    (:pinned (make-instance 'mesgb:message-box/bt :max-queue-size queue-size))
    (otherwise
     (let* ((asys (system context))
            (sys-config (asys:config asys))
            (disp-config (%get-dispatcher-config sys-config dispatcher-id))
            (dispatcher (%get-shared-dispatcher asys dispatcher-id)))
       (unless dispatcher (error "No such dispatcher '~a'" dispatcher-id))
       (let ((eff (if mbox-type mbox-type (getf disp-config :mbox-type 'mesgb:message-box/dp)))
             (ok nil)
             (cam-debug-sym (find-symbol "*CLOS-CAM-DEBUG*" :cl)))
         (cl-user::elog "  %mbox: make-instance ~A... (CAM DEBUG ON)" eff)
         (when cam-debug-sym (setf (symbol-value cam-debug-sym) t))
         (unwind-protect
              (let ((mb (make-instance eff :dispatcher dispatcher :max-queue-size queue-size)))
                (setf ok t)
                (when cam-debug-sym (setf (symbol-value cam-debug-sym) nil))
                (cl-user::elog "  %mbox: make-instance DONE")
                mb)
           (when cam-debug-sym (setf (symbol-value cam-debug-sym) nil))
           (unless ok
             (cl-user::elog "  %mbox: make-instance UNWOUND (non-local exit)"))))))))

(in-package :chipi-api.sse-manager)
(defun add-client (stream)
  (cl-user::elog "add-client: ensure-sse-manager...")
  (let ((mgr (handler-case (ensure-sse-manager)
               (serious-condition (c)
                 (cl-user::elog "add-client: ensure-sse-manager ERROR ~A: ~A" (type-of c) c)
                 (error c)))))
    (cl-user::elog "add-client: actor created = ~A" mgr)
    (let ((r (handler-case (? mgr `(:add-client :stream ,stream))
               (serious-condition (c)
                 (cl-user::elog "add-client: ? ask ERROR ~A: ~A" (type-of c) c)
                 (error c)))))
      (cl-user::elog "add-client: ? ask RETURNED ~A" r)
      r)))

(in-package :cl-user)
(defparameter *lines-read* 0)

(defun watchdog (secs)
  (mp:make-thread
   (lambda () (sleep secs)
     (elog "WATCHDOG ~Ds — lines-read=~A" secs *lines-read*)
     (quit 99))
   :name "watchdog"))

(defun run ()
  (hab:defconfig "chipi")
  (api-env:init :apikey-store apikey-store:*memory-backend*
                :apikey-lifetime (ltd:duration :day 1))
  (api:start)
  (setf eventsc:*heartbeat-sleep-time-s* 0.1
        eventsc:*max-heartbeats* 2)
  (sleep 1)
  (let* ((apikey (apikey-store:create-apikey :access-rights '(:read :update)))
         (item (hab:defitem 'test-sensor "Test Sensor" 'float :initial-value 20.0)))
    (declare (ignore item))
    (watchdog 8)
    (elog ">> opening SSE stream")
    (multiple-value-bind (stream status headers)
        (drakma:http-request "http://localhost:8765/events/items"
                             :method :get :accept "application/json"
                             :additional-headers `(("X-Api-Key" . ,apikey))
                             :want-stream t :keep-alive t :close nil)
      (declare (ignore headers))
      (elog "STATUS=~A" status)
      (let ((sse-data nil) (done nil))
        (loop while (not done)
              for line = (read-line stream nil nil)
              do (cond
                   ((null line) (elog "EOF after ~D lines" (length sse-data)) (setf done t))
                   ((> (length line) 0)
                    (incf *lines-read*) (elog "LINE ~D: ~A" *lines-read* line)
                    (push line sse-data)
                    (when (>= (length sse-data) 4) (setf done t) (close stream)))))))
    (ignore-errors (api:stop)))
  (quit 0))

(run)
