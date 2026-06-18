#!/bin/sh
# Regression test for the --tally-dir aggregation logic in
# trunk/run-load-and-test-all.sh.  Runs without clamiga or quicklisp.

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
RUNNER="$SCRIPT_DIR/../trunk/run-load-and-test-all.sh"

passed=0
failed=0
total=0

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
        echo "    expected: $expected"
        echo "    got:      $actual"
        failed=$((failed + 1))
    fi
}

# ---- Build fixture dirs ----

fixture_fail=$(mktemp -d)
fixture_pass=$(mktemp -d)
trap 'rm -rf "$fixture_fail" "$fixture_pass"' EXIT

# fiveam format: Pass: N (...) / Fail: N (...)
cat > "$fixture_fail/fiveam.log" << 'EOF'
 Pass: 5 (5 checks)
 Fail: 2 (2 checks)
EOF
cat > "$fixture_pass/fiveam.log" << 'EOF'
 Pass: 5 (5 checks)
 Fail: 0 (0 checks)
EOF

# rt format: passed: N / failed: N
cat > "$fixture_fail/rt.log" << 'EOF'
passed: 10
failed: 3
EOF
cat > "$fixture_pass/rt.log" << 'EOF'
passed: 10
failed: 0
EOF

# fset format: === FSet Results: N passed, N failed, ... ===
cat > "$fixture_fail/fset.log" << 'EOF'
=== FSet Results: 17 passed, 0 failed, 0 errors ===
EOF
cat > "$fixture_pass/fset.log" << 'EOF'
=== FSet Results: 17 passed, 0 failed, 0 errors ===
EOF

# closer-mop smoke test: "shim OK" — contributes 0/0 to numeric tally
cat > "$fixture_fail/closer-mop.log" << 'EOF'
shim OK
EOF
cat > "$fixture_pass/closer-mop.log" << 'EOF'
shim OK
EOF

# unrecognized format — contributes 0/0
cat > "$fixture_fail/unknown.log" << 'EOF'
some random output with no recognized summary line
EOF
cat > "$fixture_pass/unknown.log" << 'EOF'
some random output with no recognized summary line
EOF

# Expected totals:
#   fiveam: 5 pass, 2 fail  (fail fixture) / 5 pass, 0 fail (pass fixture)
#   rt:     10 pass, 3 fail / 10 pass, 0 fail
#   fset:   17 pass, 0 fail / 17 pass, 0 fail
#   closer: 0/0             / 0/0
#   unknown: 0/0            / 0/0
#   fail total: 32 passed, 5 failed
#   pass total: 32 passed, 0 failed

# ---- Tests against failure fixture ----

output=$(sh "$RUNNER" --tally-dir "$fixture_fail" 2>&1)

grand_tests=$(echo "$output" | grep "^tests:")
check "grand_total_pass_count_with_failures" "tests:   32 passed, 5 failed" "$grand_tests"

grand_scripts=$(echo "$output" | grep "^scripts:")
check "grand_total_scripts_line_present" "scripts: 5/5 ok" "$grand_scripts"

sh "$RUNNER" --tally-dir "$fixture_fail" >/dev/null 2>&1
ec=$?
check "exit_nonzero_when_tests_fail" "1" "$([ "$ec" -ne 0 ] && echo 1 || echo 0)"

# ---- Tests against all-passing fixture ----

output=$(sh "$RUNNER" --tally-dir "$fixture_pass" 2>&1)

grand_tests=$(echo "$output" | grep "^tests:")
check "grand_total_pass_count_all_passing" "tests:   32 passed, 0 failed" "$grand_tests"

sh "$RUNNER" --tally-dir "$fixture_pass" >/dev/null 2>&1
ec=$?
check "exit_zero_when_all_pass" "0" "$([ "$ec" -eq 0 ] && echo 0 || echo 1)"

# ---- Grand total block present in output ----

check "grand_total_header_present" "=== GRAND TOTAL ===" \
  "$(echo "$output" | grep "^=== GRAND TOTAL ===$")"

# ---- Stale-FASL-cache guard (binary fingerprint) ----
# The guard lives in the runner's normal path, so drive the runner with a fake
# clamiga and throwaway cache/log dirs (env overrides) and assert it wipes the
# ASDF FASL cache exactly when the clamiga binary's contents change — and leaves
# a warm cache untouched when the binary is unchanged.

REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
guard_cache=$(mktemp -d)
guard_log=$(mktemp -d)
guard_bindir=$(mktemp -d)
guard_bin="$guard_bindir/clamiga"
trap 'rm -rf "$fixture_fail" "$fixture_pass" "$guard_cache" "$guard_log" "$guard_bindir"' EXIT

printf '#!/bin/sh\nexit 0\n' > "$guard_bin"
chmod +x "$guard_bin"

run_guard() {
  ( cd "$REPO_ROOT" && \
    CLAMIGA="$guard_bin" \
    LOGDIR="$guard_log" \
    CLAMIGA_FASL_CACHE_PARENT="$guard_cache" \
    sh "$RUNNER" >/dev/null 2>&1 )
}

# The runner keeps its per-script caches under <cache_parent>/clamiga-test-extra
# and records the binary fingerprint there.
guard_perscript="$guard_cache/clamiga-test-extra"

seed_marker() {
  mkdir -p "$guard_perscript/somescript"
  : > "$guard_perscript/somescript/stale.fasl"
}

marker_state() {
  [ -e "$guard_perscript/somescript/stale.fasl" ] && echo present || echo gone
}

# First run: a populated cache exists but no binid is recorded yet -> wipe it
# (the binary that produced those FASLs is unknown / presumed different).
seed_marker
run_guard
check "guard_wipes_cache_when_binid_unknown" "gone" "$(marker_state)"
check "guard_records_binid" "yes" \
  "$([ -f "$guard_perscript/.clamiga-binid" ] && echo yes || echo no)"

# Second run, same binary, cache repopulated: fingerprint matches -> keep warm.
seed_marker
run_guard
check "guard_keeps_cache_when_binary_unchanged" "present" "$(marker_state)"

# Third run, binary contents changed: fingerprint differs -> wipe.
printf '#!/bin/sh\necho changed\nexit 0\n' > "$guard_bin"
chmod +x "$guard_bin"
seed_marker
run_guard
check "guard_wipes_cache_when_binary_changes" "gone" "$(marker_state)"

# ---- Summary ----

echo ""
echo "$passed passed, $failed failed, $total total"
if [ "$failed" -gt 0 ]; then
    echo "FAIL"
    exit 1
else
    echo "PASS"
    exit 0
fi
