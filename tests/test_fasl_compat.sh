#!/bin/sh
# Cross-session FASL compatibility tests
# Verifies that FASL files created in one session work correctly in another.
# Run: make test (or sh tests/test_fasl_compat.sh build/host/clamiga)

CLAMIGA="${1:-build/host/clamiga}"
passed=0
failed=0
total=0
TMPDIR="${TMPDIR:-/tmp}"
# Cache path includes CL_FASL_VERSION ("-faslN" suffix) — wildcard so the
# test cleans every version's cache, not just the current one.
CACHE_GLOB="$HOME/.cache/common-lisp/cl-amiga-0.1*"

check() {
    desc="$1"
    expected="$2"
    actual="$3"
    total=$((total + 1))
    if [ "$actual" = "$expected" ]; then
        echo "  ok  $desc"
        passed=$((passed + 1))
    else
        echo "  FAIL  $desc"
        echo "    expected: $(echo "$expected" | head -5)"
        echo "    got:      $(echo "$actual" | head -5)"
        failed=$((failed + 1))
    fi
}

check_contains() {
    desc="$1"
    pattern="$2"
    actual="$3"
    total=$((total + 1))
    if echo "$actual" | grep -q "$pattern"; then
        echo "  ok  $desc"
        passed=$((passed + 1))
    else
        echo "  FAIL  $desc"
        echo "    expected to contain: $pattern"
        echo "    got: $(echo "$actual" | head -5)"
        failed=$((failed + 1))
    fi
}

# Helper: run clamiga in batch mode with *load-verbose* suppressed
# Usage: run_quiet '(lisp expression)'
run_quiet() {
    echo "(setf *load-verbose* nil) $1" | "$CLAMIGA" --batch 2>&1
}

# Clean FASL cache to start fresh
rm -rf $CACHE_GLOB

# --- Test 1: Basic defun survives FASL round-trip ---

cat > "$TMPDIR/fasl_test_defun.lisp" << 'LISP'
(defun fasl-test-add (a b) (+ a b))
LISP

# Session 1: load source (creates FASL cache)
run_quiet '(load "'"$TMPDIR"'/fasl_test_defun.lisp")' > /dev/null

# Session 2: load again (should use FASL cache)
result=$(run_quiet '(load "'"$TMPDIR"'/fasl_test_defun.lisp") (fasl-test-add 10 20)')
check "fasl_defun_cross_session" "NIL
T
30" "$result"

# --- Test 2: defpackage + in-package survives FASL round-trip ---

cat > "$TMPDIR/fasl_test_pkg.lisp" << 'LISP'
(defpackage :fasl-test-pkg
  (:use :cl)
  (:export :greet))

(in-package :fasl-test-pkg)

(defun greet () "hello-from-pkg")

(in-package :cl-user)
LISP

# Session 1: load source
run_quiet '(load "'"$TMPDIR"'/fasl_test_pkg.lisp")' > /dev/null

# Session 2: load from FASL, verify package and function exist
result=$(run_quiet '(load "'"$TMPDIR"'/fasl_test_pkg.lisp") (not (null (find-package :fasl-test-pkg))) (fasl-test-pkg:greet)')
check "fasl_defpackage_cross_session" "NIL
T
T
\"hello-from-pkg\"" "$result"

# --- Test 3: defpackage with nicknames ---

cat > "$TMPDIR/fasl_test_nick.lisp" << 'LISP'
(defpackage :fasl-test-long-name
  (:use :cl)
  (:nicknames :ftn)
  (:export :val))

(in-package :fasl-test-long-name)
(defun val () 42)
(in-package :cl-user)
LISP

# Session 1
run_quiet '(load "'"$TMPDIR"'/fasl_test_nick.lisp")' > /dev/null

# Session 2: verify nickname works
result=$(run_quiet '(load "'"$TMPDIR"'/fasl_test_nick.lisp") (ftn:val)')
check "fasl_nickname_cross_session" "NIL
T
42" "$result"

# --- Test 4: defvar / defparameter survive FASL ---

cat > "$TMPDIR/fasl_test_vars.lisp" << 'LISP'
(defvar *fasl-test-var* 99)
(defparameter *fasl-test-param* "hello")
LISP

# Session 1
run_quiet '(load "'"$TMPDIR"'/fasl_test_vars.lisp")' > /dev/null

