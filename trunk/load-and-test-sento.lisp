;; Load and test Sento via quicklisp.
;; Skips ACTOR-MP-TESTS (known cl_tables_rwlock blocker — see project memory).
;; Works on host; on Amiga needs --heap >= 96M and a large stack.
;;
;; Usage:
;;   ./build/host/clamiga --heap 128M --load trunk/load-and-test-sento.lisp

(setq *load-verbose* t)
(require "asdf")

#+amigaos (load "S:quicklisp/setup.lisp")
#-amigaos (load (merge-pathnames "quicklisp/setup.lisp" (user-homedir-pathname)))

(load "lib/quicklisp-compat.lisp")

(format t "~%--- Quickload :sento and test dependencies ---~%")
(ql:quickload :sento)
(ql:quickload :fiveam)
(ql:quickload :serapeum)
(ql:quickload :lparallel)
(ql:quickload :cl-mock)

(format t "~%--- asdf:load-system :sento/tests (defines all sento test suites) ---~%")
(asdf:load-system :sento/tests)

(defpackage :sento-test-runner
  (:use :cl))

(in-package :sento-test-runner)

;; (package-name suite-name) — package names match defpackage forms in
;; cl-gserver/tests/*.lisp.  ACTOR-MP-TESTS deliberately omitted.
(defparameter *suites*
  '(("SENTO.ATOMIC-TEST"          "ATOMIC-TESTS")
    ("SENTO.CONFIG-TEST"          "CONFIG-TESTS")
    ("SENTO.MISCUTILS-TEST"       "SENTO.MISCUTILS-TEST")
    ("SENTO.TIMEUTILS-TEST"       "SENTO.TIMEUTILS-TEST")
    ("SENTO.WHEEL-TIMER-TEST"     "WHEEL-TIMER-TESTS")
    ("SENTO.BOUNDED-QUEUE-TEST"   "BOUNDED-QUEUE-TESTS")
    ("SENTO.UNBOUNDED-QUEUE-TEST" "UNBOUNDED-QUEUE-TESTS")
    ("SENTO.EVENTSTREAM-TEST"     "EVENTSTREAM-TESTS")
    ("SENTO.FUTURE-TEST"          "FUTURE-TESTS")
    ("SENTO.ASYNC-FUTURE-TEST"    "ASYNC-FUTURE-TESTS")
    ("SENTO.DISPATCHER-TEST"      "DISPATCHER-TESTS")
    ("SENTO.MESSAGE-BOX-TEST"     "MESSAGE-BOX-TESTS")
    ("SENTO.ACTOR-CELL-TEST"      "ACTOR-CELL-TESTS")
    ("SENTO.ACTOR-TEST"           "ACTOR-TESTS")
    ("SENTO.AGENT-TEST"           "AGENT-TESTS")
    ("SENTO.AGENT.HASH-TEST"      "AGENT.HASH-TESTS")
    ("SENTO.AGENT.ARRAY-TEST"     "AGENT.ARRAY-TESTS")
    ("SENTO.FSM-TEST"             "FSM-TESTS")
    ("SENTO.ROUTER-TEST"          "ROUTER-TESTS")
    ("SENTO.STASH-TEST"           "STASH-TESTS")
    ("SENTO.TASKS-TEST"           "TASKS-TESTS")
    ("SENTO.ACTOR-CONTEXT-TEST"   "ACTOR-CONTEXT-TESTS")
    ("SENTO.ACTOR-TREE-TEST"      "ACTOR-TREE-TESTS")
    ("SENTO.ACTOR-SYSTEM-TEST"    "ACTOR-SYSTEM-TESTS")
    ("SENTO.SPAWN-IN-RECEIVE-TEST" "SPAWN-IN-RECEIVE-TESTS")))

(defparameter *grand-pass*  0)
(defparameter *grand-fail*  0)
(defparameter *grand-skip*  0)
(defparameter *grand-error* 0)
(defparameter *suite-rows*  '())

(defun run-suite (pkg-name suite-name)
  (let ((pkg (find-package pkg-name)))
    (unless pkg
      (format t "  [missing package ~A] skipped~%" pkg-name)
      (push (list suite-name 0 0 0 1 "missing package") *suite-rows*)
      (incf *grand-error*)
      (return-from run-suite))
    (let ((sym (find-symbol suite-name pkg)))
      (unless sym
        (format t "  [missing suite ~A in ~A] skipped~%" suite-name pkg-name)
        (push (list suite-name 0 0 0 1 "missing suite") *suite-rows*)
        (incf *grand-error*)
        (return-from run-suite))
      (format t "~%==== Running suite ~A::~A ====~%" pkg-name suite-name)
      (let ((started (get-internal-real-time)))
        (handler-case
            (let* ((results  (fiveam:run sym))
                   (failed   (remove-if-not #'fiveam::test-failure-p results))
                   (skipped  (remove-if-not #'fiveam::test-skipped-p results))
                   (total    (length results))
                   (pass     (- total (length failed) (length skipped)))
                   (elapsed  (/ (- (get-internal-real-time) started)
                                internal-time-units-per-second)))
              (fiveam:explain! results)
              (format t "~%  ~A: pass=~D fail=~D skip=~D total=~D  (~,2Fs)~%"
                      suite-name pass (length failed) (length skipped) total elapsed)
              (incf *grand-pass* pass)
              (incf *grand-fail* (length failed))
              (incf *grand-skip* (length skipped))
              (push (list suite-name pass (length failed) (length skipped) 0
                          (format nil "~,2Fs" elapsed))
                    *suite-rows*))
          (error (e)
            (let ((elapsed (/ (- (get-internal-real-time) started)
                              internal-time-units-per-second)))
              (format t "~%  ~A: ERROR ~A  (~,2Fs)~%" suite-name e elapsed)
              (incf *grand-error*)
              (push (list suite-name 0 0 0 1
                          (format nil "ERROR ~A" e))
                    *suite-rows*))))))))

(format t "~%========== Sento test runner (skipping ACTOR-MP-TESTS) ==========~%")

(loop :for (pkg suite) :in *suites*
      :do (run-suite pkg suite))

(format t "~%~%========== Per-suite results ==========~%")
(format t "~A~%" (make-string 78 :initial-element #\-))
(format t "~%~A~%"
        (format nil "~30A ~6A ~6A ~6A ~7A ~A"
                "SUITE" "PASS" "FAIL" "SKIP" "ERROR" "TIME"))
(format t "~A~%" (make-string 78 :initial-element #\-))
(loop :for (name pass fail skip err note) :in (reverse *suite-rows*)
      :do (format t "~30A ~6D ~6D ~6D ~7D ~A~%"
                  name pass fail skip err note))
(format t "~A~%" (make-string 78 :initial-element #\-))
(format t "~%========== Grand totals ==========~%")
(format t "  pass    = ~D~%" *grand-pass*)
(format t "  fail    = ~D~%" *grand-fail*)
(format t "  skip    = ~D~%" *grand-skip*)
(format t "  errors  = ~D (suites that failed to run)~%" *grand-error*)
(format t "  total   = ~D test results across ~D suites~%"
        (+ *grand-pass* *grand-fail* *grand-skip*)
        (- (length *suites*) *grand-error*))
(format t "~%(ACTOR-MP-TESTS deliberately skipped — see project memory.)~%")
