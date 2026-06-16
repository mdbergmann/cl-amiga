;; Focused repro of the chipi-api SSE-streaming heartbeat hang.
;; Loads the real chipi-api system, replicates the api-start-stop fixture,
;; opens a streaming SSE connection, sets an item value, and reads lines.
;; A watchdog thread dumps live threads and quits if it hangs.

(setq *load-verbose* nil)
(require "asdf")
(load "trunk/load-libs-ql.lisp")
(ensure-ql-lib :babel) (asdf:load-system :babel)
(dolist (s '(:chipi :ironclad :cl-base64 :marshal :snooze :fiveam :cl-mock :split-sequence))
  (ensure-ql-lib s))
(asdf:load-system :chipi-api/tests)
(load "trunk/hunchentoot-clamiga.lisp")

(in-package :cl-user)
;; Instrument sento %create-actor step-by-step.
(in-package :sento.actor-context)
(defun %message-box-for-dispatcher-id (context dispatcher-id queue-size mbox-type)
  (format t "~&[MB] enter dispatcher-id=~A~%" dispatcher-id) (finish-output)
  (case dispatcher-id
    (:pinned (make-instance 'mesgb:message-box/bt :max-queue-size queue-size))
    (otherwise
     (let* ((asys (system context))
            (sys-config (asys:config asys))
            (disp-config (%get-dispatcher-config sys-config dispatcher-id))
            (dispatcher (progn (format t "~&[MB] getting shared dispatcher...~%")
                               (finish-output)
                               (%get-shared-dispatcher asys dispatcher-id))))
       (format t "~&[MB] dispatcher=~A~%" dispatcher) (finish-output)
       (unless dispatcher (error "No such dispatcher"))
       (let ((eff-mbox-type (if mbox-type mbox-type
                                (getf disp-config :mbox-type 'mesgb:message-box/dp))))
         (format t "~&[MB] make-instance ~A ...~%" eff-mbox-type) (finish-output)
         (let ((mb (make-instance eff-mbox-type :dispatcher dispatcher
                                  :max-queue-size queue-size)))
           (format t "~&[MB] make-instance returned~%") (finish-output)
           mb))))))
(defun %create-actor (context create-fun dispatcher-id queue-size mbox-type)
  (format t "~&[AC] %create-actor: calling create-fun~%") (finish-output)
  (let ((actor (funcall create-fun)))
    (format t "~&[AC] create-fun returned actor=~A~%" actor) (finish-output)
    (when actor
      (format t "~&[AC] verify-actor...~%") (finish-output)
      (%verify-actor context actor)
      (format t "~&[AC] making message-box...~%") (finish-output)
      (let ((mbox (%message-box-for-dispatcher-id context dispatcher-id queue-size mbox-type)))
        (format t "~&[AC] mbox=~A; making actor-context...~%" mbox) (finish-output)
        (let ((ctx (make-actor-context (system context)
                                       (miscutils:mkstr (id context) "/" (act-cell:name actor)))))
          (format t "~&[AC] finalize-initialization (runs init-fun)...~%") (finish-output)
          (act::finalize-initialization actor mbox ctx)
          (format t "~&[AC] finalize done~%") (finish-output))))
    actor))

;; Instrument the sse-manager add-client / ensure path.
(in-package :chipi-api.sse-manager)
(defun make-sse-manager ()
  (format t "~&[MK] make-sse-manager: ensure-isys...~%") (finish-output)
  (let ((isys (isys:ensure-isys)))
    (format t "~&[MK] isys=~A; calling actor-of...~%" isys) (finish-output)
    (let ((a (ac:actor-of isys
                          :name "SSE manager"
                          :init (lambda (self)
                                  (declare (ignore self))
                                  (format t "~&[MK] INIT running (NO subscribe)~%")
                                  (finish-output)
                                  (format t "~&[MK] INIT done~%") (finish-output))
                          :state (make-sse-manager-state)
                          :receive #'sse-manager-receive)))
      (format t "~&[MK] actor-of returned ~A~%" a) (finish-output)
      a)))
(defun ensure-sse-manager ()
  (format t "~&[MGR] ensure-sse-manager: cur=~A running=~A~%"
          *sse-manager*
          (and *sse-manager* (ignore-errors (act-cell:running-p *sse-manager*))))
  (finish-output)
  (unless (and *sse-manager* (act-cell:running-p *sse-manager*))
    (format t "~&[MGR] creating sse-manager actor...~%") (finish-output)
    (setf *sse-manager*
          (handler-case (make-sse-manager)
            (serious-condition (c)
              (format t "~&[MGR] make-sse-manager ERROR type=~A: ~A~%"
                      (type-of c) c) (finish-output)
              (error c))))
    (format t "~&[MGR] sse-manager actor created: ~A~%" *sse-manager*) (finish-output))
  *sse-manager*)
(defun add-client (stream)
  (format t "~&[MGR] add-client: asking manager...~%") (finish-output)
  (handler-case
      (let ((r (? (ensure-sse-manager) `(:add-client :stream ,stream))))
        (format t "~&[MGR] add-client ask returned ~A~%" r) (finish-output)
        r)
    (serious-condition (c)
      (format t "~&[MGR] add-client ERROR type=~A: ~A~%" (type-of c) c)
      (finish-output)
      (error c))))

;; Instrument the server-side SSE handler to log every step + the exact
;; condition that terminates the loop (type + report).
(in-package :chipi-api.events-controller)
;; ISOLATION VARIANT 2: create the sse-manager (ensure-sse-manager, incl. init/subscribe)
;; but SKIP the (? manager :add-client) ask, then write heartbeats.
(defun handle-sse-connection (stream)
  (format t "~&[SRV] (ISO2) enter~%") (finish-output)
  (write-sse-connection stream "Connected to item events")
  (format t "~&[SRV] connection written~%") (finish-output)
  (format t "~&[SRV] streams: *query-io*=~A *debug-io*=~A *std-out*=~A *err*=~A~%"
          (ignore-errors (type-of *query-io*)) (ignore-errors (type-of *debug-io*))
          (ignore-errors (type-of *standard-output*)) (ignore-errors (type-of *error-output*)))
  (finish-output)
  (format t "~&[SRV] calling ensure-sse-manager (debugger-isolated)...~%") (finish-output)
  (let ((*query-io* (make-two-way-stream (make-string-input-stream "") *standard-output*))
        (*debug-io* (make-two-way-stream (make-string-input-stream "") *standard-output*))
        (*break-on-signals* nil))
    (handler-case
        (handler-bind ((warning (lambda (c)
                                  (format t "~&[SRV] WARN during ensure: ~A~%" c)
                                  (finish-output) (muffle-warning c))))
          (let ((mgr (chipi-api.sse-manager::ensure-sse-manager)))
            (format t "~&[SRV] ensure-sse-manager RETURNED: ~A~%" mgr) (finish-output)))
      (serious-condition (c)
        (format t "~&[SRV] SERIOUS during ensure: ~A: ~A~%" (type-of c) c) (finish-output))))
  (handler-case
      (dotimes (i 2)
        (sleep 0.2)
        (format t "~&[SRV] writing hardcoded heartbeat ~A; open-p=~A~%" i (open-stream-p stream)) (finish-output)
        (write-sse-heartbeat stream (get-universal-time))
        (format t "~&[SRV] heartbeat ~A written~%" i) (finish-output))
    (condition (c) (format t "~&[SRV] CONDITION: ~A: ~A~%" (type-of c) c) (finish-output)))
  (format t "~&[SRV] (ISOLATION) EXIT~%") (finish-output))
(defun handle-sse-connection--orig (stream)
  (let ((client-id)
        (client-id-fut (add-client stream)))
    (format t "~&[SRV] add-client returned fut=~A~%" client-id-fut) (finish-output)
    (future:fcompleted client-id-fut (cid)
      (format t "~&[SRV] client connected cid=~A~%" cid) (finish-output)
      (setf client-id cid))
    (handler-case
        (let ((heartbeat-count 0))
          (loop
            (when (and *max-heartbeats* (>= heartbeat-count *max-heartbeats*))
              (format t "~&[SRV] max heartbeats reached, returning~%") (finish-output)
              (return))
            (sleep *heartbeat-sleep-time-s*)
            (format t "~&[SRV] writing heartbeat ~A; open-p=~A~%"
                    heartbeat-count (open-stream-p stream)) (finish-output)
            (write-sse-heartbeat stream (get-universal-time))
            (format t "~&[SRV] heartbeat ~A written~%" heartbeat-count) (finish-output)
            (when *max-heartbeats*
              (incf heartbeat-count))))
      (condition (c)
        (format t "~&[SRV] CONDITION caught: type=~A report=~A~%"
                (type-of c) c) (finish-output)
        (remove-client client-id)))
    (format t "~&[SRV] handle-sse-connection EXIT~%") (finish-output)))
(in-package :cl-user)

(defparameter *lines-read* 0)

(defun watchdog (secs)
  (mp:make-thread
   (lambda ()
     (sleep secs)
     (format t "~&!!!! WATCHDOG fired after ~Ds — lines-read so far = ~A~%"
             secs *lines-read*)
     (dolist (th (mp:all-threads))
       (format t "   alive thread: ~A~%" (ignore-errors (mp:thread-name th))))
     (finish-output)
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
    (watchdog 300)
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
                    (format t "~&EOF on stream after ~D lines~%" (length sse-data))
                    (finish-output)
                    (setf done t))
                   ((> (length line) 0)
                    (incf *lines-read*)
                    (format t "~&LINE ~D: ~A~%" *lines-read* line) (finish-output)
                    (push line sse-data)
                    (when (and (search "\"event\":" line)
                               (search "\"type\":\"connection\"" line))
                      (format t "~&>> connection seen, setting item value 25.5~%")
                      (finish-output)
                      (item:set-value item 25.5))
                    (when (= 4 (length sse-data))
                      (setf done t)
                      (close stream)))))
        (format t "~&COMPLETED, lines=~D~%" (length sse-data))
        (finish-output)))
    (api:stop))
  (quit 0))

(run)
