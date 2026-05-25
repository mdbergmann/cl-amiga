#!/bin/sh
# Regression test: the test harness must never read a developer's personal
# ~/.clamigarc user-init file.
#
# Bug: every C unit test calls cl_repl_init(), which loads ~/.clamigarc at
# startup.  When a developer's ~/.clamigarc pulled in quicklisp
#   (require :asdf) (load "~/quicklisp/setup.lisp") ...
# `make test-fast` ground to a crawl and HUNG in test_fasl (the userinit ran
# inside the test process; a relative (load ...) that failed from a temp CWD
# dropped into code that blocked).  C tests can't pass the --no-userinit CLI
# flag, so load_user_init() now honours the CLAMIGA_NO_USERINIT env var as an
# escape hatch, and the Makefile test targets export it.
#
# This test pins both opt-out paths (env var + CLI flag) and confirms the
# baseline (no opt-out) really does load the file — so the guard is meaningful.
#
# Run: sh tests/test_userinit.sh build/host/clamiga

CLAMIGA="${1:-build/host/clamiga}"
case "$CLAMIGA" in /*) ;; *) CLAMIGA="$PWD/$CLAMIGA";; esac

TIMEOUT=$(command -v timeout 2>/dev/null || command -v gtimeout 2>/dev/null || true)
if [ -z "$TIMEOUT" ]; then
    echo "SKIP test_userinit: neither timeout nor gtimeout on PATH"
    exit 0
fi

passed=0
failed=0
total=0

# Fake HOME holding a ~/.clamigarc that prints an unmistakable marker.
FAKE_HOME=$(mktemp -d "${TMPDIR:-/tmp}/clamiga_userinit_XXXXXX") || exit 1
cleanup() { rm -rf "$FAKE_HOME"; }
trap cleanup EXIT INT TERM
printf '(format t "~&USERINIT-WAS-LOADED~%%")\n' > "$FAKE_HOME/.clamigarc"

MARKER="USERINIT-WAS-LOADED"

# desc, expect (yes|no), captured-output -> assert marker presence/absence,
# and never a hang (124).
check() {
    desc="$1"; expect="$2"; ec="$3"; out="$4"
    total=$((total + 1))
    if [ "$ec" -eq 124 ]; then
        echo "  FAIL  $desc (timed out, ec=124 — clamiga hung)"
        failed=$((failed + 1)); return
    fi
    if echo "$out" | grep -q "$MARKER"; then seen=yes; else seen=no; fi
    if [ "$seen" = "$expect" ]; then
        echo "  ok  $desc"
        passed=$((passed + 1))
    else
        echo "  FAIL  $desc (expected userinit-loaded=$expect, got $seen)"
        echo "    output: $(echo "$out" | head -5)"
        failed=$((failed + 1))
    fi
}

# --- Baseline: with no opt-out, ~/.clamigarc IS loaded (fixture sanity) ---
out=$(echo '(quit)' | HOME="$FAKE_HOME" CLAMIGA_NO_USERINIT= "$TIMEOUT" 10 "$CLAMIGA" --batch 2>&1)
check "baseline_loads_userinit" yes "$?" "$out"

# --- CLAMIGA_NO_USERINIT=1 env var suppresses it (the C fix) ---
out=$(echo '(quit)' | HOME="$FAKE_HOME" CLAMIGA_NO_USERINIT=1 "$TIMEOUT" 10 "$CLAMIGA" --batch 2>&1)
check "env_var_skips_userinit" no "$?" "$out"

# --- --no-userinit CLI flag suppresses it ---
out=$(echo '(quit)' | HOME="$FAKE_HOME" CLAMIGA_NO_USERINIT= "$TIMEOUT" 10 "$CLAMIGA" --no-userinit --batch 2>&1)
check "flag_skips_userinit" no "$?" "$out"

echo ""
echo "$passed passed, $failed failed, $total total"
[ "$failed" -eq 0 ]
