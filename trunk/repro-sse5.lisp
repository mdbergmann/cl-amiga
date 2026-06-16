;; repro-sse5 — pin the SSE add-client/actor-of hang on the hunchentoot worker.
;; Uses the REAL chipi handle-sse-connection (add-client -> ? ask -> actor-of),
;; instruments sento %create-actor step-by-step, and on hang dumps every
;; thread's blocked-on synchronization primitive via (mp:dump-thread-waits) —
;; which tells a LOST WAKEUP (a dispatcher worker still in CONDWAIT on its queue
;; condvar after the producer notified) apart from a LOCK-ORDERING deadlock
;; (a thread blocked in LOCK-ACQUIRE on a held lock).
(setq *load-verbose* nil)
(require "asdf")
(load "trunk/load-libs-ql.lisp")
(ensure-ql-lib :babel) (asdf:load-system :babel)
(dolist (s '(:chipi :ironclad :cl-base64 :marshal :snooze :fiveam :cl-mock :split-sequence))
  (ensure-ql-lib s))
(asdf:load-system :chipi-api/tests)
(load "trunk/hunchentoot-clamiga.lisp")

;; ---- Instrument sento actor creation so we see exactly where actor-of parks.
(in-package :sento.actor-context)
(defun %message-box-for-dispatcher-id (context dispatcher-id queue-size mbox-type)
  (format t "~&[MB] enter id=~A~%" dispatcher-id) (finish-output)
  (case dispatcher-id
    (:pinned (make-instance 'mesgb:message-box/bt :max-queue-size queue-size))
    (otherwise
     (let* ((asys (progn (format t "~&[MB] system...~%") (finish-output) (system context)))
            (sys-config (progn (format t "~&[MB] config...~%") (finish-output) (asys:config asys)))
            (disp-config (progn (format t "~&[MB] disp-config...~%") (finish-output)
                                (%get-dispatcher-config sys-config dispatcher-id)))
            (dispatcher (progn (format t "~&[MB] get-shared-dispatcher...~%") (finish-output)
                               (%get-shared-dispatcher asys dispatcher-id))))
       (unless dispatcher (error "No such dispatcher '~a'" dispatcher-id))
       (let ((eff-mbox-type (if mbox-type mbox-type
                                (getf disp-config :mbox-type 'mesgb:message-box/dp))))
         (format t "~&[MB] make-instance ~A...~%" eff-mbox-type) (finish-output)
         (let ((mb (make-instance eff-mbox-type :dispatcher dispatcher
                                  :max-queue-size queue-size)))
           (format t "~&[MB] make-instance returned~%") (finish-output)
           mb))))))
(defun %create-actor (context create-fun dispatcher-id queue-size mbox-type)
  (format t "~&[AC] create-fun...~%") (finish-output)
  (let ((actor (funcall create-fun)))
    (format t "~&[AC] create-fun returned~%") (finish-output)
    (when actor
      (%verify-actor context actor)
      (format t "~&[AC] verify ok; message-box...~%") (finish-output)
      (let ((mbox (%message-box-for-dispatcher-id context dispatcher-id queue-size mbox-type)))
        (format t "~&[AC] mbox ok; actor-context...~%") (finish-output)
        (let ((ctx (make-actor-context (system context)
                                       (miscutils:mkstr (id context) "/" (act-cell:name actor)))))
          (format t "~&[AC] finalize-initialization (runs init pre-start)...~%") (finish-output)
          (act::finalize-initialization actor mbox ctx)
          (format t "~&[AC] finalize DONE~%") (finish-output))))
    actor))

;; ---- Instrument add-client / ensure-sse-manager.
(in-package :chipi-api.sse-manager)
(defun add-client (stream)
  (format t "~&[MGR] add-client: ensure+ask...~%") (finish-output)
  (let ((r (? (ensure-sse-manager) `(:add-client :stream ,stream))))
    (format t "~&[MGR] add-client returned ~A~%" r) (finish-output)
    r))

(in-package :cl-user)
(defparameter *lines-read* 0)

(defparameter *stay-hung* nil)

(defun dump-and-quit (tag)
  (format t "~&!!!! ~A — lines-read=~A~%" tag *lines-read*) (finish-output)
  (mp:dump-thread-waits)
  (dolist (th (mp:all-threads))
    (format t "   alive thread: ~A~%" (ignore-errors (mp:thread-name th))))
  (finish-output)
  (if *stay-hung*
      (progn (format t "~&[STAY-HUNG] PID parked for lldb; not quitting~%") (finish-output)
             (loop (sleep 5)))
      (quit 99)))

(defun watchdog (secs)
  (mp:make-thread
   (lambda () (sleep secs) (dump-and-quit (format nil "WATCHDOG ~Ds" secs)))
   :name "watchdog"))

(defun run ()
  (setf *stay-hung* t)
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
    (format t "~&>> opening SSE stream~%") (finish-output)
    (multiple-value-bind (stream status headers)
        (drakma:http-request "http://localhost:8765/events/items"
                             :method :get :accept "application/json"
                             :additional-headers `(("X-Api-Key" . ,apikey))
                             :want-stream t :keep-alive t :close nil)
      (declare (ignore headers))
      (format t "~&STATUS=~A~%" status) (finish-output)
      (let ((sse-data nil) (done nil))
        (loop while (not done)
              for line = (read-line stream nil nil)
              do (cond
                   ((null line)
                    (format t "~&EOF after ~D lines~%" (length sse-data)) (finish-output)
                    (dump-and-quit "EOF-on-client"))
                   ((> (length line) 0)
                    (incf *lines-read*)
                    (format t "~&LINE ~D: ~A~%" *lines-read* line) (finish-output)
                    (push line sse-data)
                    (when (>= (length sse-data) 4)
                      (setf done t) (close stream)))))
        (format t "~&COMPLETED lines=~D~%" (length sse-data)) (finish-output)))
    (api:stop))
  (quit 0))

(run)
