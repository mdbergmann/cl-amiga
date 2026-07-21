; CL-Amiga Test Suite — runs in batch mode on AmigaOS
; Multi-line expressions supported (paren-balanced accumulation)

; --- Test infrastructure ---
(setq *pass-count* 0)
(setq *fail-count* 0)
; CHECK is a macro so we can catch errors signaled while evaluating
; the ACTUAL expression — a function would never see those because the
; error short-circuits the argument evaluation before the call.  An
; uncaught error is reported as FAIL with the condition text.
(defmacro check (name expected actual)
  (let ((e (gensym "EXPECTED"))
        (a (gensym "ACTUAL"))
        (c (gensym "COND")))
    `(handler-case
         (let ((,e ,expected)
               (,a ,actual))
           (if (equal ,e ,a)
               (progn (setq *pass-count* (+ *pass-count* 1))
                      (format t "PASS: ~A~%" ,name))
               (progn (setq *fail-count* (+ *fail-count* 1))
                      (format t "FAIL: ~A - expected ~S got ~S~%"
                              ,name ,e ,a))))
       (error (,c)
         (setq *fail-count* (+ *fail-count* 1))
         (format t "FAIL: ~A - signaled error: ~A~%" ,name ,c)))))

(format t "~%=== CL-Amiga Test Suite ===~%~%")

; --- Arithmetic ---
(check "add two" 3 (+ 1 2))
(check "add many" 15 (+ 1 2 3 4 5))
(check "add zero args" 0 (+))
(check "sub two" 7 (- 10 3))
(check "sub many" 50 (- 100 30 20))
(check "sub negate" -5 (- 5))
(check "mul two" 20 (* 4 5))
(check "mul many" 24 (* 2 3 4))
(check "mul zero args" 1 (*))
(check "div" 5 (/ 10 2))
(check "div many" 2 (/ 100 10 5))
(check "mod" 1 (mod 10 3))
(check "mod even" 0 (mod 10 2))
(check "1+" 42 (1+ 41))
(check "1-" 42 (1- 43))

; --- Comparison ---
(check "= equal" t (= 5 5))
(check "= not equal" nil (= 1 2))
(check "= many" t (= 3 3 3))
(check "< true" t (< 1 2))
(check "< false" nil (< 2 1))
(check "> true" t (> 3 2))
(check "> false" nil (> 1 2))
(check "<= equal" t (<= 3 3))
(check "<= less" t (<= 2 3))
(check ">= equal" t (>= 3 3))
(check ">= greater" t (>= 5 3))

; --- Numeric predicates ---
(check "zerop zero" t (zerop 0))
(check "zerop nonzero" nil (zerop 1))
(check "plusp positive" t (plusp 5))
(check "plusp negative" nil (plusp -1))
(check "minusp negative" t (minusp -3))
(check "minusp positive" nil (minusp 3))
(check "abs positive" 42 (abs 42))
(check "abs negative" 42 (abs -42))
(check "max" 5 (max 1 5 3))
(check "min" 1 (min 1 5 3))

; --- Type predicates ---
(check "null nil" t (null nil))
(check "null non-nil" nil (null 42))
(check "numberp fixnum" t (numberp 42))
(check "numberp symbol" nil (numberp 'foo))
(check "symbolp" t (symbolp 'foo))
(check "symbolp nil" t (symbolp nil))
(check "stringp" t (stringp "hello"))
(check "stringp num" nil (stringp 42))
(check "consp" t (consp '(1 2)))
(check "consp atom" nil (consp 42))
(check "atom number" t (atom 42))
(check "atom cons" nil (atom '(1 2)))
(check "listp cons" t (listp '(1 2)))
(check "listp nil" t (listp nil))
(check "not nil" t (not nil))
(check "not t" nil (not t))
(check "integerp" t (integerp 42))

; --- Equality ---
(check "eq same symbol" t (eq 'a 'a))
(check "eq diff symbol" nil (eq 'a 'b))
(check "eql fixnum" t (eql 42 42))
(check "equal list" t (equal '(1 2 3) '(1 2 3)))
(check "equal string" t (equal "hello" "hello"))
(check "equal nested" t (equal '(1 (2 3)) '(1 (2 3))))

; --- List operations ---
(check "car" 1 (car '(1 2 3)))
(check "cdr" '(2 3) (cdr '(1 2 3)))
(check "first" 1 (first '(1 2 3)))
(check "rest" '(2 3) (rest '(1 2 3)))
(check "cons" '(1 2 3) (cons 1 '(2 3)))
(check "list" '(1 2 3) (list 1 2 3))
(check "length" 3 (length '(1 2 3)))
(check "length nil" 0 (length nil))
(check "append" '(1 2 3 4) (append '(1 2) '(3 4)))
(check "reverse" '(3 2 1) (reverse '(1 2 3)))
(check "nth 0" 10 (nth 0 '(10 20 30)))
(check "nth 2" 30 (nth 2 '(10 20 30)))

; --- String operations ---
(check "string length" 5 (length "hello"))
(check "string equal" t (equal "foo" "foo"))
(check "string not equal" nil (equal "foo" "bar"))

; --- Control flow ---
(check "if true" 1 (if t 1 2))
(check "if false" 2 (if nil 1 2))
(check "if no else" nil (if nil 1))
(check "progn" 3 (progn 1 2 3))
(check "let simple" 10 (let ((x 10)) x))
(check "let body" 8 (let ((a 5) (b 3)) (+ a b)))
(check "let* sequential" 2 (let* ((x 1) (y (+ x 1))) y))

; --- Defun and recursion ---
(defun double (x) (* x 2))
(check "defun simple" 42 (double 21))
(defun factorial (n) (if (<= n 1) 1 (* n (factorial (- n 1)))))
(check "factorial 1" 1 (factorial 1))
(check "factorial 5" 120 (factorial 5))
(check "factorial 10" 3628800 (factorial 10))
(defun fib (n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))
(check "fibonacci 0" 0 (fib 0))
(check "fibonacci 1" 1 (fib 1))
(check "fibonacci 10" 55 (fib 10))

; --- Lambda ---
(check "lambda call" 25 ((lambda (x) (* x x)) 5))
(check "funcall builtin" 7 (funcall #'+ 3 4))
; Regression: a literal NIL as a non-final lambda body form must not truncate
; the body (compile_body conflated empty-body with a NIL tail form + pending
; PROGN_ITER frame, dropping later forms).  Exposed by serapeum define-case-macro.
(check "lambda leading nil body" 42 (funcall (lambda () nil 42)))
(check "lambda leading nil after decl" 11
       (funcall (lambda (x) (declare (ignorable x)) nil (+ x 1)) 10))
(check "lambda multiple leading nils" 20 (funcall (lambda (x) nil nil (* x 2)) 10))
(check "lambda lone nil body" nil (funcall (lambda () nil)))

; --- Setq ---
(setq *my-var* 100)
(check "setq global" 100 *my-var*)
(setq *my-var* 200)
(check "setq update" 200 *my-var*)

; --- Keywords ---
(check "keyword eq" t (eq :foo :foo))
(check "keyword symbolp" t (symbolp :bar))

; --- Mapcar (built-in functions only) ---
(check "mapcar 1+" '(2 3 4) (mapcar #'1+ '(1 2 3)))
(check "mapcar -" '(-1 -2 -3) (mapcar #'- '(1 2 3)))

; --- Quote ---
(check "quote symbol" 'foo (quote foo))
(check "quote list" '(1 2 3) (quote (1 2 3)))

; --- Apply ---
(check "apply +" 6 (apply #'+ '(1 2 3)))
(check "apply list" '(1 2 3) (apply #'list '(1 2 3)))

; APPLY must spread more than the old 64-arg cap (CALL-ARGUMENTS-LIMIT).
; serapeum's (apply #'string+ <list>) tripped "too many arguments (>64)".
; Counts kept <= 255 so the JIT apply trampoline (limited by its stub frame)
; handles them too.
(defun ar-amiga (&rest xs) (length xs))
(check "apply rest 100" 100 (apply #'ar-amiga (loop for i from 1 to 100 collect i)))
(check "apply rest 255" 255 (apply #'ar-amiga (loop for i from 1 to 255 collect i)))
(defun arr-amiga (a b &rest xs) (list a b (length xs)))
(check "apply required+rest 200" '(1 2 198)
  (apply #'arr-amiga (loop for i from 1 to 200 collect i)))
(defun asum-amiga (&rest xs) (reduce #'+ xs :initial-value 0))
(check "apply rest ordered sum" 5050
  (apply #'asum-amiga (loop for i from 1 to 100 collect i)))
(check "apply builtin over 64" 200
  (length (apply #'list (loop for i from 1 to 200 collect i))))
(check "call-arguments-limit >= lambda-parameters-limit" t
  (and (>= call-arguments-limit lambda-parameters-limit)
       (plusp call-arguments-limit)))

; --- Macros ---
(defmacro always-42 () 42)
(check "macro constant" 42 (always-42))
(defmacro inc (x) (list '+ x 1))
(check "macro inc 5" 6 (inc 5))
(check "macro inc 99" 100 (inc 99))
(defmacro my-when (test &rest body) (list 'if test (cons 'progn body)))
(check "macro when true" 42 (my-when t 1 2 42))
(check "macro when false" nil (my-when nil 1 2 42))
(defmacro my-unless (test &rest body) (list 'if test nil (cons 'progn body)))
(check "macro unless nil" 99 (my-unless nil 1 2 99))
(check "macro unless t" nil (my-unless t 1 2 99))
(defmacro quote-it (x) (list 'quote x))
(check "macro identity" '(+ 1 2) (quote-it (+ 1 2)))

; --- Closures with captured upvalues ---
(check "closure let capture" 15 (let ((x 10)) ((lambda (y) (+ x y)) 5)))
(check "closure no-arg capture" 100 (let ((a 100)) ((lambda () a))))
(defun make-adder (n) (lambda (x) (+ n x)))
(check "make-adder 10+5" 15 ((make-adder 10) 5))
(check "make-adder 100+42" 142 ((make-adder 100) 42))
(check "closure multi-capture" 6 (let ((a 1) (b 2) (c 3)) ((lambda () (+ a b c)))))
(check "closure with param" 60 (let ((x 10) (y 20)) ((lambda (z) (+ x y z)) 30)))
(check "closure nested" 15 (let ((x 10)) ((lambda () ((lambda () (+ x 5)))))))
(defun make-pair (x) (list (lambda () x) (lambda () (+ x 1))))
(check "closure shared-a" 10 (let ((p (make-pair 10))) ((car p))))
(check "closure shared-b" 11 (let ((p (make-pair 10))) ((car (cdr p)))))

; --- Heap-boxed cells (mutable closure capture) ---
(let ((n 0)) (defun cell-inc () (setq n (+ n 1)) n))
(check "cell counter 1" 1 (cell-inc))
(check "cell counter 2" 2 (cell-inc))
(check "cell counter 3" 3 (cell-inc))

(let ((x 10)) (defun cg () x) (defun cs (v) (setq x v)))
(check "cell getter init" 10 (cg))
(cs 42)
(check "cell getter after set" 42 (cg))

(defun make-acc (init) (lambda (x) (setq init (+ init x)) init))
(defparameter *ta* (make-acc 0))
(check "cell accum 5" 5 (funcall *ta* 5))
(check "cell accum +3" 8 (funcall *ta* 3))
(check "cell accum +10" 18 (funcall *ta* 10))

(defun mk-ctr () (let ((n 0)) (lambda () (setq n (+ n 1)) n)))
(defparameter *tc1* (mk-ctr))
(defparameter *tc2* (mk-ctr))
(check "cell indep c1-1" 1 (funcall *tc1*))
(check "cell indep c1-2" 2 (funcall *tc1*))
(check "cell indep c2-1" 1 (funcall *tc2*))
(check "cell indep c1-3" 3 (funcall *tc1*))
(check "cell indep c2-2" 2 (funcall *tc2*))

(check "cell setq before capture" 42 (let ((x 0)) (setq x 42) ((lambda () x))))

(let ((x 0)) (defun cell-outer () (lambda () (setq x (+ x 1)) x)))
(check "cell nested 1" 1 (funcall (cell-outer)))
(check "cell nested 2" 2 (funcall (cell-outer)))

(check "cell flet mutation" 3
  (let ((n 0))
    (flet ((bump () (setq n (+ n 1))))
      (bump) (bump) (bump) n)))

(check "cell labels shared" 3
  (let ((n 0))
    (labels ((my-inc () (setq n (+ n 1)))
             (my-peek () n))
      (my-inc) (my-inc) (my-inc) (my-peek))))

(check "cell readonly" 42 (let ((x 42)) ((lambda () x))))

; --- Type-of ---
(check "type-of fixnum" 'fixnum (type-of 42))
(check "type-of string" 'string (type-of "hello"))
(check "type-of cons" 'cons (type-of '(1 2)))

; --- AND / OR / COND ---
(check "and empty" t (and))
(check "and single" 42 (and 42))
(check "and all true" 3 (and 1 2 3))
(check "and short-circuit" nil (and 1 nil 2))
(check "and first nil" nil (and nil 1))
(check "or empty" nil (or))
(check "or single" 42 (or 42))
(check "or first true" 1 (or 1 2))
(check "or skip nil" 2 (or nil 2 3))
(check "or all nil" nil (or nil nil nil))
(check "cond t-clause" 42 (cond (t 42)))
(check "cond skip nil" 2 (cond (nil 1) (t 2)))
(check "cond no match" nil (cond (nil 1)))
(check "cond multi-body" 3 (cond (t 1 2 3)))
(check "cond expr-test" 20 (cond ((= 1 2) 10) ((= 1 1) 20)))
(check "cond empty" nil (cond))

; --- WHEN / UNLESS (bootstrap macros) ---
(check "when true" 42 (when t 1 2 42))
(check "when false" nil (when nil 1 2 42))
(check "unless nil" 99 (unless nil 1 2 99))
(check "unless t" nil (unless t 1 2 99))

; --- Mapcar with lambda/closure ---
(check "mapcar lambda" '(1 4 9) (mapcar (lambda (x) (* x x)) '(1 2 3)))
(defun make-multiplier (n) (lambda (x) (* n x)))
(check "mapcar closure" '(3 6 9) (mapcar (make-multiplier 3) '(1 2 3)))

; --- Funcall with lambda/closure ---
(check "funcall lambda" 30 (funcall (lambda (a b) (+ a b)) 10 20))
(check "funcall closure" 15 (funcall (make-adder 10) 5))

; --- Apply with lambda/closure ---
(check "apply lambda" 7 (apply (lambda (a b) (+ a b)) '(3 4)))
(check "apply closure" 57 (apply (make-adder 50) '(7)))

; --- DOLIST ---
(setq *dl-result* nil)
(dolist (x '(1 2 3)) (setq *dl-result* (cons x *dl-result*)))
(check "dolist collect" '(3 2 1) *dl-result*)
(setq *dl-count* 0)
(dolist (x nil) (setq *dl-count* (+ *dl-count* 1)))
(check "dolist empty" 0 *dl-count*)
(check "dolist result" 42 (dolist (x '(1 2 3) 42)))
(check "dolist nil result" nil (dolist (x '(1 2 3))))
(check "dolist var nil in result" nil (dolist (x '(1 2 3) x)))

; --- DOTIMES ---
(setq *dt-sum* 0)
(dotimes (i 5) (setq *dt-sum* (+ *dt-sum* i)))
(check "dotimes sum 0-4" 10 *dt-sum*)
(setq *dt-count* 0)
(dotimes (i 0) (setq *dt-count* (+ *dt-count* 1)))
(check "dotimes zero" 0 *dt-count*)
(check "dotimes result" 99 (dotimes (i 3 99)))
(check "dotimes nil result" nil (dotimes (i 3)))
(check "dotimes var in result" 5 (dotimes (i 5 i)))

; --- DO ---
(check "do countdown" 42 (do ((i 10 (- i 1))) ((= i 0) 42)))
(check "do multi-var" 10 (do ((i 0 (+ i 1)) (sum 0 (+ sum i))) ((= i 5) sum)))
(check "do no-step" 100 (do ((x 100) (i 0 (+ i 1))) ((= i 3) x)))
(setq *do-body* 0)
(do ((i 0 (+ i 1))) ((= i 3)) (setq *do-body* (+ *do-body* 1)))
(check "do body exec" 3 *do-body*)
(check "do multi-result" 30 (do ((i 0 (+ i 1))) ((= i 1) 10 20 30)))
(check "do nil result" nil (do ((i 0 (+ i 1))) ((= i 1))))

; --- Loop body + conditionals + setf-expansion macros (compiler GC-safety) ---
; These exercise compile paths that held unprotected C-local cursors across
; allocating compile_expr / VM-macroexpand calls: a moving GC during
; compilation re-emitted a (when ...) loop body (doubled count), faulted on a
; stale binding cursor ("CDR not a LIST"), or dropped an INCF/DECF LET* binding
; (store gensym compiled as a global → "Unbound variable: NEW<n>").  On Amiga
; the moving GC fires under genuine heap pressure; here we also assert these
; common forms simply compile and run correctly.
(check "when in dotimes runs once"   50  (let ((n 0)) (dotimes (i 50) (when (= 1 1) (setf n (+ n 1)))) n))
(check "unless in dotimes runs once" 50  (let ((n 0)) (dotimes (i 50) (unless (= 1 2) (setf n (+ n 1)))) n))
(check "when in dolist runs once"    5   (let ((n 0)) (dolist (x '(1 2 3 4 5)) (when t (setf n (+ n 1)))) n))
(check "setf body in dotimes"        50  (let ((n 0)) (dotimes (i 50) (setf n (+ n 1))) n))
(check "dotimes non-last, trail var" 0   (let ((n 0)) (dotimes (i 5) 1) n))
(check "incf in dotimes"             50  (let ((n 0)) (dotimes (i 50) (incf n)) n))
(check "incf in when-in-dotimes"     25  (let ((n 0)) (dotimes (i 50) (when (evenp i) (incf n))) n))
(check "incf in dolist"              3   (let ((n 0)) (dolist (x '(1 2 3)) (incf n)) n))
(check "incf in do"                  3   (let ((n 0)) (do ((i 0 (1+ i))) ((= i 3) n) (incf n))))
(check "decf in dotimes"             45  (let ((n 50)) (dotimes (i 5) (decf n)) n))
(check "two sequential incf"         2   (let ((n 0)) (incf n) (incf n) n))
(check "incf in nested dotimes"      6   (let ((n 0)) (dotimes (i 3) (dotimes (j 2) (incf n))) n))
;; Regression: compile_if read then_form/else_form BEFORE compiling the test.
;; compile_expr(test) can compact (moving GC); only `form` was protected, so the
;; then/else C locals went stale and the returned then-branch was emitted as a
;; bogus interior-pointer constant, corrupting the constant pool ("malformed
;; call form" / "CDR not a list").  Exercise an IF/WHEN/UNLESS with an allocating
;; test inside a LET/DOTIMES so the then/else branches stay correct.
(check "if allocating-test then/else" :yes
  (if (equalp (copy-seq (list 1 2 3)) (list 1 2 3)) :yes :no))
(check "if alloc-test branches in dotimes" 200
  (let ((n 0))
    (dotimes (i 200)
      (if (equalp (let ((a (copy-seq (list 0 0 0 0))))
                    (map-into a (lambda (x) (list x x)) (list 1 2 3 4)))
                  (list (list 1 1) (list 2 2) (list 3 3) (list 4 4)))
          (incf n)
          (return)))
    n))
;; CLHS 6.1.1: a var-spec may be a bare symbol (bound to NIL, no step).
;; Regression: the compiler used to CAR the spec assuming a list, so a
;; bare symbol broke compilation (puri/drakma -> chipi load failure).
(check "do bare-var nil" nil (do* ((i 0 (1+ i)) untouched) ((= i 2) untouched)))
(check "do bare-var set" 10 (do ((i 0 (1+ i)) x) ((= i 2) x) (setq x (* i 10))))
(check "do* bare-var mix" '((2 1 0) 5 3)
  (do* ((a) (b 5) (c 0 (1+ c))) ((= c 3) (list a b c)) (setq a (cons c a))))
(check "do* bare-var only" 7 (do* (x) ((eq x :done) 7) (setq x :done)))

; --- Allocator: sweep-forever escape (moving GC bounded under fragmentation) ---
; Companion to the host C regression tests/test_alloc_sweep_escape.c, which
; precisely reproduces the "sweep-forever" GC pathology: once the bump pointer
; was exhausted, a fragmented free list could defeat the bounded free-list probe
; so that cl_alloc ran a full mark-and-sweep on nearly every allocation without
; ever compacting (a sweep never resets the bump pointer).  Cold-compiling a
; large system spent minutes in the GC, with collection counts exploding at some
; heap sizes (e.g. 1293 collections where a nearby size needed ~15).  Fix: after
; GC_SWEEPS_BEFORE_COMPACT bump-exhausted sweeps without progress (and when the
; heap is not near-full, judged from the FRESH post-sweep live-set size),
; compact to reset the bump pointer and escape the loop.
;
; Reproducing the exact free-list micro-structure needs C-level bump-pointer
; introspection, so the precise guard lives in the host C test.  Here we
; exercise the SAME shared allocator slow path + moving GC on the real Amiga
; target at its 8M test heap under heavy fragmented allocation, and assert that
; (a) the retained live data survives every compaction uncorrupted and (b) the
; collection count stays well below the allocation count (a gross regression to
; one-GC-per-allocation would blow this bound).
(check "alloc sweep-forever escape: live intact + GC bounded" t
  (let ((live nil) (dead nil))
    ;; Fragment + push the bump toward the top of the 8M heap.  Retained 400-elem
    ;; vectors stay live (~70% of heap); the 100-elem ones are dropped, leaving
    ;; isolated holes a sweep cannot coalesce.
    (dotimes (i 3500)
      (push (make-array 400) live)
      (push (make-array 100) dead))
    (setf dead nil)
    (let ((g0 (clamiga::%get-gc-count)))
      ;; Flood: short-lived medium vectors, each discarded immediately, forcing
      ;; the bump-exhausted slow path and repeated moving compactions.
      (dotimes (i 3000) (make-array 1000))
      (let ((collections (- (clamiga::%get-gc-count) g0)))
        ;; Live data uncorrupted by all the moving-GC compactions, AND the GC
        ;; stayed bounded (nowhere near one collection per allocation).
        (and (= (length live) 3500)
             (vectorp (car live))
             (= (length (car live)) 400)
             (< collections 1000))))))

; --- Allocator: growable GC mark stack (wide object graphs) ---
; Companion to the host C regression tests/test_gc_markstack.c.  The GC mark
; stack starts at 4096 entries and must GROW when a single object has more
; unmarked children (here: an 8000-slot vector of fresh conses).  Before the
; growth fix the overflow fell back — silently — to a full-arena re-scan per
; ~4096 recovered children, turning GC cycles on large live heaps quadratic
; (measured 49 s vs 35 ms per mark-sweep on a 210MB host heap; found via
; "loading from Sly is 60% slower" 2026-07-05).  Asserts: the graph survives
; the GC intact, the stack grew, and the quadratic re-scan fallback (third
; stat) never ran.  Stats are process-monotonic, so re-scans staying 0 also
; covers every OTHER GC this suite has triggered up to this point.
(check "growable mark stack: wide vector marks intact, no re-scan" t
  (let ((wide (make-array 8000)))
    (dotimes (i 8000) (setf (aref wide i) (cons i (* i 2))))
    (ext:gc)
    (let ((bad 0)
          (stats (ext:%gc-mark-stats)))
      (dotimes (i 8000)
        (let ((c (aref wide i)))
          (unless (and (consp c) (eql (car c) i) (eql (cdr c) (* i 2)))
            (incf bad))))
      (and (= bad 0)
           (>= (first stats) 8192)     ; capacity grew past the wide push
           (plusp (second stats))      ; via at least one grow operation
           (zerop (third stats))))))   ; quadratic re-scan fallback never ran

; --- Quasiquote ---
(check "qq atom" 42 `42)
(check "qq symbol" 'foo `foo)
(check "qq simple list" '(1 2 3) `(1 2 3))
(check "qq unquote" '(a 3 c) `(a ,(+ 1 2) c))
(check "qq unquote var" '(val 10) (let ((x 10)) `(val ,x)))
(check "qq splice" '(a 1 2 3 b) `(a ,@'(1 2 3) b))
(check "qq splice concat" '(1 2 3 4) `(,@'(1 2) ,@'(3 4)))
(check "qq nested list" '(a (b 3)) `(a (b ,(+ 1 2))))
(check "qq dotted" '(a . 3) `(a . ,(+ 1 2)))
(defmacro qq-inc (x) `(+ ,x 1))
(check "qq macro inc" 6 (qq-inc 5))
(defmacro qq-progn (&rest body) `(progn ,@body))
(check "qq macro splice" 42 (qq-progn 1 2 42))

; --- Gensym ---
(check "gensym symbolp" t (symbolp (gensym)))
(check "gensym unique" nil (eq (gensym) (gensym)))
(check "gensym prefix" t (symbolp (gensym "MY-")))

; --- Boot functions ---
(check "cadr" 2 (cadr '(1 2 3)))
(check "caar" 'a (caar '((a b) c)))
(check "cddr" '(3) (cddr '(1 2 3)))
(check "caddr" 3 (caddr '(1 2 3)))
(check "identity" 42 (identity 42))
(check "endp nil" t (endp nil))
(check "endp list" nil (endp '(1)))

; --- Format ---
(check "format nil" nil (format t ""))
; ~A/~S used a fixed 512-byte buffer: long strings truncated at 511 chars
(check "format ~A long string" 2000
       (length (format nil "~A" (make-string 2000 :initial-element #\x))))
(check "format ~S long string" 2002
       (length (format nil "~S" (make-string 2000 :initial-element #\x))))
(check "format padded long string" 2100
       (length (format nil "~2100A" (make-string 2000 :initial-element #\x))))
; ~D used a fixed 128-byte buffer: bignums past ~127 digits truncated
(check "format ~D big bignum" 201 (length (format nil "~D" (expt 10 200))))
(check "format ~:D big bignum" 267 (length (format nil "~:D" (expt 10 200))))

; --- Phase 4: return / return-from ---
(check "block return" 42 (block nil 1 (return 42) 3))
(check "block return-from" 42 (block foo (return-from foo 42)))
(check "block normal" 3 (block nil 1 2 3))
(check "return in do" 42 (do ((i 0 (1+ i))) ((= i 10) 99) (if (= i 5) (return 42))))
(check "return in dolist" 4 (dolist (x '(1 3 4 7)) (if (= 0 (mod x 2)) (return x))))
(check "return in dotimes" 7 (dotimes (i 100) (if (= i 7) (return i))))

; --- Phase 4: prog1 / prog2 ---
(check "prog1" 1 (prog1 1 2 3))
(check "prog2" 2 (prog2 1 2 3))

; --- Phase 4: case / ecase ---
(check "case match" 'b (case 2 (1 'a) (2 'b) (3 'c)))
(check "case multi-key" 'other (case 5 ((1 2) 'low) ((3 4) 'high) (t 'other)))
(check "case no match" nil (case 99 (1 'a)))
(check "ecase match" 'b (ecase 2 (1 'a) (2 'b)))
; ECASE/ETYPECASE/CCASE/CTYPECASE must signal TYPE-ERROR (CLHS 5.3), not SIMPLE-ERROR
(check "ecase no-match type-error" 'te
       (handler-case (ecase 5 (1 'a) (2 'b)) (type-error () 'te) (error () 'wrong)))
(check "ecase type-error datum" 5
       (handler-case (ecase 5 (1 'a)) (type-error (e) (type-error-datum e))))
(check "etypecase no-match type-error" 'te
       (handler-case (etypecase 5 (string 's)) (type-error () 'te) (error () 'wrong)))
(check "ccase no-match type-error" 'te
       (handler-case (let ((x 5)) (ccase x ((1) 'a))) (type-error () 'te) (error () 'wrong)))
(check "ctypecase no-match type-error" 'te
       (handler-case (let ((x 5)) (ctypecase x (string 's))) (type-error () 'te) (error () 'wrong)))
; continuable CCASE/CTYPECASE: STORE-VALUE restart stores into the place and retries (CLHS 5.3)
(check "ccase direct match" 'b (let ((x 'b)) (ccase x (a 'A) (b 'B))))
(check "ccase store-value retry" 'B
       (let ((x 'zzz))
         (handler-bind ((type-error (lambda (c) (declare (ignore c)) (store-value 'b))))
           (ccase x (a 'A) (b 'B)))))
(check "ccase store-value mutates place" 'a
       (let ((x 'zzz))
         (handler-bind ((type-error (lambda (c) (declare (ignore c)) (store-value 'a))))
           (ccase x (a 'A) (b 'B)))
         x))
(check "ccase expected-type" '(member 1 2)
       (handler-case (let ((x 9)) (ccase x (1 'a) (2 'b)))
         (type-error (c) (type-error-expected-type c))))
(check "ctypecase store-value retry" 'i
       (let ((x 'sym))
         (handler-bind ((type-error (lambda (c) (declare (ignore c)) (store-value 42))))
           (ctypecase x (string 's) (integer 'i)))))
; store-value / use-value convenience functions (CLHS 9.1)
(check "store-value function" 42 (restart-case (store-value 7) (store-value (v) (* v 6))))
(check "use-value function" 42 (restart-case (use-value 21) (use-value (v) (* v 2))))
(check "store-value no restart returns nil" nil (store-value 1))
(check "typecase int" 'num (typecase 42 (integer 'num) (string 'str)))
(check "typecase str" 'str (typecase "hi" (integer 'num) (string 'str)))

; --- Phase 4: flet / labels ---
(check "flet basic" 4 (flet ((f (x) (+ x 1))) (f 3)))
(check "flet closure" 15 (let ((x 10)) (flet ((f (y) (+ x y))) (f 5))))
(check "labels fact" 120 (labels ((fact (n) (if (<= n 1) 1 (* n (fact (- n 1)))))) (fact 5)))
(check "labels mutual" t (labels ((even2 (n) (if (= n 0) t (odd2 (- n 1)))) (odd2 (n) (if (= n 0) nil (even2 (- n 1))))) (even2 4)))
; Regression (chipz inflate.lisp): #'(setf name) must resolve to the setter
; symbol, not FLOAD the raw (SETF NAME) cons (which corrupted the const pool).
(defun (setf sl-acc) (v x) (setf (car x) v) v)
(check "sharp-quote (setf name)" 42
       (let ((cell (list 0))) (funcall #'(setf sl-acc) 42 cell) (car cell)))
; Regression (local-time/local-time-duration): (setf ACCESSOR) functions on
; same-named accessors in DIFFERENT packages must stay distinct (hidden store
; symbol is now %SETF-<package>::<name>, not the colliding %SETF-<name>).
(defpackage :setf-pkg-a (:use :cl))
(defpackage :setf-pkg-b (:use :cl))
(defun (setf setf-pkg-a::acc) (v x) (setf (car x) (list :a v)) v)
(defun (setf setf-pkg-b::acc) (v x) (setf (car x) (list :b v)) v)
(check "(setf accessor) distinct per package" '((:a 11) (:b 22))
       (let ((c1 (list 0)) (c2 (list 0)))
         (funcall #'(setf setf-pkg-a::acc) 11 c1)
         (funcall #'(setf setf-pkg-b::acc) 22 c2)
         (list (car c1) (car c2))))
; Regression (chipz inflate.lisp 37-fn state machine): a LABELS with >32
; functions must bind+resolve #' to a function past slot 32 (CL_MAX_LOCAL_FUNS
; was 32, now 64).  fn35 is the 36th; #'fn35 must call it, not a global.
(check "labels >32 fns, ref late one" 35
       (labels ((fn0 () 0) (fn1 () 1) (fn2 () 2) (fn3 () 3) (fn4 () 4)
                (fn5 () 5) (fn6 () 6) (fn7 () 7) (fn8 () 8) (fn9 () 9)
                (fn10 () 10) (fn11 () 11) (fn12 () 12) (fn13 () 13) (fn14 () 14)
                (fn15 () 15) (fn16 () 16) (fn17 () 17) (fn18 () 18) (fn19 () 19)
                (fn20 () 20) (fn21 () 21) (fn22 () 22) (fn23 () 23) (fn24 () 24)
                (fn25 () 25) (fn26 () 26) (fn27 () 27) (fn28 () 28) (fn29 () 29)
                (fn30 () 30) (fn31 () 31) (fn32 () 32) (fn33 () 33) (fn34 () 34)
                (fn35 () 35))
         (funcall #'fn35)))

; --- Phase 4: &optional / &key ---
(defun opt-test (a &optional b) (list a b))
(check "optional nil" '(1 nil) (opt-test 1))
(check "optional val" '(1 2) (opt-test 1 2))
(defun opt-def (a &optional (b 10)) (+ a b))
(check "optional default" 11 (opt-def 1))
(check "optional override" 3 (opt-def 1 2))
(defun key-test (&key x y) (list x y))
(check "key both" '(1 2) (key-test :x 1 :y 2))
(check "key none" '(nil nil) (key-test))
(check "key partial" '(nil 5) (key-test :y 5))
(defun key-def (&key (x 0) (y 10)) (+ x y))
(check "key default" 10 (key-def))
(check "key override" 15 (key-def :x 5))
; Regression: key param shadowing special var must see dynamic value as default
(defvar *key-special-test* 100)
(defun key-special-def (&key ((:path *key-special-test*) *key-special-test*))
  *key-special-test*)
(check "key default special" 100 (key-special-def))
(check "key override special" 77 (key-special-def :path 77))
(check "lambda optional" 15 ((lambda (a &optional (b 5)) (+ a b)) 10))

; --- Phase 4: &allow-other-keys ---
(defun aok-test (&key x &allow-other-keys) x)
(check "aok accepts unknown" 5 (aok-test :x 5 :z 99))
(check "aok no known key" nil (aok-test :z 99))
(check "caller allow-other-keys" '(1 nil) (key-test :x 1 :z 99 :allow-other-keys t))

; --- Phase 4: eval ---
(check "eval simple" 3 (eval '(+ 1 2)))
(check "eval complex" 42 (eval '(* 6 7)))
(check "eval quote" 'foo (eval ''foo))

; --- Phase 4: macroexpand-1 / macroexpand ---
(defmacro me-test (x) `(+ ,x 1))
(check "macroexpand-1 macro" '(+ 5 1) (macroexpand-1 '(me-test 5)))
(check "macroexpand-1 non-macro" '(+ 1 2) (macroexpand-1 '(+ 1 2)))
(defmacro me-wrap (x) `(me-test ,x))
(check "macroexpand nested" '(+ 5 1) (macroexpand '(me-wrap 5)))
(check "macroexpand non-macro" '(+ 1 2) (macroexpand '(+ 1 2)))

; --- Regression: a lexical LET shadows a SYMBOL-MACROLET / global symbol-macro ---
; (CLHS 3.1.2.1.1).  serapeum's `local` rebinds a symbol-macro's name as a fresh
; lexical variable and assigns through it; the symbol-macro must not leak in.
(check "let shadows symbol-macrolet" '(2 10)
  (let ((g 10)) (symbol-macrolet ((x g)) (let ((x nil)) (setq x 2) (list x g)))))
(check "let shadows sym-macro setf" 5
  (let ((g 10)) (symbol-macrolet ((x g)) (let ((x 5)) (setf g 99) x))))
(define-symbol-macro amiga-gsm-shadow 1)
(check "let shadows global sym-macro read" 5
  (let ((amiga-gsm-shadow 5)) amiga-gsm-shadow))
(check "let shadows global sym-macro setq" 9
  (let ((amiga-gsm-shadow 5)) (setq amiga-gsm-shadow 9) amiga-gsm-shadow))
(check "global sym-macro still active outside let" 1 amiga-gsm-shadow)

; --- Phase 4 Tier 2: tagbody/go ---
(check "tagbody basic" 2 (let ((x 0)) (tagbody (setq x 1) (setq x (+ x 1))) x))
(check "tagbody forward go" 0 (let ((x 0)) (tagbody (go end) (setq x 99) end) x))
(check "tagbody backward go" 5 (let ((x 0)) (tagbody loop (setq x (+ x 1)) (if (< x 5) (go loop))) x))
(check "tagbody fixnum tags" 42 (let ((x 0)) (tagbody (go 2) 1 (setq x 99) 2 (setq x 42)) x))
(check "tagbody go from if" 3 (let ((x 0)) (tagbody start (setq x (+ x 1)) (if (< x 3) (go start))) x))

; --- Regression: GO / RETURN-FROM through a macro-hidden closure ---
; A user macro that wraps its body in a closure (e.g. (tb-in-lambda ...) ->
; (funcall (lambda () ...))) hides the lambda from the compiler's
; NLX-detection scan.  The scan must macroexpand to see the closure and
; promote the enclosing tagbody/block to the NLX path, else GO/RETURN-FROM
; inside the closure can't reach its tag.  (Pervasive idiom in SLY.)
(defmacro tb-in-lambda (&body body) `(funcall (lambda () ,@body)))
(defmacro tb-just-progn (&body body) `(progn ,@body))
(check "go through macro closure" 3
  (let ((i 0)) (tagbody start (setq i (+ i 1)) (tb-in-lambda (if (< i 3) (go start)))) i))
(check "return-from through macro closure" 42
  (block blk (tb-in-lambda (return-from blk 42)) 99))
(check "return through macro closure anon" 7
  (block nil (tb-in-lambda (return 7)) 99))
(check "macro non-closure keeps local go" 4
  (let ((i 0)) (tagbody start (setq i (+ i 1)) (tb-just-progn (if (< i 4) (go start)))) i))

; --- Phase 4 Tier 2: catch/throw ---
(check "catch basic" 42 (catch 'done (throw 'done 42)))
(check "catch normal" 3 (catch 'done (+ 1 2)))
(defun throw-helper-t () (throw 'bail 99))
(check "catch across call" 99 (catch 'bail (+ 1 (throw-helper-t))))
(check "catch nested inner" 15 (catch 'outer (+ 10 (catch 'inner (throw 'inner 5)))))
(check "catch nested outer" 42 (catch 'outer (catch 'inner (throw 'outer 42))))
(check "throw no value" nil (catch 'done (throw 'done)))

; Regression: a local RETURN-FROM jumping over an intervening CATCH (or
; HANDLER-BIND) must not take the cheap local-jump path — that skips the
; OP_UNCATCH/OP_HANDLER_POP and strands the frame on the NLX stack on every
; NORMAL exit.  Looping past CL_MAX_NLX_FRAMES (2048) overflowed it; this is
; how handler-case's (block B (catch 'T (return-from B ...))) leaked one CATCH
; frame per call (cl+ssl reading an SSL stream byte-by-byte).
(defun rf-over-catch-t (n)
  (let ((acc 0))
    (dotimes (i n acc)
      (setq acc (block blk (catch 'tg (return-from blk 1)))))))
(check "return-from over catch no nlx leak" 1 (rf-over-catch-t 5000))
(check "handler-case normal path no nlx leak" 7
  (let ((acc 0)) (dotimes (i 5000 acc)
    (setq acc (handler-case (+ i 1) (error () -1)))) 7))
(check "handler-case mv through block" '(1 2 3)
  (multiple-value-list
    (dotimes (i 3000 (handler-case (values 1 2 3) (error () nil))))))

; handler-bind / catch in TAIL position must still pop their handler / NLX
; frame.  Previously the body's tail form emitted OP_TAILCALL, replacing the
; frame and skipping OP_HANDLER_POP / OP_UNCATCH, leaking one binding per call
; until the handler (64) / NLX (2048) stack overflowed.  These were the
; mechanism behind fiasco's "Handler stack overflow" after ~20 tests.
(defun hb-inner () 42)
(defun hb-tail () (handler-bind ((warning #'identity)) (hb-inner)))
(check "handler-bind tail no leak" :ok (progn (dotimes (i 5000) (hb-tail)) :ok))
(defun catch-tail () (catch 'tg (hb-inner)))
(check "catch tail no leak" :ok (progn (dotimes (i 5000) (catch-tail)) :ok))

; Regression (fiveam process-failure pattern): a handler-bind handler that
; invoke-restarts a with-simple-restart in its OWN dynamic extent must keep
; working for EVERY signal.  The handler's non-local exit unwinds past
; cl_signal_condition, so the CLHS 9.1.4 disabled-band must be restored from the
; NLX-frame snapshot, else the SECOND signal escapes uncaught (blocked fiveam:
; every test after the first failing check aborted the run).
(check "handler invokes restart repeatedly" 3
       (let ((count 0))
         (flet ((chk (ok)
                  (unless ok
                    (with-simple-restart (skip "skip")
                      (error 'simple-error :format-control "fail"))
                    (incf count))))
           (handler-bind ((simple-error
                            (lambda (e) (declare (ignore e))
                              (invoke-restart (find-restart 'skip)))))
             (chk nil) (chk t) (chk nil) (chk nil)))
         count))

; A WARNING handler that is a closure calling (muffle-warning) — not
; #'muffle-warning installed directly — must muffle cleanly.  WARN's catch
; landing had to restore the VM stack pointers after the muffle restart unwound
; through the bytecode handler frame, else a spurious "restart MUFFLE-WARNING
; not found" surfaced.
(check "muffle-warning from closure handler" 1
  (let ((log nil))
    (handler-bind ((warning (lambda (a) (push a log) (muffle-warning))))
      (warn 'simple-warning :format-control "w"))
    (length log)))

; A handler that establishes its own IGNORE-ERRORS / HANDLER-BIND while running
; must not clobber its own slot on the handler stack.  cl_signal_condition used
; to truncate the handler-stack top to the running handler's mark, so the inner
; ignore-errors overwrote that slot; a later error then invoked the stale
; binding and threw into a handler-case CATCH tag that no longer existed
; ("No catch for tag").  This is ASDF's condition-muffling shape (it blocked
; serapeum's suite).  With the fix the WARNING handler stays a WARNING handler
; and the real error reaches the outer HANDLER-CASE.
(check "handler no slot clobber after inner ignore-errors" "real error"
  (handler-case
      (handler-bind ((warning (lambda (c) (declare (ignore c))
                                (ignore-errors (error "inner probe"))
                                nil)))
        (warn "trigger")
        (error "real error"))
    (error (c) (simple-condition-format-control c))))

; A condition type whose name is ALSO a (&key ...) macro (serapeum's
; DISPATCH-CASE-ERROR / the fiveam SIGNALS shape) must not trip the compiler's
; boxing/NLX scanners.  Those scanners had no handler-bind/handler-case case, so
; their general walk speculatively macroexpanded the handler clause type as a
; form — (TYPE handler-fn) with one positional arg → "odd number of keyword
; arguments", aborting the compile.  This blocked serapeum's dispatch-case test.
(defmacro %amiga-dce (&key type datum) (declare (ignore type datum)) nil)
(define-condition %amiga-dce (error) ())
(check "handler-case macro-named cond type" :caught
  (handler-case (error '%amiga-dce) (%amiga-dce (c) (declare (ignore c)) :caught)))
(check "handler-bind macro-named cond type in block" :caught
  (block blk
    (handler-bind ((%amiga-dce (lambda (c) (declare (ignore c))
                                 (return-from blk :caught))))
      (error '%amiga-dce))
    :fell-through))

; A handler-case / handler-bind clause type that is a deftype aliasing a
; condition class must be expanded so it matches the signaled condition
; (serapeum/portability's NO-APPLICABLE-METHOD-ERROR pattern).
(deftype %amiga-err-alias () 'simple-error)
(check "handler-case deftype-alias clause" :caught
  (handler-case (error "boom") (%amiga-err-alias () :caught)))
(check "handler-bind deftype-alias clause" :caught-hb
  (block b
    (handler-bind ((%amiga-err-alias (lambda (c) (declare (ignore c))
                                       (return-from b :caught-hb))))
      (error "boom"))
    :fell-through))
(deftype %amiga-err-or () '(or type-error simple-warning))
(check "handler-case deftype-alias (or ...) clause" :caught
  (handler-case (error 'type-error :datum 1 :expected-type 'string)
    (%amiga-err-or () :caught)))
(check "handler-case deftype-alias declines non-member" :as-error
  (handler-case (error 'program-error)
    (%amiga-err-or () :wrong)
    (error () :as-error)))

; COMPILE detects warnings signaled during compilation (e.g. a macro that
; calls WARN at expansion time, like serapeum's EIF/EIF-LET) and returns
; warnings-p / failure-p (CLHS COMPILE values 2 and 3).
(defmacro %amiga-eif (test then &optional (else nil else?))
  (unless else? (warn "missing else"))
  (list 'if test then else))
(check "compile warnings-p on WARN at expand" t
  (let ((*error-output* (make-string-output-stream)))
    (not (null (nth-value 1 (compile nil '(lambda (x y) (%amiga-eif x y))))))))
(check "compile failure-p on WARN at expand" t
  (let ((*error-output* (make-string-output-stream)))
    (not (null (nth-value 2 (compile nil '(lambda (x y) (%amiga-eif x y))))))))
(check "compile no warnings-p when complete" nil
  (nth-value 1 (compile nil '(lambda (x y z) (%amiga-eif x y z)))))
(check "compile no warnings-p plain lambda" nil
  (nth-value 1 (compile nil '(lambda (x) x))))
; Regression: a pure ERROR (non-warning) at compile time sets BOTH warnings-p
; and failure-p (CLHS COMPILE: warnings-p is true for any condition of type
; ERROR or WARNING).  (signal ...) is used so the error is detected but not
; propagated past the COMPILE detection machinery.
;
; NB: the COMPILE itself must run OUTSIDE `check`, because `check` wraps its
; actual-form in (handler-case ... (error (c) ...)).  Per CLHS, `signal`
; invokes every matching handler, so that error handler would catch the
; macro's compile-time (signal simple-error) and report a spurious "signaled
; error" failure.  We capture warnings-p/failure-p where no ERROR handler is
; active, then assert on the captured values.
(defmacro %amiga-err-only ()
  (signal (make-condition 'simple-error :format-control "ct-err"))
  42)
(multiple-value-bind (%eo-fn %eo-warnings-p %eo-failure-p)
    (compile nil '(lambda () (%amiga-err-only)))
  (declare (ignore %eo-fn))
  (defparameter *amiga-err-only-warnings-p* %eo-warnings-p)
  (defparameter *amiga-err-only-failure-p* %eo-failure-p))
(check "compile error-only: warnings-p T" t
  (not (null *amiga-err-only-warnings-p*)))
(check "compile error-only: failure-p T" t
  (not (null *amiga-err-only-failure-p*)))

; --- Phase 4 Tier 2: unwind-protect ---
(check "uwp normal" '(42 t) (let ((log nil)) (let ((r (unwind-protect 42 (setq log t)))) (list r log))))
(check "uwp throw cleanup" t (let ((cleanup nil)) (catch 'done (unwind-protect (throw 'done 1) (setq cleanup t))) cleanup))
(check "uwp throw value" 42 (catch 'done (unwind-protect (throw 'done 42) (+ 1 2))))
(check "uwp nested" '(outer inner) (let ((log nil)) (catch 'done (unwind-protect (unwind-protect (throw 'done 1) (setq log (cons 'inner log))) (setq log (cons 'outer log)))) log))

; --- Phase 4 Tier 2: NLX multiple-values through unwind-protect ---
; (a) return-from carrying secondary values through one unwind-protect
(defun uwp-mv-cwlh (fn) (unwind-protect (funcall fn) nil))
(defun uwp-mv-recv () (block done (uwp-mv-cwlh (lambda () (return-from done (values nil t))))))
(check "uwp nlx mv basic" '(nil t) (multiple-value-list (uwp-mv-recv)))
; (c) nested unwind-protects: both cleanups run, values survive
(defvar *uwp-nlx-log* nil)
(setq *uwp-nlx-log* nil)
(check "uwp nlx mv nested" '(1 2 3)
  (multiple-value-list
    (block b
      (unwind-protect
        (unwind-protect (return-from b (values 1 2 3))
          (setq *uwp-nlx-log* (cons 'inner *uwp-nlx-log*)))
        (setq *uwp-nlx-log* (cons 'outer *uwp-nlx-log*))))))
(check "uwp nlx mv nested log" '(outer inner) *uwp-nlx-log*)
; (d) throw carrying multiple values through unwind-protect (bi_throw path)
(check "uwp nlx mv throw" '(10 20)
  (multiple-value-list (catch 'k (unwind-protect (throw 'k (values 10 20)) nil))))
; --- Phase 4 Tier 2: Multiple Values ---
(check "values none" nil (values))
(check "values one" 1 (values 1))
(check "values primary" 1 (values 1 2 3))
(check "mvb basic" '(1 2 3) (multiple-value-bind (a b c) (values 1 2 3) (list a b c)))
(check "mvb fewer values" '(1 nil nil) (multiple-value-bind (a b c) (values 1) (list a b c)))
(check "mvb fewer vars" 10 (multiple-value-bind (a) (values 10 20 30) a))
(check "mv-list values" '(1 2 3) (multiple-value-list (values 1 2 3)))
(check "mv-list none" nil (multiple-value-list (values)))
(check "mv-list single" '(3) (multiple-value-list (+ 1 2)))
(check "nth-value 0" 10 (nth-value 0 (values 10 20 30)))
(check "nth-value 1" 20 (nth-value 1 (values 10 20 30)))
(check "nth-value 2" 30 (nth-value 2 (values 10 20 30)))
(check "nth-value oob" nil (nth-value 5 (values 1 2 3)))
(check "values-list" '(1 2 3) (multiple-value-list (values-list '(1 2 3))))
(check "values-list primary" 42 (values-list '(42)))
(check "mv progn" '(2 3 4) (multiple-value-list (progn 1 (values 2 3 4))))
(check "mv if" '(1 2) (multiple-value-list (if t (values 1 2))))
(check "mv let" '(1 2 3) (multiple-value-list (let ((x 1)) (values x 2 3))))
(check "mvp1" '(1 2 3) (multiple-value-list (multiple-value-prog1 (values 1 2 3) (+ 4 5))))

; --- Dynamic (special) variables ---
(setq *dv1* 10)
(check "defvar basic" 10 *dv1*)
(defvar *dv1* 99)
(check "defvar no overwrite" 10 *dv1*)
(defparameter *dp1* 10)
(check "defparameter basic" 10 *dp1*)
(defparameter *dp1* 20)
(check "defparameter overwrite" 20 *dp1*)
(defconstant +dc-test+ 42)
(check "defconstant basic" 42 +dc-test+)
(check "defconstant set error" 42
       (handler-case (set '+dc-test+ 0)
         (error (c) 42)))
(check "defconstant value preserved" 42 +dc-test+)
;; defconstant must mark the symbol CONSTANTP at run/load time (not only at
;; compile time) so it works when loaded from a precompiled FASL.
(check "defconstant constantp" t (and (constantp '+dc-test+) t))
(check "defparameter not constantp" nil (constantp '*dp1*))
(defparameter *sv1* 1)
(check "special let binding" 2 (let ((*sv1* 2)) *sv1*))
(check "special restored" 1 *sv1*)
(check "special let* binding" 20 (let* ((*sv1* 20)) *sv1*))
(check "special let* restored" 1 *sv1*)
(defparameter *sv2* 0)
(defun read-sv2 () *sv2*)
(check "special visible in fn" 99 (let ((*sv2* 99)) (read-sv2)))
;; (defvar name) with no init-form proclaims the variable special but leaves it
;; unbound (CLHS).  The special proclamation must take effect at load time (not
;; only at compile time) so a dynamic LET binding is visible to a separately
;; compiled reader even when loaded from a precompiled FASL.
(defvar *dv-noinit*)
(defun read-dv-noinit () *dv-noinit*)
(check "defvar no-init unbound" nil (boundp '*dv-noinit*))
(check "defvar no-init special dynamic" 55 (let ((*dv-noinit* 55)) (read-dv-noinit)))
(check "special fn restored" 0 (read-sv2))
(defparameter *sv3* 1)
(check "special nested" 3 (let ((*sv3* 2)) (let ((*sv3* 3)) *sv3*)))
(check "special nested restored" 1 *sv3*)
(defparameter *sv4* 1)
(check "special setq" 20 (let ((*sv4* 10)) (setq *sv4* 20) *sv4*))
(check "special setq restored" 1 *sv4*)
(defparameter *sv5* 1)
(catch 'done (unwind-protect (let ((*sv5* 99)) (throw 'done *sv5*)) nil))
(check "special uwp restored" 1 *sv5*)
(defparameter *sv6* 10)
(check "special mixed let" 23 (let ((x 1) (*sv6* 20) (y 2)) (+ x *sv6* y)))
(check "special mixed restored" 10 *sv6*)
(check "boundp bound" t (boundp '*sv6*))
(check "boundp unbound" nil (boundp (gensym)))

; --- Mutation / setf ---
(check "rplaca" '(10 . 2) (let ((x (cons 1 2))) (rplaca x 10)))
(check "rplacd" '(1 . 20) (let ((x (cons 1 2))) (rplacd x 20)))
(check "setf car" 10 (let ((x (list 1 2 3))) (setf (car x) 10) (car x)))
(check "setf cdr" '(20 30) (let ((x (list 1 2 3))) (setf (cdr x) (list 20 30)) (cdr x)))
(check "setf first" 10 (let ((x (list 1 2 3))) (setf (first x) 10) (first x)))
(check "setf rest" '(20 30) (let ((x (list 1 2 3))) (setf (rest x) (list 20 30)) (rest x)))
(check "setf nth 0" 10 (let ((x (list 1 2 3))) (setf (nth 0 x) 10) (nth 0 x)))
(check "setf nth 1" 20 (let ((x (list 1 2 3))) (setf (nth 1 x) 20) (nth 1 x)))
(check "setf variable" 42 (let ((x 1)) (setf x 42) x))
(check "setf multiple" 2 (let ((a 0) (b 0)) (setf a 1 b 2)))
(check "setf returns val" 99 (let ((x (list 1 2))) (setf (car x) 99)))
(check "make-array" t (vectorp (make-array 3)))
(check "vectorp nil" nil (vectorp 42))
(check "aref/setf aref" 60 (let ((v (make-array 3))) (setf (aref v 0) 10) (setf (aref v 1) 20) (setf (aref v 2) 30) (+ (aref v 0) (aref v 1) (aref v 2))))
(check "svref/setf svref" 12 (let ((v (make-array 2))) (setf (svref v 0) 5) (setf (svref v 1) 7) (+ (svref v 0) (svref v 1))))
(check "vector" "#(1 2 3)" (write-to-string (vector 1 2 3)))
(check "vector empty" "#()" (write-to-string (vector)))
(check "vector aref" 20 (aref (vector 10 20 30) 1))
; --- Reader #(...) syntax (Step 8) ---
(check "#() reader empty" "#()" (write-to-string #()))
(check "#() reader elems" "#(1 2 3)" (write-to-string #(1 2 3)))
(check "#() reader aref" 20 (aref #(10 20 30) 1))
(check "#() reader length" 4 (length #(a b c d)))
(check "#() reader svp" t (simple-vector-p #(1 2)))
(check "#() reader nested" "#(1 2)" (write-to-string (aref #(#(1 2) #(3 4)) 0)))

(check "array-dimensions" '(5) (array-dimensions (make-array 5)))
(check "array-rank" 1 (array-rank (make-array 5)))

; --- Multi-dim array printing (Step 9) ---
(check "print 2d array" "#2A((1 2 3) (4 5 6))" (write-to-string (make-array '(2 3) :initial-contents '((1 2 3) (4 5 6)))))
(check "print 3d array" "#3A(((1 2) (3 4)) ((5 6) (7 8)))" (write-to-string (make-array '(2 2 2) :initial-contents '(((1 2) (3 4)) ((5 6) (7 8))))))
(check "print 2d empty" "#2A(() ())" (write-to-string (make-array '(2 0))))
(check "print-array nil vec" "#<VECTOR>" (let ((*print-array* nil)) (write-to-string (vector 1 2))))
(check "print-array nil arr" "#<ARRAY>" (let ((*print-array* nil)) (write-to-string (make-array '(2 3)))))

; --- Multi-dimensional arrays (Step 4) ---
(check "make-array 2d" '(2 3) (array-dimensions (make-array '(2 3))))
(check "aref 2d setf" 70 (let ((a (make-array '(2 3)))) (setf (aref a 0 0) 10) (setf (aref a 1 2) 60) (+ (aref a 0 0) (aref a 1 2))))
(check "setf aref 2d ret" 99 (let ((a (make-array '(3 3)))) (setf (aref a 1 2) 99)))
(check "aref 3d" 9 (let ((a (make-array '(2 2 2)))) (setf (aref a 0 0 0) 1) (setf (aref a 1 1 1) 8) (+ (aref a 0 0 0) (aref a 1 1 1))))
(check "array-rank 2d" 2 (array-rank (make-array '(3 4))))
(check "array-dimensions 2d" '(3 4) (array-dimensions (make-array '(3 4))))

; --- Array query builtins (Step 5) ---
(check "array-dimension 1d" 5 (array-dimension (make-array 5) 0))
(check "array-dimension 2d ax0" 3 (array-dimension (make-array '(3 4)) 0))
(check "array-dimension 2d ax1" 4 (array-dimension (make-array '(3 4)) 1))
(check "array-total-size 1d" 5 (array-total-size (make-array 5)))
(check "array-total-size 2d" 12 (array-total-size (make-array '(3 4))))
(check "array-total-size 3d" 24 (array-total-size (make-array '(2 3 4))))
(check "row-major-index 1d" 3 (array-row-major-index (make-array 5) 3))
(check "row-major-index 2d" 0 (array-row-major-index (make-array '(3 4)) 0 0))
(check "row-major-index 2d b" 4 (array-row-major-index (make-array '(3 4)) 1 0))
(check "row-major-index 2d c" 11 (array-row-major-index (make-array '(3 4)) 2 3))
(check "row-major-aref" 99 (let ((a (make-array '(2 3)))) (setf (aref a 1 2) 99) (row-major-aref a 5)))
(check "setf row-major-aref" 77 (let ((a (make-array '(2 3)))) (setf (row-major-aref a 5) 77) (aref a 1 2)))

; --- Fill pointer operations (Step 6) ---
(check "fill-pointer 0" 0 (fill-pointer (make-array 10 :fill-pointer 0)))
(check "fill-pointer 5" 5 (fill-pointer (make-array 10 :fill-pointer 5)))
; CLHS make-array: :fill-pointer t means fill-pointer = the dimension
(check "fill-pointer t" 10 (fill-pointer (make-array 10 :fill-pointer t)))
(check "has-fill-pointer t" t (array-has-fill-pointer-p (make-array 5 :fill-pointer 0)))
(check "has-fill-pointer nil" nil (array-has-fill-pointer-p (make-array 5)))
(check "setf fill-pointer" 7 (let ((v (make-array 10 :fill-pointer 0))) (setf (fill-pointer v) 7) (fill-pointer v)))
(check "setf fill-pointer ret" 3 (let ((v (make-array 10 :fill-pointer 0))) (setf (fill-pointer v) 3)))
(check "vector-push basic" 3 (let ((v (make-array 5 :fill-pointer 0))) (vector-push 10 v) (vector-push 20 v) (vector-push 30 v) (fill-pointer v)))
(check "vector-push ret" 1 (let ((v (make-array 5 :fill-pointer 0))) (vector-push 10 v) (vector-push 20 v)))
(check "vector-push full" nil (let ((v (make-array 2 :fill-pointer 0))) (vector-push 1 v) (vector-push 2 v) (vector-push 3 v)))
(check "vector-push data" 30 (let ((v (make-array 3 :fill-pointer 0))) (vector-push 10 v) (vector-push 20 v) (+ (aref v 0) (aref v 1))))
(check "length w/ fp" 2 (let ((v (make-array 10 :fill-pointer 0))) (vector-push 1 v) (vector-push 2 v) (length v)))
(check "adjust-array grow" 100 (let ((v (make-array 3 :adjustable t :initial-element 1))) (let ((v2 (adjust-array v 5 :initial-element 99))) (+ (aref v2 0) (aref v2 3)))))
(check "adjust-array shrink" 3 (let ((v (make-array 5 :adjustable t :initial-element 42))) (array-total-size (adjust-array v 3))))
(check "adjust-array fp" 2 (let ((v (make-array 5 :fill-pointer 2 :adjustable t))) (fill-pointer (adjust-array v 10))))
(check "adjust-array identity" t (let ((v (make-array 3 :adjustable t))) (eq v (adjust-array v 10))))
(check "adjust-array data" 104 (let ((v (make-array 3 :adjustable t :initial-element 5))) (adjust-array v 6 :initial-element 99) (+ (aref v 0) (aref v 4))))
; Regression: re-adjusting an already-adjusted (internally displaced) buffer
; must allocate a fresh non-displaced backing, else cl_vector_data follows a
; null backing and crashes.  This is drakma/chunga's HTTP-response buffer growth.
(check "adjust-array re-grow" 131 (let ((v (make-array 4 :element-type '(unsigned-byte 8) :adjustable t :fill-pointer 0))) (dotimes (i 3) (vector-push-extend i v)) (setq v (adjust-array v 8)) (dotimes (i 5) (vector-push-extend (+ 10 i) v)) (setq v (adjust-array v 20)) (dotimes (i 5) (vector-push-extend (+ 100 i) v)) (+ (length v) (aref v 0) (aref v 7) (aref v 12))))
(check "vpush-ext basic" 2 (let ((v (make-array 2 :fill-pointer 0 :adjustable t))) (vector-push-extend 10 v) (vector-push-extend 20 v) (vector-push-extend 30 v)))
(check "vpush-ext fp" 3 (let ((v (make-array 2 :fill-pointer 0 :adjustable t))) (vector-push-extend 10 v) (vector-push-extend 20 v) (vector-push-extend 30 v) (fill-pointer v)))
(check "vpush-ext data" 60 (let ((v (make-array 2 :fill-pointer 0 :adjustable t))) (vector-push-extend 10 v) (vector-push-extend 20 v) (vector-push-extend 30 v) (+ (aref v 0) (aref v 1) (aref v 2))))
(check "vpush-ext many" 20 (let ((v (make-array 1 :fill-pointer 0 :adjustable t))) (dotimes (i 20) (vector-push-extend i v)) (fill-pointer v)))
(check "vpush-ext identity" t (let ((v (make-array 1 :fill-pointer 0 :adjustable t))) (let ((v2 v)) (vector-push-extend 42 v) (vector-push-extend 99 v) (eq v v2))))
(check "vpush-ext zero" 42 (let ((v (make-array 0 :fill-pointer 0 :adjustable t))) (vector-push-extend 42 v) (aref v 0)))
(check "vpush-ext noadj basic" 2 (let ((v (make-array 2 :fill-pointer 0))) (vector-push-extend 10 v) (vector-push-extend 20 v) (vector-push-extend 30 v)))
(check "vpush-ext noadj fp" 3 (let ((v (make-array 2 :fill-pointer 0))) (vector-push-extend 10 v) (vector-push-extend 20 v) (vector-push-extend 30 v) (fill-pointer v)))
(check "vpush-ext noadj data" 60 (let ((v (make-array 2 :fill-pointer 0))) (vector-push-extend 10 v) (vector-push-extend 20 v) (vector-push-extend 30 v) (+ (aref v 0) (aref v 1) (aref v 2))))
(check "vpush-ext noadj identity" t (let ((v (make-array 1 :fill-pointer 0))) (let ((v2 v)) (vector-push-extend 42 v) (vector-push-extend 99 v) (eq v v2))))
(check "vpush-ext noadj adj-p" nil (let ((v (make-array 2 :fill-pointer 0))) (vector-push-extend 10 v) (vector-push-extend 20 v) (vector-push-extend 30 v) (adjustable-array-p v)))
(check "vpush-ext noadj zero" 42 (let ((v (make-array 0 :fill-pointer 0))) (vector-push-extend 42 v) (aref v 0)))
(check "vpush-ext noadj char" "xyz" (let ((s (make-array 2 :element-type 'character :fill-pointer 0))) (vector-push-extend #\x s) (vector-push-extend #\y s) (vector-push-extend #\z s) s))
; CHAR works on a fill-pointer character vector (string-vector): it is a valid
; non-simple STRING, so CHAR must accept it (FSet compare-strings fallback path).
(check "char on string-vector" #\b (let ((s (make-array 3 :element-type 'character :fill-pointer 3 :adjustable t))) (setf (char s 0) #\a) (setf (char s 1) #\b) (setf (char s 2) #\c) (char s 1)))
; make-array :element-type 'base-char builds a BASE-STRING, matching make-string
; (FSet seq leaves rely on this; on the Amiga all strings are narrow anyway).
(check "make-array base-char base-string" t (typep (make-array 3 :element-type 'base-char :initial-element #\a) 'base-string))

; --- Array type predicates (Step 7) ---
(check "arrayp vector" t (arrayp (vector 1 2 3)))
(check "arrayp make-array" t (arrayp (make-array 5)))
(check "arrayp 2d" t (arrayp (make-array '(2 3))))
(check "arrayp string" t (arrayp "hello"))
(check "arrayp fixnum" nil (arrayp 42))
(check "arrayp list" nil (arrayp '(1 2)))
(check "arrayp nil" nil (arrayp nil))
(check "simple-vector-p vec" t (simple-vector-p (vector 1 2 3)))
(check "simple-vector-p arr" t (simple-vector-p (make-array 5)))
(check "simple-vector-p fp" nil (simple-vector-p (make-array 5 :fill-pointer 0)))
(check "simple-vector-p adj" nil (simple-vector-p (make-array 5 :adjustable t)))
(check "simple-vector-p 2d" nil (simple-vector-p (make-array '(2 3))))
(check "simple-vector-p str" nil (simple-vector-p "hello"))
(check "simple-vector-p fix" nil (simple-vector-p 42))
(check "adjustable-array-p t" t (adjustable-array-p (make-array 5 :adjustable t)))
(check "adjustable-array-p nil" nil (adjustable-array-p (make-array 5)))
(check "adjustable-array-p vec" nil (adjustable-array-p (vector 1 2)))
(check "adjustable-array-p str" nil (adjustable-array-p "hello"))
(check "typep array vec" t (typep (vector 1 2) 'array))
(check "typep array 2d" t (typep (make-array '(2 3)) 'array))
(check "typep array str" t (typep "hello" 'array))
(check "typep array fix" nil (typep 42 'array))
(check "typep vector 1d" t (typep (vector 1 2) 'vector))
(check "typep vector str" t (typep "hello" 'vector))
(check "typep vector 2d" nil (typep (make-array '(2 3)) 'vector))
(check "typep simple-vec" t (typep (vector 1 2) 'simple-vector))
(check "typep simple-vec fp" nil (typep (make-array 5 :fill-pointer 0) 'simple-vector))
(check "typep simple-vec str" nil (typep "hello" 'simple-vector))
(check "typep simple-arr" t (typep (vector 1 2) 'simple-array))
(check "typep simple-arr 2d" t (typep (make-array '(2 3)) 'simple-array))
(check "typep simple-arr fp" nil (typep (make-array 5 :fill-pointer 0) 'simple-array))
(check "type-of simple-vec" 'simple-vector (type-of (vector 1 2 3)))
(check "type-of vec w/ fp" 'vector (type-of (make-array 5 :fill-pointer 0)))
(check "type-of 2d" 'simple-array (type-of (make-array '(2 3))))
(check "type-of string" 'string (type-of "hello"))

(defparameter *sv-t1* 42)
(check "symbol-value" 42 (symbol-value '*sv-t1*))
(defparameter *sv-t2* 10)
(check "setf symbol-value" 99 (setf (symbol-value '*sv-t2*) 99))
(check "symbol-value after setf" 99 *sv-t2*)
(defparameter *set-t* 10)
(check "set" 77 (set '*set-t* 77))
(check "set result" 77 *set-t*)
(check "push" '(1 2 3) (let ((x (list 2 3))) (push 1 x) x))
(check "pop" 1 (let ((x (list 1 2 3))) (pop x)))
(check "pop side effect" '(2 3) (let ((x (list 1 2 3))) (pop x) x))
(check "incf" 11 (let ((x 10)) (incf x)))
(check "incf delta" 15 (let ((x 10)) (incf x 5)))
(check "decf" 9 (let ((x 10)) (decf x)))
(check "decf delta" 5 (let ((x 10)) (decf x 5)))

; --- member & pushnew ---
(check "member found" '(2 3) (member 2 '(1 2 3)))
(check "member not found" nil (member 4 '(1 2 3)))
(check "member with test" '("a" "b") (member "a" (list "a" "b") :test #'equal))
(check "pushnew new item" '(3 1 2) (let ((x '(1 2))) (pushnew 3 x) x))
(check "pushnew existing" '(1 2) (let ((x '(1 2))) (pushnew 1 x) x))
(check "pushnew with test" '("a" "b") (let ((x (list "a" "b"))) (pushnew "a" x :test #'equal) x))

; --- eval-when ---
(check "eval-when execute" 3 (eval-when (:execute) (+ 1 2)))
(check "eval-when multiple" 30 (eval-when (:compile-toplevel :load-toplevel :execute) (+ 10 20)))
(check "eval-when body" 3 (eval-when (:execute) 1 2 3))

; --- load-time-value ---
(check "ltv basic" 3 (load-time-value (+ 1 2)))
(check "ltv list" '(1 2 3) (load-time-value (list 1 2 3)))
(check "ltv constant" 42 (load-time-value 42))
(check "ltv t" t (load-time-value t))
(check "ltv nil" nil (load-time-value nil))
(check "ltv read-only" 30 (load-time-value (+ 10 20) t))
(defun ltv-fn () (load-time-value (cons 'a 'b)))
(check "ltv in function" '(a . b) (ltv-fn))
(check "ltv same object" t (eq (ltv-fn) (ltv-fn)))

; --- get-properties ---
(check "get-properties found" '(:b 2 (:b 2 :c 3)) (multiple-value-list (get-properties '(:a 1 :b 2 :c 3) '(:b :c))))
(check "get-properties not found" '(nil nil nil) (multiple-value-list (get-properties '(:a 1) '(:x))))
(check "get-properties empty" '(nil nil nil) (multiple-value-list (get-properties '() '(:a))))

; --- destructuring-bind ---
(check "d-bind simple" '(1 2 3) (destructuring-bind (a b c) '(1 2 3) (list a b c)))
(check "d-bind nested" '(1 2 3 (4 5)) (destructuring-bind (a (b c) &rest d) '(1 (2 3) 4 5) (list a b c d)))
(check "d-bind &rest" '(1 (2 3)) (destructuring-bind (a &rest b) '(1 2 3) (list a b)))
(check "d-bind &optional default" '(1 10) (destructuring-bind (a &optional (b 10)) '(1) (list a b)))
;; CLHS 22.1.3.12: *print-level* bounds custom print-object dispatch — at
;; the limit the object prints "#", the method is NOT invoked (regression:
;; mutually-referencing instances with fan-out-2 print-object recursed to
;; the printer hard cap; chipi item<->binding log lines took minutes)
(defclass plevel-node () ((a :accessor plevel-a :initform nil)
                          (b :accessor plevel-b :initform nil)))
(defmethod print-object ((n plevel-node) s)
  (format s "#<pnode ~a ~a>" (plevel-a n) (plevel-b n)))
(check "print-level bounds print-object" t
  (let ((x (make-instance 'plevel-node))
        (y (make-instance 'plevel-node)))
    (setf (plevel-a x) y (plevel-b x) y
          (plevel-a y) x (plevel-b y) x)
    (let ((*print-level* 2))
      (< (length (princ-to-string x)) 100))))
;; Regression: a print-object method whose output contains a non-ASCII char
;; returns a WIDE string from the hook; the C check accepted only narrow
;; strings and fell back to raw #S(...) all-slots printing (eta-hab's
;; umlaut-labeled item graphs then printed exponentially).
(defclass wide-po-node () ((w-label :accessor w-label :initform nil)))
(defmethod print-object ((n wide-po-node) s)
  (format s "#<wide-po ~a>" (w-label n)))
(check "print-object wide-string result honored" t
  (let ((n (make-instance 'wide-po-node)))
    (setf (w-label n)
          (concatenate 'string "F" (string (code-char 252)) "llgrad"))
    (and (eql 0 (search "#<wide-po F" (princ-to-string n)))
         (null (search "#S(" (princ-to-string n)))
         (eql 0 (search "#<wide-po" (format nil "~a" n))))))
(define-condition wide-po-cond (error) ()
  (:report (lambda (c s) (declare (ignore c))
             (format s "kaputt: ~a!" (string (code-char 252))))))
(check "condition wide-string report honored" 0
  (search "kaputt: " (princ-to-string (make-condition 'wide-po-cond))))

;; CLHS 3.4.5: dotted lambda list == trailing &rest (eta-hab regression)
(check "d-bind dotted pair" '(1 2) (destructuring-bind (a . b) '(1 . 2) (list a b)))
(check "d-bind dotted tail" '(1 2 (3 4)) (destructuring-bind (a b . c) '(1 2 3 4) (list a b c)))
(check "d-bind dotted nested" '(1 2 3 (4)) (destructuring-bind (a (b . c) . d) '(1 (2 . 3) 4) (list a b c d)))
(check "d-bind dotted after &optional" '(1 2 (3)) (destructuring-bind (a &optional b . c) '(1 2 3) (list a b c)))
(check "d-bind dotted macroexpand path" '(1 2 (3))
       (eval (macroexpand-1 '(destructuring-bind (a &optional b . c) '(1 2 3) (list a b c)))))
(check "d-bind &optional provided" '(1 2) (destructuring-bind (a &optional (b 10)) '(1 2) (list a b)))
(check "d-bind &body" '(1 (2 3)) (destructuring-bind (a &body b) '(1 2 3) (list a b)))
; CLHS: a list that does not match the lambda-list structure must signal an error.
(check "d-bind too-few errors" :err
  (handler-case (destructuring-bind (a b) '("x") (list a b)) (error () :err)))
(check "d-bind too-many errors" :err
  (handler-case (destructuring-bind (a b) '(1 2 3) (list a b)) (error () :err)))
(check "d-bind nested too-few errors" :err
  (handler-case (destructuring-bind (a (b c)) '(1 (2)) (list a b c)) (error () :err)))
(check "d-bind &optional too-many errors" :err
  (handler-case (destructuring-bind (a &optional b) '(1 2 3) (list a b)) (error () :err)))
(check "d-bind exact ok" '(1 2) (destructuring-bind (a b) '(1 2) (list a b)))
(check "d-bind dotted absorbs rest" '(1 (2 3)) (destructuring-bind (a . b) '(1 2 3) (list a b)))
; CLHS 3.4.4/3.4.5: &whole, &aux, and destructuring &rest must be honoured
; (these were mis-bound as required params, causing spurious "too few" errors
;  that broke esrap's defrule / macros.lisp).
(check "d-bind &whole" '((1 2) 1 2) (destructuring-bind (&whole w a b) '(1 2) (list w a b)))
(check "d-bind &whole+rest" '((1 2 3) 1 (2 3))
  (destructuring-bind (&whole w a &rest r) '(1 2 3) (list w a r)))
(check "d-bind &aux" '(1 2 99) (destructuring-bind (a b &aux (c 99)) '(1 2) (list a b c)))
(check "d-bind &aux let*-init" '(3 nil)
  (destructuring-bind (a &aux (b (+ a 2)) c) '(1) (list b c)))
(check "d-bind &optional+&aux" '(1 9 5)
  (destructuring-bind (a &optional (b 9) &aux (c 5)) '(1) (list a b c)))
(check "d-bind destructuring &rest" '(1 2 3)
  (destructuring-bind (a &rest (b c)) '(1 2 3) (list a b c)))
(check "d-bind too-few still errors after &whole/&aux fix" :err
  (handler-case (destructuring-bind (a b c) '(1 2) (list a b c)) (error () :err)))
; destructuring-bind must be a real macro MACROEXPAND actually expands (CLHS),
; else code-walkers that recurse on (macroexpand form) loop forever.
(check "d-bind macroexpands" t
  (nth-value 1 (macroexpand-1 '(destructuring-bind (a b) y (list a b)))))
(check "d-bind expansion runs" '(1 2 3 (4 5))
  (eval (macroexpand-1 '(destructuring-bind (a (b c) &rest d) '(1 (2 3) 4 5) (list a b c d)))))
; CLHS 3.4.5: a dotted tail after &key/&aux is malformed — the dotted
; abbreviation stands in the &rest position, which must precede &key/&aux.
; Both the compiler special form AND the %dbind-expand macroexpansion path
; must reject it identically.  The compiler special form signals its error
; at COMPILE time, so the destructuring-bind must be wrapped in (eval '...)
; rather than written directly — a direct compile-time error would abort
; compiling the whole enclosing (check ...) top-level form before CHECK's
; own handler-case (or one written here) ever gets installed to catch it.
(check "d-bind dotted after &key errors" :err
  (handler-case (eval '(destructuring-bind (a &key b . c) '(1 :b 2 3 4) (list a b c)))
    (error () :err)))
(check "d-bind dotted after &key errors (macroexpand path)" :err
  (handler-case (eval (macroexpand-1 '(destructuring-bind (a &key b . c) '(1 :b 2 3 4) (list a b c))))
    (error () :err)))
(check "d-bind dotted after &aux errors" :err
  (handler-case (eval '(destructuring-bind (a &aux (b 1) . c) '(1) (list a b c)))
    (error () :err)))
(check "d-bind dotted after &aux errors (macroexpand path)" :err
  (handler-case (eval (macroexpand-1 '(destructuring-bind (a &aux (b 1) . c) '(1) (list a b c))))
    (error () :err)))

;; Compiler-chain unwind regression: the compile-time errors just above are
;; caught by HANDLER-CASE via longjmp, and the abandoned compiler must be
;; freed at the NLX landing (anchor-based unwind).  The old protect-flag walk
;; leaked it on the active-compiler chain, so every later "top-level" compile
;; inherited its stale optimize settings — DECLAIM appeared dead for the rest
;; of the session ("the safety 0" and "declaim survives fasl round trip"
;; failed thousands of lines downstream of the leak here).
(declaim (optimize (safety 0)))
(check "declaim live after caught compile error" "ok"
  (handler-case (eval '(the fixnum "ok")) (type-error () :declaim-dead)))
(declaim (optimize (safety 1)))
;; Live direction: a throwing macroexpander crosses the in-progress compile —
;; its abandoned compilers are freed at the CATCH landing, but a landing
;; inside the expander's own VM run must NOT free the enclosing live compiler.
(defmacro %chain-throwing-macro () (throw 'chain-tag :thrown))
(check "throw across in-progress compile" :thrown
  (catch 'chain-tag (eval '(funcall (lambda () (list (%chain-throwing-macro)))))))
(defmacro %chain-self-catching-macro ()
  (catch 'chain-inner (throw 'chain-inner ''survived)))
(check "in-progress compiler survives inner NLX landing" 'survived
  (eval '(funcall (lambda () (%chain-self-catching-macro)))))
(declaim (optimize (safety 0)))
(check "declaim live after NLX-across-compile stress" "ok2"
  (handler-case (eval '(the fixnum "ok2")) (type-error () :declaim-dead)))
(declaim (optimize (safety 1)))

; --- define-modify-macro with &optional (var default) ---
; Regression: the (delta 1) spec was spliced whole into the call form, emitting
; (fn place (delta 1)) → "Undefined function: DELTA".
(defun dmm-plus-t (a b) (+ a b))
(define-modify-macro dmm-incf-t (&optional (delta 1)) dmm-plus-t)
(check "define-modify-macro optional default" 6 (let ((x 5)) (dmm-incf-t x) x))
(check "define-modify-macro optional given" 15 (let ((x 5)) (dmm-incf-t x 10) x))

; --- notinline inhibits compiler-macro expansion (CLHS 3.2.2.1.3) ---
; The compiler-macro fallback idiom wraps the call in a notinline shield; without
; honoring notinline the macro re-fires forever (GC-root overflow compiling
; serapeum).
(defun nicm-fn-t (x) (* x 2))
(define-compiler-macro nicm-fn-t (&whole form x)
  (declare (ignore form))
  `(locally (declare (notinline nicm-fn-t)) (+ 0 (nicm-fn-t ,x))))
(check "notinline stops compiler-macro" 10 (nicm-fn-t 5))
(check "notinline scoped re-expands" 12 (locally (declare (notinline nicm-fn-t)) (nicm-fn-t 6)))

; --- defsetf ---
(defun my-get-t (vec i) (aref vec i))
(defun my-put-t (vec i val) (setf (aref vec i) val) val)
(defsetf my-get-t my-put-t)
(check "defsetf short" 42 (let ((v (make-array 3))) (setf (my-get-t v 0) 42) (my-get-t v 0)))
(defun set-cadr-t (l v) (rplaca (cdr l) v) v)
(defsetf cadr set-cadr-t)
(check "defsetf cadr" '(1 99 3) (let ((x (list 1 2 3))) (setf (cadr x) 99) x))

; --- defun implicit block ---
(defun find-test (item list) (do ((l list (cdr l))) ((null l) nil) (when (eql item (car l)) (return-from find-test l))))
(check "defun return-from found" '(2 3) (find-test 2 '(1 2 3)))
(check "defun return-from nil" nil (find-test 4 '(1 2 3)))

; --- Phase 5 Tier 1: Character functions ---
(check "characterp char" t (characterp #\A))
(check "characterp non-char" nil (characterp 65))
(check "char= same" t (char= #\A #\A))
(check "char= diff" nil (char= #\A #\B))
(check "char/= diff" t (char/= #\A #\B))
(check "char/= same" nil (char/= #\A #\A))
(check "char< true" t (char< #\A #\B))
(check "char< false" nil (char< #\B #\A))
(check "char> true" t (char> #\B #\A))
(check "char> false" nil (char> #\A #\B))
(check "char<= equal" t (char<= #\A #\A))
(check "char<= less" t (char<= #\A #\B))
(check "char>= equal" t (char>= #\B #\B))
(check "char>= greater" t (char>= #\B #\A))
(check "char-code A" 65 (char-code #\A))
(check "char-code a" 97 (char-code #\a))
(check "code-char 65" #\A (code-char 65))
(check "char-code roundtrip" 97 (char-code (code-char 97)))
(check "char-upcase a" #\A (char-upcase #\a))
(check "char-upcase A" #\A (char-upcase #\A))
(check "char-downcase A" #\a (char-downcase #\A))
(check "char-downcase a" #\a (char-downcase #\a))
(check "upper-case-p A" t (upper-case-p #\A))
(check "upper-case-p a" nil (upper-case-p #\a))
(check "lower-case-p a" t (lower-case-p #\a))
(check "lower-case-p A" nil (lower-case-p #\A))
(check "alpha-char-p A" t (alpha-char-p #\A))
(check "alpha-char-p 1" nil (alpha-char-p #\1))
; CLHS digit-char-p: returns the integer weight, not boolean T
(check "digit-char-p 5" 5 (digit-char-p #\5))
(check "digit-char-p A" nil (digit-char-p #\A))

; --- Phase 5 Tier 1: Symbol functions ---
(check "symbol-name foo" "FOO" (symbol-name 'foo))
(check "symbol-name nil" "NIL" (symbol-name nil))
(check "fboundp +" t (fboundp '+))
(check "fboundp gensym-result" nil (fboundp (gensym)))
(check "fdefinition +" t (functionp (fdefinition '+)))
(check "make-symbol" t (symbolp (make-symbol "TEST")))
(check "make-symbol name" "HELLO" (symbol-name (make-symbol "HELLO")))
(check "keywordp keyword" t (keywordp :foo))
(check "keywordp symbol" nil (keywordp 'foo))
(check "keywordp number" nil (keywordp 42))

; --- Phase 5 Tier 1: Fixed builtins ---
(check "length vector" 5 (length (make-array 5)))
(check "length vector 0" 0 (length (make-array 0)))
(check "mapcar multi-list" '(11 22 33) (mapcar #'+ '(1 2 3) '(10 20 30)))
(check "mapcar multi-list fn" '((a 1) (b 2)) (mapcar #'list '(a b) '(1 2)))
(check "mapcar multi uneven" '(11 22) (mapcar #'+ '(1 2 3) '(10 20)))

; --- Phase 5 Tier 2: String functions ---
(check "string= same" t (string= "hello" "hello"))
(check "string= diff" nil (string= "hello" "HELLO"))
(check "string-equal ci" t (string-equal "hello" "HELLO"))
(check "string-equal diff" nil (string-equal "hello" "world"))
(check "string< true" t (string< "abc" "abd"))
(check "string< false" nil (string< "abd" "abc"))
(check "string> true" t (string> "abd" "abc"))
(check "string<= equal" t (string<= "abc" "abc"))
(check "string>= equal" t (string>= "abc" "abc"))
(check "string< shorter" t (string< "ab" "abc"))
(check "string-upcase" "HELLO" (string-upcase "hello"))
(check "string-downcase" "hello" (string-downcase "HELLO"))
(check "string-trim" "hello" (string-trim " " "  hello  "))
(check "string-left-trim" "hello  " (string-left-trim " " "  hello  "))
(check "string-right-trim" "  hello" (string-right-trim " " "  hello  "))
(check "subseq string" "el" (subseq "hello" 1 3))
(check "subseq string end" "llo" (subseq "hello" 2))
(check "subseq list" '(b c) (subseq '(a b c d) 1 3))
(check "concatenate" "hello world" (concatenate 'string "hello" " " "world"))
; EQUAL never descends general vectors (CLHS), so a #(...) literal can't match
; the (distinct) concatenate result — compare element-wise with EQUALP, as the
; rest of the vector-content checks below do.
(check "concatenate compound byte-array" t
  (equalp #(65 66 0 2)
          (concatenate '(simple-array (unsigned-byte 8) (*))
                       (map 'vector #'char-code "AB") (vector 0) (vector 2))))
(check "concatenate compound char-array" "abcd"
  (concatenate '(vector character) "ab" "cd"))
(check "concatenate compound simple-array char" "xyz"
  (concatenate '(simple-array character (*)) "xy" "z"))
; --- ANSI sequences conformance: the last 10 SEQUENCES-chapter fixes ---
; FILL: duplicate :start/:end keywords — leftmost wins (CLHS 3.4.1.4), all
; value forms evaluated (FILL.ORDER.4).
(check "fill leftmost keyword wins" '((a z z a) 4)
  (let ((i 0) (a (copy-seq #(a a a a))))
    (let ((r (fill a 'z :end (progn (incf i) 3) :end (progn (incf i) 1)
                   :start (progn (incf i) 1) :start (progn (incf i) 0))))
      (list (coerce r 'list) i))))
; (SETF FILL-POINTER) on a fill-pointer bit-vector (MISMATCH-BIT-VECTOR.24).
(check "setf fill-pointer bit-vector" '(4 6 6)
  (let ((m (make-array 6 :element-type 'bit
            :initial-contents '(0 0 1 0 1 1) :fill-pointer 4)))
    (list (length m) (setf (fill-pointer m) 6) (length m))))
; SUBSEQ of a fill-pointer character vector returns a STRING (SUBSEQ-STRING.3).
(check "subseq string-vector returns string" '(t "abcdefgh" "cdefgh" "cde")
  (let ((s (make-array 10 :element-type 'character
            :initial-contents "abcdefghij" :fill-pointer 8)))
    (list (stringp (subseq s 0)) (subseq s 0) (subseq s 2) (subseq s 2 5))))
; CONCATENATE honours the active length of a fill-pointer bit-vector and a
; declared length constraint (CONCATENATE.30 / .30A).
(check "concatenate fill-pointer bit-vector active length" t
  (let ((x (make-array 10 :element-type 'bit
            :initial-contents '(0 1 1 0 0 1 0 1 1 1) :fill-pointer 5)))
    (and (equalp (concatenate 'bit-vector x '(0)) #*011000)
         (equalp (concatenate '(bit-vector 10) x x) #*0110001100)
         (equalp (concatenate '(bit-vector *) x) #*01100)
         t)))
; --- Packed byte vectors: (unsigned-byte 8) / (signed-byte 8) arrays -------
; True 1-byte-per-element storage (TYPE_BYTE_VECTOR) instead of 4-byte tagged
; elements — the memory win matters most on this 8MB target.
(check "byte-vector make-array + aref" '(255 0 7)
  (let ((v (make-array 3 :element-type '(unsigned-byte 8))))
    (setf (aref v 0) 255 (aref v 2) 7)
    (list (aref v 0) (aref v 1) (aref v 2))))
(check "byte-vector element type" '((unsigned-byte 8) (signed-byte 8))
  (list (array-element-type (make-array 2 :element-type '(unsigned-byte 8)))
        (array-element-type (make-array 2 :element-type '(signed-byte 8)))))
(check "byte-vector upgraded-array-element-type" '((unsigned-byte 8) (signed-byte 8))
  (list (upgraded-array-element-type '(mod 256))
        (upgraded-array-element-type '(integer -5 5))))
(check "byte-vector signed range" '(-128 127)
  (let ((v (make-array 2 :element-type '(signed-byte 8)
                       :initial-contents '(-128 127))))
    (list (aref v 0) (aref v 1))))
(check "byte-vector typep" '(t t t t nil nil)
  (let ((v (make-array 2 :element-type '(unsigned-byte 8))))
    (list (typep v 'vector) (typep v 'array) (typep v 'sequence)
          (typep v '(vector (unsigned-byte 8)))
          (typep v '(vector t)) (typep v 'simple-vector))))
(check "byte-vector range check signals" '(:caught :caught)
  (list (handler-case (let ((v (make-array 1 :element-type '(unsigned-byte 8))))
                        (setf (aref v 0) 300))
          (error () :caught))
        (handler-case (make-array 1 :element-type '(unsigned-byte 8)
                                  :initial-element -1)
          (error () :caught))))
(check "byte-vector fill + subseq + reverse" t
  (let ((v (make-array 5 :element-type '(unsigned-byte 8)
                       :initial-contents '(3 1 4 1 5))))
    (and (equalp (subseq v 1 4)
                 (make-array 3 :element-type '(unsigned-byte 8)
                             :initial-contents '(1 4 1)))
         (equal (array-element-type (subseq v 0)) '(unsigned-byte 8))
         (equalp (reverse v)
                 (make-array 5 :element-type '(unsigned-byte 8)
                             :initial-contents '(5 1 4 1 3)))
         (equalp (fill (make-array 3 :element-type '(unsigned-byte 8)) 9)
                 (make-array 3 :element-type '(unsigned-byte 8)
                             :initial-element 9))
         t)))
(check "byte-vector fill pointer push/pop" '(0 1 2 20 1)
  (let ((v (make-array 3 :element-type '(unsigned-byte 8) :fill-pointer 0)))
    (list (vector-push 10 v) (vector-push 20 v) (fill-pointer v)
          (vector-pop v) (fill-pointer v))))
(check "byte-vector sequence functions" '(2 2 5 14 (1 2 3))
  (let ((v (make-array 5 :element-type '(unsigned-byte 8)
                       :initial-contents '(3 1 4 1 5))))
    (list (position 4 v) (count 1 v) (find 5 v) (reduce #'+ v)
          (coerce (make-array 3 :element-type '(unsigned-byte 8)
                              :initial-contents '(1 2 3)) 'list))))
(check "byte-vector sort + equalp" t
  (let ((v (sort (make-array 5 :element-type '(unsigned-byte 8)
                             :initial-contents '(3 1 4 1 5)) #'<)))
    (and (equalp v (make-array 5 :element-type '(unsigned-byte 8)
                               :initial-contents '(1 1 3 4 5)))
         (equalp v #(1 1 3 4 5))
         t)))
(check "byte-vector coerce to packed" '(unsigned-byte 8)
  (array-element-type (coerce '(1 2 3) '(vector (unsigned-byte 8)))))
(check "byte-vector replace + setf subseq" '((1 2 3) (0 9 8 7 0))
  (let ((a (make-array 3 :element-type '(unsigned-byte 8)))
        (b (make-array 5 :element-type '(unsigned-byte 8))))
    (replace a (make-array 3 :element-type '(unsigned-byte 8)
                           :initial-contents '(1 2 3)))
    (setf (subseq b 1 4) (make-array 3 :element-type '(unsigned-byte 8)
                                     :initial-contents '(9 8 7)))
    (list (coerce a 'list) (coerce b 'list))))
; REPLACE fast path: same-element-type byte vectors go through one memmove;
; overlap on the same object must behave as if the source were copied first
; (CLHS REPLACE), and mixed signedness must keep the per-element type check.
(check "byte-vector replace overlap same object" '(1 1 2 3 4)
  (let ((a (make-array 5 :element-type '(unsigned-byte 8)
                       :initial-contents '(1 2 3 4 5))))
    (coerce (replace a a :start1 1 :start2 0 :end2 4) 'list)))
(check "byte-vector replace start/end windows" '(7 7 5 6 7)
  (let ((a (make-array 5 :element-type '(unsigned-byte 8)
                       :initial-element 7))
        (b (make-array 8 :element-type '(unsigned-byte 8)
                       :initial-contents '(1 2 3 4 5 6 7 8))))
    (coerce (replace a b :start1 2 :start2 4 :end2 6) 'list)))
(check "byte-vector replace u8->s8 out-of-range still signals" :caught
  (handler-case
      (replace (make-array 2 :element-type '(signed-byte 8))
               (make-array 2 :element-type '(unsigned-byte 8)
                             :initial-element 200))
    (error () :caught)))
(check "byte-vector replace u16 values survive" '(0 3000 4000 0)
  (let ((a (make-array 4 :element-type '(unsigned-byte 16)))
        (b (make-array 4 :element-type '(unsigned-byte 16)
                       :initial-contents '(1000 2000 3000 4000))))
    (coerce (replace a b :start1 1 :end1 3 :start2 2) 'list)))
; MAP-INTO bitwise-fold fast path (the ILBM mask fold) + general fallback.
(check "byte-vector map-into logior fold" '(17 34 68 136)
  (let ((a (make-array 4 :element-type '(unsigned-byte 8)
                       :initial-contents '(1 2 4 8)))
        (b (make-array 4 :element-type '(unsigned-byte 8)
                       :initial-contents '(16 32 64 128))))
    (coerce (map-into a #'logior a b) 'list)))
(check "byte-vector map-into logand/logxor" '((5 80 85) (90 165 170))
  (let ((a (make-array 3 :element-type '(unsigned-byte 8)
                       :initial-contents '(15 240 255)))
        (b (make-array 3 :element-type '(unsigned-byte 8)
                       :initial-contents '(85 85 85))))
    (list (coerce (map-into (make-array 3 :element-type '(unsigned-byte 8))
                            #'logand a b) 'list)
          (coerce (map-into (make-array 3 :element-type '(unsigned-byte 8))
                            #'logxor a b) 'list))))
(check "byte-vector map-into lambda still general" '(11 22 33)
  (let ((a (make-array 3 :element-type '(unsigned-byte 8)
                       :initial-contents '(1 2 3)))
        (b (make-array 3 :element-type '(unsigned-byte 8)
                       :initial-contents '(10 20 30))))
    (coerce (map-into a (lambda (x y) (+ x y)) a b) 'list)))
; EXT:UNPACK-BYTERUN1 — the PackBits/ILBM ByteRun1 RLE decode builtin.
(check "unpack-byterun1 literal+repeat+noop" '(7 (99 9 8 7 5 5 5 5 99))
  (let ((src (make-array 7 :element-type '(unsigned-byte 8)
                         :initial-contents '(128 2 9 8 7 253 5)))
        (dst (make-array 9 :element-type '(unsigned-byte 8)
                         :initial-element 99)))
    (list (ext:unpack-byterun1 src 0 7 dst 7 1) (coerce dst 'list))))
(check "unpack-byterun1 truncated input signals" :caught
  (handler-case
      (ext:unpack-byterun1
       (make-array 1 :element-type '(unsigned-byte 8) :initial-element 2)
       0 1 (make-array 3 :element-type '(unsigned-byte 8)) 3)
    (error () :caught)))
(check "unpack-byterun1 overlong repeat signals" :caught
  (handler-case
      (ext:unpack-byterun1
       (make-array 2 :element-type '(unsigned-byte 8)
                     :initial-contents '(253 7))
       0 2 (make-array 2 :element-type '(unsigned-byte 8)) 2)
    (error () :caught)))
; COUNT byte-vector fast path — a fixnum under the default EQL test
; runs as a C loop over the packed elements (the map-cache wall-code
; validation and the ILBM mask-opacity check are three such COUNTs).
(check "count over byte vector (fast path + keywords)" '(4 3 2 4 0 1)
  (let ((v (make-array 8 :element-type '(unsigned-byte 8)
                       :initial-contents '(1 0 2 0 0 3 0 1))))
    (list (count 0 v)
          (count 0 v :start 2)
          (count 0 v :start 2 :end 5)
          (count 0 v :from-end t)
          (count 300 v)
          (count 3 v :key #'1+ :test #'eql :start 1))))
; EXT:COPY-ROWS — the strided row gather/scatter builtin (ILBM plane
; extraction from an interleaved BODY decode).
(check "copy-rows gathers strided rows" '(1 2 3 4 5 6)
  (let ((src (make-array 12 :element-type '(unsigned-byte 8)
                         :initial-contents '(1 2 9 9 3 4 9 9 5 6 9 9)))
        (dst (make-array 6 :element-type '(unsigned-byte 8)
                         :initial-element 0)))
    (coerce (ext:copy-rows dst src 3 2 0 2 0 4) 'list)))
(check "copy-rows overrun signals" :caught
  (handler-case
      (ext:copy-rows (make-array 8 :element-type '(unsigned-byte 8))
                     (make-array 8 :element-type '(unsigned-byte 8))
                     3 2 0 2 0 4)
    (error () :caught)))
(check "byte-vector vector-push-extend full non-adjustable errors" :caught
  (handler-case
      (let ((v (make-array 1 :element-type '(unsigned-byte 8)
                           :fill-pointer 1)))
        (vector-push-extend 2 v))
    (error () :caught)))
(check "byte-vector adjustable buffer grows in place (drakma idiom)" '(t (unsigned-byte 8) 8 0 14)
  (let ((v (make-array 4 :element-type '(unsigned-byte 8)
                       :adjustable t :fill-pointer 0)))
    (dotimes (i 3) (vector-push-extend i v))
    (let ((w (adjust-array v 8)))
      (dotimes (i 5) (vector-push-extend (+ 10 i) v))
      (list (eq v w) (array-element-type v) (length v) (aref v 0) (aref v 7)))))
(check "byte-vector adjust-array fresh copy" '(nil (1 2 3 9 9))
  (let* ((v (make-array 3 :element-type '(unsigned-byte 8)
                        :initial-contents '(1 2 3)))
         (w (adjust-array v 5 :initial-element 9)))
    (list (eq v w) (coerce w 'list))))
; Slice 2: REMOVE/DELETE keep the packed representation and honor the
; keyword arguments (was: "not of type SEQUENCE (got BYTE-VECTOR)").
(check "byte-vector remove/delete family" '((3 4 5) (unsigned-byte 8) (3 1 4 5) (4) (3 4 5) (5 7))
  (let ((v (make-array 5 :element-type '(unsigned-byte 8)
                       :initial-contents '(3 1 4 1 5))))
    (list (coerce (remove 1 v) 'list)
          (array-element-type (remove 1 v))
          (coerce (remove 1 v :count 1 :from-end t) 'list)
          (coerce (remove-if #'oddp v) 'list)
          (coerce (delete 1 (copy-seq v)) 'list)
          (coerce (remove -1 (make-array 4 :element-type '(signed-byte 8)
                                         :initial-contents '(-1 5 -1 7)))
                  'list))))
; TYPEP expands deftype aliases in array element types (flexi-streams'
; (deftype octet () '(unsigned-byte 8)) — the string-to-octets regression),
; and the THE + setf aref pattern must not signal at default safety.
(deftype rt-octet () '(unsigned-byte 8))
(check "byte-vector typep deftype alias" '(t t t nil 65)
  (let ((v (make-array 2 :element-type '(unsigned-byte 8)))
        (s (make-array 2 :element-type '(signed-byte 8))))
    (list (typep v '(vector rt-octet))
          (typep v '(array rt-octet *))
          (typep s '(vector (signed-byte 8)))
          (typep s '(vector rt-octet))
          (let ((w (make-array 3 :element-type 'rt-octet)))
            (setf (aref (the (array rt-octet *) w) 0) 65)
            (aref w 0)))))
; Multi-dim :displaced-to onto a packed byte vector copies the window
; (values + byte element annotation) — serapeum's RESHAPE-on-RANGE idiom.
(check "byte-vector multi-dim displaced copy" '((2 3) 1 6 (unsigned-byte 8))
  (let* ((v (make-array 6 :element-type '(unsigned-byte 8)
                        :initial-contents '(1 2 3 4 5 6)))
         (a (make-array '(2 3) :displaced-to v)))
    (list (array-dimensions a) (aref a 0 0) (aref a 1 2)
          (array-element-type a))))
; SUBTYPEP compares UPGRADED element types (CLHS 15): distinct specialized
; array types are certainly NOT subtypes; equal ones are.  serapeum's
; with-type-dispatch relies on subtypep and typep agreeing here.
(check "byte-vector subtypep element types" '((nil t) (nil t) (t t) (t t) (nil t))
  (list (multiple-value-list (subtypep '(simple-array (signed-byte 8) (*))
                                       '(simple-array integer (*))))
        (multiple-value-list (subtypep '(simple-array bit (*))
                                       '(simple-array integer (*))))
        (multiple-value-list (subtypep '(vector rt-octet)
                                       '(vector (unsigned-byte 8))))
        (multiple-value-list (subtypep '(vector (unsigned-byte 8)) 'vector))
        (multiple-value-list (subtypep 'vector '(vector (unsigned-byte 8))))))

; --- Packed 16-bit vectors (slice 3): (unsigned-byte 16)/(signed-byte 16)
; at 2 bytes per element.  On the big-endian 68k the width-dispatching
; accessors and the FASL's canonical big-endian payload must agree with the
; little-endian host — value mismatches here mean an endianness bug.
(check "16-bit vector make-array + aref boundaries" '(65535 0 40000 -32768 32767)
  (let ((u (make-array 3 :element-type '(unsigned-byte 16)))
        (s (make-array 2 :element-type '(signed-byte 16))))
    (setf (aref u 0) 65535 (aref u 2) 40000
          (aref s 0) -32768 (aref s 1) 32767)
    (list (aref u 0) (aref u 1) (aref u 2) (aref s 0) (aref s 1))))
(check "16-bit vector element types" '((unsigned-byte 16) (signed-byte 16))
  (list (array-element-type (make-array 2 :element-type '(unsigned-byte 16)))
        (array-element-type (make-array 2 :element-type '(signed-byte 16)))))
(check "16-bit vector upgrading" '((unsigned-byte 16) (signed-byte 16) (unsigned-byte 8) t)
  (list (upgraded-array-element-type '(integer 0 1000))
        (upgraded-array-element-type '(integer -5 1000))
        (upgraded-array-element-type '(integer 0 255))
        (upgraded-array-element-type '(unsigned-byte 17))))
(check "16-bit vector typep discrimination" '(t nil nil t)
  (let ((v (make-array 2 :element-type '(unsigned-byte 16))))
    (list (typep v '(vector (unsigned-byte 16)))
          (typep v '(vector (unsigned-byte 8)))
          (typep v '(vector (signed-byte 16)))
          (typep v (type-of v)))))
(check "16-bit vector range check signals" '(:caught :caught :caught)
  (list (handler-case (setf (aref (make-array 1 :element-type '(unsigned-byte 16)) 0)
                            65536)
          (type-error () :caught))
        (handler-case (setf (aref (make-array 1 :element-type '(signed-byte 16)) 0)
                            -32769)
          (type-error () :caught))
        (handler-case (make-array 1 :element-type '(unsigned-byte 16)
                                  :initial-element -1)
          (type-error () :caught))))
(check "16-bit vector sequences" t
  (let ((v (make-array 5 :element-type '(unsigned-byte 16)
                       :initial-contents '(300 1000 40000 1000 65535))))
    (and (equalp (subseq v 1 4) (make-array 3 :element-type '(unsigned-byte 16)
                                            :initial-contents '(1000 40000 1000)))
         (equal (array-element-type (subseq v 0)) '(unsigned-byte 16))
         (equalp (reverse v) (make-array 5 :element-type '(unsigned-byte 16)
                                         :initial-contents '(65535 1000 40000 1000 300)))
         (equalp (fill (make-array 2 :element-type '(unsigned-byte 16)) 50000)
                 (make-array 2 :element-type '(unsigned-byte 16)
                             :initial-element 50000))
         (equalp (sort (copy-seq v) #'<)
                 (make-array 5 :element-type '(unsigned-byte 16)
                             :initial-contents '(300 1000 1000 40000 65535)))
         (= (reduce #'+ v) 107835)
         t)))
(check "16-bit vector cross-width equalp + setf subseq" '(t (1 2 255))
  (list (equalp (make-array 2 :element-type '(unsigned-byte 16)
                            :initial-contents '(1 2))
                (make-array 2 :element-type '(unsigned-byte 8)
                            :initial-contents '(1 2)))
        (let ((v (make-array 3 :element-type '(unsigned-byte 16))))
          (setf (subseq v 0) (make-array 3 :element-type '(unsigned-byte 8)
                                         :initial-contents '(1 2 255)))
          (coerce v 'list))))
(check "16-bit vector fill pointer + adjust" '(0 1 2 65535 (10 20 9 9))
  (let ((v (make-array 3 :element-type '(unsigned-byte 16) :fill-pointer 0)))
    (append (list (vector-push 40000 v) (vector-push 65535 v) (fill-pointer v)
                  (vector-pop v))
            (list (coerce (adjust-array
                           (make-array 2 :element-type '(unsigned-byte 16)
                                       :initial-contents '(10 20))
                           4 :initial-element 9)
                          'list)))))
(check "16-bit vector coerce" '((unsigned-byte 16) (1000 65535))
  (let ((v (coerce '(1000 65535) '(vector (unsigned-byte 16)))))
    (list (array-element-type v) (coerce v 'list))))
(check "16-bit vector subtypep disjoint from u8" '((nil t) (nil t) (t t))
  (list (multiple-value-list (subtypep '(vector (unsigned-byte 16))
                                       '(vector (unsigned-byte 8))))
        (multiple-value-list (subtypep '(vector (unsigned-byte 8))
                                       '(vector (unsigned-byte 16))))
        (multiple-value-list (subtypep '(vector (unsigned-byte 16))
                                       '(vector (unsigned-byte 16))))))
(check "16-bit vector adjustable fallback reports type" '(t (unsigned-byte 16) t)
  (let ((v (make-array 2 :element-type '(unsigned-byte 16)
                       :adjustable t :fill-pointer 0)))
    (vector-push-extend 40000 v)
    (list (adjustable-array-p v) (array-element-type v)
          (typep v '(vector (unsigned-byte 16))))))
; FASL literal round trip: written on the LE host by make fasl / compile-file,
; loaded here on the BE 68k — pins the canonical big-endian payload.
(check "16-bit vector literal via compile" '(0 255 256 4660 65535 -32768 32767)
  (append (coerce #.(make-array 5 :element-type '(unsigned-byte 16)
                                :initial-contents '(0 255 256 4660 65535))
                  'list)
          (coerce #.(make-array 2 :element-type '(signed-byte 16)
                                :initial-contents '(-32768 32767))
                  'list)))

; MAP with an (or ...) result type: branch matching the produced length (MAP.48).
(check "map or-result-type matching length" t
  (equalp (map '(or (vector t 10) (vector t 5)) #'identity '(1 2 3 4 5))
          #(1 2 3 4 5)))
; No branch matches the produced length → type-error (MAP.ERROR.11).
(check "map or-result-type no match type-error" :type-error
  (handler-case (progn (map '(or (vector t 5) (vector t 10))
                            #'identity '(1 2 3 4 5 6)) :no-error)
    (type-error () :type-error)))
; (SETF SUBSEQ) evaluates PLACE subforms left-to-right before the value form
; (CLHS 5.1.1.1.1, SUBSEQ.ORDER.3): a=1 b=2 c=3 d=4.
(check "setf subseq eval order" '("axyzefgh" 1 2 3 4)
  (let ((i 0) a b c d (s (copy-seq "abcdefgh")))
    (setf (subseq (progn (setf a (incf i)) s)
                  (progn (setf b (incf i)) 1)
                  (progn (setf c (incf i)) 4))
          (progn (setf d (incf i)) "xyz"))
    (list s a b c d)))
; (SETF fn) fast path: atom place subform + atom value — correct stored value.
(defclass setf-order-cls () ((x :initarg :x :initform 0 :accessor soc-x)))
(check "setf fn fast path atom" 42
  (let ((obj (make-instance 'setf-order-cls)))
    (setf (soc-x obj) 42)
    (soc-x obj)))
; (SETF fn) ordering regression (CLHS 5.1.1.1.1): atom place subforms must be
; evaluated BEFORE compound val_form, even when the latter has side effects that
; modify a symbol used as a place arg.  (setf (fn x) (setq x 99)) must call fn
; with the OLD value of x (1), not the post-setq value (99).
(defvar *setf-order-place-arg* nil)
(defun setf-order-probe (x) x)
(defun (setf setf-order-probe) (new-val place-arg)
  (setf *setf-order-place-arg* place-arg)
  new-val)
(check "setf fn order place-before-val" 1
  (progn
    (let ((x 1)) (setf (setf-order-probe x) (setq x 99)))
    *setf-order-place-arg*))
(check "char access" #\h (char "hello" 0))
(check "schar access" #\b (schar "abc" 1))
(check "string from sym" "FOO" (string 'foo))
(check "string from str" "hello" (string "hello"))
(check "string from char" "A" (string #\A))
(check "parse-integer" 42 (parse-integer "42"))
(check "parse-integer neg" -7 (parse-integer "-7"))
(check "parse-integer hex" 255 (parse-integer "FF" :radix 16))
(check "parse-integer ws" 123 (parse-integer "  123  "))
(check "write-to-string" "42" (write-to-string 42))
(check "princ-to-string" "hello" (princ-to-string "hello"))
(check "princ-to-string num" "42" (princ-to-string 42))
; PRINC (*print-escape* nil) of a pathname prints the bare namestring; PRIN1
; keeps the #P"..." reader syntax.  local-time's reread-timezone-repository
; takes substrings of (princ-to-string pathname), so #P"..." corrupted the
; zone-name keys and find-timezone-by-location-name returned NIL.
(check "princ-to-string pathname" "/foo/bar/UTC" (princ-to-string #P"/foo/bar/UTC"))
(check "prin1-to-string pathname" "#P\"/foo/bar/UTC\"" (prin1-to-string #P"/foo/bar/UTC"))
(check "format ~A pathname" "/foo/bar/UTC" (format nil "~A" #P"/foo/bar/UTC"))

; --- Phase 5 Tier 3: List utilities ---
(check "nthcdr 0" '(1 2 3) (nthcdr 0 '(1 2 3)))
(check "nthcdr 2" '(3) (nthcdr 2 '(1 2 3)))
(check "nthcdr past end" nil (nthcdr 5 '(1 2 3)))
(check "last" '(3) (last '(1 2 3)))
(check "last 2" '(2 3) (last '(1 2 3) 2))
(check "last nil" nil (last nil))
(check "acons" '((a . 1)) (acons 'a 1 nil))
(check "copy-list" '(1 2 3) (copy-list '(1 2 3)))
(check "copy-list nil" nil (copy-list nil))
(check "pairlis" '((a . 1) (b . 2)) (pairlis '(a b) '(1 2)))
(check "pairlis with alist" '((a . 1) (b . 2) (c . 3)) (pairlis '(a b) '(1 2) '((c . 3))))
(check "assoc found" '(b . 2) (assoc 'b '((a . 1) (b . 2) (c . 3))))
(check "assoc not found" nil (assoc 'd '((a . 1) (b . 2))))
(check "assoc with test" '("b" . 2) (assoc "b" (list (cons "a" 1) (cons "b" 2)) :test #'equal))
(check "rassoc found" '(b . 2) (rassoc 2 '((a . 1) (b . 2) (c . 3))))
(check "rassoc not found" nil (rassoc 9 '((a . 1) (b . 2))))
(check "getf found" 2 (getf '(:a 1 :b 2) :b))
(check "getf not found" nil (getf '(:a 1) :z))
(check "getf default" 99 (getf '(:a 1) :z 99))
(check "subst" '(a x (c x)) (subst 'x 'b '(a b (c b))))
(check "sublis" '(1 2 c) (sublis '((a . 1) (b . 2)) '(a b c)))
(check "adjoin new" '(1 2 3) (adjoin 1 '(2 3)))
(check "adjoin existing" '(1 2 3) (adjoin 2 '(1 2 3)))
(check "nconc" '(1 2 3 4) (nconc (list 1 2) (list 3 4)))
(check "nconc nil" '(1 2) (nconc nil (list 1 2)))
(check "nreverse" '(3 2 1) (nreverse (list 1 2 3)))
(check "nreverse nil" nil (nreverse nil))
(check "delete" '(1 3 4) (delete 2 (list 1 2 3 2 4)))
(check "delete not found" '(1 2 3) (delete 5 (list 1 2 3)))
(check "delete-if" '(1 2 3) (delete-if #'zerop (list 0 1 0 2 0 3)))
(check "nsubst" '(a x (c x)) (let ((x (list 'a 'b (list 'c 'b)))) (nsubst 'x 'b x)))
(check "butlast" '(1 2) (butlast '(1 2 3)))
(check "butlast 2" '(1) (butlast '(1 2 3) 2))
(check "butlast all" nil (butlast '(1 2 3) 5))
(check "copy-tree" '(1 (2 3) 4) (copy-tree '(1 (2 3) 4)))
(check "mapc returns list" '(1 2 3) (mapc (lambda (x) x) '(1 2 3)))
(check "mapcan" '(1 2 3) (mapcan (lambda (x) (if (numberp x) (list x) nil)) '(a 1 b 2 c 3)))
(check "maplist" '(4 3 2 1) (maplist #'length '(1 2 3 4)))
(check "mapl returns list" '(a b c) (mapl (lambda (l) l) '(a b c)))
(check "mapcon" '(3 2 1) (mapcon (lambda (l) (list (length l))) '(a b c)))
(check "intersection" '(2 4) (intersection '(1 2 3 4) '(2 4 6)))
(check "intersection empty" nil (intersection '(1 2) '(3 4)))
(check "union" '(3 2 1 4) (union '(1 2 3) '(2 3 4)))
(check "set-difference" '(1 3) (set-difference '(1 2 3 4) '(2 4)))
(check "set-difference empty" nil (set-difference '(1 2) '(1 2)))
(check "subsetp true" t (subsetp '(1 2) '(1 2 3)))
(check "subsetp false" nil (subsetp '(1 4) '(1 2 3)))
(check "subsetp empty" t (subsetp nil '(1 2 3)))

; --- Hash tables ---
(check "make-hash-table" t (hash-table-p (make-hash-table)))
(check "hash-table-p nil" nil (hash-table-p nil))
(check "hash-table-p number" nil (hash-table-p 42))
(check "type-of hash-table" 'hash-table (type-of (make-hash-table)))
(defparameter *ht-test* (make-hash-table))
(setf (gethash 'a *ht-test*) 1)
(setf (gethash 'b *ht-test*) 2)
(check "gethash found" 1 (gethash 'a *ht-test*))
(check "gethash found 2" 2 (gethash 'b *ht-test*))
(check "hash-table-count" 2 (hash-table-count *ht-test*))
(check "gethash not found" nil (gethash 'z *ht-test*))
(check "gethash default" 99 (gethash 'z *ht-test* 99))
(setf (gethash 'a *ht-test*) 10)
(check "gethash overwrite" 10 (gethash 'a *ht-test*))
(check "hash-table-count after overwrite" 2 (hash-table-count *ht-test*))
(check "remhash found" t (remhash 'a *ht-test*))
(check "gethash after remhash" nil (gethash 'a *ht-test*))
(check "hash-table-count after remhash" 1 (hash-table-count *ht-test*))
(check "remhash not found" nil (remhash 'z *ht-test*))
(clrhash *ht-test*)
(check "hash-table-count after clrhash" 0 (hash-table-count *ht-test*))
(defparameter *ht-sum* 0)
(defparameter *ht-map* (make-hash-table))
(setf (gethash 'x *ht-map*) 10)
(setf (gethash 'y *ht-map*) 20)
(maphash (lambda (k v) (setq *ht-sum* (+ *ht-sum* v))) *ht-map*)
(check "maphash sum" 30 *ht-sum*)
(defparameter *ht-eq* (make-hash-table :test 'eq))
(setf (gethash 'foo *ht-eq*) 99)
(check "hash-table eq test" 99 (gethash 'foo *ht-eq*))
(defparameter *ht-equal* (make-hash-table :test 'equal))
(setf (gethash "hello" *ht-equal*) 42)
(check "hash-table equal test string" 42 (gethash "hello" *ht-equal*))
(check "gethash mv present" t (multiple-value-bind (v p) (gethash 'x *ht-map*) p))
(check "gethash mv absent" nil (multiple-value-bind (v p) (gethash 'missing *ht-map*) p))
; --- equalp on hash tables (CLHS): same test, count, equalp values per key ---
; Regression: EQUALP used to fall back to identity for hash tables, breaking
; alexandria DEFINE-CONSTANT (babel) which re-evaluates constants under EQUALP.
(check "equalp ht same content" t
       (let ((a (make-hash-table)) (b (make-hash-table)))
         (dotimes (i 50) (setf (gethash i a) (* i i)) (setf (gethash i b) (* i i)))
         (equalp a b)))
(check "equalp ht diff value" nil
       (let ((a (make-hash-table)) (b (make-hash-table)))
         (dotimes (i 50) (setf (gethash i a) (* i i)) (setf (gethash i b) (* i i)))
         (setf (gethash 5 b) 999)
         (equalp a b)))
(check "equalp ht diff count" nil
       (let ((a (make-hash-table)) (b (make-hash-table)))
         (dotimes (i 50) (setf (gethash i a) i) (setf (gethash i b) i))
         (setf (gethash 99 b) 1)
         (equalp a b)))
(check "equalp ht diff test" nil
       (equalp (make-hash-table :test 'eql) (make-hash-table :test 'equal)))
(check "equalp ht equal-keys-and-values" t
       (let ((a (make-hash-table :test 'equal)) (b (make-hash-table :test 'equal)))
         (setf (gethash "foo" a) (list 1 2)) (setf (gethash "foo" b) (list 1 2))
         (equalp a b)))
(check "equalp ht empty" t (equalp (make-hash-table) (make-hash-table)))
; --- equal vs equalp on vectors/arrays (CLHS) ---
; EQUAL on general vectors is identity (NOT element-wise); EQUALP descends.
(check "equal genvec identity" nil (equal (vector 1 2 3) (vector 1 2 3)))
(check "equal genvec same obj" t (let ((v (vector 1 2 3))) (equal v v)))
(check "equalp genvec content" t (equalp (vector 1 2 3) (vector 1 2 3)))
(check "equalp genvec diff" nil (equalp (vector 1 2 3) (vector 1 2 4)))
; EQUALP compares vectors as sequences: by ACTIVE length (fill pointer counts)
; (ansi-test EQUALP.11 / REVERSE-VECTOR.6; matches SBCL/CLISP)
(check "equalp fillptr" t
       (equalp (make-array 5 :fill-pointer 2 :initial-contents '(1 2 3 4 5))
               (make-array 2 :initial-contents '(1 2)))) ; active (1 2) == (1 2)
; two size-5 arrays with different fill pointers -> different active lengths
(check "equalp fillptr same dim" nil
       (equalp (make-array 5 :fill-pointer 2 :initial-contents '(1 2 3 4 5))
               (make-array 5 :fill-pointer 3 :initial-contents '(1 2 3 4 5))))
; EQUALP on multidimensional arrays: same dims + equalp elements
(check "equalp 2d array" t (equalp #2a((1 2) (3 4)) #2a((1 2) (3 4))))
(check "equalp 2d array diff" nil (equalp #2a((1 2) (3 4)) #2a((1 2) (3 5))))
(check "equalp 2d vs 1d shape" nil (equalp #2a((1 2) (3 4)) #(1 2 3 4)))
; EQUALP across differing 1-D array specializations (CLHS: element type ignored).
; A string and a (vector character) of the same chars are EQUALP; likewise a
; bit-vector and a vector of the same bits.  (Regression: FIND with :test #'equalp
; over a list mixing strings and char vectors used to miss the vector.)
(check "equalp string vs charvec" t (equalp "ab" (vector #\a #\b)))
(check "equalp charvec vs string" t (equalp (vector #\a #\b) "ab"))
(check "equalp string vs charvec diff" nil (equalp "ab" (vector #\a #\c)))
(check "equalp string vs charvec len" nil (equalp "ab" (vector #\a #\b #\c)))
(check "equalp string vs charvec ci" t (equalp "AB" (vector #\a #\b)))
(check "equalp bitvec vs vec" t (equalp #*101 (vector 1 0 1)))
(check "equalp bitvec vs vec diff" nil (equalp #*101 (vector 1 1 1)))
(check "find equalp mixed list" t
       (equalp (find "ab" (list "a" (vector #\b #\a) (vector #\a #\b) (vector #\d #\e))
                     :test #'equalp)
               (vector #\a #\b)))

; --- sequence functions on a non-sequence signal TYPE-ERROR carrying :datum ---
; (Regression: these used to raise a bare type-error with NIL :datum, failing
;  the ANSI check-type-error tests.)
(defun seq-type-error-datum (thunk)
  (handler-case (progn (funcall thunk) :no-error)
    (type-error (c) (type-error-datum c))))
(check "count non-seq datum" 7 (seq-type-error-datum (lambda () (count 'a 7))))
(check "find non-seq datum" 7 (seq-type-error-datum (lambda () (find 'a 7))))
(check "reverse non-seq datum" 7 (seq-type-error-datum (lambda () (reverse 7))))
(check "copy-seq non-seq datum" 7 (seq-type-error-datum (lambda () (copy-seq 7))))
(check "remove non-seq datum" 7 (seq-type-error-datum (lambda () (remove 'a 7))))
(check "sort non-seq datum" 7 (seq-type-error-datum (lambda () (sort 7 #'<))))
(check "reduce non-seq datum" 7 (seq-type-error-datum (lambda () (reduce #'+ 7))))

; --- repeated keyword: leftmost pair wins (CLHS 3.4.1.4) ---
(check "reduce dup from-end" '(1 2 3)
       (reduce #'cons '(1 2 3) :from-end t :from-end nil
               :initial-value nil :initial-value 'a))
(check "mismatch dup test" 2
       (mismatch "1234" "1244" :test #'equal :test (complement #'equal)))
(check "search dup keys" 4
       (search '(c d) '(a b c d c d e) :start1 0 :start2 0 :start1 1
               :start2 6 :from-end t :from-end nil))
(check "replace dup keys" "xyzdefg"
       (replace (copy-seq "abcdefg") "xyz" :start1 0 :start2 0 :end1 3 :end2 3
                :start1 1 :start2 1 :end1 2 :end1 2))

; --- MAP-INTO: function called exactly once per element; fill pointer set ---
(check "map-into list once" '(6 5 4 3 2 1)
       (let ((a (copy-seq '(a b c d e f))) (b nil))
         (map-into a (lambda (x) (push x b) x) '(1 2 3 4 5 6))
         b))
(check "map-into fillptr count" 2
       (let ((a (make-array 6 :initial-element 'x :fill-pointer 3)))
         (map-into a #'identity '(1 2))
         (fill-pointer a)))
(check "map-into fillptr grows" 5
       (let ((a (make-array 6 :initial-element 'x :fill-pointer 3)))
         (map-into a #'identity '(1 2 3 4 5))
         (fill-pointer a)))

; --- MAP result-type validation is a TYPE-ERROR (not program-error) ---
(check "map bad result-type" t
       (handler-case (progn (map 'symbol #'identity '(a b c)) nil)
         (type-error () t)))
(check "map result-type length mismatch" t
       (handler-case (progn (map '(vector * 8) #'identity '(a b c)) nil)
         (type-error () t)))

; --- COERCE to a numeric type the object already satisfies is the identity ---
(check "coerce 0 rational" 0 (coerce 0 'rational))
(check "coerce ratio rational" 1/2 (coerce 1/2 'rational))
(check "coerce 5 real" 5 (coerce 5 'real))

; --- REMOVE :count as a (bignum) negative integer is treated as zero ---
(check "remove negative bignum count" '(1 2 3 2 6 1 2 4 1 3 2 7)
       (remove 3 (copy-seq '(1 2 3 2 6 1 2 4 1 3 2 7))
               :count -1000000000000))
; --- compiler use-after-free regression (cl_compile_env protect flag) ---
; A macro performing a non-local exit during its own expansion, used inside
; compiled lambda-bearing forms; pre-fix the NLX unwind freed the in-progress
; compiler env (use-after-free in cl_env_lookup_local_macro). Verify the
; runtime stays healthy and keeps compiling correctly.
(defmacro nlx-expand-mac (x)
  (handler-case (error "nlx during expand") (error () nil))
  (list '+ x 1))
(dotimes (i 40)
  (eval `(defun ,(intern (format nil "NLX-USE-~D" i)) (a)
           (let ((b a))
             (list (nlx-expand-mac b) (lambda (y) (+ y b)))))))
(check "compile-env nlx macro no-uaf 1" 11 (first (nlx-use-7 10)))
(check "compile-env nlx macro no-uaf 2" 15 (funcall (second (nlx-use-7 10)) 5))
(check "compile-env nlx macro still-healthy" 84
       (progn (defun nlx-after (p q) (let ((s (* p q))) (+ s s))) (nlx-after 6 7)))

; --- Sequence functions ---
(defun my-evenp (x) (= (mod x 2) 0))

; find
(check "find basic" 3 (find 3 '(1 2 3 4 5)))
(check "find not found" nil (find 9 '(1 2 3)))
(check "find with start" 2 (find 2 '(1 2 3 2 1) :start 2))
(check "find with test" "b" (find "b" '("a" "b" "c") :test #'equal))
(check "find from-end" 3 (find 3 '(1 3 2 3 4) :from-end t))

; find-if, find-if-not
(check "find-if" 2 (find-if #'my-evenp '(1 2 3 4)))
(check "find-if not found" nil (find-if #'my-evenp '(1 3 5)))
(check "find-if-not" 5 (find-if-not #'my-evenp '(2 4 5 6)))

; position
(check "position basic" 2 (position 3 '(1 2 3 4)))
(check "position not found" nil (position 9 '(1 2 3)))
(check "position from-end" 2 (position 1 '(1 2 1 2) :from-end t))

; position-if, position-if-not
(check "position-if" 2 (position-if #'my-evenp '(1 3 4 5)))
(check "position-if-not" 2 (position-if-not #'my-evenp '(2 4 5)))

; count
(check "count basic" 3 (count 1 '(1 2 1 3 1)))
(check "count none" 0 (count 9 '(1 2 3)))
(check "count with start" 2 (count 1 '(1 2 1 3 1) :start 1))

; count-if, count-if-not
(check "count-if" 3 (count-if #'my-evenp '(1 2 3 4 5 6)))
(check "count-if-not" 3 (count-if-not #'my-evenp '(1 2 3 4 5)))

; remove
(check "remove basic" '(1 2 4 5) (remove 3 '(1 2 3 4 3 5)))
(check "remove count" '(1 2 4 3 5) (remove 3 '(1 2 3 4 3 5) :count 1))
(check "remove not found" '(1 2 3) (remove 9 '(1 2 3)))
(check "remove from-end count" '(1 2 3 4 5) (remove 3 '(1 2 3 4 3 5) :count 1 :from-end t))

; remove-if, remove-if-not
(check "remove-if" '(1 3 5) (remove-if #'my-evenp '(1 2 3 4 5)))
(check "remove-if-not" '(2 4) (remove-if-not #'my-evenp '(1 2 3 4 5)))

; remove on a non-sequence must signal type-error, not crash (ansi-test
; REMOVE.ERROR.11: (remove 'a x) over a type universe including non-sequences).
; Previously remove_from_vector dereferenced the argument as a CL_Vector.
(check "remove non-sequence number" 'te
  (handler-case (remove 'a 5) (type-error () 'te) (error () 'wrong)))
(check "remove non-sequence symbol" 'te
  (handler-case (remove 'a 'foo) (type-error () 'te) (error () 'wrong)))
(check "remove-if non-sequence" 'te
  (handler-case (remove-if #'my-evenp 5) (type-error () 'te) (error () 'wrong)))
(check "remove-if-not non-sequence" 'te
  (handler-case (remove-if-not #'my-evenp 'foo) (type-error () 'te) (error () 'wrong)))

; A non-symbol in keyword-argument position must signal program-error, not
; crash (ansi-test MAKE-SEQUENCE.ERROR.13: (make-sequence 'list 10 0 0)).
; Previously the integer key was fed to cl_symbol_name -> garbage "%s".
(check "non-symbol keyword call" 'pe
  (handler-case (funcall (lambda (&key foo) foo) 0 0)
    (program-error () 'pe) (error () 'wrong)))
(check "non-symbol keyword apply" 'pe
  (handler-case (apply (lambda (&key foo) foo) '(0 0))
    (program-error () 'pe) (error () 'wrong)))
(check "non-symbol keyword make-sequence" 'pe
  (handler-case (make-sequence 'list 10 0 0)
    (program-error () 'pe) (error () 'wrong)))

; remove/remove-if/remove-if-not on bit-vectors
(check "remove 0 bv" "#*111" (format nil "~A" (remove 0 #*10101)))
(check "remove 1 bv" "#*00" (format nil "~A" (remove 1 #*10101)))
(check "remove-if bv" "#*111" (format nil "~A" (remove-if #'zerop #*10101)))
(check "remove-if-not bv" "#*111"
  (format nil "~A" (remove-if-not (lambda (b) (= b 1)) #*10101)))
(check "remove bv type" t (bit-vector-p (remove 0 #*101)))
(check "remove bv empty" "#*" (format nil "~A" (remove 0 #*)))
(check "remove bv all" "#*" (format nil "~A" (remove 1 #*1111)))
(check "remove bv none" "#*1111" (format nil "~A" (remove 0 #*1111)))

; remove-duplicates
(check "remove-duplicates" '(1 3 2 4) (remove-duplicates '(1 2 1 3 2 4)))
(check "remove-dup symbols" '(b a c) (remove-duplicates '(a b a c)))

; substitute
(check "substitute basic" '(1 2 99 4 99) (substitute 99 3 '(1 2 3 4 3)))
(check "substitute count" '(1 2 0 4 3) (substitute 0 3 '(1 2 3 4 3) :count 1))

; substitute-if, substitute-if-not
(check "substitute-if" '(1 0 3 0 5) (substitute-if 0 #'my-evenp '(1 2 3 4 5)))
(check "substitute-if-not" '(0 2 0 4 0) (substitute-if-not 0 #'my-evenp '(1 2 3 4 5)))

; nsubstitute
(check "nsubstitute basic" '(1 2 99 4 99) (let ((x (list 1 2 3 4 3))) (nsubstitute 99 3 x) x))
(check "nsubstitute count" '(1 2 0 4 3) (let ((x (list 1 2 3 4 3))) (nsubstitute 0 3 x :count 1) x))
(check "nsubstitute-if" '(1 0 3 0 5) (let ((x (list 1 2 3 4 5))) (nsubstitute-if 0 #'my-evenp x) x))
(check "nsubstitute-if-not" '(0 2 0 4 0) (let ((x (list 1 2 3 4 5))) (nsubstitute-if-not 0 #'my-evenp x) x))

; reduce
(check "reduce sum" 10 (reduce #'+ '(1 2 3 4)))
(check "reduce empty init" 0 (reduce #'+ '() :initial-value 0))
(check "reduce single init" 15 (reduce #'+ '(5) :initial-value 10))
(check "reduce cons" '((1 . 2) . 3) (reduce #'cons '(1 2 3)))

; fill
(defparameter *fill-list* (list 1 2 3 4))
(fill *fill-list* 0)
(check "fill all" '(0 0 0 0) *fill-list*)
(defparameter *fill-list2* (list 1 2 3 4))
(fill *fill-list2* 0 :start 1 :end 3)
(check "fill range" '(1 0 0 4) *fill-list2*)

; replace
(defparameter *repl-list* (list 1 2 3 4 5))
(replace *repl-list* '(a b c) :start1 1)
(check "replace" '(1 a b c 5) *repl-list*)
; replace into a typed sequence must enforce the element type (CLHS): storing a
; non-character into a string or a non-bit into a bit-vector is a type-error,
; not a silent drop.
(check "replace string non-char type-error" t
       (handler-case
           (progn (replace (make-array 3 :element-type 'character :initial-element #\Space)
                           '(foo bar))
                  nil)
         (type-error () t)))
(check "replace bit-vector non-bit type-error" t
       (handler-case
           (progn (replace (make-array 3 :element-type 'bit :initial-element 0) '(2))
                  nil)
         (type-error () t)))
(check "replace string char ok" "ab_"
       (replace (copy-seq "___") "ab"))

; every, some, notany, notevery
(check "every true" t (every #'my-evenp '(2 4 6)))
(check "every false" nil (every #'my-evenp '(2 3 6)))
(check "every empty" t (every #'my-evenp '()))
(check "some true" t (some #'my-evenp '(1 3 4)))
(check "some false" nil (some #'my-evenp '(1 3 5)))
(check "notany true" t (notany #'my-evenp '(1 3 5)))
(check "notany false" nil (notany #'my-evenp '(1 2 3)))
(check "notevery true" t (notevery #'my-evenp '(2 4 5)))
(check "notevery false" nil (notevery #'my-evenp '(2 4 6)))

; map
(check "map list" '(2 3 4) (map 'list #'1+ '(1 2 3)))

; mismatch
(check "mismatch none" nil (mismatch '(1 2 3) '(1 2 3)))
(check "mismatch at 2" 2 (mismatch '(1 2 3) '(1 2 4)))
(check "mismatch length" 2 (mismatch '(1 2) '(1 2 3)))

; search
(check "search found" 1 (search '(2 3) '(1 2 3 4)))
(check "search not found" nil (search '(9) '(1 2 3)))
(check "search empty" 0 (search '() '(1 2 3)))

; sort
(check "sort asc" '(1 1 3 4 5) (sort (list 3 1 4 1 5) #'<))
(check "sort desc" '(5 4 3 2 1) (sort (list 1 2 3 4 5) #'>))
(check "sort single" '(1) (sort (list 1) #'<))
(check "sort empty" nil (sort '() #'<))
(check "sort with key" '(1 -2 -3 4) (sort (list -3 1 -2 4) #'< :key #'abs))
; Regression: sort with allocating key on large list (GC bug #8)
(check "sort large+key" 200
  (length (sort (loop for i from 1 to 200 collect (format nil "item~3,'0d" i)) #'string<)))

; stable-sort
(check "stable-sort" '(1 2 3) (stable-sort (list 3 1 2) #'<))

; find/position with :key
(check "find with key" '(2 b) (find 2 '((1 a) (2 b) (3 c)) :key #'car))
(check "position with key" 1 (position 2 '((1 a) (2 b) (3 c)) :key #'car))
(check "count with key" 2 (count 1 '((1 a) (2 b) (1 c)) :key #'car))

; remove with :key
(check "remove with key" '((2 b) (3 c)) (remove 1 '((1 a) (2 b) (3 c)) :key #'car))

; every/some with multiple lists
(check "every 2 lists" t (every #'< '(1 2 3) '(2 3 4)))
(check "some 2 lists" t (some #'< '(3 2 1) '(1 2 3)))

; every/some/notany/notevery with strings
(check "every string t" t (every #'alpha-char-p "hello"))
(check "every string nil" nil (every #'alpha-char-p "hello1"))
; CLHS some: returns the first non-NIL predicate result, not boolean T
(check "some string t" 1 (some #'digit-char-p "abc1"))
(check "some string nil" nil (some #'digit-char-p "abcd"))
(check "notany string t" t (notany #'digit-char-p "abcd"))
(check "notany string nil" nil (notany #'digit-char-p "abc1"))
(check "notevery str t" t (notevery #'alpha-char-p "hello1"))
(check "notevery str nil" nil (notevery #'alpha-char-p "hello"))

; every/some/notany/notevery with vectors
(check "every vector t" t (every #'numberp #(1 2 3)))
(check "every vector nil" nil (every #'numberp #(1 :a 3)))
(check "some vector t" t (some #'zerop #(1 2 0 3)))
(check "some vector nil" nil (some #'zerop #(1 2 3)))
(check "notany vector t" t (notany #'zerop #(1 2 3)))
(check "notany vector nil" nil (notany #'zerop #(1 0 3)))
(check "notevery vec t" t (notevery #'numberp #(1 :a 3)))
(check "notevery vec nil" nil (notevery #'numberp #(1 2 3)))

; every/some with empty sequences
(check "every empty str" t (every #'identity ""))
(check "every empty vec" t (every #'identity #()))
(check "some empty str" nil (some #'identity ""))
(check "some empty vec" nil (some #'identity #()))

; every/some with bit-vectors
(check "every bv t" t (every #'zerop #*000))
(check "every bv nil" nil (every #'zerop #*010))
(check "some bv t" t (some #'plusp #*010))
(check "some bv nil" nil (some #'plusp #*000))

; map over strings and vectors
(check "map str" '(97 98 99) (map 'list #'char-code "abc"))
(check "map vec" '(11 21 31) (map 'list #'1+ #(10 20 30)))
(check "map mixed" '(11 22 33) (map 'list #'+ '(1 2 3) #(10 20 30)))
(check "map str result" "HELLO" (map 'string #'char-upcase "hello"))
; EQUAL on general vectors is identity (CLHS); compare contents with EQUALP.
(check "map vec result" t (equalp #(2 3 4) (map 'vector #'1+ '(1 2 3))))
(check "map shortest" '(11 22) (map 'list #'+ '(1 2 3 4) #(10 20)))
(check "map simple-vector" t (equalp #(2 3 4) (map 'simple-vector #'1+ '(1 2 3))))
(check "map simple-string" "HELLO" (map 'simple-string #'char-upcase "hello"))
(check "map cons" '(2 3 4) (map 'cons #'1+ '(1 2 3)))

; reduce with key
(check "reduce with key" 6 (reduce #'+ '((1 a) (2 b) (3 c)) :key #'car))

; --- Type system: typep ---
(check "typep integer" t (typep 42 'integer))
(check "typep fixnum" t (typep 42 'fixnum))
(check "typep number" t (typep 42 'number))
(check "typep not string" nil (typep 42 'string))
(check "typep string" t (typep "hello" 'string))
(check "typep character" t (typep #\A 'character))
(check "typep null" t (typep nil 'null))
(check "typep nil symbol" t (typep nil 'symbol))
(check "typep nil list" t (typep nil 'list))
(check "typep cons" t (typep '(1 2) 'cons))
(check "typep list" t (typep '(1 2) 'list))
(check "typep atom int" t (typep 42 'atom))
(check "typep atom cons" nil (typep '(1) 'atom))
(check "typep keyword" t (typep :foo 'keyword))
(check "typep not keyword" nil (typep 'bar 'keyword))
(check "typep function" t (typep #'+ 'function))
(check "typep hash-table" t (typep (make-hash-table) 'hash-table))
(check "typep t" t (typep 42 't))
(check "typep nil-type" nil (typep 42 'nil))
(check "typep sequence list" t (typep '(1 2) 'sequence))
(check "typep sequence vec" t (typep (vector 1 2) 'sequence))
(check "typep vector" t (typep (vector 1 2) 'vector))
(check "typep compiled-function" t (typep (lambda (x) x) 'compiled-function))

; --- Type system: coerce ---
(check "coerce t" 42 (coerce 42 't))
(check "coerce int->char" #\A (coerce 65 'character))
(check "coerce char->int" 65 (coerce #\A 'integer))
(check "coerce char identity" #\A (coerce #\A 'character))
(check "coerce int identity" 42 (coerce 42 'integer))
(check "coerce sym->string" "FOO" (coerce 'foo 'string))
(check "coerce char->string" "A" (coerce #\A 'string))
(check "coerce string identity" "hello" (coerce "hello" 'string))
(check "coerce list->vector" t (equalp (coerce '(1 2 3) 'vector) (vector 1 2 3)))
(check "coerce vector->list" '(1 2 3) (coerce (vector 1 2 3) 'list))
(check "coerce nil->list" nil (coerce nil 'list))
(check "coerce nil->vector" t (equalp (coerce nil 'vector) (vector)))
; A BIT element type in (vector/array bit ...) must yield a bit-vector
(check "coerce list->(vector bit N)" t (bit-vector-p (coerce '(1 0 1) '(vector bit 3))))
(check "coerce list->(vector bit)" t (bit-vector-p (coerce '(1 0 1) '(vector bit))))
(check "coerce list->(array bit (N))" t (bit-vector-p (coerce '(1 0 1) '(array bit (3)))))
(check "coerce (vector bit) value" #*10111100 (coerce '(1 0 1 1 1 1 0 0) '(vector bit 8)))
(check "coerce (vector bit) equal lit" t (equal (coerce '(1 0 1) '(vector bit 3)) #*101))
(check "coerce (vector fixnum) stays general" t
       (not (bit-vector-p (coerce '(1 2 3) '(vector fixnum 3)))))

; --- Type system: compound typep ---
(check "typep or match int" t (typep 42 '(or integer string)))
(check "typep or match str" t (typep "hi" '(or integer string)))
(check "typep or no match" nil (typep #\A '(or integer string)))
(check "typep and match" t (typep nil '(and symbol list)))
(check "typep and no match" nil (typep 42 '(and number string)))
(check "typep not match" t (typep 42 '(not string)))
(check "typep not no match" nil (typep "hi" '(not string)))
(check "typep member match" t (typep 1 '(member 1 2 3)))
(check "typep member no match" nil (typep 4 '(member 1 2 3)))
(check "typep eql match" t (typep 42 '(eql 42)))
(check "typep eql no match" nil (typep 43 '(eql 42)))
(check "typep satisfies match" t (typep 42 '(satisfies numberp)))
(check "typep satisfies no" nil (typep "hi" '(satisfies numberp)))
(check "typep nested compound" t (typep 42 '(or (and integer atom) string)))

; --- Type system: deftype ---
(deftype my-num () 'number)
(check "deftype basic match" t (typep 42 'my-num))
(check "deftype basic no" nil (typep "hi" 'my-num))
(deftype str-or-num () '(or string number))
(check "deftype compound match int" t (typep 42 'str-or-num))
(check "deftype compound match str" t (typep "hi" 'str-or-num))
(check "deftype compound no" nil (typep #\A 'str-or-num))
; Several deftypes in sequence: the type NAME must register as the canonical
; interned symbol so SUBTYPEP / UPGRADED-ARRAY-ELEMENT-TYPE resolve the expander.
; (Host gc-stress regression for the compile_deftype name-protection fix; here it
; is the functional check that the expander is found for each definition.)
(deftype my-char () 'character)
(deftype my-u8 () '(unsigned-byte 8))
(deftype my-int () 'integer)
(check "deftype my-char subtypep character" t (and (subtypep 'my-char 'character) t))
(check "deftype my-u8 subtypep integer"     t (and (subtypep 'my-u8 'integer) t))
(check "deftype my-int subtypep integer"    t (and (subtypep 'my-int 'integer) t))
(check "deftype upgraded-array-element-type" 'character
       (upgraded-array-element-type 'my-char))

; --- Type system: condition/deftype table hash index ---
; TYPEP resolves symbol type specs through hash indexes over the condition
; hierarchy and the deftype table (CL_AlistIndex).  Three index-specific
; hazards: (1) a compacting GC relocates the key symbols, so a lookup after
; (gc) must hit the rebuilt index, not stale offsets; (2) redefinition
; prepends, and the index must preserve the linear walk's head-most-wins
; semantics; (3) a fresh definition made AFTER the index was built must be
; visible immediately (registration dirty-marks the index).
(deftype %ix-shadowed () 'integer)
(check "index deftype pre-gc" t (typep 5 '%ix-shadowed))
(gc) (gc)
(check "index deftype survives compaction" t (typep 5 '%ix-shadowed))
(deftype %ix-shadowed () 'string)
(check "index deftype redefinition wins (pos)" t (typep "s" '%ix-shadowed))
(check "index deftype redefinition wins (neg)" nil (typep 5 '%ix-shadowed))
(define-condition %ix-cond (error) ())
(check "index condition visible after define" t
       (typep (make-condition '%ix-cond) '%ix-cond))
(gc)
(check "index condition survives compaction" t
       (typep (make-condition '%ix-cond) 'error))
(check "index non-condition symbol misses cleanly" nil
       (typep 42 '%ix-cond))

; CLAMIGA::%TYPE-EXPANDER exposes the deftype expander table to Lisp so a
; portable TYPEXPAND can resolve user deftype aliases (introspect-environment
; shim → serapeum EXPLODE-TYPE).  A built-in type name has no expander; an
; atom alias's expander takes no args; a parameterized alias is applied to
; the compound type's args.
(check "%type-expander builtin is nil" nil (clamiga::%type-expander 'list))
(check "%type-expander non-symbol is nil" nil (clamiga::%type-expander 42))
(deftype %tx-disj () '(or string number))
(check "%type-expander atom alias expands" '(or string number)
       (funcall (clamiga::%type-expander '%tx-disj)))
(deftype %tx-range (lo hi) `(integer ,lo ,hi))
(check "%type-expander parameterized alias expands" '(integer 0 9)
       (funcall (clamiga::%type-expander '%tx-range) 0 9))

; Integer subtypes of BIT upgrade to a bit-vector (serapeum RANGE/SBIT regression).
(check "uaet (integer 0 1)" 'bit (upgraded-array-element-type '(integer 0 1)))
(check "uaet (integer 0 (1))" 'bit (upgraded-array-element-type '(integer 0 (1))))
(check "uaet (unsigned-byte 1)" 'bit (upgraded-array-element-type '(unsigned-byte 1)))
(check "uaet (mod 2)" 'bit (upgraded-array-element-type '(mod 2)))
(check "uaet (unsigned-byte 8)" '(unsigned-byte 8)
       (upgraded-array-element-type '(unsigned-byte 8)))
(check "uaet (unsigned-byte 16) packs" '(unsigned-byte 16)
       (upgraded-array-element-type '(unsigned-byte 16)))
(check "uaet (unsigned-byte 17) stays t" t
       (upgraded-array-element-type '(unsigned-byte 17)))
(check "make-array bit-subtype is bit-vector" t
       (bit-vector-p (make-array 3 :element-type '(integer 0 (1)))))
(check "make-array bit-subtype sbit works" #*100
       (let ((b (make-array 3 :element-type '(integer 0 (1))))) (setf (sbit b 0) 1) b))
; (simple-array ET (*)) honors the element type — a T vector is not a bit array.
(check "typep T-vec (simple-array bit)" nil (typep (vector 1 2) '(simple-array bit (*))))
(check "typep T-vec (simple-array t)" t (typep (vector 1 2) '(simple-array t (*))))
(check "typep bit-vec (simple-array bit)" t (typep #*101 '(simple-array bit (*))))
(check "typep bit-vec (simple-array t)" nil (typep #*101 '(simple-array t (*))))
(check "typep string (simple-array character)" t (typep "ab" '(simple-array character (*))))
(check "typep string (simple-array t)" nil (typep "ab" '(simple-array t (*))))

; --- Type system: subtypep ---
(check "subtypep fixnum<number" t (subtypep 'fixnum 'number))
(check "subtypep fixnum<integer" t (subtypep 'fixnum 'integer))
(check "subtypep integer<number" t (subtypep 'integer 'number))
(check "subtypep cons<list" t (subtypep 'cons 'list))
(check "subtypep null<list" t (subtypep 'null 'list))
(check "subtypep list<sequence" t (subtypep 'list 'sequence))
(check "subtypep keyword<symbol" t (subtypep 'keyword 'symbol))
(check "subtypep compiled-fn<fn" t (subtypep 'compiled-function 'function))
(check "subtypep same type" t (subtypep 'integer 'integer))
(check "subtypep not subtype" nil (subtypep 'string 'number))
(check "subtypep nil<anything" t (subtypep 'nil 'number))
(check "subtypep anything<t" t (subtypep 'number 't))
(check "subtypep mv second val" '(t t) (multiple-value-list (subtypep 'fixnum 'number)))
(check "subtypep mv not sub" '(nil t) (multiple-value-list (subtypep 'string 'number)))

; --- subtypep: bounded float/real/rational ranges (serapeum random-range-type) ---
(check "subtypep sf-range<number" '(t t)
       (multiple-value-list (subtypep '(single-float 0.0 (10.0)) 'number)))
(check "subtypep sf-range<float" '(t t)
       (multiple-value-list (subtypep '(single-float 0.0 (10.0)) 'float)))
(check "subtypep df-range<real" '(t t)
       (multiple-value-list (subtypep '(double-float 0.0d0 (10.0d0)) 'real)))
(check "subtypep ratl-range<number" '(t t)
       (multiple-value-list (subtypep '(rational 0 (10)) 'number)))
; no false positives
(check "subtypep sf-not<sf-range" nil
       (nth-value 0 (subtypep 'single-float '(single-float 0.0 1.0))))

; --- subtypep: (not B) overlap case (serapeum soft-list-of) ---
(check "subtypep null<(not null)" '(nil t)
       (multiple-value-list (subtypep 'null '(not null))))
(check "subtypep fixnum<(not integer)" '(nil t)
       (multiple-value-list (subtypep 'fixnum '(not integer))))
(check "subtypep integer<(not null) uncertain" '(nil nil)
       (multiple-value-list (subtypep 'integer '(not null))))

; --- subtypep: element-less (member) is the empty type (serapeum explode-type) ---
(check "subtypep (member)<nil" '(t t)
       (multiple-value-list (subtypep '(member) nil)))
(check "subtypep nil<(member)" '(t t)
       (multiple-value-list (subtypep 'nil '(member))))
(check "subtypep (member)<(member)" '(t t)
       (multiple-value-list (subtypep '(member) '(member))))
(check "subtypep (member)<integer" '(t t)
       (multiple-value-list (subtypep '(member) 'integer)))
(check "subtypep integer<(member)" '(nil t)
       (multiple-value-list (subtypep 'integer '(member))))
(check "typep :x (member)" nil (typep :x '(member)))

; --- subtypep: zero-disjunct (or) is also the empty type ---
; CLHS type-specifier OR: (or) with no disjuncts denotes the empty type (≡ NIL).
; tspec_is_empty must recognise (or) the same way it does (member).
; The old compound-OR handler returned (NIL T) for (subtypep 'nil '(or)).
(check "subtypep (or)<nil" '(t t)
       (multiple-value-list (subtypep '(or) nil)))
(check "subtypep nil<(or)" '(t t)
       (multiple-value-list (subtypep 'nil '(or))))
(check "subtypep (or)<(or)" '(t t)
       (multiple-value-list (subtypep '(or) '(or))))
(check "subtypep (or)<integer" '(t t)
       (multiple-value-list (subtypep '(or) 'integer)))
(check "subtypep integer<(or)" '(nil t)
       (multiple-value-list (subtypep 'integer '(or))))
(check "subtypep (member)<(or)" '(t t)
       (multiple-value-list (subtypep '(member) '(or))))
(check "subtypep (or)<(member)" '(t t)
       (multiple-value-list (subtypep '(or) '(member))))

; --- Type system: typecase with compound ---
(check "typecase or int" "match" (typecase 42 ((or integer string) "match") (t "no")))
(check "typecase or str" "match" (typecase "hi" ((or integer string) "match") (t "no")))
(check "typecase or no" "no" (typecase #\A ((or integer string) "match") (t "no")))
(check "typecase list nil" "list" (typecase nil (list "list") (t "other")))

; --- Disassemble ---
(defun disasm-test-fn (x) (+ x 1))
(check "disassemble defun" nil (disassemble 'disasm-test-fn))
(check "disassemble closure" nil (disassemble (lambda (x) (+ x 1))))
(check "disassemble builtin" nil (disassemble 'cons))

; --- Disassemble stream redirection ---
(check "disassemble builtin to *standard-output*"
       t
       (let ((out (with-output-to-string (*standard-output*)
                    (disassemble 'car))))
         (and (search "Built-in function:" out) (search "CAR" out) t)))

(defun disasm-stream-redir-fn (x) (+ x 1))
(check "disassemble defun to *standard-output*"
       t
       (let ((out (with-output-to-string (*standard-output*)
                    (disassemble 'disasm-stream-redir-fn))))
         (and (search "Disassembly of" out)
              (search "DISASM-STREAM-REDIR-FN" out)
              (or (search "OP_" out) (search "RET" out))
              t)))

; --- Declarations: declare, declaim, proclaim, locally ---
(declaim (special *dcl-x*))
(setq *dcl-x* 10)
(defun get-dcl-x () *dcl-x*)
(check "declaim special" 99 (let ((*dcl-x* 99)) (get-dcl-x)))

(setq *dcl-ds* 10)
(defun get-dcl-ds () *dcl-ds*)
(check "declare special let" 42 (let ((*dcl-ds* 42)) (declare (special *dcl-ds*)) (get-dcl-ds)))

(check "declare ignore" 42 (let ((x 1)) (declare (ignore x)) 42))
(check "declare type" 1 (let ((x 1)) (declare (type fixnum x)) x))
(check "declaim optimize" nil (declaim (optimize (speed 3))))
(check "declaim inline" nil (declaim (inline cons)))

(proclaim '(special *dcl-p*))
(setq *dcl-p* 100)
(defun get-dcl-p () *dcl-p*)
(check "proclaim special" 200 (let ((*dcl-p* 200)) (get-dcl-p)))

(check "locally basic" 3 (locally 1 2 3))
(check "locally declare" 5 (locally (declare (special *dcl-lv*)) (setq *dcl-lv* 5) *dcl-lv*))

(check "declare in lambda" 42 ((lambda (x) (declare (ignore x)) 42) 99))

(declaim (special *dcl-m1*) (special *dcl-m2*))
(setq *dcl-m1* 1)
(setq *dcl-m2* 2)
(defun get-dcl-m1 () *dcl-m1*)
(defun get-dcl-m2 () *dcl-m2*)
(check "declaim multi 1" 10 (let ((*dcl-m1* 10)) (get-dcl-m1)))
(check "declaim multi 2" 20 (let ((*dcl-m2* 20)) (get-dcl-m2)))

; --- Trace/Untrace ---
(defun tr-fact (n) (if (<= n 1) 1 (* n (tr-fact (1- n)))))
(trace tr-fact)
(check "trace result" 120 (tr-fact 5))
(untrace tr-fact)
(check "untrace result" 6 (tr-fact 3))
(defun tr-bar () 42)
(check "trace returns list" '(tr-bar) (trace tr-bar))
(check "trace query" '(tr-bar) (trace))
(check "untrace all" nil (untrace))
(check "trace query empty" nil (trace))
(trace cons)
(check "trace builtin" '(1 . 2) (cons 1 2))
(untrace cons)
(defun tr-p2 (x) (* x x))
(defun tr-q2 (x) (+ x 1))
(trace tr-p2 tr-q2)
(check "trace multiple" 16 (tr-p2 (tr-q2 3)))
(untrace)

; --- Trace stream redirection ---
(defun tr-stream-fn (x) (* x x))
(trace tr-stream-fn)
(let* ((s (make-string-output-stream))
       (captured (progn (let ((*trace-output* s)) (tr-stream-fn 5))
                        (get-output-stream-string s))))
  (check "trace stream has fn name" t (not (null (search "TR-STREAM-FN" captured))))
  (check "trace stream has returned" t (not (null (search "returned" captured)))))
(untrace tr-stream-fn)
(let* ((s (make-string-output-stream))
       (captured (progn (let ((*trace-output* s)) (tr-stream-fn 5))
                        (get-output-stream-string s))))
  (check "trace stream silent after untrace" "" captured))

;; Regression: creating MANY string-output-streams in a loop fills the
;; internal outbuf table; that path grows the table (and, at the growth
;; ceiling, runs a GC to reclaim dead streams) and must not sweep/relocate
;; the not-yet-rooted stream being built (was returned dangling ->
;; "argument is not a stream").  Each newly created stream must stay a
;; valid, writable string stream throughout.
(check "string-output-stream survives table-full GC churn" t
       (let ((all-ok t))
         (dotimes (i 800)
           (let ((s (make-string-output-stream)))
             (unless (streamp s) (setf all-ok nil))
             (write-string "abc" s)
             (unless (string= "abc" (get-output-stream-string s))
               (setf all-ok nil))))
         all-ok))

;; Regression: a LIVE string-output-stream's content must survive a GC.  The
;; outbuf side table recycles a handle the moment a dead stream's slot is freed;
;; the GC used to free outbufs from each dead stream's stale out_buf_handle, so
;; if a live stream had recycled that handle, re-finalizing the dead corpse
;; freed the live stream's buffer — its accumulated text silently vanished
;; (observed as chunga READ-LINE* returning "" for a non-empty HTTP header under
;; load, desyncing drakma's framing).  Each iteration builds a string in its own
;; WITH-OUTPUT-TO-STRING — reusing the outbuf handle the previous iteration's
;; CLOSE just freed — and forces a sweep GC (EXT:GC, no compaction) WHILE the
;; stream is live and half-built, so the reclaim runs at exactly the moment the
;; dead corpse would have freed this live stream's recycled buffer.  The string
;; must come back intact.  (The host C test
;; outbuf_live_survives_dead_stream_sharing_handle is the deterministic guard;
;; this is the on-Amiga coverage.  Allocation is kept light on purpose: heavy
;; compaction churn here trips a separate, pre-existing moving-GC bug.)
(check "string-output-stream content survives outbuf reclaim under GC" 0
       (let ((fails 0))
         (dotimes (i 50)
           (let ((s (with-output-to-string (out)
                      (write-string "Content-Length: 32" out)
                      (ext:gc)   ; reclaim runs while OUT is live & half-built
                      (write-string " (ok)" out)
                      (ext:gc))))
             (unless (string= s "Content-Length: 32 (ok)") (incf fails))))
         fails))

;; The outbuf side table grows on demand in 256-slot blocks (it is no longer
;; a fixed 256-slot table that forced a full GC per allocation once
;; saturated).  Hold well over one block's worth of open string streams at
;; once; every stream must keep its own content, and closing them all must
;; release the slots (eager free on CLOSE, checked via
;; ext:%stream-outbuf-stats).
(check "outbuf table grows past one block, close frees eagerly" t
       (let ((base (car (ext:%stream-outbuf-stats)))
             (streams nil)
             (ok t))
         (dotimes (i 300)
           (let ((s (make-string-output-stream)))
             (write-string "c-" s)
             (princ i s)
             (push s streams)))
         (setf streams (nreverse streams))
         (let ((i 0))
           (dolist (s streams)
             (unless (string= (get-output-stream-string s)
                              (with-output-to-string (tmp)
                                (write-string "c-" tmp)
                                (princ i tmp)))
               (setf ok nil))
             (incf i)))
         (dolist (s streams) (close s))
         (and ok (<= (car (ext:%stream-outbuf-stats)) (+ base 5)))))

; --- Backtrace (error recovery) ---
; Test that errors in nested calls don't break subsequent evaluation
(defun bt-err-inner () (error "test error"))
(defun bt-err-outer () (+ 1 (bt-err-inner)))
(handler-case (bt-err-outer) (error () nil))
(check "backtrace recovery" 42 42)
(defun bt-rec-err (n) (if (= n 0) (error "depth") (+ 1 (bt-rec-err (1- n)))))
(handler-case (bt-rec-err 5) (error () nil))
(check "backtrace deep recovery" 99 99)
(defun bt-uwp-fn () (unwind-protect (error "uwp err") nil))
(handler-case (bt-uwp-fn) (error () nil))
(check "backtrace uwp recovery" 7 (+ 3 4))

; Regression: cl_debug_base_fp must be reset between top-level forms so that
; ext:backtrace in a later form sees the live stack, not a stale error snapshot.
; These are three separate top-level forms: defun, handler-case (sets
; cl_debug_base_fp), then check (cl_debug_base_fp should be 0 by now so
; bt_resolve_base uses the live frame_top).
(defun bt-cross-form-probe () (ext:backtrace))
(handler-case (error "bt-base-fp-reset-test") (error () nil))
(check "backtrace cross-form no stale base" t
  (consp (bt-cross-form-probe)))

; --- Time ---
(check "time returns value" 3 (time (+ 1 2)))
(check "time nested" 22 (+ 10 (time (* 3 4))))
(defun time-sq (x) (* x x))
(check "time defun" 25 (time (time-sq 5)))
(check "get-internal-real-time" t (integerp (get-internal-real-time)))
(check "get-internal-run-time" t (integerp (get-internal-run-time)))
(check "run-time non-decreasing" t
  (let ((before (get-internal-run-time)))
    (dotimes (i 10000))
    (>= (get-internal-run-time) before)))
; TIME start-value helpers stay in fixnum range (regression: 31-bit masks
; overflowed the 30-bit fixnum encoding once a counter passed 2^30)
(check "time helpers fixnum range" t
  (and (<= 0 (%get-internal-time) most-positive-fixnum)
       (<= 0 (%get-run-time) most-positive-fixnum)
       (<= 0 (%get-bytes-consed) most-positive-fixnum)
       (<= 0 (%get-gc-count) most-positive-fixnum)
       t))

; --- Time stream redirection ---
(check "time captured by *trace-output*" t
  (let ((s (with-output-to-string (*trace-output*) (time (+ 1 2)))))
    (and (search "ms real" s) (search "ms cpu" s) (search "consed" s) t)))

; --- Source location tracking ---
; Reader line tracking is implicitly tested by batch mode itself
; The fact that errors show proper backtraces validates source locations
(defun srcloc-inner (x) (/ x 0))
(defun srcloc-outer (x) (let ((r (srcloc-inner x))) r))
(handler-case (srcloc-outer 1) (error () nil))
(check "srcloc error recovery" 42 42)

; --- Condition signaling ---
(check "signal returns nil" nil (signal (make-condition 'simple-condition :format-control "test")))
(check "signal string returns nil" nil (signal "something"))
(check "signal symbol returns nil" nil (signal 'simple-condition))
(check "warn returns nil" nil (warn "test warning"))
(check "warn symbol returns nil" nil (warn 'simple-warning))
; --- Warning stream redirection ---
(check "warn captured by *error-output*" t (let ((s (with-output-to-string (*error-output*) (warn "hi")))) (and (search "WARNING" s) (search "hi" s) t)))
; error test: error is caught, next expression returns 42
(handler-case (error "test error for batch") (error () nil))
(check "error recovery" 42 42)

; --- handler-bind ---
(check "handler-bind basic" 'simple-condition (catch 'test-tag (handler-bind ((simple-condition (lambda (c) (throw 'test-tag (condition-type-name c))))) (signal (make-condition 'simple-condition :format-control "test")))))
(check "handler-bind error match" 42 (catch 'test-tag (handler-bind ((error (lambda (c) (throw 'test-tag 42)))) (signal (make-condition 'simple-error :format-control "boom")))))
(check "handler-bind no match" :untouched (catch 'test-tag (handler-bind ((type-error (lambda (c) (throw 'test-tag :touched)))) (signal (make-condition 'simple-warning :format-control "w")) :untouched)))
(check "handler-bind body value" 3 (handler-bind ((error (lambda (c) nil))) (+ 1 2)))
; CLHS 9.1.4.1: declining handlers fire in textual (top-to-bottom) order.
(check "handler-bind textual order" '(:a :b) (let ((log nil)) (handler-bind ((condition (lambda (c) (declare (ignore c)) (push :a log))) (warning (lambda (c) (declare (ignore c)) (push :b log)))) (signal (make-condition 'simple-warning :format-control "w"))) (nreverse log)))
(check "handler-bind earlier sets state for later" :ready (let ((ready nil) (result :unset)) (catch 'tag (handler-bind ((condition (lambda (c) (declare (ignore c)) (setf ready t))) (warning (lambda (c) (declare (ignore c)) (setf result (if ready :ready :not-ready)) (throw 'tag result)))) (signal (make-condition 'simple-warning :format-control "w")))) result))

; --- handler-case ---
(check "handler-case catches error" 42 (handler-case (error "boom") (error (c) 42)))
(check "handler-case no error" 3 (handler-case (+ 1 2) (error (c) 99)))
(check "handler-case type dispatch" 42 (handler-case (error 'type-error :datum 42 :expected-type 'string) (type-error (c) (type-error-datum c)) (error (c) :generic)))

; --- ignore-errors ---
(check "ignore-errors catches" nil (ignore-errors (error "boom")))
(check "ignore-errors no error" 30 (ignore-errors (+ 10 20)))

; --- restart-case ---
(check "restart-case basic" 42 (restart-case (invoke-restart 'use-value 42) (use-value (v) v)))
(check "restart-case normal exit" 3 (restart-case (+ 1 2) (abort () 99)))
(check "restart-case no params" 42 (restart-case (invoke-restart 'abort) (abort () 42)))
(check "restart-case multiple" :continued (restart-case (invoke-restart 'continue) (abort () :aborted) (continue () :continued)))
(check "find-restart found" :found (restart-case (if (find-restart 'continue) :found :not-found) (continue () nil)))
(check "find-restart missing" nil (find-restart 'continue))
(check "compute-restarts count" 2 (restart-case (length (compute-restarts)) (abort () nil) (continue () nil)))
(check "restart with handler" 99 (handler-bind ((error (lambda (c) (invoke-restart 'continue)))) (restart-case (error "boom") (continue () 99))))
(check "with-simple-restart invoke" nil (with-simple-restart (abort "Abort") (invoke-restart 'abort)))
(check "with-simple-restart normal" 42 (with-simple-restart (abort "Abort") 42))
(check "cerror continue" :after (handler-bind ((error (lambda (c) (invoke-restart 'continue)))) (cerror "Continue" "bad") :after))

; --- first-class restart objects (CLHS 9.1) ---
(check "find-restart returns restart object" t (restart-case (typep (find-restart 'foo) 'restart) (foo () nil)))
(check "find-restart not a symbol" nil (restart-case (symbolp (find-restart 'foo)) (foo () nil)))
(check "restart type-of" 'restart (restart-case (type-of (find-restart 'foo)) (foo () nil)))
(check "restart-name" 'my-restart (restart-case (restart-name (find-restart 'my-restart)) (my-restart () nil)))
(check "compute-restarts names innermost first" '(inner outer)
       (restart-case (restart-case (mapcar #'restart-name (compute-restarts)) (inner () nil)) (outer () nil)))
(check "compute-restarts all objects" t
       (restart-case (every (lambda (r) (typep r 'restart)) (compute-restarts)) (a () nil) (b () nil)))
(check "invoke-restart by object" 42 (restart-case (invoke-restart (find-restart 'doit) 21) (doit (x) (* x 2))))
(check "restart princ report string" "Do the foo thing"
       (restart-case (princ-to-string (find-restart 'foo)) (foo () :report "Do the foo thing" nil)))
(check "restart prin1 escaped" "#<RESTART FOO>"
       (restart-case (prin1-to-string (find-restart 'foo)) (foo () :report "Do the foo thing" nil)))
(check "restart report function" "computed 3"
       (restart-case (princ-to-string (find-restart 'foo))
         (foo () :report (lambda (s) (format s "computed ~D" (+ 1 2))) nil)))
(check "restart no report falls back" "#<RESTART BARE>"
       (restart-case (princ-to-string (find-restart 'bare)) (bare () nil)))
(check "restart :test hides" :not-found
       (restart-case (if (find-restart 'r) :found :not-found)
         (r () :test (lambda (c) (declare (ignore c)) nil) nil)))
(check "invoke-restart-interactively" 7
       (restart-case (invoke-restart-interactively (find-restart 'add))
         (add (a b) :interactive (lambda () (list 3 4)) (+ a b))))

; --- unwind-protect + restart-case ordering (CLHS INVOKE-RESTART) ---
(defvar *restart-uwp-log* nil)
(check "uwp cleanup before handler"
       '(before cleanup handler)
       (let ((*restart-uwp-log* nil))
         (restart-case
           (unwind-protect
             (progn (push 'before *restart-uwp-log*) (invoke-restart 'abort))
             (push 'cleanup *restart-uwp-log*))
           (abort () (push 'handler *restart-uwp-log*) :done))
         (reverse *restart-uwp-log*)))
(check "nested uwp cleanup order"
       '(before inner-cleanup outer-cleanup handler)
       (let ((*restart-uwp-log* nil))
         (restart-case
           (unwind-protect
             (unwind-protect
               (progn (push 'before *restart-uwp-log*) (invoke-restart 'abort))
               (push 'inner-cleanup *restart-uwp-log*))
             (push 'outer-cleanup *restart-uwp-log*))
           (abort () (push 'handler *restart-uwp-log*) :done))
         (reverse *restart-uwp-log*)))
(check "handler return is restart-case value"
       :handler-result
       (restart-case (unwind-protect (invoke-restart 'abort) nil) (abort () :handler-result)))

; --- define-condition ---
(check "define-condition basic" 'my-error (define-condition my-error (error) ()))
(check "define-condition conditionp" t (conditionp (make-condition 'my-error)))
(define-condition file-err (error) ((pathname :initarg :pathname :reader file-err-pathname)))
(check "define-condition reader" "/tmp" (file-err-pathname (make-condition 'file-err :pathname "/tmp")))
(check "define-condition typep parent" t (typep (make-condition 'my-error) 'error))
(check "define-condition typep condition" t (typep (make-condition 'my-error) 'condition))
(check "define-condition typep not warning" nil (typep (make-condition 'my-error) 'warning))
(define-condition app-err (error) ())
(check "define-condition handler-case" :caught (handler-case (error 'app-err) (app-err (c) :caught)))
; A slot-specifier may be a bare SYMBOL (no options), not only a list — CLHS
; DEFINE-CONDITION/DEFCLASS.  Used to signal "CAR: argument is not of type
; LIST" at compile time; this is the fast-http (clog dependency) shape
; (define-condition fast-http-error (simple-error) (description) ...).
(check "define-condition bare-slot" t
       (conditionp (make-condition (define-condition bss-err (simple-error) (description)))))
(define-condition bss-sub (bss-err) ((description :initarg :description :reader bss-desc)))
(check "define-condition bare-slot subtype" t (typep (make-condition 'bss-sub) 'bss-err))
(check "define-condition bare-slot reader" "d"
       (bss-desc (make-condition 'bss-sub :description "d")))
(define-condition bss-err2 (error) (a (b :initarg :b :reader bss-b)))
(check "define-condition mixed-slots" 7 (bss-b (make-condition 'bss-err2 :b 7)))
; Regression: the map family (mapcar/map/map-into/...) walked their list cursors
; in unprotected C arrays across the mapped-function call, so an allocating
; mapped function relocated the list under the moving GC and the cursor went
; stale ("CAR: argument is not of type LIST").  define-condition expands to
; (mapcar (lambda (spec) ... (list spec)) slot-specs), so a multi-slot condition
; compiled after a cons-allocating top-level form lost its slot walk.
(defparameter *mc-reg* :default)
(define-condition mc-reg-cond (error) ((a :reader mc-a) (b :reader mc-b) (c :reader mc-c)))
(check "define-condition multi-slot after defparameter" t
       (typep (make-condition 'mc-reg-cond) 'mc-reg-cond))
(check "mapcar allocating fn keeps cursor" '((1 1) (2 2) (3 3) (4 4))
       (mapcar (lambda (e) (list e e)) (list 1 2 3 4)))
(check "map 'list allocating fn keeps cursor" '((10) (20) (30))
       (map 'list (lambda (e) (list e)) (list 10 20 30)))
(check "maplist allocating fn keeps cursor" '((4) (3) (2) (1))
       (maplist (lambda (tl) (list (length tl))) (list 1 2 3 4)))
(check "mapcon allocating fn keeps cursor" '(1 10 2 20 3 30)
       (mapcon (lambda (tl) (list (car tl) (* (car tl) 10))) (list 1 2 3)))
(check "mapc allocating fn keeps cursor" '(1 2 3 4)
       (let ((acc '()))
         (mapc (lambda (e) (push (list e) acc)) (list 1 2 3 4))
         (mapcar #'car (nreverse acc))))
(check "mapl allocating fn keeps cursor" '(1 2 3)
       (let ((acc '()))
         (mapl (lambda (tl) (push (list (car tl)) acc)) (list 1 2 3))
         (mapcar #'car (nreverse acc))))
(check "every consing predicate keeps cursor" t
       (every (lambda (e) (car (list (plusp e)))) (list 1 2 3 4 5)))
(check "some consing predicate keeps cursor" 4
       (some (lambda (e) (car (list (and (evenp e) e)))) (list 1 3 4 5)))
(check "notevery consing predicate keeps cursor" t
       (notevery (lambda (e) (car (list (evenp e)))) (list 2 4 5 6)))
(check "notany consing predicate keeps cursor" t
       (notany (lambda (e) (car (list (oddp e)))) (list 2 4 6 8)))
(check "reduce :key + allocating reducer keeps cursor" 30
       (reduce (lambda (a b) (car (list (+ a b))))
               (list '(1) '(2) '(3) '(4)) :key #'car :initial-value 20))
(check "reduce :from-end :key keeps elements" 24
       (reduce (lambda (e a) (car (list (+ e a))))
               (list '(1) '(2) '(3) '(4)) :key #'car :from-end t :initial-value 14))

; ERROR as a non-first superclass must still make the condition an ERROR for
; typep / subtypep / handler-case (regression: usocket SOCKET-ERROR is
; (define-condition socket-error (socket-condition error)) — only the first
; parent was registered, so it escaped (error ...) handlers and chipi's
; InfluxDB fetch future never completed on an unknown host).
(define-condition mi-base (condition) ())
(define-condition mi-err (mi-base error) ())
(define-condition mi-sub (mi-err) ())
(check "define-condition error-2nd-parent typep"  t   (typep (make-condition 'mi-err) 'error))
(check "define-condition error-2nd-parent typep-base" t (typep (make-condition 'mi-err) 'mi-base))
(check "define-condition error-2nd-parent subtypep" t (subtypep 'mi-err 'error))
(check "define-condition error-2nd-parent inherited" t (typep (make-condition 'mi-sub) 'error))
(check "define-condition error-2nd-parent handler-case" :as-error
       (handler-case (error 'mi-err) (error () :as-error) (condition () :as-cond)))
; must not spuriously widen a pure non-error condition
(check "define-condition non-error-not-widened" nil (typep (make-condition 'mi-base) 'error))

; Slot :initform must be evaluated per instance when the initarg is unsupplied
; (CLHS 7.1.3).  Covers a plain slot, a class-allocated slot with no initarg
; (fiasco's PROGRESS-CHAR / CONTEXT shape, which returned NIL and crashed the
; fiasco failure reporter), and make-time evaluation of a dynamic initform.
(define-condition ci-inst (error) ((s :initform 42 :accessor ci-s)))
(check "condition slot initform instance" 42 (ci-s (make-condition 'ci-inst)))
(define-condition ci-inst2 (error) ((s :initarg :s :initform 42 :accessor ci-s2)))
(check "condition slot initform overridden by initarg" 7 (ci-s2 (make-condition 'ci-inst2 :s 7)))
(define-condition ci-cls (warning) ((pc :initform #\X :accessor ci-pc :allocation :class)))
(check "condition class-allocated slot initform" #\X (ci-pc (make-condition 'ci-cls)))
(defvar *ci-dyn* :outer)
(define-condition ci-dyn (error) ((ctx :initform *ci-dyn* :accessor ci-ctx)))
(check "condition slot initform evaluated at make-time" :inner
       (ci-ctx (let ((*ci-dyn* :inner)) (make-condition 'ci-dyn))))

; --- check-type ---
(check "check-type pass" :ok (let ((x 42)) (check-type x integer) :ok))
(check "check-type fail" "hello" (handler-case (let ((x "hello")) (check-type x integer)) (type-error (c) (type-error-datum c))))

; --- assert ---
(check "assert pass" :ok (progn (assert (= 1 1)) :ok))
(check "assert fail" :failed (handler-case (assert (= 1 2)) (simple-error (c) :failed)))

; --- Package System ---
(check "find-package CL" t (not (null (find-package "COMMON-LISP"))))
(check "find-package nickname CL" t (not (null (find-package "CL"))))
(check "find-package CL-USER" t (not (null (find-package "CL-USER"))))
(check "find-package KEYWORD" t (not (null (find-package "KEYWORD"))))
(check "find-package nil" nil (find-package "NONEXISTENT"))
(check "package-name CL" "COMMON-LISP" (package-name (find-package "CL")))
(check "package-nicknames CL" "CL" (car (package-nicknames (find-package "CL"))))
(check "package-use-list CL-USER"
       #+amigaos 7 #-amigaos 6
       (length (package-use-list (find-package "CL-USER"))))
(check "list-all-packages" t (>= (length (list-all-packages)) 3))
(check "make-package" t (not (null (or (find-package "TEST-PKG-1") (make-package "TEST-PKG-1")))))
(check "find-package after make" t (not (null (find-package "TEST-PKG-1"))))
(check "find-symbol CAR external" :external (multiple-value-bind (sym status) (find-symbol "CAR" (find-package "CL")) status))
(check "find-symbol CAR inherited" :inherited (multiple-value-bind (sym status) (find-symbol "CAR" (find-package "CL-USER")) status))
(check "find-symbol not found" nil (multiple-value-bind (sym status) (find-symbol "ZZZZZ" (find-package "CL")) status))
(check "intern new" nil (let ((name (symbol-name (gensym "INTERN-TEST-")))) (multiple-value-bind (sym status) (intern name) status)))
(check "intern existing" :inherited (multiple-value-bind (sym status) (intern "CAR") status))
(unless (find-package "EXP-AMI") (make-package "EXP-AMI"))
(intern "EXP-SYM" (find-package "EXP-AMI"))
(check "export" :external (progn (export (find-symbol "EXP-SYM" (find-package "EXP-AMI")) (find-package "EXP-AMI")) (multiple-value-bind (s st) (find-symbol "EXP-SYM" (find-package "EXP-AMI")) st)))
(check "unexport" :internal (progn (unexport (find-symbol "EXP-SYM" (find-package "EXP-AMI")) (find-package "EXP-AMI")) (multiple-value-bind (s st) (find-symbol "EXP-SYM" (find-package "EXP-AMI")) st)))
(unless (find-package "USE-AMI-SRC") (make-package "USE-AMI-SRC"))
(unless (find-package "USE-AMI-DST") (make-package "USE-AMI-DST"))
(intern "SHARED-SYM" (find-package "USE-AMI-SRC"))
(export (find-symbol "SHARED-SYM" (find-package "USE-AMI-SRC")) (find-package "USE-AMI-SRC"))
(use-package "USE-AMI-SRC" "USE-AMI-DST")
(check "use-package inherit" :inherited (multiple-value-bind (s st) (find-symbol "SHARED-SYM" (find-package "USE-AMI-DST")) st))
(check "delete-package" nil (progn (unless (find-package "DEL-AMI") (make-package "DEL-AMI")) (delete-package (find-package "DEL-AMI")) (find-package "DEL-AMI")))
(check "rename-package" "RENAMED-AMI" (progn (unless (find-package "ORIG-AMI") (make-package "ORIG-AMI")) (unless (find-package "RENAMED-AMI") (rename-package (find-package "ORIG-AMI") "RENAMED-AMI")) (package-name (find-package "RENAMED-AMI"))))
(unless (find-package "SHAD-AMI") (make-package "SHAD-AMI"))
(use-package "CL" "SHAD-AMI")
(shadow "CAR" (find-package "SHAD-AMI"))
(check "shadow" :internal (multiple-value-bind (s st) (find-symbol "CAR" (find-package "SHAD-AMI")) st))
(unless (find-package "UNINT-AMI") (make-package "UNINT-AMI"))
(intern "REM-SYM" (find-package "UNINT-AMI"))
(unintern (find-symbol "REM-SYM" (find-package "UNINT-AMI")) (find-package "UNINT-AMI"))
(check "unintern" nil (multiple-value-bind (s st) (find-symbol "REM-SYM" (find-package "UNINT-AMI")) st))

; === Reader Qualified Syntax ===

; pkg:sym external access
(unless (find-package "QRA-FOO") (make-package "QRA-FOO"))
(intern "QBAR" (find-package "QRA-FOO"))
(export (find-symbol "QBAR" (find-package "QRA-FOO")) (find-package "QRA-FOO"))
(check "pkg:sym external" t (eq (find-symbol "QBAR" (find-package "QRA-FOO")) 'QRA-FOO:QBAR))

; pkg::sym internal access
(intern "QSECRET" (find-package "QRA-FOO"))
(check "pkg::sym internal" t (eq (find-symbol "QSECRET" (find-package "QRA-FOO")) 'QRA-FOO::QSECRET))

; pkg::sym creates symbol
(unless (find-package "QRA-BAR") (make-package "QRA-BAR"))
(check "pkg::sym creates" t (symbolp 'QRA-BAR::NEWSYM))
(check "pkg::sym status" :internal (multiple-value-bind (s st) (find-symbol "NEWSYM" (find-package "QRA-BAR")) st))

; CL:CAR = CAR
(check "cl:sym" t (eq 'CL:CAR 'CAR))
; CL:NIL must resolve to NIL (not error as "not exported")
(check "cl:nil" t (eq 'CL:NIL nil))

; KEYWORD:TEST = :TEST
(check "keyword:sym" t (eq 'KEYWORD:TEST :TEST))

; #:sym uninterned
(check "uninterned symbolp" t (symbolp '#:TEMP))
(check "uninterned no pkg" t (null (symbol-package '#:TEMP)))
; Within a single READ, multiple #:foo with the same name share identity
; (needed for compile-file output: e.g. SBCL emits LETs that bind #:gensymN
; and reference it in the body, all parsed as one top-level form).
(check "uninterned shared in one read" t (eq '#:X '#:X))

; #n= / #n# shared-and-circular-structure reader labels (CLHS 2.4.8.15-16).
; Regression: jzon's jzon.lisp uses (macrolet ((#1=#:|| () ...)) ... (#1# ...))
; which previously errored "Invalid radix prefix #1=", blocking the whole load.
(check "label value" 42 (read-from-string "#1=42"))
(check "label shared eq" t (let ((x (read-from-string "(#1=#:foo #1#)")))
                             (eq (first x) (second x))))
(check "label multidigit eq" t (let ((x (read-from-string "(#10=#:bar #10#)")))
                                 (eq (first x) (second x))))
(check "label circular car" 1 (car (read-from-string "#1=(1 2 . #1#)")))
(check "label circular self" t (let ((c (read-from-string "#1=(1 2 . #1#)")))
                                 (eq (cddr c) c)))
(check "label scoped per read" 8 (progn (read-from-string "#1=(7)")
                                        (car (read-from-string "#1=(8)"))))
; Regression: (nil . nil) label value must not be mistaken for the placeholder
(check "label nil dotpair shared" t
  (let* ((x (read-from-string "(#1=(nil . nil) #1#)"))
         (a (car x))
         (b (cadr x)))
    (and (null (car a)) (null (cdr a)) (eq a b))))

; READ-FROM-STRING conformance (CLHS): :start/:end/:preserve-whitespace
; keywords, second value = index of first unread char, EOF-ERROR-P
; defaults to T.  uiop/asdf read .asd files with the full keyword form.
; Per CLHS 23.1.2, plain READ (the PRESERVE-WHITESPACE-NIL default)
; consumes the single trailing whitespace character that terminated the
; token, so the reported position lands one past that character.
(check "read-from-string keywords+position" '(a 3)
       (multiple-value-list (read-from-string "(a b) c" t nil :start 1 :end 5)))
(check "read-from-string position" '(foo 6)
       (multiple-value-list (read-from-string "  foo  ")))
(check "read-from-string eof default signals" :eof
       (handler-case (read-from-string "") (end-of-file () :eof)))
(check "read-from-string preserve-whitespace" 'xy
       (read-from-string "xy " t nil :preserve-whitespace t))
(check "read-from-string clhs example" '(1 2)
       (multiple-value-list (read-from-string "1 3 5")))
(check "read-preserving-whitespace does not consume terminator" '(1 1)
       (let ((s (make-string-input-stream "1 3 5")))
         (list (read-preserving-whitespace s) (file-position s))))

; Word-size + endianness features come from the runtime itself (CFFI sizes
; :SIZE via #+64-bit/#+32-bit, babel picks codecs via endianness).  The
; m68k/AmigaOS target is 32-bit big-endian.
(check "features 32-bit" t (and (member :32-bit *features*) t))
(check "features not 64-bit" nil (member :64-bit *features*))
(check "features big-endian" t (and (member :big-endian *features*) t))
(check "features not little-endian" nil (member :little-endian *features*))

; Printer: base string with a high byte (> 127) round-trips through a string
; output stream as its real code point, not the U+FFFD replacement char.
; (Host CL_WIDE_STRINGS regression for print_string's signed-char bug; on the
; non-wide Amiga build base strings are 8-bit transparent — this guards that the
; unsigned-char + per-char princ change keeps the byte faithful here too.)
(check "prin1 base-string high byte round-trip" '(34 92 34 252 92 92 34)
       (map 'list 'char-code
            (with-output-to-string (o)
              (prin1 (coerce (list #\" (code-char 252) #\\) 'string) o))))
(check "princ base-string high byte round-trip" '(252)
       (map 'list 'char-code
            (with-output-to-string (o)
              (princ (coerce (list (code-char 252)) 'string) o))))

; Printer: uninterned
(check "print uninterned" "#:HELLO" (prin1-to-string '#:HELLO))

; Printer: keyword unchanged
(check "print keyword" ":FOO" (prin1-to-string :FOO))

; Printer: current pkg no prefix
(check "print current pkg" "CAR" (prin1-to-string 'CAR))

; Printer: other pkg external
(unless (find-package "QRA-PR") (make-package "QRA-PR"))
(intern "XSYM" (find-package "QRA-PR"))
(export (find-symbol "XSYM" (find-package "QRA-PR")) (find-package "QRA-PR"))
(check "print external" "QRA-PR:XSYM" (prin1-to-string 'QRA-PR:XSYM))

; Printer: other pkg internal
(unless (find-package "QRA-PR2") (make-package "QRA-PR2"))
(intern "ISYM" (find-package "QRA-PR2"))
(check "print internal" "QRA-PR2::ISYM" (prin1-to-string 'QRA-PR2::ISYM))

; --- CDR-10: Package-local nicknames ---
(unless (find-package "LN-AMI1") (make-package "LN-AMI1" :use '("COMMON-LISP")))
(add-package-local-nickname "KW-AMI" (find-package "KEYWORD") (find-package "LN-AMI1"))
(check "pkg-local-nick add" 1 (length (package-local-nicknames (find-package "LN-AMI1"))))
(in-package "LN-AMI1")
(cl-user::check "pkg-local-nick resolve" t (not (null (find-package "KW-AMI"))))
(in-package "COMMON-LISP-USER")
(check "pkg-local-nick scoped" nil (find-package "KW-AMI"))
(unless (find-package "LN-AMI2") (make-package "LN-AMI2" :use '("COMMON-LISP")))
(add-package-local-nickname "RK" (find-package "KEYWORD") (find-package "LN-AMI2"))
(remove-package-local-nickname "RK" (find-package "LN-AMI2"))
(check "pkg-local-nick remove" 0 (length (package-local-nicknames (find-package "LN-AMI2"))))
(defpackage "LN-AMI3" (:use "COMMON-LISP") (:local-nicknames ("KD" "KEYWORD")))
(in-package "LN-AMI3")
(cl-user::check "defpackage local-nick" t (not (null (find-package "KD"))))
(cl-user::check "local-nick reader" t (eq 'KD:TEST :TEST))
(in-package "COMMON-LISP-USER")

; --- Macrolet ---
(check "macrolet basic" 25 (macrolet ((square (x) `(* ,x ,x))) (square 5)))
(check "macrolet nested" 15 (macrolet ((m (x) `(+ ,x 1))) (macrolet ((m (x) `(+ ,x 10))) (m 5))))
(check "macrolet body" 14 (macrolet ((double (x) `(* 2 ,x))) (double 3) (double 7)))

; --- Symbol-macrolet ---
(check "symbol-macrolet basic" 42 (symbol-macrolet ((x 42)) x))
(check "symbol-macrolet expr" 3 (symbol-macrolet ((x (+ 1 2))) x))
(check "symbol-macrolet setq" '(99 . 2) (let ((a (cons 1 2))) (symbol-macrolet ((x (car a))) (setq x 99) a)))
(check "symbol-macrolet nested" 20 (symbol-macrolet ((x 10)) (symbol-macrolet ((x 20)) x)))
(check "symbol-macrolet multi" 30 (symbol-macrolet ((x 10) (y 20)) (+ x y)))

; --- Scanner + symbol-macrolet + &environment (ironclad sha3.lisp:65) ---
;; The closure/NLX compiler pre-scanners speculatively expand any global macro
;; they meet; they must register enclosing symbol-macrolet bindings into the
;; compiler env first, so an &environment-aware expander that resolves a
;; symbol-macro via (macroexpand x env) — like ironclad's GET-KECCAK-ROTATE-
;; OFFSET inside a DOTIMES-UNROLLED — folds to its constant instead of signaling
;; "Unbound variable: X" mid-scan.  The error only escapes when an outer
;; HANDLER-CASE is active during compilation (as ASDF installs); CHECK supplies
;; one, and EVAL makes the DEFUN compile inside it.
(defun smm-tmexpand (form env)
  (let ((real (macroexpand form env)))
    (if (atom real) real
        (cons (car real)
              (mapcar (lambda (x) (smm-tmexpand x env)) (cdr real))))))
(defmacro smm-unrolled ((var limit) &body body &environment env)
  (loop for i from 0 below (eval (smm-tmexpand limit env))
        collect (list 'symbol-macrolet (list (list var i))
                      (cons 'progn body)) into forms
        finally (return (cons 'progn forms))))
(defparameter *smm-offs*
  (make-array '(2 2) :initial-contents '((10 11) (12 13))))
(defmacro smm-get (x y &environment env)
  (aref *smm-offs* (eval (smm-tmexpand x env)) (eval (smm-tmexpand y env))))
(check "scanner symbol-macrolet &environment macro (ironclad sha3)" 46
  (progn
    (eval '(defun smm-keccak (acc)
             (smm-unrolled (x 2)
               (smm-unrolled (y 2)
                 (setf acc (+ acc (smm-get x y)))))))
    (smm-keccak 0)))

;; A MACROLET-defined (not DEFMACRO) macro whose &environment is NOT the first
;; lambda-list element, used inside a LET that triggers the boxing pre-scan.
;; cl_macrolet_install_expanders runs twice on the shared lambda list (once in
;; the pre-scan, once in the real compile); the &environment strip must NOT
;; mutate that shared structure, else the 2nd install drops &environment and the
;; expander body's env reference becomes "Unbound variable: ENV".  This is the
;; exact shape of ironclad sha3.lisp's STATE-AREF / GET-KECCAK-ROTATE-OFFSET.
(check "scanner macrolet non-first &environment in binding form" 99
  (let ((v (make-array 3 :initial-element 0)))
    (macrolet ((setit (i val &environment env)
                 `(setf (aref v ,(macroexpand i env)) ,val)))
      (symbol-macrolet ((idx 1))
        (setit idx 99)
        (aref v 1)))))

;; Deeply-nested binding form: scan_body_for_boxing recurses through the LET/
;; MACROLET/... handlers and the macro fall-through holding GC roots across each
;; recursive call.  Serapeum's sequences.lisp expands into nesting deep enough to
;; exhaust the GC-root stack and abort the compile (host sento-system suite); a
;; gc_root_count backstop in scan_body_for_boxing now bails before that.  The full
;; trigger (~1000 levels) would overflow the Amiga C stack first and is host-only
;; (covered by test_vm.c scanner_deep_nesting_no_root_overflow), so here just
;; confirm the scanner still boxes correctly through MODERATE nesting: ACC is
;; mutated by the ADD closure (so it must be boxed) and the call is buried 60 LETs
;; deep, forcing the scanner to recurse through all of them and still mark ACC.
(check "scanner boxes mutated var through deep nesting" 1
  (let ((acc 0))
    (flet ((add () (setf acc (1+ acc))))
      (macrolet ((deep ()
                   (let ((form '(add)))
                     (dotimes (i 60) (setf form (list 'let '((d 0)) form)))
                     form)))
        (deep)))
    acc))

; --- Scanner: macrolet/symbol-macrolet setf boxing (local-time parse-timestring) ---
;; The boxing pre-scan must macroexpand MACROLET (and SYMBOL-MACROLET) forms
;; while scanning a FLET/LABELS body, or a `(setf var ...)` hidden in such a
;; macro is never seen -> the enclosing variable is not boxed and the closure's
;; write is silently dropped.  local-time's %SPLIT-TIMESTRING uses a
;; PARSE-INTEGER-INTO macrolet to `(setf month value)` inside its date LABELS;
;; without the fix month/day came back NIL and `(<= 1 month 12)` raised
;; "<=: argument is not of type REAL", which broke chipi influx persistence.
(check "scanner macrolet setf outer var in labels" 99
  (let ((m 0))
    (macrolet ((setm (x) `(setf m ,x)))
      (labels ((s () (setm 99)))
        (s)
        m))))
(check "scanner macrolet inside labels body setf" 7
  (let ((m 0))
    (labels ((s () (macrolet ((setm (x) `(setf m ,x))) (setm 7))))
      (s)
      m)))
(check "scanner macrolet setf outer var in flet" 5
  (let ((m 0))
    (flet ((s () (macrolet ((setm (x) `(setf m ,x))) (setm 5))))
      (s)
      m)))
(check "scanner macrolet parse-into shape (local-time)" '(2026 6 27)
  (let (y mo d)
    (macrolet ((into (place v lo hi)
                 `(progn (setf ,place ,v)
                         (unless (<= ,lo ,place ,hi) (error "range"))
                         (values))))
      (labels ((sy (v) (into y v 1 9999))
               (sm (v) (into mo v 1 12))
               (sd (v) (into d v 1 31)))
        (sy 2026) (sm 6) (sd 27)
        (list y mo d)))))
(check "scanner symbol-macrolet setf outer var in labels" 42
  (let ((m 0))
    (labels ((s () (symbol-macrolet ((q m)) (setf q 42))))
      (s)
      m)))
;; In-place macrolet (no FLET/LABELS): the expansion both setfs a place and
;; references global macro stubs (SETF/PROGN have macro-functions for
;; MACROEXPAND conformance).  Compiling the inner (setf y v) re-enters the
;; global-macro check, whose cl_build_lex_env conses a non-empty lexical env
;; from the enclosing macrolet binding -> a compaction; compile_expr_step's
;; local expr/head copies must be refreshed afterwards or the setf compiles
;; the relocated enclosing PROGN form (was "%SETF-SETF" / hi value under GC
;; stress).  Must be 2026, not 9999.
(check "scanner in-place macrolet setf+stub keeps live expr" 2026
  (let ((y 0))
    (macrolet ((into (place v lo hi)
                 `(progn (setf ,place ,v)
                         (unless (<= ,lo ,place ,hi) (error "range"))
                         (values))))
      (into y 2026 1 9999))
    y))

;; (setf (values a b) ...) inside a closure must box a AND b — the boxing
;; scanner used to treat the VALUES place as a read, so the closure's writes
;; were dropped (outer a/b kept their initial values).  serapeum's MVFOLD
;; compiler macro emits (setf (values seed0 seed1) (funcall ...)) hoisted into a
;; do-each thunk.
(check "scanner setf-values boxing across closure" '(6 3)
  (let ((a 0) (b 0))
    (flet ((stp (x) (setf (values a b) (values (+ a x) (max b x)))))
      (mapc #'stp '(1 2 3)))
    (list a b)))
(check "scanner setf-values boxes second value" '(7 9)
  (let ((a 0) (b 0))
    (funcall (lambda () (setf (values a b) (values 7 9))))
    (list a b)))

; --- Debugger ---
(check "invoke-debugger exists" t (functionp #'invoke-debugger))
(check "*debugger-hook* initial" nil *debugger-hook*)
(check "break exists" t (functionp #'break))

; --- Multi-line expressions ---
(defun multiline-fact (n)
  (if (<= n 1)
    1
    (* n (multiline-fact (1- n)))))
(check "multiline defun" 120 (multiline-fact 5))
(check "multiline nested"
  15
  (let ((x 10)
        (y 5))
    (+ x y)))

; --- defstruct ---
;; Use DST-POINT (not POINT) so accessors don't leave compiler macros that
;; collide with the later CLOS (defclass point ...) test fixtures.  A defstruct
;; emits (define-compiler-macro <acc> ...) that inlines to %struct-ref by
;; index — fine for the struct, but stale once a class with different slot
;; layout reuses the name.
(check "defstruct basic" 'dst-point (defstruct dst-point (x 0) (y 0)))
(check "defstruct make default" 0 (dst-point-x (make-dst-point)))
(check "defstruct make keyword" 10 (dst-point-x (make-dst-point :x 10 :y 20)))
(check "defstruct accessor y" 20 (dst-point-y (make-dst-point :x 10 :y 20)))
(check "defstruct predicate t" t (dst-point-p (make-dst-point)))
(check "defstruct predicate nil" nil (dst-point-p 42))
(check "defstruct setf" 99 (let ((p (make-dst-point :x 1))) (setf (dst-point-x p) 99) (dst-point-x p)))
(check "defstruct copier" 1 (let ((a (make-dst-point :x 1))) (let ((b (copy-dst-point a))) (setf (dst-point-x b) 99) (dst-point-x a))))
(check "defstruct typep" t (typep (make-dst-point) 'dst-point))
(check "defstruct typep nil" nil (typep 42 'dst-point))
(check "defstruct type-of" 'dst-point (type-of (make-dst-point)))
(check "defstruct structurep" t (structurep (make-dst-point)))
(check "defstruct structurep nil" nil (structurep 42))
(check "defstruct printer" "#S(DST-POINT :X 1 :Y 2)" (prin1-to-string (make-dst-point :x 1 :y 2)))
(check "defstruct no-slot define" 'empty-st (defstruct empty-st))
(check "defstruct no-slot make" t (empty-st-p (make-empty-st)))
(defstruct (cst (:conc-name c-)) (r 0) (g 0))
(check "defstruct conc-name" 255 (c-r (make-cst :r 255)))
(defstruct (nst (:constructor new-nst)) (val 0))
(check "defstruct custom ctor" 7 (nst-val (new-nst :val 7)))
(defstruct base-st (a 1) (b 2))
(defstruct (child-st (:include base-st)) (c 3))
(check "defstruct include a" 10 (child-st-a (make-child-st :a 10 :c 30)))
(check "defstruct include b" 2 (child-st-b (make-child-st)))
(check "defstruct include c" 30 (child-st-c (make-child-st :c 30)))
(check "defstruct include typep parent" t (typep (make-child-st) 'base-st))
(check "defstruct include typep child" t (typep (make-child-st) 'child-st))
(check "defstruct include typep neg" nil (typep (make-base-st) 'child-st))
;; Multiple (:constructor ...) options — all must be defined (CLHS 3.4.6.6).
;; Regression: previously only the last option survived (esrap failed-parse's
;; make-failed-parse vanished -> "Undefined function").
(defstruct (mctor (:constructor mctor-full (a b c))
                  (:constructor mctor-short (a c)))
  (a nil) (b 0) (c nil))
(check "defstruct multi-ctor full a"  'x (mctor-a (mctor-full 'x 5 'z)))
(check "defstruct multi-ctor full b"  5  (mctor-b (mctor-full 'x 5 'z)))
(check "defstruct multi-ctor short b" 0  (mctor-b (mctor-short 'p 'q)))
(check "defstruct multi-ctor short c" 'q (mctor-c (mctor-short 'p 'q)))
(check "defstruct multi-ctor fboundp 1" t (and (fboundp 'mctor-full) t))
(check "defstruct multi-ctor fboundp 2" t (and (fboundp 'mctor-short) t))
;; Two BOA constructors over (:include)d slots — esrap failed-parse's shape.
(defstruct (mc-base (:constructor nil)) (e nil) (p 0) (d nil))
(defstruct (mc-leaf (:include mc-base)
                    (:constructor mc-leaf-full (e p d))
                    (:constructor mc-leaf/no-pos (e d))))
(check "defstruct multi-ctor include full p" 7 (mc-base-p (mc-leaf-full 'e 7 'd)))
(check "defstruct multi-ctor include nopos p" 0 (mc-base-p (mc-leaf/no-pos 'e 'd)))
(check "defstruct multi-ctor include nopos d" 'd (mc-base-d (mc-leaf/no-pos 'e 'd)))
;; Mixed BOA + keyword constructor on the same struct.
(defstruct (mc-mix (:constructor mc-mix-boa (x y)) (:constructor mc-mix-kw))
  (x 1) (y 2))
(check "defstruct multi-ctor boa+kw boa" 10 (mc-mix-x (mc-mix-boa 10 20)))
(check "defstruct multi-ctor boa+kw kw"  99 (mc-mix-y (mc-mix-kw :y 99)))

; --- define-compiler-macro &environment ---
;; define-compiler-macro must strip &environment from its lambda list (any
;; position, with or without &whole); otherwise it leaks into the inner
;; destructuring-bind as a required param -> "too few elements" at compile.
(defun cmenv-fn (x) x)
(define-compiler-macro cmenv-fn (&whole form x &environment env)
  (declare (ignorable env)) (if (integerp x) (* 2 x) form))
(check "compiler-macro &environment folds" 42 (cmenv-fn 21))
(defparameter *cmv* 5)
(check "compiler-macro &environment declines" 5 (cmenv-fn *cmv*))
(defun cmenv2-fn (a b) (list a b))
(define-compiler-macro cmenv2-fn (a b &environment env)
  (declare (ignorable env)) (list 'list b a))
(check "compiler-macro &environment no-whole" '(2 1) (cmenv2-fn 1 2))

; --- the ---
(check "the fixnum" 42 (the fixnum 42))
(check "the string" "hello" (the string "hello"))
(check "the fixnum expr" 3 (the fixnum (+ 1 2)))
(check "the symbol" 'foo (the symbol 'foo))
(check "the list" '(1 2) (the list '(1 2)))
(check "the null" nil (the null nil))
(check "the or fixnum null 1" 42 (the (or fixnum null) 42))
(check "the or fixnum null 2" nil (the (or fixnum null) nil))
(check "the nested" 3 (the fixnum (the fixnum (+ 1 2))))
(check "the type-error" t (handler-case (progn (the fixnum "oops") nil) (type-error () t)))
; compound FUNCTION/VALUES specs are declaration-only (CLHS 4.2.3):
; THE must not hand them to TYPEP at runtime (jzon regression)
(check "the compound function" 42 (funcall (the (function () t) (lambda () 42))))
(check "the values spec" 7 (the (values t &optional) 7))
(check "the or-with-function" nil (the (or null (function () t)) nil))
(check "the plain function still checks" t
       (handler-case (progn (the function 3) nil) (type-error () t)))
(declaim (optimize (safety 0)))
(check "the safety 0" "oops" (the fixnum "oops"))
(declaim (optimize (safety 1)))

; --- Bignums ---
(check "bignum add overflow" 1073741824 (+ 1073741823 1))
(check "bignum sub overflow" -1073741825 (+ -1073741824 -1))
(check "bignum mul overflow" 1000000000000 (* 1000000 1000000))
(check "bignum demotion" 1073741823 (- (+ 1073741823 1) 1))
(check "bignum read large" 1073741824 1073741824)
(check "bignum expt 2^32" 4294967296 (expt 2 32))
(defun bn-fact (n) (if (<= n 1) 1 (* n (bn-fact (1- n)))))
(check "bignum factorial 10" 3628800 (bn-fact 10))
(check "bignum factorial 20" 2432902008176640000 (bn-fact 20))
(check "bignum compare eq" t (= (expt 2 64) (expt 2 64)))
(check "bignum compare lt" t (< (expt 2 64) (expt 2 65)))
(check "bignum compare gt" t (> (expt 2 65) (expt 2 64)))
(check "bignum compare mixed" t (> (expt 2 64) 42))
(check "bignum negate" t (minusp (- (expt 2 64))))
(check "bignum abs" t (plusp (abs (- (expt 2 64)))))
(check "bignum div" 1125899906842624 (/ (expt 2 100) (expt 2 50)))
(check "bignum mod" 1 (mod 10 3))
(check "bignum gcd" 4 (gcd 12 8))
(check "bignum gcd large" 1125899906842624 (gcd (expt 2 100) (expt 2 50)))
(check "bignum lcm" 12 (lcm 4 6))
(check "bignum evenp" t (evenp (expt 2 64)))
(check "bignum oddp" t (oddp (+ (expt 2 64) 1)))
(check "bignum eql" t (eql (expt 2 64) (expt 2 64)))
(check "bignum logand" 9 (logand 15 9))
(check "bignum logand -1 identity" 65536 (logand -1 65536))
(check "bignum logand -1 multi-limb" 610777 (logand -1 610777))
(check "bignum logior" 15 (logior 15 9))
(check "bignum logxor" 6 (logxor 15 9))
(check "bignum lognot" -1 (lognot 0))
(check "bignum ash left" 1024 (ash 1 10))
(check "bignum ash right" 1 (ash 1024 -10))
(check "bignum integer-length" 8 (integer-length 255))
(check "bignum isqrt" 1000 (isqrt 1000000))
(check "bignum typep fixnum" t (typep 42 'fixnum))
(check "bignum typep bignum" t (typep (expt 2 64) 'bignum))
(check "bignum typep integer" t (typep (expt 2 64) 'integer))
(check "bignum typep number" t (typep (expt 2 64) 'number))
(check "bignum type-of" 'bignum (type-of (expt 2 64)))
(check "bignum numberp" t (numberp (expt 2 64)))
(check "bignum integerp" t (integerp (expt 2 64)))
(check "most-positive-fixnum" 1073741823 most-positive-fixnum)
(check "most-negative-fixnum" -1073741824 most-negative-fixnum)
(check "subtypep bignum<integer" t (subtypep 'bignum 'integer))
(check "subtypep bignum<number" t (subtypep 'bignum 'number))

; --- Complex numbers ---
(check "complex reader" #C(5 16) #C(5 16))
(check "complex eql" t (eql #C(5 16) #C(5 16)))
(check "complex eql diff" nil (eql #C(5 16) #C(5 17)))
(check "complex equal" t (equal #C(3 4) #C(3 4)))
(check "complex complexp" t (complexp #C(5 16)))
(check "complex complexp not" nil (complexp 42))
(check "complex realpart" 5 (realpart #C(5 16)))
(check "complex imagpart" 16 (imagpart #C(5 16)))
(check "complex realpart real" 42 (realpart 42))
(check "complex imagpart real" 0 (imagpart 42))
(check "complex constructor" #C(3 4) (complex 3 4))
(check "complex zero imag" 5 (complex 5 0))
(check "complex type-of" 'complex (type-of #C(1 2)))
(check "complex typep" t (typep #C(1 2) 'complex))
(check "complex numberp" t (numberp #C(1 2)))
(check "complex conjugate" #C(3 -4) (conjugate #C(3 4)))
(check "complex conjugate real" 5 (conjugate 5))
(check "complex negative" #C(-4 15) #C(-4 15))
(check "complex hash-table" 42 (let ((ht (make-hash-table :test 'eql))) (setf (gethash #C(5 16) ht) 42) (gethash #C(5 16) ht)))

; --- Float basics (Steps 1-8) ---
(check "float read single" 3.14 3.14)
(check "float read double" 1.0d0 1.0d0)
(check "float add" 5.0 (+ 2.0 3.0))
(check "float sub" 1.5 (- 3.5 2.0))
(check "float mul" 6.0 (* 2.0 3.0))
(check "float div" 2.5 (/ 5.0 2.0))
(check "float negate" -3.5 (- 3.5))
(check "float compare =" t (= 3.14 3.14))
(check "float compare <" t (< 1.0 2.0))
(check "float compare >" t (> 3.0 2.0))
(check "float zerop" t (zerop 0.0))
(check "float plusp" t (plusp 1.0))
(check "float minusp" t (minusp -1.0))
(check "float abs" 3.5 (abs -3.5))
(check "floatp yes" t (floatp 1.0))
(check "floatp no" nil (floatp 1))
(check "float numberp" t (numberp 1.0))
(check "float typep single" t (typep 1.0 'single-float))
(check "float typep double" t (typep 1.0d0 'double-float))
(check "float typep float" t (typep 1.0 'float))
(check "float eql same" t (eql 1.0 1.0))
(check "float eql diff type" nil (eql 1.0 1.0d0))
(check "float int add" 6.0 (+ 3.0 3))
(check "float contagion" t (typep (+ 1 1.0d0) 'double-float))

; --- Float-specific functions (Step 9) ---
(check "float convert int" 5.0 (float 5))
(check "float convert proto double" 5.0d0 (float 5 1.0d0))
(check "float convert proto single" 5.0 (float 5 1.0))
(check "float passthrough" 3.14 (float 3.14))
(check "float-digits single" 24 (float-digits 1.0))
(check "float-digits double" 53 (float-digits 1.0d0))
(check "float-radix" 2 (float-radix 1.0))
(check "float-sign positive" 1.0 (float-sign 3.5))
(check "float-sign negative" -1.0 (float-sign -3.5))
(check "float-sign two args" -2.5 (float-sign -1.0 2.5))
(check "decode-float 1.5" (list 0.75 1 1.0) (multiple-value-list (decode-float 1.5)))
(check "decode-float -1.5" (list 0.75 1 -1.0) (multiple-value-list (decode-float -1.5)))
(check "decode-float 0.0" (list 0.0 0 1.0) (multiple-value-list (decode-float 0.0)))
(check "decode-float 8.0" (list 0.5 4 1.0) (multiple-value-list (decode-float 8.0)))
(check "integer-decode-float 1.5" (list 12582912 -23 1) (multiple-value-list (integer-decode-float 1.5)))
(check "integer-decode-float 0.0" (list 0 0 1) (multiple-value-list (integer-decode-float 0.0)))
(check "scale-float up" 8.0 (scale-float 1.0 3))
(check "scale-float down" 1.0 (scale-float 8.0 -3))
(check "scale-float double" 8.0d0 (scale-float 1.0d0 3))
(check "decode-float reconstruct" 3.14 (multiple-value-bind (s e g) (decode-float 3.14) (* (scale-float s e) g)))

; --- IEEE-754 raw bit access (float-features :cl-amiga backend, used by jzon) ---
(check "double-float-bits 25.5d0" 4627870829588250624 (clamiga:double-float-bits 25.5d0))
(check "double-float-bits 1.0d0" 4607182418800017408 (clamiga:double-float-bits 1.0d0))
(check "single-float-bits 1.0" 1065353216 (clamiga:single-float-bits 1.0))
(check "single-float-bits 25.5" 1103888384 (clamiga:single-float-bits 25.5))
(check "double-float-bits roundtrip" t (= 25.5d0 (clamiga:bits-double-float (clamiga:double-float-bits 25.5d0))))
(check "single-float-bits roundtrip" t (eql -2.5 (clamiga:bits-single-float (clamiga:single-float-bits -2.5))))
(check "double-float-bits -0.0d0 sign bit" 9223372036854775808 (clamiga:double-float-bits -0.0d0))

; --- Regression: 2-arg LOG of a large double overflowed to infinity ---
(check "log huge double base2" 1024 (round (log most-positive-double-float 2)))
(check "log huge single base2" 128 (round (log most-positive-single-float 2)))
(check "log two-arg 8 2" t (< (abs (- (log 8.0d0 2.0d0) 3.0d0)) 1.0d-10))
(check "log two-arg 100 10" t (< (abs (- (log 100.0d0 10.0d0) 2.0d0)) 1.0d-10))
(check "log complex modulus" t (< (abs (- (realpart (log #C(3.0d0 4.0d0))) 1.6094379124341003d0)) 1.0d-10))

; --- Rounding functions (Step 10) ---
(check "truncate float 1arg" 2 (truncate 2.7))
(check "truncate float neg" -2 (truncate -2.7))
(check "truncate mv" (list 3 1) (multiple-value-list (truncate 7 2)))
(check "truncate mv neg" (list -3 -1) (multiple-value-list (truncate -7 2)))
(check "truncate mv float" (list 2 0.5) (multiple-value-list (truncate 2.5)))
(check "floor int" 3 (floor 7 2))
(check "floor int neg" -4 (floor -7 2))
(check "floor float 1arg" 2 (floor 2.7))
(check "floor float neg" -3 (floor -2.7))
(check "floor mv" (list 3 1) (multiple-value-list (floor 7 2)))
(check "floor mv neg" (list -4 1) (multiple-value-list (floor -7 2)))
(check "floor mv float" (list 2 0.5) (multiple-value-list (floor 2.5)))
(check "ceiling int" 4 (ceiling 7 2))
(check "ceiling int neg" -3 (ceiling -7 2))
(check "ceiling float 1arg" 3 (ceiling 2.3))
(check "ceiling float neg" -2 (ceiling -2.3))
(check "ceiling mv" (list 4 -1) (multiple-value-list (ceiling 7 2)))
(check "ceiling mv neg" (list -3 -1) (multiple-value-list (ceiling -7 2)))
(check "round banker even" 2 (round 2.5))
(check "round banker odd" 4 (round 3.5))
(check "round down" 2 (round 2.3))
(check "round up" 3 (round 2.7))
(check "round neg" -2 (round -2.5))
(check "round int" 4 (round 7 2))
(check "round int even" 2 (round 5 2))
(check "ffloor float" 2.0 (ffloor 2.7))
(check "ffloor neg" -3.0 (ffloor -2.7))
(check "ffloor int" 3.0 (ffloor 7 2))
(check "fceiling float" 3.0 (fceiling 2.3))
(check "ftruncate float" 2.0 (ftruncate 2.7))
(check "ftruncate neg" -2.0 (ftruncate -2.7))
(check "fround even" 2.0 (fround 2.5))
(check "fround odd" 4.0 (fround 3.5))
(check "ffloor double" 2.0d0 (ffloor 2.7d0))
(check "mod float" 1.0 (mod 10.0 3))
(check "mod float neg" 2.0 (mod -10.0 3))
(check "mod float neg div" -2.0 (mod 10.0 -3))
(check "rem float" 1.0 (rem 10.0 3))
(check "rem float neg" -1.0 (rem -10.0 3))
(check "rem float neg div" 1.0 (rem 10.0 -3))

; --- Streams (Phase 7 Step 1) ---
(check "streamp on stream" t (streamp (%make-test-stream 1 0)))
(check "streamp on fixnum" nil (streamp 42))
(check "streamp on string" nil (streamp "hello"))
(check "streamp on nil" nil (streamp nil))
(check "type-of stream" 'stream (type-of (%make-test-stream 1 0)))
(check "typep stream" t (typep (%make-test-stream 2 0) 'stream))
(check "input-stream-p input" t (input-stream-p (%make-test-stream 1 0)))
(check "input-stream-p output" nil (input-stream-p (%make-test-stream 2 0)))
(check "input-stream-p io" t (input-stream-p (%make-test-stream 3 0)))
(check "output-stream-p output" t (output-stream-p (%make-test-stream 2 0)))
(check "output-stream-p input" nil (output-stream-p (%make-test-stream 1 0)))
(check "output-stream-p io" t (output-stream-p (%make-test-stream 3 0)))
(check "stream console type" t (streamp (%make-test-stream 1 0)))
(check "stream file type" t (streamp (%make-test-stream 1 1)))
(check "stream string type" t (streamp (%make-test-stream 1 2)))

; --- Streams (Phase 7 Step 2) ---
; Standard stream variables are bound to streams
(check "standard-input bound" t (streamp *standard-input*))
(check "standard-output bound" t (streamp *standard-output*))
(check "error-output bound" t (streamp *error-output*))
(check "trace-output bound" t (streamp *trace-output*))
(check "debug-io bound" t (streamp *debug-io*))
(check "query-io bound" t (streamp *query-io*))
(check "terminal-io bound" t (streamp *terminal-io*))
; Direction checks
(check "standard-input is input" t (input-stream-p *standard-input*))
(check "standard-input not output" nil (output-stream-p *standard-input*))
(check "standard-output is output" t (output-stream-p *standard-output*))
(check "standard-output not input" nil (input-stream-p *standard-output*))
(check "error-output is output" t (output-stream-p *error-output*))
; interactive-stream-p
(check "interactive-stream-p console" t (interactive-stream-p *standard-output*))
(check "interactive-stream-p stdin" t (interactive-stream-p *standard-input*))
(check "interactive-stream-p file" nil (interactive-stream-p (%make-test-stream 1 1)))
(check "interactive-stream-p string" nil (interactive-stream-p (%make-test-stream 1 2)))

; --- Streams (Phase 7 Step 3) ---
; String output streams
(check "make-string-output-stream" t (streamp (make-string-output-stream)))
(check "write-char to string stream" "A" (let ((s (make-string-output-stream))) (write-char #\A s) (get-output-stream-string s)))
(check "write-string to string stream" "Hello" (let ((s (make-string-output-stream))) (write-string "Hello" s) (get-output-stream-string s)))
(check "write-line to string stream" (concatenate 'string "Hello" (string #\Newline)) (let ((s (make-string-output-stream))) (write-line "Hello" s) (get-output-stream-string s)))
(check "get-output-stream-string resets" "second" (let ((s (make-string-output-stream))) (write-string "first" s) (get-output-stream-string s) (write-string "second" s) (get-output-stream-string s)))
(check "multiple write-char" "ABC" (let ((s (make-string-output-stream))) (write-char #\A s) (write-char #\B s) (write-char #\C s) (get-output-stream-string s)))

; String input streams
(check "make-string-input-stream" t (streamp (make-string-input-stream "hello")))
(check "read-char from string" #\H (read-char (make-string-input-stream "Hello")))
(check "read-char eof error" t (handler-case (progn (read-char (make-string-input-stream "")) nil) (error (c) t)))
(check "read-char eof no error" :eof (read-char (make-string-input-stream "") nil :eof))
(check "read-line from string" "Hello" (read-line (make-string-input-stream "Hello")))
(check "read-line with newline" "Hello" (read-line (make-string-input-stream (concatenate 'string "Hello" (string #\Newline) "World"))))
(check "read-line missing-newline-p" t (nth-value 1 (read-line (make-string-input-stream "Hello"))))
(check "read-line has newline" nil (nth-value 1 (read-line (make-string-input-stream (concatenate 'string "Hello" (string #\Newline))))))
(check "string-input-stream start end" #\W (read-char (make-string-input-stream "Hello World" 6)))
; Regression: cl_make_string_input_stream must GC-protect its source string
; across cl_make_stream's allocation, else st->string_buf goes stale under a
; moving GC and read-from-string drops/mangles list elements (>=3-elem lists).
(check "read-from-string 3-elem length" 3 (length (read-from-string "(a b c)")))
(check "read-from-string 5-elem length" 5 (length (read-from-string "(a b c d e)")))
(check "read-from-string contents intact" '(a b c) (read-from-string "(a b c)"))
(check "read-from-string nested length" 3 (length (read-from-string "((1 2) (3 4) (5 6))")))

; peek-char
(check "peek-char nil" #\A (peek-char nil (make-string-input-stream "ABC")))
(check "peek-char no consume" #\A (let ((s (make-string-input-stream "ABC"))) (peek-char nil s) (read-char s)))
(check "peek-char t skip ws" #\A (peek-char t (make-string-input-stream "  A")))
(check "peek-char char skip" #\B (peek-char #\B (make-string-input-stream "xxByz")))

; unread-char
(check "unread-char" #\H (let ((s (make-string-input-stream "Hi"))) (let ((c (read-char s))) (unread-char c s) (read-char s))))

; with-input-from-string :start/:end/:index (CLHS) and file-position vs peek/unread.
; Regression for serapeum PARSE-FLOAT's second value (stop index over junk).
(check "wifs :start value" 1 (with-input-from-string (s "21" :start 1) (read s)))
(check "wifs :index value" 2 (let (pos) (with-input-from-string (s "21" :start 1 :index pos) (read s)) pos))
(check "wifs :end bounds" 12 (with-input-from-string (s "12 34" :end 2) (read s nil :eof)))
(check "file-position after peek" 1
  (let (pos) (with-input-from-string (s "2x" :index pos) (read-char s) (peek-char nil s nil nil)) pos))
(check "file-position after unread" 1
  (let ((s (make-string-input-stream "ab"))) (read-char s) (read-char s) (unread-char #\b s) (file-position s)))

; write-string :start :end
(check "write-string start end" "llo" (let ((s (make-string-output-stream))) (write-string "Hello" s :start 2 :end 5) (get-output-stream-string s)))

; open-stream-p / close
(check "open-stream-p open" t (open-stream-p (make-string-output-stream)))
(check "close returns t" t (close (make-string-output-stream)))
(check "open-stream-p closed" nil (let ((s (make-string-output-stream))) (close s) (open-stream-p s)))

; terpri with stream
(check "terpri to stream" (string #\Newline) (let ((s (make-string-output-stream))) (terpri s) (get-output-stream-string s)))

; fresh-line
(check "fresh-line at BOL" nil (let ((s (make-string-output-stream))) (fresh-line s)))
(check "fresh-line not at BOL" t (let ((s (make-string-output-stream))) (write-char #\X s) (fresh-line s)))
(check "fresh-line writes newline" (concatenate 'string "X" (string #\Newline)) (let ((s (make-string-output-stream))) (write-char #\X s) (fresh-line s) (get-output-stream-string s)))

; finish-output / force-output / clear-output (no error)
(check "finish-output no error" nil (finish-output))
(check "force-output no error" nil (force-output))
(check "clear-output no error" nil (clear-output))

; write-char returns character
(check "write-char returns char" #\X (write-char #\X (make-string-output-stream)))

; --- Read from stream ---
(check "read from string-input-stream list" '(+ 1 2) (read (make-string-input-stream "(+ 1 2)")))
(check "read from string-input-stream integer" 42 (read (make-string-input-stream "42")))
(check "read from string-input-stream symbol" 'hello (read (make-string-input-stream "hello")))
(check "read from string-input-stream string" "hello" (read (make-string-input-stream "\"hello\"")))
(check "read eof-value" :eof (read (make-string-input-stream "") nil :eof))
(check "read-from-string list" '(hello world) (read-from-string "(hello world)"))
(check "read-from-string integer" 42 (read-from-string "42"))
(check "read-from-string eof-value" :eof (read-from-string "" nil :eof))

; --- Two-way streams ---
(check "make-two-way-stream returns stream" t
  (streamp (make-two-way-stream (make-string-input-stream "") (make-string-output-stream))))
(check "two-way-stream input-stream-p" t
  (input-stream-p (make-two-way-stream (make-string-input-stream "") (make-string-output-stream))))
(check "two-way-stream output-stream-p" t
  (output-stream-p (make-two-way-stream (make-string-input-stream "") (make-string-output-stream))))
(check "two-way-stream-input-stream identity" t
  (let* ((in (make-string-input-stream ""))
         (tw (make-two-way-stream in (make-string-output-stream))))
    (eq (two-way-stream-input-stream tw) in)))
(check "two-way-stream-output-stream identity" t
  (let* ((out (make-string-output-stream))
         (tw (make-two-way-stream (make-string-input-stream "") out)))
    (eq (two-way-stream-output-stream tw) out)))
(check "two-way-stream read from input" #\H
  (let ((tw (make-two-way-stream (make-string-input-stream "Hello") (make-string-output-stream))))
    (read-char tw)))
(check "two-way-stream write to output" "Hi"
  (let* ((out (make-string-output-stream))
         (tw (make-two-way-stream (make-string-input-stream "") out)))
    (write-string "Hi" tw)
    (get-output-stream-string out)))
(check "two-way-stream read-write roundtrip" "pong"
  (let* ((out (make-string-output-stream))
         (tw (make-two-way-stream (make-string-input-stream "ping") out)))
    (read-char tw)
    (write-string "pong" tw)
    (get-output-stream-string out)))
(check "two-way-stream typep two-way-stream" t
  (typep (make-two-way-stream (make-string-input-stream "") (make-string-output-stream)) 'two-way-stream))
(check "two-way-stream typep stream" t
  (typep (make-two-way-stream (make-string-input-stream "") (make-string-output-stream)) 'stream))
(check "two-way-stream typep synonym-stream nil" nil
  (typep (make-two-way-stream (make-string-input-stream "") (make-string-output-stream)) 'synonym-stream))
(check "two-way-stream-input-stream type error" :type-error
  (handler-case (two-way-stream-input-stream (make-string-output-stream))
    (error (c) (declare (ignore c)) :type-error)))
(check "two-way-stream-output-stream type error" :type-error
  (handler-case (two-way-stream-output-stream (make-string-input-stream ""))
    (error (c) (declare (ignore c)) :type-error)))

; --- Broadcast / concatenated streams (CLHS 21.2) ---
(check "broadcast-stream fans out writes" '("hi!" "hi!")
  (let* ((a (make-string-output-stream))
         (b (make-string-output-stream))
         (bc (make-broadcast-stream a b)))
    (write-string "hi" bc)
    (write-char #\! bc)
    (list (get-output-stream-string a) (get-output-stream-string b))))
(check "broadcast-stream no components discards" :ok
  (let ((bc (make-broadcast-stream)))
    (write-string "dropped" bc)
    (finish-output bc)
    :ok))
(check "broadcast-stream-streams identity" t
  (let* ((a (make-string-output-stream))
         (bc (make-broadcast-stream a)))
    (eq (car (broadcast-stream-streams bc)) a)))
(check "broadcast-stream typep" t
  (typep (make-broadcast-stream) 'broadcast-stream))
(check "broadcast-stream fresh-line uses component column" '(t nil)
  (let* ((a (make-string-output-stream))
         (bc (make-broadcast-stream a)))
    (write-string "x" bc)
    (list (fresh-line bc) (fresh-line bc))))
(check "make-broadcast-stream rejects input stream" :type-error
  (handler-case (make-broadcast-stream (make-string-input-stream ""))
    (error (c) (declare (ignore c)) :type-error)))
(check "concatenated-stream reads in order" "abcdef"
  (let ((cc (make-concatenated-stream
             (make-string-input-stream "abc")
             (make-string-input-stream "")
             (make-string-input-stream "def")))
        (acc (make-string-output-stream)))
    (loop for ch = (read-char cc nil nil) while ch
          do (write-char ch acc))
    (get-output-stream-string acc)))
(check "concatenated-stream no components is eof" :eof
  (read-char (make-concatenated-stream) nil :eof))
(check "concatenated-stream-streams drains to nil" nil
  (let ((cc (make-concatenated-stream (make-string-input-stream "a"))))
    (read-char cc)
    (read-char cc nil nil)
    (concatenated-stream-streams cc)))
(check "concatenated-stream read-line spans components" "foobar"
  (read-line (make-concatenated-stream
              (make-string-input-stream "foo")
              (make-string-input-stream (format nil "bar~%")))))
(check "concatenated-stream typep" t
  (typep (make-concatenated-stream) 'concatenated-stream))
(check "make-concatenated-stream rejects output stream" :type-error
  (handler-case (make-concatenated-stream (make-string-output-stream))
    (error (c) (declare (ignore c)) :type-error)))
(check "icl slynk muffle pattern (two-way over concatenated+broadcast)" '(:eof :eof)
  (let* ((null-stream (make-broadcast-stream))
         (dbg (make-two-way-stream (make-concatenated-stream) null-stream)))
    (write-line "muffled" dbg)
    (list (read-char dbg nil :eof) (read-line dbg nil :eof))))
(check "load from stream (CLHS filespec stream)" 42
  (progn
    (with-input-from-string (s "(defvar cl-user::*lfs-var* 41)
                                (defun cl-user::lfs-fn () (1+ cl-user::*lfs-var*))")
      (load s))
    (cl-user::lfs-fn)))
(check "load from stream binds *package*" t
  (let ((before (package-name *package*)))
    (with-input-from-string (s "(in-package :keyword)") (load s))
    (string= before (package-name *package*))))
(check "load from stream binds *load-pathname* to nil" '(t nil)
  (progn
    (with-input-from-string (s "(defvar cl-user::*lfs-lp* :unset)
                                (setq cl-user::*lfs-lp* *load-pathname*)")
      (load s))
    (list t cl-user::*lfs-lp*)))

; --- Regression: FASL auto-cache stays fresh after an in-place rewrite ---
; FS-UAE directory hard drives keep a replaced file's ORIGINAL datestamp
; (the .uaem sidecar is not refreshed on overwrite), so a recompiled cache
; FASL still Examine()d as older than the edited source — stale forever,
; recompiled on EVERY launch (Closure on the 020: ~5.5 min of recompiles
; per launch).  bi_load now deletes the cache file before rewriting it so
; the recreated file carries a fresh stamp.  Compiles are counted through
; a macro whose EXPANSION bumps a counter: a source compile expands it, a
; cache (FASL) hit evaluates the already-expanded bytecode and does not.
; Sequence: cold load (compile #1) -> rewrite source, load (compile #2,
; cache rewritten) -> load again (must hit the cache: still 2).
(defvar *recache-count* 0)
(defmacro recache-bump ()
  (setq *recache-count* (+ *recache-count* 1))
  nil)
(check "fasl auto-cache fresh after rewrite (uaem datestamp regression)" 2
  (let ((path "T:recache-regress.lisp"))
    (flet ((write-src ()
             ;; delete-then-create (not :supersede) so the SOURCE also
             ;; gets a fresh datestamp on FS-UAE directory volumes — the
             ;; same overwrite quirk the cache fix works around
             (when (probe-file path) (delete-file path))
             (with-open-file (s path :direction :output
                                     :if-does-not-exist :create)
               (write-line "(cl-user::recache-bump)" s)
               (write-line "(defvar cl-user::*recache-dummy* t)" s))))
      (setq *recache-count* 0)
      (write-src)
      (load path)     ; cold: compile #1, cache FASL written
      (sleep 2)       ; datestamp granularity: source must become strictly newer
      (write-src)     ; source rewritten with a fresh stamp
      (load path)     ; stale cache: compile #2, cache FASL rewritten
      (load path)     ; must load from the cache — no further compile
      *recache-count*)))

; --- TCP sockets (server side: socket-listen / socket-accept / socket-local-port) ---
; FS-UAE provides a TCP stack (bsdsocket.library on Amiga), so these run for
; real.  Single-threaded loopback pattern, same as the host tests: a loopback
; connect lands in the accept backlog, so socket-accept returns without
; blocking.  Each test closes its sockets to avoid exhausting the 16-slot table.
(check "socket-listen ephemeral returns stream" t
  (let ((l (ext:socket-listen 0 t)))
    (unwind-protect (streamp l) (close l))))
(check "socket-local-port ephemeral positive" t
  (let ((l (ext:socket-listen 0 t)))
    (unwind-protect
         (let ((p (ext:socket-local-port l)))
           (and (integerp p) (> p 0)))
      (close l))))
(check "socket-local-port on non-listener errors" :not-a-listener
  (handler-case (ext:socket-local-port (make-string-output-stream))
    (error (c) (declare (ignore c)) :not-a-listener)))
; Full round-trip driven entirely by the OS-assigned port from socket-local-port
; (the exact shape a Sly/SLYNK server uses to advertise its port).
(check "socket round-trip via local-port" 90  ; #\Z
  (let ((l (ext:socket-listen 0 t)))
    (unwind-protect
         (let* ((p (ext:socket-local-port l))
                (c (ext:open-tcp-stream "127.0.0.1" p))
                (s (ext:socket-accept l)))
           (unwind-protect
                (progn
                  (write-char #\Z c)
                  (force-output c)
                  (char-code (read-char s)))
             (close c) (close s)))
      (close l))))

; --- UDP datagram sockets (ext:open-udp-stream & co.) ---
; UDP connect only records the peer (no handshake), so these run without a
; live peer: the datagram to the discard port goes nowhere, and the receive
; must trip the read timeout rather than block the suite.
(check "udp open + local endpoint" t
  (let ((s (ext:open-udp-stream "127.0.0.1" 9)))
    (unwind-protect
         (multiple-value-bind (ip port)
             (ext:socket-stream-local-endpoint s)
           (and (stringp ip) (integerp port) (> port 0)))
      (close s))))
(check "udp send returns t" t
  (let ((s (ext:open-udp-stream "127.0.0.1" 9)))
    (unwind-protect
         (ext:udp-stream-send s (coerce #(1 2 3) '(vector (unsigned-byte 8))) 3)
      (close s))))
; No send before the receive: a datagram to the (dead) discard port raises
; an ICMP port-unreachable that a connected UDP socket reports as an error
; on the NEXT recv — which would mask the timeout being tested here.
(check "udp receive timeout" :timed-out
  (let ((s (ext:open-udp-stream "127.0.0.1" 9))
        (buf (make-array 8 :element-type '(unsigned-byte 8))))
    (unwind-protect
         (progn
           (setf (ext:socket-stream-timeout s :input) 0.2)
           (handler-case (progn (ext:udp-stream-receive s buf) :data)
             (ext:socket-timeout () :timed-out)))
      (close s))))
(check "udp send rejects non-udp stream" :rejected
  (handler-case (ext:udp-stream-send (make-string-output-stream)
                                     (make-array 1 :element-type '(unsigned-byte 8)))
    (error () :rejected)))

; --- Socket read/write timeouts (ext:socket-stream-timeout) ---
; (setf (ext:socket-stream-timeout stream :input/:output) seconds) arms a
; WaitSelect deadline in the platform socket layer; a read/write that misses it
; raises EXT:SOCKET-TIMEOUT (a subtype of STREAM-ERROR) instead of blocking.
; 0.5 is exactly representable, so the numeric = round-trip is float-safe.
(check "socket-stream-timeout accessor round-trips (input)" t
  (let ((l (ext:socket-listen 0 t)))
    (unwind-protect
         (let* ((p (ext:socket-local-port l))
                (c (ext:open-tcp-stream "127.0.0.1" p))
                (s (ext:socket-accept l)))
           (unwind-protect
                (progn
                  (setf (ext:socket-stream-timeout c :input) 0.5)
                  (= 0.5 (ext:socket-stream-timeout c :input)))
             (close c) (close s)))
      (close l))))

(check "socket-stream-timeout unset reads NIL; :output independent" t
  (let ((l (ext:socket-listen 0 t)))
    (unwind-protect
         (let* ((p (ext:socket-local-port l))
                (c (ext:open-tcp-stream "127.0.0.1" p))
                (s (ext:socket-accept l)))
           (unwind-protect
                (progn
                  (setf (ext:socket-stream-timeout c :input) 1)
                  (null (ext:socket-stream-timeout c :output)))
             (close c) (close s)))
      (close l))))

; A read that exceeds the deadline (peer sends nothing) signals EXT:SOCKET-TIMEOUT,
; which must be catchable both as socket-timeout and as a generic stream-error.
(check "socket read timeout signals EXT:SOCKET-TIMEOUT" :timeout-streamerr
  (let ((l (ext:socket-listen 0 t)))
    (unwind-protect
         (let* ((p (ext:socket-local-port l))
                (c (ext:open-tcp-stream "127.0.0.1" p))
                (s (ext:socket-accept l)))
           (unwind-protect
                (progn
                  (setf (ext:socket-stream-timeout c :input) 0.5)
                  ;; server (s) deliberately sends nothing
                  (handler-case (progn (read-char c) :no-timeout)
                    (ext:socket-timeout (e)
                      (if (typep e 'stream-error) :timeout-streamerr :timeout))))
             (close c) (close s)))
      (close l))))

; Arming a write timeout must not break a normal write to a draining peer, and
; the timed flush (force-output) must not spuriously signal.  (A write timeout
; cannot be reliably *triggered* over loopback — buffers absorb the data — so we
; test the complementary success path; the write deadline shares the read path's
; mechanism, which the read-timeout test above exercises.)
(check "write with timeout set succeeds to a draining peer" 90  ; #\Z
  (let ((l (ext:socket-listen 0 t)))
    (unwind-protect
         (let* ((p (ext:socket-local-port l))
                (c (ext:open-tcp-stream "127.0.0.1" p))
                (s (ext:socket-accept l)))
           (unwind-protect
                (progn
                  (setf (ext:socket-stream-timeout c :output) 5)  ; generous deadline
                  (write-char #\Z c)
                  (force-output c)             ; timed flush path; must not signal
                  (char-code (read-char s)))   ; peer drains -> verifies receipt
             (close c) (close s)))
      (close l))))

(check "socket-stream-timeout bad direction is a type error" :bad-dir
  (let ((l (ext:socket-listen 0 t)))
    (unwind-protect
         (let* ((p (ext:socket-local-port l))
                (c (ext:open-tcp-stream "127.0.0.1" p))
                (s (ext:socket-accept l)))
           (unwind-protect
                (handler-case (ext:socket-stream-timeout c :sideways)
                  (type-error () :bad-dir))
             (close c) (close s)))
      (close l))))

; --- Connect timeout (ext:open-tcp-stream third arg) ---
; The optional connect-timeout (seconds) bounds the TCP handshake so an
; unreachable host fails fast instead of stalling on the OS connect timeout.
; A reachable peer must still connect normally when a budget is supplied.
(check "open-tcp-stream connect timeout: reachable peer round-trips" 90  ; #\Z
  (let ((l (ext:socket-listen 0 t)))
    (unwind-protect
         (let* ((p (ext:socket-local-port l))
                (c (ext:open-tcp-stream "127.0.0.1" p 5))  ; 5s budget, plenty
                (s (ext:socket-accept l)))
           (unwind-protect
                (progn (write-char #\Z c) (force-output c)
                       (char-code (read-char s)))
             (close c) (close s)))
      (close l))))

; A connect to a port with nothing listening must fail (refused) rather than
; succeed, and must not hang — the bounded path tears the socket down.  Bind an
; ephemeral port, then close the listener so the port is free but unserved.
(check "open-tcp-stream connect timeout: refused port fails fast" :refused
  (let* ((l (ext:socket-listen 0 t))
         (p (ext:socket-local-port l)))
    (close l)
    (handler-case
        (let ((c (ext:open-tcp-stream "127.0.0.1" p 2)))
          (close c) :connected)
      (error () :refused))))

; A hostname that doesn't fit the internal 256-byte copy buffer must signal an
; error rather than silently connecting to a truncated (and wrong) host.
(check "open-tcp-stream hostname too long signals error" :too-long
  (handler-case
      (let ((c (ext:open-tcp-stream (make-string 300 :initial-element #\a) 80)))
        (close c) :connected)
    (error () :too-long)))

; --- Concurrent socket I/O: a thread parked in a blocking socket read must
; not block other I/O (regression for the SLY :spawn deadlock).  Stream I/O
; locks are split per socket and per direction (src/core/stream.c), and the
; blocking recv() runs inside a GC safe region (platform_amiga.c), so a parked
; reader holds only that socket's read lock and counts as stopped during GC.
; If either regresses the operation below hangs forever — the FS-UAE watchdog
; then kills the run and the suite fails loudly.
;
; A reader thread parks in (read-char s) on the server side; the main thread
; must still be able to write the reply on that SAME socket (opposite
; direction) and have the client receive it.
(check "socket read does not block reply on same socket" 65  ; #\A
  (let ((l (ext:socket-listen 0 t)))
    (unwind-protect
         (let* ((p (ext:socket-local-port l))
                (c (ext:open-tcp-stream "127.0.0.1" p))
                (s (ext:socket-accept l)))
           (unwind-protect
                (let ((reader (mp:make-thread
                                (lambda () (read-char s nil :eof))
                                :name "sock-reader")))
                  (sleep 0.5)                 ; let reader park in (read-char s)
                  (write-char #\A s)          ; reply on the same socket
                  (force-output s)
                  (let ((got (char-code (read-char c))))
                    (write-char #\Z c)        ; unblock the reader
                    (force-output c)
                    (mp:join-thread reader)
                    got))
             (close c) (close s)))
      (close l))))

; A full stop-the-world GC must return while a thread is parked in a socket
; read — the user's literal "ext:gc deadlocks" symptom.
(check "GC returns while a thread is parked in socket read" :ok
  (let ((l (ext:socket-listen 0 t)))
    (unwind-protect
         (let* ((p (ext:socket-local-port l))
                (c (ext:open-tcp-stream "127.0.0.1" p))
                (s (ext:socket-accept l)))
           (unwind-protect
                (let ((reader (mp:make-thread
                                (lambda () (read-char s nil :eof))
                                :name "sock-reader")))
                  (sleep 0.5)                 ; let reader park in (read-char s)
                  (ext:gc)                    ; must not wait forever for reader
                  (write-char #\Z c)          ; unblock the reader
                  (force-output c)
                  (mp:join-thread reader)
                  :ok)
             (close c) (close s)))
      (close l))))

; --- Printer + stream integration (Step 7) ---

; prin1/princ/print with optional stream arg
(check "prin1 to stream" "42" (let ((s (make-string-output-stream))) (prin1 42 s) (get-output-stream-string s)))
(check "princ to stream" "hello" (let ((s (make-string-output-stream))) (princ "hello" s) (get-output-stream-string s)))
(check "prin1 string escaped" "\"hello\"" (let ((s (make-string-output-stream))) (prin1 "hello" s) (get-output-stream-string s)))
(check "princ string unescaped" "hello" (let ((s (make-string-output-stream))) (princ "hello" s) (get-output-stream-string s)))
(check "prin1 list to stream" "(1 2 3)" (let ((s (make-string-output-stream))) (prin1 '(1 2 3) s) (get-output-stream-string s)))
(check "prin1 nil to stream" "NIL" (let ((s (make-string-output-stream))) (prin1 nil s) (get-output-stream-string s)))
(check "prin1 char to stream" "#\\A" (let ((s (make-string-output-stream))) (prin1 #\A s) (get-output-stream-string s)))
(check "princ char to stream" "A" (let ((s (make-string-output-stream))) (princ #\A s) (get-output-stream-string s)))
(check "multiple prints to stream" "1+2=3" (let ((s (make-string-output-stream))) (princ 1 s) (write-char #\+ s) (princ 2 s) (write-char #\= s) (princ 3 s) (get-output-stream-string s)))
(check "prin1 returns object" 42 (prin1 42 (make-string-output-stream)))
(check "princ returns object" "hi" (princ "hi" (make-string-output-stream)))

; format with destination
(check "format nil returns string" "hello 42" (format nil "hello ~A" 42))
(check "format nil multi args" "1 + 2 = 3" (format nil "~A + ~A = ~A" 1 2 3))
(check "format nil ~S" "\"hi\"" (format nil "~S" "hi"))
(check "format nil tilde" "~" (format nil "~~"))
(check "format to stream" "val=99" (let ((s (make-string-output-stream))) (format s "val=~A" 99) (get-output-stream-string s)))
(check "format t returns nil" nil (format t ""))

; with-output-to-string macro
(check "with-output-to-string basic" "hello" (with-output-to-string (s) (write-string "hello" s)))
(check "with-output-to-string format" "1 + 2 = 3" (with-output-to-string (s) (format s "~A + ~A = ~A" 1 2 3)))
(check "with-output-to-string prin1" "42" (with-output-to-string (s) (prin1 42 s)))
(check "with-output-to-string multiple" "AB" (with-output-to-string (s) (princ "A" s) (princ "B" s)))

; with-input-from-string macro
(check "with-input-from-string read" '(+ 1 2) (with-input-from-string (s "(+ 1 2)") (read s)))
(check "with-input-from-string read-char" #\H (with-input-from-string (s "Hello") (read-char s)))

; prin1-to-string / princ-to-string / write-to-string
(check "prin1-to-string fixnum" "42" (prin1-to-string 42))
(check "prin1-to-string string" "\"hello\"" (prin1-to-string "hello"))
(check "prin1-to-string list" "(1 2 3)" (prin1-to-string '(1 2 3)))
(check "prin1-to-string nil" "NIL" (prin1-to-string nil))
(check "prin1-to-string char" "#\\A" (prin1-to-string #\A))
(check "princ-to-string string" "hello" (princ-to-string "hello"))
(check "princ-to-string fixnum" "42" (princ-to-string 42))
(check "write-to-string" "(1 2 3)" (write-to-string '(1 2 3)))

; --- Float printer *read-default-float-format* (regression) ---
(check "print double default-single" "1.5d0" (prin1-to-string 1.5d0))
(check "print double default-double markerless" "1.5" (let ((*read-default-float-format* 'double-float)) (prin1-to-string 1.5d0)))
(check "print double 5.0d0 default-double" "5.0" (let ((*read-default-float-format* 'double-float)) (prin1-to-string 5.0d0)))
(check "print single default-single markerless" "1.5" (prin1-to-string 1.5))
(check "print single default-double f0" "1.5f0" (let ((*read-default-float-format* 'double-float)) (prin1-to-string 1.5)))
(check "print single 5.0 default-double" "5.0f0" (let ((*read-default-float-format* 'double-float)) (prin1-to-string 5.0)))
(check "print single sci-notation default-double" "1f-05" (let ((*read-default-float-format* 'double-float)) (prin1-to-string 1.0e-5)))

; --- Float printer full-precision round-trip (regression: %g 6-sig-dig truncation) ---
(check "float printer single full precision" "1234.567" (prin1-to-string 1234.567))
(check "float printer single roundtrip" t (= 1234.567 (read-from-string (prin1-to-string 1234.567))))
(check "float printer double pi full precision" "3.141592653589793d0" (prin1-to-string (coerce pi 'double-float)))
(check "float printer double pi roundtrip" t (= 3.141592653589793d0 (read-from-string (prin1-to-string 3.141592653589793d0))))
(check "format ~f full precision" "1234.567" (format nil "~f" 1234.567))

; --- File streams / open / with-open-file (Step 8) ---
; Use /tmp/ on POSIX, T: on Amiga — both are writable temp directories
; Detect by trying /tmp/ first (fails on Amiga), fall back to T:
(defparameter *test-tmp*
  (handler-case
    (let ((s (open "/tmp/_cltest_probe" :direction :output)))
      (close s) "/tmp/")
    (error (c) "T:")))

; open for output and input
(check "open output creates stream" t (let ((s (open (concatenate 'string *test-tmp* "_clt_s8.txt") :direction :output))) (close s) (streamp s)))
(let ((s (open (concatenate 'string *test-tmp* "_clt_s8.txt") :direction :output))) (write-string "Test123" s) (close s))
(check "open input reads" "Test123" (let ((s (open (concatenate 'string *test-tmp* "_clt_s8.txt") :direction :input))) (let ((line (read-line s))) (close s) line)))

; open with :if-exists :append
(let ((s (open (concatenate 'string *test-tmp* "_clt_s8a.txt") :direction :output))) (write-string "First" s) (close s))
(let ((s (open (concatenate 'string *test-tmp* "_clt_s8a.txt") :direction :output :if-exists :append))) (write-string "Second" s) (close s))
(check "open append" "FirstSecond" (let ((s (open (concatenate 'string *test-tmp* "_clt_s8a.txt") :direction :input))) (let ((line (read-line s))) (close s) line)))

; open with :if-exists :supersede (overwrites existing file)
(let ((s (open (concatenate 'string *test-tmp* "_clt_sup.txt") :direction :output)))
  (write-string "Original" s) (close s))
(let ((s (open (concatenate 'string *test-tmp* "_clt_sup.txt") :direction :output :if-exists :supersede)))
  (write-string "New" s) (close s))
(check "open supersede" "New"
  (with-open-file (s (concatenate 'string *test-tmp* "_clt_sup.txt") :direction :input)
    (read-line s)))

; open with :if-exists :rename-and-delete
(let ((s (open (concatenate 'string *test-tmp* "_clt_rad.txt") :direction :output)))
  (write-string "Before" s) (close s))
(let ((s (open (concatenate 'string *test-tmp* "_clt_rad.txt") :direction :output :if-exists :rename-and-delete)))
  (write-string "After" s) (close s))
(check "open rename-and-delete" "After"
  (with-open-file (s (concatenate 'string *test-tmp* "_clt_rad.txt") :direction :input)
    (read-line s)))

; open with :if-exists :rename
(let ((s (open (concatenate 'string *test-tmp* "_clt_ren.txt") :direction :output)))
  (write-string "Old" s) (close s))
(let ((s (open (concatenate 'string *test-tmp* "_clt_ren.txt") :direction :output :if-exists :rename)))
  (write-string "Fresh" s) (close s))
(check "open rename creates new" "Fresh"
  (with-open-file (s (concatenate 'string *test-tmp* "_clt_ren.txt") :direction :input)
    (read-line s)))
(check "open rename keeps backup" t
  (not (null (probe-file (concatenate 'string *test-tmp* "_clt_ren.txt.bak")))))

; open with :if-exists :overwrite (treated as supersede)
(let ((s (open (concatenate 'string *test-tmp* "_clt_ovw.txt") :direction :output)))
  (write-string "ABCDEF" s) (close s))
(let ((s (open (concatenate 'string *test-tmp* "_clt_ovw.txt") :direction :output :if-exists :overwrite)))
  (write-string "XY" s) (close s))
(check "open overwrite" "XY"
  (with-open-file (s (concatenate 'string *test-tmp* "_clt_ovw.txt") :direction :input)
    (read-line s)))

; open with :if-exists nil returns nil
(let ((s (open (concatenate 'string *test-tmp* "_clt_sup.txt") :direction :output :if-exists nil)))
  (check "open if-exists nil" nil s))

; open with :if-exists :error on existing file
(check "open if-exists error" t
  (handler-case
    (progn (open (concatenate 'string *test-tmp* "_clt_sup.txt") :direction :output :if-exists :error) nil)
    (error (c) t)))

; --- READ-SEQUENCE / WRITE-SEQUENCE builtins on file streams ---
; The (unsigned-byte 8) <-> binary file combination takes the bulk chunked
; path (one platform read/write per 4KB, exercised past one chunk on the
; Amiga IOBuf); strings take the char path; reads past EOF return short.
(check "write/read-sequence u8 file bulk round-trip" '(300 1 65 2)
  (let ((path (concatenate 'string *test-tmp* "_clt_rseq.bin")))
    (let ((v (make-array 300 :element-type '(unsigned-byte 8)
                             :initial-element 65)))
      (setf (aref v 0) 1)
      (setf (aref v 299) 2)
      (with-open-file (s path :direction :output
                              :element-type '(unsigned-byte 8)
                              :if-exists :supersede)
        (write-sequence v s)))
    (let ((v (make-array 300 :element-type '(unsigned-byte 8))))
      (with-open-file (s path :element-type '(unsigned-byte 8))
        (list (read-sequence v s) (aref v 0) (aref v 1) (aref v 299))))))
; 9000 bytes spans three CL_SEQ_IO_CHUNK (4096-byte) iterations of the bulk
; chunked read/write loop; sentinels flank the first chunk boundary so a
; stale stream/offset reused across iterations would corrupt one of them.
(check "write/read-sequence u8 file bulk multi-chunk round-trip" '(9000 1 3 4 2)
  (let ((path (concatenate 'string *test-tmp* "_clt_rseq_mc.bin")))
    (let ((v (make-array 9000 :element-type '(unsigned-byte 8)
                             :initial-element 65)))
      (setf (aref v 0) 1)
      (setf (aref v 4095) 3)
      (setf (aref v 4096) 4)
      (setf (aref v 8999) 2)
      (with-open-file (s path :direction :output
                              :element-type '(unsigned-byte 8)
                              :if-exists :supersede)
        (write-sequence v s)))
    (let ((v (make-array 9000 :element-type '(unsigned-byte 8))))
      (with-open-file (s path :element-type '(unsigned-byte 8))
        (list (read-sequence v s) (aref v 0) (aref v 4095) (aref v 4096) (aref v 8999))))))
(check "read-sequence u8 file short read at EOF" '(300 2 9)
  (let ((v (make-array 400 :element-type '(unsigned-byte 8)
                           :initial-element 9)))
    (with-open-file (s (concatenate 'string *test-tmp* "_clt_rseq.bin")
                       :element-type '(unsigned-byte 8))
      (list (read-sequence v s) (aref v 299) (aref v 300)))))
(check "read-sequence u8 file start/end window" '(5 0 1 65 0)
  (let ((v (make-array 8 :element-type '(unsigned-byte 8)
                         :initial-element 0)))
    (with-open-file (s (concatenate 'string *test-tmp* "_clt_rseq.bin")
                       :element-type '(unsigned-byte 8))
      (list (read-sequence v s :start 2 :end 5)
            (aref v 1) (aref v 2) (aref v 4) (aref v 5)))))
(check "write/read-sequence string char path" '(5 "Test1")
  (let ((str (make-string 5)))
    (with-open-file (s (concatenate 'string *test-tmp* "_clt_s8.txt"))
      (list (read-sequence str s) str))))
(check "read-sequence consumes unread-char first" "Test"
  (with-open-file (s (concatenate 'string *test-tmp* "_clt_s8.txt"))
    (read-char s)
    (unread-char #\T s)
    (let ((str (make-string 4)))
      (read-sequence str s)
      str)))
(check "write/read-sequence list of bytes" '(3 (0 10 20))
  (let ((path (concatenate 'string *test-tmp* "_clt_rseq2.bin")))
    (with-open-file (s path :direction :output
                            :element-type '(unsigned-byte 8)
                            :if-exists :supersede)
      (write-sequence '(10 20 30 40) s))
    (let ((l (list 0 0 0)))
      (with-open-file (s path :element-type '(unsigned-byte 8))
        (list (read-sequence l s :start 1) l)))))

; with-open-file with :if-exists :rename-and-delete
(with-open-file (s (concatenate 'string *test-tmp* "_clt_wrad.txt") :direction :output)
  (write-string "First version" s))
(with-open-file (s (concatenate 'string *test-tmp* "_clt_wrad.txt") :direction :output :if-exists :rename-and-delete)
  (write-string "Second version" s))
(check "with-open-file rename-and-delete" "Second version"
  (with-open-file (s (concatenate 'string *test-tmp* "_clt_wrad.txt") :direction :input)
    (read-line s)))

; open nonexistent for input -> error
(check "open nonexistent error" t (handler-case (progn (open (concatenate 'string *test-tmp* "_nonexist_xyz.txt") :direction :input) nil) (error (c) t)))

; open nonexistent for input with :if-does-not-exist nil -> nil
(check "open nonexistent nil" nil (open (concatenate 'string *test-tmp* "_nonexist_xyz.txt") :direction :input :if-does-not-exist nil))

; with-open-file write then read
(with-open-file (s (concatenate 'string *test-tmp* "_clt_wof.txt") :direction :output)
  (format s "Hello ~A" "WOF"))
(check "with-open-file read" "Hello WOF" (with-open-file (s (concatenate 'string *test-tmp* "_clt_wof.txt") :direction :input) (read-line s)))

; with-open-file returns body value
(check "with-open-file returns value" 42 (with-open-file (s (concatenate 'string *test-tmp* "_clt_wof.txt") :direction :input) (read-line s) 42))

; with-open-file close on error
(check "with-open-file closes on error" t (let ((stream nil)) (handler-case (with-open-file (s (concatenate 'string *test-tmp* "_clt_wof.txt") :direction :input) (setq stream s) (error "boom")) (error (c) (not (open-stream-p stream))))))

; format to file stream
(with-open-file (s (concatenate 'string *test-tmp* "_clt_fmt.txt") :direction :output)
  (format s "~A + ~A = ~A" 1 2 3))
(check "format to file" "1 + 2 = 3" (with-open-file (s (concatenate 'string *test-tmp* "_clt_fmt.txt") :direction :input) (read-line s)))

; prin1/princ to file stream
(with-open-file (s (concatenate 'string *test-tmp* "_clt_pr.txt") :direction :output)
  (prin1 42 s)
  (princ " " s)
  (princ "hello" s))
(check "prin1/princ to file" "42 hello" (with-open-file (s (concatenate 'string *test-tmp* "_clt_pr.txt") :direction :input) (read-line s)))

; --- Feature conditionals (#+ / #-) ---

; *features* is a list
(check "features-is-list" t (consp *features*))

; :CL-AMIGA is in *features*
(check "features-has-cl-amiga" t (if (member :cl-amiga *features*) t nil))

; :COMMON-LISP is in *features*
(check "features-has-common-lisp" t (if (member :common-lisp *features*) t nil))

; #+cl-amiga includes form
(check "feature-plus-present" 42 #+cl-amiga 42)

; #-cl-amiga skips form
(check "feature-minus-present" 99 #-cl-amiga 42 99)

; #+nonexistent skips form
(check "feature-plus-absent" 99 #+nonexistent 42 99)

; #-nonexistent includes form
(check "feature-minus-absent" 42 #-nonexistent 42)

; Feature conditionals in a list
(check "feature-in-list" '(1 2 3) '(1 #+cl-amiga 2 3))
(check "feature-skip-in-list" '(1 3) '(1 #+nonexistent 2 3))

; Compound feature expressions
(check "feature-and" 42 #+(and cl-amiga common-lisp) 42)
(check "feature-or" 42 #+(or nonexistent cl-amiga) 42)
(check "feature-not" 42 #+(not nonexistent) 42)
(check "feature-not-fail" 99 #+(not cl-amiga) 42 99)

; Platform-specific features on Amiga
(check "features-has-amigaos" t (if (member :amigaos *features*) t nil))
(check "features-has-m68k" t (if (member :m68k *features*) t nil))
#+amigaos (check "feature-plus-amigaos" 42 42)

; sleep with zero duration
(check "sleep-zero" nil (sleep 0))

; with-standard-io-syntax executes body
(check "with-standard-io-syntax" 42 (with-standard-io-syntax 42))

; --- Step 10: Time Functions + Minimal Pathnames ---

; get-universal-time returns a large integer
(check "get-universal-time" t (> (get-universal-time) 3786912000))

; file-write-date on existing file returns a number
(check "file-write-date" t (numberp (file-write-date "lib/boot.lisp")))

; probe-file on existing file
; CLHS probe-file: returns the truename (absolute path), or NIL if not found
(check "probe-file-exists" t (not (null (probe-file "lib/boot.lisp"))))

; probe-file on nonexistent file
(check "probe-file-nil" nil (probe-file "nonexistent_xyz_123.txt"))

; file-namestring extracts filename
(check "file-namestring-posix" "baz.txt" (file-namestring "/foo/bar/baz.txt"))
(check "file-namestring-amiga" "test.lisp" (file-namestring "Work:dev/test.lisp"))

; directory-namestring extracts directory
(check "dir-namestring-posix" "/foo/bar/" (directory-namestring "/foo/bar/baz.txt"))
(check "dir-namestring-amiga" "Work:dev/" (directory-namestring "Work:dev/test.lisp"))

; pathname-name extracts name without extension
(check "pathname-name" "test" (pathname-name "/foo/test.lisp"))

; pathname-type extracts extension
(check "pathname-type" "lisp" (pathname-type "/foo/test.lisp"))
(check "pathname-type-none" nil (pathname-type "/foo/test"))

; namestring is identity
(check "namestring" "/foo/bar" (namestring "/foo/bar"))

; truename: per CLHS, signals a file-error for non-existent paths.
; (The earlier "identity-for-now" stub behavior is gone now that
; truename actually walks the filesystem.)
(check "truename file-error" t
  (handler-case (progn (truename "/foo/bar") nil)
    (file-error () t)))

; make-pathname constructs path (returns pathname, use namestring to get string)
(check "make-pathname" "hello.txt" (namestring (make-pathname :name "hello" :type "txt")))

; merge-pathnames fills in missing directory (returns pathname)
(check "merge-pathnames" "/some/dir/file.txt" (namestring (merge-pathnames "file.txt" "/some/dir/")))
(check "merge-pathnames-has-dir" "/other/file.txt" (namestring (merge-pathnames "/other/file.txt" "/some/dir/")))

; enough-namestring
(check "enough-namestring" "/foo/bar/baz.txt" (enough-namestring "/foo/bar/baz.txt" "/foo/"))

; --- Pathname type (proper #P pathnames) ---

; pathnamep predicate
(check "pathnamep-true" t (pathnamep #P"test.lisp"))
(check "pathnamep-false" nil (pathnamep "test.lisp"))

; #P reader creates pathname objects
(check "pathname-read-print" "#P\"foo.lisp\"" (format nil "~S" #P"foo.lisp"))

; pathname coercion from string
(check "pathname-from-string" t (pathnamep (pathname "foo.lisp")))

; pathname-name from #P
(check "pathname-name-p" "bar" (pathname-name #P"/foo/bar.lisp"))

; pathname-type from #P
(check "pathname-type-p" "lisp" (pathname-type #P"/foo/bar.lisp"))

; pathname-directory from #P
(check "pathname-dir-p" (list :absolute "foo") (pathname-directory #P"/foo/bar.lisp"))

; pathname-device from Amiga path
(check "pathname-device-amiga" "DH0" (pathname-device #P"DH0:Work/test.lisp"))

; namestring round-trip
(check "namestring-roundtrip" "/foo/bar.lisp" (namestring #P"/foo/bar.lisp"))

; --- Amiga device: pathname tests ---

; Device-only paths (PROGDIR:, S:, DH0:)
(check "device-only S: device" "S" (pathname-device #P"S:"))
(check "device-only S: dir" (list :absolute) (pathname-directory #P"S:"))
(check "device-only S: name" nil (pathname-name #P"S:"))
(check "device-only S: namestring" "S:" (namestring #P"S:"))
(check "device-only PROGDIR: namestring" "PROGDIR:" (namestring #P"PROGDIR:"))
(check "device-only PROGDIR: device" "PROGDIR" (pathname-device #P"PROGDIR:"))
(check "device-only PROGDIR: dir" (list :absolute) (pathname-directory #P"PROGDIR:"))

; Device-only paths are absolute (critical for ASDF)
(check "device S: absolute" :absolute (car (pathname-directory #P"S:")))
(check "device PROGDIR: absolute" :absolute (car (pathname-directory #P"PROGDIR:")))
(check "device DH0: absolute" :absolute (car (pathname-directory #P"DH0:")))

; Device-only roundtrips
(check "device S: roundtrip" "S:" (namestring (pathname (namestring #P"S:"))))
(check "device PROGDIR: roundtrip" "PROGDIR:" (namestring (pathname (namestring #P"PROGDIR:"))))

; Device path with subdirectory
(check "device subdir parse" "S:asdfcache/" (namestring #P"S:asdfcache/"))
(check "device subdir dir" (list :absolute "asdfcache") (pathname-directory #P"S:asdfcache/"))

; Merge-pathnames with device paths
(check "merge device+relative" "PROGDIR:lib/boot.lisp"
  (namestring (merge-pathnames "lib/boot.lisp" #P"PROGDIR:")))
(check "merge device+subdir" "S:asdfcache/"
  (namestring (merge-pathnames "asdfcache/" #P"S:")))
(check "merge device dir" (list :absolute "lib")
  (pathname-directory (merge-pathnames "lib/boot.lisp" #P"PROGDIR:")))
(check "merge device preserves device" "PROGDIR"
  (pathname-device (merge-pathnames "lib/boot.lisp" #P"PROGDIR:")))
(check "merge device preserves name" "boot"
  (pathname-name (merge-pathnames "boot.lisp" #P"PROGDIR:lib/")))
(check "merge device preserves type" "lisp"
  (pathname-type (merge-pathnames "boot.lisp" #P"PROGDIR:lib/")))
(check "merge device absolute overrides" "DH0:Work/test.lisp"
  (namestring (merge-pathnames #P"DH0:Work/test.lisp" #P"S:")))
(check "merge device nested subdir" "S:asdfcache/common-lisp/cache/"
  (namestring (merge-pathnames "common-lisp/cache/" #P"S:asdfcache/")))

; Make-pathname with device
(check "make-pathname device" "DH0:Work/test.lisp"
  (namestring (make-pathname :device "DH0" :directory '(:absolute "Work") :name "test" :type "lisp")))
(check "make-pathname device-only" "S:"
  (namestring (make-pathname :device "S" :directory '(:absolute))))

; equal on pathnames
(check "pathname-equal" t (equal #P"/foo/bar.lisp" #P"/foo/bar.lisp"))
(check "pathname-not-equal" nil (equal #P"/foo/bar.lisp" #P"/baz/bar.lisp"))

; typep / type-of
(check "pathname-typep" t (typep #P"test" 'pathname))
(check "pathname-type-of" 'pathname (type-of #P"test"))

; *default-pathname-defaults* is a pathname
(check "default-pn-defaults" t (pathnamep *default-pathname-defaults*))

; *default-pathname-defaults* is seeded with the cwd as an ABSOLUTE directory
; pathname, so relative merges resolve to absolute (ASDF :tree needs this).
(check "default-pn-defaults-absolute" :absolute
       (first (pathname-directory *default-pathname-defaults*)))
(check "default-pn-defaults-merge-absolute" :absolute
       (first (pathname-directory
               (merge-pathnames "systems/" *default-pathname-defaults*))))

; --- LOGICAL-PATHNAME class ---
(check "logical-pathname-class-exists" 'logical-pathname
  (class-name (find-class 'logical-pathname)))
(check "logical-pathname-superclass" 'pathname
  (class-name (car (class-direct-superclasses (find-class 'logical-pathname)))))
(check "logical-pathname-defmethod-loads" :ok
  (progn (defgeneric lp-test-amiga (x))
         (defmethod lp-test-amiga ((x logical-pathname)) :lp)
         :ok))
(check "translate-logical-pathname-fboundp" t (fboundp 'translate-logical-pathname))
(check "translate-logical-pathname-returns-pathname" t (pathnamep (translate-logical-pathname #P"foo.lisp")))
(check "translate-logical-pathname-roundtrip" "foo.lisp" (namestring (translate-logical-pathname #P"foo.lisp")))
(check "logical-pathname-translations-nil" nil (logical-pathname-translations "SYS"))

; --- FILE-STREAM class ---
(check "file-stream-class-exists" 'file-stream
  (class-name (find-class 'file-stream)))
(check "file-stream-superclass" 'stream
  (class-name (car (class-direct-superclasses (find-class 'file-stream)))))
(check "file-stream-defmethod-loads" :ok
  (progn (defgeneric fs-test-amiga (x))
         (defmethod fs-test-amiga ((x file-stream)) :fs)
         :ok))

; --- Regression: dispatch self-heals a stale NEGATIVE cache entry ---
; A corrupt negative dispatch-cache entry (present, value NIL = "no method")
; must not shadow a method that plainly exists.  This is the mechanism behind
; the ASDF field report "No applicable method for FIND-OPERATION with args of
; types (COMPILE-OP SYMBOL)": a method existed but a corrupt negative cache
; entry (GC relocation of the EQ-keyed cache / a concurrent cache write) made
; dispatch wrongly report a miss.  On a negative hit the dispatcher now
; re-verifies against the live method list and heals the entry.
(defclass neg-op-amiga () ())
(defgeneric neg-find-amiga (ctx spec))
(defmethod neg-find-amiga ((c t) (s neg-op-amiga)) :op)
(defmethod neg-find-amiga ((c t) (s symbol)) :sym)
(defmethod neg-find-amiga ((c t) (s string)) :str)
(defparameter *neg-op-amiga* (make-instance 'neg-op-amiga))
(check "dispatch-negative-cache-primes" :sym (neg-find-amiga *neg-op-amiga* 'a-thing))
(check "dispatch-negative-cache-self-heals" :sym
  (progn
    ;; Poke a stale NEGATIVE entry for (neg-op-amiga -> SYMBOL) and drop the
    ;; inline cache so the dispatch table is actually consulted.
    (let* ((gf (fdefinition 'neg-find-amiga))
           (cache (gf-dispatch-cache gf))
           (inner (gethash (class-of *neg-op-amiga*) cache)))
      (setf (gethash (class-of 'x) inner) nil)
      (%set-gf-inline-cache gf nil))
    ;; Before the fix this signaled "No applicable method"; now it heals.
    (neg-find-amiga *neg-op-amiga* 'other-thing)))
; A genuine no-applicable-method must STILL error (heal recomputes empty).
(check "dispatch-genuine-miss-still-errors" :caught
  (handler-case (progn (defgeneric gm-str-amiga (x))
                       (defmethod gm-str-amiga ((x string)) :str)
                       (gm-str-amiga 42))
    (error () :caught)))

; --- Regression: standard combination self-heals a missing-primary set ---
; "No applicable primary method" field report: dispatch got an applicable set
; with only :around (primary dropped by transient corruption).
; %dispatch-standard-emf re-verifies against the live method list.
(defclass npf-op-amiga () ())
(defclass npf-comp-amiga () ())
(defgeneric npf-amiga (o c))
(defmethod npf-amiga :around ((o t) (c t)) (list :around (call-next-method)))
(defmethod npf-amiga ((o npf-op-amiga) (c npf-comp-amiga)) :prim)
(defparameter *npf-o-amiga* (make-instance 'npf-op-amiga))
(defparameter *npf-c-amiga* (make-instance 'npf-comp-amiga))
(check "dispatch-no-primary-normal" '(:around :prim)
  (npf-amiga *npf-o-amiga* *npf-c-amiga*))
(check "dispatch-no-primary-self-heals" '(:around :prim)
  (let* ((gf (fdefinition 'npf-amiga))
         (around-only (remove-if (lambda (m) (null (method-qualifiers m)))
                                 (%compute-applicable-methods
                                  gf (list *npf-o-amiga* *npf-c-amiga*)))))
    (let ((*current-method-args* (list *npf-o-amiga* *npf-c-amiga*)))
      (funcall (%dispatch-standard-emf gf around-only)
               *npf-o-amiga* *npf-c-amiga*))))
; Genuine no-primary (only :around) must still error.
(check "dispatch-genuine-no-primary-errors" :caught
  (handler-case (progn (defgeneric only-around-amiga (x))
                       (defmethod only-around-amiga :around ((x t)) (call-next-method))
                       (only-around-amiga 5))
    (error () :caught)))

; --- Regression: the FIRST (uncached) dispatch miss self-heals too ---
; The negative-cache heal only fires on a SECOND call; the first call computes
; the applicable set directly and, before the fix, cached a negative AND
; signaled "No applicable method" if that single computation came up empty (the
; field report "No applicable method for FIND-OPERATION with args (PREPARE-OP
; SYMBOL)" hitting on the very first find-operation dispatch of a load).
; %dispatch-heal-empty recomputes from the live method list first.
(defclass fme-op-amiga () ())
(defgeneric fme-amiga (ctx spec))
(defmethod fme-amiga ((c t) (s symbol)) :sym)
(defparameter *fme-op-amiga* (make-instance 'fme-op-amiga))
(check "dispatch-fresh-miss-self-heals" '(:sym t)
  (let* ((gf (fdefinition 'fme-amiga))
         (h0 *gf-cache-heals*)
         (emf (%dispatch-heal-empty gf (list *fme-op-amiga* 'a-sym))))
    (list (and (functionp emf) (funcall emf *fme-op-amiga* 'a-sym))
          (> *gf-cache-heals* h0))))
; A genuine fresh miss (no method for these types) must return NIL / still error.
(check "dispatch-fresh-miss-genuine-nil" nil
  (progn (defgeneric fmg-amiga (x))
         (defmethod fmg-amiga ((x string)) :str)
         (%dispatch-heal-empty (fdefinition 'fmg-amiga) (list 42))))
(check "dispatch-fresh-miss-genuine-errors" :caught
  (handler-case (fmg-amiga 42) (error () :caught)))

; --- Regression: no-primary heal is gated on the GF roster + shared retry ---
; The no-primary self-heal retries-with-yield (%recompute-methods-until) only
; when the GF's roster actually contains a primary (%gf-roster-has-primary-p);
; an :around-only GF errors immediately with no wasted retries.
(defgeneric rg-prim-amiga (x))
(defmethod rg-prim-amiga ((x integer)) :int)
(defgeneric rg-around-amiga (x))
(defmethod rg-around-amiga :around ((x t)) (call-next-method))
(check "dispatch-roster-has-primary-t" t
  (%gf-roster-has-primary-p (fdefinition 'rg-prim-amiga)))
(check "dispatch-roster-has-primary-nil" nil
  (%gf-roster-has-primary-p (fdefinition 'rg-around-amiga)))
(check "dispatch-recompute-until-finds" t
  (let ((s (%recompute-methods-until (fdefinition 'rg-prim-amiga) (list 5)
                                     #'%methods-have-primary-p)))
    (and (consp s) (%methods-have-primary-p s) t)))
(check "dispatch-recompute-until-nil" nil
  (%recompute-methods-until (fdefinition 'rg-prim-amiga) (list "str")
                            #'%methods-have-primary-p))

; --- Regression: %gf-dispatch's uncached/variadic (T) fallback branch must
; bind *current-method-args* to ARGS before calling %dispatch-heal-empty ---
; Every other dispatch resolver (%gf-dispatch-eql, %gf-dispatch-cached,
; %gf-dispatch-1-slow, %gf-2-resolve) binds *current-method-args* before
; invoking %dispatch-heal-empty; the T branch used to invoke it first and only
; bind afterward, so %dispatch-heal-empty's no-primary fallback (which reads
; *current-method-args*) could see a stale/default binding instead of the real
; call args.  Spy on %dispatch-heal-empty to capture *current-method-args* at
; the moment %gf-dispatch (reached via %gf-dispatch-1-slow's T clause, forced
; by a non-integer/non-:eql cacheable-p mode) invokes it.
(defgeneric gdca-fn-amiga (x))
(defmethod gdca-fn-amiga ((x symbol)) :sym)
(%set-gf-cacheable-p (fdefinition 'gdca-fn-amiga) :forced-slow)
(defparameter *gdca-captured-amiga* :unset)
(check "gf-dispatch-slow-path-binds-current-method-args" (list 42)
  (let ((orig (symbol-function '%dispatch-heal-empty)))
    (unwind-protect
        (progn
          (setf (symbol-function '%dispatch-heal-empty)
                (lambda (gf args)
                  (setq *gdca-captured-amiga* *current-method-args*)
                  (funcall orig gf args)))
          (ignore-errors (gdca-fn-amiga 42)))
      (setf (symbol-function '%dispatch-heal-empty) orig))
    *gdca-captured-amiga*))

; decode-universal-time returns 9 values
(check "decode-ut-count" 9 (length (multiple-value-list (decode-universal-time (get-universal-time)))))

; encode/decode round-trip
(check "encode-decode-roundtrip" (list 30 45 12 15 6 2025) (multiple-value-bind (s mi h d mo y) (decode-universal-time (encode-universal-time 30 45 12 15 6 2025)) (list s mi h d mo y)))

; encode-universal-time absolute values (regression: leap-day overcount made
; dates in 1999-2000 one day (86400s) too large; UTC universal = unix + 2208988800)
(check "encode-ut-1999-01-01" 3124137600 (encode-universal-time 0 0 0 1 1 1999 0))
(check "encode-ut-2000-01-01" 3155673600 (encode-universal-time 0 0 0 1 1 2000 0))
(check "encode-ut-2000-02-29" 3160771200 (encode-universal-time 0 0 0 29 2 2000 0))
(check "encode-ut-2000-03-01" 3160857600 (encode-universal-time 0 0 0 1 3 2000 0))
(check "encode-ut-2001-01-01" 3187296000 (encode-universal-time 0 0 0 1 1 2001 0))
(check "encode-ut-2026-06-27" 3991507200 (encode-universal-time 0 0 0 27 6 2026 0))
; encode is the inverse of decode in the formerly-broken range
; (encode args are second=33 minute=22 hour=11; decode returns s mi h ... in that order)
(check "encode-decode-roundtrip-2000" (list 33 22 11 29 2 2000)
  (multiple-value-bind (s mi h d mo y)
      (decode-universal-time (encode-universal-time 33 22 11 29 2 2000 0) 0)
    (list s mi h d mo y)))

; get-decoded-time returns 9 values
(check "get-decoded-time-count" 9 (length (multiple-value-list (get-decoded-time))))

; values propagation through defun (regression test for OP_LOAD MV fix)
(defun %test-mv-values () (values 10 20 30))
(check "defun-mv-propagation" (list 10 20 30) (multiple-value-list (%test-mv-values)))

; delete-file and rename-file via write/probe/delete cycle
(with-open-file (s "T:cl_test_step10.tmp" :direction :output) (write-string "test" s))
(check "probe-written" #P"T:cl_test_step10.tmp" (probe-file "T:cl_test_step10.tmp"))
(rename-file "T:cl_test_step10.tmp" "T:cl_test_step10_ren.tmp")
(check "rename-old-gone" nil (probe-file "T:cl_test_step10.tmp"))
(check "rename-new-exists" #P"T:cl_test_step10_ren.tmp" (probe-file "T:cl_test_step10_ren.tmp"))
(delete-file "T:cl_test_step10_ren.tmp")
(check "delete-file" nil (probe-file "T:cl_test_step10_ren.tmp"))

; %mkdir creates directory
(check "mkdir" t (%mkdir "T:cl_test_step10_dir"))
; PROBE-FILE of an on-disk directory returns directory form (trailing slash),
; even when queried without one — matches conformant TRUENAME/PROBE-FILE so
; (directory-pathname-p (probe-file ...)) / uiop:directory-exists-p work.
(check "mkdir-probe" #P"T:cl_test_step10_dir/" (probe-file "T:cl_test_step10_dir"))
(check "mkdir-probe-dir-form" (list nil nil)
       (let ((p (probe-file "T:cl_test_step10_dir")))
         (list (pathname-name p) (pathname-type p))))

; --- Step 12: Readtable + Compile ---

; readtablep
(check "readtablep-current" t (readtablep *readtable*))
(check "readtablep-non-rt" nil (readtablep 42))

; get-macro-character for ( => NIL (built-in), not non-terminating
(check "get-macro-char-paren" nil (get-macro-character #\())

; get-macro-character for # => NIL fn, T non-term-p (second value)
(check "get-macro-char-hash" (list nil t) (multiple-value-list (get-macro-character #\#)))

; get-macro-character for #\" returns a real FUNCTION (the built-in string
; reader), not NIL — pythonic-string-reader (mgl-pax/clog) grabs and funcalls
; it, and would otherwise hit "Not a function: NIL".  Funcalling it reads a
; "..." body with the opening quote treated as already consumed.
(check "get-macro-char-string-fn" t (functionp (get-macro-character #\")))
(check "get-macro-char-string-read" "hi"
       (let ((s (make-string-input-stream "hi\" rest")))
         (funcall (get-macro-character #\") s #\")))

; copy-readtable
(check "copy-readtable" t (readtablep (copy-readtable)))

; set-dispatch-macro-character with an infix numeric arg (#n@), installed on a
; COPIED readtable made the current *READTABLE* — the ironclad #32@ s-box reader
; shape (which blocked loading ironclad/chipi-api).  Reading #32@(...) must
; consult the user dispatch table, and must keep working across a moving
; compaction: the *READTABLE* symbol is reached only via the C SYM_STAR_READTABLE
; global handle and the dispatch closure lives in the readtable pool — both must
; be GC-forwarded or the macro is silently dropped ("Invalid radix prefix #32@").
(let ((*readtable* (copy-readtable *readtable*)))
  (set-dispatch-macro-character #\# #\@
    (lambda (s c arg) (declare (ignore c)) (list arg (read s nil nil))))
  (check "dispatch-macro-infix-arg" '(32 (1 2 3)) (read-from-string "#32@(1 2 3)"))
  (check "dispatch-macro-no-arg"    '(nil (4 5))  (read-from-string "#@(4 5)"))
  (dotimes (i 30000) (cons i i))   ; churn to provoke compaction
  (check "dispatch-macro-after-gc" '(32 (1 2 3)) (read-from-string "#32@(1 2 3)")))

; compile nil lambda
(check "compile-nil-lambda" 42 (funcall (compile nil '(lambda (x) (+ x 1))) 41))

; compile named function
(check "compile-named" t (functionp (compile 'car)))

; set-macro-character: define ! then test it
(set-macro-character #\! (lambda (stream char) (list 'quote (read stream t nil t))))
(check "set-macro-char" t (equal !hello 'hello))

; --- Phase 8 Step 2: Missing string operations ---

; string-capitalize
(check "string-capitalize" "Hello World" (string-capitalize "hello world"))
(check "string-capitalize upper" "Hello World" (string-capitalize "HELLO WORLD"))
(check "string-capitalize hyphen" "Foo-Bar" (string-capitalize "foo-bar"))

; nstring-upcase
(check "nstring-upcase" "HELLO" (let ((s (copy-seq "hello"))) (nstring-upcase s) s))

; nstring-downcase
(check "nstring-downcase" "hello" (let ((s (copy-seq "HELLO"))) (nstring-downcase s) s))

; nstring-capitalize
(check "nstring-capitalize" "Hello World" (let ((s (copy-seq "hello world"))) (nstring-capitalize s) s))

; :start/:end bounding indices on the case-conversion family (CLHS 21.2)
(check "string-upcase :start :end" "Dr. LiVINGstone, I presume?"
       (string-upcase "Dr. Livingstone, I presume?" :start 6 :end 10))
(check "string-upcase :start" "hello WORLD" (string-upcase "hello world" :start 6))
(check "string-downcase :end" "hello WORLD" (string-downcase "HELLO WORLD" :end 5))
(check "string-upcase :end nil" "aBC" (string-upcase "abc" :start 1 :end nil))
(check "string-upcase empty region" "abc" (string-upcase "abc" :start 1 :end 1))
(check "string-capitalize :start :end" "hello World"
       (string-capitalize "hello world" :start 6 :end 11))
(check "string-upcase designator :start" "FOO-BAR" (string-upcase 'foo-bar :start 4))
(check "nstring-downcase :start :end" '(t "Dr. Livingstone, I presume?")
       (let ((s (copy-seq "Dr. LiVINGstone, I presume?")))
         (list (eq s (nstring-downcase s :start 6 :end 10)) s)))
(check "nstring-upcase :start" "hello WORLD"
       (let ((s (copy-seq "hello world"))) (nstring-upcase s :start 6) s))
(check "nstring-capitalize :end" "Hello world"
       (let ((s (copy-seq "hello world"))) (nstring-capitalize s :end 5) s))
(check "string-upcase bad bounds" :bad-bounds
       (handler-case (string-upcase "abc" :start 2 :end 1) (error () :bad-bounds)))
(check "string-upcase :end past length" :bad-bounds
       (handler-case (string-upcase "abc" :end 4) (error () :bad-bounds)))
(check "string-upcase unknown keyword" :unknown-keyword
       (handler-case (string-upcase "abc" :frob 1) (error () :unknown-keyword)))
(check "string-upcase allow-other-keys" "ABC"
       (string-upcase "abc" :frob 1 :allow-other-keys t))
(check "string-upcase :start type-error" :start-type
       (handler-case (string-upcase "abc" :start 'x) (type-error () :start-type)))

; char-name
(check "char-name space" "Space" (char-name #\Space))
(check "char-name newline" "Newline" (char-name #\Newline))
(check "char-name graphic" nil (char-name #\A))
; U+FFFD REPLACEMENT CHARACTER — the standard Unicode name flexi-streams uses
; (via clack's hunchentoot handler, a clog dependency).  The reader, char-name
; and name-char must all know it.
(check "char-name replacement" "Replacement_Character" (char-name #\Replacement_Character))
(check "char-code replacement" 65533 (char-code #\Replacement_Character))

; name-char
(check "name-char space" #\Space (name-char "Space"))
(check "name-char upper" #\Space (name-char "SPACE"))
(check "name-char newline" #\Newline (name-char "Newline"))
(check "name-char invalid" nil (name-char "xyzzy"))
(check "name-char replacement" 65533 (char-code (name-char "Replacement_Character")))

; char-equal (case-insensitive)
(check "char-equal ci" t (char-equal #\a #\A))
(check "char-not-equal ci" t (char-not-equal #\a #\b))
(check "char-lessp ci" t (char-lessp #\a #\B))
(check "char-greaterp ci" t (char-greaterp #\b #\A))
(check "char-not-greaterp ci" t (char-not-greaterp #\a #\A))
(check "char-not-lessp ci" t (char-not-lessp #\A #\a))

; graphic-char-p
(check "graphic-char-p A" t (graphic-char-p #\A))
(check "graphic-char-p space" t (graphic-char-p #\Space))
(check "graphic-char-p newline" nil (graphic-char-p #\Newline))

; alphanumericp
(check "alphanumericp A" t (alphanumericp #\A))
(check "alphanumericp 5" t (alphanumericp #\5))
(check "alphanumericp !" nil (alphanumericp #\!))

; digit-char
(check "digit-char 0" #\0 (digit-char 0))
(check "digit-char 9" #\9 (digit-char 9))
(check "digit-char hex" #\A (digit-char 10 16))
(check "digit-char over" nil (digit-char 10))

; both-case-p
(check "both-case-p A" t (both-case-p #\A))
(check "both-case-p a" t (both-case-p #\a))
(check "both-case-p 1" nil (both-case-p #\1))

; standard-char-p
(check "standard-char-p A" t (standard-char-p #\A))
(check "standard-char-p nl" t (standard-char-p #\Newline))

; --- Phase 8 Step 3: Missing sequence operations ---

; elt
(check "elt list" 'b (elt '(a b c) 1))
(check "elt vector" 20 (elt (vector 10 20 30) 1))
(check "elt string" #\b (elt "abc" 1))

; (setf elt)
(check "setf elt vector" 99 (let ((v (vector 1 2 3))) (setf (elt v 1) 99) (elt v 1)))
(check "setf elt list" '(99 2 3) (let ((l (list 1 2 3))) (setf (elt l 0) 99) l))

; copy-seq
(check "copy-seq list" '(1 2 3) (copy-seq '(1 2 3)))
(check "copy-seq nil" nil (copy-seq nil))
(check "copy-seq string" "hello" (copy-seq "hello"))
(check "copy-seq indep" 1 (let ((a (list 1 2))) (let ((b (copy-seq a))) (setf (car b) 99) (car a))))

; map-into
(check "map-into vector" 33 (let ((v (vector 0 0 0))) (map-into v #'+ (vector 1 2 3) (vector 10 20 30)) (elt v 2)))

; --- Phase 8 Step 4: Higher-order functions ---

; complement
(check "complement t" nil (funcall (complement #'zerop) 0))
(check "complement nil" t (funcall (complement #'zerop) 5))

; constantly
(check "constantly" 42 (funcall (constantly 42) 1 2 3))
(check "constantly mapcar" '(x x x) (mapcar (constantly 'x) '(1 2 3)))

; --- Phase 8 Step 1: Missing list operations ---

; list*
(check "list* 2args" '(1 . 2) (list* 1 2))
(check "list* 3args" '(1 2 . 3) (list* 1 2 3))
(check "list* with nil" '(1 2 3) (list* 1 2 3 nil))
(check "list* single" 'a (list* 'a))

; make-list
(check "make-list basic" '(nil nil nil) (make-list 3))
(check "make-list init" '(x x x) (make-list 3 :initial-element 'x))
(check "make-list zero" nil (make-list 0))

; tree-equal
(check "tree-equal match" t (tree-equal '(1 (2 3)) '(1 (2 3))))
(check "tree-equal mismatch" nil (tree-equal '(1 2) '(1 3)))
(check "tree-equal nil" t (tree-equal nil nil))
(check "tree-equal test" t (tree-equal '("a" "b") '("a" "b") :test #'equal))

; list-length
(check "list-length" 3 (list-length '(1 2 3)))
(check "list-length nil" 0 (list-length nil))

; tailp
(check "tailp cdr" t (let ((x '(1 2 3))) (tailp (cdr x) x)))
(check "tailp nil" t (tailp nil '(1 2 3)))
(check "tailp no" nil (tailp '(4) '(1 2 3)))

; ldiff
(check "ldiff basic" '(1 2) (let* ((x '(1 2 3 4)) (tail (cddr x))) (ldiff x tail)))
(check "ldiff nil" '(1 2 3) (ldiff '(1 2 3) nil))

; revappend
(check "revappend" '(3 2 1 4 5) (revappend '(1 2 3) '(4 5)))
(check "revappend nil" '(1 2) (revappend nil '(1 2)))

; nreconc
(check "nreconc" '(3 2 1 4 5) (nreconc (list 1 2 3) '(4 5)))
(check "nreconc nil" '(1 2) (nreconc nil '(1 2)))

; assoc-if
(check "assoc-if found" '(b . 2) (assoc-if #'symbolp '((1 . a) (b . 2) (3 . c))))
(check "assoc-if not found" nil (assoc-if #'null '((1 . a) (2 . b))))

; assoc-if-not
(check "assoc-if-not" '(1 . a) (assoc-if-not #'symbolp '((1 . a) (b . 2))))

; rassoc-if
(check "rassoc-if found" '(3 . b) (rassoc-if #'symbolp '((1 . 2) (3 . b) (4 . 5))))
(check "rassoc-if not found" nil (rassoc-if #'null '((1 . a) (2 . b))))

; rassoc-if-not
(check "rassoc-if-not" '(1 . 2) (rassoc-if-not #'symbolp '((1 . 2) (3 . b))))

; remf
(check "remf middle" '(:a 1 :c 3) (let ((p (list :a 1 :b 2 :c 3))) (remf p :b) p))
; Per CLHS, REMF returns a single boolean (found/not-found) and
; *destructively modifies* the place — so the place must be SETF'able.
; A fresh (list ...) literal isn't (there's no SETF expander for LIST,
; and "modifying a list literal" has no useful meaning), so bind to a
; variable first and inspect both the boolean and the post-call list.
(check "remf head" '((:b 2) t)
  (let ((p (list :a 1 :b 2)))
    (let ((found (remf p :a))) (list p found))))
(check "remf missing" '((:a 1 :b 2) nil)
  (let ((p (list :a 1 :b 2)))
    (let ((found (remf p :z))) (list p found))))

; --- Loop macro ---
; Simple loop
(check "loop simple return" 5 (let ((i 0)) (loop (when (= i 5) (return i)) (setq i (+ i 1)))))
(check "loop simple accum" '(0 1 2) (let ((result nil) (i 0)) (loop (when (>= i 3) (return (nreverse result))) (push i result) (setq i (+ i 1)))))

; Extended loop - while/until/do
(check "loop while" 10 (let ((i 0) (sum 0)) (loop while (< i 5) do (setq sum (+ sum i)) (setq i (+ i 1))) sum))
(check "loop until" 6 (let ((i 0) (sum 0)) (loop until (= i 4) do (setq sum (+ sum i)) (setq i (+ i 1))) sum))
(check "loop do multiple" 30 (let ((x 0) (y 0)) (loop while (< x 3) do (setq x (+ x 1)) (setq y (+ y 10))) y))

; Loop for/as/repeat
(check "loop for in" '(1 2 3) (let ((r nil)) (loop for x in '(1 2 3) do (push x r)) (nreverse r)))
(check "loop for in by" '(1 3 5) (let ((r nil)) (loop for x in '(1 2 3 4 5) by #'cddr do (push x r)) (nreverse r)))
(check "loop for on" '((1 2 3) (2 3) (3)) (let ((r nil)) (loop for x on '(1 2 3) do (push x r)) (nreverse r)))
(check "loop for from to" 15 (let ((sum 0)) (loop for i from 1 to 5 do (setq sum (+ sum i))) sum))
(check "loop for from below" 6 (let ((sum 0)) (loop for i from 0 below 4 do (setq sum (+ sum i))) sum))
(check "loop for upto" '(0 1 2 3 4 5) (loop for i upto 5 collect i))
(check "loop for to" '(0 1 2 3) (loop for i to 3 collect i))
(check "loop for upto by" '(0 2 4 6 8 10) (loop for i upto 10 by 2 collect i))
(check "loop for downto" '(0 -1 -2 -3) (loop for i downto -3 collect i))
(check "loop for downto by" '(0 -2 -4 -6) (loop for i downto -6 by 2 collect i))
(check "loop for from by" '(0 3 6 9) (let ((r nil)) (loop for i from 0 to 10 by 3 do (push i r)) (nreverse r)))
(check "loop for downfrom" '(5 4 3 2 1) (let ((r nil)) (loop for i downfrom 5 to 1 do (push i r)) (nreverse r)))
(check "loop for downfrom above" '(5 4 3) (let ((r nil)) (loop for i downfrom 5 above 2 do (push i r)) (nreverse r)))
(check "loop for across" '(1 2 3) (let ((r nil)) (loop for x across (vector 1 2 3) do (push x r)) (nreverse r)))
(check "loop for = then" '(1 2 4 8 16) (let ((r nil)) (loop for x = 1 then (* x 2) while (< x 20) do (push x r)) (nreverse r)))
(check "loop repeat" 5 (let ((c 0)) (loop repeat 5 do (setq c (+ c 1))) c))
(check "loop for multiple" '((1 . a) (2 . b) (3 . c)) (let ((r nil)) (loop for x in '(a b c) for i from 1 do (push (cons i x) r)) (nreverse r)))

; Loop accumulation
(check "loop collect" '(1 2 3) (loop for x in '(1 2 3) collect x))
(check "loop collect expr" '(1 4 9) (loop for x in '(1 2 3) collect (* x x)))
(check "loop for type-spec" '(0 1 2 3) (loop for i fixnum from 0 to 3 collect i))
(check "loop collect into" '(1 2 3) (loop for x in '(1 2 3 4 5) collect x into r do (when (= x 3) (return (nreverse r)))))
(check "loop sum" 15 (loop for x in '(1 2 3 4 5) sum x))
(check "loop sum into" 6 (loop for x in '(1 2 3 4 5) sum x into tot do (when (= x 3) (return tot))))
(check "loop count" 3 (loop for x in '(1 nil 2 nil 3) count x))
(check "loop maximize" 9 (loop for x in '(3 1 4 1 5 9 2 6) maximize x))
(check "loop minimize" 1 (loop for x in '(3 1 4 1 5 9 2 6) minimize x))
(check "loop append" '(a b c d e) (loop for x in '((a b) (c d) (e)) append x))
(check "loop nconc" '(1 2 3 4) (loop for x in '((1 2) (3 4)) nconc (copy-list x)))
;; Regression: mixing COLLECT with NCONC/APPEND in one loop must keep source
;; order (CLHS 6.1.3).  COLLECT builds reversed (push + final nreverse); NCONC/
;; APPEND used to build forward, so the finalizer reversed the result when the
;; two were mixed -- which reversed cl-ppcre register-groups-bind (the
;; normalize-var-list `if (consp e) nconc ... else collect ...' shape).
(check "loop if nconc else collect" '(a b c)
  (loop for x in '(a b c) if (consp x) nconc (list (car x)) else collect x))
(check "loop if nconc else collect both" '(a b1 b2 c)
  (loop for x in '(a (b1 b2) c) if (consp x) nconc (copy-list x) else collect x))
(check "loop collect+nconc interleaved" '(1 10 2 20 3 30)
  (loop for x in '(1 2 3) collect x nconc (list (* x 10))))
(check "loop if append else collect" '(1 2 3 4 5)
  (loop for x in '((1 2) 3 (4 5)) if (listp x) append x else collect x))
(check "loop return" 3 (loop for x in '(1 2 3 4 5) do (when (= x 3) (return x))))
(check "loop return in do+let" 42 (loop while t do (let ((x 42)) (return x))))
(check "loop return in do+let*" 3 (loop while t do (let* ((a 1) (b 2)) (return (+ a b)))))
(check "loop return in do+let defun" 7 (progn (defun %test-loop-ret2 () (loop while t do (let ((x 7)) (return x)))) (%test-loop-ret2)))

; Loop Step 4 — conditionals, always/never/thereis, with, named, initially/finally
(check "loop when collect" '(2 4 6) (loop for x in '(1 2 3 4 5 6) when (evenp x) collect x))
(check "loop if/else" '(1 -2 3 -4 5) (loop for x in '(1 2 3 4 5) if (oddp x) collect x else collect (- x)))
(check "loop unless collect" '(1 3 5) (loop for x in '(1 2 3 4 5) unless (evenp x) collect x))
(check "loop when and" '((2 4) (2 4)) (let ((side nil)) (let ((r (loop for x in '(1 2 3 4) when (evenp x) collect x and do (push x side)))) (list r (nreverse side)))))
(check "loop always true" t (loop for x in '(2 4 6) always (evenp x)))
(check "loop always false" nil (loop for x in '(2 3 6) always (evenp x)))
(check "loop never true" t (loop for x in '(1 3 5) never (evenp x)))
(check "loop never false" nil (loop for x in '(1 2 5) never (evenp x)))
(check "loop thereis found" 4 (loop for x in '(1 2 3 4 5) thereis (and (> x 3) x)))
(check "loop thereis nil" nil (loop for x in '(1 2 3) thereis (and (> x 10) x)))
(check "loop with" 6 (loop with sum = 0 for x in '(1 2 3) do (setq sum (+ sum x)) finally (return sum)))
(check "loop with and" '(30) (loop with x = 10 and y = 20 repeat 1 collect (+ x y)))
(check "loop named" 6 (loop named my-loop for x from 1 do (when (> x 5) (return-from my-loop x))))
(check "loop initially" '(start 1 2) (let ((log nil)) (loop initially (push 'start log) for x in '(1 2) do (push x log)) (nreverse log)))
(check "loop finally return" 15 (loop for x in '(1 2 3 4 5) sum x into total finally (return total)))
(check "loop collect into finally" '(a b c) (loop for x in '(a b c) collect x into items finally (return items)))

;; loop-finish
(check "loop-finish" 15 (loop for i from 1 to 10 sum i do (when (= i 5) (loop-finish))))
(check "loop-finish+finally" t (let ((ran nil)) (loop for i from 1 to 10 do (when (= i 3) (loop-finish)) finally (setq ran t)) ran))

;; loop BEING clause
(check "loop being hash-keys" '(:a :b)
  (let ((ht (make-hash-table)))
    (setf (gethash :a ht) 1)
    (setf (gethash :b ht) 2)
    (sort (loop for k being the hash-keys of ht collect k)
          #'string< :key #'symbol-name)))
(check "loop being hash-values" '(10 20)
  (let ((ht (make-hash-table)))
    (setf (gethash :a ht) 10)
    (setf (gethash :b ht) 20)
    (sort (loop for v being the hash-values in ht collect v) #'<)))
(check "loop being hash-keys using" '((:x 42))
  (let ((ht (make-hash-table)))
    (setf (gethash :x ht) 42)
    (loop for k being each hash-key of ht using (hash-value v)
          collect (list k v))))
(check "loop being hash-values using" '((:x 42))
  (let ((ht (make-hash-table)))
    (setf (gethash :x ht) 42)
    (loop for v being each hash-value of ht using (hash-key k)
          collect (list k v))))
(defpackage "LOOP-SYM-AMI" (:use))
(intern "AA" "LOOP-SYM-AMI")
(intern "BB" "LOOP-SYM-AMI")
(check "loop being symbols" 2
  (length (loop for s being the symbols of "LOOP-SYM-AMI" collect s)))
(defpackage "LOOP-EXT-AMI" (:use) (:export "PUB"))
(intern "PRIV" "LOOP-EXT-AMI")
(check "loop being external-symbols" 1
  (length (loop for s being the external-symbols of "LOOP-EXT-AMI" collect s)))

;; do-all-symbols — iterate symbols of all packages; CAR must be visited,
;; result-form sees var=NIL, and a symbol present in two packages is seen once.
(check "do-all-symbols finds car" t
  (let ((found nil)) (do-all-symbols (s) (when (eq s 'car) (setq found t))) found))
(check "do-all-symbols result-form" :rf
  (do-all-symbols (s :rf)))
(defpackage "DAS-A-AMI" (:use))
(defpackage "DAS-B-AMI" (:use))
(intern "SHARED" "DAS-A-AMI")
(import (find-symbol "SHARED" "DAS-A-AMI") (find-package "DAS-B-AMI"))
(check "do-all-symbols dedup" 1
  (let ((n 0) (target (find-symbol "SHARED" "DAS-A-AMI")))
    (do-all-symbols (s) (when (eq s target) (incf n))) n))
;; Implicit block nil: (return value) must exit the entire do-all-symbols form.
(check "do-all-symbols early return" :found
  (do-all-symbols (s) (when (eq s 'car) (return :found))))

;; loop destructuring
(check "loop destr in" '(3 7 11) (loop for (a b) in '((1 2) (3 4) (5 6)) collect (+ a b)))
(check "loop destr dotted" '((x 1) (y 2) (z 3)) (loop for (a . b) in '((x . 1) (y . 2) (z . 3)) collect (list a b)))
(check "loop destr nested" '(6 15) (loop for (a (b c)) in '((1 (2 3)) (4 (5 6))) collect (+ a b c)))
(check "loop destr on" '((1 (2 3)) (2 (3)) (3 nil)) (loop for (a . b) on '(1 2 3) collect (list a b)))
; CLHS 6.1.2.1.3: FOR/ON tests the end of the list "as if by using atom" (NOT
; endp), so ON over a non-list atom or the dotted tail of an improper list
; terminates instead of signalling a TYPE-ERROR.  FOR/IN keeps endp semantics.
(check "loop on non-list atom" nil (loop for x on 'foo collect x))
(check "loop destr on non-list atom" nil (loop for (k v) on 'foo by #'cddr collect k))
(check "loop destr on hash-table" nil (loop for (k v) on (make-hash-table) by #'cddr collect k))
(check "loop on dotted list stops at atom" '(1 2) (loop for x on '(1 2 . 3) collect (car x)))
(check "loop destr on plist by cddr" '((:a 1) (:b 2)) (loop for (k v) on '(:a 1 :b 2) by #'cddr collect (list k v)))
(check "loop in non-list atom still errors" :caught (handler-case (loop for x in 'foo collect x) (type-error () :caught)))

; --- Printer control variables (Steps 1-2) ---
; Default values per CL spec
(check "print-escape default" t *print-escape*)
(check "print-readably default" nil *print-readably*)
(check "print-base default" 10 *print-base*)
(check "print-radix default" nil *print-radix*)
(check "print-level default" nil *print-level*)
(check "print-length default" nil *print-length*)
(check "print-case default" :upcase *print-case*)
(check "print-gensym default" t *print-gensym*)
(check "print-array default" t *print-array*)
(check "print-circle default" nil *print-circle*)
(check "print-pretty default" nil *print-pretty*)

; prin1-to-string / princ-to-string honor escape variable
(check "prin1-to-string string" "\"hello\"" (prin1-to-string "hello"))
(check "princ-to-string string" "hello" (princ-to-string "hello"))
(check "prin1-to-string char" "#\\Space" (prin1-to-string #\Space))
(check "princ-to-string char" " " (princ-to-string #\Space))
(check "prin1-to-string number" "42" (prin1-to-string 42))

; Dynamic binding of *print-escape* affects princ (which binds NIL)
; and prin1 (which binds T) — verify they override correctly
(check "let print-escape nil princ-to-string"
  "hello" (let ((*print-escape* nil)) (princ-to-string "hello")))
(check "let print-escape t prin1-to-string"
  "\"hello\"" (let ((*print-escape* t)) (prin1-to-string "hello")))

; *print-level* tests
(check "print-level 0" "#"
  (let ((*print-level* 0)) (prin1-to-string '(a b))))
(check "print-level 1" "(A #)"
  (let ((*print-level* 1)) (prin1-to-string '(a (b (c))))))
(check "print-level 2" "(A (B #))"
  (let ((*print-level* 2)) (prin1-to-string '(a (b (c))))))

; *print-length* tests
(check "print-length 0" "(...)"
  (let ((*print-length* 0)) (prin1-to-string '(a b c))))
(check "print-length 2" "(A B ...)"
  (let ((*print-length* 2)) (prin1-to-string '(a b c d e))))
(check "print-length no trunc" "(A B)"
  (let ((*print-length* 10)) (prin1-to-string '(a b))))
(check "print-length dotted" "(A . B)"
  (let ((*print-length* 2)) (prin1-to-string '(a . b))))

; Combined level + length
(check "print-level+length" "(A B ...)"
  (let ((*print-level* 1) (*print-length* 2))
    (prin1-to-string '(a b (c d) e))))

; *print-base* tests
(check "print-base binary" "1010"
  (let ((*print-base* 2)) (prin1-to-string 10)))
(check "print-base binary 255" "11111111"
  (let ((*print-base* 2)) (prin1-to-string 255)))
(check "print-base binary 0" "0"
  (let ((*print-base* 2)) (prin1-to-string 0)))
(check "print-base binary neg" "-101"
  (let ((*print-base* 2)) (prin1-to-string -5)))
(check "print-base octal" "10"
  (let ((*print-base* 8)) (prin1-to-string 8)))
(check "print-base octal 255" "377"
  (let ((*print-base* 8)) (prin1-to-string 255)))
(check "print-base hex FF" "FF"
  (let ((*print-base* 16)) (prin1-to-string 255)))
(check "print-base hex 100" "100"
  (let ((*print-base* 16)) (prin1-to-string 256)))
(check "print-base hex 0" "0"
  (let ((*print-base* 16)) (prin1-to-string 0)))
(check "print-base hex neg" "-1"
  (let ((*print-base* 16)) (prin1-to-string -1)))
(check "print-base 3" "100"
  (let ((*print-base* 3)) (prin1-to-string 9)))
(check "print-base 36" "Z"
  (let ((*print-base* 36)) (prin1-to-string 35)))

; *print-radix* tests
(check "print-radix decimal" "42."
  (let ((*print-radix* t)) (prin1-to-string 42)))
(check "print-radix decimal 0" "0."
  (let ((*print-radix* t)) (prin1-to-string 0)))
(check "print-radix decimal neg" "-7."
  (let ((*print-radix* t)) (prin1-to-string -7)))
(check "print-radix binary" "#b1010"
  (let ((*print-base* 2) (*print-radix* t)) (prin1-to-string 10)))
(check "print-radix octal" "#o377"
  (let ((*print-base* 8) (*print-radix* t)) (prin1-to-string 255)))
(check "print-radix hex" "#xFF"
  (let ((*print-base* 16) (*print-radix* t)) (prin1-to-string 255)))
(check "print-radix base3" "#3r100"
  (let ((*print-base* 3) (*print-radix* t)) (prin1-to-string 9)))

; *print-base* with bignums
(check "print-base bignum hex" "100000000"
  (let ((*print-base* 16)) (prin1-to-string (expt 2 32))))
(check "print-base bignum binary" "10000000000000000"
  (let ((*print-base* 2)) (prin1-to-string (expt 2 16))))
(check "print-base bignum octal" "200000"
  (let ((*print-base* 8)) (prin1-to-string 65536)))
(check "print-radix bignum hex" "#x100000000"
  (let ((*print-base* 16) (*print-radix* t)) (prin1-to-string (expt 2 32))))
(check "print-radix bignum decimal" "4294967296."
  (let ((*print-radix* t)) (prin1-to-string (expt 2 32))))

; *print-case* tests
(check "print-case upcase" "HELLO"
  (let ((*print-case* :upcase)) (prin1-to-string 'hello)))
(check "print-case downcase" "hello"
  (let ((*print-case* :downcase)) (prin1-to-string 'hello)))
(check "print-case downcase hyphen" "foo-bar"
  (let ((*print-case* :downcase)) (prin1-to-string 'foo-bar)))
(check "print-case downcase nil" "nil"
  (let ((*print-case* :downcase)) (prin1-to-string nil)))
(check "print-case downcase t" "t"
  (let ((*print-case* :downcase)) (prin1-to-string t)))
(check "print-case downcase keyword" ":foo"
  (let ((*print-case* :downcase)) (prin1-to-string :foo)))
(check "print-case capitalize" "Hello"
  (let ((*print-case* :capitalize)) (prin1-to-string 'hello)))
(check "print-case capitalize hyphen" "Foo-Bar"
  (let ((*print-case* :capitalize)) (prin1-to-string 'foo-bar)))
(check "print-case capitalize nil" "Nil"
  (let ((*print-case* :capitalize)) (prin1-to-string nil)))
(check "print-case capitalize keyword" ":Test"
  (let ((*print-case* :capitalize)) (prin1-to-string :test)))
(check "print-case downcase list" "(a b c)"
  (let ((*print-case* :downcase)) (prin1-to-string '(a b c))))
(check "print-case capitalize list" "(Hello World)"
  (let ((*print-case* :capitalize)) (prin1-to-string '(hello world))))

; *print-gensym* tests
(check "print-gensym default" "#:FOO"
  (prin1-to-string (make-symbol "FOO")))
(check "print-gensym nil" "FOO"
  (let ((*print-gensym* nil)) (prin1-to-string (make-symbol "FOO"))))
(check "print-gensym nil + downcase" "foo"
  (let ((*print-gensym* nil) (*print-case* :downcase))
    (prin1-to-string (make-symbol "FOO"))))

; *print-array* tests
(check "print-array default" "#(1 2 3)"
  (prin1-to-string (vector 1 2 3)))
(check "print-array nil vector" "#<VECTOR>"
  (let ((*print-array* nil)) (prin1-to-string (vector 1 2 3))))
(check "print-array nil multi-dim" "#<ARRAY>"
  (let ((*print-array* nil)) (prin1-to-string (make-array '(2 3)))))

; --- WRITE function with keyword arguments ---
(check "write returns object" 42
  (let ((x (write 42 :stream (make-string-output-stream)))) x))
(check "write :escape nil" "hello"
  (let ((s (make-string-output-stream)))
    (write "hello" :stream s :escape nil)
    (get-output-stream-string s)))
(check "write :escape t" "\"hello\""
  (let ((s (make-string-output-stream)))
    (write "hello" :stream s :escape t)
    (get-output-stream-string s)))
(check "write :base 16" "FF"
  (let ((s (make-string-output-stream)))
    (write 255 :stream s :base 16)
    (get-output-stream-string s)))
(check "write :base 2" "1010"
  (let ((s (make-string-output-stream)))
    (write 10 :stream s :base 2)
    (get-output-stream-string s)))
(check "write :radix t :base 16" "#xFF"
  (let ((s (make-string-output-stream)))
    (write 255 :stream s :base 16 :radix t)
    (get-output-stream-string s)))
(check "write :level 1" "(A #)"
  (let ((s (make-string-output-stream)))
    (write '(a (b (c))) :stream s :level 1)
    (get-output-stream-string s)))
(check "write :length 2" "(A B ...)"
  (let ((s (make-string-output-stream)))
    (write '(a b c d e) :stream s :length 2)
    (get-output-stream-string s)))
(check "write :case :downcase" "hello"
  (let ((s (make-string-output-stream)))
    (write 'hello :stream s :case :downcase)
    (get-output-stream-string s)))
(check "write :case :capitalize" "Foo-Bar"
  (let ((s (make-string-output-stream)))
    (write 'foo-bar :stream s :case :capitalize)
    (get-output-stream-string s)))
(check "write :gensym nil" "FOO"
  (let ((s (make-string-output-stream)))
    (write (make-symbol "FOO") :stream s :gensym nil)
    (get-output-stream-string s)))
(check "write :array nil" "#<VECTOR>"
  (let ((s (make-string-output-stream)))
    (write (vector 1 2) :stream s :array nil)
    (get-output-stream-string s)))

; --- *print-circle* ---
(check "circle cdr cycle" "#0=(1 2 . #0#)"
  (let ((*print-circle* t))
    (let ((x (list 1 2)))
      (rplacd (cdr x) x)
      (prin1-to-string x))))
(check "circle car self-ref" "#0=(#0#)"
  (let ((*print-circle* t))
    (let ((x (list nil)))
      (rplaca x x)
      (prin1-to-string x))))
(check "circle shared sub" "(#0=(1 2) #0#)"
  (let ((*print-circle* t))
    (let ((sub (list 1 2)))
      (prin1-to-string (list sub sub)))))
(check "circle no sharing" "(1 2 3)"
  (let ((*print-circle* t))
    (prin1-to-string '(1 2 3))))
(check "circle nil default" "((1) (1))"
  (let ((sub (list 1)))
    (prin1-to-string (list sub sub))))
(check "circle vector self" "#0=#(#0# 42)"
  (let ((*print-circle* t))
    (let ((v (vector nil 42)))
      (setf (aref v 0) v)
      (prin1-to-string v))))
(check "circle deep shared" "((#0=(99)) (#0#))"
  (let ((*print-circle* t))
    (let ((leaf (list 99)))
      (prin1-to-string (list (list leaf) (list leaf))))))
(check "write-to-string :circle" "#0=(1 2 . #0#)"
  (let ((x (list 1 2)))
    (rplacd (cdr x) x)
    (write-to-string x :circle t)))

; --- Format directive extensions ---
(check "format ~D" "42" (format nil "~D" 42))
(check "format ~D negative" "-7" (format nil "~D" -7))
(check "format ~D zero" "0" (format nil "~D" 0))
(check "format ~B" "1010" (format nil "~B" 10))
(check "format ~B 255" "11111111" (format nil "~B" 255))
(check "format ~O" "10" (format nil "~O" 8))
(check "format ~O 255" "377" (format nil "~O" 255))
(check "format ~X" "FF" (format nil "~X" 255))
(check "format ~X 256" "100" (format nil "~X" 256))
(check "format ~X zero" "0" (format nil "~X" 0))
(check "format ~C" "A" (format nil "~C" #\A))
(check "format ~C space" " " (format nil "~C" #\Space))
(check "format ~W string" "\"hello\"" (format nil "~W" "hello"))
(check "format ~W number" "42" (format nil "~W" 42))
(check "format ~& freshline" 11 (length (format nil "hello~&world")))
(check "format ~| page" 3 (length (format nil "a~|b")))
(check "format ~| page char" 12 (let ((s (format nil "a~|b"))) (if (>= (length s) 2) (char-code (char s 1)) -1)))
(check "format mixed" "dec=255 hex=FF bin=11111111"
  (format nil "dec=~D hex=~X bin=~B" 255 255 255))

; --- Advanced format: padding/commas/sign (Step 1) ---
(check "format ~8D pad" "      42" (format nil "~8D" 42))
(check "format ~8,'0D padchar" "00000042" (format nil "~8,'0D" 42))
(check "format ~:D commas" "1,234,567" (format nil "~:D" 1234567))
(check "format ~@D sign" "+42" (format nil "~@D" 42))
(check "format ~@D neg" "-7" (format nil "~@D" -7))
(check "format ~10A pad right" "hello     " (format nil "~10A" "hello"))
(check "format ~10@A pad left" "     hello" (format nil "~10@A" "hello"))
(check "format ~10S" "\"hello\"   " (format nil "~10S" "hello"))
(check "format ~3%" 3 (length (format nil "~3%")))
(check "format ~5~" "~~~~~" (format nil "~5~"))

; --- Advanced format: ~* goto, ~T tabulate (Step 2) ---
(check "format ~* skip" "B" (format nil "~*~A" 'a 'b))
(check "format ~2* skip2" "C" (format nil "~2*~A" 'a 'b 'c))
(check "format ~:* backup" "XX" (format nil "~A~:*~A" 'x))

; --- Advanced format: ~(~) case conversion (Step 3) ---
(check "format ~( lowercase" "hello world" (format nil "~(HELLO WORLD~)"))
(check "format ~:( capitalize" "Hello World" (format nil "~:(hello world~)"))
(check "format ~@( cap first" "Hello world" (format nil "~@(hello world~)"))
(check "format ~:@( upcase" "HELLO WORLD" (format nil "~:@(hello world~)"))

; --- Advanced format: ~[~;~] conditional (Step 4) ---
(check "format ~[ numeric 0" "zero" (format nil "~[zero~;one~;two~]" 0))
(check "format ~[ numeric 1" "one" (format nil "~[zero~;one~;two~]" 1))
(check "format ~[ default" "other" (format nil "~[zero~;one~:;other~]" 99))
(check "format ~:[ false" "false" (format nil "~:[false~;true~]" nil))
(check "format ~:[ true" "true" (format nil "~:[false~;true~]" 42))
(check "format ~@[ non-nil" "x=42 done" (format nil "~@[x=~A ~]done" 42))
(check "format ~@[ nil" "done" (format nil "~@[x=~A ~]done" nil))

; --- Advanced format: ~{~} iteration, ~^ (Step 5) ---
(check "format ~{~} list" "1 2 3 " (format nil "~{~A ~}" '(1 2 3)))
(check "format ~{~^~} sep" "1, 2, 3" (format nil "~{~A~^, ~}" '(1 2 3)))
(check "format ~{~} empty" "" (format nil "~{~A~}" nil))
(check "format ~:{~} sublists" "X=1 Y=2 " (format nil "~:{~A=~A ~}" '((x 1) (y 2))))
(check "format ~@{~^~} atsign" "1, 2, 3" (format nil "~@{~A~^, ~}" 1 2 3))
(check "format ~2{~}" "1 2 " (format nil "~2{~A ~}" '(1 2 3 4 5)))
(check "format ~^ single" "X" (format nil "~{~A~^-~}" '(x)))
(check "format ~^ multi" "X-Y-Z" (format nil "~{~A~^-~}" '(x y z)))

; --- Advanced format: ~? recursive, ~R radix (Step 6) ---
(check "format ~? recursive" "1 2 and 3" (format nil "~? and ~A" "~A ~A" '(1 2) 3))
(check "format ~@? atsign" "1 2 and 3" (format nil "~@? and ~A" "~A ~A" 1 2 3))
; formatter (CLHS): returns a function usable as a ~? control.
(check "formatter funcall" "3-4"
  (with-output-to-string (s) (funcall (formatter "~d-~d") s 3 4)))
(check "formatter via ~?" "7/8" (format nil "~?" (formatter "~d/~d") '(7 8)))
; A ~? function control re-enters the VM; a trailing directive must still see
; the right args (serapeum file-size-human-readable regression).
(check "formatter ~? no arg clobber" "1234X"
  (format nil "~?~a" (formatter "~a~a~a~a") '(1 2 3 4) "X"))
(check "formatter ~? conditional no clobber" "500 k"
  (format nil "~?~a" (formatter "~:[~d~;~,1f~]~:[~; ~]~a") '(nil 500 t "k") ""))
; ~@? with a function (formatter) control: trailing directive must see unconsumed arg.
(check "formatter ~@? function trailing" "1 2 and 3"
  (format nil "~@? and ~A" (formatter "~A ~A") 1 2 3))
(check "formatter ~@? function partial" "4299"
  (format nil "~@?~A" (formatter "~D") 42 99))
(check "formatter ~@? function consumes all" "1 2"
  (format nil "~@?" (formatter "~A ~A") 1 2))
; with-output-to-string with a target fill-pointer string (serapeum with-string).
(check "with-output-to-string target appends" "the string"
  (let ((out (make-array 4 :adjustable t :fill-pointer 4
                         :element-type 'character :initial-contents "the ")))
    (with-output-to-string (s out) (write-string "string" s))
    out))
(check "with-output-to-string target returns body" :body-value
  (let ((out (make-array 0 :adjustable t :fill-pointer 0 :element-type 'character)))
    (with-output-to-string (s out) (write-string "x" s) :body-value)))
(check "with-output-to-string target string-form evaluated once" 1
  (let ((n 0))
    (flet ((get-buf () (incf n)
                       (make-array 0 :adjustable t :fill-pointer 0
                                   :element-type 'character)))
      (with-output-to-string (s (get-buf)) (write-string "abc" s))
      n)))
; CLHS: string-form is evaluated before the body forms (side-effects visible inside body).
(check "with-output-to-string target string-form eval before body" t
  (let ((ready nil)
        (out (make-array 0 :adjustable t :fill-pointer 0 :element-type 'character)))
    (with-output-to-string (s (progn (setf ready t) out))
      (write-string (if ready "ok" "bad") s))
    (string= (coerce out 'string) "ok")))
; CLHS: with a target string, the form returns the values returned by the body forms.
(check "with-output-to-string target multiple values" '(1 2)
  (let ((out (make-array 0 :adjustable t :fill-pointer 0 :element-type 'character)))
    (multiple-value-list
      (with-output-to-string (s out) (values 1 2)))))
(check "format ~R zero" "zero" (format nil "~R" 0))
(check "format ~R cardinal" "forty-two" (format nil "~R" 42))
(check "format ~R thousand" "one thousand two hundred thirty-four" (format nil "~R" 1234))
(check "format ~:R ordinal" "first" (format nil "~:R" 1))
(check "format ~:R 21" "twenty-first" (format nil "~:R" 21))
(check "format ~@R roman" "XLII" (format nil "~@R" 42))
(check "format ~@R 1999" "MCMXCIX" (format nil "~@R" 1999))
(check "format ~:@R old roman" "IIII" (format nil "~:@R" 4))
(check "format ~2R binary" "1010" (format nil "~2R" 10))
(check "format ~16R hex" "FF" (format nil "~16R" 255))

; --- Pretty-printing / *print-pretty* ---
(check "print-pretty default nil" nil *print-pretty*)
(check "print-right-margin default nil" nil *print-right-margin*)
(check "pretty short list" "(1 2 3)"
  (let ((*print-pretty* t) (*print-right-margin* 40))
    (write-to-string '(1 2 3))))
(check "pretty long list" 25
  (length (let ((*print-pretty* t) (*print-right-margin* 10))
    (write-to-string '(alpha beta gamma delta)))))
(check "pretty off no break" "(ALPHA BETA GAMMA)"
  (let ((*print-pretty* nil) (*print-right-margin* 5))
    (write-to-string '(alpha beta gamma))))
(check "pretty write keyword" "(1 2 3)"
  (write-to-string '(1 2 3) :pretty t :right-margin 40))
(check "pretty write-to-string narrow" 25
  (length (write-to-string '(alpha beta gamma delta) :pretty t :right-margin 10)))
(check "pprint basic" 8
  (length (let ((s (make-string-output-stream)))
    (pprint '(1 2 3) s)
    (get-output-stream-string s))))
(check "pretty empty nil" "NIL"
  (let ((*print-pretty* t) (*print-right-margin* 10))
    (write-to-string nil)))
(check "pretty single elt" "(X)"
  (let ((*print-pretty* t) (*print-right-margin* 10))
    (write-to-string '(x))))
(check "pretty dotted" "(A . B)"
  (let ((*print-pretty* t) (*print-right-margin* 40))
    (write-to-string '(a . b))))

; --- Ratio Numbers ---
; Reader
(check "read ratio" 1/2 1/2)
(check "read ratio neg" -3/4 -3/4)
(check "read ratio normalize" 1/2 2/4)
(check "read ratio demote" 2 6/3)

; Predicates
(check "ratiop ratio" t (typep 1/2 'ratio))
(check "ratiop int" nil (typep 5 'ratio))
(check "rationalp ratio" t (rationalp 1/2))
(check "rationalp int" t (rationalp 5))
(check "rationalp float" nil (rationalp 1.0))

; Type
(check "type-of ratio" 'ratio (type-of 1/2))
(check "subtypep ratio rational" t (subtypep 'ratio 'rational))

; Arithmetic
(check "ratio add" 5/6 (+ 1/2 1/3))
(check "ratio add int" 3/2 (+ 1/2 1))
(check "ratio sub" 1/6 (- 1/2 1/3))
(check "ratio mul" 1/6 (* 1/2 1/3))
(check "ratio div" 3/2 (/ 1/2 1/3))
(check "ratio negate" -1/2 (- 1/2))
(check "div to ratio" 1/2 (/ 1 2))
(check "div reduces" 3/2 (/ 6 4))
(check "div to integer" 2 (/ 6 3))
(check "reciprocal" 1/3 (/ 3))

; Comparison
(check "ratio < true" t (< 1/3 1/2))
(check "ratio > true" t (> 1/2 1/3))
(check "ratio = same" t (= 1/2 2/4))
(check "ratio = float" t (= 1/2 0.5))
(check "ratio < int" t (< 1/3 1))
(check "ratio > int" t (> 3/2 1))

; Regression: rational/float comparison must stay correct across a moving GC.
; cl_ratio_compare held the first cross-multiply result, and cl_float_compare
; held a rational operand, in CL_Obj locals left unprotected across an
; allocating call; a compaction fired mid-compare relocated them and the
; comparison returned the wrong sign.  Mirrors ansi-test
; RATIONAL.LONG-FLOAT.RANDOM.COMPARE.1.  For x = p/q > 1, fr = (floor r) and
; cr = (ceiling r), the relations fr/x < r < cr*x must hold at every magnitude.
; The retained filler array keeps the heap pressured so the big-bignum
; cross-multiplies trigger compaction.  (aref filler 0) is 1, so the returned
; value is exactly the failure count; expected 0.
(check "rational/float compare gc-safe" 0
  (let ((fails 0)
        (filler (make-array 50000 :initial-element 1)))
    (do ((bound 1.0d0 (* bound 16)))
        ((> bound 1.0d120))
      (dotimes (k 12)
        (let* ((r (+ 1.0d0 (* bound (/ (coerce (1+ k) 'double-float) 13.0d0))))
               (fr (floor r))
               (cr (ceiling r))
               (p (+ 1000003 (* k 7)))
               (q (+ 7 k))
               (x (/ p q))
               (fr/x (/ fr x))
               (cr*x (* cr x)))
          (unless (and (<= fr/x r cr*x)
                       (< fr/x r cr*x)
                       (> cr*x r fr/x)
                       (>= cr*x r fr/x))
            (setq fails (+ fails 1))))))
    (+ fails (aref filler 0) -1)))

; Predicates on ratios
(check "zerop ratio" nil (zerop 1/2))
(check "plusp ratio" t (plusp 1/2))
(check "minusp ratio" t (minusp -1/2))
(check "abs ratio" 3/4 (abs -3/4))

; min/max
(check "min ratio" 1/4 (min 1/2 1/3 1/4))
(check "max ratio" 1/2 (max 1/2 1/3 1/4))

; Accessors
(check "numerator ratio" 3 (numerator 3/4))
(check "numerator int" 5 (numerator 5))
(check "denominator ratio" 4 (denominator 3/4))
(check "denominator int" 1 (denominator 5))

; Rounding
(check "floor ratio" 3 (floor 7/2))
(check "ceiling ratio" 4 (ceiling 7/2))
(check "truncate ratio" 3 (truncate 7/2))
(check "round ratio" 4 (round 7/2))

; Coerce
(check "coerce ratio float" 0.5 (coerce 1/2 'single-float))

; Float contagion
(check "ratio + float" 1.0 (+ 1/2 0.5))

; eql/equal
(check "eql ratio" t (eql 1/2 2/4))
(check "equal ratio" t (equal 1/2 2/4))
(check "eql ratio diff" nil (eql 1/2 1/3))

; Hash tables with ratio keys
(check "ratio hash key"
  'half
  (let ((ht (make-hash-table)))
    (setf (gethash 1/2 ht) 'half)
    (gethash 1/2 ht)))
(check "ratio hash normalized"
  'half
  (let ((ht (make-hash-table)))
    (setf (gethash 1/2 ht) 'half)
    (gethash 2/4 ht)))

; expt with ratios
(check "expt ratio" 1/8 (expt 1/2 3))
(check "expt ratio neg" 3/2 (expt 2/3 -1))
(check "expt int neg" 1/2 (expt 2 -1))

; 1+/1-
(check "1+ ratio" 3/2 (1+ 1/2))
(check "1- ratio" -1/2 (1- 1/2))

; mod/rem
(check "mod ratio" 1/2 (mod 5/2 1))
(check "rem ratio" 1/2 (rem 5/2 1))

; rational/rationalize
(check "rational int" 5 (rational 5))
(check "rational ratio" 1/2 (rational 1/2))
(check "rational float" 1/2 (rational 0.5))
(check "rationalize float" 1/2 (rationalize 0.5))

; Printer
(check "print ratio" "1/2" (write-to-string 1/2))
(check "print ratio neg" "-3/4" (write-to-string -3/4))

; --- rotatef / shiftf ---
(defparameter *rfa* 1)
(defparameter *rfb* 2)
(defparameter *rfc* 3)
(rotatef *rfa* *rfb* *rfc*)
(check "rotatef a" 2 *rfa*)
(check "rotatef b" 3 *rfb*)
(check "rotatef c" 1 *rfc*)

(defparameter *rf2x* 10)
(defparameter *rf2y* 20)
(rotatef *rf2x* *rf2y*)
(check "rotatef two x" 20 *rf2x*)
(check "rotatef two y" 10 *rf2y*)

(check "rotatef returns nil" nil (let ((a 1) (b 2)) (rotatef a b)))

(defparameter *sfa* 1)
(defparameter *sfb* 2)
(defparameter *sfc* 3)
(defparameter *sf-result* (shiftf *sfa* *sfb* *sfc* 99))
(check "shiftf result" 1 *sf-result*)
(check "shiftf a" 2 *sfa*)
(check "shiftf b" 3 *sfb*)
(check "shiftf c" 99 *sfc*)

(defparameter *sf2x* 10)
(check "shiftf two" 10 (shiftf *sf2x* 42))
(check "shiftf two val" 42 *sf2x*)

(defparameter *sfc-x* (cons 5 6))
(check "shiftf car" 5 (shiftf (car *sfc-x*) 77))
(check "shiftf car val" 77 (car *sfc-x*))

; --- pprint-newline ---
(check "pprint-newline mandatory" t (progn (pprint-newline :mandatory) t))
(check "pprint-newline fill" t (progn (pprint-newline :fill) t))
(check "pprint-newline linear" t (progn (pprint-newline :linear) t))
(check "pprint-newline miser" t (progn (pprint-newline :miser) t))

; --- pprint-indent ---
(check "pprint-indent current" t (progn (pprint-indent :current 2) t))
(check "pprint-indent block" t (progn (pprint-indent :block 4) t))

; --- pprint-logical-block ---
(check "pprint-logical-block" "[1 2 3]" (with-output-to-string (s) (pprint-logical-block (s '(1 2 3) :prefix "[" :suffix "]") (princ (pprint-pop) s) (pprint-exit-if-list-exhausted) (write-char #\Space s) (princ (pprint-pop) s) (pprint-exit-if-list-exhausted) (write-char #\Space s) (princ (pprint-pop) s))))

; --- pprint-dispatch ---
(check "copy-pprint-dispatch" nil (copy-pprint-dispatch))
(set-pprint-dispatch 'integer (lambda (s obj) (write-string "[" s) (princ obj s) (write-string "]" s)) 1)
(check "pprint-dispatch custom" "[42]" (let ((*print-pretty* t)) (with-output-to-string (s) (write 42 :stream s :pretty t))))
(set-pprint-dispatch 'integer nil)

; --- Bit manipulation ---
(check "logcount 0" 0 (logcount 0))
(check "logcount 13" 3 (logcount 13))
(check "logcount -1" 0 (logcount -1))
(check "logcount -13" 2 (logcount -13))
(check "logcount 255" 8 (logcount 255))
(check "logbitp 0 1" t (logbitp 0 1))
(check "logbitp 1 1" nil (logbitp 1 1))
(check "logbitp 0 0" nil (logbitp 0 0))
(check "logbitp 3 15" t (logbitp 3 15))
(check "logbitp 4 15" nil (logbitp 4 15))
(check "logbitp neg" t (logbitp 0 -1))
(check "logbitp neg high" t (logbitp 100 -1))
(check "logtest true" t (logtest 6 5))
(check "logtest false" nil (logtest 4 2))
(check "byte" '(8 . 4) (byte 8 4))
(check "byte-size" 8 (byte-size (byte 8 4)))
(check "byte-position" 4 (byte-position (byte 8 4)))
(check "ldb low" 5 (ldb (byte 4 0) 4085))
(check "ldb high" 15 (ldb (byte 4 4) 255))
(check "ldb mid" 6 (ldb (byte 4 4) 111))
(check "dpb basic" 245 (dpb 15 (byte 4 4) 5))
(check "ldb-test true" t (ldb-test (byte 4 0) 15))
(check "ldb-test false" nil (ldb-test (byte 4 4) 15))
(check "mask-field" 240 (mask-field (byte 4 4) 255))
(check "deposit-field" 175 (deposit-field 170 (byte 4 4) 15))
(check "boole and" 9 (boole boole-and 15 9))
(check "boole ior" 15 (boole boole-ior 15 9))
(check "boole xor" 6 (boole boole-xor 15 9))
(check "boole clr" 0 (boole boole-clr 15 9))
(check "boole set" -1 (boole boole-set 15 9))
(check "boole 1" 15 (boole boole-1 15 9))
(check "boole 2" 9 (boole boole-2 15 9))
(check "boole c1" -16 (boole boole-c1 15 9))
(check "boole c2" -10 (boole boole-c2 15 9))
(check "boole eqv" -7 (boole boole-eqv 15 9))
(check "boole nand" -10 (boole boole-nand 15 9))
(check "boole nor" -16 (boole boole-nor 15 9))
(check "boole andc1" 0 (boole boole-andc1 15 9))
(check "boole andc2" 6 (boole boole-andc2 15 9))
(check "boole orc1" -7 (boole boole-orc1 15 9))
(check "boole orc2" -1 (boole boole-orc2 15 9))
(check "boole-clr val" 0 boole-clr)
(check "boole-set val" 1 boole-set)
(check "boole-and val" 6 boole-and)
(check "boole-ior val" 7 boole-ior)
(check "boole-xor val" 8 boole-xor)
; Bignum logcount/logbitp
(check "logcount bignum" 1 (logcount (expt 2 40)))
(check "logbitp bignum" t (logbitp 40 (expt 2 40)))
(check "logbitp bignum false" nil (logbitp 39 (expt 2 40)))

; --- Bit Vectors ---
; Reader/printer round-trip
(check "bv reader empty" "#*" (format nil "~A" #*))
(check "bv reader 10110" "#*10110" (format nil "~A" #*10110))
(check "bv reader 32bit" "#*10101010101010101010101010101010" (format nil "~A" #*10101010101010101010101010101010))

; Predicates
(check "bit-vector-p t" t (bit-vector-p #*1010))
(check "bit-vector-p nil" nil (bit-vector-p #(1 0 1 0)))
(check "simple-bit-vector-p" t (simple-bit-vector-p #*1010))
(check "vectorp bv" t (vectorp #*1010))
(check "arrayp bv" t (arrayp #*1010))

; type-of, typep
(check "type-of bv" 'simple-bit-vector (type-of #*1010))
(check "typep bv bit-vector" t (typep #*1010 'bit-vector))
(check "typep bv vector" t (typep #*1010 'vector))
(check "typep bv array" t (typep #*1010 'array))
(check "typep bv sequence" t (typep #*1010 'sequence))
(check "typep bv simple-array" t (typep #*1010 'simple-array))
(check "typep bv string" nil (typep #*1010 'string))

; subtypep
(check "subtypep bv<vector" t (subtypep 'bit-vector 'vector))
(check "subtypep sbv<bv" t (subtypep 'simple-bit-vector 'bit-vector))
(check "subtypep bv<sequence" t (subtypep 'bit-vector 'sequence))

; Element access
(check "bit 0" 1 (bit #*10110 0))
(check "bit 1" 0 (bit #*10110 1))
(check "bit 2" 1 (bit #*10110 2))
(check "sbit 0" 1 (sbit #*1100 0))
(check "sbit 3" 0 (sbit #*1100 3))
(check "aref bv" 1 (aref #*10110 2))

; setf bit
(check "setf bit" "#*0010" (let ((bv #*0000)) (setf (bit bv 2) 1) (format nil "~A" bv)))
(check "setf sbit" "#*1011" (let ((bv #*1111)) (setf (sbit bv 1) 0) (format nil "~A" bv)))
(check "setf aref bv" "#*1000" (let ((bv #*0000)) (setf (aref bv 0) 1) (format nil "~A" bv)))

; make-array with element-type bit
(check "make-array bit" t (bit-vector-p (make-array 5 :element-type 'bit)))
(check "make-array bit ie1" "#*1111" (format nil "~A" (make-array 4 :element-type 'bit :initial-element 1)))
(check "make-array bit ic" "#*1010" (format nil "~A" (make-array 4 :element-type 'bit :initial-contents '(1 0 1 0))))

; Length / elt
(check "length bv" 5 (length #*10110))
(check "length bv empty" 0 (length #*))
(check "elt bv 0" 1 (elt #*10110 0))
(check "elt bv 4" 0 (elt #*10110 4))

; find / position / count
(check "find 1 bv" 1 (find 1 #*00100))
(check "find 1 bv nil" nil (find 1 #*00000))
(check "position 1 bv" 2 (position 1 #*00100))
(check "count 1 bv" 3 (count 1 #*10110))
(check "count 0 bv" 2 (count 0 #*10110))

; copy-seq
(check "copy-seq bv" "#*10110" (format nil "~A" (copy-seq #*10110)))
(check "copy-seq independent" '("#*001" "#*101")
  (let ((a #*101) (b (copy-seq #*101)))
    (setf (bit a 0) 0)
    (list (format nil "~A" a) (format nil "~A" b))))

; reverse
(check "reverse bv" "#*01101" (format nil "~A" (reverse #*10110)))
(check "reverse bv empty" "#*" (format nil "~A" (reverse #*)))

; fill
(check "fill bv" "#*11111" (let ((bv #*00000)) (fill bv 1) (format nil "~A" bv)))
(check "fill bv range" "#*10011" (let ((bv #*11111)) (fill bv 0 :start 1 :end 3) (format nil "~A" bv)))

; setf elt
(check "setf elt bv" "#*0010" (let ((bv #*0000)) (setf (elt bv 2) 1) (format nil "~A" bv)))

; equal
(check "equal bv same" t (equal #*10110 #*10110))
(check "equal bv diff" nil (equal #*10110 #*10111))
(check "equal bv len" nil (equal #*10110 #*1011))
(check "equal bv empty" t (equal #* #*))
(check "equal bv vs vec" nil (equal #*101 #(1 0 1)))

; coerce
(check "coerce bv->list" '(1 0 1 1 0) (coerce #*10110 'list))
(check "coerce list->bv" "#*10110" (format nil "~A" (coerce '(1 0 1 1 0) 'bit-vector)))
(check "coerce bv->vector" t (equalp #(1 0 1) (coerce #*101 'vector)))

; Bitwise operations
(check "bit-and" "#*1000" (format nil "~A" (bit-and #*1100 #*1010)))
(check "bit-ior" "#*1110" (format nil "~A" (bit-ior #*1100 #*1010)))
(check "bit-xor" "#*0110" (format nil "~A" (bit-xor #*1100 #*1010)))
(check "bit-eqv" "#*1001" (format nil "~A" (bit-eqv #*1100 #*1010)))
(check "bit-nand" "#*0111" (format nil "~A" (bit-nand #*1100 #*1010)))
(check "bit-nor" "#*0001" (format nil "~A" (bit-nor #*1100 #*1010)))
(check "bit-andc1" "#*0010" (format nil "~A" (bit-andc1 #*1100 #*1010)))
(check "bit-andc2" "#*0100" (format nil "~A" (bit-andc2 #*1100 #*1010)))
(check "bit-orc1" "#*1011" (format nil "~A" (bit-orc1 #*1100 #*1010)))
(check "bit-orc2" "#*1101" (format nil "~A" (bit-orc2 #*1100 #*1010)))
(check "bit-not" "#*01001" (format nil "~A" (bit-not #*10110)))
(check "bit-and in-place" "#*1000" (let ((a #*1100)) (bit-and a #*1010 t) (format nil "~A" a)))

; Array queries
(check "array-dimensions bv" '(5) (array-dimensions #*10110))
(check "array-rank bv" 1 (array-rank #*10110))
(check "array-dimension bv" 5 (array-dimension #*10110 0))
(check "array-total-size bv" 5 (array-total-size #*10110))
(check "row-major-aref bv" 1 (row-major-aref #*10110 2))
(check "array-has-fp bv" nil (array-has-fill-pointer-p #*1010))
(check "adjustable-p bv" nil (adjustable-array-p #*1010))

; --- Modules (provide / require) ---
(check "provide returns t" t (provide "test-mod-1"))
(check "modules contains provided" "test-mod-1" (find "test-mod-1" *modules* :test #'string=))
(check "provide no duplicate" 1 (progn (provide "test-mod-1") (count "test-mod-1" *modules* :test #'string=)))
(check "provide symbol" t (provide 'test-mod-sym))
(check "provide symbol name" "TEST-MOD-SYM" (find "TEST-MOD-SYM" *modules* :test #'string=))
(check "require already provided" nil (require "test-mod-1"))
(check "modules is list" t (listp *modules*))
; CLHS: module-name is a string designator, which includes characters
; (a character denotes the singleton string containing it).
(check "provide character designator" t (provide #\Z))
(check "modules contains char-named module" "Z" (find "Z" *modules* :test #'string=))
(check "require character already provided" nil (require #\Z))

; --- Read-time eval (#.) ---
(check "#. arithmetic" 3 #.(+ 1 2))
(check "#. list" '(1 2) '#.(list 1 2))
(check "#. in list" '(a 30 b) (list 'a #.(+ 10 20) 'b))
(check "*read-eval* default" t *read-eval*)

; --- CLOS: %class-of ---
(check "%class-of fixnum" 'fixnum (%class-of 42))
(check "%class-of nil" 'null (%class-of nil))
(check "%class-of symbol" 'symbol (%class-of 'foo))
(check "%class-of cons" 'cons (%class-of '(1 2)))
(check "%class-of string" 'string (%class-of "hello"))
; Regression: MAKE-ARRAY character strings must report the STRING class so
; STRING-specialized CLOS methods dispatch (host wide-string class-of bug).
(check "%class-of make-array char string" 'string
       (%class-of (make-array 3 :element-type 'character :initial-element #\a)))
(check "%class-of make-array base-char" 'string
       (%class-of (make-array '(1) :element-type 'base-char :initial-element #\a)))
(check "%class-of character" 'character (%class-of #\A))
(check "%class-of function" 'function (%class-of #'car))
(check "%class-of lambda" 'function (%class-of (lambda (x) x)))
(check "%class-of vector" 'vector (%class-of (vector 1 2 3)))
(check "%class-of hash-table" 'hash-table (%class-of (make-hash-table)))
(check "%class-of float" 'single-float (%class-of 3.14))
(check "%class-of ratio" 'ratio (%class-of 1/3))
(check "%class-of pathname" 'pathname (%class-of #P"/tmp/foo"))
(check "%class-of package" 'package (%class-of (find-package :cl)))
(check "%class-of stream" 'stream (%class-of (make-string-input-stream "hi")))
(defstruct clos-amiga-pt (x 0) (y 0))
(check "%class-of struct" 'clos-amiga-pt (%class-of (make-clos-amiga-pt :x 1)))

; Regression (CLOS dispatch field bug): CLASS-OF of a built-in-typed object
; must NOT depend on *PACKAGE*.  bi_class_of interned the class name via a
; *PACKAGE*-relative intern (backed by a thread-shared cl_current_package
; global); when *PACKAGE* did not resolve the name to its CL-package symbol
; (KEYWORD here, or a peer thread's *PACKAGE* in a multi-threaded session),
; CLASS-OF returned the wrong symbol, missed *class-table*, and fell back to
; the T class — making GF dispatch signal "No applicable method for (X SYMBOL)".
(check "class-of symbol independent of *package*" t
       (let ((sc (find-class 'symbol)))
         (let ((*package* (find-package :keyword)))
           (eq sc (class-of 'foo)))))
(check "class-of builtins independent of *package*" t
       (let ((sc (find-class 'symbol))
             (cc (find-class 'cons))
             (strc (find-class 'string))
             (fc (find-class 'fixnum)))
         (let ((*package* (find-package :keyword)))
           (and (eq sc (class-of 'foo))
                (eq cc (class-of '(1 2)))
                (eq strc (class-of "x"))
                (eq fc (class-of 42))))))
(check "%class-of name is CL-package symbol under any *package*" t
       (let ((*package* (find-package :keyword)))
         (eq (find-package :common-lisp) (symbol-package (%class-of 'foo)))))
(defgeneric clos-amiga-symdisp (a b))
(defmethod clos-amiga-symdisp ((a t) (b symbol)) :sym)
(defmethod clos-amiga-symdisp ((a t) (b string)) :str)
(check "dispatch (t symbol) applicable under foreign *package*" :sym
       (let ((*package* (find-package :keyword)))
         (clos-amiga-symdisp 42 'anything)))

; --- CLOS: Bootstrap core classes (Step 2) ---
(require "clos")
(check "find-class integer" t (structurep (find-class 'integer)))
(check "class-name integer" 'integer (class-name (find-class 'integer)))
(check "class-name string" 'string (class-name (find-class 'string)))
(check "class-name t" 't (class-name (find-class 't)))
(check "find-class unknown nil" nil (find-class 'no-such-class nil))
(check "class-of 42" 'fixnum (class-name (class-of 42)))
(check "class-of string" 'string (class-name (class-of "hi")))
(check "class-of nil" 'null (class-name (class-of nil)))
(check "class-of cons" 'cons (class-name (class-of '(1 2))))
(check "class-of eq find-class" t (eq (class-of 42) (find-class 'fixnum)))
(check "direct-supers integer" 'rational (class-name (car (class-direct-superclasses (find-class 'integer)))))
(check "direct-supers t" nil (class-direct-superclasses (find-class 't)))
(check "cpl fixnum" '(fixnum integer rational real number t) (mapcar #'class-name (class-precedence-list (find-class 'fixnum))))
(check "cpl cons" '(cons list sequence t) (mapcar #'class-name (class-precedence-list (find-class 'cons))))
;; NULL is a subclass of both SYMBOL and LIST; the CPL must keep T last so
;; that a LIST-specialized method outranks the default (T) method for NIL.
(check "cpl null" '(null symbol list sequence t) (mapcar #'class-name (class-precedence-list (find-class 'null))))
(check "cpl vector" '(vector array sequence t) (mapcar #'class-name (class-precedence-list (find-class 'vector))))
;; Dispatch regression: NIL is a LIST, so the LIST method must win over T.
(defgeneric %dnl (x))
(defmethod %dnl (x) "t")
(defmethod %dnl ((x list)) "list")
(check "dispatch nil as list" "list" (%dnl nil))
(check "dispatch list as list" "list" (%dnl '(1 2)))
(check "dispatch non-list as t" "t" (%dnl 5))
(check "direct-subclasses integer has fixnum" t (if (member (find-class 'fixnum) (class-direct-subclasses (find-class 'integer)) :test #'eq) t nil))
(check "direct-subclasses integer has bignum" t (if (member (find-class 'bignum) (class-direct-subclasses (find-class 'integer)) :test #'eq) t nil))

; --- CLOS Phase 2: C3 Linearization ---
(check "c3 single" '(fixnum integer rational real number t) (mapcar #'class-name (%compute-class-precedence-list (find-class 'fixnum))))
(check "c3 root" '(t) (mapcar #'class-name (%compute-class-precedence-list (find-class 't))))
(%make-bootstrap-class 'c3-a (list (find-class 't)))
(%make-bootstrap-class 'c3-b (list (find-class 'c3-a)))
(%make-bootstrap-class 'c3-c (list (find-class 'c3-a)))
(let ((d (%make-struct 'standard-class
           'c3-d
           (list (find-class 'c3-b) (find-class 'c3-c))
           nil nil nil nil nil nil nil t nil nil)))
  (setf (find-class 'c3-d) d)
  (%set-class-cpl d (%compute-class-precedence-list d)))
(check "c3 diamond" '(c3-d c3-b c3-c c3-a t) (mapcar #'class-name (class-precedence-list (find-class 'c3-d))))

; --- CLOS Phase 3: defclass ---
(defclass point () ((x :initarg :x :accessor point-x) (y :initarg :y :accessor point-y)))
(check "defclass find-class" 'point (class-name (find-class 'point)))
(check "defclass cpl" '(point standard-object t) (mapcar #'class-name (class-precedence-list (find-class 'point))))
(check "defclass effective slots" 2 (length (class-effective-slots (find-class 'point))))
(check "defclass slot-index x" 0 (slot-definition-location (gethash 'x (class-slot-index-table (find-class 'point)))))
(check "defclass slot-index y" 1 (slot-definition-location (gethash 'y (class-slot-index-table (find-class 'point)))))
(check "defclass accessor defined" t (functionp #'point-x))
(defclass point3d (point) ((z :initarg :z :accessor point-z)))
(check "defclass inheritance slots" 3 (length (class-effective-slots (find-class 'point3d))))
(check "defclass inheritance cpl" '(point3d point standard-object t) (mapcar #'class-name (class-precedence-list (find-class 'point3d))))
(defclass empty-class () ())
(check "defclass no supers defaults" '(standard-object) (mapcar #'class-name (class-direct-superclasses (find-class 'empty-class))))

; --- CLOS Phase 4: make-instance ---
(check "make-instance basic" '(10 20) (let ((p (make-instance 'point :x 10 :y 20))) (list (point-x p) (point-y p))))
(defclass counted () ((count :initform 0 :accessor obj-count) (name :initarg :name :accessor obj-name)))
(check "make-instance initform" '(0 "test") (let ((c (make-instance 'counted :name "test"))) (list (obj-count c) (obj-name c))))
(defclass counter () ((val :initarg :val :initform 0 :accessor counter-val)))
(check "initarg overrides initform" 42 (counter-val (make-instance 'counter :val 42)))
(check "initform default" 0 (counter-val (make-instance 'counter)))
(check "unbound slot" nil (let ((c (make-instance 'counted))) (slot-boundp c 'name)))
(check "make-instance by class" 1 (let ((p (make-instance (find-class 'point) :x 1 :y 2))) (point-x p)))
(check "make-instance inherited" '(1 2 3) (let ((p (make-instance 'point3d :x 1 :y 2 :z 3))) (list (point-x p) (point-y p) (point-z p))))

; --- UNBOUND-SLOT condition (CLHS 7.7.11) ---
; Reading an unbound slot signals UNBOUND-SLOT, a CELL-ERROR subtype carrying
; the slot name and the instance — not a bare SIMPLE-ERROR.
(defclass ubs-amiga () ((x :reader ubs-amiga-x) (y :initform 7)))
(check "unbound slot signals unbound-slot" :ok
       (handler-case (slot-value (make-instance 'ubs-amiga) 'x)
         (unbound-slot () :ok)))
(check "unbound-slot cell-error-name" 'x
       (handler-case (slot-value (make-instance 'ubs-amiga) 'x)
         (unbound-slot (c) (cell-error-name c))))
(check "unbound-slot-instance is the object" t
       (let ((o (make-instance 'ubs-amiga)))
         (handler-case (slot-value o 'x)
           (unbound-slot (c) (eq (unbound-slot-instance c) o)))))
(check "unbound-slot through reader gf" '(x t)
       (let ((o (make-instance 'ubs-amiga)))
         (handler-case (ubs-amiga-x o)
           (unbound-slot (c) (list (cell-error-name c)
                                   (eq (unbound-slot-instance c) o))))))
(check "unbound-slot after slot-makunbound" 'y
       (let ((o (make-instance 'ubs-amiga)))
         (slot-makunbound o 'y)
         (handler-case (slot-value o 'y) (unbound-slot (c) (cell-error-name c)))))
(check "unbound-slot is a cell-error" :cell
       (handler-case (slot-value (make-instance 'ubs-amiga) 'x)
         (cell-error () :cell)))
(check "unbound-slot subtypep cell-error" t (subtypep 'unbound-slot 'cell-error))
(check "unbound-slot report names the slot" t
       (handler-case (slot-value (make-instance 'ubs-amiga) 'x)
         (unbound-slot (c) (and (search "The slot X is unbound"
                                        (princ-to-string c)) t))))
(check "unbound-slot-instance accessor" 42
       (unbound-slot-instance (make-condition 'unbound-slot :name 'q :instance 42)))

; --- CLOS Phase 5: defgeneric + defmethod ---
(defgeneric greet (obj))
(defmethod greet ((p point)) (list 'point (point-x p) (point-y p)))
(check "defmethod basic" '(point 10 20) (greet (make-instance 'point :x 10 :y 20)))
(defmethod greet ((p point3d)) (list 'point3d (point-x p) (point-y p) (point-z p)))
(check "dispatch point3d" '(point3d 1 2 3) (greet (make-instance 'point3d :x 1 :y 2 :z 3)))
(check "dispatch point" '(point 5 6) (greet (make-instance 'point :x 5 :y 6)))
(defgeneric describe-obj (obj))
(defmethod describe-obj ((p point)) (list 'base-point (point-x p)))
(defmethod describe-obj ((p point3d)) (cons 'extended (call-next-method)))
(check "call-next-method" '(extended base-point 7) (describe-obj (make-instance 'point3d :x 7 :y 8 :z 9)))
(defparameter *trace* nil)
(defgeneric traced-op (obj))
(defmethod traced-op :before ((p point)) (push 'before *trace*))
(defmethod traced-op ((p point)) (push 'primary *trace*) 'done)
(defmethod traced-op :after ((p point)) (push 'after *trace*))
(setq *trace* nil)
(check "before/after result" 'done (traced-op (make-instance 'point :x 0 :y 0)))
(check "before/after order" '(before primary after) (nreverse *trace*))
(defgeneric wrapped-op (obj))
(defmethod wrapped-op ((p point)) (point-x p))
(defmethod wrapped-op :around ((p point)) (+ 100 (call-next-method)))
(check "around method" 105 (wrapped-op (make-instance 'point :x 5 :y 0)))
(defgeneric identity-gf (x))
(defmethod identity-gf (x) x)
(check "unspecialized fixnum" 42 (identity-gf 42))
(check "unspecialized string" "hello" (identity-gf "hello"))
(defgeneric nmp-test (x))
(defmethod nmp-test ((x point)) (if (next-method-p) 'has-next 'no-next))
(check "next-method-p" 'no-next (nmp-test (make-instance 'point :x 0 :y 0)))
; Regression: an auxiliary (:after) method must NOT see an ENCLOSING dispatch's
; call-next-method specials.  :after methods run raw, so the effective method
; must bind *call-next-method-function*/*next-method-p-function* to NIL around
; them; otherwise a non-conformant :after doing (when (next-method-p)
; (call-next-method)) leaks into the outer GF's chain whenever the GF is
; dispatched from inside another GF's method (real failure: a sento mailbox
; init :after jumped into hunchentoot's request handler on a worker thread).
(defparameter *cnm-leak* nil)
(defclass cnm-inner () ())
(defmethod initialize-instance :after ((o cnm-inner) &key)
  (when (next-method-p) (call-next-method)))
(defclass cnm-base () ())
(defclass cnm-derived (cnm-base) ())
(defgeneric cnm-ping (x))
(defmethod cnm-ping ((x cnm-base)) (setf *cnm-leak* t) :base)
(defmethod cnm-ping ((x cnm-derived)) (make-instance 'cnm-inner) :derived)
(check "aux-method cnm no-leak result" :derived (cnm-ping (make-instance 'cnm-derived)))
(check "aux-method cnm no-leak flag" nil *cnm-leak*)
; Regression: &aux in a specialized method lambda list whose init-form calls
; an accessor on a specialized arg must NOT be parsed as a specializer (it
; was handed to FIND-CLASS -> "No class named (...)").  Broke cl-routes/drakma.
(defgeneric aux-gf (a b))
(defmethod aux-gf ((a point) b &aux (px (point-x a))) (list px b))
(check "defmethod &aux specialized" '(7 9) (aux-gf (make-instance 'point :x 7 :y 0) 9))
; Regression: COND/AND/OR/CASE are compiler-inlined but per CLHS are MACROS;
; MACROEXPAND-1 must make progress so code-walkers (iterate) can descend.
(check "cond macroexpands to if" 'if (car (macroexpand-1 '(cond (a 1) (t 2)))))
(check "and macroexpands" '(if a (and b c)) (macroexpand-1 '(and a b c)))
(check "case macroexpands to let" 'let (car (macroexpand-1 '(case x (1 'a) (t 'b)))))
(check "macroexpand progresses" t (not (eq (macroexpand-1 '(or a b)) '(or a b))))

; --- CLOS Phase 6: with-slots ---
(check "with-slots read" '(10 20) (let ((p (make-instance 'point :x 10 :y 20))) (with-slots (x y) p (list x y))))
(check "with-slots write" 99 (let ((p (make-instance 'point :x 1 :y 2))) (with-slots (x) p (setf x 99)) (point-x p)))
(check "with-slots renamed" 11 (let ((p (make-instance 'point :x 5 :y 6))) (with-slots ((px x) (py y)) p (+ px py))))

; --- CLOS Phase 7: GF accessors ---
(defclass gf-point () ((x :initarg :x :accessor gf-point-x) (y :initarg :y :accessor gf-point-y)))
(check "gf accessor is gf" t (if (gethash 'gf-point-x *generic-function-table*) t nil))
(check "gf accessor read" '(10 20) (let ((p (make-instance 'gf-point :x 10 :y 20))) (list (gf-point-x p) (gf-point-y p))))
(check "gf accessor setf" 99 (let ((p (make-instance 'gf-point :x 1 :y 2))) (setf (gf-point-x p) 99) (gf-point-x p)))
(defparameter *init-log* nil)
(defmethod initialize-instance :after ((p gf-point) &rest initargs) (push 'gf-point-initialized *init-log*))
(setq *init-log* nil)
(make-instance 'gf-point :x 1 :y 2)
(check "gf initialize-instance :after" '(gf-point-initialized) *init-log*)

; --- CLOS Phase 8: change-class + reinitialize-instance ---
(check "reinitialize-instance" 99 (let ((p (make-instance 'gf-point :x 1 :y 2))) (reinitialize-instance p :x 99) (gf-point-x p)))
(defclass color-point (gf-point) ((color :initarg :color :initform :red :accessor point-color)))
(check "change-class shared slots" '(10 20 :red) (let* ((p (make-instance 'gf-point :x 10 :y 20)) (cp (change-class p 'color-point))) (list (gf-point-x cp) (gf-point-y cp) (point-color cp))))
(check "change-class with initargs" :blue (let* ((p (make-instance 'gf-point :x 1 :y 2)) (cp (change-class p 'color-point :color :blue))) (point-color cp)))

; --- Slot definition metaobjects (MOP) ---
(check "mop1 direct slot typep" t (typep (first (class-direct-slots (find-class 'point))) 'standard-direct-slot-definition))
(check "mop1 direct slot is slot-definition" t (typep (first (class-direct-slots (find-class 'point))) 'slot-definition))
(check "mop1 effective slot typep" t (typep (first (class-slots (find-class 'point))) 'standard-effective-slot-definition))
(check "mop1 class-slots length" 2 (length (class-slots (find-class 'point))))
(check "mop1 slot-def name" 'x (slot-definition-name (first (class-direct-slots (find-class 'point)))))
(check "mop1 slot-def initargs" '(:x) (slot-definition-initargs (first (class-direct-slots (find-class 'point)))))
(check "mop1 slot-def readers" '(point-x) (slot-definition-readers (first (class-direct-slots (find-class 'point)))))
(check "mop1 slot-def writers" '((setf point-x)) (slot-definition-writers (first (class-direct-slots (find-class 'point)))))
(check "mop1 slot-def allocation" :instance (slot-definition-allocation (first (class-direct-slots (find-class 'point)))))
(check "mop1 effective-slot location 0" 0 (slot-definition-location (first (class-slots (find-class 'point)))))
(check "mop1 effective-slot location 1" 1 (slot-definition-location (second (class-slots (find-class 'point)))))
(defclass mop1-amiga-ifm () ((n :initarg :n :initform 42)))
(check "mop1 slot-def initform" 42 (slot-definition-initform (first (class-direct-slots (find-class 'mop1-amiga-ifm)))))
(check "mop1 slot-def initfunction call" 42 (funcall (slot-definition-initfunction (first (class-direct-slots (find-class 'mop1-amiga-ifm))))))
(check "mop1 make-instance uses initform" 42 (slot-value (make-instance 'mop1-amiga-ifm) 'n))
(check "mop1 direct-slot-definition-class" 'standard-direct-slot-definition (class-name (direct-slot-definition-class (find-class 'standard-class))))
(check "mop1 effective-slot-definition-class" 'standard-effective-slot-definition (class-name (effective-slot-definition-class (find-class 'standard-class))))
(defclass mop1-amiga-p1 () ((x :initarg :a)))
(defclass mop1-amiga-p2 (mop1-amiga-p1) ((x :initarg :b)))
(check "mop1 inherited initarg :a" 1 (slot-value (make-instance 'mop1-amiga-p2 :a 1) 'x))
(check "mop1 inherited initarg :b" 2 (slot-value (make-instance 'mop1-amiga-p2 :b 2) 'x))

; --- Class finalization protocol (MOP) ---
(defclass cf-amiga-f1 () ((x :initarg :x)))
(check "class-finalized-p new class" t (class-finalized-p (find-class 'cf-amiga-f1)))
(check "class-finalized-p bootstrap" t (class-finalized-p (find-class 'standard-object)))
(check "finalize-inheritance idempotent" t (progn (finalize-inheritance (find-class 'cf-amiga-f1)) (finalize-inheritance (find-class 'cf-amiga-f1)) (class-finalized-p (find-class 'cf-amiga-f1))))
(defclass cf-amiga-a () ())
(defclass cf-amiga-b (cf-amiga-a) ())
(check "compute-class-precedence-list" '(cf-amiga-b cf-amiga-a standard-object t) (mapcar #'class-name (compute-class-precedence-list (find-class 'cf-amiga-b))))
(defclass cf-amiga-cs () ((a :initarg :a) (b :initarg :b)))
(check "compute-slots length" 2 (length (compute-slots (find-class 'cf-amiga-cs))))
(check "compute-slots names" '(a b) (mapcar #'slot-definition-name (compute-slots (find-class 'cf-amiga-cs))))
(check "compute-effective-slot-definition name" 'a (slot-definition-name (compute-effective-slot-definition (find-class 'cf-amiga-cs) 'a (list (first (class-direct-slots (find-class 'cf-amiga-cs)))))))
(defclass cf-amiga-di-base () ((a :initarg :a)) (:default-initargs :a 99))
(defclass cf-amiga-di-sub (cf-amiga-di-base) ())
(check "default-initargs inherited" 99 (slot-value (make-instance 'cf-amiga-di-sub) 'a))
(defclass cf-amiga-di-p () ((a :initarg :a)) (:default-initargs :a 'parent))
(defclass cf-amiga-di-c (cf-amiga-di-p) () (:default-initargs :a 'child))
(check "default-initargs most-specific wins" 'child (slot-value (make-instance 'cf-amiga-di-c) 'a))
(check "validate-superclass T" t (validate-superclass (find-class 'standard-class) (find-class 'standard-object)))
(defclass cf-amiga-ddi () ((x :initarg :x)) (:default-initargs :x 42))
(check "class-direct-default-initargs key" :x (first (first (class-direct-default-initargs (find-class 'cf-amiga-ddi)))))
(check "class-direct-default-initargs value" 42 (funcall (second (first (class-direct-default-initargs (find-class 'cf-amiga-ddi))))))
(defclass cf-amiga-proto () ((x :initarg :x)))
(check "class-prototype typep" t (typep (class-prototype (find-class 'cf-amiga-proto)) 'cf-amiga-proto))
(check "class-prototype cached" t (eq (class-prototype (find-class 'cf-amiga-proto)) (class-prototype (find-class 'cf-amiga-proto))))
(ensure-class 'cf-amiga-ec :direct-superclasses '() :direct-slots (list (%make-direct-slot-def 'v '(:v) nil nil nil :instance nil nil nil)) :direct-default-initargs '())
(check "ensure-class creates" 'cf-amiga-ec (class-name (find-class 'cf-amiga-ec)))
(check "ensure-class slot count" 1 (length (class-slots (find-class 'cf-amiga-ec))))

; --- Fused fast-path slot access (spec 3.1, second half) ---
;; These run while *SLOT-ACCESS-PROTOCOL-EXTENDED-P* is still NIL, so
;; SLOT-VALUE and the DEFCLASS-generated accessors take the fused
;; %STRUCT-SLOT-VALUE / %STRUCT-SLOT-STORE builtins and the GF
;; discriminators hit the %GF-IC-EMF inline-cache probe.
(defvar *fsv-miss* (make-symbol "MISS"))
(defstruct fsv-amiga-st (a 10) (b 20))
(check "fused read hit" 20
       (clamiga::%struct-slot-value (make-fsv-amiga-st) 'b *fsv-miss*))
(check "fused read no-such-slot miss" t
       (eq (clamiga::%struct-slot-value (make-fsv-amiga-st) 'nope *fsv-miss*) *fsv-miss*))
(check "fused read non-struct miss" t
       (eq (clamiga::%struct-slot-value 42 'a *fsv-miss*) *fsv-miss*))
(let ((s (make-fsv-amiga-st)))
  (check "fused store hit" t (clamiga::%struct-slot-store s 'a 99))
  (check "fused store took effect" 99 (fsv-amiga-st-a s))
  (check "fused store no-such-slot miss" nil (clamiga::%struct-slot-store s 'nope 1))
  (check "fused store non-struct miss" nil (clamiga::%struct-slot-store 42 'a 1)))
(defclass fsv-amiga-base ()
  ((x :initarg :x :accessor fsv-amiga-x)
   (u :accessor fsv-amiga-u)
   (c :allocation :class :initform 5 :accessor fsv-amiga-c)))
(defclass fsv-amiga-sub (fsv-amiga-base)
  ((z :initform 100 :accessor fsv-amiga-z)))
(let ((b (make-instance 'fsv-amiga-base :x 3))
      (s (make-instance 'fsv-amiga-sub :x 30)))
  (check "accessor read" 3 (fsv-amiga-x b))
  (setf (fsv-amiga-x b) 4)
  (check "accessor write" 4 (fsv-amiga-x b))
  ;; Subclass instance through the superclass accessor: Z sits before
  ;; the inherited X in FSV-AMIGA-SUB's layout, so a captured/stale
  ;; index would read the wrong slot here.
  (check "accessor on subclass shifted index" 30 (fsv-amiga-x s))
  (check "accessor subclass own slot" 100 (fsv-amiga-z s))
  ;; Unbound slot: fast path reads the marker -> slow path signals.
  (check "accessor unbound signals" :fsv-unbound
         (handler-case (fsv-amiga-u b) (unbound-slot () :fsv-unbound)))
  (check "slot-boundp unbound via fast front" nil (slot-boundp b 'u))
  (check "slot-boundp bound via fast front" t (slot-boundp b 'x))
  ;; :CLASS slot is not in the struct registry -> accessor falls back
  ;; to the full protocol and reads/writes the shared cons cell.
  (check "accessor :class slot read" 5 (fsv-amiga-c b))
  (setf (fsv-amiga-c b) 6)
  (check "accessor :class slot shared" 6 (fsv-amiga-c s)))
;; GF inline-cache probe: cold before any dispatch, populated by the
;; first dispatch, misses on a receiver of another class, never errors
;; on junk input.
(defstruct fsv-amiga-probe-st (v 7))
(defgeneric fsv-amiga-probe (x))
(defmethod fsv-amiga-probe ((x fsv-amiga-probe-st)) (fsv-amiga-probe-st-v x))
(check "gf-ic-emf cold cache miss" nil
       (functionp (clamiga::%gf-ic-emf (gethash 'fsv-amiga-probe *generic-function-table*)
                                       (make-fsv-amiga-probe-st))))
(check "gf dispatch through probe path" 7 (fsv-amiga-probe (make-fsv-amiga-probe-st)))
(check "gf-ic-emf populated after dispatch" t
       (functionp (clamiga::%gf-ic-emf (gethash 'fsv-amiga-probe *generic-function-table*)
                                       (make-fsv-amiga-probe-st))))
(check "gf-ic-emf wrong class miss" nil
       (clamiga::%gf-ic-emf (gethash 'fsv-amiga-probe *generic-function-table*) 42))
(check "gf-ic-emf non-gf miss" nil (clamiga::%gf-ic-emf 42 42))

; --- Reader-GF fast dispatch ---
; A GF whose whole method set is DEFCLASS-generated readers for one slot is
; "promoted": its inline cache holds up to 4 (TYPE-NAME . SLOT-INDEX) entries
; (polymorphic, so alternating receiver classes hit too) and OP_CALL answers
; the call without unwrapping to the discriminating function.
;
; These MUST stay above the "Slot-access protocol" section below: the
; DEFMETHOD SLOT-VALUE-USING-CLASS there latches
; *SLOT-ACCESS-PROTOCOL-EXTENDED-P* for the rest of the image, which demotes
; every reader GF permanently.  Run after it and each check below would pass
; vacuously on the slow path -- so each one first asserts the GF is promoted.
;
; On Amiga the m68k JIT does not route through the bytecode OP_CALL; JIT'd
; callers reach cl_jit_runtime_call -> cl_vm_apply, whose reader-IC probe
; answers the call before the GF is unwrapped to the discriminating function
; (FUNCALL and APPLY probe the same way in their builtins).  Every defun'd
; caller below is JIT-compiled on Amiga, so these checks exercise that probe
; end-to-end; direct top-level calls exercise the interpreter's OP_CALL probe.
; All paths must agree.
(defclass rg-amiga-a () ((x :initform 3 :reader rg-amiga-x)
                         (n :initform nil :reader rg-amiga-n)
                         (u :reader rg-amiga-u)))
(defclass rg-amiga-b () ((pad :initform 0) (x :initform 30 :reader rg-amiga-x)))
(defclass rg-amiga-c (rg-amiga-a) ((z :initform 100)))
(check "reader GF promoted" t (and (gethash #'rg-amiga-x clamiga:*reader-gfs*) t))
(check "reader on base" 3 (rg-amiga-x (make-instance 'rg-amiga-a)))
(check "reader on other class, different index" 30
       (rg-amiga-x (make-instance 'rg-amiga-b)))
(check "reader on subclass" 3 (rg-amiga-x (make-instance 'rg-amiga-c)))
(check "polymorphic alternation stays correct" '(3 30 3 30)
       (let ((a (make-instance 'rg-amiga-a)) (b (make-instance 'rg-amiga-b)))
         (list (rg-amiga-x a) (rg-amiga-x b) (rg-amiga-x a) (rg-amiga-x b))))
; the IC caches one entry per receiver class; alternating calls are all hits,
; and a hit never rewrites slot 8 — the spine staying EQ proves it
(check "polymorphic IC caches both classes and hits" '(t t)
       (let ((a (make-instance 'rg-amiga-a)) (b (make-instance 'rg-amiga-b)))
         (rg-amiga-x a) (rg-amiga-x b)
         (let ((ic (clamiga::gf-inline-cache #'rg-amiga-x)))
           (dotimes (i 5) (rg-amiga-x a) (rg-amiga-x b))
           (list (>= (length ic) 2)
                 (eq ic (clamiga::gf-inline-cache #'rg-amiga-x))))))
; five receiver classes exceed the 4-entry cap: oldest evicted, values correct
(defclass rg-amiga-e1 () ((x :initform 1 :reader rg-amiga-ex)))
(defclass rg-amiga-e2 () ((x :initform 2 :reader rg-amiga-ex)))
(defclass rg-amiga-e3 () ((x :initform 3 :reader rg-amiga-ex)))
(defclass rg-amiga-e4 () ((x :initform 4 :reader rg-amiga-ex)))
(defclass rg-amiga-e5 () ((pad :initform 0) (x :initform 5 :reader rg-amiga-ex)))
(check "polymorphic IC eviction cap" '(4 (1 2 3 4 5 1 2 3 4 5))
       (let ((os (list (make-instance 'rg-amiga-e1) (make-instance 'rg-amiga-e2)
                       (make-instance 'rg-amiga-e3) (make-instance 'rg-amiga-e4)
                       (make-instance 'rg-amiga-e5)))
             (acc nil) (maxlen 0))
         (dotimes (i 2)
           (dolist (o os)
             (push (rg-amiga-ex o) acc)
             (setq maxlen (max maxlen
                               (length (clamiga::gf-inline-cache #'rg-amiga-ex))))))
         (list maxlen (nreverse acc))))
(check "NIL-valued slot is not the unbound marker" nil
       (rg-amiga-n (make-instance 'rg-amiga-a)))
(check "unbound slot signals through reader" '(u t)
       (let ((o (make-instance 'rg-amiga-a)))
         (handler-case (progn (rg-amiga-u o) "NO-ERROR")
           (unbound-slot (c) (list (cell-error-name c)
                                   (eq (unbound-slot-instance c) o))))))
(check "unbound slot signals again (no bad cache hit)" '(u t)
       (let ((o (make-instance 'rg-amiga-a)))
         (handler-case (progn (rg-amiga-u o) "NO-ERROR")
           (unbound-slot (c) (list (cell-error-name c)
                                   (eq (unbound-slot-instance c) o))))))
; tail position (OP_TAILCALL, excluded from the VM fast path; the JIT's
; tailcall fallback funnels through cl_vm_apply's probe) and FUNCALL/APPLY
; (probed in their builtins) must agree with the direct call
(defun rg-amiga-tail (o) (rg-amiga-x o))
(check "tail-position reader" 3 (rg-amiga-tail (make-instance 'rg-amiga-a)))
(check "funcall reader" 3 (funcall #'rg-amiga-x (make-instance 'rg-amiga-a)))
(check "apply reader" 3 (apply #'rg-amiga-x (list (make-instance 'rg-amiga-a))))
; a probe hit must leave exactly one value
(check "funcall reader single value" '(3)
       (multiple-value-list (funcall #'rg-amiga-x (make-instance 'rg-amiga-a))))
; unbound slot is a probe miss on every trampoline: SLOT-UNBOUND must signal
(check "unbound slot signals through funcall" 'u
       (handler-case (funcall #'rg-amiga-u (make-instance 'rg-amiga-a))
         (unbound-slot (c) (cell-error-name c))))
; JIT'd caller, non-tail position: on Amiga this is the
; cl_jit_runtime_call -> cl_vm_apply probe end-to-end.  (This cannot live in
; test-jit.lisp — that file loads AFTER the slot-access-protocol latch below,
; where every reader GF is already demoted.)
(defun rg-amiga-mid (o) (let ((v (rg-amiga-x o))) (+ v 1)))
(check "JIT'd caller non-tail reader" 4 (rg-amiga-mid (make-instance 'rg-amiga-a)))
; prove the caller genuinely executes as native code where the JIT is active
; (vacuous under --no-jit and on host, where the check below is skipped)
(check "reader-calling defun runs native" t
       (if (clamiga::%jit-active-p)
           (let ((before (clamiga::%jit-invoke-count)))
             (rg-amiga-mid (make-instance 'rg-amiga-a))
             (> (clamiga::%jit-invoke-count) before))
           t))
; a hit never rewrites slot 8: the spine staying EQ across JIT'd-caller,
; funcall, and apply calls proves the probes answered them
(check "trampoline probes hit, spine stays EQ" '(t (3 30))
       (let ((a (make-instance 'rg-amiga-a)) (b (make-instance 'rg-amiga-b)))
         (rg-amiga-x a) (rg-amiga-x b)
         (let ((ic (clamiga::gf-inline-cache #'rg-amiga-x)) (acc nil))
           (dotimes (i 5)
             (rg-amiga-mid a) (rg-amiga-tail b)
             (push (funcall #'rg-amiga-x a) acc)
             (push (apply #'rg-amiga-x (list b)) acc))
           (list (eq ic (clamiga::gf-inline-cache #'rg-amiga-x))
                 (remove-duplicates (nreverse acc))))))
; accessor writer round-trip
(defclass rg-amiga-w () ((x :initform 1 :accessor rg-amiga-wx)))
(check "accessor write then read" 42
       (let ((o (make-instance 'rg-amiga-w))) (setf (rg-amiga-wx o) 42) (rg-amiga-wx o)))
; an :around method retracts the fast path
(defclass rg-amiga-s () ((x :initform 10 :reader rg-amiga-sx)))
(check "sx fast before around" 10 (rg-amiga-sx (make-instance 'rg-amiga-s)))
(defmethod rg-amiga-sx :around ((o rg-amiga-s)) (* 100 (call-next-method)))
(check "around demotes reader GF" nil (and (gethash #'rg-amiga-sx clamiga:*reader-gfs*) t))
(check "around runs" 1000 (rg-amiga-sx (make-instance 'rg-amiga-s)))
; demotion must disarm the trampoline probes too
(check "around runs through funcall" 1000
       (funcall #'rg-amiga-sx (make-instance 'rg-amiga-s)))
(check "around runs through apply" 1000
       (apply #'rg-amiga-sx (list (make-instance 'rg-amiga-s))))
(defun rg-amiga-around-mid (o) (rg-amiga-sx o))
(check "around runs through JIT'd caller" 1000
       (rg-amiga-around-mid (make-instance 'rg-amiga-s)))
; class redefinition moves the slot; the cached index must be invalidated
(defclass rg-amiga-redef () ((a :initform 'one :reader rg-amiga-ra)))
(check "redef before" 'one (rg-amiga-ra (make-instance 'rg-amiga-redef)))
(defclass rg-amiga-redef () ((pad :initform 0) (a :initform 'two :reader rg-amiga-ra)))
(check "redef after slot reorder" 'two (rg-amiga-ra (make-instance 'rg-amiga-redef)))
; SET-FUNCALLABLE-INSTANCE-FUNCTION must beat the VM's slot-3 bypass
(defclass rg-amiga-sfi () ((x :initform 'orig :reader rg-amiga-sfix)))
(check "sfix fast" 'orig (rg-amiga-sfix (make-instance 'rg-amiga-sfi)))
(set-funcallable-instance-function #'rg-amiga-sfix (lambda (o) (declare (ignore o)) 'hijacked))
(check "set-funcallable demotes" nil (and (gethash #'rg-amiga-sfix clamiga:*reader-gfs*) t))
(check "set-funcallable-instance-function wins" 'hijacked
       (rg-amiga-sfix (make-instance 'rg-amiga-sfi)))
(check "set-funcallable-instance-function wins via funcall" 'hijacked
       (funcall #'rg-amiga-sfix (make-instance 'rg-amiga-sfi)))
(check "unrelated reader still fast" 3 (rg-amiga-x (make-instance 'rg-amiga-a)))

; --- Writer-GF fast dispatch ---
; The mirror machinery for (setf (x obj) v): a GF whose whole method set is
; DEFCLASS-generated writer methods for one slot is promoted, its IC entries
; encode the slot index negative ((- -1 idx)), and the VM's 2-arg probes
; (OP_CALL, cl_vm_apply — the JIT path — FUNCALL/APPLY builtins, OP_APPLY)
; store straight through the cached index.  Same latch caveat as the reader
; section: these MUST stay above "Slot-access protocol" below.
(defclass wg-amiga-a () ((x :initform 0 :accessor wg-amiga-x)
                         (guard :initform :g)))
(defclass wg-amiga-b () ((pad :initform 0) (x :initform 0 :accessor wg-amiga-x)))
(check "writer GF promoted" t (and (gethash #'(setf wg-amiga-x) clamiga:*writer-gfs*) t))
(check "writer stores and setf returns value" '(42 42)
       (let ((o (make-instance 'wg-amiga-a)))
         (list (setf (wg-amiga-x o) 42) (wg-amiga-x o))))
(check "writer neighbour slot untouched" :g
       (let ((o (make-instance 'wg-amiga-a)))
         (setf (wg-amiga-x o) 9) (slot-value o 'guard)))
; alternating classes with X at DIFFERENT indices: one negative-encoded IC
; entry per class; a hit never rewrites slot 8, so the spine staying EQ
; proves the fast path performed the stores
(check "writer polymorphic alternation, spine stays EQ" '(t 4 40)
       (let ((a (make-instance 'wg-amiga-a)) (b (make-instance 'wg-amiga-b)))
         (setf (wg-amiga-x a) 0) (setf (wg-amiga-x b) 0)
         (let ((ic (clamiga::gf-inline-cache #'(setf wg-amiga-x))))
           (dotimes (i 5) (setf (wg-amiga-x a) i) (setf (wg-amiga-x b) (* 10 i)))
           (list (eq ic (clamiga::gf-inline-cache #'(setf wg-amiga-x)))
                 (wg-amiga-x a) (wg-amiga-x b)))))
; :writer option names the GF directly; it takes (VAL OBJ)
(defclass wg-amiga-wo () ((x :initform 0 :writer wg-amiga-set-x :reader wg-amiga-wox)))
(check "writer via :writer option promoted" t
       (and (gethash #'wg-amiga-set-x clamiga:*writer-gfs*) t))
(check ":writer stores" '(7 7)
       (let ((o (make-instance 'wg-amiga-wo)))
         (list (wg-amiga-set-x 7 o) (wg-amiga-wox o))))
; a 1-arg call to a promoted writer GF must signal, never read the slot
; (this is what the negative index encoding guarantees)
(check "writer arity error, not slot read" :err
       (handler-case (funcall #'(setf wg-amiga-x) (make-instance 'wg-amiga-a))
         (error () :err)))
; writing an unbound slot binds it
(defclass wg-amiga-u () ((x :accessor wg-amiga-ux)))
(check "write binds unbound slot" '(nil :now)
       (let ((o (make-instance 'wg-amiga-u)))
         (list (slot-boundp o 'x)
               (progn (setf (wg-amiga-ux o) :now) (wg-amiga-ux o)))))
; trampolines: FUNCALL/APPLY builtins and, in JIT'd callers, cl_vm_apply
(check "funcall writer" 5
       (let ((o (make-instance 'wg-amiga-a)))
         (funcall #'(setf wg-amiga-x) 5 o) (wg-amiga-x o)))
(check "funcall writer single value" '(6)
       (multiple-value-list (funcall #'(setf wg-amiga-x) 6 (make-instance 'wg-amiga-a))))
(check "apply writer" 7
       (let ((o (make-instance 'wg-amiga-a)))
         (apply #'(setf wg-amiga-x) 7 (list o)) (wg-amiga-x o)))
; JIT'd caller, non-tail position: on Amiga this is the
; cl_jit_runtime_call -> cl_vm_apply writer probe end-to-end
(defun wg-amiga-mid (v o) (setf (wg-amiga-x o) v) (wg-amiga-x o))
(check "JIT'd caller writer" 11 (wg-amiga-mid 11 (make-instance 'wg-amiga-a)))
(check "writer-calling defun runs native" t
       (if (clamiga::%jit-active-p)
           (let ((before (clamiga::%jit-invoke-count)))
             (wg-amiga-mid 12 (make-instance 'wg-amiga-a))
             (> (clamiga::%jit-invoke-count) before))
           t))
; an :after method on the setter retracts the fast path (a probe hit would
; silently skip it) — the sento-actor pattern
(defclass wg-amiga-s () ((x :initform 0 :accessor wg-amiga-sx)
                         (log :initform nil)))
(check "sx write fast before after-method" 1
       (let ((o (make-instance 'wg-amiga-s))) (setf (wg-amiga-sx o) 1) (wg-amiga-sx o)))
(defmethod (setf wg-amiga-sx) :after (val (o wg-amiga-s))
  (push val (slot-value o 'log)))
(check "after-method demotes writer GF" nil
       (and (gethash #'(setf wg-amiga-sx) clamiga:*writer-gfs*) t))
(check "after-method runs on write" '(8 (8 7))
       (let ((o (make-instance 'wg-amiga-s)))
         (setf (wg-amiga-sx o) 7)
         (funcall #'(setf wg-amiga-sx) 8 o)     ; trampolines disarmed too
         (list (wg-amiga-sx o) (slot-value o 'log))))
; class redefinition moves the slot; the cached index must be invalidated
(defclass wg-amiga-redef () ((a :initform :one :accessor wg-amiga-ra)))
(check "writer redef before" :warm
       (let ((o (make-instance 'wg-amiga-redef)))
         (setf (wg-amiga-ra o) :warm) (wg-amiga-ra o)))
(defclass wg-amiga-redef () ((pad :initform :pad)
                             (a :initform :two :accessor wg-amiga-ra)))
(check "writer redef after slot reorder" '(:new :pad)
       (let ((o (make-instance 'wg-amiga-redef)))
         (setf (wg-amiga-ra o) :new)
         (list (wg-amiga-ra o) (slot-value o 'pad))))
; SET-FUNCALLABLE-INSTANCE-FUNCTION must beat the VM's slot-3 bypass
(defclass wg-amiga-sfi () ((x :initform 0 :accessor wg-amiga-sfix)))
(check "sfix write fast" 1
       (let ((o (make-instance 'wg-amiga-sfi)))
         (setf (wg-amiga-sfix o) 1) (wg-amiga-sfix o)))
(set-funcallable-instance-function
  #'(setf wg-amiga-sfix) (lambda (v o) (declare (ignore v o)) 'hijacked))
(check "set-funcallable demotes writer" nil
       (and (gethash #'(setf wg-amiga-sfix) clamiga:*writer-gfs*) t))
(check "set-funcallable wins for writer" 'hijacked
       (funcall #'(setf wg-amiga-sfix) 9 (make-instance 'wg-amiga-sfi)))
(check "hijacked writer does not store" 0
       (let ((o (make-instance 'wg-amiga-sfi)))
         (setf (wg-amiga-sfix o) 9) (wg-amiga-sfix o)))
(check "unrelated writer still fast" 13
       (let ((o (make-instance 'wg-amiga-a)))
         (setf (wg-amiga-x o) 13) (wg-amiga-x o)))

; --- SLOT-VALUE compiler-macro inline + (type, slot) pair index ---
; SLOT-VALUE and (SETF (SLOT-VALUE ...)) compile to an inlined fast-path
; test (compiler macros in clos.lisp; compile_setf routes defsetf updaters
; through compile_call), and struct_slot_resolve answers through the pair
; index built alongside the struct type index.  Same latch caveat as the
; reader-GF section: these MUST stay above "Slot-access protocol" below.
(defclass sv-amiga-i () ((x :initform 1) (y :initform 2)))
(defvar *sv-amiga-log* nil)
(defvar *sv-amiga-inst* (make-instance 'sv-amiga-i))
; place subforms evaluate once each, left-to-right, before the value
(check "inlined setf slot-value eval order" '((:obj :name :val) 5)
       (progn (setq *sv-amiga-log* nil)
              (setf (slot-value (progn (push :obj *sv-amiga-log*) *sv-amiga-inst*)
                                (progn (push :name *sv-amiga-log*) 'x))
                    (progn (push :val *sv-amiga-log*) 5))
              (list (nreverse *sv-amiga-log*) (slot-value *sv-amiga-inst* 'x))))
(check "inlined setf returns the value" 11 (setf (slot-value *sv-amiga-inst* 'x) 11))
(check "funcall #'slot-value agrees" 11 (funcall #'slot-value *sv-amiga-inst* 'x))
; non-constant slot-name goes through the macro's gensym-bound path
(check "variable slot-name" '(11 2)
       (mapcar (lambda (n) (slot-value *sv-amiga-inst* n)) '(x y)))
; every slot of a wide class resolves through the pair index
(defclass sv-amiga-wide ()
  ((s0 :initform 0) (s1 :initform 10) (s2 :initform 20) (s3 :initform 30)
   (s4 :initform 40) (s5 :initform 50) (s6 :initform 60) (s7 :initform 70)
   (s8 :initform 80) (s9 :initform 90) (s10 :initform 100) (s11 :initform 110)))
(check "wide class deep slots via pair index" '(0 50 110)
       (let ((p (make-instance 'sv-amiga-wide)))
         (list (slot-value p 's0) (slot-value p 's5) (slot-value p 's11))))
; redefinition must invalidate the pair index, not read a stale position
(defclass sv-amiga-redef () ((a :initform 1) (b :initform 2)))
(check "pair index before redef" 2 (slot-value (make-instance 'sv-amiga-redef) 'b))
(defclass sv-amiga-redef () ((pad1 :initform 0) (pad2 :initform 0) (b :initform 9)))
(check "pair index after redef" 9 (slot-value (make-instance 'sv-amiga-redef) 'b))
; unbound slot routes to the SLOT-UNBOUND protocol through the inline
(defclass sv-amiga-ub () ((x)))
(check "inlined slot-value unbound-slot" '(x t)
       (let ((o (make-instance 'sv-amiga-ub)))
         (handler-case (progn (slot-value o 'x) "NO-ERROR")
           (unbound-slot (c) (list (cell-error-name c)
                                   (eq (unbound-slot-instance c) o))))))

; --- Slot-access protocol (MOP) ---
(defclass sv-amiga-fast () ((x :initarg :x :initform 10)))
(check "protocol flag clean" nil *slot-access-protocol-extended-p*)
(check "fast path slot-value" 7 (slot-value (make-instance 'sv-amiga-fast :x 7) 'x))
(defclass sv-amiga-default () ((n :initarg :n)))
(check "default svuc = slot-value" 42 (let ((inst (make-instance 'sv-amiga-default :n 42))) (slot-value-using-class (class-of inst) inst (first (class-slots (class-of inst))))))
(check "slot-definition-location 0" 0 (slot-definition-location (first (class-slots (find-class 'sv-amiga-default)))))
(defclass sv-amiga-loc2 () ((a :initarg :a) (b :initarg :b)))
(check "slot-definition-location 1" 1 (slot-definition-location (second (class-slots (find-class 'sv-amiga-loc2)))))
(defclass sv-amiga-setf () ((z :initarg :z :initform 1)))
(let ((inst (make-instance 'sv-amiga-setf)))
  (setf (slot-value-using-class (class-of inst) inst (first (class-slots (class-of inst)))) 77)
  (check "setf svuc default round-trip" 77 (slot-value inst 'z)))
(defclass sv-amiga-obs () ((k :initarg :k :initform 'orig)))
(defvar *sv-amiga-log* nil)
(defmethod slot-value-using-class :around ((class t) (inst sv-amiga-obs) slot)
  (push (slot-definition-name slot) *sv-amiga-log*)
  (call-next-method))
(check "protocol flag extended" t *slot-access-protocol-extended-p*)
; the latch demotes every promoted reader AND writer GF (see the reader-GF /
; writer-GF sections above, which ran while the flag was still NIL)
(check "latch demoted reader GFs" nil (and (gethash #'rg-amiga-x clamiga:*reader-gfs*) t))
(check "latch demoted writer GFs" nil
       (and (gethash #'(setf wg-amiga-x) clamiga:*writer-gfs*) t))
(check "demoted writer still stores" 21
       (let ((o (make-instance 'wg-amiga-a)))
         (setf (wg-amiga-x o) 21) (wg-amiga-x o)))
(check "around read returns orig" 'orig (slot-value (make-instance 'sv-amiga-obs) 'k))
(check "around read logged" 'k (first *sv-amiga-log*))
(defclass sv-amiga-mod () ((v :initarg :v :initform 1)))
(defmethod slot-value-using-class :around ((class t) (inst sv-amiga-mod) slot)
  (declare (ignore slot))
  (* 10 (call-next-method)))
(check "around modifies read" 30 (slot-value (make-instance 'sv-amiga-mod :v 3) 'v))
;; DEFCLASS-generated accessors must also honor the extended protocol:
;; once the flag is set they bypass the fused fast path entirely, so
;; SLOT-VALUE-USING-CLASS :around methods intercept accessor reads and
;; writes too.
(defclass sv-amiga-mod-acc2 () ((q :initarg :q :accessor sv-amiga-mod-q)))
(defmethod slot-value-using-class :around ((class t) (inst sv-amiga-mod-acc2) slot)
  (declare (ignore slot))
  (* 100 (call-next-method)))
(check "accessor honors extended protocol" 700
       (sv-amiga-mod-q (make-instance 'sv-amiga-mod-acc2 :q 7)))
(defvar *sv-amiga-acc-writes* 0)
(defmethod (setf slot-value-using-class) :around (nv (class t) (inst sv-amiga-mod-acc2) slot)
  (declare (ignore nv slot))
  (setq *sv-amiga-acc-writes* (+ *sv-amiga-acc-writes* 1))
  (call-next-method))
(let ((inst (make-instance 'sv-amiga-mod-acc2 :q 0)))
  (setf (sv-amiga-mod-q inst) 5)
  (check "setf accessor routes through protocol" t (> *sv-amiga-acc-writes* 0))
  (check "setf accessor stored via protocol" 500 (sv-amiga-mod-q inst)))
(defclass sv-amiga-set () ((w :initarg :w :initform 0)))
(defvar *sv-amiga-writes* nil)
(defmethod (setf slot-value-using-class) :around (new-value (class t) (inst sv-amiga-set) slot)
  (push (cons (slot-definition-name slot) new-value) *sv-amiga-writes*)
  (call-next-method))
(let ((inst (make-instance 'sv-amiga-set))) (setf (slot-value inst 'w) 99) (check "setf-svuc around value" 99 (slot-value inst 'w)))
(check "setf-svuc around logged new-value" 99 (cdr (first *sv-amiga-writes*)))
(check "setf-svuc around logged slot" 'w (car (first *sv-amiga-writes*)))
(defclass sv-amiga-boundp () ((q :initarg :q)))
(defvar *sv-amiga-saw-boundp* nil)
(defmethod slot-boundp-using-class :around ((class t) (inst sv-amiga-boundp) slot)
  (declare (ignore slot))
  (setq *sv-amiga-saw-boundp* t)
  (call-next-method))
(check "boundp via protocol bound" t (slot-boundp (make-instance 'sv-amiga-boundp :q 1) 'q))
(check "boundp around fired" t *sv-amiga-saw-boundp*)
(check "boundp via protocol unbound" nil (slot-boundp (make-instance 'sv-amiga-boundp) 'q))
(defclass sv-amiga-mu () ((r :initarg :r :initform 5)))
(defvar *sv-amiga-cleared* nil)
(defmethod slot-makunbound-using-class :around ((class t) (inst sv-amiga-mu) slot)
  (push (slot-definition-name slot) *sv-amiga-cleared*)
  (call-next-method))
(let ((inst (make-instance 'sv-amiga-mu))) (slot-makunbound inst 'r) (check "makunbound via protocol" nil (slot-boundp inst 'r)))
(check "makunbound around fired" 'r (first *sv-amiga-cleared*))
(defstruct sv-amiga-struct a b)
(let ((s (make-sv-amiga-struct :a 100 :b 200)))
  (check "struct fallback a" 100 (slot-value s 'a))
  (check "struct fallback b" 200 (slot-value s 'b)))

; --- struct registry hash index + %STRUCT-SLOT-INDEX (spec 3.1) ---
;; The registry is prepended on registration; bury a type behind later
;; registrations so lookups must resolve a non-head entry, then verify
;; the full slot-value protocol and that the index survives compaction.
(defstruct sidx-amiga (x 3) (y 4))
(dotimes (i 60)
  (clamiga::%register-struct-type
   (make-symbol (format nil "SIDX-AMIGA-FILLER-~D" i)) 1 nil '((f nil))))
(check "struct-index slot index x" 0 (clamiga::%struct-slot-index 'sidx-amiga 'x))
(check "struct-index slot index y" 1 (clamiga::%struct-slot-index 'sidx-amiga 'y))
(check "struct-index missing slot" nil (clamiga::%struct-slot-index 'sidx-amiga 'nope))
(check "struct-index unknown type" nil (clamiga::%struct-slot-index 'sidx-amiga-no-such 'x))
(let ((s (make-sidx-amiga)))
  (check "struct-index buried slot-value" 7 (+ (slot-value s 'x) (slot-value s 'y)))
  (setf (slot-value s 'y) 40)
  (check "struct-index buried setf slot-value" 40 (slot-value s 'y))
  (check "struct-index buried slot-boundp" t (slot-boundp s 'x))
  (check "struct-index buried typep" t (typep s 'sidx-amiga)))
(ext:gc-compact)
(check "struct-index survives compaction" 1
       (clamiga::%struct-slot-index 'sidx-amiga 'y))
(check "struct-index slot-value after compaction" 3
       (slot-value (make-sidx-amiga) 'x))
;; Re-registration: the NEWEST entry must win (first-match semantics).
(clamiga::%register-struct-type 'sidx-amiga-re 1 nil '((a nil)))
(clamiga::%register-struct-type 'sidx-amiga-re 3 nil '((p nil) (q nil) (r nil)))
(check "struct-index redefinition newest wins" 2
       (clamiga::%struct-slot-index 'sidx-amiga-re 'r))
(check "struct-index redefinition drops old slot" nil
       (clamiga::%struct-slot-index 'sidx-amiga-re 'a))
(defclass sv-amiga-unbnd () ((u)))
(defmethod slot-unbound ((class t) (inst sv-amiga-unbnd) slot-name)
  (declare (ignore class slot-name))
  :sv-amiga-unbound-sentinel)
(check "slot-unbound via protocol" :sv-amiga-unbound-sentinel (slot-value (make-instance 'sv-amiga-unbnd) 'u))

; --- :allocation :class shared slots (MOP) ---
(defclass cs-amiga-share () ((n :allocation :class :initform 0)))
(let ((a (make-instance 'cs-amiga-share)) (b (make-instance 'cs-amiga-share)))
  (setf (slot-value a 'n) 42)
  (check "class-alloc shared write" 42 (slot-value b 'n)))
(defclass cs-amiga-parent () ((x :allocation :class :initform 'hello)))
(defclass cs-amiga-child (cs-amiga-parent) ())
(let ((p (make-instance 'cs-amiga-parent)) (c (make-instance 'cs-amiga-child)))
  (declare (ignore p))
  (setf (slot-value c 'x) 'world)
  (check "class-alloc inherited cell" 'world (slot-value (make-instance 'cs-amiga-parent) 'x)))
(check "class-alloc shared location identity" t
  (eq (slot-definition-location (first (class-slots (find-class 'cs-amiga-parent))))
      (slot-definition-location (first (class-slots (find-class 'cs-amiga-child))))))
(defclass cs-amiga-p2 () ((x :allocation :class :initform 1)))
(defclass cs-amiga-c2 (cs-amiga-p2) ((x :allocation :class :initform 2)))
(let ((p (make-instance 'cs-amiga-p2)) (c (make-instance 'cs-amiga-c2)))
  (check "class-alloc redefined parent initform" 1 (slot-value p 'x))
  (check "class-alloc redefined child initform" 2 (slot-value c 'x))
  (setf (slot-value p 'x) 100)
  (check "class-alloc redefined child unaffected" 2 (slot-value c 'x))
  (check "class-alloc redefined parent written" 100 (slot-value p 'x)))
(defclass cs-amiga-loc () ((shared :allocation :class :initform 'hi)))
(check "class-alloc location is cons" t (consp (slot-definition-location (first (class-slots (find-class 'cs-amiga-loc))))))
(make-instance 'cs-amiga-loc)
(check "class-alloc cell cdr holds value" 'hi (cdr (slot-definition-location (first (class-slots (find-class 'cs-amiga-loc))))))
(defclass cs-amiga-mix () ((a :initarg :a) (b :allocation :class :initform 'shared) (c :initarg :c)))
(let ((slots (class-slots (find-class 'cs-amiga-mix))))
  (check "class-alloc instance a loc" 0 (slot-definition-location (find 'a slots :key #'slot-definition-name)))
  (check "class-alloc class b loc consp" t (consp (slot-definition-location (find 'b slots :key #'slot-definition-name))))
  (check "class-alloc instance c loc" 1 (slot-definition-location (find 'c slots :key #'slot-definition-name))))
(let ((inst (make-instance 'cs-amiga-mix :a 10 :c 30)))
  (check "class-alloc read a" 10 (slot-value inst 'a))
  (check "class-alloc read b" 'shared (slot-value inst 'b))
  (check "class-alloc read c" 30 (slot-value inst 'c))
  (check "class-alloc struct-ref 0" 10 (%struct-ref inst 0))
  (check "class-alloc struct-ref 1" 30 (%struct-ref inst 1)))
(defclass cs-amiga-bp () ((s :allocation :class)))
(let ((a (make-instance 'cs-amiga-bp)) (b (make-instance 'cs-amiga-bp)))
  (check "class-alloc boundp unbound" nil (slot-boundp a 's))
  (setf (slot-value a 's) 7)
  (check "class-alloc boundp bound sibling" t (slot-boundp b 's))
  (slot-makunbound a 's)
  (check "class-alloc makunbound affects sibling" nil (slot-boundp b 's)))
(defvar *cs-amiga-init-counter* 0)
(defclass cs-amiga-init () ((k :allocation :class :initform (incf *cs-amiga-init-counter*))))
(let ((a (make-instance 'cs-amiga-init)) (b (make-instance 'cs-amiga-init)))
  (check "class-alloc initform runs once" 1 *cs-amiga-init-counter*)
  (check "class-alloc initform same for a" 1 (slot-value a 'k))
  (check "class-alloc initform same for b" 1 (slot-value b 'k)))
(defclass cs-amiga-ia () ((k :allocation :class :initarg :k :initform 0)))
(let ((a (make-instance 'cs-amiga-ia :k 99)) (b (make-instance 'cs-amiga-ia)))
  (check "class-alloc initarg write a" 99 (slot-value a 'k))
  (check "class-alloc initarg persists b" 99 (slot-value b 'k)))

; --- Reified EQL specializers (MOP) ---
(check "eql-spec intern same value eq" t (eq (intern-eql-specializer 42) (intern-eql-specializer 42)))
(check "eql-spec intern different value not eq" nil (eq (intern-eql-specializer 42) (intern-eql-specializer 43)))
(check "eql-spec intern symbol" t (eq (intern-eql-specializer 'amiga-sym) (intern-eql-specializer 'amiga-sym)))
(check "eql-spec object accessor integer" 42 (eql-specializer-object (intern-eql-specializer 42)))
(check "eql-spec object accessor symbol" 'amiga-tag (eql-specializer-object (intern-eql-specializer 'amiga-tag)))
(check "eql-spec object accessor nil" nil (eql-specializer-object (intern-eql-specializer nil)))
(check "eql-spec predicate true" t (eql-specializer-p (intern-eql-specializer 1)))
(check "eql-spec predicate on class" nil (eql-specializer-p (find-class 'integer)))
(check "eql-spec predicate on fixnum" nil (eql-specializer-p 42))
(check "eql-spec predicate on cons" nil (eql-specializer-p '(eql 42)))
(check "eql-spec class bootstrapped" 'eql-specializer (class-name (class-of (intern-eql-specializer 42))))
(defgeneric es-amiga-disp (x))
(defmethod es-amiga-disp ((x (eql 'alpha))) :a)
(defmethod es-amiga-disp ((x (eql 'beta))) :b)
(defmethod es-amiga-disp ((x symbol)) :other)
(check "eql-spec dispatch alpha" :a (es-amiga-disp 'alpha))
(check "eql-spec dispatch beta"  :b (es-amiga-disp 'beta))
(check "eql-spec dispatch other" :other (es-amiga-disp 'gamma))
(defgeneric es-amiga-ms (x))
(defmethod es-amiga-ms ((x (eql 7))) :seven)
(check "eql-spec method-specializers is metaobject" t
  (eql-specializer-p (first (method-specializers (first (gf-methods (gethash 'es-amiga-ms *generic-function-table*)))))))
(check "eql-spec method-specializers object" 7
  (eql-specializer-object (first (method-specializers (first (gf-methods (gethash 'es-amiga-ms *generic-function-table*)))))))
(defgeneric es-amiga-share1 (x))
(defgeneric es-amiga-share2 (x))
(defmethod es-amiga-share1 ((x (eql :amiga-shared))) :one)
(defmethod es-amiga-share2 ((x (eql :amiga-shared))) :two)
(check "eql-spec shared across methods" t
  (eq (first (method-specializers (first (gf-methods (gethash 'es-amiga-share1 *generic-function-table*)))))
      (first (method-specializers (first (gf-methods (gethash 'es-amiga-share2 *generic-function-table*)))))))
(check "extract-specializer-names eql+class" '((eql 99) string)
  (extract-specializer-names '((x (eql 99)) (y string))))
(check "extract-specializer-names class-only" '(integer)
  (extract-specializer-names '((x integer))))
(defgeneric es-amiga-rep (x))
(defmethod es-amiga-rep ((x (eql 1))) :first)
(defmethod es-amiga-rep ((x (eql 1))) :second)
(check "eql-spec redef replaces" :second (es-amiga-rep 1))
(check "eql-spec redef single method" 1
  (length (gf-methods (gethash 'es-amiga-rep *generic-function-table*))))
(defgeneric es-amiga-cache (x))
(defmethod es-amiga-cache ((x (eql 42))) :fortytwo)
(defmethod es-amiga-cache ((x integer)) :int)
(check "eql-spec cacheable-p :EQL" :eql
  (gf-cacheable-p (gethash 'es-amiga-cache *generic-function-table*)))

; --- Funcallable standard class (MOP) ---
(check "funcallable-standard-class bootstrapped" 'funcallable-standard-class
  (class-name (find-class 'funcallable-standard-class)))
(check "funcallable-standard-object bootstrapped" 'funcallable-standard-object
  (class-name (find-class 'funcallable-standard-object)))
(defgeneric fsc-amiga-foo (x))
(defmethod fsc-amiga-foo ((x integer)) (* x 2))
(check "gf typep funcallable-standard-object" t (typep #'fsc-amiga-foo 'funcallable-standard-object))
(check "gf typep standard-generic-function" t (typep #'fsc-amiga-foo 'standard-generic-function))
(check "gf typep generic-function" t (typep #'fsc-amiga-foo 'generic-function))
(check "non-gf typep generic-function" nil (typep 42 'generic-function))
(check "gf typep function" t (typep #'fsc-amiga-foo 'function))
(check "gf functionp" t (functionp #'fsc-amiga-foo))
(check "gf class-of" t (eq (class-of #'fsc-amiga-foo) (find-class 'standard-generic-function)))
(check "gf type-of" 'standard-generic-function (type-of #'fsc-amiga-foo))
(check "gf callable via symbol" 14 (fsc-amiga-foo 7))
(check "gf funcall"    20 (funcall #'fsc-amiga-foo 10))
(check "gf apply"      50 (apply #'fsc-amiga-foo '(25)))
(check "gf funcall ensure" 30 (funcall (ensure-generic-function 'fsc-amiga-foo) 15))
(defgeneric fsc-amiga-sfi (x))
(defmethod fsc-amiga-sfi ((x integer)) :default)
(check "gf dispatch before sfi" :default (fsc-amiga-sfi 5))
(set-funcallable-instance-function
  (ensure-generic-function 'fsc-amiga-sfi)
  (lambda (&rest args) (cons :replaced args)))
(check "gf dispatch after sfi" '(:replaced 5) (fsc-amiga-sfi 5))
(check "sfi returns gf" t
  (eq (set-funcallable-instance-function
        (ensure-generic-function 'fsc-amiga-sfi)
        (lambda (&rest args) args))
      (ensure-generic-function 'fsc-amiga-sfi)))
(defclass sia-amiga () ((a :initarg :a) (b :initarg :b)))
(defvar *sia-amiga-inst* (make-instance 'sia-amiga :a 10 :b 20))
(check "standard-instance-access a" 10 (standard-instance-access *sia-amiga-inst* 0))
(check "standard-instance-access b" 20 (standard-instance-access *sia-amiga-inst* 1))
(setf (standard-instance-access *sia-amiga-inst* 0) 99)
(check "standard-instance-access setf" 99 (slot-value *sia-amiga-inst* 'a))
(check "standard-instance-access == struct-ref" t
  (eq (standard-instance-access *sia-amiga-inst* 1) (%struct-ref *sia-amiga-inst* 1)))
(check "funcallable-standard-instance-access" 99
  (funcallable-standard-instance-access *sia-amiga-inst* 0))
(defgeneric fsc-amiga-cdf (x))
(defmethod fsc-amiga-cdf ((x integer)) :int-case)
(check "compute-discriminating-function functionp" t
  (functionp (compute-discriminating-function (ensure-generic-function 'fsc-amiga-cdf))))
(check "compute-discriminating-function dispatches" :int-case
  (funcall (compute-discriminating-function (ensure-generic-function 'fsc-amiga-cdf)) 7))

; --- Custom generic-function metaclass (CLHS :generic-function-class) ---
; This is the snooze defroute pattern: a custom GF class whose instances
; self-register via initialize-instance :after.
(defclass rgf-amiga (cl:standard-generic-function) ()
  (:metaclass funcallable-standard-class))
(defvar *rgf-amiga-registry* (make-hash-table))
(defmethod initialize-instance :after ((gf rgf-amiga) &rest args)
  (declare (ignore args))
  (setf (gethash (generic-function-name gf) *rgf-amiga-registry*) gf))
(defgeneric rga-amiga (x) (:generic-function-class rgf-amiga)
  (:method ((x integer)) (* x 2)))
(check "custom gf-class type-of" 'rgf-amiga (type-of #'rga-amiga))
(check "custom gf-class functionp" t (functionp #'rga-amiga))
(check "custom gf typep own class" t (typep #'rga-amiga 'rgf-amiga))
(check "custom gf typep standard-generic-function" t
  (typep #'rga-amiga 'standard-generic-function))
(check "custom gf callable" 42 (rga-amiga 21))
(check "custom gf funcall" 16 (funcall #'rga-amiga 8))
(check "custom gf initialize-instance :after fired" t
  (eq (gethash 'rga-amiga *rgf-amiga-registry*) #'rga-amiga))
; dispatch ON the custom GF as a specialized argument
(defgeneric classify-amiga-gf (g))
(defmethod classify-amiga-gf ((g rgf-amiga)) :custom)
(defmethod classify-amiga-gf ((g standard-generic-function)) :standard)
(defgeneric plain-amiga-gf (x))
(check "dispatch on custom gf arg" :custom (classify-amiga-gf #'rga-amiga))
(check "dispatch on plain gf arg" :standard (classify-amiga-gf #'plain-amiga-gf))
; explicit standard-generic-function keeps base behavior
(defgeneric gfd-amiga-explicit (x)
  (:generic-function-class standard-generic-function)
  (:method ((x t)) x))
(check "explicit standard gf-class type-of" 'standard-generic-function
  (type-of #'gfd-amiga-explicit))

; --- Method metaobject protocol (MOP) ---
(defgeneric mmop-amiga-bl (x))
(defmethod mmop-amiga-bl ((x integer)) :int)
(check "method-generic-function back-link" t
  (eq (method-generic-function (first (gf-methods (gethash 'mmop-amiga-bl *generic-function-table*))))
      (gethash 'mmop-amiga-bl *generic-function-table*)))
(check "extract-lambda-list plain" '(x y z)
  (extract-lambda-list '((x point) (y (eql 3)) z)))
(check "extract-lambda-list preserves tail" '(x &optional (y 5) &rest rest &key k)
  (extract-lambda-list '((x integer) &optional (y 5) &rest rest &key k)))
(check "extract-specializer-names padded t" '(t integer t)
  (extract-specializer-names '(x (y integer) z)))
(check "extract-specializer-names stops at &" '(string)
  (extract-specializer-names '((a string) &optional b &key c)))

(defgeneric mmop-amiga-add (x))
(defmethod mmop-amiga-add ((x t)) :default)
(let* ((gf (ensure-generic-function 'mmop-amiga-add))
       (fn (lambda (x) (declare (ignore x)) :added))
       (m (%make-struct 'standard-method
            nil (list (find-class 'integer)) '() fn '(x))))
  (add-method gf m))
(check "add-method installs" :added (mmop-amiga-add 42))
(check "add-method keeps default" :default (mmop-amiga-add "hi"))

(defgeneric mmop-amiga-addret (x))
(check "add-method returns gf" t
  (let* ((gf (ensure-generic-function 'mmop-amiga-addret))
         (m (%make-struct 'standard-method
              nil (list (find-class 't)) '()
              (lambda (x) (declare (ignore x)) :ok) '(x))))
    (eq (add-method gf m) gf)))

(defgeneric mmop-amiga-rm (x))
(defmethod mmop-amiga-rm ((x t)) :default)
(defmethod mmop-amiga-rm ((x integer)) :int)
(check "remove-method before" :int (mmop-amiga-rm 42))
(let* ((gf (gethash 'mmop-amiga-rm *generic-function-table*))
       (m  (find-if (lambda (m)
                      (equal (method-specializers m) (list (find-class 'integer))))
                    (gf-methods gf))))
  (remove-method gf m))
(check "remove-method after" :default (mmop-amiga-rm 42))

(defgeneric mmop-amiga-rmbl (x))
(defmethod mmop-amiga-rmbl ((x integer)) :int)
(defvar *mmop-amiga-rm-m*
  (first (gf-methods (gethash 'mmop-amiga-rmbl *generic-function-table*))))
(remove-method (gethash 'mmop-amiga-rmbl *generic-function-table*) *mmop-amiga-rm-m*)
(check "remove-method clears back-link" nil
  (method-generic-function *mmop-amiga-rm-m*))

(defgeneric mmop-amiga-fm (x))
(defmethod mmop-amiga-fm ((x string)) :s)
(defmethod mmop-amiga-fm :before ((x string)) :before)
(check "find-method locates" t
  (not (null (find-method (gethash 'mmop-amiga-fm *generic-function-table*)
                          '() (list (find-class 'string))))))
(check "find-method resolves class names" t
  (not (null (find-method (gethash 'mmop-amiga-fm *generic-function-table*)
                          '() '(string)))))
(check "find-method resolves (eql v)" t
  (progn
    (defgeneric mmop-amiga-fmeql (x))
    (defmethod mmop-amiga-fmeql ((x (eql 7))) :seven)
    (not (null (find-method (gethash 'mmop-amiga-fmeql *generic-function-table*)
                            '() '((eql 7)))))))
(check "find-method missing returns nil" nil
  (progn
    (defgeneric mmop-amiga-fmnil (x))
    (find-method (gethash 'mmop-amiga-fmnil *generic-function-table*)
                 '() '(integer) nil)))

(defgeneric mmop-amiga-em (x))
(ensure-method 'mmop-amiga-em
  '(lambda (x) (declare (ignore x)) :ensured)
  :specializers '(integer))
(check "ensure-method installs" :ensured (mmop-amiga-em 99))

(defgeneric mmop-amiga-emdef (x))
(ensure-method 'mmop-amiga-emdef
  '(lambda (x) (declare (ignore x)) :any))
(check "ensure-method default specs catches string" :any (mmop-amiga-emdef "a"))
(check "ensure-method default specs catches integer" :any (mmop-amiga-emdef 42))

(defgeneric mmop-amiga-mml (x))
(check "make-method-lambda default identity" '((lambda (x) x) nil)
  (let* ((gf (ensure-generic-function 'mmop-amiga-mml))
         (m (%make-struct 'standard-method
              gf (list (find-class 't)) '()
              (lambda (x) x) '(x))))
    (multiple-value-list (make-method-lambda gf m '(lambda (x) x) nil))))

; --- User-defined metaclasses (MOP, specs/mop.md) ---
;; A metaclass is a subclass of standard-class; its instances (classes) reuse
;; the fixed 12-slot class layout plus any metaclass slots at index 12+.
(defclass amiga-abstract-mc (standard-class) ())
(defmethod allocate-instance ((a amiga-abstract-mc) &rest initargs)
  (declare (ignore initargs))
  (error "Cannot allocate instances of abstract class ~s" (class-name a)))
(defclass amiga-abstract-shape () () (:metaclass amiga-abstract-mc))
(check "metaclass: class-of is the metaclass" 'amiga-abstract-mc
       (class-name (class-of (find-class 'amiga-abstract-shape))))
(check "metaclass abstract: make-instance signals" :signalled
       (handler-case (progn (make-instance 'amiga-abstract-shape) :no-error)
         (error () :signalled)))
;; abstract metaclass must not break normal make-instance
(defclass amiga-plain-pt () ((x :initarg :x :accessor amiga-plain-x)))
(check "metaclass abstract: normal class still instantiable" 9
       (amiga-plain-x (make-instance 'amiga-plain-pt :x 9)))
;; TOPMOST-OBJECT: metaclass :around injects a superclass + :default-initargs
(defclass amiga-topmost () ())
(defclass amiga-topmost-mc (standard-class)
  ((topc :initarg :topc :reader amiga-topc)))
(defmethod validate-superclass ((c1 amiga-topmost-mc) (c2 standard-class)) t)
(defun amiga-ins-super (sc list)
  (cond ((null list) (list sc))
        ((subtypep sc (first list)) (cons sc list))
        (t (cons (first list) (amiga-ins-super sc (rest list))))))
(defmethod initialize-instance :around
    ((class amiga-topmost-mc) &rest initargs &key direct-superclasses topc)
  (if (find topc direct-superclasses :test (lambda (a b) (subtypep b a)))
      (call-next-method)
      (apply #'call-next-method class
             :direct-superclasses (amiga-ins-super (find-class topc) direct-superclasses)
             initargs)))
(defclass amiga-meta (amiga-topmost-mc) () (:default-initargs :topc 'amiga-topmost))
(defclass amiga-tc-user () () (:metaclass amiga-meta))
(check "metaclass topmost: instance is typep injected superclass" t
       (typep (make-instance 'amiga-tc-user) 'amiga-topmost))
(check "metaclass topmost: metaclass slot from default-initargs" 'amiga-topmost
       (amiga-topc (find-class 'amiga-tc-user)))

; --- Specialized array element types (VECT-TYPE, specs/mop.md) ---
(check "array-element-type fixnum array" 'fixnum
       (array-element-type (make-array 3 :element-type 'fixnum)))
(check "array-element-type general stays T" t
       (eq 't (array-element-type (make-array 3))))
(check "fixnum array is not (vector t)" nil
       (typep (make-array 3 :element-type 'fixnum :adjustable t :fill-pointer 0)
              '(vector t)))
(check "general vector is (vector t)" t (typep (vector 1 2 3) '(vector t)))
(check "fixnum array is (vector fixnum)" t
       (typep (make-array 3 :element-type 'fixnum) '(vector fixnum)))
(check "general vector is not (vector fixnum)" nil
       (typep #(1 2 3) '(vector fixnum)))
(check "fixnum array still stores tagged values" 5
       (let ((a (make-array 2 :element-type 'fixnum)))
         (setf (aref a 0) 5) (aref a 0)))
; copy-seq / subseq / make-sequence preserve a specialized element type
(check "copy-seq preserves single-float element type" 'single-float
       (array-element-type
        (copy-seq (make-array 3 :element-type 'single-float
                                :initial-contents '(1.0 2.0 3.0)))))
(check "subseq preserves single-float element type" 'single-float
       (array-element-type
        (subseq (make-array 5 :element-type 'single-float
                              :initial-contents '(1.0 2.0 3.0 4.0 5.0)) 1 4)))
(check "make-sequence honors (vector single-float)" 'single-float
       (array-element-type (make-sequence '(vector single-float) 4
                                          :initial-element 1.0)))
(check "make-sequence (vector single-float) typep" t
       (typep (make-sequence '(vector single-float) 4 :initial-element 1.0)
              '(vector single-float)))
(check "copy-seq of general vector stays T" t
       (eq 't (array-element-type (copy-seq (vector 1 2 3)))))

; --- CLOS Phase 9: print-object ---
(check "print-object class" "#<STANDARD-CLASS INTEGER>" (print-object (find-class 'integer) nil))
(check "print-object gf" "#<STANDARD-GENERIC-FUNCTION GREET>" (print-object (gethash 'greet *generic-function-table*) nil))
(defclass printable () ((tag :initarg :tag :accessor printable-tag)))
(defmethod print-object ((obj printable) stream) (concatenate 'string "#<PRINTABLE " (printable-tag obj) ">"))
(check "print-object custom" "#<PRINTABLE hello>" (print-object (make-instance 'printable :tag "hello") nil))
(check "print-object default nil" nil (print-object 42 nil))

; --- LOOP nested conditionals ---
(check "loop nested if" '(:two :three) (loop :for x :in '(1 2 3) :when (> x 1) :if (= x 2) :collect :two :else :collect :three :end))
(check "loop nested when collect" '(3 5) (loop :for x :in '(1 2 3 4 5) :when (oddp x) :if (> x 2) :collect x :end))
(check "loop nested unless" '(1 3) (loop :for x :in '(1 2 3 4) :when t :unless (evenp x) :collect x :end))
(check "loop nested if do" '(2 4) (let ((r nil)) (loop :for x :in '(1 2 3 4) :when t :if (evenp x) :do (push x r) :end) (nreverse r)))

; --- Defmacro destructuring ---
(defmacro amiga-dm1 ((a b) c) `(list ,a ,b ,c))
(check "defmacro destructuring req" '(1 2 3) (amiga-dm1 (1 2) 3))
(defmacro amiga-dm2 (var &body (then-form &optional else-form)) `(let ((,var t)) (if ,var ,then-form ,else-form)))
(check "defmacro destructuring body" :yes (amiga-dm2 x :yes :no))
(check "defmacro destructuring body opt" :only (amiga-dm2 x :only))
(defmacro amiga-dm3 (x &optional (y 42)) `(list ,x ,y))
(check "defmacro optional not destructured" '(1 42) (amiga-dm3 1))
(check "defmacro optional provided" '(1 2) (amiga-dm3 1 2))

; --- define-modify-macro ---
(define-modify-macro amiga-appendf (&rest args) append)
(check "define-modify-macro" '(1 2 3 4) (let ((x '(1 2))) (amiga-appendf x '(3 4)) x))

; --- reduce :from-end ---
(check "reduce from-end cons" '(1 2 3 . 4) (reduce #'cons '(1 2 3 4) :from-end t))
(check "reduce from-end initial" '(1 2 3) (reduce #'cons '(1 2 3) :from-end t :initial-value '()))
(check "reduce from-end vector" '(1 2 3 . 4) (reduce #'cons #(1 2 3 4) :from-end t))
(check "reduce forward unchanged" 10 (reduce #'+ '(1 2 3 4)))

; --- defmethod implicit block ---
(defgeneric amiga-block-test (x))
(defmethod amiga-block-test ((x t)) (return-from amiga-block-test 42) 99)
(check "defmethod implicit block" 42 (amiga-block-test 1))

; --- user-homedir-pathname ---
(check "user-homedir-pathname type" t (pathnamep (user-homedir-pathname)))

; --- define-condition :default-initargs (merged by make-condition/error) ---
(define-condition amiga-di-cond (simple-condition) ()
  (:default-initargs :format-control "amiga default"))
;; Default applies when the initarg is omitted (was NIL while the setter was a stub).
(check "default-initargs via make-condition" "amiga default"
       (simple-condition-format-control (make-condition 'amiga-di-cond)))
;; Default applies via error/signal too.
(check "default-initargs via error" "amiga default"
       (handler-case (error 'amiga-di-cond)
         (amiga-di-cond (c) (simple-condition-format-control c))))
;; Explicit initarg overrides the default (CLHS 7.1.4).
(check "default-initargs explicit overrides" "explicit"
       (simple-condition-format-control
        (make-condition 'amiga-di-cond :format-control "explicit")))
;; Explicit NIL is preserved (a default must not clobber it).
(check "default-initargs explicit nil preserved" nil
       (simple-condition-format-control
        (make-condition 'amiga-di-cond :format-control nil)))
;; A subclass without its own default-initargs inherits the parent's.
(define-condition amiga-di-sub (amiga-di-cond) ())
(check "default-initargs inherited by subclass" "amiga default"
       (simple-condition-format-control (make-condition 'amiga-di-sub)))

; --- ASDF-session features ---
(check "lisp-implementation-type returns string" t (stringp (lisp-implementation-type)))
(check "lisp-implementation-version returns string" t (stringp (lisp-implementation-version)))
(check "software-type returns string" t (stringp (software-type)))
(check "machine-type returns string" t (stringp (machine-type)))

; --- make-string ---
(check "make-string basic" "aaa" (make-string 3 :initial-element #\a))
(check "make-string default" 3 (length (make-string 3)))

; --- fboundp / fmakunbound ---
(defun amiga-fboundp-test () 42)
(check "fboundp defined function" t (fboundp 'amiga-fboundp-test))
(fmakunbound 'amiga-fboundp-test)
(check "fmakunbound removes binding" nil (fboundp 'amiga-fboundp-test))
(defun (setf amiga-setf-test) (v o) (declare (ignore o)) v)
(check "fboundp (setf name)" t (fboundp '(setf amiga-setf-test)))
(fmakunbound '(setf amiga-setf-test))
(check "fmakunbound (setf name)" nil (fboundp '(setf amiga-setf-test)))
(check "fmakunbound unknown (setf name)" '(setf no-such-fn) (fmakunbound '(setf no-such-fn)))
(defvar *amiga-mak-test* 123)
(check "makunbound returns symbol" '*amiga-mak-test* (makunbound '*amiga-mak-test*))
(check "makunbound unbinds" nil (boundp '*amiga-mak-test*))

; --- ext:getenv ---
(check "getenv non-existent returns NIL" nil (ext:getenv "CLAMIGA_NONEXISTENT_VAR_12345"))
(check "getenv type check" t (handler-case (progn (ext:getenv 42) nil) (error () t)))

; --- Nested quasiquote ---
(check "nested-qq double unquote" '(quasiquote (a (unquote 42)))
  (let ((x 42)) ``(a ,,x)))
(check "nested-qq unquote-splice" '(quasiquote (a (unquote 1) (unquote 2) (unquote 3)))
  (let ((xs '(1 2 3))) ``(a ,,@xs)))
(check "nested-qq simple" '(quasiquote (a b))
  ``(a b))
(check "nested-qq inner splice" '(a 1 2 b)
  (let ((xs '(1 2))) `(a ,@xs b)))
(check "dotted quasiquote" '(a . 3)
  `(a . ,(+ 1 2)))

; --- Large quasiquote: > 255 top-level elements expands to one APPEND;
; qq_append must chunk the call so the OP_CALL byte count cannot wrap
; (regression for ironclad threefish.lisp 1024-bit ARX macro).
(defun qq-big-1 ()
  (let ((x 7))
    `(k0 ,x k1 ,x k2 ,x k3 ,x k4 ,x k5 ,x k6 ,x k7 ,x k8 ,x k9 ,x k10 ,x k11 ,x k12 ,x k13 ,x k14 ,x k15 ,x k16 ,x k17 ,x k18 ,x k19 ,x k20 ,x k21 ,x k22 ,x k23 ,x k24 ,x k25 ,x k26 ,x k27 ,x k28 ,x k29 ,x k30 ,x k31 ,x k32 ,x k33 ,x k34 ,x k35 ,x k36 ,x k37 ,x k38 ,x k39 ,x k40 ,x k41 ,x k42 ,x k43 ,x k44 ,x k45 ,x k46 ,x k47 ,x k48 ,x k49 ,x k50 ,x k51 ,x k52 ,x k53 ,x k54 ,x k55 ,x k56 ,x k57 ,x k58 ,x k59 ,x k60 ,x k61 ,x k62 ,x k63 ,x k64 ,x k65 ,x k66 ,x k67 ,x k68 ,x k69 ,x k70 ,x k71 ,x k72 ,x k73 ,x k74 ,x k75 ,x k76 ,x k77 ,x k78 ,x k79 ,x k80 ,x k81 ,x k82 ,x k83 ,x k84 ,x k85 ,x k86 ,x k87 ,x k88 ,x k89 ,x k90 ,x k91 ,x k92 ,x k93 ,x k94 ,x k95 ,x k96 ,x k97 ,x k98 ,x k99 ,x k100 ,x k101 ,x k102 ,x k103 ,x k104 ,x k105 ,x k106 ,x k107 ,x k108 ,x k109 ,x k110 ,x k111 ,x k112 ,x k113 ,x k114 ,x k115 ,x k116 ,x k117 ,x k118 ,x k119 ,x k120 ,x k121 ,x k122 ,x k123 ,x k124 ,x k125 ,x k126 ,x k127 ,x k128 ,x k129 ,x k130 ,x k131 ,x k132 ,x k133 ,x k134 ,x k135 ,x k136 ,x k137 ,x k138 ,x k139 ,x k140 ,x k141 ,x k142 ,x k143 ,x k144 ,x k145 ,x k146 ,x k147 ,x k148 ,x k149 ,x k150 ,x k151 ,x k152 ,x k153 ,x k154 ,x k155 ,x k156 ,x k157 ,x k158 ,x k159 ,x k160 ,x k161 ,x k162 ,x k163 ,x k164 ,x k165 ,x k166 ,x k167 ,x k168 ,x k169 ,x k170 ,x k171 ,x k172 ,x k173 ,x k174 ,x k175 ,x k176 ,x k177 ,x k178 ,x k179 ,x k180 ,x k181 ,x k182 ,x k183 ,x k184 ,x k185 ,x k186 ,x k187 ,x k188 ,x k189 ,x k190 ,x k191 ,x k192 ,x k193 ,x k194 ,x k195 ,x k196 ,x k197 ,x k198 ,x k199 ,x k200 ,x k201 ,x k202 ,x k203 ,x k204 ,x k205 ,x k206 ,x k207 ,x k208 ,x k209 ,x k210 ,x k211 ,x k212 ,x k213 ,x k214 ,x k215 ,x k216 ,x k217 ,x k218 ,x k219 ,x k220 ,x k221 ,x k222 ,x k223 ,x k224 ,x k225 ,x k226 ,x k227 ,x k228 ,x k229 ,x k230 ,x k231 ,x k232 ,x k233 ,x k234 ,x k235 ,x k236 ,x k237 ,x k238 ,x k239 ,x k240 ,x k241 ,x k242 ,x k243 ,x k244 ,x k245 ,x k246 ,x k247 ,x k248 ,x k249 ,x k250 ,x k251 ,x k252 ,x k253 ,x k254 ,x k255 ,x k256 ,x k257 ,x k258 ,x k259 ,x k260 ,x k261 ,x k262 ,x k263 ,x k264 ,x k265 ,x k266 ,x k267 ,x k268 ,x k269 ,x k270 ,x k271 ,x k272 ,x k273 ,x k274 ,x k275 ,x k276 ,x k277 ,x k278 ,x k279 ,x k280 ,x k281 ,x k282 ,x k283 ,x k284 ,x k285 ,x k286 ,x k287 ,x k288 ,x k289 ,x k290 ,x k291 ,x k292 ,x k293 ,x k294 ,x k295 ,x k296 ,x k297 ,x k298 ,x k299 ,x )))
(check "large-qq length" 600 (length (qq-big-1)))
(check "large-qq unquote slot survives chunking" 7 (nth 257 (qq-big-1)))
(check "large-qq literal slot survives chunking" (quote k199) (nth 398 (qq-big-1)))

; --- reader-error condition ---
(check "reader-error typep" t (typep (make-condition 'reader-error) 'reader-error))
(check "reader-error is parse-error" t (typep (make-condition 'reader-error) 'parse-error))
(check "reader-error is stream-error" t (typep (make-condition 'reader-error) 'stream-error))
(check "reader-error is error" t (typep (make-condition 'reader-error) 'error))

; --- Standard condition accessors ---
(check "stream-error-stream" *standard-output*
  (stream-error-stream (make-condition 'stream-error :stream *standard-output*)))
(check "cell-error-name" 'foo
  (cell-error-name (make-condition 'cell-error :name 'foo)))
(check "file-error-pathname" "/tmp/x"
  (file-error-pathname (make-condition 'file-error :pathname "/tmp/x")))

; --- define-compiler-macro ---
(check "define-compiler-macro" 'test-cm
  (define-compiler-macro test-cm (&whole form x) (declare (ignore form x)) nil))
(check "compiler-macro-function installed" t (functionp (compiler-macro-function 'test-cm)))
(check "compiler-macro-function unknown" nil (compiler-macro-function 'test-cm-not-defined))
; NIL is a symbol with no macro binding -> returns NIL, like (macro-function t)
(check "macro-function nil" nil (macro-function nil))
(check "macro-function t" nil (macro-function t))
(check "compiler-macro-function nil" nil (compiler-macro-function nil))

; --- setf values ---
(check "setf values" '(10 20 30)
  (let ((a 0) (b 0) (c 0))
    (setf (values a b c) (values 10 20 30))
    (list a b c)))

; --- define-setf-expander (no-op) ---
(check "define-setf-expander" 'my-place
  (define-setf-expander my-place (x) (declare (ignore x)) nil))

; --- PROGV ---
(defparameter *progv-x* 10)
(check "progv basic" 99 (progv '(*progv-x*) '(99) *progv-x*))
(check "progv restored" 10 *progv-x*)

(defparameter *progv-a* 1)
(defparameter *progv-b* 2)
(check "progv multiple" 30 (progv '(*progv-a* *progv-b*) '(10 20) (+ *progv-a* *progv-b*)))
(check "progv multiple restored a" 1 *progv-a*)
(check "progv multiple restored b" 2 *progv-b*)

(check "progv empty symbols" 3 (progv '() '() (+ 1 2)))

(check "progv extra values" 77
  (progv '(*progv-a*) '(77 88 99) *progv-a*))

(check "progv fewer values unbound" nil
  (progv '(*progv-a*) '() (boundp '*progv-a*)))
(check "progv fewer values restored" 1 *progv-a*)

(check "progv restore on throw" 42
  (catch 'done (progv '(*progv-x*) '(999) (throw 'done 42))))
(check "progv restored after throw" 10 *progv-x*)

(check "progv nested" 2
  (progv '(*progv-x*) '(1)
    (progv '(*progv-x*) '(2) *progv-x*)))
(check "progv nested restored" 10 *progv-x*)

; --- Macrolet/symbol-macrolet across lambda ---
(check "macrolet across lambda" 42
  (macrolet ((foo () '42))
    (funcall (lambda () (foo)))))
(check "symbol-macrolet across lambda" 42
  (symbol-macrolet ((x 42))
    (funcall (lambda () x))))
(check "macrolet across nested lambda" 15
  (macrolet ((add10 (x) `(+ ,x 10)))
    (funcall (lambda () (funcall (lambda () (add10 5)))))))

; --- Bignum logand/logxor masking (CDB hash pattern) ---
(check "logand mask 32bit 1" 2087597948 (logand #xFFFFFFFF 6382565244))
(check "logand mask 32bit 2" 1356358965 (logand #xFFFFFFFF 6950613443893))
(check "logand mask 32bit 3" 0 (logand #xFFFFFFFF 4294967296))
(check "logand mask 32bit 4" 4294967295 (logand #xFFFFFFFF 4294967295))
(check "logand mask 32bit 5" 4294967295 (logand #xFFFFFFFF 8589934591))
(check "logand bignum+bignum 1" 4294967295 (logand #xFFFFFFFF #xFFFFFFFF))
(check "logand bignum+bignum 2" 4294967295 (logand #x1FFFFFFFF #xFFFFFFFF))
(check "logxor bignum 1" 2087597849 (logxor 2087597948 101))
(check "logxor bignum 2" 4294967295 (logxor #xFFFFFFFF 0))
(check "logxor bignum 3" 0 (logxor #xFFFFFFFF #xFFFFFFFF))
(check "ash bignum left" 6189154176 (ash 193411068 5))
(check "logand+ash" 1894186880 (logand #xFFFFFFFF (ash 193411068 5)))
(check "cdb hash fiveam" 1356358965
  (let ((h 5381))
    (dolist (c '(102 105 118 101 97 109) h)
      (setf h (logand #xFFFFFFFF (+ h (ash h 5))))
      (setf h (logxor h c)))))
(check "cdb hash step f" 177603 (logxor (logand #xFFFFFFFF (+ 5381 (ash 5381 5))) 102))
(check "cdb hash step i" 5860938 (logxor (logand #xFFFFFFFF (+ 177603 (ash 177603 5))) 105))
(check "cdb hash step v" 193411068 (logxor (logand #xFFFFFFFF (+ 5860938 (ash 5860938 5))) 118))
(check "cdb hash add e" 6382565244 (+ 193411068 (ash 193411068 5)))
(check "cdb hash mask e" 2087597948 (logand #xFFFFFFFF 6382565244))
(check "cdb hash xor e" 2087597849 (logxor 2087597948 101))

; --- FASL compile-file and load ---

; Write a test Lisp file, compile it, load the FASL
(with-open-file (s "T:fasl-test1.lisp" :direction :output :if-exists :supersede)
  (write-string "(defun fasl-test-fn (x) (* x 3))" s)
  (terpri s))
(compile-file "T:fasl-test1.lisp" :output-file "T:fasl-test1.fasl")
(check "compile-file creates FASL" t (not (null (probe-file "T:fasl-test1.fasl"))))

; Load the FASL and verify function works
(load "T:fasl-test1.fasl")
(check "load FASL defines function" 15 (fasl-test-fn 5))

; compile-file with multiple forms
(with-open-file (s "T:fasl-test2.lisp" :direction :output :if-exists :supersede)
  (write-string "(defvar *fasl-var-a* 100)" s) (terpri s)
  (write-string "(defvar *fasl-var-b* 200)" s) (terpri s)
  (write-string "(defun fasl-sum () (+ *fasl-var-a* *fasl-var-b*))" s) (terpri s))
(compile-file "T:fasl-test2.lisp" :output-file "T:fasl-test2.fasl")
(load "T:fasl-test2.fasl")
(check "FASL multiple forms" 300 (fasl-sum))

; compile-file-pathname returns a pathname
(check "compile-file-pathname type" t (pathnamep (compile-file-pathname "T:foo.lisp")))

; compile-file returns a pathname
(check "compile-file return type" t
  (pathnamep (compile-file "T:fasl-test1.lisp" :output-file "T:fasl-test1.fasl")))

; FASL with macros (eval-during-compile)
(with-open-file (s "T:fasl-test3.lisp" :direction :output :if-exists :supersede)
  (write-string "(defmacro fasl-triple (x) `(* 3 ,x))" s) (terpri s)
  (write-string "(defun fasl-apply-triple (n) (fasl-triple n))" s) (terpri s))
(compile-file "T:fasl-test3.lisp" :output-file "T:fasl-test3.fasl")
(load "T:fasl-test3.fasl")
(check "FASL with macros" 21 (fasl-apply-triple 7))

; Regression: compile-file must not run (make-package) twice.
; compile-file + load in the same session must not signal "Package already exists".
(with-open-file (s "T:fasl-pkgonce.lisp" :direction :output :if-exists :supersede)
  (write-string "(defvar *p* (make-package :fasl-pkgonce-test))" s) (terpri s))
(compile-file "T:fasl-pkgonce.lisp" :output-file "T:fasl-pkgonce.fasl")
(load "T:fasl-pkgonce.fasl")
(check "compile-file no double make-package" t (not (null (find-package :fasl-pkgonce-test))))

; Regression: a circular list constant in a compiled file must serialize
; without the FASL writer walking the cycle forever (which overflowed the
; per-unit buffer cap and forced a slow source-load fallback — serapeum's
; copy-firstn test).  The reconstructed literal must still be circular.
(with-open-file (s "T:fasl-circ.lisp" :direction :output :if-exists :supersede)
  (write-string "(defun fasl-circ-fn () '#1=(1 2 3 . #1#))" s) (terpri s))
(compile-file "T:fasl-circ.lisp" :output-file "T:fasl-circ.fasl")
(check "compile-file circular constant creates FASL" t
  (not (null (probe-file "T:fasl-circ.fasl"))))
(load "T:fasl-circ.fasl")
(check "FASL circular constant first three" '(1 2 3)
  (let ((c (fasl-circ-fn))) (list (first c) (second c) (third c))))
(check "FASL circular constant loops back" t
  (let ((c (fasl-circ-fn))) (eq c (nthcdr 3 c))))
(check "FASL circular constant fourth elt" 1
  (fourth (fasl-circ-fn)))

; Regression: a sublist appearing both as an element (CAR) and as the
; final CDR tail must round-trip as the SAME (EQ) object.
(with-open-file (s "T:fasl-share.lisp" :direction :output :if-exists :supersede)
  (write-string "(defun fasl-share-fn () '(#2=(9 9) 1 . #2#))" s) (terpri s))
(compile-file "T:fasl-share.lisp" :output-file "T:fasl-share.fasl")
(load "T:fasl-share.fasl")
(check "FASL shared sublist preserved" t
  (let ((c (fasl-share-fn))) (eq (car c) (cddr c))))

; --- LOAD-TIME-VALUE under COMPILE-FILE ---

; Regression: LTV referencing a forward function must not signal Undefined function.
(with-open-file (s "T:fasl-ltv1.lisp" :direction :output :if-exists :supersede)
  (write-string "(defun ltv-gen-fn () (list 4 5 6))" s) (terpri s)
  (write-string "(defun ltv-get-fn () (load-time-value (ltv-gen-fn)))" s) (terpri s))
(compile-file "T:fasl-ltv1.lisp" :output-file "T:fasl-ltv1.fasl")
(load "T:fasl-ltv1.fasl")
(check "LTV forward function" '(4 5 6) (ltv-get-fn))

; LTV must return the same (EQ) object on every call.
(with-open-file (s "T:fasl-ltv2.lisp" :direction :output :if-exists :supersede)
  (write-string "(defun ltv-eq-fn () (load-time-value (cons 1 2)))" s) (terpri s))
(compile-file "T:fasl-ltv2.lisp" :output-file "T:fasl-ltv2.fasl")
(load "T:fasl-ltv2.fasl")
(check "LTV eq stable" t (eq (ltv-eq-fn) (ltv-eq-fn)))

; Two distinct LTV forms must produce independent objects (no cross-contamination).
(with-open-file (s "T:fasl-ltv3.lisp" :direction :output :if-exists :supersede)
  (write-string "(defun ltv-d1 () (load-time-value (cons 'x 'y)))" s) (terpri s)
  (write-string "(defun ltv-d2 () (load-time-value (cons 'x 'y)))" s) (terpri s))
(compile-file "T:fasl-ltv3.lisp" :output-file "T:fasl-ltv3.fasl")
(load "T:fasl-ltv3.fasl")
(check "LTV distinct forms" nil (eq (ltv-d1) (ltv-d2)))

; --- MAKE-LOAD-FORM under COMPILE-FILE (CLHS 7.6) ---
; A literal CLOS instance whose class defines a MAKE-LOAD-FORM method is
; serialized as creation + init forms and reconstructed at load time, NOT
; dumped slot-for-slot.  MAKE-LOAD-FORM-SAVING-SLOTS embeds the object in its
; init form, so the self-referential slot must reconstruct EQ to the object
; itself (the circular self-reference).  Uses FIND-SYMBOL so the check reads
; without the file's package existing at this reader's read time.
(with-open-file (s "T:fasl-mlf.lisp" :direction :output :if-exists :supersede)
  (write-string "(defpackage :mlf-ami (:use :cl))" s) (terpri s)
  (write-string "(in-package :mlf-ami)" s) (terpri s)
  (write-string "(defclass mlfnode () ((label :initarg :label :accessor mlf-label) (n :initarg :n :accessor mlf-n) (self :accessor mlf-self)))" s) (terpri s)
  (write-string "(defmethod make-load-form ((x mlfnode) &optional env) (declare (ignore env)) (make-load-form-saving-slots x))" s) (terpri s)
  (write-string "(defvar *mlf* #.(let ((x (make-instance 'mlfnode :label \"hi\" :n 7))) (setf (mlf-self x) x) x))" s) (terpri s)
  (write-string "(in-package :cl-user)" s) (terpri s))
(compile-file "T:fasl-mlf.lisp" :output-file "T:fasl-mlf.fasl")
(load "T:fasl-mlf.fasl")
(check "make-load-form slot values + circular self-ref" '("hi" 7 t)
  (let ((x (symbol-value (find-symbol "*MLF*" "MLF-AMI"))))
    (list (funcall (find-symbol "MLF-LABEL" "MLF-AMI") x)
          (funcall (find-symbol "MLF-N" "MLF-AMI") x)
          (if (eq x (funcall (find-symbol "MLF-SELF" "MLF-AMI") x)) t nil))))

; --- Top-level MACROLET / SYMBOL-MACROLET / LOCALLY under COMPILE-FILE ---
; (CLHS 3.2.3.1: their body forms are processed as top-level forms, so
; compile-time side effects inside them — DEFPACKAGE, DEFMACRO — take effect
; at compile time.  Regression for the log4cl package.lisp load failure.)

; A top-level MACROLET whose body expands (via the local macro) to a DEFPACKAGE
; must create the package at compile time so a later form can read its symbols.
(with-open-file (s "T:fasl-mlpkg.lisp" :direction :output :if-exists :supersede)
  (write-string "(macrolet ((def-it () '(defpackage :cf-ml-pkg (:use :cl) (:export #:marker)))) (def-it))" s) (terpri s)
  (write-string "(defun cf-ml-fn () (symbol-name 'cf-ml-pkg:marker))" s) (terpri s))
(compile-file "T:fasl-mlpkg.lisp" :output-file "T:fasl-mlpkg.fasl")
(load "T:fasl-mlpkg.fasl")
(check "macrolet defpackage at compile time" "MARKER" (cf-ml-fn))

; A DEFMACRO inside a top-level MACROLET body must be installed at compile time
; so a later same-file form expands it instead of compiling a function call.
(with-open-file (s "T:fasl-mlmac.lisp" :direction :output :if-exists :supersede)
  (write-string "(macrolet ((emit () '(defmacro cf-ml-mac (x) (list 'list :ml x)))) (emit))" s) (terpri s)
  (write-string "(defparameter *cf-ml-mac* (cf-ml-mac 7))" s) (terpri s))
(compile-file "T:fasl-mlmac.lisp" :output-file "T:fasl-mlmac.fasl")
(load "T:fasl-mlmac.fasl")
(check "macrolet defmacro available later" '(:ml 7) *cf-ml-mac*)

; Same for a DEFMACRO inside a top-level LOCALLY.
(with-open-file (s "T:fasl-locmac.lisp" :direction :output :if-exists :supersede)
  (write-string "(locally (declare (optimize speed)) (defmacro cf-loc-mac (x) (list 'list :loc x)))" s) (terpri s)
  (write-string "(defparameter *cf-loc* (cf-loc-mac 9))" s) (terpri s))
(compile-file "T:fasl-locmac.lisp" :output-file "T:fasl-locmac.fasl")
(load "T:fasl-locmac.fasl")
(check "locally defmacro available later" '(:loc 9) *cf-loc*)

; A top-level DECLAIM must survive the FASL round trip (CLHS: declaim ~
; eval-when (:compile-toplevel :load-toplevel :execute) proclaim) — loading
; the fasl must re-establish the proclamation.  compile-file's compile-time
; leg sets safety 0 in this session too, so reset to safety 1 in between;
; only the load-time leg can flip it back to 0.
(with-open-file (s "T:fasl-declaim.lisp" :direction :output :if-exists :supersede)
  (write-string "(declaim (optimize (safety 0)))" s) (terpri s))
(compile-file "T:fasl-declaim.lisp" :output-file "T:fasl-declaim.fasl")
(declaim (optimize (safety 1)))
(check "declaim reset control: THE signals at safety 1" :signaled
  (handler-case (eval '(the fixnum "dc")) (error () :signaled)))
(load "T:fasl-declaim.fasl")
(check "declaim survives fasl round trip (safety 0 after load)" "dc"
  (eval '(the fixnum "dc")))
(declaim (optimize (safety 1)))

; --- COMPILE-FILE reader package vs *PACKAGE* churn ---
; The reader interns each form's symbols using the package set by the file's
; IN-PACKAGE forms.  A compile-time *PACKAGE* change by means other than
; IN-PACKAGE (here a SETF, standing in for ASDF's session machinery unwinding
; dynamic *PACKAGE* bindings) must NOT leak into the reader, so a later form
; still interns in the file's IN-PACKAGE package.  Regression for esrap's
; "FUNCTION: OUTPUT is not a valid function name" (EXPRESSION-CASE interning in
; COMMON-LISP-USER).  IN-PACKAGE — including nested in PROGN — must still switch
; the reader package.
(defpackage "CF-CHURN-AMI" (:use "CL"))
(defpackage "CF-OTHER-AMI" (:use "CL"))
(with-open-file (s "T:fasl-churn.lisp" :direction :output :if-exists :supersede)
  (write-string "(in-package :cf-churn-ami)" s) (terpri s)
  (write-string "(eval-when (:compile-toplevel :load-toplevel :execute) (setf *package* (find-package :cf-other-ami)))" s) (terpri s)
  (write-string "(defvar cf-churn-marker-ami 1)" s) (terpri s))
(compile-file "T:fasl-churn.lisp" :output-file "T:fasl-churn.fasl")
(check "compile-file package churn does not leak to reader" t
  (and (not (null (find-symbol "CF-CHURN-MARKER-AMI" :cf-churn-ami)))
       (null (find-symbol "CF-CHURN-MARKER-AMI" :cf-other-ami))))

(defpackage "CF-NEST-AMI" (:use "CL"))
(with-open-file (s "T:fasl-nestpkg.lisp" :direction :output :if-exists :supersede)
  (write-string "(progn (in-package :cf-nest-ami))" s) (terpri s)
  (write-string "(defvar cf-nest-marker-ami 2)" s) (terpri s))
(compile-file "T:fasl-nestpkg.lisp" :output-file "T:fasl-nestpkg.fasl")
(check "compile-file progn-wrapped in-package switches reader" t
  (not (null (find-symbol "CF-NEST-MARKER-AMI" :cf-nest-ami))))
(in-package "COMMON-LISP-USER")

; --- Dispatch cache ---
(defgeneric dcache-test (x))
(defmethod dcache-test ((x point)) (point-x x))
(check "cache basic dispatch" 42 (dcache-test (make-instance 'point :x 42 :y 0)))
; second call uses cache
(check "cache hit correct" 77 (dcache-test (make-instance 'point :x 77 :y 0)))
; cache populated
(check "cache populated" t
  (let ((gf (gethash 'dcache-test *generic-function-table*)))
    (not (null (gf-dispatch-cache gf)))))
; cacheable-p is 1 for single-dispatch
(check "cache cacheable-p" 1
  (let ((gf (gethash 'dcache-test *generic-function-table*)))
    (gf-cacheable-p gf)))
; invalidation on defmethod
(defmethod dcache-test ((x point3d)) (+ (point-x x) (point-z x)))
(check "cache cleared on defmethod" nil
  (let ((gf (gethash 'dcache-test *generic-function-table*)))
    (gf-dispatch-cache gf)))
(check "cache new method works" 15
  (dcache-test (make-instance 'point3d :x 10 :y 0 :z 5)))
; EQL specializer bypass
(defgeneric dcache-eql-test (x))
(defmethod dcache-eql-test ((x (eql 42))) 'forty-two)
(defmethod dcache-eql-test ((x integer)) 'other-int)
(check "cache eql dispatch 42" 'forty-two (dcache-eql-test 42))
(check "cache eql dispatch 7" 'other-int (dcache-eql-test 7))
(check "cache eql cacheable-p" :eql
  (let ((gf (gethash 'dcache-eql-test *generic-function-table*)))
    (gf-cacheable-p gf)))
; Multi-dispatch bypass
(defgeneric dcache-multi (x y))
(defmethod dcache-multi ((x point) (y point)) 'both-points)
(check "cache multi dispatch" 'both-points
  (dcache-multi (make-instance 'point :x 1 :y 2)
                (make-instance 'point :x 3 :y 4)))
(check "cache multi cacheable-p" 2
  (let ((gf (gethash 'dcache-multi *generic-function-table*)))
    (gf-cacheable-p gf)))
; before/after/around with cache
(defparameter *dcache-trace* nil)
(defgeneric dcache-combo (x))
(defmethod dcache-combo :before ((x point)) (push 'before *dcache-trace*))
(defmethod dcache-combo ((x point)) (push 'primary *dcache-trace*) 'done)
(defmethod dcache-combo :after ((x point)) (push 'after *dcache-trace*))
(defmethod dcache-combo :around ((x point)) (push 'around *dcache-trace*) (call-next-method))
(setq *dcache-trace* nil)
(check "cache combo result" 'done (dcache-combo (make-instance 'point :x 0 :y 0)))
(check "cache combo order" '(around before primary after) (nreverse *dcache-trace*))
; second call uses cache, same result
(setq *dcache-trace* nil)
(dcache-combo (make-instance 'point :x 1 :y 1))
(check "cache combo cached order" '(around before primary after) (nreverse *dcache-trace*))
; no applicable method with cache
(defgeneric dcache-err (x))
(defmethod dcache-err ((x point)) 'point-ok)
(check "cache no-applicable errors" 'got-error
  (handler-case (dcache-err "hello") (error (c) 'got-error)))

; --- EQL specializer cache ---
(defgeneric eql-c-test (x))
(defmethod eql-c-test ((x (eql 42))) 'forty-two)
(defmethod eql-c-test ((x integer)) 'some-int)
(check "eql cache 42" 'forty-two (eql-c-test 42))
(check "eql cache 7" 'some-int (eql-c-test 7))
; cache hit
(check "eql cache hit 42" 'forty-two (eql-c-test 42))
(check "eql cache populated" t
  (let ((gf (gethash 'eql-c-test *generic-function-table*)))
    (not (null (gf-dispatch-cache gf)))))
; mixed eql + class
(defgeneric eql-mix (x))
(defmethod eql-mix ((x (eql :alpha))) 'got-alpha)
(defmethod eql-mix ((x (eql :beta))) 'got-beta)
(defmethod eql-mix ((x symbol)) 'some-symbol)
(check "eql cache alpha" 'got-alpha (eql-mix :alpha))
(check "eql cache beta" 'got-beta (eql-mix :beta))
(check "eql cache gamma fallback" 'some-symbol (eql-mix :gamma))
; class fallback
(check "eql cache class fallback" 'some-int (eql-c-test 100))
; call-next-method
(defgeneric eql-cnm (x))
(defmethod eql-cnm ((x (eql 99))) (list 'eql-99 (call-next-method)))
(defmethod eql-cnm ((x integer)) (list 'int x))
(check "eql cache cnm" '(eql-99 (int 99)) (eql-cnm 99))
(check "eql cache cnm cached" '(eql-99 (int 99)) (eql-cnm 99))
(check "eql cache cnm fallback" '(int 50) (eql-cnm 50))
; (eql nil)
(defgeneric eql-nil-test (x))
(defmethod eql-nil-test ((x (eql nil))) 'got-nil)
(defmethod eql-nil-test ((x t)) 'got-other)
(check "eql cache nil" 'got-nil (eql-nil-test nil))
(check "eql cache nil other" 'got-other (eql-nil-test 'foo))
(check "eql cache nil cached" 'got-nil (eql-nil-test nil))
; EQL on arg2
(defgeneric eql-arg2 (x y))
(defmethod eql-arg2 ((x t) (y (eql :load))) (list 'loading x))
(defmethod eql-arg2 ((x t) (y (eql :compile))) (list 'compiling x))
(defmethod eql-arg2 ((x t) (y t)) (list 'default x y))
(check "eql cache arg2 load" '(loading foo) (eql-arg2 'foo :load))
(check "eql cache arg2 compile" '(compiling bar) (eql-arg2 'bar :compile))
(check "eql cache arg2 default" '(default baz :other) (eql-arg2 'baz :other))
(check "eql cache arg2 cached" '(loading x) (eql-arg2 'x :load))
; invalidation
(eql-c-test 42) ; populate cache
(defmethod eql-c-test ((x (eql 0))) 'zero)
(check "eql cache invalidated" nil
  (let ((gf (gethash 'eql-c-test *generic-function-table*)))
    (gf-dispatch-cache gf)))
(check "eql cache new method" 'zero (eql-c-test 0))
(check "eql cache old still works" 'forty-two (eql-c-test 42))
; cacheable-p
(check "eql cache cacheable-p" :eql
  (let ((gf (gethash 'eql-c-test *generic-function-table*)))
    (gf-cacheable-p gf)))

; --- Multi-dispatch cache ---
(defgeneric mc-test (x y))
(defmethod mc-test ((x point) (y point))
  (+ (point-x x) (point-x y)))
(defmethod mc-test ((x point3d) (y point))
  (+ (point-x x) (point-z x) (point-x y)))
(check "multi cache point+point" 30
  (mc-test (make-instance 'point :x 10 :y 0)
           (make-instance 'point :x 20 :y 0)))
(check "multi cache point3d+point" 35
  (mc-test (make-instance 'point3d :x 10 :y 0 :z 5)
           (make-instance 'point :x 20 :y 0)))
; cache hit
(check "multi cache hit" 3
  (mc-test (make-instance 'point :x 1 :y 0)
           (make-instance 'point :x 2 :y 0)))
(check "multi cache populated" t
  (let ((gf (gethash 'mc-test *generic-function-table*)))
    (not (null (gf-dispatch-cache gf)))))
(check "multi cache cacheable-p" 2
  (let ((gf (gethash 'mc-test *generic-function-table*)))
    (gf-cacheable-p gf)))
; invalidation
(defmethod mc-test ((x point) (y point3d))
  (+ (point-x x) (point-z y)))
(check "multi cache invalidated" nil
  (let ((gf (gethash 'mc-test *generic-function-table*)))
    (gf-dispatch-cache gf)))
(check "multi cache new method" 40
  (mc-test (make-instance 'point :x 10 :y 0)
           (make-instance 'point3d :x 1 :y 2 :z 30)))
; call-next-method in multi-dispatch
(defgeneric mc-cnm (x y))
(defmethod mc-cnm ((x point) (y point))
  (list 'base (point-x x) (point-x y)))
(defmethod mc-cnm ((x point3d) (y point))
  (list 'p3d (call-next-method)))
(check "multi cache cnm" '(p3d (base 10 20))
  (mc-cnm (make-instance 'point3d :x 10 :y 0 :z 5)
          (make-instance 'point :x 20 :y 0)))
(check "multi cache cnm cached" '(p3d (base 99 88))
  (mc-cnm (make-instance 'point3d :x 99 :y 0 :z 1)
          (make-instance 'point :x 88 :y 0)))

; --- EMF cache (effective method closure caching) ---
(defgeneric emf-cnm-test (x))
(defmethod emf-cnm-test ((x point3d))
  (list 'p3d (point-x x) (call-next-method)))
(defmethod emf-cnm-test ((x point))
  (list 'pt (point-x x)))
; First call — populates cache
(check "emf cache cnm no-args 1st" '(p3d 10 (pt 10))
  (emf-cnm-test (make-instance 'point3d :x 10 :y 20 :z 30)))
; Second call — uses cached EMF
(check "emf cache cnm no-args cached" '(p3d 99 (pt 99))
  (emf-cnm-test (make-instance 'point3d :x 99 :y 0 :z 0)))

; call-next-method with explicit args
(defgeneric emf-cnm-args (x))
(defmethod emf-cnm-args ((x point3d))
  (call-next-method (make-instance 'point :x 42 :y 0)))
(defmethod emf-cnm-args ((x point))
  (point-x x))
(check "emf cache cnm with-args" 42
  (emf-cnm-args (make-instance 'point3d :x 1 :y 2 :z 3)))
(check "emf cache cnm with-args cached" 42
  (emf-cnm-args (make-instance 'point3d :x 7 :y 8 :z 9)))

; Full method combination with EMF cache
(defparameter *emf-trace* nil)
(defgeneric emf-combo (x))
(defmethod emf-combo :before ((x point)) (push (list 'before (point-x x)) *emf-trace*))
(defmethod emf-combo ((x point)) (push 'primary *emf-trace*) (point-x x))
(defmethod emf-combo :after ((x point)) (push 'after *emf-trace*))
(defmethod emf-combo :around ((x point)) (push 'around *emf-trace*) (call-next-method))
(setq *emf-trace* nil)
(check "emf cache combo result" 5
  (emf-combo (make-instance 'point :x 5 :y 0)))
(check "emf cache combo order" '(around (before 5) primary after) (nreverse *emf-trace*))
; Cached call
(setq *emf-trace* nil)
(check "emf cache combo cached" 88
  (emf-combo (make-instance 'point :x 88 :y 0)))
(check "emf cache combo cached order" '(around (before 88) primary after) (nreverse *emf-trace*))

; next-method-p in cached path
(defgeneric emf-nmp (x))
(defmethod emf-nmp ((x point3d)) (list (next-method-p) (call-next-method)))
(defmethod emf-nmp ((x point)) (next-method-p))
(check "emf cache next-method-p" '(t nil)
  (emf-nmp (make-instance 'point3d :x 0 :y 0 :z 0)))
(check "emf cache next-method-p cached" '(t nil)
  (emf-nmp (make-instance 'point3d :x 1 :y 1 :z 1)))

; Negative caching
(defgeneric emf-neg (x))
(defmethod emf-neg ((x point)) 'ok)
(check "emf cache negative ok" 'ok
  (emf-neg (make-instance 'point :x 0 :y 0)))
(check "emf cache negative error" 'no-method
  (handler-case (emf-neg "hello") (error (c) 'no-method)))
(check "emf cache negative cached" 'no-method
  (handler-case (emf-neg "world") (error (c) 'no-method)))

; --- Long-form method combination: MAKE-METHOD + :ORDER (serapeum) ---
; (make-method FORM) in a CALL-METHOD next-methods list builds an
; anonymous method whose body evaluates FORM.
(define-method-combination amc-mk ()
  ((primary ()))
  `(call-method ,(first primary) ((make-method 99))))
(defgeneric amc-mkg (x) (:method-combination amc-mk))
(defmethod amc-mkg ((x t)) (call-next-method))
(check "long-form make-method next" 99 (amc-mkg 1))

; Regression: (make-method (call-next-method)) as the last next-method must
; signal "No next method" rather than infinitely recursing (bug: simple-primary-p
; =T on anon methods caused the leaf optimization to bind
; *call-next-method-function* to the anon method's own function).
(define-method-combination amc-mmcnm ()
  ((primary ()))
  `(call-method ,(first primary) ((make-method (call-next-method)))))
(defgeneric amc-mmcnmg (x) (:method-combination amc-mmcnm))
(defmethod amc-mmcnmg ((x t)) (call-next-method))
(check "make-method (call-next-method) signals error" :no-next
  (handler-case (amc-mmcnmg 1) (error () :no-next)))

; :ORDER :MOST-SPECIFIC-LAST reverses the matched methods.
(defclass amc-a () ())
(defclass amc-b (amc-a) ())
(define-method-combination amc-msl ()
  ((all () :order :most-specific-last))
  `(list ,@(mapcar (lambda (m) `(call-method ,m ())) all)))
(defgeneric amc-mslg (x) (:method-combination amc-msl))
(defmethod amc-mslg ((x amc-a)) 'a)
(defmethod amc-mslg ((x amc-b)) 'b)
(check "long-form order most-specific-last" '(a b)
  (amc-mslg (make-instance 'amc-b)))

; End-to-end serapeum STANDARD/CONTEXT combination shape.
(define-method-combination amc-sc ()
  ((around (:around))
   (context (:context) :order :most-specific-last)
   (primary ()))
  (let* ((form `(call-method ,(first primary)))
         (around-form (if around
                          `(call-method ,(first around)
                                        (,@(rest around) (make-method ,form)))
                          form)))
    (if context
        `(call-method ,(first context)
                      (,@(rest context) (make-method ,around-form)))
        around-form)))
(defclass amc-sa () ())
(defclass amc-sb (amc-sa) ())
(defgeneric amc-scg (x) (:method-combination amc-sc))
(defmethod amc-scg ((x amc-sa)) '(prim))
(defmethod amc-scg :around ((x amc-sa)) (cons 'a/ar (call-next-method)))
(defmethod amc-scg :around ((x amc-sb)) (cons 'b/ar (call-next-method)))
(defmethod amc-scg :context ((x amc-sa)) (cons 'a/cx (call-next-method)))
(defmethod amc-scg :context ((x amc-sb)) (cons 'b/cx (call-next-method)))
(check "long-form standard/context order" '(a/cx b/cx b/ar a/ar prim)
  (amc-scg (make-instance 'amc-sb)))

; --- no-applicable-method (CLHS 7.6.6.3) ---
; Standard GF exists and signals an error of type ERROR.
(check "no-applicable-method is bound" t (and (fboundp 'no-applicable-method) t))
(check "no-applicable-method signals error" t
  (handler-case (no-applicable-method #'no-applicable-method (list 1))
    (error (e) (typep e 'error))))
; A direct call and a real dispatch miss signal the SAME condition type
; (the serapeum NO-APPLICABLE-METHOD-ERROR pattern depends on this).
(defgeneric nam-gwnm (x))
(check "no-applicable-method consistent type" t
  (eq (handler-case (no-applicable-method #'no-applicable-method (list 1))
        (error (e) (type-of e)))
      (handler-case (nam-gwnm 1)
        (error (e) (type-of e)))))
; Full serapeum deftype pattern: derive the miss type, catch a real miss.
(deftype nam-err ()
  (load-time-value
   (handler-case (no-applicable-method #'no-applicable-method (list 1))
     (error (e) (type-of e)))))
(defgeneric nam-gwnm2 (x))
(check "no-applicable-method deftype proper-subtype" t
  (and (subtypep 'nam-err 'error)
       (not (nth-value 0 (subtypep 'error 'nam-err)))))
(check "no-applicable-method deftype catches miss" 'caught
  (handler-case (nam-gwnm2 1) (nam-err () 'caught)))
; CLHS 7.6.6.3: a user method on no-applicable-method that returns normally
; must have its return value propagated (regression for when vs return-from).
(defgeneric nam-rv-gf (x))
(defmethod no-applicable-method ((gf (eql #'nam-rv-gf)) &rest args)
  (declare (ignore args))
  42)
(check "no-applicable-method return value propagated" 42
  (nam-rv-gf 'anything))

; ============================================================
; Threading (MP package)
; ============================================================
(format t "~%--- Threading ---~%")

; --- Thread creation and join ---
(check "thread make and join" 42
  (mp:join-thread (mp:make-thread (lambda () 42))))

(check "thread join returns computed result" 60
  (mp:join-thread (mp:make-thread (lambda () (+ 10 20 30)))))

(check "thread join cons result" '(1 2 3)
  (mp:join-thread (mp:make-thread (lambda () (list 1 2 3)))))

(check "thread make with name" "worker"
  (mp:thread-name (mp:make-thread (lambda () nil) :name "worker")))

; NOTE: the worker deep-recursion / deep-NLX-nesting regression tests live in
; the HOST suite only (tests/test_threads.c).  Two AmigaOS-specific reasons, both
; empirically confirmed against the FS-UAE suite:
;   1. The worker VM frame-budget increase (256 -> 1024) that lets a worker run
;      the same call depth as the main thread is host-only: bumping it on Amiga
;      shifts heap layout and tips a pre-existing moving-GC bug (see the
;      CL_WORKER_* rationale in src/core/thread.h).  So Amiga workers keep the
;      256-frame budget and cannot assert deep-CALL-nesting.
;   2. NLX-nesting can't be probed on an Amiga worker at all: executing nested
;      CATCH/BLOCK/UNWIND-PROTECT recurses the native C stack (the VM runs each
;      catch body one level deeper), and an Amiga worker's OS-default ~64KB
;      process stack Gurus at ~150-200 frames — far below the 256-slot nlx_stack
;      capacity the guard protects.  A recursive OR a macro-expanded nested-CATCH
;      test therefore crashes the worker before it can exercise the guard.
; The per-thread NLX guard (cl_nlx_max == CT->nlx_max, which bounds a worker at
; its real 256-slot allocation) IS active on Amiga; it is exercised on host,
; where workers get the full budget and can nest deeply.

; --- Thread predicates ---
(check "thread alive-p after join" nil
  (let ((thr (mp:make-thread (lambda () 42))))
    (mp:join-thread thr)
    (mp:thread-alive-p thr)))

(check "current-thread returns thread" t
  (not (null (mp:current-thread))))

(check "all-threads includes main" t
  (>= (length (mp:all-threads)) 1))

; Regression: the main thread carries a default name ("main thread") to
; match bordeaux-threads / SBCL / CCL instead of printing #<THREAD NIL>.
(check "main thread has default name" "main thread"
  (mp:thread-name (mp:current-thread)))

(check "main thread name via all-threads" "main thread"
  (mp:thread-name (first (mp:all-threads))))

; Regression: a worker's function object must stay GC-protected for the whole
; duration of its application.  thread_entry used to null t->result (the
; parent's only GC root for the closure) right after grabbing func, so a
; concurrent (gc) could sweep the closure mid-apply, the worker would error,
; and join-thread would return NIL instead of the real result.  The worker
; body allocates nothing (42 is a fixnum), so a non-42 result means the
; closure itself was collected.
(check "worker function GC-protected across apply" 0
  (let ((bad 0))
    (dotimes (i 150)
      (let ((th (mp:make-thread (lambda () 42))))
        (gc) (gc)
        (unless (eql (mp:join-thread th) 42)
          (setq bad (1+ bad)))))
    bad))

; Regression: concurrent MP:MAKE-THREAD + EXT:GC-COMPACT at several workers must
; not hang, corrupt, or crash.  Guards the thread-vs-moving-GC cluster:
;   - newborn stop-the-world hang (registered-before-OS-thread child waited on
;     forever) — a hang here fails the whole suite via the FS-UAE watchdog;
;   - child->result double-forward (gc_roots aliasing t->result) and the stale
;     `func` C-local in thread_entry — both showed as a worker applying a
;     wrong-typed object, so a corrupt worker returns :corrupt not :ok.
; Each worker allocates a fresh closure and fresh list, compacts, and validates.
(check "concurrent gc-compact multi-worker no corruption" t
  (let ((threads nil) (ok t))
    (dotimes (w 4)
      (let ((id w))
        (push (mp:make-thread
                (lambda ()
                  (let ((bad nil))
                    (dotimes (i 40)
                      (let ((s (list id i (list 'k i) "abc")))
                        (ext:gc-compact)
                        (unless (and (eql (first s) id) (eql (second s) i)
                                     (equal (third s) (list 'k i))
                                     (equal (fourth s) "abc"))
                          (setq bad t))))
                    (if bad :corrupt :ok)))
                :name "w")
              threads)))
    (dolist (thr threads)
      (unless (eq (mp:join-thread thr) :ok) (setq ok nil)))
    ok))

; Regression: a worker's fresh heap return value (cons/string) must survive a
; peer thread's compaction that runs while JOIN-THREAD is parked.  The result
; used to live only in the unregistered worker's t->result — neither marked nor
; forwarded — so JOIN returned a stale offset (garbage).  Fixed by publishing it
; into the GC-managed wrapper.  A wrong result here means the fix regressed.
(check "join returns live heap result under concurrent gc" t
  (let ((threads nil) (ok t))
    (dotimes (w 4)
      (let ((id w))
        (push (cons id
                    (mp:make-thread
                      (lambda ()
                        (dotimes (i 20) (ext:gc-compact))
                        (list id (list 'tag id)))
                      :name "w"))
              threads)))
    (dolist (pair threads)
      (let* ((id (car pair)) (r (mp:join-thread (cdr pair))))
        (unless (and (consp r) (eql (first r) id)
                     (equal (second r) (list 'tag id)))
          (setq ok nil))))
    ok))

; Regression: concurrent (re)definition of a method must not make a peer
; thread's dispatch of the SAME generic function spuriously signal "No
; applicable method" for a method that plainly exists.  This is the multi-
; threaded race behind the ASDF field report "No applicable method for
; ACTION-STATUS with args of types (NULL LOAD-OP SYSTEM)" during
; (asdf:load-system ...): worker/watcher threads dispatch GFs while the main
; thread (re)defines methods.  Root cause (lib/clos.lisp %install-method-in-gf):
; the method list was updated in TWO stores (store old-minus-replaced, THEN
; cons the new method on), leaving a window where GF-METHODS transiently
; EXCLUDED the method being (re)defined; a dispatcher walking the list in that
; window computed an empty applicable set.  Fixed by a SINGLE atomic
; %set-gf-methods store, so a concurrent reader always sees a complete list.
; Shaped like ACTION-STATUS: a NULL-specialized method beside a class method.
(defclass amdisp-op () ())
(defclass amdisp-load-op (amdisp-op) ())
(defclass amdisp-comp () ())
(defclass amdisp-sys (amdisp-comp) ())
(defgeneric amdisp-status (plan operation component))
(defmethod amdisp-status ((plan null) (o amdisp-op) (c amdisp-comp)) :null-method)
(defmethod amdisp-status ((p amdisp-comp) (o amdisp-op) (c amdisp-comp)) :plan-method)
(defparameter *amdisp-o* (make-instance 'amdisp-load-op))
(defparameter *amdisp-c* (make-instance 'amdisp-sys))
(check "concurrent add-method does not break peer dispatch" 0
  (let ((dispatchers nil) (redefiners nil) (fails 0))
    ;; Redefiners: repeatedly re-install the null method (each install first
    ;; removes the existing matching method — the window the fix closes).
    (dotimes (r 2)
      (push (mp:make-thread
              (lambda ()
                (dotimes (i 600)
                  (eval '(defmethod amdisp-status ((plan null) (o amdisp-op)
                                                    (c amdisp-comp)) :null-method)))
                :ok)
              :name "redef")
            redefiners))
    ;; Dispatchers: (amdisp-status nil o c) must ALWAYS resolve to :null-method.
    ;; Each returns its own spurious-miss count (no shared counter to race on).
    (dotimes (d 3)
      (push (mp:make-thread
              (lambda ()
                (let ((bad 0))
                  (dotimes (i 5000)
                    (let ((v (handler-case (amdisp-status nil *amdisp-o* *amdisp-c*)
                               (error () :fail))))
                      (unless (eq v :null-method) (setq bad (1+ bad)))))
                  bad))
              :name "disp")
            dispatchers))
    (dolist (thr dispatchers) (setq fails (+ fails (mp:join-thread thr))))
    (dolist (thr redefiners) (mp:join-thread thr))
    fails))

; Regression: writer-writer race — two threads installing GENUINELY
; DIFFERENT methods (distinct EQL specializers) on the SAME gf concurrently
; must not lose either install.  The single-store fix above only guarantees
; a concurrent READER never sees a torn list; it does not by itself stop two
; WRITERS from racing: both can read the same old GF-METHODS before either
; stores, so whichever %set-gf-methods lands second silently discards the
; other thread's method (a lost update).  Unlike the test above, which
; repeatedly reinstalls the SAME method (so a lost update is invisible —
; any surviving install is equivalent), every method here is unique, so a
; lost update leaves a permanent, detectable gap.  Guarded by
; *gf-methods-lock* in lib/clos.lisp.
(defgeneric amdisp-writer-gf (x))
(check "concurrent add-method of distinct methods loses none" 0
  (let ((installers nil) (missing 0) (n-per-writer 300))
    (dotimes (w 2)
      (push (mp:make-thread
              (let ((base (* w n-per-writer)))
                (lambda ()
                  (dotimes (i n-per-writer)
                    (let ((n (+ base i)))
                      (eval `(defmethod amdisp-writer-gf ((x (eql ,n))) ,n))))
                  :ok))
              :name "writer")
            installers))
    (dolist (thr installers) (mp:join-thread thr))
    (dotimes (n (* 2 n-per-writer))
      (let ((v (handler-case (amdisp-writer-gf n) (error () :fail))))
        (unless (eql v n) (setq missing (1+ missing)))))
    missing))

; Regression: concurrent SLOW-PATH dispatch must not corrupt a generic
; function's dispatch-cache hash table.  A GF's dispatch cache is read AND
; written from multiple threads (main loader + sento/log4cl workers dispatching
; the same GF).  It was a plain lock-free hash table: two threads splicing new
; entries onto the same bucket, or one rehashing (relinking every cell into a
; doubled bucket vector) while another walks it, could cross-link the chains and
; crash rarely under contention.  Fixed by creating dispatch caches with
; %MAKE-SYNC-HASH-TABLE (lib/clos.lisp %make-dispatch-cache), whose access is
; serialized (CL_HT_FLAG_SYNC in builtins_hashtable.c) with an allocation-free
; critical section — so only the slow path pays, and the monomorphic inline
; cache fast path stays lock-free.  Many threads dispatch one polymorphic GF
; over many classes, refilling a fresh (empty) cache each round to maximise the
; splice/rehash contention window.  A corrupt table shows up as a crash (the
; FS-UAE watchdog kills the run) or a wrong/errored dispatch result.
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
(check "concurrent dispatch does not corrupt the dispatch cache" 0
  (let ((fails 0))
    (dotimes (round 4)
      ;; Fresh GF each round → empty dispatch cache, refilled concurrently.
      (eval '(defgeneric amcache-dispatch (x)))
      (dotimes (i 24)
        (eval `(defmethod amcache-dispatch ((x ,(aref *amcache-classes* i))) ,i)))
      (let ((workers nil))
        (dotimes (w 4)
          (push (mp:make-thread
                  (lambda ()
                    (let ((bad 0))
                      (dotimes (p 20)
                        (dotimes (i 24)
                          (let ((r (handler-case
                                       (amcache-dispatch (aref *amcache-instances* i))
                                     (error () :fail))))
                            (unless (eql r i) (setq bad (1+ bad))))))
                      bad))
                  :name "amcache-disp")
                workers))
        (dolist (thr workers) (setq fails (+ fails (mp:join-thread thr))))))
    fails))

; --- Thread yield ---
(check "thread-yield no crash" nil
  (mp:thread-yield))

; --- Lock operations ---
(check "make-lock" t
  (not (null (mp:make-lock "test-lock"))))

(check "lock acquire and release" t
  (let ((lk (mp:make-lock)))
    (mp:acquire-lock lk)
    (mp:release-lock lk)
    t))

(check "lock trylock" t
  (let ((lk (mp:make-lock)))
    (mp:acquire-lock lk nil)))

(check "with-lock-held" 42
  (let ((lk (mp:make-lock)))
    (mp:with-lock-held (lk)
      42)))

; --- Lock contention (two threads) ---
(check "lock contention two threads" 200
  (let ((counter (list 0))
        (lk (mp:make-lock)))
    (let ((t1 (mp:make-thread
                (lambda ()
                  (dotimes (i 100)
                    (mp:with-lock-held (lk)
                      (setf (car counter) (+ (car counter) 1)))))))
          (t2 (mp:make-thread
                (lambda ()
                  (dotimes (i 100)
                    (mp:with-lock-held (lk)
                      (setf (car counter) (+ (car counter) 1))))))))
      (mp:join-thread t1)
      (mp:join-thread t2)
      (car counter))))

; --- Condition variables ---
(check "make-condition-variable" t
  (not (null (mp:make-condition-variable))))

(check "condvar notify/wait" t
  (let ((lk (mp:make-lock))
        (cv (mp:make-condition-variable))
        (ready (list nil)))
    (let ((consumer (mp:make-thread
                      (lambda ()
                        (mp:acquire-lock lk)
                        (loop until (car ready)
                              do (mp:condition-wait cv lk))
                        (mp:release-lock lk)
                        (car ready)))))
      (mp:thread-yield)
      (mp:acquire-lock lk)
      (setf (car ready) t)
      (mp:condition-notify cv)
      (mp:release-lock lk)
      (mp:join-thread consumer))))

; --- Dynamic binding isolation ---
(check "thread sees global package value" "COMMON-LISP-USER"
  (mp:join-thread
    (mp:make-thread
      (lambda () (package-name *package*)))))

(defvar *thread-test-var* :parent)
(check "thread let binding isolation" '(:parent :child)
  (let ((child (mp:make-thread
                 (lambda ()
                   (let ((*thread-test-var* :child))
                     *thread-test-var*)))))
    (let ((child-result (mp:join-thread child)))
      (list *thread-test-var* child-result))))

; Regression: thread spawned inside an active LET sees only the GLOBAL value,
; never the parent's dynamic binding (fresh per-thread dynamic environment).
(defvar *fresh-dyn-var* :global)
(check "thread fresh dynamic env" :global
  (let ((*fresh-dyn-var* :bound-in-parent))
    (mp:join-thread
      (mp:make-thread
        (lambda () *fresh-dyn-var*)))))

; --- Error handling in threads ---
(check "thread error sets aborted" :survived
  (let ((thr (mp:make-thread
               (lambda () (error "boom")))))
    (mp:join-thread thr)
    :survived))

; --- Multiple concurrent threads ---
(check "multiple concurrent threads" '(10 20 30 40)
  (let ((threads (list
                   (mp:make-thread (lambda () (* 1 10)))
                   (mp:make-thread (lambda () (* 2 10)))
                   (mp:make-thread (lambda () (* 3 10)))
                   (mp:make-thread (lambda () (* 4 10))))))
    (mapcar #'mp:join-thread threads)))

; --- Tier-4 batch 6: interrupt/destroy delivery to PARKED threads (I5) ---
; Pre-fix, interrupts were delivered only at safepoints; a target parked in
; mp:condition-wait (never notified) or a blocking mp:acquire-lock never ran
; another safepoint, so mp:destroy-thread of it was deferred FOREVER and the
; destroyer's join hung (the FS-UAE watchdog would kill the whole suite).
(check "t4b6 destroy thread parked in condition-wait" :done
  (let* ((lk (mp:make-lock))
         (cv (mp:make-condition-variable))
         (th (mp:make-thread
               (lambda ()
                 (mp:acquire-lock lk)
                 (loop (mp:condition-wait cv lk))))))
    (sleep 0.4)
    (mp:destroy-thread th)
    (handler-case (mp:join-thread th) (error () nil))
    :done))

(check "t4b6 destroy thread parked in acquire-lock" :done
  (let ((lk (mp:make-lock)))
    (mp:acquire-lock lk)
    (let ((th (mp:make-thread (lambda () (mp:acquire-lock lk) :got))))
      (sleep 0.4)
      (mp:destroy-thread th)
      (handler-case (mp:join-thread th) (error () nil))
      (mp:release-lock lk))
    :done))

; T1 (batch 6a): operations on a wrapper for an exited thread refuse cleanly
; (generation counter) — join errors, alive-p is NIL, interrupt errors.
(check "t4b6 stale wrapper join errors after exit" '(:err nil :err)
  (let ((th (mp:make-thread (lambda () 1))))
    (mp:join-thread th)
    (list (handler-case (progn (mp:join-thread th) :joined)
            (error () :err))
          (mp:thread-alive-p th)
          (handler-case (progn (mp:interrupt-thread th (lambda () nil)) :sent)
            (error () :err)))))

; Tier-4 follow-up: contended blocking acquire-lock parks on the shared
; lock-parking condvar (broadcast by release-lock) instead of sleep-polling
; on a 10ms grid — the poll grid collapsed sento message throughput ~6x on
; host.  No timing assertion here (14MHz 68020, 20ms Delay ticks): this
; exercises the park/broadcast path functionally — waiters must all be
; woken (no lost broadcast = no hang; the FS-UAE watchdog catches one) and
; mutual exclusion must hold under work done inside the critical section.
(check "t4-lockpark contended waiters all woken, counter exact" 300
  (let ((lk (mp:make-lock))
        (counter (list 0)))
    (let ((workers
            (let ((acc nil))
              (dotimes (i 3 (nreverse acc))
                (push (mp:make-thread
                        (lambda ()
                          (dotimes (j 100)
                            (mp:acquire-lock lk)
                            (let ((work 0))
                              (dotimes (k 50) (setq work (+ work k)))
                              (setf (car counter)
                                    (+ 1 (car counter) (* 0 work))))
                            (mp:release-lock lk))))
                      acc)))))
      (dolist (th workers) (mp:join-thread th)))
    (car counter)))

; I8 (batch 6b): dropping the last reference to a HELD lock leaks the OS
; mutex instead of destroying it under the holder — must not crash/Guru.
(check "t4b6 held lock finalize leaks not destroys" :ok
  (progn (let ((l (mp:make-lock))) (mp:acquire-lock l))
         (gc) (gc)
         :ok))

; ST5 (batch 6a): closing a synonym stream whose symbol holds a NON-stream
; must not scribble stream fields into the object.
(defvar *t4b6-not-a-stream* (copy-seq "keep me intact"))
(check "t4b6 synonym close to non-stream is safe" t
  (progn (handler-case (close (make-synonym-stream '*t4b6-not-a-stream*))
           (error () nil))
         (string= *t4b6-not-a-stream* "keep me intact")))

; NOTE: the GC-safe-region fix for the "STW GC deadlocks behind a thread parked
; in a blocking socket syscall" bug (the SLY :spawn read-loop deadlock) is
; regression-tested at the C level in tests/test_gc_threaded.c
; (stw_gc_with_thread_blocked_in_accept).  It deliberately is NOT tested here:
; reproducing it needs one task blocked in socket-accept while another runs GC,
; but AmigaOS bsdsocket.library is per-Task — a worker task cannot accept() on a
; listener owned by the main task (the runtime keeps a single global SocketBase),
; so such a test would itself hang.  See the "single-threaded loopback pattern"
; note on the socket tests above.

; --- Named condition variables ---
(check "make-condition-variable with name" t
  (not (null (mp:make-condition-variable "my-cv"))))

(check "condition-name accessor" "test-cv"
  (mp:condition-name (mp:make-condition-variable "test-cv")))

(check "condition-name nil when unnamed" nil
  (mp:condition-name (mp:make-condition-variable)))

; --- Lock name accessor ---
(check "lock-name accessor" "my-lock"
  (mp:lock-name (mp:make-lock "my-lock")))

(check "lock-name nil when unnamed" nil
  (mp:lock-name (mp:make-lock)))

; --- macroexpand-1 returns two values; accepts optional env ---
(defmacro mx1amiga (x) `(* ,x 10))
(check "macroexpand-1 returns two values"
  '((* 3 10) t)
  (multiple-value-list (macroexpand-1 '(mx1amiga 3))))
(check "macroexpand-1 accepts env arg"
  '((* 3 10) t)
  (multiple-value-list (macroexpand-1 '(mx1amiga 3) nil)))
(check "macroexpand non-macro"
  '((+ 1 2) nil)
  (multiple-value-list (macroexpand-1 '(+ 1 2))))

; --- subtypep / typep accept optional env (CLHS) ---
(check "subtypep with env" t (nth-value 0 (subtypep 'fixnum 'integer nil)))
(check "typep with env" t (typep 5 'integer nil))

; --- LOOP: OF-TYPE after INTO (CLHS 6.1.3.1) ---
(check "loop sum into of-type fixnum" 55
  (loop for i from 1 to 10 sum i into total of-type fixnum
        finally (return total)))

; --- Reader ,. (nconc-splice) — iterate/trivia need this in macros ---
(check "quasi splice ,. basic" '(a 1 2 3 b)
  (let ((xs '(1 2 3))) `(a ,.xs b)))
(check "quasi splice ,. twice" '(1 2 3 4)
  `(,.'(1 2) ,.'(3 4)))

; --- special-operator-p (CLHS) ---
(check "special-operator-p quote" t (special-operator-p 'quote))
(check "special-operator-p if" t (special-operator-p 'if))
(check "special-operator-p tagbody" t (special-operator-p 'tagbody))
(check "special-operator-p car" nil (special-operator-p 'car))
(check "special-operator-p undefined" nil (special-operator-p 'no-such-sym))

; --- EXT:FUNCTION-ARGLIST (Sly arglist introspection) ---
; Path B: source-compiled functions return their exact written lambda-list
; (captured on the bytecode, survives FASL).  Path A: C builtins get a
; reconstructed lambda-list with #:ARGn placeholders.
(defun al-amiga (a b &optional (c 3) &rest more &key kw) (list a b c more kw))
(check "function-arglist defun source"
  '(a b &optional (c 3) &rest more &key kw)
  (ext:function-arglist #'al-amiga))
(check "function-arglist lambda"
  '(x y) (ext:function-arglist (lambda (x y) (+ x y))))
(check "function-arglist empty lambda-list"
  nil (ext:function-arglist (lambda () 1)))
(check "function-arglist builtin arity"
  1 (length (ext:function-arglist #'car)))
(check "function-arglist builtin variadic"
  '&rest (car (ext:function-arglist #'list)))
(check "function-arglist not-available"
  :not-available (ext:function-arglist 42))

; --- EXT:FUNCTION-SOURCE-LOCATION (Sly find-definitions / M-.) ---
; This suite is loaded from a file, so functions defined here carry a
; source location: (FILE LINE), FILE a namestring string, LINE a fixnum.
; REPL/builtins/non-functions return :not-available.
(defun sl-amiga (x) x)
(check "function-source-location is a list"
  t (consp (ext:function-source-location #'sl-amiga)))
(check "function-source-location file is a string"
  t (stringp (first (ext:function-source-location #'sl-amiga))))
(check "function-source-location line is an integer"
  t (integerp (second (ext:function-source-location #'sl-amiga))))
(check "function-source-location builtin not-available"
  :not-available (ext:function-source-location #'car))
(check "function-source-location non-function not-available"
  :not-available (ext:function-source-location 42))

; --- EXT:BACKTRACE / EXT:FRAME-LOCALS (Sly SLDB backend) ---
; Each call is non-tail (wrapped in a LET initform) so the VM's tail-call
; optimization does not collapse the intermediate frames.  Backtrace entries
; are (INDEX NAME FILE LINE), innermost first; frame-locals returns
; (PLACEHOLDER-NAME . VALUE) pairs.
; JIT shadow frames are opt-in (they cost a few % on call-heavy code), so a
; debug/introspection session enables them.  Turn them on for this section so
; EXT:BACKTRACE / EXT:FRAME-LOCALS can see the JIT'd bt-* calls; restore after.
; No-op on host / --no-jit (no native frames to begin with).
(clamiga::%jit-set-frames t)
(defun bt-c (z) (declare (ignore z)) (ext:backtrace))
(defun bt-b (y) (let ((r (bt-c (* y 2)))) r))
(defun bt-a (x) (let ((r (bt-b (+ x 1)))) r))
(defparameter *amiga-bt* (bt-a 5))
(check "backtrace is a list" t (consp *amiga-bt*))
(check "backtrace innermost index is 0" 0 (first (first *amiga-bt*)))
(check "backtrace innermost frame name" "BT-C"
  (symbol-name (second (first *amiga-bt*))))
(check "backtrace caller frame name" "BT-B"
  (symbol-name (second (second *amiga-bt*))))

(defun bt-locals (a b) (let ((c 77)) (ext:frame-locals 0)))
(defparameter *amiga-locals* (bt-locals 3 4))
(check "frame-locals is a list" t (consp *amiga-locals*))
(check "frame-locals exposes argument value" t
  (and (member 3 (mapcar #'cdr *amiga-locals*)) t))
; Interior LET-bound locals live on the m68k operand stack inside JIT'd code
; and are not introspectable; only the argument slots are recoverable there.
; In the bytecode interpreter (host, or --no-jit) all locals are visible.
(check "frame-locals exposes let-bound value" t
  (or (clamiga::%jit-active-p)
      (and (member 77 (mapcar #'cdr *amiga-locals*)) t)))
(check "frame-locals out-of-range index" :not-available
  (ext:frame-locals 9999))
(check "jit shadow frames toggle off" nil (clamiga::%jit-set-frames nil))

; --- documentation is a generic function ---
; Storage via (setf documentation) + retrieval; adding a specialized
; method for a user-defined doc-type must NOT break the (t t) fallback.
(progn
  (setf (documentation 'amiga-doc-sym 'function) "hello")
  (defmethod documentation ((x symbol) (type (eql 'amiga-ns))) 'specialized))
(check "documentation fallback retains string"
  "hello" (documentation 'amiga-doc-sym 'function))
(check "documentation specialized method used"
  'specialized (documentation 'amiga-doc-sym 'amiga-ns))

; --- Type predicates ---
(check "threadp true" t
  (mp:threadp (mp:current-thread)))

(check "threadp false" nil
  (mp:threadp 42))

(check "lockp true" t
  (mp:lockp (mp:make-lock)))

(check "lockp false" nil
  (mp:lockp "not-a-lock"))

(check "condition-variable-p true" t
  (mp:condition-variable-p (mp:make-condition-variable)))

(check "condition-variable-p false" nil
  (mp:condition-variable-p nil))

; --- interrupt-thread ---
(check "interrupt-thread self" 42
  (let ((x 0))
    (mp:interrupt-thread (mp:current-thread)
      (lambda () (setf x 42)))
    x))

(check "interrupt-thread basic" :interrupted
  (let ((flag (list nil))
        (lk (mp:make-lock))
        (cv (mp:make-condition-variable)))
    (let ((thr (mp:make-thread
                 (lambda ()
                   (mp:acquire-lock lk)
                   (loop until (car flag)
                         do (mp:condition-wait cv lk))
                   (mp:release-lock lk)
                   (car flag)))))
      (mp:thread-yield)
      (mp:interrupt-thread thr
        (lambda () (setf (car flag) :interrupted)))
      (mp:acquire-lock lk)
      (mp:condition-notify cv)
      (mp:release-lock lk)
      (mp:join-thread thr))))

; --- destroy-thread ---
(check "destroy-thread basic" :destroyed
  (let ((thr (mp:make-thread
               (lambda ()
                 (loop (mp:thread-yield))))))
    (mp:thread-yield)
    (mp:destroy-thread thr)
    (mp:join-thread thr)
    :destroyed))

(check "destroy-thread not alive after" nil
  (let ((thr (mp:make-thread
               (lambda ()
                 (loop (mp:thread-yield))))))
    (mp:thread-yield)
    (mp:destroy-thread thr)
    (mp:join-thread thr)
    (mp:thread-alive-p thr)))

; --- GC stress under threads ---
(check "thread GC stress" 400
  (let ((t1 (mp:make-thread
              (lambda () (length (make-list 200)))))
        (t2 (mp:make-thread
              (lambda () (length (make-list 200))))))
    (+ (mp:join-thread t1) (mp:join-thread t2))))

; --- Thread top-level ABORT restart ---
; Every worker thread has a top-level ABORT restart whose :report is
; "Return to top level" (matching SBCL/CCL).  PRINC / ~A yields the
; report string; PRIN1 / ~S yields the escaped #<RESTART ABORT> form.
(check "thread top abort restart princ shows report" "Return to top level"
  (mp:join-thread (mp:make-thread
    (lambda () (princ-to-string (find-restart 'abort))))))
(check "thread top abort restart prin1 unchanged" "#<RESTART ABORT>"
  (mp:join-thread (mp:make-thread
    (lambda () (prin1-to-string (find-restart 'abort))))))

; --- MP:DUMP-THREAD-WAITS ---
(check "dump-thread-waits returns nil single-threaded" nil
  (mp:dump-thread-waits))

(check "dump-thread-waits returns nil with live worker" nil
  (let* ((lk (mp:make-lock))
         (cv (mp:make-condition-variable))
         (ready (list nil))
         (thr (mp:make-thread
                (lambda ()
                  (mp:acquire-lock lk)
                  (setf (car ready) t)
                  (mp:condition-wait cv lk)
                  (mp:release-lock lk)))))
    (mp:acquire-lock lk)
    (loop until (car ready) do (mp:release-lock lk) (mp:thread-yield) (mp:acquire-lock lk))
    (let ((result (mp:dump-thread-waits)))
      (mp:condition-notify cv)
      (mp:release-lock lk)
      (mp:join-thread thr)
      result)))

; --- FFI (generic) ---
(require "ffi")

(check "ffi-package-exists" "FFI" (package-name (find-package "FFI")))
(check "ffi-make-foreign-pointer" 'foreign-pointer (type-of (ffi:make-foreign-pointer 42)))
(check "ffi-foreign-pointer-p" t (ffi:foreign-pointer-p (ffi:make-foreign-pointer 0)))
(check "ffi-foreign-pointer-p-int" nil (ffi:foreign-pointer-p 42))
(check "ffi-null-pointer-p-zero" t (ffi:null-pointer-p (ffi:make-foreign-pointer 0)))
(check "ffi-null-pointer-p-nonzero" nil (ffi:null-pointer-p (ffi:make-foreign-pointer 1)))
(check "ffi-foreign-pointer-address" 42 (ffi:foreign-pointer-address (ffi:make-foreign-pointer 42)))
(check "ffi-alloc-free" nil
  (let ((p (ffi:alloc-foreign 64)))
    (prog1 (ffi:null-pointer-p p)
      (ffi:free-foreign p))))
(check "ffi-peek-poke-u32" 12345
  (let ((p (ffi:alloc-foreign 16)))
    (ffi:poke-u32 p 12345)
    (prog1 (ffi:peek-u32 p)
      (ffi:free-foreign p))))
(check "ffi-peek-poke-u32-offset" 300
  (let ((p (ffi:alloc-foreign 16)))
    (ffi:poke-u32 p 100 0)
    (ffi:poke-u32 p 200 4)
    (prog1 (+ (ffi:peek-u32 p 0) (ffi:peek-u32 p 4))
      (ffi:free-foreign p))))
(check "ffi-peek-poke-u16" 1234
  (let ((p (ffi:alloc-foreign 16)))
    (ffi:poke-u16 p 1234)
    (prog1 (ffi:peek-u16 p)
      (ffi:free-foreign p))))
(check "ffi-peek-poke-u8" 255
  (let ((p (ffi:alloc-foreign 16)))
    (ffi:poke-u8 p 255)
    (prog1 (ffi:peek-u8 p)
      (ffi:free-foreign p))))
;;; Bulk byte transfer — the path the Lambda's Tale tile loader uses to
;;; push bitplane rows into a BitMap and cookie-cut masks into chip RAM.
(check "ffi-poke-bytes-vector" '(4 1 2 254 255)
  (let ((p (ffi:alloc-foreign 16))
        (v (make-array 4 :element-type '(unsigned-byte 8)
                         :initial-contents '(1 2 254 255))))
    (let ((n (ffi:poke-bytes p v)))
      (prog1 (list n (ffi:peek-u8 p 0) (ffi:peek-u8 p 1)
                   (ffi:peek-u8 p 2) (ffi:peek-u8 p 3))
        (ffi:free-foreign p)))))
(check "ffi-poke-bytes-string" '(2 65 66)
  (let ((p (ffi:alloc-foreign 16)))
    (let ((n (ffi:poke-bytes p "AB" 8)))
      (prog1 (list n (ffi:peek-u8 p 8) (ffi:peek-u8 p 9))
        (ffi:free-foreign p)))))
(check "ffi-poke-bytes-offset-start-end" '(2 2 254)
  (let ((p (ffi:alloc-foreign 16))
        (v (make-array 4 :element-type '(unsigned-byte 8)
                         :initial-contents '(1 2 254 255))))
    (let ((n (ffi:poke-bytes p v 12 1 3)))
      (prog1 (list n (ffi:peek-u8 p 12) (ffi:peek-u8 p 13))
        (ffi:free-foreign p)))))
(check "ffi-peek-bytes-roundtrip" '(3 (9 0 200))
  (let ((p (ffi:alloc-foreign 16))
        (v (make-array 3 :element-type '(unsigned-byte 8)
                         :initial-contents '(9 0 200)))
        (out (make-array 3 :element-type '(unsigned-byte 8)
                           :initial-element 0)))
    (ffi:poke-bytes p v)
    (let ((n (ffi:peek-bytes p out)))
      (prog1 (list n (coerce out 'list))
        (ffi:free-foreign p)))))
(check "ffi-poke-bytes-overrun-rejected" :rejected
  (let ((p (ffi:alloc-foreign 4))
        (v (make-array 4 :element-type '(unsigned-byte 8) :initial-element 1)))
    (prog1 (handler-case (progn (ffi:poke-bytes p v 2) :no-error)
             (error () :rejected))
      (ffi:free-foreign p))))
(check "ffi-poke-bytes-element-range-rejected" '(:rejected :rejected)
  (let ((p (ffi:alloc-foreign 16)))
    (prog1 (list (handler-case (progn (ffi:poke-bytes p (vector 1 300)) :no-error)
                   (error () :rejected))
                 (handler-case (progn (ffi:poke-bytes p (vector 1 -1)) :no-error)
                   (error () :rejected)))
      (ffi:free-foreign p))))
(check "ffi-peek-bytes-overrun-rejected" :rejected
  (let ((p (ffi:alloc-foreign 4))
        (out (make-array 4 :element-type '(unsigned-byte 8) :initial-element 0)))
    (prog1 (handler-case (progn (ffi:peek-bytes p out 2) :no-error)
             (error () :rejected))
      (ffi:free-foreign p))))
(check "ffi-poke-bytes-rank-rejected" :rejected
  (let ((p (ffi:alloc-foreign 16))
        (v (make-array '(2 2) :element-type '(unsigned-byte 8) :initial-element 1)))
    (prog1 (handler-case (progn (ffi:poke-bytes p v) :no-error)
             (error () :rejected))
      (ffi:free-foreign p))))
(check "ffi-peek-bytes-rank-rejected" :rejected
  (let ((p (ffi:alloc-foreign 16))
        (out (make-array '(2 2) :element-type '(unsigned-byte 8) :initial-element 0)))
    (prog1 (handler-case (progn (ffi:peek-bytes p out) :no-error)
             (error () :rejected))
      (ffi:free-foreign p))))
(check "ffi-foreign-string-roundtrip" "hello"
  (let ((p (ffi:foreign-string "hello")))
    (prog1 (ffi:foreign-to-string p)
      (ffi:free-foreign p))))
(check "ffi-pointer-plus" 120
  (ffi:foreign-pointer-address (ffi:pointer+ (ffi:make-foreign-pointer 100) 20)))
(check "ffi-with-foreign-alloc" 42
  (ffi:with-foreign-alloc (p 16)
    (ffi:poke-u32 p 42)
    (ffi:peek-u32 p)))
(check "ffi-defcstruct" '(100 200)
  (progn
    (ffi:defcstruct test-point (x :u16 0) (y :u16 2))
    (ffi:with-foreign-alloc (p 4)
      (setf (test-point-x p) 100)
      (setf (test-point-y p) 200)
      (list (test-point-x p) (test-point-y p)))))

; --- FFI typed peek/poke (signed, 64-bit, float/double, pointer) ---
(check "ffi-peek-poke-i8" -5
  (let ((p (ffi:alloc-foreign 8))) (ffi:poke-i8 p -5)
    (prog1 (ffi:peek-i8 p) (ffi:free-foreign p))))
(check "ffi-peek-poke-i16" -1000
  (let ((p (ffi:alloc-foreign 8))) (ffi:poke-i16 p -1000)
    (prog1 (ffi:peek-i16 p) (ffi:free-foreign p))))
(check "ffi-peek-poke-i32" -123456
  (let ((p (ffi:alloc-foreign 8))) (ffi:poke-i32 p -123456)
    (prog1 (ffi:peek-i32 p) (ffi:free-foreign p))))
(check "ffi-peek-poke-u64" 4294967300
  (let ((p (ffi:alloc-foreign 8))) (ffi:poke-u64 p 4294967300)
    (prog1 (ffi:peek-u64 p) (ffi:free-foreign p))))
(check "ffi-peek-poke-i64" -5000000000
  (let ((p (ffi:alloc-foreign 8))) (ffi:poke-i64 p -5000000000)
    (prog1 (ffi:peek-i64 p) (ffi:free-foreign p))))
(check "ffi-peek-poke-single" 2.5
  (let ((p (ffi:alloc-foreign 8))) (ffi:poke-single p 2.5)
    (prog1 (ffi:peek-single p) (ffi:free-foreign p))))
(check "ffi-peek-poke-double" 6.25d0
  (let ((p (ffi:alloc-foreign 8))) (ffi:poke-double p 6.25d0)
    (prog1 (ffi:peek-double p) (ffi:free-foreign p))))
(check "ffi-peek-poke-pointer" t
  (let ((p (ffi:alloc-foreign 8))) (ffi:poke-pointer p p)
    (prog1 (ffi:pointer-eq p (ffi:peek-pointer p)) (ffi:free-foreign p))))
(check "ffi-pointer-eq" t
  (let* ((p (ffi:alloc-foreign 8)) (a (ffi:foreign-pointer-address p)))
    (prog1 (ffi:pointer-eq p (ffi:make-foreign-pointer a)) (ffi:free-foreign p))))
(check "ffi-pointer-eq-diff" nil
  (ffi:pointer-eq (ffi:make-foreign-pointer 16) (ffi:make-foreign-pointer 32)))

; --- FFI (Amiga-specific) ---
#+amigaos (require "amiga/ffi")
#+amigaos
(progn
  ; --- m68k template JIT (encoders + native dispatch + behavior).
  ; Lives in a sibling file so JIT coverage can grow without bloating
  ; this file, and so it can be loaded in isolation while iterating. ---
  (handler-case
    (load "tests/amiga/test-jit.lisp")
    (error (e) (format t "ERROR loading JIT tests: ~A~%" e)))

  (check "amiga-package-exists" "AMIGA" (package-name (find-package "AMIGA")))
  (check "amiga-open-close-library" t
    (let ((lib (amiga:open-library "dos.library" 36)))
      (prog1 (not (null lib))
        (amiga:close-library lib))))
  ;; ExecBase is at absolute address 4 on all Amigas
  (check "amiga-peek-execbase" t
    (let ((p (ffi:make-foreign-pointer 4)))
      (> (ffi:peek-u32 p) 0)))
  (check "amiga-alloc-chip" t
    (let ((p (amiga:alloc-chip 256)))
      (prog1 (not (ffi:null-pointer-p p))
        (amiga:free-chip p))))

  ;; CALL-LIBRARY-FAST: dos.library IoErr() -> LVO -132, no register args.
  ;; Returns a fixnum (the last I/O error code, typically 0). The point of
  ;; this test is to exercise the zero-arg path and confirm fixnum return.
  (check "amiga-call-library-fast-zero-args" t
    (let ((lib (amiga:open-library "dos.library" 36)))
      (prog1 (integerp (amiga:call-library-fast lib -132 0))
        (amiga:close-library lib))))

  ;; CALL-LIBRARY-FAST and CALL-LIBRARY must agree for the same call.
  (check "amiga-call-library-fast-matches-slow" t
    (let ((lib (amiga:open-library "dos.library" 36)))
      (prog1
        (= (amiga:call-library-fast lib -132 0)
           (amiga:call-library lib -132 nil))
        (amiga:close-library lib))))

  ;; DEFCFUN regspec encoding: (:a1 :d0 :d1) -> 9 | (0<<4) | (1<<8) = #x109.
  ;; macroexpand-1 produces (progn (defun NAME PARAMS
  ;;   (amiga:%ffi-call BASE OFFSET REGSPEC . PARAMS))
  ;;   (define-compiler-macro NAME ...) 'NAME) — the compiler
  ;; matches the %FFI-CALL head exactly to emit OP_AMIGA_CALL.  We walk
  ;; the expansion structurally rather than string-searching the printed
  ;; form, so the test is robust to printer formatting.
  (check "amiga-defcfun-regspec-encoding" 265   ; #x109
    (let* ((expanded (macroexpand-1
                       '(amiga.ffi:defcfun foo *base* -42
                                           (:a1 r :d0 x :d1 y))))
           (defun-form (second expanded))       ; (defun NAME PARAMS BODY)
           (call-form (fourth defun-form)))     ; the %FFI-CALL form
      (and (eq (first call-form) 'amiga:%ffi-call)
           (fourth call-form))))                ; regspec literal

  ;; DEFCFUN :VOID T sets bit 28 of regspec (#x10000000 = 268435456).
  ;; (:d0 x) alone encodes to 0; with :void t it becomes #x10000000.
  (check "amiga-defcfun-void-regspec-encoding" #x10000000
    (let* ((expanded (macroexpand-1
                       '(amiga.ffi:defcfun foo *base* -42 (:d0 x) :void t)))
           (defun-form (second expanded))
           (call-form (fourth defun-form)))
      (and (eq (first call-form) 'amiga:%ffi-call)
           (fourth call-form))))

  ;; DEFCFUN with an empty regspec — regspec literal is 0, the defined
  ;; function takes no parameters, and no register args follow.
  (check "amiga-defcfun-empty-regspec" '(0 nil nil)
    (let* ((expanded (macroexpand-1
                       '(amiga.ffi:defcfun foo *base* -132 ())))
           (defun-form (second expanded))
           (params (third defun-form))          ; defun lambda list
           (call-form (fourth defun-form)))
      (list (fourth call-form)                  ; regspec
            params                              ; () == NIL
            (nthcdr 4 call-form))))             ; trailing args (none)

  ;; DEFCFUN end-to-end via the compiled %FFI-CALL → OP_AMIGA_CALL path.
  ;; Wrap dos.library IoErr (LVO -132, no register args) and verify it
  ;; agrees with the runtime CALL-LIBRARY-FAST path on the same call.
  (defparameter *dos-base-for-defcfun-test* nil)
  (amiga.ffi:defcfun dos-io-err *dos-base-for-defcfun-test* -132 ())
  (check "amiga-defcfun-end-to-end" t
    (let ((lib (amiga:open-library "dos.library" 36)))
      (setq *dos-base-for-defcfun-test* lib)
      (prog1
        (= (dos-io-err)
           (amiga:call-library-fast lib -132 0))
        (amiga:close-library lib))))

  ; --- Intuition/Graphics/GadTools tests ---
  ; These are in a separate file because the reader needs the packages
  ; to exist before it can read amiga.intuition:* qualified symbols.
  (handler-case
    (load "tests/amiga/test-gui.lisp")
    (error (e) (format t "ERROR loading GUI tests: ~A~%" e))))

; --- Gray streams: (typep gray-stream 'stream) regression ---
; Load gray-streams.lisp and verify that a CLOS-based Gray stream instance
; is recognised as a STREAM by TYPEP (fix for the root cause of the Slynk
; SLDB bug where %%condition-message leaked output to the terminal).
;
; gray-streams.lisp MUST be loaded in its own top-level form, BEFORE the
; test body below.  The reader reads each top-level form in full before
; evaluating it, so the GRAY package has to already exist when the reader
; reaches the gray:-qualified symbols (gray:fundamental-output-stream, ...).
; Putting the (load ...) and the gray:-symbols in one combined form fails
; at READ time with "Package GRAY not found".  (Same read-time-package
; pitfall already handled for amiga/ffi and the GUI tests above.)
(handler-case
  (load "lib/gray-streams.lisp")
  (error (e) (format t "ERROR loading gray-streams: ~A~%" e)))

(handler-case
  (progn
    (defclass test-gs-out (gray:fundamental-output-stream) ())
    (defmethod gray:stream-write-char ((s test-gs-out) c) (declare (ignore s c)) nil)
    (let ((g (make-instance 'test-gs-out)))
      (check "gray streamp"           t   (streamp g))
      (check "gray output-stream-p"   t   (output-stream-p g))
      (check "gray output not input-stream-p" nil (input-stream-p g))
      (check "gray typep stream"      t   (typep g 'stream))
      (check "gray typep not fixnum"  nil (typep 42 'stream))
      (check "native typep stream"    t   (typep (make-string-output-stream) 'stream))
      ; Negative subtypes: a Gray stream must not match built-in stream subtypes.
      (check "gray not file-stream"    nil (typep g 'file-stream))
      (check "gray not string-stream"  nil (typep g 'string-stream))
      (check "gray not synonym-stream" nil (typep g 'synonym-stream)))
    ; Verify fundamental-input-stream subclass is also recognised as a STREAM.
    (defclass test-gs-in (gray:fundamental-input-stream) ())
    (defmethod gray:stream-read-char ((s test-gs-in)) :eof)
    (let ((gi (make-instance 'test-gs-in)))
      (check "gray input streamp"         t   (streamp gi))
      (check "gray input typep stream"    t   (typep gi 'stream))
      (check "gray input input-stream-p"  t   (input-stream-p gi))
      (check "gray input not output-stream-p" nil (output-stream-p gi)))
    ; --- item-2: printer functions route to Gray streams ---
    (defclass test-gs-cap (gray:fundamental-character-output-stream)
      ((buf :initform nil)))
    (defmethod gray:stream-write-char ((s test-gs-cap) c)
      (setf (slot-value s 'buf) (cons c (slot-value s 'buf))))
    (defun gs-flush (s)
      (let ((str (coerce (nreverse (slot-value s 'buf)) 'string)))
        (setf (slot-value s 'buf) nil)
        str))
    (let ((g (make-instance 'test-gs-cap)))
      (check "gray princ"       "hello"   (progn (princ "hello" g) (gs-flush g)))
      (check "gray prin1"       "\"hi\""  (progn (prin1 "hi" g)    (gs-flush g)))
      (check "gray write"       "\"hi\""  (progn (write "hi" :stream g) (gs-flush g)))
      (check "gray format"      "42"      (progn (format g "~A" 42) (gs-flush g)))
      (check "gray format nil"  "99"      (format nil "~A" 99))
      (check "gray print"
             (concatenate 'string (string #\Newline) "42 ")
             (progn (print 42 g) (gs-flush g)))
      (check "gray pprint"      t         (progn (pprint '(a b c) g) (> (length (gs-flush g)) 0)))
      (handler-case (error "test-error-msg")
        (error (e)
          (princ e g)
          (check "gray princ condition" t (> (length (gs-flush g)) 0))))
      ; Regression: the Gray printing fallbacks (princ/prin1/print/write/
      ; format/pprint) capture through a temp string-output-stream that must
      ; be CLOSEd — the old code never closed it, leaking one outbuf slot per
      ; call until the next GC (under logging-heavy loads that saturated the
      ; table and turned printing into a per-node full-GC storm).  50 rounds
      ; x 4 calls = 200 slots if leaked; eagerly closed, the occupancy stays
      ; within nesting depth of where it started.
      (check "gray printing fallbacks free outbufs eagerly" t
             (let ((before (car (ext:%stream-outbuf-stats))))
               (dotimes (i 50)
                 (princ i g)
                 (prin1 i g)
                 (format g "x~D" i)
                 (write i :stream g))
               (gs-flush g)
               (< (- (car (ext:%stream-outbuf-stats)) before) 10))))
    ; --- item-2 continued: Gray two-way stream ---
    (defclass test-gs-qin (gray:fundamental-character-input-stream)
      ((chars :initarg :chars :initform nil)))
    (defmethod gray:stream-read-char ((s test-gs-qin))
      (let ((q (slot-value s 'chars)))
        (if q (progn (setf (slot-value s 'chars) (cdr q)) (car q)) :eof)))
    ; Both-Gray two-way stream
    (let* ((gin  (make-instance 'test-gs-qin :chars (list #\A #\B)))
           (gcap (make-instance 'test-gs-cap))
           (tw   (make-two-way-stream gin gcap)))
      (check "gray tw streamp"             t   (streamp tw))
      (check "gray tw typep two-way-stream" t  (typep tw 'two-way-stream))
      (check "gray tw typep stream"         t  (typep tw 'stream))
      (check "gray tw input-stream-p"      t   (input-stream-p tw))
      (check "gray tw output-stream-p"     t   (output-stream-p tw))
      (check "gray tw input accessor"      t   (eq (two-way-stream-input-stream tw) gin))
      (check "gray tw output accessor"     t   (eq (two-way-stream-output-stream tw) gcap))
      (check "gray tw read-char A"         #\A (read-char tw))
      (check "gray tw read-char B"         #\B (read-char tw))
      (write-char #\X tw)
      (write-string "hi" tw)
      (check "gray tw write accumulates"   "Xhi" (gs-flush gcap))
      (format tw "f~D" 9)
      (check "gray tw format delegates"   "f9"  (gs-flush gcap)))
    ; Mixed: native input + Gray output
    (let* ((nin  (make-string-input-stream "hello"))
           (gcap2 (make-instance 'test-gs-cap))
           (tw   (make-two-way-stream nin gcap2)))
      (check "mixed native-in gray-out read-char"  #\h (read-char tw))
      (write-char #\Z tw)
      (check "mixed native-in gray-out write-char" "Z" (gs-flush gcap2)))
    ; Mixed: Gray input + native output
    (let* ((gin2 (make-instance 'test-gs-qin :chars (list #\W)))
           (nout (make-string-output-stream))
           (tw   (make-two-way-stream gin2 nout)))
      (check "mixed gray-in native-out read-char"  #\W (read-char tw))
      (write-char #\Q tw)
      (check "mixed gray-in native-out write-char" "Q" (get-output-stream-string nout)))
    ; --- Bulk sequence I/O: READ-SEQUENCE / WRITE-SEQUENCE dispatch ---
    ; GRAY must export the bulk-I/O GFs (trivial-gray-streams' fallback imports them).
    (check "gray exports stream-read-sequence"
           :external (nth-value 1 (find-symbol "STREAM-READ-SEQUENCE" "GRAY")))
    (check "gray exports stream-write-sequence"
           :external (nth-value 1 (find-symbol "STREAM-WRITE-SEQUENCE" "GRAY")))
    ; Default STREAM-READ-SEQUENCE (binary) — queue of bytes, only stream-read-byte.
    (defclass test-gs-byte-in (gray:fundamental-binary-input-stream)
      ((bytes :initarg :bytes :initform nil)))
    (defmethod gray:stream-read-byte ((s test-gs-byte-in))
      (let ((q (slot-value s 'bytes)))
        (if q (progn (setf (slot-value s 'bytes) (cdr q)) (car q)) :eof)))
    (let* ((s (make-instance 'test-gs-byte-in :bytes (list 10 20 30 40)))
           (buf (make-array 4 :element-type '(unsigned-byte 8))))
      (check "read-sequence binary full count" 4 (read-sequence buf s))
      (check "read-sequence binary contents" (list 10 20 30 40) (coerce buf 'list)))
    (let* ((s (make-instance 'test-gs-byte-in :bytes (list 1 2)))
           (buf (make-array 5 :element-type '(unsigned-byte 8) :initial-element 0)))
      (check "read-sequence binary short count" 2 (read-sequence buf s))
      (check "read-sequence binary short contents" (list 1 2 0 0 0) (coerce buf 'list)))
    (let* ((s (make-instance 'test-gs-byte-in :bytes (list 7 8 9)))
           (buf (make-array 5 :element-type '(unsigned-byte 8) :initial-element 0)))
      (check "read-sequence start/end count" 4 (read-sequence buf s :start 1 :end 4))
      (check "read-sequence start/end contents" (list 0 7 8 9 0) (coerce buf 'list)))
    ; Default STREAM-READ-SEQUENCE (character) — reuse the queue char stream.
    (let* ((s (make-instance 'test-gs-qin :chars (list #\f #\o #\o)))
           (buf (make-string 3)))
      (check "read-sequence char count" 3 (read-sequence buf s))
      (check "read-sequence char contents" "foo" buf))
    ; Overridden STREAM-READ-SEQUENCE — confirm the bulk method is dispatched.
    (defclass test-gs-bulk-in (gray:fundamental-binary-input-stream)
      ((fill-byte :initarg :fill-byte :initform 0)
       (calls :initform 0 :accessor gs-bulk-calls)))
    (defmethod gray:stream-read-sequence ((s test-gs-bulk-in) seq start end &key)
      (incf (gs-bulk-calls s))
      (do ((i start (1+ i))) ((>= i end) end)
        (setf (aref seq i) (slot-value s 'fill-byte))))
    (let* ((s (make-instance 'test-gs-bulk-in :fill-byte 99))
           (buf (make-array 6 :element-type '(unsigned-byte 8))))
      (check "read-sequence bulk count" 6 (read-sequence buf s))
      (check "read-sequence bulk one GF call" 1 (gs-bulk-calls s))
      (check "read-sequence bulk contents" (list 99 99 99 99 99 99) (coerce buf 'list)))
    ; WRITE-SEQUENCE to a capturing character stream.
    (let ((g (make-instance 'test-gs-cap)))
      (check "write-sequence char return" "abc" (write-sequence "abc" g))
      (check "write-sequence char contents" "abc" (gs-flush g))
      (write-sequence "xYYz" g :start 1 :end 3)
      (check "write-sequence start/end contents" "YY" (gs-flush g)))
    ; WRITE-SEQUENCE of bytes to a capturing binary stream.
    (defclass test-gs-byte-cap (gray:fundamental-binary-output-stream)
      ((buf :initform nil :accessor gs-byte-cap-buf)))
    (defmethod gray:stream-write-byte ((s test-gs-byte-cap) b)
      (push b (gs-byte-cap-buf s)))
    (let* ((g (make-instance 'test-gs-byte-cap))
           (data (make-array 3 :element-type '(unsigned-byte 8)
                               :initial-contents '(5 6 7))))
      (write-sequence data g)
      (check "write-sequence byte contents" (list 5 6 7) (nreverse (gs-byte-cap-buf g))))
    ; Native (non-Gray) streams must keep the boot.lisp behaviour.
    (let ((ss (make-string-output-stream)))
      (write-sequence "native" ss)
      (check "write-sequence native unaffected" "native" (get-output-stream-string ss)))
    (let* ((sis (make-string-input-stream "native"))
           (buf (make-string 6)))
      (read-sequence buf sis)
      (check "read-sequence native unaffected" "native" buf))
    ; make-broadcast-stream/make-concatenated-stream fan out only to native
    ; CL_Stream components in C (stream.c); a Gray-stream component must be
    ; rejected at construction rather than silently dropping output or
    ; being skipped as if already at EOF.
    (check "make-broadcast-stream rejects gray output component" :type-error
      (handler-case (make-broadcast-stream (make-instance 'test-gs-cap))
        (error (c) (declare (ignore c)) :type-error)))
    (check "make-concatenated-stream rejects gray input component" :type-error
      (handler-case (make-concatenated-stream (make-instance 'test-gs-qin))
        (error (c) (declare (ignore c)) :type-error))))
  (error (e)
    (setq *fail-count* (+ *fail-count* 1))
    (format t "FAIL: gray-stream regression - ~A~%" e)))

; --- Room ---
(handler-case
  (let ((s (with-output-to-string (*standard-output*) (room))))
    (check "room captured by *standard-output*" t
           (and (search "Heap:" s) (search "GC:" s) t)))
  (error (e)
    (setq *fail-count* (+ *fail-count* 1))
    (format t "FAIL: room test - ~A~%" e)))

; --- Load verbose stream redirection ---
; *load-verbose* output must go to *standard-output* so SLY can capture it.
(handler-case
  (let* ((tmp (concatenate 'string *test-tmp* "_clt_load_verbose.lisp")))
    (with-open-file (s tmp :direction :output :if-exists :supersede)
      (write-string "(defvar *load-verbose-amiga-test* t)" s))
    (let ((out (with-output-to-string (*standard-output*)
                 (let ((*load-verbose* t))
                   (load tmp)))))
      (check "load verbose to *standard-output*" t
             (and (search "; Loading" out) t)))
    (let ((out2 (with-output-to-string (*standard-output*)
                  (let ((*load-verbose* nil))
                    (load tmp)))))
      (check "load verbose nil suppresses" t
             (null (search "; Loading" out2)))))
  (error (e)
    (setq *fail-count* (+ *fail-count* 1))
    (format t "FAIL: load verbose test - ~A~%" e)))

; --- Nested UWP in cleanup regression (pending-throw truncation bug) ---
; If the abort handler fires, restart-case returns :HANDLER-RAN.
; If the pending transfer is lost (bug), the UWP returns NIL.
(check "nested-uwp-in-cleanup does not truncate transfer"
  :handler-ran
  (flet ((nested-uwp (thunk)
           (unwind-protect (funcall thunk) nil)))
    (restart-case
      (unwind-protect
        (invoke-restart 'abort)
        (nested-uwp (lambda () nil)))
      (abort () :handler-ran))))

(check "nested-uwp-in-cleanup runs code after nested-uwp"
  '(:cleanup-start :inside-nested-uwp :cleanup-after-nested :handler)
  (let ((log nil))
    (flet ((nested-uwp (thunk)
             (unwind-protect (funcall thunk) nil))
           (mk (x) (push x log) x))
      (restart-case
        (unwind-protect (invoke-restart 'abort)
          (progn (mk :cleanup-start)
                 (nested-uwp (lambda () (mk :inside-nested-uwp)))
                 (mk :cleanup-after-nested)))
        (abort () (mk :handler)))
      (reverse log))))

(check "nested-uwp-in-cleanup runs code after for error-propagation"
  '(:cleanup-start :inside-nested-uwp :cleanup-after-nested :caught)
  (let ((log nil))
    (flet ((nested-uwp (thunk)
             (unwind-protect (funcall thunk) nil))
           (mk (x) (push x log) x))
      (handler-case
        (unwind-protect (error "test-error")
          (progn (mk :cleanup-start)
                 (nested-uwp (lambda () (mk :inside-nested-uwp)))
                 (mk :cleanup-after-nested)))
        (error () (mk :caught)))
      (reverse log))))

; --- Long-form DEFSETF (CLHS 5.5.5) + string-vector string fns + END-OF-FILE
;     condition.  Regression for the hunchentoot session/cookie page bugs:
;     (1) long-form defsetf was unsupported (the threaded "type=0" was really
;     (setf (session-value ..) ..) mis-compiling to FLOAD of the access lambda
;     list); (2) string fns rejected adjustable/fill-pointer strings; (3) EOF
;     signalled a plain error, not END-OF-FILE.  Mirrors tests/test_vm.c. ---
(defvar *dsl-h* (make-hash-table :test 'equal))
(defun dsl-get (k &optional (tag :d)) (gethash (cons k tag) *dsl-h*))
(defsetf dsl-get (k &optional (tag :d)) (v)
  `(setf (gethash (cons ,k ,tag) *dsl-h*) ,v))
(check "long-form defsetf, default optional" 1
  (progn (setf (dsl-get "a") 1) (dsl-get "a")))
(check "long-form defsetf, supplied optional" 2
  (progn (setf (dsl-get "b" :x) 2) (dsl-get "b" :x)))
(check "long-form defsetf, optional defaults on read" nil (dsl-get "b"))
(check "long-form defsetf, incf composes" 15
  (progn (setf (dsl-get "n") 10) (incf (dsl-get "n") 5) (dsl-get "n")))

(defun make-fp-string (s)
  (let ((v (make-array 0 :element-type 'character :fill-pointer 0 :adjustable t)))
    (dotimes (i (length s)) (vector-push-extend (char s i) v))
    v))
(check "string-equal on fill-pointer string" t (string-equal (make-fp-string "hi") "HI"))
(check "string= on fill-pointer string" t (string= (make-fp-string "hi") "hi"))
(check "string-upcase on fill-pointer string" "HI" (string-upcase (make-fp-string "hi")))
(check "write-string on fill-pointer string" "hi"
  (with-output-to-string (o) (write-string (make-fp-string "hi") o)))
(check "princ fill-pointer string is chars not #(...)" "hi"
  (princ-to-string (make-fp-string "hi")))
(check "string-trim on fill-pointer string" "hi"
  (string-trim " " (make-fp-string " hi ")))
(check "string-capitalize on fill-pointer string" "Hello"
  (string-capitalize (make-fp-string "hello")))

(check "read-char EOF signals end-of-file" :eof
  (handler-case (with-input-from-string (s "a") (read-char s) (read-char s))
    (end-of-file () :eof)))
(check "peek-char EOF signals end-of-file" :eof
  (handler-case (with-input-from-string (s "") (peek-char nil s))
    (end-of-file () :eof)))
(check "read-line EOF signals end-of-file" :eof
  (handler-case (with-input-from-string (s "x") (read-line s) (read-line s))
    (end-of-file () :eof)))
(check "read-char eof-error-p nil returns eof-value" :none
  (with-input-from-string (s "") (read-char s nil :none)))
(check "read-byte EOF signals end-of-file" :eof
  (handler-case (with-input-from-string (s "") (read-byte s))
    (end-of-file () :eof)))
(check "read-byte eof-error-p nil returns eof-value" :none
  (with-input-from-string (s "") (read-byte s nil :none)))
; read-char-no-hang EOF requires a file stream (CL_STREAM_FLAG_EOF is only
; set by byte-oriented streams); create a temp file to test against.
(let ((eof-path #+amigaos "T:cl-rcnh-eof.tmp"
                #-amigaos "/tmp/.cl-clamiga-rcnh-eof.tmp"))
  (with-open-file (out eof-path :direction :output
                   :if-exists :supersede :if-does-not-exist :create))
  (check "read-char-no-hang EOF signals end-of-file" :eof
    (handler-case
      (with-open-file (s eof-path)
        (read-char s nil nil)      ; sets CL_STREAM_FLAG_EOF
        (read-char-no-hang s))     ; flag set → signals END-OF-FILE
      (end-of-file () :eof)))
  (check "read-char-no-hang eof-value at EOF" :none
    (with-open-file (s eof-path)
      (read-char s nil nil)
      (read-char-no-hang s nil :none))))

; --- Regression: backtrace buffer overflow must not corrupt c_stack_base ---
; A deep chain of 25 distinct functions with a long source-file name fills the
; 2048-byte backtrace_buf and would overflow into c_stack_base if pos were not
; clamped.  A corrupted c_stack_base makes every subsequent cl_check_c_stack
; compute a garbage "used" value and signal a bogus "C stack overflow".
; After triggering the deep backtrace, (+ 1 2) must still return 3.
(progn
  (defun bt-overflow-f25 () (error "bt-overflow-bottom"))
  (defun bt-overflow-f24 () (let ((r (bt-overflow-f25))) r))
  (defun bt-overflow-f23 () (let ((r (bt-overflow-f24))) r))
  (defun bt-overflow-f22 () (let ((r (bt-overflow-f23))) r))
  (defun bt-overflow-f21 () (let ((r (bt-overflow-f22))) r))
  (defun bt-overflow-f20 () (let ((r (bt-overflow-f21))) r))
  (defun bt-overflow-f19 () (let ((r (bt-overflow-f20))) r))
  (defun bt-overflow-f18 () (let ((r (bt-overflow-f19))) r))
  (defun bt-overflow-f17 () (let ((r (bt-overflow-f18))) r))
  (defun bt-overflow-f16 () (let ((r (bt-overflow-f17))) r))
  (defun bt-overflow-f15 () (let ((r (bt-overflow-f16))) r))
  (defun bt-overflow-f14 () (let ((r (bt-overflow-f15))) r))
  (defun bt-overflow-f13 () (let ((r (bt-overflow-f14))) r))
  (defun bt-overflow-f12 () (let ((r (bt-overflow-f13))) r))
  (defun bt-overflow-f11 () (let ((r (bt-overflow-f12))) r))
  (defun bt-overflow-f10 () (let ((r (bt-overflow-f11))) r))
  (defun bt-overflow-f9  () (let ((r (bt-overflow-f10))) r))
  (defun bt-overflow-f8  () (let ((r (bt-overflow-f9)))  r))
  (defun bt-overflow-f7  () (let ((r (bt-overflow-f8)))  r))
  (defun bt-overflow-f6  () (let ((r (bt-overflow-f7)))  r))
  (defun bt-overflow-f5  () (let ((r (bt-overflow-f6)))  r))
  (defun bt-overflow-f4  () (let ((r (bt-overflow-f5)))  r))
  (defun bt-overflow-f3  () (let ((r (bt-overflow-f4)))  r))
  (defun bt-overflow-f2  () (let ((r (bt-overflow-f3)))  r))
  (defun bt-overflow-f1  () (let ((r (bt-overflow-f2)))  r)))
(handler-case (bt-overflow-f1) (error () nil))
(check "backtrace-buf overflow: c_stack_base intact, no spurious stack-overflow" 3
  (+ 1 2))

; --- cl-spark regressions: FORMAT justification, wide-string output,
;     LOOP parallel stepping, and FLOOR float consistency ---

; ~mincol,colinc,minpad,padchar<...~> justification (CLHS 22.3.6.2): the
; segments are spread across the field with padding in the gaps.
(check "justify three segments" "0            75            150"
  (format nil "~30<~A~;~A~;~A~>" "0" "75" "150"))
(check "justify with pad char" "0------------75------------150"
  (format nil "~30,,,'-<~A~;~A~;~A~>" "0" "75" "150"))
; SBCL reference (spark-test.lisp): the later gap absorbs the odd column.
(check "justify uneven padding" "11-----------222------------33"
  (format nil "~30,,,'-<~A~;~A~;~A~>" 11 222 33))
(check "justify single right" "        hi"
  (format nil "~10<~A~>" "hi"))
(check "justify atsign left" "hi        "
  (format nil "~10@<~A~>" "hi"))
(check "justify V mincol" "0            75            150"
  (format nil "~V<~A~;~A~;~A~>" 30 "0" "75" "150"))

; PRINC of an 8-bit (Latin-1) string with a high byte round-trips its code.
(check "princ high-byte string code" 252
  (char-code (char (princ-to-string (string (code-char 252))) 0)))
(check "with-output-to-string high byte" 252
  (char-code (char (with-output-to-string (s)
                     (princ (string (code-char 252)) s)) 0)))

; LOOP FOR ... AND ... steps in parallel (CLHS 6.1.2.1.4) — Fibonacci, not
; powers of two.
(check "loop for/and parallel fib"
  '(0 1 1 2 3 5 8 13 21)
  (loop for i from 0 to 8 collect
        (loop for f1 = 0 then f2 and f2 = 1 then (+ f1 f2)
              repeat i finally (return f1))))
(check "loop for/and parallel swap" '(2 1)
  (loop for a = 1 then b and b = 2 then a
        repeat 3 finally (return (list a b))))
(check "loop for = then uses init first" '(7 14 28 56)
  (loop for x = 7 then (* x 2) repeat 4 collect x))
(check "loop sequential for stays sequential" '(10 20 30)
  (loop for i from 1 to 3 for j = (* i 10) collect j))
; cl-ppcre create-matcher-aux idiom: separate FOR = THEN clauses step in
; source order (the THEN stepper keeps its place before the no-THEN clause).
(check "loop sequential then chain" '(c (b (a nf)))
  (loop for e in '(a b c)
        for curr = 'nf then next
        for next = (list e curr)
        finally (return next)))
; CLHS 6.1.2.1.4: mixed AND group (= THEN + FROM) must step in parallel.
; a's step (+ a j) uses pre-step j. Correct: (0 1 3); wrong: (0 2 5).
(check "loop for/and mixed then+from parallel" '(0 1 3)
  (loop for a = 0 then (+ a j) and j from 1 repeat 3 collect a))
(check "loop for/and mixed from+then parallel" '(0 1 3)
  (loop for j from 1 and a = 0 then (+ a j) repeat 3 collect a))

; Two-arg FLOOR of floats: quotient agrees with (/ a b); remainder shares the
; divisor's sign (never a tiny negative against a positive divisor).
(check "floor float quotient matches /" 7
  (floor (log 6) (/ (log 6) 7)))
(check "floor float remainder non-negative" 0.0d0
  (nth-value 1 (floor 1.0d0 (* (/ 2.0d0 350) 7))))
(check "floor float remainder sign invariant" t
  (loop for n in '(0.0d0 1.0d0 0.5d0 0.25d0)
        always (>= (nth-value 1 (floor n (* (/ 2.0d0 350) 7))) 0)))

; --- Sequence functions: ANSI conformance regressions (2026-06-25) ---
; SUBSTITUTE / NSUBSTITUTE on bit-vectors and strings, :count handling,
; :from-end reverse order, leftmost-keyword precedence.
(check "substitute bit-vector" '(0 0 0 0 0 0)
       (coerce (substitute 0 1 (copy-seq #*010101)) 'list))
(check "substitute string" "bZnZnZ" (substitute #\Z #\a (copy-seq "banana")))
(check "substitute :count from-end" '(0 1 0 1 1 1)
       (coerce (substitute 1 0 (copy-seq #*010101) :count 1 :from-end t) 'list))
(check "nsubstitute :count -1 is zero" '(a b a c)
       (nsubstitute 'b 'a (copy-seq '(a b a c)) :count -1))
(check "nsubstitute :count 1 from-end" '(a b b c)
       (coerce (nsubstitute 'b 'a (copy-seq #(a b a c)) :count 1 :from-end t) 'list))
(check "substitute-if bit-vector" '(0 0 0)
       (coerce (substitute-if 0 #'oddp (copy-seq #*111)) 'list))
(check "leftmost :key wins" '(a 2 0 3 a 0 3)
       (substitute 'a 0 (list 1 2 0 3 1 0 3) :key #'1- :key #'identity))

; MAKE-SEQUENCE: bit-vector / cons / null / class objects / compound types
(check "make-sequence bit-vector" '(0 0 0 0 0)
       (coerce (make-sequence 'bit-vector 5 :initial-element 0) 'list))
(check "make-sequence cons" '(x x x) (make-sequence 'cons 3 :initial-element 'x))
(check "make-sequence null" nil (make-sequence 'null 0))
(check "make-sequence (string *)" "xxxx"
       (make-sequence '(string *) 4 :initial-element #\x))
(check "make-sequence find-class list" '(z z)
       (make-sequence (find-class 'list) 2 :initial-element 'z))
(check "make-sequence symbol -> type-error" 'te
       (handler-case (make-sequence 'symbol 5) (type-error () 'te) (error () 'wrong)))
(check "make-sequence cons 0 -> type-error" 'te
       (handler-case (make-sequence 'cons 0) (type-error () 'te) (error () 'wrong)))
(check "make-sequence (vector * 4) 3 -> type-error" 'te
       (handler-case (make-sequence '(vector * 4) 3) (type-error () 'te) (error () 'wrong)))

; MERGE on string / bit-vector / compound result types + length constraints
(check "merge string" "1234"
       (merge 'string (list #\1 #\3) (list #\2 #\4) #'char<))
(check "merge bit-vector" '(0 0 1 1)
       (coerce (merge 'bit-vector (list 0 1) (list 0 1) #'<) 'list))
(check "merge (vector * 6)" '(1 2 3 4 5 6)
       (coerce (merge '(vector * 6) (list 1 2 3) (list 4 5 6) #'<) 'list))
(check "merge length mismatch -> type-error" 'te
       (handler-case (merge '(vector * 3) (list 1 2 3) (list 4 5 6) #'<)
         (type-error () 'te) (error () 'wrong)))

; CONCATENATE on bit-vector / cons / null + length constraints
(check "concatenate bit-vector" '(0 1 1 0)
       (coerce (concatenate 'bit-vector #*01 #*10) 'list))
(check "concatenate cons" '(a b c) (concatenate 'cons '(a b) '(c)))
(check "concatenate null empty" nil (concatenate 'null nil nil))
(check "concatenate (vector * 3) mismatch -> type-error" 'te
       (handler-case (concatenate '(vector * 3) '(a b c d e))
         (type-error () 'te) (error () 'wrong)))

; SORT / STABLE-SORT on strings and bit-vectors
(check "sort string" "abcd" (sort (copy-seq "dbca") #'char<))
(check "sort bit-vector" '(0 0 1 1) (coerce (sort (copy-seq #*1010) #'<) 'list))
(check "stable-sort string" "abc" (stable-sort (copy-seq "cba") #'char<))

; REPLACE on bit-vectors, overlap-safe self replace
(check "replace bit-vector" '(0 1 1 1 0 0 1)
       (coerce (replace (copy-seq #*1101001) #*011) 'list))

; ELT bounds and FILL bounds signal type-error
(check "elt out of bounds -> type-error" 'te
       (handler-case (elt '(a b c) 5) (type-error () 'te) (error () 'wrong)))
(check "elt nil -> type-error" 'te
       (handler-case (elt nil 0) (type-error () 'te) (error () 'wrong)))
(check "elt negative -> type-error" 'te
       (handler-case (elt '(a b c) -1) (type-error () 'te) (error () 'wrong)))
(check "fill :start -1 -> type-error" 'te
       (handler-case (fill (make-array 5) 'x :start -1) (type-error () 'te) (error () 'wrong)))

; REDUCE: empty subsequence calls fn with no args; from-end on strings
(check "reduce empty calls fn no args" 0 (reduce #'+ nil))
(check "reduce empty range" 0 (reduce #'+ '(1 2 3) :start 0 :end 0))
(check "reduce from-end string" '(#\a #\b . g)
       (reduce #'cons "ab" :from-end t :initial-value 'g))

; MAP-INTO on bit-vector results (self-map)
(check "map-into bit-vector self" '(1 0 1 1 0 0 1)
       (coerce (let ((v (copy-seq #*0100110))) (map-into v (lambda (x) (- 1 x)) v)) 'list))

; REMOVE-DUPLICATES keep-last default / keep-first with :from-end
(check "remove-duplicates keep-last" '(3 4 1 5 6 2 7)
       (remove-duplicates '(1 2 3 4 1 3 4 1 2 5 6 2 7)))
(check "remove-duplicates from-end keep-first" '(1 2 3 4 5 6)
       (remove-duplicates '(1 2 3 4 1 3 4 1 2 5 6 2) :from-end t))

; DELETE delegates to REMOVE's full keyword behaviour
(check "delete :count" '(b a a c) (delete 'a (list 'a 'b 'a 'a 'c) :count 1))
(check "delete-if :start" '(1 3) (delete-if #'evenp (list 1 2 3 4) :start 1))

; SEARCH :from-end and :test-not; MISMATCH :test-not
(check "search from-end" 2 (search '(a) '(a b a b) :from-end t))
(check "mismatch :test-not" 2
       (mismatch '(1 2 3 4) '(10 11 7 123) :test-not #'(lambda (x y) (= x (- y 4)))))

; Keyword-argument validation (CLHS 3.4.1.4) signals program-error
(check "count bad keyword -> program-error" 'pe
       (handler-case (count nil nil :bad t) (program-error () 'pe) (error () 'wrong)))
(check "count odd keyword -> program-error" 'pe
       (handler-case (count nil nil :key) (program-error () 'pe) (error () 'wrong)))
(check "remove bad keyword -> program-error" 'pe
       (handler-case (remove 'a nil :bad t) (program-error () 'pe) (error () 'wrong)))
(check "count :allow-other-keys lets bad through" 0
       (count 'a nil :bad t :allow-other-keys t))
; FIND/POSITION/COUNT must reject :count (not in their CLHS lambda lists)
(check "find :count -> program-error" 'pe
       (handler-case (find 'a '(1 2 3) :count 1) (program-error () 'pe) (error () 'wrong)))
(check "position :count -> program-error" 'pe
       (handler-case (position 'a '(1 2 3) :count 1) (program-error () 'pe) (error () 'wrong)))
(check "count :initial-value -> program-error" 'pe
       (handler-case (count 'a '(1 2 3) :initial-value 0) (program-error () 'pe) (error () 'wrong)))
; FIND-IF/-NOT, POSITION-IF/-NOT, COUNT-IF/-NOT must reject :test/:test-not
(check "find-if :test -> program-error" 'pe
       (handler-case (find-if #'numberp '(1) :test #'eql) (program-error () 'pe) (error () 'wrong)))
(check "count-if-not :test-not -> program-error" 'pe
       (handler-case (count-if-not #'numberp '(1) :test-not #'eql) (program-error () 'pe) (error () 'wrong)))

; --- Adjustable / fill-pointer / displaced array sequence ops ---
; EQUALP compares vectors as sequences: by ACTIVE length (fill pointer counts).
; (ansi-test EQUALP.11 / REVERSE-VECTOR.6; matches SBCL/CLISP)
(check "equalp fill-pointer vector active" t
       (equalp (make-array 5 :fill-pointer 2 :initial-contents '(1 2 3 4 5)) #(1 2))) ; active (1 2) == (1 2)
(check "equalp fill-pointer vector differing active" nil
       (equalp (make-array 5 :fill-pointer 2 :initial-contents '(1 2 3 4 5))
               (make-array 5 :fill-pointer 3 :initial-contents '(1 2 3 4 5)))) ; fp 2 vs 3 -> different active lengths
(check "equalp fill-pointer bit-vector active" t
       (equalp (make-array 6 :element-type 'bit :fill-pointer 3
                           :initial-contents '(1 0 1 1 1 1)) #*101)) ; active (1 0 1) == #*101
; REVERSE of a fill-pointer string -> STRING of the active length.
(check "reverse fill-pointer string is string" t
       (let ((s (make-array 9 :element-type 'character
                            :initial-contents "12345ZZZZ" :fill-pointer 5)))
         (let ((r (reverse s))) (and (stringp r) (string= r "54321")))))
; REVERSE of a fill-pointer bit-vector honours active length.
(check "reverse fill-pointer bit-vector" '(1 1 0 0 0)
       (coerce (reverse (make-array 10 :element-type 'bit
                          :initial-contents '(0 0 0 1 1 0 1 0 1 0) :fill-pointer 5)) 'list))
; SUBSTITUTE/REMOVE-IF on an adjustable string return STRINGs.
(check "substitute adjustable string is string" t
       (let ((s (make-array 5 :element-type 'character
                            :initial-contents "abcab" :adjustable t)))
         (let ((r (substitute #\x #\a s))) (and (stringp r) (string= r "xbcxb")))))
(check "remove-if adjustable string is string" t
       (let ((s (make-array 10 :element-type 'character
                            :initial-contents "ab1c23def4" :adjustable t)))
         (let ((r (remove-if #'alpha-char-p s))) (and (stringp r) (string= r "1234")))))
; NSUBSTITUTE on a fill-pointer string mutates only the active region.
(check "nsubstitute fill-pointer string active" t
       (let ((s (make-array 9 :element-type 'character
                            :initial-contents "abcabZZZZ" :fill-pointer 5)))
         (nsubstitute #\x #\a s) (and (= (length s) 5) (string= s "xbcxb"))))
; COPY-SEQ of an adjustable base-char string is a SIMPLE string.
(check "copy-seq adjustable base string simple" t
       (simple-string-p (copy-seq (make-array 4 :element-type 'base-char
                            :initial-contents '(#\a #\b #\c #\d) :adjustable t))))
; MAKE-ARRAY: a one-element list dimension displaces like the integer form.
(check "make-array list-dim displaced view" '(c d e f)
       (let* ((v1 (copy-seq #(a b c d e f g h)))
              (v2 (make-array '(4) :displaced-to v1 :displaced-index-offset 2)))
         (coerce (copy-seq v2) 'list)))
; MAKE-ARRAY: displacing onto a string with a fill pointer keeps it.
(check "make-array displaced string keeps fill pointer" t
       (let* ((s0 "XXabcdeYYY")
              (s (make-array 5 :element-type 'character :displaced-to s0
                             :displaced-index-offset 2 :fill-pointer 3)))
         (and (= (length s) 3) (string= s "abc"))))
; MAKE-ARRAY: a multi-dimensional array can displace onto a vector; element
; access chases the backing (regression: the multi-dim path used to ignore
; :displaced-to and return a fresh NIL-filled array).
(check "make-array multidim displaced read" 6
       (let* ((a (make-array 12))
              (d (make-array '(2 3) :displaced-to a :displaced-index-offset 0)))
         (dotimes (i 12) (setf (aref a i) (1+ i)))
         (aref d 1 2)))                          ; row-major 5 -> a[5]=6
(check "make-array multidim displaced write-through" 99
       (let* ((a (make-array 12 :initial-element 0))
              (d (make-array '(2 3) :displaced-to a :displaced-index-offset 2)))
         (setf (aref d 1 2) 99)                  ; row-major 5 -> a[7]
         (aref a 7)))
(check "make-array multidim displaced dims/displacement" '(2 (2 3) 6 t 4)
       (let* ((a (make-array 12))
              (d (make-array '(2 3) :displaced-to a :displaced-index-offset 4)))
         (multiple-value-bind (arr off) (array-displacement d)
           (list (array-rank d) (array-dimensions d) (array-total-size d)
                 (eq arr a) off))))
; serapeum RESHAPE pattern: 1-D displaced onto multi-dim displaced onto vector.
(check "make-array nested multidim displacement" 36
       (let* ((a (make-array 48))
              (m (make-array '(2 3 3 2) :displaced-to a))
              (r (make-array 36 :displaced-to m)))
         (dotimes (i 48) (setf (aref a i) (1+ i)))
         (aref r 35)))
; AREF ignores the fill pointer (whole bit-vector storage is accessible).
(check "aref ignores bit-vector fill pointer" '(1 1 1 1 0 0 0 0)
       (let ((b (make-array 8 :element-type 'bit :initial-element 0 :fill-pointer 4)))
         (fill b 1) (loop for i from 0 below 8 collect (aref b i))))
; MAKE-ARRAY :initial-contents from a bit-vector source copies its bits.
(check "make-array initial-contents from bit-vector" '(0 0 1 0 1 1 0 0 0)
       (coerce (make-array 9 :element-type 'bit :initial-contents #*001011000) 'list))
; REMOVE :count :from-end on a string removes the LAST matches.
(check "remove count from-end string" "abcdad"
       (remove #\b "abcdbad" :count 1 :from-end t))
(check "remove count from-start string" "acdbad"
       (remove #\b "abcdbad" :count 1))

; SCHAR requires a simple-string (CLHS 17.1); it must reject non-simple strings.
(check "schar reads simple string" #\e (schar "hello" 1))
(check "schar rejects non-simple string signals type-error" t
       (let ((s (make-array 3 :element-type 'character :fill-pointer 3 :adjustable t)))
         (setf (char s 0) #\a) (setf (char s 1) #\b) (setf (char s 2) #\c)
         (handler-case (progn (schar s 0) nil)
           (type-error () t))))

; --- Gray-streams reload idempotency (regression) ---
; Re-LOADing lib/gray-streams.lisp must not corrupt CLOS dispatch.  The
; integration block captured ORIG-CLOSE/-READ-CHAR/etc. as (SYMBOL-FUNCTION
; 'X) at load time; a second LOAD (boot pulls it once, quicklisp-compat LOADs
; it again) would capture the already-installed gray GFs, so the (STREAM T)
; CLOSE method's (FUNCALL ORIG-CLOSE STREAM) re-dispatched CLOSE on the same
; class -> unbounded self-recursion -> VM frame stack overflow.  A one-shot
; install guard makes the file reload-safe.  Run this LAST: it installs the
; gray CL-I/O wrappers into the image.
(load "lib/gray-streams.lisp")
(load "lib/gray-streams.lisp")
(check "gray-streams reload: close file stream does not recurse" t
       (let ((p (concatenate 'string *test-tmp* "_clt_grayreload.txt")))
         (let ((s (open p :direction :output))) (write-string "hi" s) (close s))
         (let ((s (open p :direction :input)))
           (prog1 (and (eql (read-char s) #\h) t) (close s)))))
(check "gray-streams reload: close string stream does not recurse" t
       (let ((s (make-string-output-stream))) (write-char #\X s) (close s) t))

; --- Alexandria-driven conformance regressions ---
; Bugs surfaced by ALEXANDRIA's test suite; see tests/test_alexandria_regress.c
; and trunk/load-and-test-alexandria.lisp.

; DELETE is destructive on lists: deleting the middle element mutates the
; shared head cons so an alias sees the splice.
(check "delete splices list in place" '(1 3)
       (let* ((x (list 1 2 3)) (x* x)) (delete 2 x) x*))
(check "delete-if destructive on list" '(1 3)
       (let* ((x (list 1 2 3 4)) (x* x)) (delete-if #'evenp x) x*))

; ROTATEF / SHIFTF / DEFINE-MODIFY-MACRO must evaluate each place's subforms
; exactly once (via GET-SETF-EXPANSION).
(check "rotatef evaluates place subforms once" 2
       (let ((p 0) (v (vector 10 20 30)))
         (rotatef (svref v (incf p)) (svref v (incf p))) p))
; (coerce result vectors to lists — the CHECK macro compares with EQUAL,
;  which does not descend into vectors)
(check "rotatef swaps values" '(30 20 10)
       (let ((v (vector 10 20 30))) (rotatef (aref v 0) (aref v 2)) (coerce v 'list)))
(check "shiftf subforms once + returns first old" '(20 1 (10 99 30))
       (let ((p 0) (v (vector 10 20 30)))
         (list (shiftf (svref v (incf p)) 99) p (coerce v 'list))))
; (defined as its own top-level form so the macro exists before the using
;  form below is compiled)
(define-modify-macro %amiga-maxf (&rest n) max)
(check "define-modify-macro evaluates place once" '(2 (0 2 0))
       (let ((xv (vector 0 0 0)) (p 0))
         (%amiga-maxf (svref xv (incf p)) (incf p))
         (list p (coerce xv 'list))))

; A correct Fisher-Yates shuffle (depends on ROTATEF being single-eval) yields
; a permutation, never duplicates/drops.
(check "fisher-yates shuffle is a permutation" t
       (let ((v (vector 0 1 2 3 4 5 6 7 8 9)))
         (loop for i from 0 below 10
               do (rotatef (aref v i) (aref v (+ i (random (- 10 i))))))
         (equalp (sort (copy-seq v) #'<) (vector 0 1 2 3 4 5 6 7 8 9))))

; EVERY returns a boolean T, not the last (truthy) predicate value.
(check "every returns boolean T" t
       (every (lambda (x) (member x '(1 2 3))) '(1 2 3)))
(check "every returns NIL on failure" nil
       (every (lambda (x) (member x '(1 2 3))) '(1 2 9)))

; SUBTYPEP settles exact compound identities.
(check "subtypep list <= (or null cons)" t (subtypep 'list '(or null cons)))
(check "subtypep (or null cons) <= list" t (subtypep '(or null cons) 'list))
(check "subtypep null <= (and symbol list)" t (subtypep 'null '(and symbol list)))
(check "subtypep (and symbol list) <= null" t (subtypep '(and symbol list) 'null))

; TYPEP: a multidimensional array is not a sequence.
(check "typep 2d-array is not sequence" nil (typep #2a((1 2) (3 4)) 'sequence))
(check "typep vector is sequence" t (typep #(1 2 3) 'sequence))

; READ-BYTE on an output-only stream signals an error (not EOF).
(check "read-byte on broadcast stream errors" :err
       (handler-case (read-byte (make-broadcast-stream)) (error () :err)))

; WITH-STANDARD-IO-SYNTAX binds *PRINT-CASE* to :UPCASE.
(check "with-standard-io-syntax resets *print-case*" "FOO"
       (let ((*print-case* :downcase))
         (with-standard-io-syntax (princ-to-string 'foo))))

; --- Regression: trivia conformance fixes ---
(require "clos")

; #S(type slot value ...) structure-literal reader (CLHS 2.4.8.13).
(defstruct sread-pt x y)
(defstruct (sread-pt3 (:include sread-pt)) z)
(check "#S reader constructs struct" 1 (sread-pt-x #S(sread-pt :x 1 :y 2)))
(check "#S reader second slot"       2 (sread-pt-y #S(sread-pt :x 1 :y 2)))
(check "#S reader inherited slots"   3 (sread-pt3-z #S(sread-pt3 :x 1 :y 2 :z 3)))
(check "#S reader value unevaluated" 'foo (sread-pt-x #S(sread-pt :x foo :y 2)))
(check "#S reader bare symbol slots" 5 (sread-pt-x #S(sread-pt x 5 y 6)))

; SLOT-VALUE / WITH-SLOTS work on condition objects (CLHS 9.1).
(define-condition sv-cnd () ((a :initarg :a) (b :initarg :b)))
(check "slot-value on condition" 10
       (slot-value (make-condition 'sv-cnd :a 10 :b 20) 'a))
(check "slot-value on condition NIL value" nil
       (slot-value (make-condition 'sv-cnd :a nil :b 1) 'a))
(check "with-slots on condition" '(7 8)
       (with-slots (a b) (make-condition 'sv-cnd :a 7 :b 8) (list a b)))
(check "setf slot-value on condition" 99
       (let ((c (make-condition 'sv-cnd :a 1 :b 2)))
         (setf (slot-value c 'a) 99)
         (slot-value c 'a)))
(check "slot-boundp on condition bound"   t
       (slot-boundp (make-condition 'sv-cnd :a 1 :b 2) 'a))
(check "slot-boundp on condition unbound" nil
       (slot-boundp (make-condition 'sv-cnd :a 1) 'b))

; DEFSTRUCT CLOS classes expose slot metaobjects (MOP introspection).
(defstruct mopst-pt mx my)
(check "struct class-slots populated" '(mx my)
       (mapcar #'slot-definition-name (class-slots (find-class 'mopst-pt))))
(check "struct class-slots initarg" '(:mx)
       (slot-definition-initargs (car (class-slots (find-class 'mopst-pt)))))

; symbol-macrolet whose expansion is NIL is a valid binding, not unbound.
(check "symbol-macrolet nil expansion" nil (symbol-macrolet ((sm nil)) sm))
(check "symbol-macrolet nil in list" '(nil nil)
       (symbol-macrolet ((sm nil)) (list sm sm)))
(check "symbol-macrolet non-nil still ok" 6 (symbol-macrolet ((sm 5)) (+ sm 1)))

; get-dispatch-macro-character signals an error for a non-dispatch character.
(check "get-dispatch-macro-character on # ok" nil
       (get-dispatch-macro-character #\# #\Z))
(check "get-dispatch-macro-character on constituent errors" :err
       (handler-case (get-dispatch-macro-character #\a #\x) (error () :err)))

; --- Tier-3 GC/threading audit regressions (2026-07) ---
; Lisp-visible fixes from the tier-3 batch: numeric-tower staleness with
; big operands, coerce/typep/subtypep re-derives, bit-vector source
; re-derive after result alloc, package mutation stores, condition
; creation staleness, the WARN muffle active-mask restore, and the
; interactive-restart argument list.

(check "t3 abs bignum" (expt 10 30) (abs (- (expt 10 30))))
(check "t3 mod bignum" (- (expt 10 25) 1)
       (mod (- (+ (expt 10 30) 1)) (expt 10 25)))
(check "t3 ash floor" (- (+ (expt 2 97) 1)) (ash (- (+ (expt 2 100) 1)) -3))
(check "t3 isqrt bignum" (expt 10 15) (isqrt (expt 10 30)))
(check "t3 ratio add" 37037036703703703677/21 (+ 12345678901234567890/7 1/3))
(check "t3 ratio neg" -12345678901234567890/7 (- 12345678901234567890/7))
(check "t3 sqrt neg" #C(0.0 2.0) (sqrt -4.0))
(check "t3 floor big ratio" '(1763668414462081127 1/7)
       (multiple-value-list (floor 12345678901234567890/7)))
(check "t3 mod ratio" 9/14 (mod 12345678901234567890/7 3/2))
(check "t3 signum" -1.0 (signum -3.5))
(check "t3 lcm big" (* (expt 2 70) 243) (lcm (expt 2 70) 243))
(check "t3 max mixed" 12345678901234567890 (max 1.5 12345678901234567890))
(check "t3 dpb 64" (- (expt 2 64) 1) (dpb -1 (byte 64 0) 0))
(check "t3 boole andc1 big" 5 (boole boole-andc1 (expt 2 70) (+ (expt 2 70) 5)))
(check "t3 random bignum" t
       (let ((r (random (expt 2 64))))
         (and (integerp r) (>= r 0) (< r (expt 2 64)) t)))
(check "t3 equalp complex" t (equalp (sqrt -4.0) #C(0.0 2.0)))

(deftype t3a-small () '(integer 0 10))
(check "t3 typep deftype" t (typep 5 't3a-small))
(check "t3 typep or" t (typep "x" '(or t3a-small string)))
(check "t3 typep ub64" t (typep (- (expt 2 64) 1) '(unsigned-byte 64)))
(check "t3 coerce list->string" "abc" (coerce '(#\a #\b #\c) 'string))
(check "t3 coerce vec->list" '(1 2 3) (coerce #(1 2 3) 'list))
(check "t3 coerce str->vector" t (equalp #(#\a #\b) (coerce "ab" 'vector)))
(check "t3 subtypep deftype" t (and (subtypep 't3a-small 'integer) t))
(check "t3 subtypep or" t
       (and (subtypep '(or t3a-small string) '(or string integer)) t))

(check "t3 bit-and" #*1000 (bit-and #*1010 #*1100))
(check "t3 bit-not" #*0101 (bit-not #*1010))

(defpackage :t3a-pkg (:use :cl))
(check "t3 export inherited" :external
       (progn (export (intern "T3FOO" :t3a-pkg) :t3a-pkg)
              (nth-value 1 (find-symbol "T3FOO" :t3a-pkg))))
(check "t3 setf get new indicator" 42
       (progn (setf (get 't3-some-sym 't3-fresh-ind) 42)
              (get 't3-some-sym 't3-fresh-ind)))

(define-condition t3a-cond (error)
  ((a :initarg :a :reader t3a-a :initform 41)
   (b :initarg :b :reader t3a-b))
  (:default-initargs :b 7))
(check "t3 make-condition initform" 41 (t3a-a (make-condition 't3a-cond)))
(check "t3 error by name" '(5 7)
       (handler-case (error 't3a-cond :a 5)
         (t3a-cond (c) (list (t3a-a c) (t3a-b c)))))
; three warns under ONE handler-bind — pre-fix only the first was caught
; (handler active mask not restored across the muffle longjmp)
(check "t3 warn muffle x3" 3
       (let ((cnt 0))
         (handler-bind ((warning (lambda (c) (declare (ignore c))
                                   (incf cnt) (muffle-warning))))
           (warn "w1") (warn "w2") (warn "w3"))
         cnt))
(check "t3 invoke-restart-interactively" '(1 2 3)
       (restart-case
           (invoke-restart-interactively (find-restart 'use-these))
         (use-these (&rest vals)
           :interactive (lambda () (list 1 2 3))
           vals)))
(check "t3 merge-pathnames relative" "f"
       (pathname-name (merge-pathnames "sub/f.lisp" #P"/base/d/")))

; --- Tier-4 audit batch 2: format directives + printer element loops ---
; Mirrors the host gc-stress tier4-printer block (see
; tests/test_gc_stress_regression.sh); behavior-level checks here.
(defstruct t4b-pt x y)
(check "t4 print vector of structs" "#(#S(T4B-PT :X 1 :Y 2) #S(T4B-PT :X 3 :Y 4))"
       (format nil "~S" (vector (make-t4b-pt :x 1 :y 2) (make-t4b-pt :x 3 :y 4))))
(check "t4 print #2A of structs" "#2A((#S(T4B-PT :X 1 :Y 2) 5) (6 #S(T4B-PT :X 3 :Y 4)))"
       (format nil "~S" (make-array '(2 2)
                         :initial-contents (list (list (make-t4b-pt :x 1 :y 2) 5)
                                                 (list 6 (make-t4b-pt :x 3 :y 4))))))
(check "t4 grouped 101-digit bignum length" 201
       (length (format nil "~,,,1:D" (expt 10 100))))
(check "t4 grouped integer commas" "1,234,567" (format nil "~:D" 1234567))
(check "t4 goto negative clamps" t (stringp (format nil "~-5*~A" 1)))
(check "t4 recursive format string control" "<1 2> 3"
       (format nil "~? ~A" "<~A ~A>" (list 1 2) 3))
(check "t4 print-base/radix restore" "#b11"
       (let ((*print-radix* t) (*print-base* 2))
         (format nil "~X" 255)
         (write-to-string 3)))
(deftype t4b-small () '(integer 0 9))
(set-pprint-dispatch 't4b-small (lambda (s obj) (format s "<small ~D>" obj)))
(check "t4 pprint dispatch buffer-mode capture" "<small 5>"
       (write-to-string 5 :pretty t))
(check "t4 restart report fn print" "resume-123"
       (restart-case
           (let ((r (find-restart 't4b-resume)))
             (format nil "~A" r))
         (t4b-resume () :report
                      (lambda (s) (format s "resume-~{~A~}" (list 1 2 3)))
                      nil)))
(defparameter *t4b-n* 0)
(defstruct t4b-once v)
(defmethod print-object ((o t4b-once) s)
  (incf *t4b-n*)
  (if (= *t4b-n* 1) (error "first print boom") (format s "[ok ~A]" (t4b-once-v o))))
(defparameter *t4b-obj* (make-t4b-once :v 7))
(check "t4 aborted print caught" :caught
       (handler-case (progn (format nil "~A" *t4b-obj*) :printed)
         (error () :caught)))
(check "t4 no leaked pr-inprog marker" "[ok 7]" (format nil "~A" *t4b-obj*))
(deftype t4b-neg () '(integer -9 -1))
(set-pprint-dispatch 't4b-neg (lambda (s o) (declare (ignore s o)) (error "dispatch boom")))
(check "t4 erroring dispatch fn caught" :caught
       (handler-case (progn (let ((*print-pretty* t)) (prin1-to-string -5)) :printed)
         (error () :caught)))
(check "t4 pprint dispatch survives aborted dispatch" "<small 5>"
       (let ((*print-pretty* t)) (prin1-to-string 5)))
(set-pprint-dispatch 't4b-neg nil)
(check "t4 set-pprint-dispatch removal" "-5"
       (let ((*print-pretty* t)) (prin1-to-string -5)))
(check "t4 uwprot after completed throw" 1
       (let ((tag (cons nil nil)))
         (catch tag (throw tag 1))
         (unwind-protect (progn (make-list 10) 1) (make-list 100))))

; --- Tier-4 audit batch 3: IO / strings / reader ---
; Mirrors the host gc-stress tier4-io block (see
; tests/test_gc_stress_regression.sh); behavior-level checks here.
(check "t4 write :base override restored" "255"
       (progn (let ((s (make-string-output-stream)))
                (write 255 :stream s :base 16))
              (write-to-string 255)))
(check "t4 write returns its object" 7
       (let ((s (make-string-output-stream))) (write 7 :stream s)))
(check "t4 write-to-string :base restored" '("FF" "255")
       (list (write-to-string 255 :base 16) (write-to-string 255)))
(check "t4 pprint restores print-escape binding" :restored
       (let ((*print-escape* nil))
         (let ((s (make-string-output-stream))) (pprint '(1 2) s))
         (if *print-escape* :leaked :restored)))
(check "t4 read-delimited-list" '(1 2 3 4 5)
       (with-input-from-string (in "1 2 3 4 5 ]")
         (read-delimited-list #\] in)))
(deftype t4c-small () '(integer 0 9))
; priority 5 so this entry beats the batch-2 t4b-small entry (same range)
(set-pprint-dispatch 't4c-small (lambda (s o) (format s "[sm ~d]" o)) 5)
(check "t4 pprint-dispatch lookup + funcall" "[sm 5]"
       (let ((fn (pprint-dispatch 5)))
         (with-output-to-string (s) (funcall fn s 5))))
(check "t4 copy-pprint-dispatch copy dispatches" :hit
       (if (nth-value 1 (pprint-dispatch 5 (copy-pprint-dispatch))) :hit :miss))
(defmacro t4c-m1 (x) (list 't4c-m2 x))
(defmacro t4c-m2 (x) (list 'list x x))
(check "t4 macroexpand chain + expanded-p" '((list 3 3) t)
       (multiple-value-list (macroexpand '(t4c-m1 3))))
(check "t4 macroexpand-1 unchanged expanded-p" nil
       (nth-value 1 (macroexpand-1 '(quote z))))
(defun t4c-d (x) (+ x 1))
(check "t4 disassemble to string stream" :ok
       (let ((out (with-output-to-string (*standard-output*)
                    (disassemble 't4c-d))))
         (if (and (search "Disassembly" out) (search "Constants" out)) :ok :fail)))
(check "t4 string-trim char designator" "a" (string-trim "x" #\a))
(check "t4 string-right-trim symbol designator" "X" (string-right-trim "A" 'xa))
(deftype t4c-oct () '(unsigned-byte 8))
(check "t4 concatenate deftype compound type" '(1 2 3)
       (coerce (concatenate '(vector t4c-oct) #(1 2) #(3)) 'list))
(check "t4 #3A reader" '(1 6 8)
       (let ((a (read-from-string "#3A(((1 2) (3 4)) ((5 6) (7 8)))")))
         (list (aref a 0 0 0) (aref a 1 0 1) (aref a 1 1 1))))
(check "t4 nested read-from-string" '(a (x y) b)
       (read-from-string "(a #.(cl:read-from-string \"(x y)\") b)"))
(check "t4 cons feature expressions" '(7 8 9)
       (list (read-from-string "#+(and) 7")
             (read-from-string "#-(or) 8")
             (read-from-string "#+(and common-lisp (not t4c-no-such-ft)) 9")))
(check "t4 skip sentinel quote" '(quote bar)
       (read-from-string "'#+t4c-no-such-feature foo bar"))
(check "t4 skip sentinel function" '(function bar)
       (read-from-string "#'#+t4c-no-such-feature foo bar"))
(check "t4 skip sentinel dotted cdr" '(a . c)
       (read-from-string "(a . #+t4c-no-such-feature b c)"))
(check "t4 skip sentinel #nA contents" 4
       (aref (read-from-string "#2A#+t4c-no-such-feature x ((1 2) (3 4))") 1 1))
(check "t4 skip sentinel #. read-time eval" 5
       (read-from-string "#.#+t4c-no-such-feature x 5"))

; --- Tier-4 audit batch 4: sequences / lists / hashtable / VM opcodes ---
; Mirrors the host gc-stress tier4-seq block (see
; tests/test_gc_stress_regression.sh); behavior-level checks here.
(check "t4 sort list allocating pred" '(1 2 3 4 5 6 7 8 9)
       (sort (list 5 3 8 1 9 2 7 6 4)
             (lambda (a b) (< (car (list a)) (car (list b))))))
(check "t4 sort string heap key" "abcd"
       (sort (copy-seq "dcba") #'string< :key #'string))
(check "t4 sort bit-vector allocating pred" #*0011
       (sort (copy-seq #*0110)
             (lambda (a b) (< (car (list a)) (car (list b))))))
(check "t4 map vector cursors" '(2 4 6 8 10)
       (coerce (map 'vector (lambda (x) (* x 2)) (list 1 2 3 4 5)) 'list))
(check "t4 map string result" "HELLO"
       (map 'string #'char-upcase (coerce "hello" 'list)))
(deftype t4s-str () 'string)
(check "t4 map deftype result-type" "ABC" (map 't4s-str #'char-upcase "abc"))
(check "t4 map or result-type" '(2 3 4)
       (coerce (map '(or symbol (vector t 3)) #'1+ (list 1 2 3)) 'list))
(deftype t4s-lst () 'list)
(check "t4 merge deftype result-type" '(1 2 3 4 5 6)
       (merge 't4s-lst (list 1 3 5) (list 2 4 6) #'<))
(check "t4 merge vector allocating key" '(1 2 3 4)
       (coerce (merge 'vector (vector 1 4) (list 2 3) #'<
                      :key (lambda (x) (car (list x)))) 'list))
(check "t4 remove allocating test" '(1 2 4 5)
       (remove 3 (list 1 2 3 4 3 5)
               :test (lambda (a b) (eql a (car (list b))))))
(check "t4 remove-if from-end count" '(1 2 4 6)
       (remove-if (lambda (x) (oddp (car (list x))))
                  (list 1 2 3 4 5 6) :from-end t :count 2))
(check "t4 reduce from-end initial" '(1 2 3 4 . end)
       (reduce #'cons (list 1 2 3 4) :from-end t :initial-value 'end))
(check "t4 reduce allocating key" 6
       (reduce #'+ (list 1 2 3) :from-end t :key (lambda (x) (car (list x)))))
(check "t4 nsubst allocating test" '(a (x c) x)
       (nsubst 'x 'b (list 'a (list 'b 'c) 'b)
               :test (lambda (a b) (eq a (car (list b))))))
(check "t4 reverse vector" '(5 4 3 2 1)
       (coerce (reverse (vector 1 2 3 4 5)) 'list))
(check "t4 reverse bit-vector" #*01001 (reverse #*10010))
(check "t4 make-list heap init" '(12 (x))
       (let ((l (make-list 12 :initial-element (list 'x))))
         (list (count-if (lambda (e) (equal e '(x))) l) (first l))))
(check "t4 hash iteration" '(285 10)
       (let ((h (make-hash-table)))
         (dotimes (i 10) (setf (gethash i h) (* i i)))
         (list (loop for v being the hash-values of h sum v)
               (let ((n 0))
                 (maphash (lambda (k v) (declare (ignore k v)) (incf n)) h)
                 n))))
(deftype t4s-oct () '(unsigned-byte 8))
(deftype t4s-any () 't)
(check "t4 make-array contents before deftype elt" '(1 2 3 4)
       (coerce (make-array 4 :initial-contents (list 1 2 3 4)
                             :element-type 't4s-oct) 'list))
(check "t4 make-array list dims deftype elt" '(7)
       (aref (make-array (list 2 2) :initial-element (list 7)
                         :element-type 't4s-any) 1 1))
(check "t4 adjust-array negative dim" :caught
       (handler-case (adjust-array (make-array 3 :adjustable t) -1)
         (error () :caught)))
(check "t4 vpe negative extension" :caught
       (handler-case (let ((v (make-array 2 :fill-pointer 2 :adjustable t)))
                       (vector-push-extend 9 v -5))
         (error () :caught)))
(check "t4 defmacro value" 't4s-dm4 (defmacro t4s-dm4 (x) (list 'list x)))
(check "t4 deftype value" 't4s-ty4 (deftype t4s-ty4 () 'integer))
(defun t4s-get4 (x) (car x))
(defun t4s-set4 (x v) (setf (car x) v))
(check "t4 defsetf value" 't4s-get4 (defsetf t4s-get4 t4s-set4))
(deftype t4s-small4 () '(integer 0 9))
(check "t4 the deftype ok" 5 (the t4s-small4 5))
(check "t4 the deftype type-error slots" '(99 t4s-small4)
       (handler-case (let ((v 99)) (the t4s-small4 v))
         (type-error (c) (list (type-error-datum c)
                               (type-error-expected-type c)))))
(defun t4s-tr4 (n) (if (zerop n) (list 'done) (t4s-tr4 (1- n))))
(trace t4s-tr4)
(check "t4 traced tail-recursive fn" '(done) (t4s-tr4 3))
(untrace t4s-tr4)
(trace char-upcase)
(check "t4 traced builtin call" #\A (char-upcase #\a))
(untrace char-upcase)
(trace +)
(check "t4 traced builtin apply" 10 (apply #'+ 1 2 (list 3 4)))
(untrace +)

; --- Tier-4 batch 7a: fixed C-buffer caps removed (apply/format/strings) ---
; bi_apply's C path (reached when APPLY itself is funcalled) silently dropped
; args past 64; it now spreads onto the VM stack.
(check "t4b7 apply builtin past 64 args" 100
  (funcall #'apply #'+ (make-list 100 :initial-element 1)))
; remove_from_string truncated results at 1023 chars (char buf[1024]).
(check "t4b7 remove string past 1023 chars" 2000
  (length (remove #\a (make-string 2000 :initial-element #\b))))
; concatenate's string path truncated results at 4096 chars.
(check "t4b7 concatenate string past 4096 chars" 6000
  (length (concatenate 'string (make-string 3000 :initial-element #\x)
                       (make-string 3000 :initial-element #\y))))
; ~? staged its arg list in a 64-slot C array; FORMATTER's %formatter-inner
; and the ~:{ sublist path had the same cap.
(let ((t4b7-ctrl (with-output-to-string (s)
                   (dotimes (i 80) (write-string "~A" s)))))
  (check "t4b7 format ~? past 64 args" 80
    (length (format nil "~?" t4b7-ctrl (make-list 80 :initial-element 7))))
  (check "t4b7 formatter past 64 args" 80
    (length (with-output-to-string (s)
              (apply (eval (list 'formatter t4b7-ctrl)) s
                     (make-list 80 :initial-element 3))))))
(check "t4b7 format iteration sublist past 64 elements" 90
  (length (format nil "~:{~@{~A~}~}" (list (make-list 90 :initial-element 1)))))
; remove-duplicates / remove / sort / replace keep-flag and snapshot staging
; moved to GC objects (no leak when a user :test/:key does a non-local exit)
; -- behavior must be unchanged.
(check "t4b7 remove-duplicates keeps last occurrences" '(1 3 2 4)
  (remove-duplicates (list 1 2 1 3 2 4)))
(check "t4b7 replace self-overlap snapshot" '(1 1 2 3 4)
  (coerce (let ((v (vector 1 2 3 4 5)))
            (replace v v :start1 1 :start2 0 :end2 4))
          'list))
(check "t4b7 string sort staging" "abcd"
  (sort (copy-seq "dcba") #'char<))

; --- Tier-4 batch 7b: hashtable contract + map designators + reader caps ---
; keys_equal gained a bit-vector branch (CLHS EQUAL descends bit-vectors)
; and general vectors are EQ-keyed under EQUAL like the EQUAL builtin.
(check "t4b7 equal ht bit-vector keys" :bv
  (let ((ht (make-hash-table :test 'equal)))
    (setf (gethash #*1011 ht) :bv)
    (gethash (copy-seq #*1011) ht)))
(check "t4b7 equalp ht vector keys" :v
  (let ((ht (make-hash-table :test 'equalp)))
    (setf (gethash (vector 1 2 3) ht) :v)
    (gethash (vector 1 2 3) ht)))
(check "t4b7 equal ht general vector is eq-keyed" '(:id nil)
  (let ((ht (make-hash-table :test 'equal)) (v (vector 1 2)))
    (setf (gethash v ht) :id)
    (list (gethash v ht) (gethash (vector 1 2) ht))))
; MAPL/MAPCON accept symbol designators like their siblings.
(check "t4b7 mapl symbol designator" '(1 2) (mapl 'identity (list 1 2)))
(check "t4b7 mapcon symbol designator" '((1 2) (2)) (mapcon 'list (list 1 2)))
; The silent 16-list clamp on the map family is a loud error now.
(check "t4b7 mapcar over 16 lists errors" :err
  (handler-case (apply #'mapcar #'+ (make-list 17 :initial-element '(1)))
    (error () :err)))
; Reader token caps signal errors instead of silently splitting/truncating.
(check "t4b7 300-digit number read errors" :err
  (handler-case (read-from-string (make-string 300 :initial-element #\5))
    (error () :err)))
(check "t4b7 255-char symbol still reads" 255
  (length (symbol-name (read-from-string
                        (make-string 255 :initial-element #\a)))))

; --- Tier-4 batch 7c: print-control overrides are thread-local dynbinds ---
(check "t4b7c write-to-string base override + restore" '("FF" 10)
  (list (write-to-string 255 :base 16) *print-base*))
(check "t4b7c write stream base+radix override" "#b1010"
  (let ((s (make-string-output-stream)))
    (write 10 :stream s :base 2 :radix t)
    (get-output-stream-string s)))
(check "t4b7c format radix renderer" "FF/10/101"
  (format nil "~X/~O/~B" 255 8 5))
(check "t4b7c print-level override" "(1 #)"
  (write-to-string '(1 (2 (3))) :level 1))

; --- Optimize declarations (spec 1.3): folding / dead branches / scoping ---
; Constant folding must be value-transparent (these run with speed 1 default,
; so the calls below fold at compile time — results must match the runtime).
(check "opt fold arith" 13 (+ 1 (* 2 3) (- 10 4)))
(check "opt fold ash floor" -5 (ash -17 -2))
(check "opt fold logops" '(8 14 6) (list (logand 12 10) (logior 12 10) (logxor 12 10)))
(check "opt fold cmp chain" '(t nil) (list (< 1 2 3) (< 1 3 2)))
(check "opt fold not/null" '(t nil t) (list (not nil) (not 5) (null '())))
; Overflow must decline folding and reach the runtime bignum path
(check "opt fold overflow declines" 1073741824 (+ 1073741823 1))
(check "opt fold mul overflow declines" 1000000000000 (* 1000000 1000000))
; Dead-branch elimination: the dead branch is never compiled or run
(check "opt dead branch then" :live (if (> 2 1) :live (error "dead")))
(check "opt dead branch else" :else (if (= 1 2) (error "dead") :else))
(check "opt dead when nil" nil (when nil (error "dead")))
; FLET shadow suppresses folding of the CL builtin
(check "opt fold flet shadow" '(1 2) (flet ((+ (a b) (list a b))) (+ 1 2)))
; notinline suppresses folding and opcode inlining, value unchanged
(check "opt fold notinline" 3 (locally (declare (notinline +)) (+ 1 2)))
; speed 0 disables folding, value unchanged
(check "opt fold speed 0" 3 (locally (declare (optimize (speed 0))) (+ 1 2)))
; Scoping: body (declare (optimize (safety 0))) elides THE check inside...
(check "opt local safety 0 elides the-check" "x"
  (locally (declare (optimize (safety 0))) (the fixnum "x")))
; ...and is restored after the body ends (THE signals again)
(check "opt safety restored after body" :signaled
  (handler-case (eval '(the fixnum "y")) (error () :signaled)))
; destructuring-bind guards: signal at default safety, elided at safety 0
(check "opt dbind too-few signals" :signaled
  (handler-case (eval '(destructuring-bind (a b) '(1) (list a b)))
    (error () :signaled)))
(check "opt dbind guards elided at safety 0" '(1 nil)
  (locally (declare (optimize (safety 0)))
    (destructuring-bind (a b) '(1) (list a b))))

; --- Peephole post-pass (spec 1.8, speed >= 2) ---
; The (declaim (optimize (speed 3))) earlier in this file already routes
; most of the suite through the pass; these cases target each rewrite
; pattern and each guard explicitly.  The m68k JIT consumes the rewritten
; bytecode, so on Amiga this also validates the JIT walker over optimized
; streams.
(defun peep-setq-chain (x)
  (declare (optimize (speed 3)))
  (setq x (+ x 1)) (setq x (* x 2)) (setq x (- x 3)) x)
(check "peep setq chain" 19 (peep-setq-chain 10))
(defun peep-not-if (x)
  (declare (optimize (speed 3)))
  (if (not (< x 10)) :big :small))
(check "peep not-if small" :small (peep-not-if 5))
(check "peep not-if big" :big (peep-not-if 50))
(defun peep-loop (n)
  (declare (optimize (speed 3)))
  (let ((s 0)) (dotimes (i n) (setq s (+ s i))) s))
(check "peep loop backward jump" 4950 (peep-loop 100))
(defun peep-tagbody (n)
  (declare (optimize (speed 3)))
  (let ((s 0) (i 0))
    (tagbody
     top (when (>= i n) (go done))
         (setq s (+ s i)) (setq i (+ i 1)) (go top)
     done)
    s))
(check "peep tagbody landing pad" 4950 (peep-tagbody 100))
(defun peep-catch (x)
  (declare (optimize (speed 3)))
  (catch 'peep-tag (when x (throw 'peep-tag :thrown)) :fell))
(check "peep catch thrown" :thrown (peep-catch t))
(check "peep catch fell" :fell (peep-catch nil))
(defun peep-mv ()
  (declare (optimize (speed 3)))
  (progn 42 (values 1 2 3)))
(check "peep mv preserved" '(1 2 3) (multiple-value-list (peep-mv)))
(defun peep-discard-car (x)
  (declare (optimize (speed 3)))
  (progn (car x) :ok))
(check "peep discarded car signals" :signaled
  (handler-case (peep-discard-car 5) (type-error () :signaled)))
(check "peep discarded car ok" :ok (peep-discard-car '(1)))
(defun peep-closure (n)
  (declare (optimize (speed 3)))
  (let ((acc nil))
    (dotimes (i n) (push (let ((j i)) (lambda () j)) acc))
    (let ((s 0)) (dolist (f acc) (setq s (+ s (funcall f)))) s)))
(check "peep closure captures" 45 (peep-closure 10))
(defun peep-dead-branch (x)
  (declare (optimize (speed 3)))
  (if (< x 0) (if (< x -10) :ll :l) (if (> x 10) :h :m)))
(check "peep nested branches" '(:ll :l :m :h)
  (list (peep-dead-branch -50) (peep-dead-branch -5)
        (peep-dead-branch 5) (peep-dead-branch 50)))

; --- EXT TTY primitives (raw mode / size / console availability) ---
; Backing for TUI libraries (cl-tuition): EXT:TTY-RAW-MODE / EXT:TTY-SIZE
; plus exact LISTEN / READ-CHAR-NO-HANG on the console while raw.
; This suite runs with stdout redirected into the results log and stdin
; on the CLI console, so the assertions check the CONTRACT (types, no
; error, clean cooked-mode restore) rather than one environment's
; answers — the behavioral leg with a real pty lives in tests/test_tty.c.
(check "tty-p returns boolean" t (typep (ext:tty-p) 'boolean))
(check "tty-size nil or positive cell pair" t
  (let ((s (ext:tty-size)))
    (or (null s)
        (and (consp s)
             (integerp (car s)) (integerp (cdr s))
             (> (car s) 0) (> (cdr s) 0)))))
(check "tty-raw-mode toggles and restores" t
  (if (ext:tty-p)
      ; interactive console: both transitions must succeed, and a second
      ; disable must stay a successful no-op (idempotence)
      (and (ext:tty-raw-mode t)
           (ext:tty-raw-mode nil)
           (ext:tty-raw-mode nil)
           t)
      ; non-interactive stdin: enabling must fail cleanly with NIL
      (null (ext:tty-raw-mode t))))
; *TERMINAL-IO* must be bidirectional (CLHS) — it was historically bound
; to the output-only stdout singleton, so (read-char t) saw instant EOF.
(check "terminal-io is an input stream" t (input-stream-p *terminal-io*))
(check "terminal-io is an output stream" t (output-stream-p *terminal-io*))
(check "console listen does not block" t (typep (listen) 'boolean))
(check "console read-char-no-hang does not block" t
  (let ((c (read-char-no-hang *standard-input* nil :eof)))
    ; no typed input pending: NIL; leaked type-ahead: a character;
    ; redirected-and-drained stdin: :eof — never a hang, never an error
    (or (null c) (characterp c) (eq c :eof))))

; --- Boot logging is opt-in (--boot-log) ---
; The suite launches clamiga WITHOUT --boot-log, so the boot must have been
; quiet: %BOOT-QUIET-P reports T and %BOOT-TRACE-CLOS no-ops (returns NIL
; and writes nothing into the results log).
(check "boot is quiet by default" t (clamiga::%boot-quiet-p))
(check "boot trace no-ops when quiet" nil
  (clamiga::%boot-trace-clos 1 1 "must-not-appear-in-results-log"))

; --- Summary ---
(format t "~%=== Results ===~%")
(format t "Passed: ~A~%" *pass-count*)
(format t "Failed: ~A~%" *fail-count*)
(format t "Total:  ~A~%" (+ *pass-count* *fail-count*))
(if (= *fail-count* 0) (format t "~%ALL TESTS PASSED~%") (format t "~%SOME TESTS FAILED~%"))

; --- JIT benchmark (Amiga only; host has no native codegen) ---
; Runs after the test summary so its timings land in the same log the
; harness consumes.  Kept in trunk/ so the benchmark can iterate on
; its own cadence without touching the test file.
#+amigaos
(handler-case
  (load "trunk/bench-jit-loop.lisp")
  (error (e) (format t "ERROR running JIT bench: ~A~%" e)))