# Session 2
result=$(run_quiet '(load "'"$TMPDIR"'/fasl_test_vars.lisp") (list *fasl-test-var* *fasl-test-param*)')
check "fasl_defvar_cross_session" "NIL
T
(99 \"hello\")" "$result"

# --- Test 5: Multiple dependent files (load order) ---

cat > "$TMPDIR/fasl_test_base.lisp" << 'LISP'
(defpackage :fasl-dep-base (:use :cl) (:export :base-val))
(in-package :fasl-dep-base)
(defun base-val () 100)
(in-package :cl-user)
LISP

cat > "$TMPDIR/fasl_test_dep.lisp" << 'LISP'
(defpackage :fasl-dep-child (:use :cl :fasl-dep-base) (:export :child-val))
(in-package :fasl-dep-child)
(defun child-val () (+ (base-val) 50))
(in-package :cl-user)
LISP

# Session 1
run_quiet '(load "'"$TMPDIR"'/fasl_test_base.lisp") (load "'"$TMPDIR"'/fasl_test_dep.lisp")' > /dev/null

# Session 2: both files loaded from FASL
result=$(run_quiet '(load "'"$TMPDIR"'/fasl_test_base.lisp") (load "'"$TMPDIR"'/fasl_test_dep.lisp") (fasl-dep-child:child-val)')
check "fasl_dependent_files_cross_session" "NIL
T
T
150" "$result"

# --- Test 6: compile-file produces loadable FASL ---

cat > "$TMPDIR/fasl_test_cf.lisp" << 'LISP'
(defun cf-test-fn () :compiled-ok)
LISP

# Session 1: compile-file explicitly
run_quiet '(compile-file "'"$TMPDIR"'/fasl_test_cf.lisp")' > /dev/null

# Session 2: load should find the FASL cache
result=$(run_quiet '(load "'"$TMPDIR"'/fasl_test_cf.lisp") (cf-test-fn)')
check "fasl_compile_file_cross_session" "NIL
T
:COMPILED-OK" "$result"

# --- Test 7: ASDF compile-file integration ---
# Verify that ASDF-compiled FASLs are real binaries (not dummy markers)

cat > "$TMPDIR/fasl_test_asdf.lisp" << 'LISP'
(defpackage :fasl-asdf-test (:use :cl) (:export :asdf-val))
(in-package :fasl-asdf-test)
(defun asdf-val () :asdf-ok)
(in-package :cl-user)
LISP

# Session 1: compile with ASDF loaded
run_quiet '(require "asdf") (compile-file "'"$TMPDIR"'/fasl_test_asdf.lisp")' > /dev/null

# Session 2: load from FASL
result=$(run_quiet '(load "'"$TMPDIR"'/fasl_test_asdf.lisp") (fasl-asdf-test:asdf-val)')
check "fasl_asdf_compile_cross_session" "NIL
T
:ASDF-OK" "$result"

# --- Test 8: FASL loads from cache (verify cache is used) ---

cat > "$TMPDIR/fasl_test_cache.lisp" << 'LISP'
(defun cache-test-fn () :cached)
LISP

# Session 1: load to create cache
run_quiet '(load "'"$TMPDIR"'/fasl_test_cache.lisp")' > /dev/null

# Session 2: load with *load-verbose* to verify FASL path is used
result=$(echo '(setf *load-verbose* t) (load "'"$TMPDIR"'/fasl_test_cache.lisp") (cache-test-fn)' \
    | "$CLAMIGA" --batch 2>&1)
check_contains "fasl_loaded_from_cache" ".fasl" "$result"
check_contains "fasl_cached_result_correct" "CACHED" "$result"

# --- Test 9: Stale FASL (wrong version) is replaced by source recompile ---
# Regression: bumping CL_FASL_VERSION used to leave stale FASLs in the cache
# directory. The loader now (a) keys the cache directory on the FASL version
# AND (b) pre-validates the version field, so a stale FASL transparently
# falls back to recompiling from source.

cat > "$TMPDIR/fasl_test_stale.lisp" << 'LISP'
(defun stale-test-fn () :recompiled-ok)
LISP

# Session 1: load to create a fresh FASL cache.
run_quiet '(load "'"$TMPDIR"'/fasl_test_stale.lisp")' > /dev/null

# Locate the cached FASL and corrupt its version bytes (offset 4-5, big-endian
# u16). Setting them to 0xFFFF guarantees mismatch with any real version.
stale_fasl=$(find $CACHE_GLOB -name 'fasl_test_stale.fasl' 2>/dev/null | head -1)
if [ -z "$stale_fasl" ] || [ ! -f "$stale_fasl" ]; then
    echo "  FAIL  fasl_stale_version_recovery (could not find cached FASL)"
    failed=$((failed + 1))
    total=$((total + 1))
else
    printf '\xff\xff' | dd of="$stale_fasl" bs=1 seek=4 count=2 conv=notrunc 2>/dev/null

    # Make the FASL newer than the source so the loader prefers it. Without
    # the fix, this would error out with "FASL: unsupported version".
    touch "$stale_fasl"

    # Session 2: should silently recompile and return the right value.
    result=$(run_quiet '(load "'"$TMPDIR"'/fasl_test_stale.lisp") (stale-test-fn)')
    check "fasl_stale_version_recovery" "NIL
T
:RECOMPILED-OK" "$result"
fi

# --- Cleanup ---

rm -f "$TMPDIR"/fasl_test_*.lisp

# --- Summary ---

echo ""
echo "$passed passed, $failed failed, $total total"
if [ "$failed" -gt 0 ]; then
    echo "FAIL"
    exit 1
else
    echo "PASS"
    exit 0
fi
