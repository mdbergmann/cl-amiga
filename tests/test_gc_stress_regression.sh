#!/bin/sh
# GC-stress regression tests for unprotected-CL_Obj / stale-arena-pointer bugs.
#
# Each case reproduces a compaction-corruption bug that only fires when a
# moving GC runs *during* an allocating call.  Run against a binary built with
# -DDEBUG_GC_STRESS and with CLAMIGA_GC_STRESS=1, which forces a compacting GC
# before every allocation so the bug is deterministic (see CLAUDE.md "GC
# Safety" and the [GC-stress debug flag] memory note).
#
#   make test-gc-stress            # builds the stress binary and runs this
#   sh tests/test_gc_stress_regression.sh build/host-gcstress/clamiga
#
# The binary MUST be a DEBUG_GC_STRESS build — the env var is a no-op otherwise.

CLAMIGA="${1:-build/host-gcstress/clamiga}"
passed=0
failed=0
total=0
TMPDIR="${TMPDIR:-/tmp}"
WORK="$TMPDIR/clamiga_gcstress_$$"
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

if [ ! -x "$CLAMIGA" ]; then
    echo "  SKIP  GC-stress regression: no stress binary at $CLAMIGA"
    echo "0 passed, 0 failed, 0 total"
    exit 0
fi

# Compile a source file to a FASL with a CLEAN (non-stress) run, so each case
# exercises the *load* path under stress against a known-good FASL.  The env
# var must be fully UNSET (the runtime checks presence, not value — an empty
# CLAMIGA_GC_STRESS= still enables stress and would corrupt the FASL itself).
compile_fasl() {
    src="$1"; fasl="$2"
    env -u CLAMIGA_GC_STRESS timeout 60 "$CLAMIGA" --no-userinit --heap 64M \
        --non-interactive \
        --eval "(progn (compile-file \"$src\" :output-file \"$fasl\") (format t \"COMPILED~%\"))" \
        2>/dev/null | grep -q COMPILED
}

# Run under GC stress, capture stdout+stderr.
run_stress() {
    CLAMIGA_GC_STRESS=1 timeout 90 "$CLAMIGA" --no-userinit --heap 48M \
        --non-interactive --load "$1" 2>&1
}

check_contains() {
    desc="$1"; pattern="$2"; actual="$3"
    total=$((total + 1))
    if echo "$actual" | grep -q "$pattern"; then
        echo "  ok  $desc"
        passed=$((passed + 1))
    else
        echo "  FAIL  $desc (expected /$pattern/)"
        echo "$actual" | tail -4 | sed 's/^/      /'
        failed=$((failed + 1))
    fi
}

check_absent() {
    desc="$1"; pattern="$2"; actual="$3"
    total=$((total + 1))
    if echo "$actual" | grep -q "$pattern"; then
        echo "  FAIL  $desc (saw /$pattern/)"
        echo "$actual" | grep "$pattern" | head -3 | sed 's/^/      /'
        failed=$((failed + 1))
    else
        echo "  ok  $desc"
        passed=$((passed + 1))
    fi
}

# --- Case 1: COMPILE-FILE input pathname survives compaction ---------------
# Bug: cl_parse_namestring held a raw pointer into the arena string while
# allocating; compaction relocated it, truncating "/tmp/x.lisp" to "/".
cat > "$WORK/cf.lisp" <<'EOF'
(defun cf-fn () 42)
EOF
cat > "$WORK/cf-driver.lisp" <<EOF
(compile-file "$WORK/cf.lisp" :output-file "$WORK/cf.fasl")
(format t "CF-DONE~%")
EOF
out=$(run_stress "$WORK/cf-driver.lisp")
check_contains "compile-file resolves multi-component input path" "CF-DONE" "$out"
check_absent   "compile-file path not truncated to \"/\""           "cannot open file \"/\"" "$out"

# --- Case 2: PARSE-NAMESTRING / MERGE-PATHNAMES under stress ---------------
cat > "$WORK/pn.lisp" <<'EOF'
(format t "PN:~s~%" (namestring (parse-namestring "/tmp/sub/file.lisp")))
EOF
out=$(run_stress "$WORK/pn.lisp")
check_contains "parse-namestring keeps directory + name + type" 'PN:"/tmp/sub/file.lisp"' "$out"

