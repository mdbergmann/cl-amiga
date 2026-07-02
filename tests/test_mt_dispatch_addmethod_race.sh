#!/bin/sh
# Regression test: concurrent (re)definition of a method must not make a
# peer thread's dispatch of the SAME generic function spuriously signal
# "No applicable method" for a method that plainly exists.
#
# Root cause this guards (lib/clos.lisp %install-method-in-gf): the method
# list was updated in TWO stores — first store the old list minus any method
# it replaces, THEN cons the new method on:
#
#     (%set-gf-methods gf (remove-if ...replaced... (gf-methods gf)))  ; A
#     (%set-gf-methods gf (cons method (gf-methods gf)))               ; B
#
# Between A and B, GF-METHODS transiently EXCLUDED the method being
# (re)defined.  A dispatcher on another thread that walked the list in that
# window computed an empty applicable-method set and wrongly signalled
# "No applicable method".  This is the multi-threaded race behind the ASDF
# field reports "No applicable method for ACTION-STATUS with args of types
# (NULL LOAD-OP SYSTEM)" during (asdf:load-system :chipi/tests): worker /
# watcher threads (log4cl, sento) dispatch GFs while the main thread
# (re)defines methods.
#
# Fix: update the method list with a SINGLE atomic %set-gf-methods store
# (new method consed onto the filtered old list), so a concurrent reader
# always observes a COMPLETE method list — either the old set or the new
# set, both of which contain every applicable method.
#
# The single-threaded REPL never hit this: it only mutates the method list
# at its own defmethod points, with no peer reading in between.  The scenario
# below runs several dispatcher threads calling a null-specialized 3-arg GF
# (mirroring ACTION-STATUS's ((plan null) (o operation) (c component)) method)
# while redefiner threads repeatedly redefine that exact method.  Before the
# fix thousands of dispatches per run wrongly error; after it every run
# reports zero failures and a genuine no-applicable-method still errors.
#
# Run: sh tests/test_mt_dispatch_addmethod_race.sh build/host/clamiga

CLAMIGA="${1:-build/host/clamiga}"

TIMEOUT=$(command -v timeout 2>/dev/null || command -v gtimeout 2>/dev/null || true)
if [ -z "$TIMEOUT" ]; then
    echo "SKIP test_mt_dispatch_addmethod_race: neither timeout nor gtimeout on PATH"
    exit 0
fi

tmp=$(mktemp "${TMPDIR:-/tmp}/clamiga_mtdisp_XXXXXX") || exit 1
cleanup() { rm -f "$tmp"; }
trap cleanup EXIT INT TERM

