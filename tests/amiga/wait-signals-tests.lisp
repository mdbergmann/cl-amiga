; AMIGA:WAIT-SIGNALS tests -- AmigaOS / MorphOS
;
; Loaded by tests/amiga/run-tests.lisp (CHECK, *PASS-COUNT*, *FAIL-COUNT*
; come from there).  A separate file because the forms name symbols in
; AMIGA.RAW.EXEC, which the reader resolves as it reads -- so the REQUIRE
; that creates the package has to have run in an earlier read/eval cycle.
;
; What is under test is the GC bracket, not the Wait: a task asleep in a raw
; exec Wait() is invisible to a stop-the-world collection started by another
; thread, which then waits for a safepoint the sleeper cannot reach until a
; signal wakes it.  A GUI event loop with a worker thread stalls every
; collection that way (found by the Lisp Clamacs, whose client thread
; consed while the MUI task waited).  So the worker below runs a FULL GC
; while the main task is parked in WAIT-SIGNALS and only signals it
; afterwards: without the safe region the collection never finishes, the
; worker never signals, and the suite's watchdog reports a hang.

(require "amiga/raw/exec")

(handler-case
    (let ((me (amiga.raw.exec:find-task nil))
          (bit (amiga.raw.exec:alloc-signal -1)))
      (check "wait-signals: a signal bit is allocated for the test" t
             (and (integerp bit) (>= bit 0) t))
      (when (>= bit 0)
        (let* ((mask (ash 1 bit))
               (gc-ran nil)
               (worker (mp:make-thread
                        (lambda ()
                          (sleep 0.3)
                          (ext:gc)
                          (setf gc-ran t)
                          (amiga.raw.exec:signal me mask))
                        :name "wait-signals-test")))
          ;; SIGBREAKF_CTRL_C in the mask as an event loop would have it.
          (let ((got (amiga:wait-signals (logior mask #x1000))))
            (check "wait-signals returns the signals received" t
                   (and (integerp got) (logtest got mask) t))
            (check "a full GC on another thread completed while the main task waited" t
                   gc-ran))
          (mp:join-thread worker)
          (amiga.raw.exec:free-signal bit)))
      (check "wait-signals rejects an empty mask" t
             (handler-case (progn (amiga:wait-signals 0) nil)
               (error () t)))
      (check "wait-signals rejects a non-integer mask" t
             (handler-case (progn (amiga:wait-signals "all") nil)
               (error () t))))
  (error (e)
    (setq *fail-count* (+ *fail-count* 1))
    (format t "FAIL: wait-signals tests signaled: ~A~%" e)))
