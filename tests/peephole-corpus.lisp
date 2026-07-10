; Peephole differential corpus (spec 1.8).
;
; Run twice by tests/test_peephole_diff.sh — once with CLAMIGA_FORCE_SPEED=0
; (peephole off) and once with CLAMIGA_FORCE_SPEED=3 (peephole on for every
; compile) — and the printed output must be byte-identical.  Every form
; below exercises a rewrite pattern or a guard the pass must respect:
; store-then-reload chains, discarded pure values, (not ...) branch fusion,
; jump threading through nested conditionals, dead code, NLX landing-pad
; relocation (tagbody/go, catch/throw, unwind-protect, block/return-from),
; multiple-values state, closures, and type errors from discarded values.
;
; Results print via a single reporting function so the output is stable and
; machine-diffable.

(defvar *r* nil)
(defun report (name value)
  (push (list name value) *r*)
  (format t "~A = ~S~%" name value))

;; setq chains: STORE;POP;LOAD sites
(defun c-setq-chain (x)
  (setq x (+ x 1))
  (setq x (* x 2))
  (setq x (- x 3))
  x)
(report "setq-chain" (c-setq-chain 10))

;; discarded pure values in progn bodies
(defun c-discard (x)
  1 :k nil t x
  (progn 2 x)
  (+ x 1))
(report "discard" (c-discard 41))

;; (not ...) under if/when/unless: NOT;Jcc fusion both senses
(defun c-not-if (x) (if (not (< x 10)) :big :small))
(report "not-if-a" (c-not-if 5))
(report "not-if-b" (c-not-if 50))
(defun c-not-count (xs)
  (let ((n 0))
    (dolist (x xs)
      (unless (not (numberp x)) (setq n (+ n 1)))
      (when (not (symbolp x)) (setq n (+ n 10))))
    n))
(report "not-count" (c-not-count '(1 a 2.5 b "s")))

;; nested conditionals: jump-to-jump threading + dead branches
(defun c-nest (x)
  (if (< x 0)
      (if (< x -10) :low-low :low)
      (if (> x 10) (if (> x 100) :high-high :high) :mid)))
(report "nest" (list (c-nest -50) (c-nest -5) (c-nest 5)
                     (c-nest 50) (c-nest 500)))

;; loops: backward jumps must stay correct across deletions
(defun c-loop-sum (n)
  (let ((s 0))
    (dotimes (i n) (setq s (+ s i)))
    s))
(report "loop-sum" (c-loop-sum 1000))
(defun c-shuffle (n)
  (let ((a 1) (b 2) (c 3) (tmp 0))
    (dotimes (r n)
      (setq tmp a) (setq a b) (setq b c) (setq c tmp))
    (list a b c)))
(report "shuffle" (c-shuffle 999))

;; tagbody/go: NLX landing pads + local jumps
(defun c-tagbody (n)
  (let ((s 0) (i 0))
    (tagbody
     top (when (>= i n) (go done))
         (setq s (+ s i))
         (setq i (+ i 1))
         (go top)
     done)
    s))
(report "tagbody" (c-tagbody 100))

;; catch/throw + unwind-protect: CATCH/UWPROT landing pads
(defun c-catch (x)
  (catch 'tag
    (when x (throw 'tag :thrown))
    :fell))
(report "catch" (list (c-catch t) (c-catch nil)))
(defvar *cleanups* 0)
(defun c-uwprot (x)
  (catch 'out
    (unwind-protect
        (progn (when x (throw 'out :out)) :body)
      (setq *cleanups* (+ *cleanups* 1)))))
(report "uwprot" (list (c-uwprot t) (c-uwprot nil) *cleanups*))

;; block/return-from
(defun c-block (x)
  (block b
    (dotimes (i 10)
      (when (= i x) (return-from b (* i 100))))
    :not-found))
(report "block" (list (c-block 3) (c-block 99)))

;; multiple values: discarded constants near values forms must not disturb
;; the mv state an observer sees
(defun c-mv-pass ()
  (progn 42 (values 1 2 3)))
(report "mv-pass" (multiple-value-list (c-mv-pass)))
(defun c-mv-single ()
  (values-list '(7 8 9))
  :single)
(report "mv-single" (multiple-value-list (c-mv-single)))
(defun c-mv-bind (x)
  (multiple-value-bind (q r) (floor x 7)
    (list q r)))
(report "mv-bind" (c-mv-bind 100))

;; closures: OP_CLOSURE capture descriptors survive re-encode
(defun c-closures (n)
  (let ((acc nil))
    (dotimes (i n)
      (push (let ((j i)) (lambda () (* j j))) acc))
    (let ((s 0))
      (dolist (f acc) (setq s (+ s (funcall f))))
      s)))
(report "closures" (c-closures 10))

;; ANSI: a discarded (car 5) still signals at any speed
(defun c-discard-car (x)
  (progn (car x) :ok))
(report "car-signal"
        (handler-case (c-discard-car 5)
          (type-error () :signaled)))
(report "car-ok" (c-discard-car '(1 2)))

;; recursion through optimized code
(defun c-fib (n)
  (if (< n 2) n (+ (c-fib (- n 1)) (c-fib (- n 2)))))
(report "fib" (c-fib 15))

;; final checksum over everything reported, so a silent mid-corpus
;; divergence still flips the last line
(format t "CHECKSUM = ~S~%" (sxhash (format nil "~S" *r*)))
(format t "CORPUS-DONE~%")
