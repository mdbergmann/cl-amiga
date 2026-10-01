#!/bin/sh
# The call generation word from Lisp: (clamiga::%call-gen) must move across
# every event that could leave a m68k JIT call site with a stale cached
# callee (specs/jit-direct-calls.md §1) -- each public way of changing a
# function cell (DEFUN from source and from a FASL, (SETF FDEFINITION),
# (SETF SYMBOL-FUNCTION), FMAKUNBOUND, a SETF function), every collection,
# TRACE/UNTRACE, and the thread events a looping caller must notice
# (MP:INTERRUPT-THREAD, MP:DESTROY-THREAD) -- and must NOT move on a plain
# call, since nothing on the per-call path may write shared memory.
# tests/test_call_gen.c pins the exact counts at the C level.
#
# Also run by `make test-gc-stress` (CLAMIGA_GC_STRESS=1: compaction on
# every allocation, so every allocating form bumps -- the checks below
# assert "moved", which a stress run must still satisfy, and the no-bump
# check uses a call loop that does not allocate).
# Run: sh tests/test_call_gen.sh [path-to-clamiga]

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
            echo "    got: $(echo "$haystack" | head -12)"
            failed=$((failed + 1)) ;;
    esac
}

WORK="${TMPDIR:-/tmp}/call_gen_$$"
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/defs.lisp" <<'EOF'
(defun call-gen-from-fasl () :fasl)
EOF

cat > "$WORK/gen.lisp" <<EOF
(defmacro moves (tag form)
  \`(let ((g (clamiga::%call-gen)))
     ,form
     (format t "~A ~A~%" ,tag (if (/= g (clamiga::%call-gen)) "MOVED" "STILL"))))
(format t "NONZERO ~A~%" (if (plusp (clamiga::%call-gen)) "YES" "NO"))
(moves "DEFUN" (defun cg-f () 1))
(moves "REDEFUN" (defun cg-f () 2))
(moves "SETF-FDEFINITION" (setf (fdefinition 'cg-f) (lambda () 3)))
(moves "SETF-SYMBOL-FUNCTION" (setf (symbol-function 'cg-f) (lambda () 4)))
(format t "VALUE ~A~%" (cg-f))
(moves "SETF-FUNCTION" (defun (setf cg-place) (v x) (list v x)))
(moves "FMAKUNBOUND" (fmakunbound 'cg-f))
(moves "FMAKUNBOUND-SETF" (fmakunbound '(setf cg-place)))
(format t "UNBOUND ~A~%" (list (fboundp 'cg-f) (fboundp '(setf cg-place))))
(compile-file "$WORK/defs.lisp" :output-file "$WORK/defs.fasl")
(moves "FASL-DEFUN" (load "$WORK/defs.fasl"))
(format t "FASL-VALUE ~A~%" (call-gen-from-fasl))
(defun cg-traced () 5)
(moves "TRACE" (trace cg-traced))
(moves "UNTRACE" (untrace cg-traced))
(moves "GC" (gc))
(let ((g (clamiga::%call-gen)))
  (dotimes (i 5) (gc))
  (format t "GC-EACH ~A~%" (if (>= (- (clamiga::%call-gen) g) 5) "YES" "NO")))
(defun cg-leaf (x) (+ x 1))
(defun cg-loop (n) (let ((s 0)) (dotimes (i n s) (setq s (cg-leaf s)))))
(cg-loop 10)
(let ((g (clamiga::%call-gen)))
  (cg-loop 10000)
  (format t "CALLS ~A~%" (if (= g (clamiga::%call-gen)) "STILL" "MOVED")))
;; Thread events: a caller looping on direct calls must miss and poll.
(let* ((stop nil)
       (th (mp:make-thread (lambda () (loop until stop do (mp:thread-yield)))
                           :name "cg-worker")))
  (sleep 0.05)
  (moves "INTERRUPT" (mp:interrupt-thread th (lambda () nil)))
  (moves "DESTROY" (mp:destroy-thread th))
  (setf stop t)
  (ignore-errors (mp:join-thread th)))
(format t "GEN-DONE~%")
EOF

out=$("$CLAMIGA" --no-userinit --non-interactive --load "$WORK/gen.lisp" </dev/null 2>&1)

check_contains "the script ran to the end" "GEN-DONE" "$out"
check_contains "the word starts non-zero (0 = an empty site)" "NONZERO YES" "$out"
check_contains "DEFUN bumps" "DEFUN MOVED" "$out"
check_contains "redefinition bumps" "REDEFUN MOVED" "$out"
check_contains "(SETF FDEFINITION) bumps" "SETF-FDEFINITION MOVED" "$out"
check_contains "(SETF SYMBOL-FUNCTION) bumps" "SETF-SYMBOL-FUNCTION MOVED" "$out"
check_contains "the last write is the one called" "VALUE 4" "$out"
check_contains "a SETF function's definition bumps" "SETF-FUNCTION MOVED" "$out"
check_contains "FMAKUNBOUND bumps" "FMAKUNBOUND MOVED" "$out"
check_contains "FMAKUNBOUND of (SETF name) bumps" "FMAKUNBOUND-SETF MOVED" "$out"
check_contains "both are unbound afterwards" "UNBOUND (NIL NIL)" "$out"
check_contains "a DEFUN loaded from a FASL bumps" "FASL-DEFUN MOVED" "$out"
check_contains "the FASL's definition is called" "FASL-VALUE FASL" "$out"
check_contains "TRACE bumps" "TRACE MOVED" "$out"
check_contains "UNTRACE bumps" "UNTRACE MOVED" "$out"
check_contains "(GC) bumps" "GC MOVED" "$out"
check_contains "every collection bumps, not only the first" "GC-EACH YES" "$out"
check_contains "calling a function does not bump" "CALLS STILL" "$out"
check_contains "MP:INTERRUPT-THREAD bumps" "INTERRUPT MOVED" "$out"
check_contains "MP:DESTROY-THREAD bumps" "DESTROY MOVED" "$out"

echo "$passed passed, $failed failed, $total total"
[ "$failed" -eq 0 ]
