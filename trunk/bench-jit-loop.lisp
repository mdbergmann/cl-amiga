;;; bench-jit-loop.lisp — measure m68k JIT call/dispatch overhead.
;;;
;;; Loaded by tests/amiga/run-tests.lisp after the test summary (Amiga
;;; only — host has no native codegen).  Reports calls/sec for a tight
;;; loop of JIT'd function invocations, which is the path that carries
;;; the per-call cl_jit_invoke cost (incl. the backtrace shadow frame).

(defun bjl-leaf (x) x)            ; 1-arg identity (JIT passthrough shape)
(defun bjl-add (a b) (+ a b))     ; small arithmetic leaf

(defun bjl-run (n)
  (let ((s 0))
    (dotimes (i n)
      (setq s (bjl-add (bjl-leaf i) 1)))
    s))

(let* ((n 1000000)
       (t0 (get-internal-real-time))
       (r  (bjl-run n))
       (t1 (get-internal-real-time))
       (ms (- t1 t0))
       (calls (* n 2)))
  (declare (ignore r))
  (format t "JIT-BENCH calls=~D ms=~D calls/sec=~D~%"
          calls ms
          (if (> ms 0) (round (/ (* calls 1000) ms)) -1)))
