;;; bench-opt.lisp — optimization-tracking micro-benchmark suite.
;;;
;;; One benchmark (or a before/after pair) per PENDING item in
;;; specs/performance.md, so each optimization's gain can be captured as
;;; a like-for-like delta against the baselines in docs/benchmarks.md:
;;;
;;;   1.3  declaim-speed (const folding, dead branches, check elision)
;;;        -> opt.const-fold, opt.dead-branch, safety0.* vs safety1.*
;;;   1.8  bytecode peephole post-pass
;;;        -> vm.* (store/load, dead code), opt.dead-branch
;;;   2.4  set operations as C builtins
;;;        -> set.*
;;;   2.5  free-list size class segregation
;;;        -> alloc.*
;;;   3.1  CLOS slot access optimization
;;;        -> clos.slot-value, clos.accessor
;;;   3.2  keyword argument pre-computation
;;;        -> kw.call-8keys, clos.make-instance
;;;   3.3  cl_mv_count write reduction
;;;        -> vm.* (every simple opcode writes cl_mv_count today)
;;;
;;; Design:
;;;   - All functions are compiled with the JIT OFF (pure bytecode), so
;;;     VM/compiler changes are measured directly and results are
;;;     comparable across platforms.  trunk/bench.lisp is the suite for
;;;     JIT-vs-bytecode comparisons.
;;;   - Every benchmark has a closed-form expected result computed
;;;     independently of the code under test; a mismatch prints
;;;     BENCH-FAIL — an optimization that breaks semantics fails loudly.
;;;   - Output is one machine-parseable line per benchmark:
;;;       BENCH <name> ms=<best-of-N> bytes=<consed> gc=<cycles> check=<digest> <ok|FAIL>
;;;   - Inner loop sizes are FIXED (expected values stay valid);
;;;     *bo-scale* multiplies only the repetition counts.
;;;
;;; Usage:
;;;   host:   ./build/host/clamiga --heap 64M --load trunk/bench-opt.lisp
;;;   Amiga:  set the knobs before loading, e.g.:
;;;             (defparameter cl-user::*bo-scale* 1/20)
;;;             (defparameter cl-user::*bo-set-size* 150)
;;;             (load "trunk/bench-opt.lisp")

(defvar *bo-scale* 1
  "Multiplier applied to every repetition count (not to inner sizes).")
(defvar *bo-set-size* 600
  "Element count of the large set-operation lists (must be even).")
(defvar *bo-runs* 3
  "Timed runs per benchmark; the best (minimum) time is reported.")

(defvar *bo-total-ms* 0)
(defvar *bo-fails* 0)

(defun %bo-scaled (n) (max 1 (round (* n *bo-scale*))))

;; Sum of the integers in [0, n): n(n-1)/2.
(defun %bo-sum-below (n) (/ (* n (1- n)) 2))
;; Sum of the integers in [a, b).
(defun %bo-sum-range (a b) (- (%bo-sum-below b) (%bo-sum-below a)))

(defun %bo-iota (n &optional (start 0))
  "Ascending list (start start+1 ... start+n-1)."
  (let ((acc nil))
    (dotimes (i n)
      (push (+ start (- n 1 i)) acc))
    acc))

(defun %bo-digest (lst &optional key)
  "Order-independent digest of a list: (count . sum-of-keyed-elements).
Set-operation result order is unspecified by the HyperSpec, so
benchmarks compare digests, not lists."
  (let ((c 0) (s 0))
    (dolist (x lst)
      (setq c (+ c 1))
      (setq s (+ s (if key (funcall key x) x))))
    (cons c s)))

(defun %bo-time-call (thunk)
  "Run THUNK once; return (values result elapsed-ms bytes-consed gc-cycles)."
  (let ((t0 (clamiga::%get-internal-time))
        (b0 (clamiga::%get-bytes-consed))
        (g0 (clamiga::%get-gc-count)))
    (let ((r (funcall thunk)))
      (values r
              (- (clamiga::%get-internal-time) t0)
              (- (clamiga::%get-bytes-consed) b0)
              (- (clamiga::%get-gc-count) g0)))))

