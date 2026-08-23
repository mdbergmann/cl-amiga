#!/bin/sh
# End-to-end tests for heap images (EXT:SAVE-IMAGE / --image) — the
# two-process legs the in-process units (tests/test_image.c) cannot cover:
# a full-boot session saves an image, a separate process restores it and
# must find every kind of session state intact.
#
# Covered:
#   save from --eval, restore via --image: functions, CLOS dispatch,
#     macros, EQ-hashtable identity, closures, readtable customization
#   restore into a larger --heap than the save used
#   (ext:save-image f :quit t): image written after the enclosing form,
#     later actions skipped, EXT:*EXIT-HOOKS* still run
#   deferred-dump semantics at the REPL (batch): the session continues
#     after the dump, state created before the save is in the image
#   corrupt image → clean refusal (explicit --image exits 1)
#   auto-discovery of clamiga.img in the cwd; --no-image bypasses it
#   ~/.clamigarc runs after a restore with EXT:*IMAGE-RESTORED-P* = T
#
# Run: sh tests/test_image.sh build/host/clamiga

CLAMIGA="${1:-build/host/clamiga}"
case "$CLAMIGA" in
    /*) : ;;
    *) CLAMIGA="$(pwd)/$CLAMIGA" ;;
esac

TIMEOUT=$(command -v timeout 2>/dev/null || command -v gtimeout 2>/dev/null || true)
if [ -z "$TIMEOUT" ]; then
    echo "SKIP test_image: neither timeout nor gtimeout on PATH"
    exit 0
fi

passed=0
failed=0

WORK=$(mktemp -d "${TMPDIR:-/tmp}/clamiga_image_XXXXXX") || exit 1
trap 'rm -rf "$WORK"' EXIT INT TERM
cd "$WORK" || exit 1

CLI="--no-userinit --no-image"

fail() {
    desc="$1"; why="$2"; out="$3"
    failed=$((failed + 1))
    echo "  FAIL  $desc ($why)"
    echo "    output: $(echo "$out" | head -8)"
}

ok() { passed=$((passed + 1)); echo "  ok  $desc"; }

check() {
    desc="$1"; want_ec="$2"; ec="$3"; out="$4"
    shift 4
    if [ "$ec" -eq 124 ]; then
        fail "$desc" "timed out" "$out"
        return 1
    fi
    if [ "$ec" -ne "$want_ec" ]; then
        fail "$desc" "exit code $ec, wanted $want_ec" "$out"
        return 1
    fi
    for marker in "$@"; do
        if ! echo "$out" | grep -q "$marker"; then
            fail "$desc" "missing marker $marker" "$out"
            return 1
        fi
    done
    ok
    return 0
}

check_absent() {
    # last argument style: assert marker NOT in output
    desc="$1"; out="$2"; marker="$3"
    if echo "$out" | grep -q "$marker"; then
        fail "$desc" "unexpected marker $marker" "$out"
        return 1
    fi
    ok
    return 0
}

# --- Build a full-boot session and save it -------------------------------

cat > state.lisp <<'EOF'
(defun im-fib (n) (if (< n 2) n (+ (im-fib (- n 1)) (im-fib (- n 2)))))
(defmacro im-twice (x) `(* 2 ,x))
(defclass im-animal () ((name :initarg :name :accessor im-name)))
(defmethod im-speak ((a im-animal)) (format nil "~a speaks" (im-name a)))
(defvar *im-pet* (make-instance 'im-animal :name "Rex"))
(defvar *im-ht* (make-hash-table :test 'eq))
(defvar *im-key* (cons 'k 'v))
(setf (gethash *im-key* *im-ht*) 'im-hit)
(defvar *im-close* (let ((n 31)) (lambda (x) (+ x n))))
(set-macro-character #\! (lambda (s c) (declare (ignore s c)) 4242))
;; FFI stubs (DEFCFUN / DEFCSTRUCT binding descriptors): pure heap data,
;; must come back callable with their fields intact.
(require "amiga/ffi")
(defvar *im-base* nil)
(amiga.ffi:defcfun im-lc *im-base* -30 (:a0 x :d0 y) :result :u16)
(ffi:defcstruct (im-pt :size 8) (x :i16 0) (arr (:array :u8 4) 4))
;; A demand-interned binding table (bindtab.c): the blob hangs off the
;; package (CL_Package.bindings), one name built before the save, the rest
;; must still materialise lazily after the restore.
(defpackage "IM-BT" (:use "CL"))
(amiga.ffi:define-binding-table "IM-BT" (:base *im-base*)
  (:const "+BEFORE+" 11) (:const "+AFTER+" 22) (:fn "IM-BT-CALL" -36 (:a0) :bool)
  (:struct "IM-NODE" 4 ("X" :i16 0)))
(defvar *im-bt-before* 'im-bt:+before+)
(format t "STATE-LOADED~%")
EOF

cat > verify.lisp <<'EOF'
(format t "FIB=~a~%" (im-fib 12))
(format t "MACRO=~a~%" (im-twice 21))
(format t "CLOS=~a~%" (im-speak *im-pet*))
(format t "HT=~a~%" (gethash *im-key* *im-ht*))
(format t "CLOSURE=~a~%" (funcall *im-close* 11))
(format t "READER=~a~%" (read-from-string "!"))
(format t "RESTOREDP=~a~%" ext:*image-restored-p*)
(format t "STUB=~a~%" (list (getf (ffi::%ffi-stub-info #'im-lc) :result)
                            (getf (ffi::%ffi-stub-info #'im-lc) :lvo)
                            (let ((m (ffi:alloc-foreign 8)))
                              (setf (im-pt-x m) -7 (im-pt-arr m 3) 9)
                              (prog1 (list (im-pt-x m) (im-pt-arr m 3))
                                (ffi:free-foreign m)))
                            (handler-case (im-lc 1 2)
                              (error (e) (if (search "not open" (format nil "~a" e)) :not-open e)))))
(format t "BT=~a~%" (list (getf (clamiga::%binding-table-info "IM-BT") :entries)
                          (eq *im-bt-before* 'im-bt:+before+)
                          (multiple-value-bind (s st) (find-symbol "+AFTER+" "IM-BT") (list st (symbol-value s)))
                          (getf (ffi::%ffi-stub-info #'im-bt:im-bt-call) :lvo)
                          im-bt:*im-node-size*))
(dotimes (i 20000) (cons i i))
(ext:gc)
(format t "GC-OK=~a~%" (im-fib 10))
EOF

out=$("$TIMEOUT" 60 "$CLAMIGA" $CLI --heap 8M --non-interactive \
    --load state.lisp --eval '(ext:save-image "session.img")' </dev/null 2>&1)
ec=$?
check "save_full_boot_session" 0 "$ec" "$out" STATE-LOADED "Image saved"

# --- Restore and verify every state class --------------------------------

out=$("$TIMEOUT" 60 "$CLAMIGA" --no-userinit --image session.img \
    --non-interactive --load verify.lisp </dev/null 2>&1)
ec=$?
check "restore_verifies_state" 0 "$ec" "$out" \
    "FIB=144" "MACRO=42" "CLOS=Rex speaks" "HT=IM-HIT" "CLOSURE=42" \
    "READER=4242" "RESTOREDP=T" "STUB=(U16 -30 (-7 9) NOT-OPEN)" \
    "BT=(5 T (EXTERNAL 22) -36 4)" "GC-OK=55"

# --- Restore into a larger heap ------------------------------------------

out=$("$TIMEOUT" 60 "$CLAMIGA" --no-userinit --image session.img --heap 48M \
    --non-interactive --eval '(format t "BIGHEAP=~a~%" (im-fib 12))' </dev/null 2>&1)
ec=$?
check "restore_into_larger_heap" 0 "$ec" "$out" "BIGHEAP=144"

# --- :quit t: dump after the form, skip later actions, run exit hooks ----

out=$("$TIMEOUT" 60 "$CLAMIGA" $CLI --heap 8M --non-interactive \
    --eval '(ext:add-exit-hook (lambda () (format t "EXIT-HOOK~%")))' \
    --eval '(defvar *im-q* 7)' \
    --eval '(ext:save-image "quit.img" :quit t)' \
    --eval '(format t "NOT-REACHED~%")' </dev/null 2>&1)
ec=$?
check "save_quit_writes_and_exits" 0 "$ec" "$out" "Image saved" "EXIT-HOOK"
desc="save_quit_skips_later_actions"
check_absent "$desc" "$out" "NOT-REACHED"

out=$("$TIMEOUT" 60 "$CLAMIGA" --no-userinit --image quit.img \
    --non-interactive --eval '(format t "QVAR=~a~%" *im-q*)' </dev/null 2>&1)
ec=$?
check "quit_image_restores" 0 "$ec" "$out" "QVAR=7"

# --- Deferred dump at the (batch) REPL: session continues after save -----

out=$(printf '(defvar *im-r* 5)\n(ext:save-image "repl.img")\n(format t "AFTER-SAVE=~a~%%" (+ *im-r* 1))\n(quit)\n' \
    | "$TIMEOUT" 60 "$CLAMIGA" $CLI --heap 8M --batch 2>&1)
ec=$?
check "repl_save_session_continues" 0 "$ec" "$out" "Image saved" "AFTER-SAVE=6"

out=$("$TIMEOUT" 60 "$CLAMIGA" --no-userinit --image repl.img \
    --non-interactive --eval '(format t "RVAR=~a~%" *im-r*)' </dev/null 2>&1)
ec=$?
check "repl_image_restores" 0 "$ec" "$out" "RVAR=5"

# --- Corrupt image: explicit --image refuses cleanly ---------------------

# Flip a fingerprint byte (offset 12 is inside the 32-byte fingerprint).
cp session.img corrupt.img
printf '\377' | dd of=corrupt.img bs=1 seek=12 count=1 conv=notrunc 2>/dev/null
out=$("$TIMEOUT" 60 "$CLAMIGA" --no-userinit --image corrupt.img \
    --non-interactive --eval '(print 1)' </dev/null 2>&1)
ec=$?
check "corrupt_image_refused" 1 "$ec" "$out" "different build"

# Truncated file: refused, exit 1.
head -c 50000 session.img > trunc.img
out=$("$TIMEOUT" 60 "$CLAMIGA" --no-userinit --image trunc.img \
    --non-interactive --eval '(print 1)' </dev/null 2>&1)
ec=$?
check "truncated_image_refused" 1 "$ec" "$out" "corrupt"

# --- Auto-discovery of clamiga.img in the cwd ----------------------------

cp session.img clamiga.img
out=$("$TIMEOUT" 60 "$CLAMIGA" --no-userinit --non-interactive \
    --eval '(format t "DISC=~a~%" ext:*image-restored-p*)' </dev/null 2>&1)
ec=$?
check "auto_discovery_cwd" 0 "$ec" "$out" "DISC=T"

out=$("$TIMEOUT" 60 "$CLAMIGA" --no-userinit --no-image --non-interactive \
    --eval '(format t "NODISC=~a~%" ext:*image-restored-p*)' </dev/null 2>&1)
ec=$?
check "no_image_bypasses_discovery" 0 "$ec" "$out" "NODISC=NIL"

# A corrupt discovered image falls back to a normal boot (exit 0), unlike
# an explicit --image.
cp corrupt.img clamiga.img
out=$("$TIMEOUT" 60 "$CLAMIGA" --no-userinit --non-interactive \
    --eval '(format t "FALLBACK=~a~%" ext:*image-restored-p*)' </dev/null 2>&1)
ec=$?
check "corrupt_discovery_falls_back" 0 "$ec" "$out" "FALLBACK=NIL" "different build"
rm -f clamiga.img

# --- ~/.clamigarc runs after restore with *IMAGE-RESTORED-P* = T ---------

mkdir -p rc-home
cat > rc-home/.clamigarc <<'EOF'
(format t "RC-RESTORED=~a~%" ext:*image-restored-p*)
EOF
out=$(HOME="$WORK/rc-home" CLAMIGA_NO_USERINIT= "$TIMEOUT" 60 "$CLAMIGA" \
    --image session.img --non-interactive \
    --eval '(format t "RC-DONE~%")' </dev/null 2>&1)
ec=$?
check "clamigarc_sees_restored_p" 0 "$ec" "$out" "RC-RESTORED=T" "RC-DONE"

# --- Report --------------------------------------------------------------

echo "test_image: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
