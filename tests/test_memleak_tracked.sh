#!/bin/sh
# Every off-heap byte must be handed back before the process exits.
#
# Requires a -DDEBUG_MEM_TRACK binary (see `make test-memleak`), where every
# platform_alloc is recorded with the file and line that made it and
# CLAMIGA_MEM_DIAG=1 prints whatever is still outstanding at exit.
#
# Why this is a real test and not hygiene: AmigaOS does not reclaim a
# process's memory.  A block clamiga fails to free is Fast RAM gone from the
# system pool until the machine reboots — this suite exists because clamiga
# lost 3,743,160 bytes on every single launch, measured with `Avail` on a
# Vampire.  A host build cannot observe that at all, so the tracer is the only
# way to keep it fixed from CI.
#
# Each scenario below is run to completion and must report zero blocks live.
#
# Run: sh tests/test_memleak_tracked.sh build/host-memtrack/clamiga

CLAMIGA="${1:-build/host-memtrack/clamiga}"
case "$CLAMIGA" in
  /*) ;;
  *) CLAMIGA="$(pwd)/$CLAMIGA" ;;
esac
passed=0
failed=0
total=0

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# Confirm we were handed a tracked build — otherwise every check below would
# pass vacuously (no report line at all is not "no leak").
probe=$(CLAMIGA_MEM_DIAG=1 "$CLAMIGA" --no-userinit --eval '(quit)' --non-interactive </dev/null 2>&1)
case "$probe" in
  *"[mem] leak report:"*) ;;
  *)
    echo "  FAIL  binary_is_a_DEBUG_MEM_TRACK_build"
    echo "        no '[mem] leak report:' line — build with:"
    echo "        make host BUILDDIR=build/host-memtrack DEBUG_FLAGS=-DDEBUG_MEM_TRACK"
    echo ""
    echo "0 passed, 1 failed, 1 total"
    exit 1 ;;
esac

# run_case NAME FILE — run FILE and require a zero-byte leak report.
run_case() {
    name=$1
    file=$2
    total=$((total + 1))
    out=$(CLAMIGA_MEM_DIAG=1 "$CLAMIGA" --no-userinit --load "$file" </dev/null 2>&1)
    report=$(echo "$out" | sed -n 's/^\[mem\] leak report: \([0-9][0-9]*\) block(s), \([0-9][0-9]*\) bytes.*/\1 \2/p' | tail -1)
    blocks=$(echo "$report" | cut -d' ' -f1)
    bytes=$(echo "$report" | cut -d' ' -f2)
    if [ -z "$report" ]; then
        echo "  FAIL  $name (no leak report — did the run reach shutdown?)"
        echo "$out" | tail -8
        failed=$((failed + 1))
        return
    fi
    case "$out" in
      *"table full"*)
        echo "  FAIL  $name (tracker table overflowed — raise MT_CAP in mem_track.c)"
        failed=$((failed + 1))
        return ;;
    esac
    if [ "$blocks" = "0" ] && [ "$bytes" = "0" ]; then
        echo "  ok  $name"
        passed=$((passed + 1))
    else
        echo "  FAIL  $name ($bytes bytes in $blocks block(s) never freed)"
        echo "$out" | grep '^\[mem\]   ' | head -10
        failed=$((failed + 1))
    fi
}

# --- boot only: the whole live image must still be released ----------------
cat > "$WORK/nul.lisp" <<'LISPEOF'
(quit)
LISPEOF
run_case "no_leak_after_bare_boot" "$WORK/nul.lisp"

# --- compile churn: dead bytecode payloads must be swept -------------------
cat > "$WORK/churn.lisp" <<'LISPEOF'
(dotimes (i 500)
  (eval (list 'defun 'churn-victim (list 'x)
              (list '+ 'x i i i i i i i i))))
(gc)
(quit)
LISPEOF
run_case "no_leak_after_compile_churn" "$WORK/churn.lisp"

# --- CLOS, streams, locks, threads, string output, a dropped stream --------
cat > "$WORK/heavy.lisp" <<LISPEOF
(defclass pt () ((x :initarg :x :accessor px) (y :initarg :y :accessor py)))
(defmethod norm ((p pt)) (+ (abs (px p)) (abs (py p))))
(norm (make-instance 'pt :x 3 :y 4))
(let ((s (open "$WORK/o1.tmp" :direction :output :if-exists :supersede)))
  (format s "hello~%")
  (close s))
;; deliberately dropped without CLOSE — the sweep finalizer must reclaim it
(open "$WORK/o2.tmp" :direction :output :if-exists :supersede)
(with-open-file (s "$WORK/o1.tmp") (read-line s nil nil))
(let ((l (mp:make-lock))) (mp:with-lock-held (l) t))
(mp:join-thread (mp:make-thread (lambda () (loop for i from 1 to 500 sum i))))
(with-output-to-string (s) (format s "~a" (list 1 2 3)))
(gc)
(quit)
LISPEOF
run_case "no_leak_after_clos_streams_threads" "$WORK/heavy.lisp"

# --- compile-file + FASL load: the reader's own allocations ----------------
cat > "$WORK/src.lisp" <<'LISPEOF'
(defun fl-a (x) (+ x 1))
(defun fl-b (x &key (k 2) j) (list x k j))
(defmacro fl-m (x) `(list ,x))
(defun fl-c (x) (fl-m (fl-a x)))
LISPEOF
cat > "$WORK/fasl.lisp" <<LISPEOF
(compile-file "$WORK/src.lisp" :output-file "$WORK/src.fasl")
(load "$WORK/src.fasl")
(load "$WORK/src.fasl")
(gc)
(quit)
LISPEOF
run_case "no_leak_after_compile_file_and_fasl_load" "$WORK/fasl.lisp"

# --- (QUIT) from inside LOAD: the early-exit shutdown path -----------------
cat > "$WORK/inner.lisp" <<'LISPEOF'
(defun q-a (x) (+ x 1))
(quit)
LISPEOF
cat > "$WORK/quitload.lisp" <<LISPEOF
(load "$WORK/inner.lisp")
(format t "NOT-REACHED~%")
LISPEOF
run_case "no_leak_when_quit_inside_load" "$WORK/quitload.lisp"

echo ""
echo "$passed passed, $failed failed, $total total"
[ "$failed" -eq 0 ] || exit 1
exit 0