(defun %bo-fmt-check (v)
  (if (consp v)
      (format nil "~D:~D" (car v) (cdr v))
      (format nil "~A" v)))

(defun %bo-run (name expected thunk)
  "Warm up once, then time *BO-RUNS* runs of THUNK; report the best.
Verify the (deterministic) result against EXPECTED."
  (funcall thunk)                       ; warmup
  (let ((best-ms nil) (best-b 0) (best-g 0) (result nil) (steady t))
    (dotimes (k *bo-runs*)
      (multiple-value-bind (r ms b g) (%bo-time-call thunk)
        (when (and (> k 0) (not (equal r result)))
          (setq steady nil))
        (setq result r)
        (when (or (null best-ms) (< ms best-ms))
          (setq best-ms ms best-b b best-g g))))
    (setq *bo-total-ms* (+ *bo-total-ms* best-ms))
    (let ((ok (and steady (equal result expected))))
      (unless ok (setq *bo-fails* (+ *bo-fails* 1)))
      (format t "BENCH ~A ms=~D bytes=~D gc=~D check=~A ~A~%"
              name best-ms best-b best-g (%bo-fmt-check result)
              (if ok "ok" "FAIL"))
      (unless ok
        (format t "BENCH-FAIL ~A expected=~A got=~A~A~%"
                name (%bo-fmt-check expected) (%bo-fmt-check result)
                (if steady "" " (nondeterministic across runs)"))))))

;; ---------------------------------------------------------------------
;; Compile everything below as pure bytecode; restore the JIT setting at
;; the end of the file.

(defvar *bo-prior-jit* (clamiga::%jit-active-p))
(clamiga::%jit-set-active nil)

(format t "~%BENCH-META platform=~A scale=~A set-size=~D runs=~D engine=bytecode~%"
        #+amigaos "amiga" #-amigaos "host"
        *bo-scale* *bo-set-size* *bo-runs*)

;; =====================================================================
;; vm.* — VM dispatch-loop hot paths (spec 3.3 mv_count writes, 1.8
;; peephole).  Every arithmetic/load/store opcode currently writes
;; cl_mv_count = 1; these tight loops make that write a measurable
;; fraction of the work.

;; Fixnum accumulate: OP_LOAD/OP_ADD/OP_STORE per iteration.  Inner sum
;; stays comfortably inside fixnum range and resets per rep.
(defun %bo-fixnum-loop (n reps)
  (let ((s 0))
    (dotimes (r reps)
      (setq s 0)
      (dotimes (i n)
        (setq s (+ s i))))
    s))

;; Local shuffle: pure load/store traffic (peephole store-then-reload
;; target).  N is a multiple of 3, so the rotation returns to identity.
(defun %bo-local-shuffle (n reps)
  (let ((a 1) (b 2) (c 3) (tmp 0))
    (dotimes (r reps)
      (setq a 1 b 2 c 3)
      (dotimes (i n)
        (setq tmp a)
        (setq a b)
        (setq b c)
        (setq c tmp)))
    (+ a (* 10 b) (* 100 c))))

;; Call/return overhead at default safety (arg-count check per call).
(defun %bo-inc (x) (+ x 1))
(defun %bo-call-return (n reps)
  (let ((s 0))
    (dotimes (r reps)
      (setq s 0)
      (dotimes (i n)
        (setq s (+ s (%bo-inc i)))))
    s))

(let ((n 20000) (reps (%bo-scaled 100)))
  (%bo-run "vm.fixnum-loop" (%bo-sum-below n)
           (lambda () (%bo-fixnum-loop n reps))))

(let ((n 21000) (reps (%bo-scaled 60)))
  (%bo-run "vm.local-shuffle" 321
           (lambda () (%bo-local-shuffle n reps))))