cat > "$tmp" <<'EOF'
;; A GF shaped like ASDF's ACTION-STATUS: a NULL-specialized method that only
;; applies when the first arg is NIL, alongside a class-specialized method.
(defclass op () ())
(defclass load-op (op) ())
(defclass comp () ())
(defclass sys (comp) ())
(defgeneric act-status (plan operation component))
(defmethod act-status ((plan null) (o op) (c comp)) :null-method)
(defmethod act-status ((p comp) (o op) (c comp)) :plan-method)
(defparameter *o* (make-instance 'load-op))
(defparameter *c* (make-instance 'sys))
(defparameter *fails* 0)
(defparameter *lk* (mp:make-lock))

;; Dispatchers: (act-status nil o c) must ALWAYS resolve to the null method.
(defun dispatcher ()
  (dotimes (i 20000)
    (let ((r (handler-case (act-status nil *o* *c*) (error () :FAIL))))
      (unless (eq r :null-method)
        (mp:with-lock-held (*lk*) (setf *fails* (+ *fails* 1)))))))

;; Redefiners: repeatedly re-install the null method (each install first
;; removes the existing matching method — the exact window the fix closes).
(defun redefiner ()
  (dotimes (i 3000)
    (eval '(defmethod act-status ((plan null) (o op) (c comp)) :null-method))))

(let ((threads '()))
  (dotimes (i 4) (push (mp:make-thread #'dispatcher) threads))
  (dotimes (i 3) (push (mp:make-thread #'redefiner) threads))
  (dolist (th threads) (mp:join-thread th)))

;; A GENUINE no-applicable-method must still error (the fix must not mask it).
(defgeneric only-str (x))
(defmethod only-str ((x string)) :s)
(let ((genuine (handler-case (only-str 42) (error () :caught))))
  (format t "MT-DISP fails=~a genuine=~a~%" *fails* genuine))

;; Writer-writer race: two threads installing GENUINELY DIFFERENT methods
;; (distinct EQL specializers) on the SAME gf concurrently.  This targets the
;; lost-update window %install-method-in-gf's single-store fix does NOT
;; close on its own: two writers can both read the same old GF-METHODS
;; before either stores, so whichever %set-gf-methods lands second silently
;; discards the other thread's method.  Guarded by *gf-methods-lock* in
;; lib/clos.lisp.  Unlike the redefiner test above (which repeatedly
;; reinstalls the SAME method, so a lost update is invisible), every method
;; installed here is unique, so a lost update leaves a permanent gap.
(defgeneric wgf (x))
(defparameter *n-per-writer* 400)
(defun writer (base)
  (dotimes (i *n-per-writer*)
    (let ((n (+ base i)))
      (eval `(defmethod wgf ((x (eql ,n))) ,n)))))

(let ((threads '()))
  (dotimes (w 2)
    (push (mp:make-thread (let ((b (* w *n-per-writer*)))
                             (lambda () (writer b))))
          threads))
  (dolist (th threads) (mp:join-thread th)))

(let ((missing 0))
  (dotimes (n (* 2 *n-per-writer*))
    (let ((v (handler-case (wgf n) (error () :FAIL))))
      (unless (eql v n) (setf missing (+ missing 1)))))
  (format t "MT-WRITERS missing=~a~%" missing))
EOF

# Pre-fix this errors hundreds of times per run (deterministic); the fixed
# build is deterministically clean.  A few repeats guard against scheduling
# variance masking a regression.
RUNS=5
i=0
while [ "$i" -lt "$RUNS" ]; do
    i=$((i + 1))
    out=$("$TIMEOUT" 60 "$CLAMIGA" --no-userinit --heap 10M --non-interactive --load "$tmp" </dev/null 2>&1)
    ec=$?
    if [ "$ec" -eq 124 ]; then
        echo "  FAIL mt_dispatch_addmethod_race (run $i/$RUNS timed out)"
        echo "    output: $(echo "$out" | tail -5)"
        echo ""; echo "0 passed, 1 failed, 1 total"; echo "FAIL"; exit 1
    elif [ "$ec" -ne 0 ]; then
        echo "  FAIL mt_dispatch_addmethod_race (run $i/$RUNS ec=$ec — crash)"
        echo "    output: $(echo "$out" | tail -8)"
        echo ""; echo "0 passed, 1 failed, 1 total"; echo "FAIL"; exit 1
    elif ! echo "$out" | grep -q "MT-DISP fails=0 genuine=CAUGHT"; then
        echo "  FAIL mt_dispatch_addmethod_race (run $i/$RUNS — spurious no-applicable-method or masked genuine miss)"
        echo "    output: $(echo "$out" | grep 'MT-DISP' | head -2)"
        echo ""; echo "0 passed, 1 failed, 1 total"; echo "FAIL"; exit 1
    elif ! echo "$out" | grep -q "MT-WRITERS missing=0"; then
        echo "  FAIL mt_dispatch_addmethod_race (run $i/$RUNS — writer-writer race lost a distinct method install)"
        echo "    output: $(echo "$out" | grep 'MT-WRITERS' | head -2)"
        echo ""; echo "0 passed, 1 failed, 1 total"; echo "FAIL"; exit 1
    fi
done

echo "  ok  mt_dispatch_addmethod_race ($RUNS runs clean)"
echo ""
echo "1 passed, 0 failed, 1 total"
echo "PASS"
exit 0
