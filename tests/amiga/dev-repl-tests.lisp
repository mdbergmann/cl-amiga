;;; dev-repl-tests.lisp -- the editor's debugger protocol (lib/dev-repl.lisp)
;;; on the Amiga, in-process: no ARexx, a Lisp function stands in for the
;;; editor as in tests/test_dev_commands.sh.  Loaded by run-tests.lisp.
;;;
;;; Why a file of its own on the target: on the host every case here is
;;; green, and the m68k build was not (2026-09-11, the phase-4 leg of
;;; clamacs's drive.rexx) -- a restart back into a nested debugger level,
;;; and CONTINUE on a CERROR, ended the whole form when the function that
;;; signalled had been compiled natively by the JIT.  So each scenario runs
;;; with a natively compiled signaller and with a bytecode-only one, and a
;;; minimal HANDLER-BIND / RESTART-CASE shape without dev-repl runs the same
;;; matrix, so a failure names the layer.

(require "dev-commands")
(require "dev-repl")

(defvar *drt-sent* '())
(defvar *drt-lock* (mp:make-lock))

(setf ext.dev:*repl-send*
      (lambda (port command)
        (declare (ignore port))
        (mp:with-lock-held (*drt-lock*) (push command *drt-sent*))
        (values 0 "")))

(defun drt-prefix-p (prefix s)
  (and (>= (length s) (length prefix)) (string= prefix s :end2 (length prefix))))

(defun drt-sent-p (pred)
  (mp:with-lock-held (*drt-lock*) (and (find-if pred *drt-sent*) t)))

(defun drt-wait (pred)
  (loop repeat 750
        do (when (drt-sent-p pred) (return t))
           (sleep 0.02)))

(defun drt-wait-level (n)
  (let ((want (format nil "DEBUGGER ~d " n)))
    (drt-wait (lambda (c) (drt-prefix-p want c)))))

(defun drt-wait-result ()
  (drt-wait (lambda (c) (drt-prefix-p "RESULT" c))))

(defun drt-clear ()
  (mp:with-lock-held (*drt-lock*) (setf *drt-sent* '())))

(defun drt-last-result ()
  (mp:with-lock-held (*drt-lock*)
    (or (find-if (lambda (c) (drt-prefix-p "RESULT" c)) *drt-sent*) "")))

(defun drt-cmd (c)
  (ext.dev:handle-command c))

;;; The signallers: one the JIT may compile natively, one kept as bytecode.
(defun drt-native-fn (n m)
  (let ((c (* n m)))
    (error "drt native ~a" c)))

(defun drt-native-cerror ()
  (cerror "Go on" "drt native stop")
  :drt-went-on)

(clamiga::%jit-set-active nil)
(defun drt-bytecode-fn (n m)
  (let ((c (* n m)))
    (error "drt bytecode ~a" c)))

(defun drt-bytecode-cerror ()
  (cerror "Go on" "drt bytecode stop")
  :drt-went-on)
(clamiga::%jit-set-active t)

(format t "~&; dev-repl-tests: native signaller has native code: ~a, bytecode one: ~a~%"
        (and (clamiga::%jit-dump-bytes #'drt-native-fn) t)
        (and (clamiga::%jit-dump-bytes #'drt-bytecode-fn) t))
(check "dev-repl: the bytecode signaller has no native code" nil
       (clamiga::%jit-dump-bytes #'drt-bytecode-fn))

;;; ----------------------------------------------------------------
;;; The minimal shape, without dev-repl: a handler on top of the erring
;;; frame runs a nested job with its own RESTART-CASE and handler; the
;;; nested handler invokes the nested ABORT; the outer handler then
;;; invokes the outer one.  The trace says where control actually went.
;;; ----------------------------------------------------------------

(defvar *drt-trace* '())
(defvar *drt-park-lock* (mp:make-lock))
(defvar *drt-park-cv* (mp:make-condition-variable))

(defun drt-park ()
  ;; What the REPL thread does between the announcement and the restart:
  ;; wait on a condition variable (here with a timeout).
  (mp:with-lock-held (*drt-park-lock*)
    (mp:condition-wait *drt-park-cv* *drt-park-lock* 0.05)))

(defmacro drt-define-machinery (inner outer)
  `(progn
     (defun ,inner (fn park)
       (restart-case
           (handler-bind ((error (lambda (c)
                                   (push :hook2 *drt-trace*)
                                   (when park (drt-park))
                                   (invoke-restart (find-restart 'abort c)))))
             (funcall fn 1 2))
         (abort ()
           (push :inner-abort *drt-trace*)
           :inner)))
     (defun ,outer (fn park)
       (restart-case
           (handler-bind ((error (lambda (c)
                                   (push :hook1 *drt-trace*)
                                   (when park (drt-park))
                                   (push (list :inner (,inner fn park)) *drt-trace*)
                                   (invoke-restart (find-restart 'abort c)))))
             (funcall fn 3 4))
         (abort ()
           (push :outer-abort *drt-trace*)
           :outer)))))

(drt-define-machinery drt-inner-native drt-outer-native)
(clamiga::%jit-set-active nil)
(drt-define-machinery drt-inner-bytecode drt-outer-bytecode)
(clamiga::%jit-set-active t)

(dolist (machinery (list (list "native machinery" #'drt-outer-native)
                         (list "bytecode machinery" #'drt-outer-bytecode)))
  (dolist (signaller (list (list "native signaller" #'drt-native-fn)
                           (list "bytecode signaller" #'drt-bytecode-fn)))
    (dolist (park '(nil t))
      (let ((label (format nil "nested abort, ~a, ~a~a" (first machinery) (first signaller)
                           (if park ", parked" ""))))
        (setf *drt-trace* '())
        (let ((result (handler-case (funcall (second machinery) (second signaller) park)
                        (error (e) (list :escaped (princ-to-string e))))))
          (check (format nil "~a: trace" label)
                 '(:hook1 :hook2 :inner-abort (:inner :inner) :outer-abort)
                 (reverse *drt-trace*))
          (check (format nil "~a: result" label) :outer result))))))

;;; ----------------------------------------------------------------
;;; The real thing: dev-repl's debugger, driven as the editor drives it.
;;; ----------------------------------------------------------------

(multiple-value-bind (rc text) (drt-cmd "REPL-ATTACH DRT DEBUG")
  (check "dev-repl: REPL-ATTACH DEBUG" 0 rc)
  (check "dev-repl: REPL-ATTACH answers the package" "CL-USER" text))

(defun drt-nested-abort-scenario (label fname)
  (drt-clear)
  (drt-cmd (format nil "REPL-EVAL (~a 3 4)" fname))
  (check (format nil "dev-repl ~a: an error opens level 1" label) t (drt-wait-level 1))
  (drt-clear)
  (drt-cmd (format nil "FRAME-EVAL 0 (~a 1 2)" fname))
  (check (format nil "dev-repl ~a: an error in a frame eval opens level 2" label) t
         (drt-wait-level 2))
  (drt-clear)
  (drt-cmd "ABORT")
  (drt-wait (lambda (c) (drt-prefix-p "OUTPUT ; Aborted" c)))
  (sleep 0.3)
  (check (format nil "dev-repl ~a: ABORT from level 2 announces level 1 again" label) t
         (drt-sent-p (lambda (c) (drt-prefix-p "DEBUGGER 1 " c))))
  (multiple-value-bind (rc text) (drt-cmd "RESTARTS")
    (check (format nil "dev-repl ~a: RESTARTS after the abort answers level 1" label) t
           (and (= rc 0) (search "level 1:" text) t))
    (when (/= rc 0) (format t "~&; dev-repl ~a: RESTARTS said ~a; sent so far: ~s~%" label text
                            (mp:with-lock-held (*drt-lock*) (reverse *drt-sent*)))))
  (drt-clear)
  (drt-cmd "ABORT")
  (check (format nil "dev-repl ~a: ABORT from level 1 ends the form" label) t (drt-wait-result))
  (check (format nil "dev-repl ~a: the form's value says it was aborted" label) t
         (and (search "; Aborted" (drt-last-result)) t))
  (multiple-value-bind (rc text) (drt-cmd "RESTARTS")
    (declare (ignore text))
    (check (format nil "dev-repl ~a: no level is left" label) 10 rc)))

(defun drt-continue-scenario (label fname)
  (drt-clear)
  (drt-cmd (format nil "REPL-EVAL (~a)" fname))
  (check (format nil "dev-repl ~a: CERROR opens level 1" label) t (drt-wait-level 1))
  (drt-clear)
  (drt-cmd "CONTINUE")
  (check (format nil "dev-repl ~a: CONTINUE ends the form" label) t (drt-wait-result))
  (check (format nil "dev-repl ~a: with the value after the CERROR" label) t
         (and (search ":DRT-WENT-ON" (drt-last-result)) t))
  (when (not (search ":DRT-WENT-ON" (drt-last-result)))
    (format t "~&; dev-repl ~a: RESULT was ~s~%" label (drt-last-result))))

(drt-nested-abort-scenario "bytecode signaller" "drt-bytecode-fn")
(drt-continue-scenario "bytecode cerror" "drt-bytecode-cerror")
(drt-nested-abort-scenario "native signaller" "drt-native-fn")
(drt-continue-scenario "native cerror" "drt-native-cerror")
;; And once more with the bytecode one, so a fault left behind by the native
;; run shows as such rather than as a flake.
(drt-nested-abort-scenario "bytecode signaller again" "drt-bytecode-fn")

(multiple-value-bind (rc text) (drt-cmd "REPL-DETACH")
  (declare (ignore text))
  (check "dev-repl: REPL-DETACH" 0 rc))
(setf ext.dev:*repl-send* nil)
