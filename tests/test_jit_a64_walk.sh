#!/bin/sh
# The AArch64 walker (specs/native-backend-a64.md): functions run as native
# code built from the interpreter's own helpers (phase 1) plus inline fast
# paths and a top-of-stack register cache (phase 2), so every result must be
# the interpreter's.  Two parts:
#
#   1. tests/amiga/test-jit.lisp -- the m68k JIT suite's behavioural checks
#      -- run against this backend, eager (the file sets the hot threshold
#      to 0 itself).  Its #+m68k checks are the m68k backend's own.
#   2. Checks of this backend: the shapes it compiles, results across GC
#      and errors, deep and self-tail recursion, redefinition, the
#      interpreter's multiple-value state, and a call-free native loop in
#      one thread while another collects (the loop poll, spec "Rule 4" --
#      without it the collection waits forever, so the run is bounded).
#      Phase 2: every inline fast path against its slow path -- fixnum
#      boundaries, other number types, type errors -- with a heap value
#      held in the register cache while the slow path runs, which under
#      CLAMIGA_GC_STRESS allocates and moves it.
#
# A build without the backend (x86-64, Linux, Windows, `make host JIT=0`)
# skips both parts.  Also run by `make test-gc-stress` (CLAMIGA_GC_STRESS=1).
# Run: sh tests/test_jit_a64_walk.sh [path-to-clamiga]

CLAMIGA="${1:-build/host/clamiga}"
passed=0
failed=0
total=0

check_contains() {
    desc="$1"
    needle="$2"
    haystack="$3"
    total=$((total + 1))
    case "$haystack" in
        *"$needle"*)
            echo "  ok  $desc"
            passed=$((passed + 1)) ;;
        *)
            echo "  FAIL  $desc"
            echo "    expected to contain: $needle"
            echo "    got: $(echo "$haystack" | tail -20)"
            failed=$((failed + 1)) ;;
    esac
}

TIMEOUT=$(command -v timeout 2>/dev/null || command -v gtimeout 2>/dev/null || true)

WORK="${TMPDIR:-/tmp}/jit_a64_walk_$$"
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

backend=$("$CLAMIGA" --no-userinit --non-interactive \
    --eval '(defun probe-fn () 1)' \
    --eval '(format t "BACKEND ~A~%" (if (clamiga::%jit-compile-stub (function probe-fn)) "A64" "NONE"))' \
    </dev/null 2>&1)
case "$backend" in
*"BACKEND A64"*) ;;
*)
    echo "  (no AArch64 backend in this build: walker checks skipped)"
    check_contains "%JIT-COMPILE-STUB is NIL without a backend" "BACKEND NONE" "$backend"
    echo "$passed passed, $failed failed, $total total"
    [ "$failed" -eq 0 ]
    exit $? ;;
esac

# 32M: four threads consing at once exhaust the 4M default heap, and a
# thread that does dies (MP:*THREAD-DEATH-HOOKS*) instead of signalling.
run() {
    if [ -n "$TIMEOUT" ]; then
        "$TIMEOUT" 300 "$CLAMIGA" --heap 32M --no-userinit --non-interactive --load "$1" </dev/null 2>&1
    else
        "$CLAMIGA" --heap 32M --no-userinit --non-interactive --load "$1" </dev/null 2>&1
    fi
}

