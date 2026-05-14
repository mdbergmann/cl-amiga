;;; bench-jit-loop.lisp — measure the m68k JIT's headline number.
;;;
;;; Computes sum 0..(N-1) via tagbody+go with fixnum arithmetic and
;;; comparison.  Two flavours of the same function, defined back-to-back
;;; with the JIT toggled in between:
;;;
;;;   sum-bytecode  — JIT disabled at defun time, runs through the
;;;                   bytecode interpreter forever.
;;;   sum-native    — JIT enabled, walker compiles every op (LOAD,
;;;                   STORE, POP, ADD, LT, JNIL, JMP, RET) so the loop
;;;                   executes entirely in native m68k with no re-entry
;;;                   into the interpreter until OP_RET.
;;;
;;; The loop body is identical — only the dispatch path changes.  Run
;;; both with the same N, compare wall-clock time, report a speedup.
;;;
;;; The benchmark stays in fixnum range (max sum at N=10000 is
;;; 49 995 000, well below CL_FIXNUM_MAX = 2^30 - 1) so OP_ADD's
;;; fixnum fast path runs the whole time and the slow-path JSR is
;;; never reached.  On host (no JIT) both functions are bytecode and
;;; the "speedup" should be ~1.0×; on Amiga we expect the projected
;;; ~2× from specs/native-backend.md.

(defun bench-once (label fn n)
  (let ((t0 (get-internal-real-time)))
    (let ((result (funcall fn n)))
      (let ((dt (- (get-internal-real-time) t0)))
        (format t "  ~A: ~A ms  (result ~A)~%" label dt result)
        dt))))

(defun bench (n)
  (format t "~%sum 0..~A via tagbody+go fixnum loop:~%" n)
  ;; Define a bytecode-only copy.
  (clamiga::%jit-set-active nil)
  (eval '(defun sum-to-bc (n)
           (let ((s 0) (i 0))
             (tagbody
                top
                (if (< i n)
                    (progn (setq s (+ s i))
                           (setq i (+ i 1))
                           (go top))))
             s)))
  ;; Define a JIT'd copy of the same body.
  (clamiga::%jit-set-active t)
  (eval '(defun sum-to-nat (n)
           (let ((s 0) (i 0))
             (tagbody
                top
                (if (< i n)
                    (progn (setq s (+ s i))
                           (setq i (+ i 1))
                           (go top))))
             s)))
  ;; Sanity: both produce the same result before timing.
  (let ((b (sum-to-bc n)) (j (sum-to-nat n)))
    (unless (= b j)
      (format t "  MISMATCH: bytecode=~A jit=~A~%" b j)
      (return-from bench nil)))
  ;; Warm-up: one run each to settle caches.
  (sum-to-bc 100) (sum-to-nat 100)
  ;; Timed runs.
  (let ((tb (bench-once "bytecode" #'sum-to-bc  n))
        (tj (bench-once "jit     " #'sum-to-nat n)))
    (cond
      ((or (zerop tb) (zerop tj))
       (format t "  (loop too short to time at this N — increase)~%"))
      ((< tb tj)
       (format t "  jit slower by ~,2Fx — investigate~%"
               (/ (float tj) (float tb))))
      (t
       (format t "  speedup: ~,2Fx (~A ms → ~A ms)~%"
               (/ (float tb) (float tj)) tb tj)))))

;; Run at a few sizes so the noise floor is obvious.  The exact N
;; values that produce meaningful timings depend on the target: a
;; 14 MHz 68020 needs ~10k iters to land above 1 ms; a 100 MHz JIT
;; A4000 might need 100k+.
(bench 10000)
(bench 50000)
(bench 100000)

(format t "~%(jit native-fn disassembly:)~%")
(clamiga::%jit-disassemble #'sum-to-nat)
