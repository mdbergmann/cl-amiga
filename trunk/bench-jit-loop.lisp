;;; bench-jit-loop.lisp — measure the m68k JIT across opcode mixes.
;;;
;;; Each micro-benchmark defines two copies of the same function back-
;;; to-back with the JIT toggled in between:
;;;
;;;   *-bc   — JIT disabled at defun time, runs through the bytecode
;;;            interpreter forever.
;;;   *-nat  — JIT enabled, walker compiles every supported op so the
;;;            loop runs in native m68k with no re-entry into the
;;;            interpreter until OP_RET.
;;;
;;; The bodies are identical — only the dispatch path changes.  Each
;;; micro-bench targets a different opcode mix so the cache's
;;; contribution is visible per shape:
;;;
;;;   sum-to         tagbody+go fixnum loop — LOAD/STORE/POP traffic
;;;                  dominates, plus one ADD + one LT per iteration.
;;;                  The stack-cache headline case.
;;;
;;;   arith-chain    same loop shape but the body performs a chain of
;;;                  binary arithmetic ops on cached operands —
;;;                  stresses how well ADD/SUB/MUL/CMP reuse D7/D6
;;;                  without spilling.
;;;
;;;   call-loop      same loop shape but the body invokes a separate
;;;                  function each iteration — OP_CALL must spill the
;;;                  cache before the JSR, so this measures spill cost
;;;                  versus the dispatch saving of the native call.
;;;
;;;   struct-loop    body reads two struct slots per iteration —
;;;                  OP_STRUCT_REF is a helper call too, but the
;;;                  helper is short and the spill cost dominates.
;;;
;;; All benchmarks stay in fixnum range at their measured N (max
;;; intermediate < CL_FIXNUM_MAX = 2^30 - 1) so the fixnum fast paths
;;; run the whole time and the slow-path JSR is never reached.  On host
;;; (no JIT) both copies are bytecode and the "speedup" should be ~1.0×.

;; Run one benchmark shape at a given N.  `bc-form` / `nat-form` are
;; the same `(defun NAME (n) ...)` body, with NAME suffixed so the
;; bytecode and JIT'd versions don't clobber each other.  The JIT
;; toggle is flipped around each EVAL so the bytecode copy never sees
;; the JIT.  `caller` is a function (lambda) that takes a function-
;; designator and an `n` and invokes the benchmark — this indirection
;; lets the caller pass extra closed-over arguments (e.g. a struct
;; for struct-loop).  After both are defined, sanity-check that they
;; agree on a warm-up call before timing.
(defun run-bench (label bc-sym nat-sym bc-form nat-form caller n)
  (format t "~%~A (N=~A):~%" label n)
  (clamiga::%jit-set-active nil)
  (eval bc-form)
  (clamiga::%jit-set-active t)
  (eval nat-form)
  (format t "  diag: bc native=~A nat native=~A~%"
          (if (clamiga::%jit-dump-bytes (symbol-function bc-sym)) 'yes 'no)
          (if (clamiga::%jit-dump-bytes (symbol-function nat-sym)) 'yes 'no))
  (let ((b (funcall caller bc-sym n))
        (j (funcall caller nat-sym n)))
    (unless (equal b j)
      (format t "  MISMATCH: bytecode=~A jit=~A~%" b j)
      (return-from run-bench nil)))
  ;; Warm-up.
  (funcall caller bc-sym 100) (funcall caller nat-sym 100)
  (let ((tb (let ((t0 (get-internal-real-time)))
              (let ((r (funcall caller bc-sym n)))
                (let ((dt (- (get-internal-real-time) t0)))
                  (format t "  ~A: ~A ms  (result ~A)~%" "bytecode" dt r)
                  dt))))
        (tj (let ((t0 (get-internal-real-time)))
              (let ((r (funcall caller nat-sym n)))
                (let ((dt (- (get-internal-real-time) t0)))
                  (format t "  ~A: ~A ms  (result ~A)~%" "jit     " dt r)
                  dt)))))
    (cond
      ((or (zerop tb) (zerop tj))
       (format t "  (loop too short to time at this N — increase)~%"))
      ((< tb tj)
       (format t "  jit slower by ~,2Fx — investigate~%"
               (/ (float tj) (float tb))))
      (t
       (format t "  speedup: ~,2Fx (~A ms → ~A ms)~%"
               (/ (float tb) (float tj)) tb tj)))))