# --- Part 1: the m68k suite's behavioural checks ------------------------------
cat > "$WORK/suite.lisp" <<'EOF'
(setq *pass-count* 0)
(setq *fail-count* 0)
(defmacro check (name expected actual)
  (let ((e (gensym)) (a (gensym)) (c (gensym)))
    `(handler-case
         (let ((,e ,expected) (,a ,actual))
           (if (equal ,e ,a)
               (setq *pass-count* (+ *pass-count* 1))
               (progn (setq *fail-count* (+ *fail-count* 1))
                      (format t "FAIL: ~A - expected ~S got ~S~%" ,name ,e ,a))))
       (error (,c)
         (setq *fail-count* (+ *fail-count* 1))
         (format t "FAIL: ~A - signaled error: ~A~%" ,name ,c)))))
(defmacro stress-check (name expected actual) `(check ,name ,expected ,actual))
(load "tests/amiga/test-jit.lisp")
(format t "~%SUITE pass=~D fail=~D~%" *pass-count* *fail-count*)
EOF
out=$(run "$WORK/suite.lisp")
check_contains "test-jit.lisp ran to the end" "SUITE pass=" "$out"
check_contains "every behavioural check of test-jit.lisp passes" "fail=0" "$out"
# Its checks number in the hundreds: guard against the file running only a few.
pass_n=$(echo "$out" | sed -n 's/.*SUITE pass=\([0-9]*\).*/\1/p')
total=$((total + 1))
if [ -n "$pass_n" ] && [ "$pass_n" -ge 350 ]; then
    echo "  ok  test-jit.lisp ran $pass_n checks"; passed=$((passed + 1))
else
    echo "  FAIL  test-jit.lisp ran only '$pass_n' checks"; failed=$((failed + 1))
fi

# --- Part 2: this backend ------------------------------------------------------
cat > "$WORK/walk.lisp" <<'EOF'
(clamiga::%jit-set-hot-threshold 0)
;; Under CLAMIGA_GC_STRESS every allocation compacts: fewer conses give the
;; same coverage of the collections the consing loops run into.
(defparameter *churn-n* (if (ext:getenv "CLAMIGA_GC_STRESS") 300 50000))
(defmacro chk (name form)
  `(format t "~A ~S~%" ,name
           (handler-case ,form (error (c) (list :error (princ-to-string c))))))
(defun native-p (f) (not (null (clamiga::%jit-dump-bytes f))))

;; Shapes phase 1 compiles: stack and locals, constants, branches and
;; loops, calls, arithmetic, lists, structs, vectors, strings, globals.
(defvar *w-global* 0)
(defstruct wpt x y)
(defun w-add (a b) (+ a b))
(defun w-sum (n) (let ((s 0)) (dotimes (i n s) (setq s (+ s i)))))
(defun w-fact (n) (if (<= n 1) 1 (* n (w-fact (- n 1)))))
(defun w-count (n acc) (if (= n 0) acc (w-count (- n 1) (+ acc 1))))
(defun w-deep (n) (if (= n 0) 0 (+ 1 (w-deep (- n 1)))))
(defun w-lists (a b c) (list a b c (cons a b) (car (list c)) (cdr (list a b)) (eq a a) (not b)))
(defun w-chars (s) (let ((n 0)) (dotimes (i (length s) n) (when (char= (char s i) #\a) (setq n (+ n 1))))))
(defun w-push (lst) (let ((acc nil)) (dolist (x lst acc) (push (* x x) acc))))
(defun w-pop (lst) (let ((acc 0)) (loop (when (null lst) (return acc)) (setq acc (+ acc (pop lst))))))
(defun w-struct (p) (setf (wpt-y p) (+ (wpt-x p) 1)) (wpt-y p))
(defun w-vec (v) (setf (aref v 1) 'x) (list (svref v 0) (aref v 1)))
(defun w-apply (f args) (apply f args))
(defun w-funcall (f x) (funcall f x))
(defun w-global () (setq *w-global* (+ *w-global* 1)))
(defun w-const () (list 'w-sym "str" '(1 2) 3.5))
(defun w-other (x) (w-add x 1))
(defun w-type (x) (the fixnum x))
(chk "NATIVE"
     (mapcar #'native-p (list #'w-add #'w-sum #'w-fact #'w-count #'w-deep #'w-lists
                              #'w-chars #'w-push #'w-pop #'w-struct #'w-vec #'w-apply
                              #'w-funcall #'w-global #'w-const #'w-other #'w-type)))

(chk "ADD" (list (w-add 1 2) (w-add most-positive-fixnum 1) (w-add 1/2 1/3) (w-add 1.5 2)))
(chk "SUM" (w-sum 100000))
(chk "FACT" (w-fact 25))
(chk "LISTS" (w-lists 1 nil 3))
(chk "CHARS" (w-chars "banana bandana"))
(chk "PUSH" (w-push '(1 2 3)))
(chk "POP" (w-pop '(1 2 3 4)))
(chk "STRUCT" (w-struct (make-wpt :x 41)))
(chk "VEC" (w-vec (vector 'a 'b)))
(chk "APPLY" (list (w-apply #'+ '(1 2 3)) (w-apply '+ (make-list 300 :initial-element 1))
                   (w-funcall #'1+ 9) (w-funcall 'car '(7))))
(chk "GLOBAL" (progn (setq *w-global* 10) (w-global) (w-global)))
(chk "TAIL-OTHER" (w-other 41))
(chk "TYPE" (w-type 5))

;; The native call stack: a deep non-tail recursion, a self tail call that
;; runs in one frame, and a runaway recursion that signals and recovers.
(chk "DEEP" (w-deep 2000))
(chk "SELF-TAIL" (w-count 1000000 0))
;; Mutual tail calls between native functions hand the callee to
;; cl_jit_invoke instead of nesting a C call: constant space.
(defun w-even (n) (if (= n 0) :even (w-odd (- n 1))))
(defun w-odd (n) (if (= n 0) :odd (w-even (- n 1))))
(chk "MUTUAL-TAIL" (list (native-p #'w-even) (w-even 1000000) (w-even 1000001)))
(chk "RUNAWAY" (handler-case (w-deep 10000000) (error () :overflowed)))
(chk "AFTER-RUNAWAY" (w-deep 10))

;; Errors from a helper unwind through native frames.
(chk "TYPE-ERROR" (handler-case (w-add 'a 1) (type-error () :te)))
(chk "THE-ERROR" (handler-case (w-type "s") (type-error () :te)))
(chk "UNDEFINED" (handler-case (funcall (compile nil '(lambda () (w-undefined-fn 1))))
                   (undefined-function () :undef)))
(chk "AFTER-ERRORS" (w-add 2 3))

;; Values held by native frames across collections (moving under gengc).
(defun w-gc (x) (let ((a (cons x (list x 'w-sym)))) (ext:gc) (list (car a) (cddr a) (w-const))))
(chk "GC" (w-gc "keep"))
(defun w-churn (n) (let ((acc nil)) (dotimes (i n) (setq acc (cons (list i i) acc))) (length acc)))
(chk "CHURN" (progn (w-churn *churn-n*) (ext:gc) (= (w-churn *churn-n*) *churn-n*)))
(chk "CONST-AFTER-GC" (progn (ext:gc) (w-const)))

;; Redefinition recompiles; the old code is not run again.
(defun w-add (a b) (- a b))
(chk "REDEFINED" (list (w-add 5 3) (native-p #'w-add) (w-other 41)))   ; w-other: 41-1

;; The interpreter's multiple-value state: a single-valued opcode resets it.
(defun w-mv () (values 1 2 3))
(defun w-car (x) (car x))
(chk "MV" (list (multiple-value-list (w-mv))
                (multiple-value-list (progn (w-mv) (w-car '(9))))
                (multiple-value-list (w-funcall #'values 4))))

;; The loop poll: a call-free native loop in a worker while this thread
;; collects.  The worker reaches a safepoint only at its loop header.
(defvar *w-stop* nil)
(defun w-spin () (let ((n 0)) (loop (when *w-stop* (return (> n 0))) (setq n (+ n 1)))))
(chk "SPIN-NATIVE" (native-p #'w-spin))
(chk "LOOP-POLL"
     (let ((th (mp:make-thread #'w-spin)))
       (sleep 0.05)
       (dotimes (i 3) (ext:gc))
       (setq *w-stop* t)
       (mp:join-thread th)))
;; Native code on several threads at once, consing (each collection stops
;; all).
(chk "THREADS"
     (let ((out (make-array 4 :initial-element nil)))
       (mapc #'mp:join-thread
             (loop for k below 4
                   collect (let ((k k))
                             (mp:make-thread
                              (lambda () (setf (svref out k) (list (w-churn *churn-n*) (w-fact 10))))))))
       (every (lambda (r) (equal r (list *churn-n* 3628800))) out)))

;; --- Phase 2: the fast paths and the top-of-stack cache ---------------------
;; X is a fresh heap object loaded before each operation, so it sits in the
;; cache while the operation's slow path calls out (and, for a bignum, a
;; ratio or a float, allocates: a moving collection under gc-stress).
(defstruct p2s a b c)
(defvar *p2-var* 10)
(defvar *p2-unbound*)
(defun p2-arith (x a b) (list x (+ a b) x (- a b) x (* a b) x))
(defun p2-cmp (x a b) (list x (< a b) x (> a b) x (<= a b) x (>= a b) x (= a b)))
(defun p2-br (a b)
  (let ((n 0))
    (when (< a b) (setq n (+ n 1)))
    (when (> a b) (setq n (+ n 10)))
    (when (<= a b) (setq n (+ n 100)))
    (when (>= a b) (setq n (+ n 1000)))
    (when (= a b) (setq n (+ n 10000)))
    (unless (< a b) (setq n (+ n 100000)))
    n))
(defun p2-chr (a b) (if (char= a b) :same :diff))
(defun p2-car (x l) (list x (car l) x (cdr l) x))
(defun p2-st (x s v) (list x (p2s-a s) x (setf (p2s-c s) v) x (p2s-c s)))
(defun p2-sref-bad (s) (clamiga::%struct-ref s 7))
(defun p2-sset-bad (s v) (clamiga::%struct-set s 7 v))
(defun p2-gread () *p2-var*)
(defun p2-gwrite (v) (setq *p2-var* v) *p2-var*)
(defun p2-gbr () (if *p2-var* :yes :no))
(defun p2-geq (x) (if (eq x *p2-var*) :eq :ne))
(defun p2-gub () *p2-unbound*)
(defun p2-pkg (p) (setq *package* p) (package-name *package*))
(defun p2-deep (a b c d e) (+ a (+ b (+ c (+ d (+ e (car (list a))))))))
(defun p2-deep-heap (a b c d) (list a b c d (+ (length a) most-positive-fixnum) a b c d))
(defun p2-eqnot (x y) (list x (eq x y) x (not x) (not nil) y))
(defun p2-mv (a) (+ a 1))
(chk "P2-NATIVE"
     (mapcar #'native-p (list #'p2-arith #'p2-cmp #'p2-br #'p2-chr #'p2-car #'p2-st
                              #'p2-sref-bad #'p2-sset-bad #'p2-gread #'p2-gwrite
                              #'p2-gbr #'p2-geq #'p2-gub #'p2-pkg #'p2-deep
                              #'p2-deep-heap #'p2-eqnot #'p2-mv)))
(defun p2-k () (list "k"))
(chk "P2-FIX" (p2-arith (p2-k) 3 4))
(chk "P2-MAX" (p2-arith (p2-k) most-positive-fixnum 1))
(chk "P2-MIN" (p2-arith (p2-k) most-negative-fixnum 1))
(chk "P2-MIN-1" (p2-arith (p2-k) most-negative-fixnum -1))
(chk "P2-SQ" (p2-arith (p2-k) 46341 46341))
(chk "P2-SQ-FITS" (p2-arith (p2-k) 23170 -23170))
(chk "P2-RATIO-FLOAT" (p2-arith (p2-k) 1/2 0.5))
(chk "P2-ARITH-TE" (handler-case (p2-arith (p2-k) 'a 1) (type-error () :te)))
(chk "P2-ARITH-TE2" (handler-case (p2-arith (p2-k) 1 "s") (type-error () :te)))
(chk "P2-CMP" (list (p2-cmp (p2-k) 1 2) (p2-cmp (p2-k) 2 2) (p2-cmp (p2-k) -3 -4)))
(chk "P2-CMP-MIXED" (list (p2-cmp (p2-k) 1 1.0) (p2-cmp (p2-k) 1/3 1) (p2-cmp (p2-k) (expt 2 40) 1)))
(chk "P2-CMP-TE" (handler-case (p2-cmp (p2-k) 'a 1) (type-error () :te)))
(chk "P2-CMP-COMPLEX" (handler-case (p2-cmp (p2-k) #c(1 2) 1) (type-error () :te)))
(chk "P2-BR" (list (p2-br 1 2) (p2-br 2 1) (p2-br 3 3) (p2-br 1.5 2) (p2-br 2 3/2)
                   (p2-br most-negative-fixnum most-positive-fixnum) (p2-br (expt 2 40) 0)))
(chk "P2-BR-TE" (handler-case (p2-br 1 'b) (type-error () :te)))
(chk "P2-CHR" (list (p2-chr #\a #\a) (p2-chr #\a #\b)
                    (handler-case (p2-chr 1 #\a) (type-error () :te))))
(chk "P2-CAR" (list (p2-car (p2-k) (cons 1 2)) (p2-car (p2-k) nil)))
(chk "P2-CAR-TE" (list (handler-case (p2-car (p2-k) 5) (type-error () :te))
                       (handler-case (p2-car (p2-k) (make-p2s)) (type-error () :te))
                       (handler-case (p2-car (p2-k) #\a) (type-error () :te))))
(chk "P2-STRUCT" (let ((s (make-p2s :a 'one))) (list (p2-st (p2-k) s (list 'v)) (p2s-c s))))
(chk "P2-STRUCT-TE" (list (handler-case (p2-st (p2-k) (cons 1 2) 3) (type-error () :te))
                          (handler-case (p2-st (p2-k) nil 3) (type-error () :te))
                          (handler-case (p2-st (p2-k) 7 3) (type-error () :te))))
(chk "P2-STRUCT-RANGE" (list (handler-case (p2-sref-bad (make-p2s)) (error () :range))
                             (handler-case (p2-sset-bad (make-p2s) 1) (error () :range))))
(chk "P2-GLOBAL" (progn (setq *p2-var* 10)
                        (list (p2-gread) (p2-gbr) (p2-geq 10) (p2-geq 11)
                              (let ((*p2-var* 20))
                                (list (p2-gread) (p2-gwrite 30) (p2-gread) (p2-geq 30)))
                              (p2-gread) (p2-gwrite nil) (p2-gbr) (p2-gwrite (list 1)))))
(chk "P2-UNBOUND" (handler-case (p2-gub) (unbound-variable () :unbound)))
(chk "P2-PACKAGE" (let ((*package* *package*))
                    (list (p2-pkg (find-package :keyword)) (package-name *package*)
                          (symbol-package (intern "P2-PKG-PROBE")))))
(chk "P2-DEEP" (list (p2-deep 1 2 3 4 5) (p2-deep most-positive-fixnum 1 1 1 1)))
(chk "P2-DEEP-HEAP" (p2-deep-heap "abc" (p2-k) (p2-k) (p2-k)))
(chk "P2-EQNOT" (list (p2-eqnot 'a 'a) (p2-eqnot nil 1)))
(chk "P2-MV" (list (multiple-value-list (progn (w-mv) (p2-mv 1)))
                   (multiple-value-list (progn (w-mv) (p2-mv most-positive-fixnum)))
                   (multiple-value-list (progn (w-mv) (p2-gread)))))
;; A struct slot written on four threads at once (the publication barrier).
(chk "P2-THREADS"
     (let ((out (make-array 4 :initial-element nil)))
       (mapc #'mp:join-thread
             (loop for k below 4
                   collect (let ((k k))
                             (mp:make-thread
                              (lambda ()
                                (let ((s (make-p2s)))
                                  (dotimes (i 2000) (p2-st (p2-k) s (list i)))
                                  (setf (svref out k) (p2s-c s))))))))
       (every (lambda (r) (equal r '(1999))) out)))

(chk "DISASM" (with-output-to-string (*standard-output*) (clamiga::%jit-disassemble #'w-add)))
(format t "DONE~%")
EOF
out=$(run "$WORK/walk.lisp")
check_contains "the script ran to the end" "DONE" "$out"
check_contains "the phase-1 shapes compile" \
    "NATIVE (T T T T T T T T T T T T T T T T T)" "$out"
check_contains "arithmetic: fixnum, bignum overflow, ratio, float" "ADD (3 1073741824 5/6 3.5)" "$out"
check_contains "a counting loop" "SUM 4999950000" "$out"
check_contains "a recursive bignum product" "FACT 15511210043330985984000000" "$out"
check_contains "list, cons, car, cdr, eq, not" "LISTS (1 NIL 3 (1) 3 (NIL) T T)" "$out"
check_contains "string scan: char, char=" "CHARS 6" "$out"
check_contains "push onto a local" "PUSH (9 4 1)" "$out"
check_contains "pop from a local" "POP 10" "$out"
check_contains "struct slot read and write" "STRUCT 42" "$out"
check_contains "vector aref, svref, aset" "VEC (A X)" "$out"
check_contains "apply and funcall, 300 spread arguments" "APPLY (6 300 10 7)" "$out"
check_contains "a special variable read and set" "GLOBAL 12" "$out"
check_contains "a tail call to another function returns its value" "TAIL-OTHER 42" "$out"
check_contains "THE passes a fixnum" "TYPE 5" "$out"
check_contains "deep native recursion" "DEEP 2000" "$out"
check_contains "a self tail call reuses its frame" "SELF-TAIL 1000000" "$out"
check_contains "mutual native tail calls run in constant space" "MUTUAL-TAIL (T :EVEN :ODD)" "$out"
check_contains "runaway native recursion signals" "RUNAWAY :OVERFLOWED" "$out"
check_contains "native code runs after the overflow" "AFTER-RUNAWAY 10" "$out"
check_contains "a type error unwinds through native code" "TYPE-ERROR :TE" "$out"
check_contains "THE signals a type error" "THE-ERROR :TE" "$out"
check_contains "an undefined function signals" "UNDEFINED :UNDEF" "$out"
check_contains "native code runs after the errors" "AFTER-ERRORS 5" "$out"
check_contains "locals survive a collection" 'GC ("keep" (W-SYM) (W-SYM "str" (1 2) 3.5))' "$out"
check_contains "consing loops across collections" "CHURN T" "$out"
check_contains "constants after a collection" 'CONST-AFTER-GC (W-SYM "str" (1 2) 3.5)' "$out"
check_contains "a redefinition recompiles" "REDEFINED (2 T 40)" "$out"
check_contains "multiple values: kept, reset, forwarded" "MV ((1 2 3) (9) (4))" "$out"
check_contains "the spin loop is native" "SPIN-NATIVE T" "$out"
check_contains "a native loop lets another thread collect" "LOOP-POLL T" "$out"
check_contains "native code on four threads" "THREADS T" "$out"
check_contains "the phase-2 shapes compile" \
    "P2-NATIVE (T T T T T T T T T T T T T T T T T T)" "$out"
check_contains "fixnum + - *" 'P2-FIX (("k") 7 ("k") -1 ("k") 12 ("k"))' "$out"
check_contains "+ overflows at the top" 'P2-MAX (("k") 1073741824 ("k") 1073741822 ("k") 1073741823 ("k"))' "$out"
check_contains "- overflows at the bottom, * reaches it" 'P2-MIN (("k") -1073741823 ("k") -1073741825 ("k") -1073741824 ("k"))' "$out"
check_contains "* of the minimum by -1 overflows" 'P2-MIN-1 (("k") -1073741825 ("k") -1073741823 ("k") 1073741824 ("k"))' "$out"
check_contains "a square past the fixnum range" 'P2-SQ (("k") 92682 ("k") 0 ("k") 2147488281 ("k"))' "$out"
check_contains "a negative product inside it" 'P2-SQ-FITS (("k") 0 ("k") 46340 ("k") -536848900 ("k"))' "$out"
check_contains "ratio and float arithmetic" 'P2-RATIO-FLOAT (("k") 1.0 ("k") 0.0 ("k") 0.25 ("k"))' "$out"
check_contains "+ of a symbol signals" "P2-ARITH-TE :TE" "$out"
check_contains "+ of a string signals" "P2-ARITH-TE2 :TE" "$out"
check_contains "fixnum comparisons" 'P2-CMP ((("k") T ("k") NIL ("k") T ("k") NIL ("k") NIL) (("k") NIL ("k") NIL ("k") T ("k") T ("k") T) (("k") NIL ("k") T ("k") NIL ("k") T ("k") NIL))' "$out"
check_contains "mixed comparisons" 'P2-CMP-MIXED ((("k") NIL ("k") NIL ("k") T ("k") T ("k") T) (("k") T ("k") NIL ("k") T ("k") NIL ("k") NIL) (("k") NIL ("k") T ("k") NIL ("k") T ("k") NIL))' "$out"
check_contains "< of a symbol signals" "P2-CMP-TE :TE" "$out"
check_contains "< of a complex signals" "P2-CMP-COMPLEX :TE" "$out"
check_contains "fused compare-and-branch" "P2-BR (101 101010 111100 101 101010 101 101010)" "$out"
check_contains "a fused < of a symbol signals" "P2-BR-TE :TE" "$out"
check_contains "char= in a branch" "P2-CHR (:SAME :DIFF :TE)" "$out"
check_contains "car and cdr of a cons and of NIL" 'P2-CAR ((("k") 1 ("k") 2 ("k")) (("k") NIL ("k") NIL ("k")))' "$out"
check_contains "car of a fixnum, a struct, a char signals" "P2-CAR-TE (:TE :TE :TE)" "$out"
check_contains "struct slot read and write" 'P2-STRUCT ((("k") ONE ("k") (V) ("k") (V)) (V))' "$out"
check_contains "a struct accessor on a non-struct signals" "P2-STRUCT-TE (:TE :TE :TE)" "$out"
check_contains "a slot index past the struct signals" "P2-STRUCT-RANGE (:RANGE :RANGE)" "$out"
check_contains "special variables: global, bound, set" "P2-GLOBAL (10 :YES :EQ :NE (20 30 30 :EQ) 10 NIL :NO (1))" "$out"
check_contains "an unbound variable signals" "P2-UNBOUND :UNBOUND" "$out"
check_contains "setq of *package* switches the package" 'P2-PACKAGE ("KEYWORD" "KEYWORD" #<PACKAGE KEYWORD>)' "$out"
check_contains "a deep expression spills the cache" "P2-DEEP (16 2147483650)" "$out"
check_contains "heap values across a spill and a bignum" 'P2-DEEP-HEAP ("abc" ("k") ("k") ("k") 1073741826 "abc" ("k") ("k") ("k"))' "$out"
check_contains "eq and not from the cache" "P2-EQNOT ((A T A NIL T A) (NIL NIL NIL T T 1))" "$out"
check_contains "the fast and slow paths reset the values count" "P2-MV ((2) (1073741824) ((1)))" "$out"
check_contains "struct writes on four threads" "P2-THREADS T" "$out"
check_contains "the disassembler decodes the frame" "stp x29, x30, [sp, #-80]!" "$out"
check_contains "the disassembler decodes a fast path" "tbz w9, #0," "$out"
check_contains "the disassembler decodes a helper call" "blr x16" "$out"

echo "$passed passed, $failed failed, $total total"
[ "$failed" -eq 0 ]
