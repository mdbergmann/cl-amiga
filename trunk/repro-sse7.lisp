;; repro-sse7 — separate actor-CREATION from the ASK in add-client, logging each
;; sub-step to STDERR (unbuffered, never the socket) with the current thread name,
;; so we know exactly how far the SSE handler gets before it stalls.  On EOF /
;; watchdog, dump every thread's blocked-on primitive AND interrupt the SSE
;; worker to print its own Lisp backtrace.
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

;; Instrument sento actor-creation internals (to stderr).
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
             (ok nil))
         (cl-user::elog "  %mbox: make-instance ~A..." eff)
         (unwind-protect
              (let ((mb (make-instance eff :dispatcher dispatcher :max-queue-size queue-size)))
                (setf ok t)
                (cl-user::elog "  %mbox: make-instance DONE")
                mb)
           (unless ok
             (cl-user::elog "  %mbox: make-instance UNWOUND (non-local exit)"))))))))
(defun %create-actor (context create-fun dispatcher-id queue-size mbox-type)
  (cl-user::elog "  %create-actor: create-fun...")
  (let ((actor (funcall create-fun)))
    (cl-user::elog "  %create-actor: actor built; verify+mbox...")
    (when actor
      (%verify-actor context actor)
      (let ((mbox (%message-box-for-dispatcher-id context dispatcher-id queue-size mbox-type))
            (ctx (make-actor-context (system context)
                                     (miscutils:mkstr (id context) "/" (act-cell:name actor)))))
        (cl-user::elog "  %create-actor: finalize-initialization (pre-start/init)...")
        (act::finalize-initialization actor mbox ctx)
        (cl-user::elog "  %create-actor: finalize DONE")))
    actor))

;; Separate ensure-sse-manager (actor creation) from the ? ask.
(in-package :chipi-api.sse-manager)
(defun add-client (stream)
  (cl-user::elog "add-client: ensure-sse-manager (actor creation)...")
  (let ((mgr (handler-case (ensure-sse-manager)
               (serious-condition (c)
                 (cl-user::elog "add-client: ensure-sse-manager ERROR ~A: ~A" (type-of c) c)
                 (error c)))))
    (cl-user::elog "add-client: actor created = ~A; now ? ask :add-client..." mgr)
    (let ((r (handler-case (? mgr `(:add-client :stream ,stream))
               (serious-condition (c)
                 (cl-user::elog "add-client: ? ask ERROR ~A: ~A" (type-of c) c)
                 (error c)))))
      (cl-user::elog "add-client: ? ask RETURNED ~A" r)
      r)))

(in-package :cl-user)
(defparameter *lines-read* 0)
(defparameter *worker-thread* nil)

;; Capture the SSE worker thread the first time it enters the handler.
(in-package :chipi-api.events-controller)
(let ((orig (fdefinition 'handle-sse-connection)))
  (defun handle-sse-connection (stream)
    (setf cl-user::*worker-thread* (mp:current-thread))
    (cl-user::elog "handle-sse-connection ENTER on this thread")
    (handler-case
        (progn (funcall orig stream)
               (cl-user::elog "handle-sse-connection RETURNED NORMALLY"))
      (serious-condition (c)
        (cl-user::elog "handle-sse-connection UNWOUND via ~A: ~A" (type-of c) c)
        (error c)))))

(in-package :cl-user)
(defun dump-state (tag)
  (elog "==== ~A — lines-read=~A ====" tag *lines-read*)
  (mp:dump-thread-waits)
  (sleep 1)
  (finish-output *error-output*))

(defun watchdog (secs)
  (mp:make-thread
   (lambda () (sleep secs) (dump-state (format nil "WATCHDOG ~Ds" secs)) (quit 99))
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
                   ((null line) (elog "EOF after ~D lines" (length sse-data))
                    (dump-state "EOF-on-client") (setf done t))
                   ((> (length line) 0)
                    (incf *lines-read*) (elog "LINE ~D: ~A" *lines-read* line)
                    (push line sse-data)
                    (when (>= (length sse-data) 4) (setf done t) (close stream)))))))
    (ignore-errors (api:stop)))
  (quit 0))

(run)