(let ((n 10000) (reps (%bo-scaled 100)))
  (%bo-run "vm.call-return" (+ (%bo-sum-below n) n)
           (lambda () (%bo-call-return n reps))))

;; =====================================================================
;; opt.* — emit-time optimization targets (spec 1.3).

;; Constant subexpressions: today each (* 3 4), (- 100 58), (ash 1 5),
;; (logand 12 10), (logior 1 2) emits CONST/CONST/op; after constant
;; folding each becomes a single CONST.  Adds 97 per iteration.
(defun %bo-const-fold (n reps)
  (let ((s 0))
    (dotimes (r reps)
      (setq s 0)
      (dotimes (i n)
        (setq s (+ s (* 3 4) (- 100 58) (ash 1 5) (logand 12 10) (logior 1 2)))))
    s))

;; Constant-test branches (the shape macro expansions produce): today
;; each emits test + conditional jump + both arms; after dead-branch
;; elimination only the live arm remains.  Adds 6 per iteration.
(defun %bo-dead-branch (n reps)
  (let ((s 0))
    (dotimes (r reps)
      (setq s 0)
      (dotimes (i n)
        (setq s (+ s
                   (if t 1 99)
                   (if nil 98 2)
                   (if t 3 97)))))
    s))

(let ((n 10000) (reps (%bo-scaled 30)))
  (%bo-run "opt.const-fold" (* 97 n)
           (lambda () (%bo-const-fold n reps))))

(let ((n 10000) (reps (%bo-scaled 60)))
  (%bo-run "opt.dead-branch" (* 6 n)
           (lambda () (%bo-dead-branch n reps))))

;; =====================================================================
;; safety0.* vs safety1.* — safety-gated check elision (spec 1.3).
;; Identical function bodies compiled under (safety 0) and (safety 1)
;; via PROCLAIM (deterministic today and after local-declare scoping
;; lands).  Today only OP_ASSERT_TYPE from `the` is safety-gated, so the
;; pairs should time ~equal; after 1.3 (bounds/arg-count check elision)
;; the safety0 variant should pull ahead.

