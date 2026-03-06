#!/bin/sh
# Integration tests for --batch mode piping
# Run: make test-batch (or sh tests/test_batch.sh)

CLAMIGA="${1:-build/host/clamiga}"
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
        echo "    expected: $(echo "$expected" | head -5)"
        echo "    got:      $(echo "$actual" | head -5)"
        failed=$((failed + 1))
    fi
}

# --- Basic output ---

result=$(echo '(+ 1 2)' | "$CLAMIGA" --batch 2>&1)
check "batch_single_expr_prints_result" "3" "$result"

result=$(printf '(+ 1 2)\n(* 3 4)\n' | "$CLAMIGA" --batch 2>&1)
check "batch_two_exprs_separate_lines" "3
12" "$result"

# --- Multiple expressions on one line ---

result=$(echo '(+ 1 2) (* 3 4)' | "$CLAMIGA" --batch 2>&1)
check "batch_two_exprs_same_line" "3
12" "$result"

result=$(echo '(+ 1 2) (* 3 4) (- 10 3)' | "$CLAMIGA" --batch 2>&1)
check "batch_three_exprs_same_line" "3
12
7" "$result"

# --- Multi-line expression ---

result=$(printf '(+ 1\n   2)\n' | "$CLAMIGA" --batch 2>&1)
check "batch_multiline_expr" "3" "$result"

result=$(printf '(defun foo (x)\n  (* x x))\n(foo 7)\n' | "$CLAMIGA" --batch 2>&1)
check "batch_defun_and_call" "FOO
49" "$result"

# --- String results ---

result=$(echo '(string-upcase "hello")' | "$CLAMIGA" --batch 2>&1)
check "batch_string_result" '"HELLO"' "$result"

# --- NIL and T ---

result=$(echo '(null nil)' | "$CLAMIGA" --batch 2>&1)
check "batch_nil_t" "T" "$result"

# --- Comments and blank lines skipped ---

result=$(printf '; comment\n(+ 1 2)\n' | "$CLAMIGA" --batch 2>&1)
check "batch_skip_comment_line" "3" "$result"

result=$(printf '\n\n(+ 1 2)\n' | "$CLAMIGA" --batch 2>&1)
check "batch_skip_blank_lines" "3" "$result"

# --- List and nested results ---

result=$(echo '(list 1 2 3)' | "$CLAMIGA" --batch 2>&1)
check "batch_list_result" "(1 2 3)" "$result"

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