# --- Case 3: defun loaded from FASL — OP_CLOSURE bytecode template --------
# Bug: OP_CLOSURE cached the bytecode-template CL_Obj across cl_alloc; a
# compaction relocated the template, so the closure's bytecode field went
# stale ("Corrupted closure: bytecode field is type 0") when later called.
cat > "$WORK/clo.lisp" <<'EOF'
(defpackage :gcstress-clo (:use :cl) (:export #:greet))
(in-package :gcstress-clo)
(defun greet (x) (format nil "Hi ~a" x))
EOF
if compile_fasl "$WORK/clo.lisp" "$WORK/clo.fasl"; then
    cat > "$WORK/clo-load.lisp" <<EOF
(load "$WORK/clo.fasl")
(format t "CLO:~a~%" (gcstress-clo:greet "world"))
EOF
    out=$(run_stress "$WORK/clo-load.lisp")
    check_contains "FASL-loaded defun is callable"             "CLO:Hi world" "$out"
    check_absent   "no corrupted-closure error"                "Corrupted closure" "$out"
else
    echo "  SKIP  defun FASL compile failed"
fi

# --- Case 4: FASL with gensyms + shared structure -------------------------
# Bug: the FASL reader's gensym_objs[]/shared_objs[] dedup tables were not
# GC-rooted, so a forward GENSYM_REF/OBJ_REF resolved to a relocated object's
# stale offset ("Undefined function: G<n>" / corrupted forms) on load.
cat > "$WORK/gs.lisp" <<'EOF'
(in-package :cl-user)
(defmacro gcstress-swap (a b)
  (let ((tmp (gensym "TMP")))
    `(let ((,tmp ,a)) (setf ,a ,b) (setf ,b ,tmp))))
(defun gcstress-use-swap (x y)
  (let ((p x) (q y)) (gcstress-swap p q) (list p q)))
(defvar *gcstress-shared* '(common . common))
(defun gcstress-shared () *gcstress-shared*)
EOF
if compile_fasl "$WORK/gs.lisp" "$WORK/gs.fasl"; then
    cat > "$WORK/gs-load.lisp" <<EOF
(load "$WORK/gs.fasl")
(format t "GS:~a ~a~%" (gcstress-use-swap 1 2) (gcstress-shared))
EOF
    out=$(run_stress "$WORK/gs-load.lisp")
    check_contains "FASL gensym/shared structure loads correctly" "GS:(2 1) (COMMON . COMMON)" "$out"
    check_absent   "no undefined-function from stale gensym ref"   "Undefined function" "$out"
else
    echo "  SKIP  gensym FASL compile failed"
fi

# --- Case 5: DEFINE-CONDITION under GC stress --------------------------------
# Bug: bi_register_condition_type cached `name`/`parent`/`slot_pairs` C locals
# before cl_cons() calls that can trigger compaction; stale offsets produced
# OOB-offset cons cells in condition_hierarchy / condition_slot_table.
cat > "$WORK/cond.lisp" <<'EOF'
(define-condition my-gcstress-condition (error)
  ((code :initarg :code :reader condition-code)
   (msg  :initarg :msg  :reader condition-msg)))
(let ((c (make-condition 'my-gcstress-condition :code 42 :msg "ok")))
  (format t "COND:~a ~a~%" (condition-code c) (condition-msg c)))
EOF
out=$(run_stress "$WORK/cond.lisp")
check_contains "define-condition works under GC stress"            "COND:42 ok" "$out"
check_absent   "no OOB offset in condition hierarchy/slot table"   "Undefined function\|type 0\|corrupted" "$out"

# --- Case 6: SETF GETHASH new key in EQ hashtable under GC stress -----------
# Bug: bi_setf_gethash captured bucket_idx + chain head before cl_cons();
# compaction relocated objects (EQ hashing is by offset) and rehashed the table,
# invalidating the stale bucket_idx — splicing onto wrong bucket cross-linked
# chains, corrupting subsequent GC walks of the table.
cat > "$WORK/ht.lisp" <<'EOF'
(let ((ht (make-hash-table :test 'eq)))
  (dotimes (i 20)
    (let ((k (make-symbol (format nil "K~a" i))))
      (setf (gethash k ht) (* i 10))))
  (let ((sum 0))
    (maphash (lambda (k v) (declare (ignore k)) (incf sum v)) ht)
    (format t "HT:~a~%" sum)))
EOF
out=$(run_stress "$WORK/ht.lisp")
check_contains "eq-hashtable setf-gethash new key correct under stress" "HT:1900" "$out"
check_absent   "no OOB offset in hash-table chains"                     "Undefined function\|type 0\|corrupted" "$out"

echo ""
echo "$passed passed, $failed failed, $total total"
[ "$failed" -eq 0 ]
