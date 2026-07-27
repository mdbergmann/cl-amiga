;; repro-mos-amcache.lisp -- standalone repro for the MorphOS hang at the
;; run-tests.lisp check "concurrent dispatch does not corrupt the dispatch
;; cache" (the test AFTER "concurrent add-method of distinct methods loses
;; none", which is the last PASS printed before the hang).
;;
;; Run on MorphOS:
;;     setenv CLAMIGA_STW_DIAG 5000
;;     build/morphos/clamiga --load tests/repro-mos-amcache.lisp
;;
;; What the instrumentation tells you:
;;   * Progress markers print before/after every phase (defmethod eval,
;;     worker spawn, join), so the exact stall point is visible.
;;   * A watchdog thread dumps every thread's wait state (mp:dump-thread-waits)
;;     each time 10s pass without the progress counter moving.
;;   * If the watchdog itself goes silent, the stop-the-world GC is stuck and
;;     every thread (including the watchdog) is parked at a safepoint -- that
;;     case is covered by CLAMIGA_STW_DIAG, which makes the STW initiator
;;     report the straggler thread id and dump all threads from inside the
;;     stuck wait loop, and turns its wait into a timed wait whose rescan
;;     also un-sticks (and reports) a lost gc_condvar wakeup.

(defparameter *progress* 0)
(defparameter *done* nil)

(defparameter *watchdog*
  (mp:make-thread
   (lambda ()
     (let ((last -1))
       (loop
         (sleep 10)
         (when *done* (return :ok))
         (if (= last *progress*)
             (progn
               (format t "~%[watchdog] NO PROGRESS since last check (counter=~a) -- thread dump:~%"
                       *progress*)
               (force-output)
               (mp:dump-thread-waits))
             (setq last *progress*)))))
   :name "repro-watchdog"))

(format t "[repro] defining 24 classes + instances...~%") (force-output)

(defparameter *amcache-classes*
  (let ((v (make-array 24)))
    (dotimes (i 24)
      (let ((name (intern (format nil "AMCACHE-CLASS-~a" i))))
        (eval `(defclass ,name () ()))
        (setf (aref v i) name)))
    v))
(defparameter *amcache-instances*
  (let ((v (make-array 24)))
    (dotimes (i 24) (setf (aref v i) (make-instance (aref *amcache-classes* i))))
    v))

(setq *progress* (1+ *progress*))

;; 8 rounds (the suite runs 4) to raise the repro probability.
(let ((total-fails 0))
  (dotimes (round 8)
    (format t "[repro] round ~a: defgeneric + 24 defmethods (main thread)...~%" round)
    (force-output)
    (eval '(defgeneric amcache-dispatch (x)))
    (dotimes (i 24)
      (eval `(defmethod amcache-dispatch ((x ,(aref *amcache-classes* i))) ,i)))
    (setq *progress* (1+ *progress*))
    (format t "[repro] round ~a: spawning 4 dispatch workers...~%" round)
    (force-output)
    (let ((workers nil) (fails 0))
      (dotimes (w 4)
        (push (mp:make-thread
               (lambda ()
                 (let ((bad 0))
                   (dotimes (p 20)
                     (dotimes (i 24)
                       (let ((r (handler-case
                                    (amcache-dispatch (aref *amcache-instances* i))
                                  (error () :fail))))
                         (unless (eql r i) (setq bad (1+ bad)))))
                     ;; benignly racy counter -- only "did it move" matters
                     (setq *progress* (1+ *progress*)))
                   bad))
               :name "amcache-disp")
              workers))
      (format t "[repro] round ~a: joining workers...~%" round) (force-output)
      (dolist (thr workers) (setq fails (+ fails (mp:join-thread thr))))
      (format t "[repro] round ~a: joined, fails=~a~%" round fails) (force-output)
      (setq total-fails (+ total-fails fails))))
  (setq *done* t)
  (format t "~%[repro] COMPLETE, no hang; total fails=~a (expect 0)~%" total-fails)
  (force-output))

(quit)