;; --- 1) sum-to: cache headline — heavy LOAD/STORE/POP + ADD + LT ---
;;
;; sum at N=40000 is 799 980 000, comfortably under CL_FIXNUM_MAX
;; (2^30 - 1 = 1 073 741 823), so the ADD fast path runs the entire
;; loop without overflowing into bignum.
(run-bench "sum-to (tagbody+go fixnum loop)"
           'sum-to-bc 'sum-to-nat
           '(defun sum-to-bc (n)
              (let ((s 0) (i 0))
                (tagbody
                   top
                   (if (< i n)
                       (progn (setq s (+ s i))
                              (setq i (+ i 1))
                              (go top))))
                s))
           '(defun sum-to-nat (n)
              (let ((s 0) (i 0))
                (tagbody
                   top
                   (if (< i n)
                       (progn (setq s (+ s i))
                              (setq i (+ i 1))
                              (go top))))
                s))
           (lambda (fn n) (let ((r (funcall fn n))) r))
           40000)

;; --- 2) arith-chain: binary-op-heavy body ---
;;
;; Each iteration runs: s = (s + i) - (i * 2 - 1).  Four binary ops
;; per iteration plus one LT for the loop guard.  The two-source
;; operands ((s i) for +, (i 2) for *) sit in adjacent cache slots
;; before each op, which is the 3-slot cache's sweet spot.
;;
;; The intermediate values stay fixnum-bounded: at N=20000, i ranges
;; 0..19999, (i*2-1) is small, the running s converges to 0 (the
;; formula nets to s = sum - sum + small).
(run-bench "arith-chain (binary ops on cached operands)"
           'arith-chain-bc 'arith-chain-nat
           '(defun arith-chain-bc (n)
              (let ((s 0) (i 0))
                (tagbody
                   top
                   (if (< i n)
                       (progn (setq s (- (+ s i) (- (* i 2) 1)))
                              (setq i (+ i 1))
                              (go top))))
                s))
           '(defun arith-chain-nat (n)
              (let ((s 0) (i 0))
                (tagbody
                   top
                   (if (< i n)
                       (progn (setq s (- (+ s i) (- (* i 2) 1)))
                              (setq i (+ i 1))
                              (go top))))
                s))
           (lambda (fn n) (let ((r (funcall fn n))) r))
           20000)

;; --- 3) call-loop: OP_CALL must spill the cache ---
;;
;; Body calls a one-arg identity function each iteration.  This is the
;; spill-cost stress case — the cache pays a cost at each OP_CALL
;; even though the JIT'd dispatch is still cheaper than bytecode
;; OP_CALL.  Expect a modest speedup but smaller than the cache-
;; favoured benches.  The LET-wrap forces non-tail position so the
;; compiler emits OP_CALL, not OP_TAILCALL (which the walker bails on).
(defun bench-callee (x) x)
(run-bench "call-loop (OP_CALL spills cache each iter)"
           'call-loop-bc 'call-loop-nat
           '(defun call-loop-bc (n)
              (let ((s 0) (i 0))
                (tagbody
                   top
                   (if (< i n)
                       (progn (setq s (+ s (let ((r (bench-callee i))) r)))
                              (setq i (+ i 1))
                              (go top))))
                s))
           '(defun call-loop-nat (n)
              (let ((s 0) (i 0))
                (tagbody
                   top
                   (if (< i n)
                       (progn (setq s (+ s (let ((r (bench-callee i))) r)))
                              (setq i (+ i 1))
                              (go top))))
                s))
           (lambda (fn n) (let ((r (funcall fn n))) r))
           20000)

;; --- 4) struct-loop: OP_STRUCT_REF helper-call traffic ---
;;
;; Body reads two slots of a struct each iteration and folds them
;; into the running sum.  STRUCT_REF is a helper call that needs the
;; cache spilled at the call site, similar to OP_CALL.  Both slots
;; hold small fixnums so the running sum stays bounded.
(defstruct bench-pt x y)
(let ((p (make-bench-pt :x 3 :y 5)))
  (run-bench "struct-loop (OP_STRUCT_REF helper x2 per iter)"
             'struct-loop-bc 'struct-loop-nat
             '(defun struct-loop-bc (n p)
                (let ((s 0) (i 0))
                  (tagbody
                     top
                     (if (< i n)
                         (progn (setq s (+ s (+ (bench-pt-x p) (bench-pt-y p))))
                                (setq i (+ i 1))
                                (go top))))
                  s))
             '(defun struct-loop-nat (n p)
                (let ((s 0) (i 0))
                  (tagbody
                     top
                     (if (< i n)
                         (progn (setq s (+ s (+ (bench-pt-x p) (bench-pt-y p))))
                                (setq i (+ i 1))
                                (go top))))
                  s))
             (lambda (fn n) (let ((r (funcall fn n p))) r))
             20000))

(format t "~%(sum-to-nat disassembly:)~%")
(clamiga::%jit-disassemble #'sum-to-nat)