(proclaim '(optimize (safety 0)))

(defun %bo-svref-sum-s0 (v n reps)
  (let ((s 0))
    (dotimes (r reps)
      (setq s 0)
      (dotimes (j n)
        (setq s (+ s (svref v j)))))
    s))

(defun %bo-add3-s0 (a b c) (+ a b c))
(defun %bo-call3-s0 (n reps)
  (let ((s 0))
    (dotimes (r reps)
      (setq s 0)
      (dotimes (i n)
        (setq s (+ s (%bo-add3-s0 i 1 2)))))
    s))

(proclaim '(optimize (safety 1)))

(defun %bo-svref-sum-s1 (v n reps)
  (let ((s 0))
    (dotimes (r reps)
      (setq s 0)
      (dotimes (j n)
        (setq s (+ s (svref v j)))))
    s))

(defun %bo-add3-s1 (a b c) (+ a b c))
(defun %bo-call3-s1 (n reps)
  (let ((s 0))
    (dotimes (r reps)
      (setq s 0)
      (dotimes (i n)
        (setq s (+ s (%bo-add3-s1 i 1 2)))))
    s))

(let* ((n 1000) (reps (%bo-scaled 1000))
       (v (make-array n)))
  (dotimes (j n) (setf (svref v j) j))
  (%bo-run "safety1.svref-loop" (%bo-sum-below n)
           (lambda () (%bo-svref-sum-s1 v n reps)))
  (%bo-run "safety0.svref-loop" (%bo-sum-below n)
           (lambda () (%bo-svref-sum-s0 v n reps))))

(let ((n 10000) (reps (%bo-scaled 60)))
  (%bo-run "safety1.call-args" (+ (%bo-sum-below n) (* 3 n))
           (lambda () (%bo-call3-s1 n reps)))
  (%bo-run "safety0.call-args" (+ (%bo-sum-below n) (* 3 n))
           (lambda () (%bo-call3-s0 n reps))))

;; =====================================================================
;; set.* — set operations (spec 2.4: today pure Lisp O(n*m) in
;; boot.lisp; planned C builtins with hash-set path for large inputs).
;; Inputs are duplicate-free integer ranges with 50% overlap, so the
;; result digests have closed forms regardless of result order.

;; Small lists: measures per-call/interpreter overhead.
(let* ((l1 (%bo-iota 16))               ; 0..15
       (l2 (%bo-iota 16 8))             ; 8..23
       (reps (%bo-scaled 3000)))
  (%bo-run "set.intersection-small"
           (cons 8 (%bo-sum-range 8 16))
           (lambda ()
             (let ((d nil))
               (dotimes (r reps)
                 (setq d (%bo-digest (intersection l1 l2))))
               d)))
  (%bo-run "set.union-small"
           (cons 24 (%bo-sum-below 24))
           (lambda ()
             (let ((d nil))
               (dotimes (r reps)
                 (setq d (%bo-digest (union l1 l2))))
               d))))

;; Large lists: measures the O(n*m) -> O(n+m) algorithmic win.
(let* ((m *bo-set-size*)                ; even
       (h (/ m 2))
       (l1 (%bo-iota m))                ; 0..m-1
       (l2 (%bo-iota m h))              ; h..m+h-1
       (lsub (%bo-iota h))              ; 0..h-1 (subset of l1)
       (reps (%bo-scaled 5))
       (sub-reps (%bo-scaled 30)))
  (%bo-run "set.intersection-large"
           (cons h (%bo-sum-range h m))
           (lambda ()
             (let ((d nil))
               (dotimes (r reps)
                 (setq d (%bo-digest (intersection l1 l2))))
               d)))
  (%bo-run "set.union-large"
           (cons (+ m h) (%bo-sum-below (+ m h)))
           (lambda ()
             (let ((d nil))
               (dotimes (r reps)
                 (setq d (%bo-digest (union l1 l2))))
               d)))
  (%bo-run "set.difference-large"
           (cons h (%bo-sum-below h))
           (lambda ()
             (let ((d nil))
               (dotimes (r reps)
                 (setq d (%bo-digest (set-difference l1 l2))))
               d)))
  (%bo-run "set.subsetp-large" t
           (lambda ()
             (let ((d nil))
               (dotimes (r sub-reps)
                 (setq d (and (subsetp lsub l1) t)))
               d))))

;; :test #'equal — fresh strings so EQ fails and the equality function
;; is actually exercised.  All strings are length 5 ("s0042").
(let* ((m 200)
       (h (/ m 2))
       (l1 (let ((acc nil))
             (dotimes (i m) (push (format nil "s~4,'0D" i) acc))
             acc))
       (l2 (let ((acc nil))
             (dotimes (i m) (push (format nil "s~4,'0D" (+ h i)) acc))
             acc))
       (reps (%bo-scaled 20)))
  (%bo-run "set.intersection-equal"
           (cons h (* 5 h))
           (lambda ()
             (let ((d nil))
               (dotimes (r reps)
                 (setq d (%bo-digest (intersection l1 l2 :test #'equal)
                                     #'length)))
               d))))

;; :key #'car on alists.
(let* ((m 300)
       (h (/ m 2))
       (l1 (let ((acc nil))
             (dotimes (i m) (push (cons i 'a) acc))
             acc))
       (l2 (let ((acc nil))
             (dotimes (i m) (push (cons (+ h i) 'b) acc))
             acc))
       (reps (%bo-scaled 10)))
  (%bo-run "set.intersection-key"
           (cons h (%bo-sum-range h m))
           (lambda ()
             (let ((d nil))
               (dotimes (r reps)
                 (setq d (%bo-digest (intersection l1 l2 :key #'car)
                                     #'car)))
               d))))

;; =====================================================================
;; kw.* — keyword-argument matching (spec 3.2: O(n_keys * n_supplied)
;; scan per call today; planned per-function keyword map).

(defun %bo-kw8 (a &key (k1 0) (k2 0) (k3 0) (k4 0)
                       (k5 0) (k6 0) (k7 0) (k8 0))
  (+ a k1 k2 k3 k4 k5 k6 k7 k8))

(let ((n 10000) (reps (%bo-scaled 40)))
  (%bo-run "kw.call-8keys" (+ (%bo-sum-below n) (* 10 n))
           (lambda ()
             (let ((s 0))
               (dotimes (r reps)
                 (setq s 0)
                 (dotimes (i n)
                   (setq s (+ s (%bo-kw8 i :k2 1 :k5 2 :k7 3 :k8 4)))))
               s))))

;; =====================================================================
;; clos.* — CLOS slot access (spec 3.1) and make-instance initarg
;; processing (specs 3.1 + 3.2).

(defclass %bo-point ()
  ((x :initarg :x :accessor %bo-point-x)
   (y :initarg :y :accessor %bo-point-y)))

(let ((n 1000) (reps (%bo-scaled 20)))
  (%bo-run "clos.make-instance" (+ (* 2 (%bo-sum-below n)) n)
           (lambda ()
             (let ((s 0))
               (dotimes (r reps)
                 (setq s 0)
                 (dotimes (i n)
                   (let ((p (make-instance '%bo-point :x i :y (+ i 1))))
                     (setq s (+ s (%bo-point-x p) (%bo-point-y p))))))
               s))))

(let ((n 10000) (reps (%bo-scaled 10))
      (p (make-instance '%bo-point :x 3 :y 4)))
  (%bo-run "clos.slot-value" (* 7 n)
           (lambda ()
             (let ((s 0))
               (dotimes (r reps)
                 (setq s 0)
                 (dotimes (i n)
                   (setq s (+ s (slot-value p 'x) (slot-value p 'y)))))
               s)))
  (%bo-run "clos.accessor" (* 7 n)
           (lambda ()
             (let ((s 0))
               (dotimes (r reps)
                 (setq s 0)
                 (dotimes (i n)
                   (setq s (+ s (%bo-point-x p) (%bo-point-y p)))))
               s))))

;; =====================================================================
;; alloc.* — allocator behavior (spec 2.5 free-list size classes).
;; Mixed object sizes retained in a 512-slot ring: continuous frees of
;; assorted sizes keep the free list populated and exercised.

(defun %bo-alloc-mixed (n reps)
  (let ((ring (make-array 512 :initial-element nil))
        (count 0))
    (dotimes (r reps)
      (dotimes (i n)
        (setf (svref ring (logand count 511))
              (list (list i i i i)
                    (make-array 8 :initial-element i)
                    (make-string 16 :initial-element #\x)))
        (setq count (+ count 1))))
    count))

;; Pure cons churn: short-lived lists, the 8-byte size class.
(defun %bo-alloc-cons (len reps)
  (let ((last 0))
    (dotimes (r reps)
      (let ((acc nil))
        (dotimes (i len) (push i acc))
        (setq last (length acc))))
    last))

(let ((n 1000) (reps (%bo-scaled 300)))
  (%bo-run "alloc.mixed-churn" (* n reps)
           (lambda () (%bo-alloc-mixed n reps))))

(let ((len 100) (reps (%bo-scaled 10000)))
  (%bo-run "alloc.cons-churn" len
           (lambda () (%bo-alloc-cons len reps))))

;; ---------------------------------------------------------------------

(clamiga::%jit-set-active *bo-prior-jit*)

(format t "BENCH-TOTAL ms=~D fails=~D~%" *bo-total-ms* *bo-fails*)
(format t "done.~%")
