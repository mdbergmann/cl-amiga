#!/bin/sh
# Regression test: SLOT-VALUE / (SETF SLOT-VALUE) / SLOT-BOUNDP on
# DEFSTRUCT instances after the spec-3.1 registry hash index +
# %STRUCT-SLOT-INDEX optimization (builtins_struct.c, clos.lisp
# %find-struct-slot-index).
#
# The pre-optimization path resolved a struct slot on EVERY access via a
# linear walk of the prepended struct registry alist plus a freshly
# consed slot-name list.  These checks pin the semantics the fast path
# must preserve:
#   1. slot-value / setf / slot-boundp / with-slots on a struct instance
#   2. a type buried behind 300 later registrations still resolves
#   3. re-registering a type name: the NEWEST entry wins
#   4. missing slot signals an error naming the slot
#   5. lookups still resolve after an explicit compaction (index rebuild)
#
# Run: sh tests/test_struct_slot_access.sh build/host/clamiga

CLAMIGA="${1:-build/host/clamiga}"
passed=0
failed=0
total=0

run_quiet() {
    "$CLAMIGA" --no-userinit --batch 2>&1 <<EOF
(setf *load-verbose* nil)
$1
EOF
}

check_contains() {
    desc="$1"; pattern="$2"; out="$3"
    total=$((total + 1))
    if echo "$out" | grep -q "$pattern"; then
        echo "  ok  $desc"
        passed=$((passed + 1))
    else
        echo "  FAIL  $desc (missing: $pattern)"
        echo "    output: $(echo "$out" | head -5)"
        failed=$((failed + 1))
    fi
}

# --- 1. Basic slot-value protocol on a struct instance ---
out=$(run_quiet '
(defstruct ssa-pt (x 1) (y 2))
(let ((s (make-ssa-pt :x 3 :y 4)))
  (format t "SV:~a~%" (+ (slot-value s (quote x)) (slot-value s (quote y))))
  (setf (slot-value s (quote y)) 10)
  (format t "SETF:~a~%" (slot-value s (quote y)))
  (format t "BOUNDP:~a~%" (slot-boundp s (quote x)))
  (with-slots (x y) s
    (format t "WITH-SLOTS:~a~%" (+ x y))))')
check_contains "slot-value reads struct slots"        "SV:7"           "$out"
check_contains "setf slot-value writes struct slots"  "SETF:10"        "$out"
check_contains "slot-boundp true on struct slot"      "BOUNDP:T"       "$out"
check_contains "with-slots works on struct instance"  "WITH-SLOTS:13"  "$out"

# --- 2. Type buried behind 300 later registrations still resolves ---
out=$(run_quiet '
(defstruct ssa-early (a 100) (b 200))
(dotimes (i 300)
  (clamiga::%register-struct-type
   (make-symbol (format nil "SSA-FILLER-~D" i)) 1 nil (quote ((f nil)))))
(let ((s (make-ssa-early)))
  (format t "BURIED:~a~%" (+ (slot-value s (quote a)) (slot-value s (quote b))))
  (format t "TYPEP:~a~%" (typep s (quote ssa-early))))')
check_contains "buried type slot-value resolves" "BURIED:300" "$out"
check_contains "buried type typep resolves"      "TYPEP:T"    "$out"

# --- 3. Re-registration: newest entry wins ---
out=$(run_quiet '
(clamiga::%register-struct-type (quote ssa-re) 1 nil (quote ((a nil))))
(clamiga::%register-struct-type (quote ssa-re) 3 nil (quote ((p nil) (q nil) (r nil))))
(format t "COUNT:~a~%" (clamiga::%struct-slot-count (quote ssa-re)))
(format t "NEWSLOT:~a~%" (clamiga::%struct-slot-index (quote ssa-re) (quote r)))
(format t "OLDSLOT:~a~%" (clamiga::%struct-slot-index (quote ssa-re) (quote a)))')
check_contains "redefined type has new slot count"  "COUNT:3"     "$out"
check_contains "redefined type resolves new slot"   "NEWSLOT:2"   "$out"
check_contains "redefined type drops old slot"      "OLDSLOT:NIL" "$out"

# --- 4. Missing slot errors and names the slot ---
out=$(run_quiet '
(defstruct ssa-err (v 1))
(handler-case (slot-value (make-ssa-err) (quote no-such-slot))
  (error (e) (format t "ERR:~a~%" e)))')
check_contains "missing slot signals error"     "ERR:"         "$out"
check_contains "missing slot error names slot"  "NO-SUCH-SLOT" "$out"

# --- 5. Lookups resolve after explicit compaction (index rebuild) ---
out=$(run_quiet '
(defstruct ssa-gc (v 41))
(format t "PRE:~a~%" (slot-value (make-ssa-gc) (quote v)))
(ext:gc-compact)
(format t "POST:~a~%" (slot-value (make-ssa-gc) (quote v)))
(ext:gc-compact)
(format t "IDX:~a~%" (clamiga::%struct-slot-index (quote ssa-gc) (quote v)))')
check_contains "slot-value before compaction"       "PRE:41"  "$out"
check_contains "slot-value after compaction"        "POST:41" "$out"
check_contains "slot index rebuilt after compaction" "IDX:0"   "$out"

echo ""
echo "$passed passed, $failed failed, $total total"
if [ "$failed" -gt 0 ]; then
    echo "FAIL"
    exit 1
fi
echo "PASS"
exit 0
