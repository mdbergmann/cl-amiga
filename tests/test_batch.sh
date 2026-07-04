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

result=$(echo '(+ 1 2)' | "$CLAMIGA" --no-userinit --batch 2>&1)
check "batch_single_expr_prints_result" "3" "$result"

result=$(printf '(+ 1 2)\n(* 3 4)\n' | "$CLAMIGA" --no-userinit --batch 2>&1)
check "batch_two_exprs_separate_lines" "3
12" "$result"

# --- Multiple expressions on one line ---

result=$(echo '(+ 1 2) (* 3 4)' | "$CLAMIGA" --no-userinit --batch 2>&1)
check "batch_two_exprs_same_line" "3
12" "$result"

result=$(echo '(+ 1 2) (* 3 4) (- 10 3)' | "$CLAMIGA" --no-userinit --batch 2>&1)
check "batch_three_exprs_same_line" "3
12
7" "$result"

# --- Multi-line expression ---

result=$(printf '(+ 1\n   2)\n' | "$CLAMIGA" --no-userinit --batch 2>&1)
check "batch_multiline_expr" "3" "$result"

result=$(printf '(defun foo (x)\n  (* x x))\n(foo 7)\n' | "$CLAMIGA" --no-userinit --batch 2>&1)
check "batch_defun_and_call" "FOO
49" "$result"

# --- String results ---

result=$(echo '(string-upcase "hello")' | "$CLAMIGA" --no-userinit --batch 2>&1)
check "batch_string_result" '"HELLO"' "$result"

# --- NIL and T ---

result=$(echo '(null nil)' | "$CLAMIGA" --no-userinit --batch 2>&1)
check "batch_nil_t" "T" "$result"

# --- Comments and blank lines skipped ---

result=$(printf '; comment\n(+ 1 2)\n' | "$CLAMIGA" --no-userinit --batch 2>&1)
check "batch_skip_comment_line" "3" "$result"

result=$(printf '\n\n(+ 1 2)\n' | "$CLAMIGA" --no-userinit --batch 2>&1)
check "batch_skip_blank_lines" "3" "$result"

# --- List and nested results ---

result=$(echo '(list 1 2 3)' | "$CLAMIGA" --no-userinit --batch 2>&1)
check "batch_list_result" "(1 2 3)" "$result"

# --- REPL buffer overflow: loud discard, never a silently-truncated eval ---
# A multi-line form larger than the 4096-char accumulator used to have its
# overflowing lines silently dropped, then the mangled prefix was evaluated.
# It must now print a diagnostic, discard the form, and keep the session
# usable (the trailing (+ 40 2) still evaluates).

bigform=$(awk 'BEGIN{print "(list";for(i=0;i<60;i++){s="";for(j=0;j<40;j++)s=s" 1";print s};print ")"}')
result=$(printf '%s
(+ 40 2)
' "$bigform" | "$CLAMIGA" --no-userinit --batch 2>&1)
total=$((total + 1))
case "$result" in
  *"exceeds the REPL buffer"*)
    case "$result" in
      *42*) echo "  ok  repl_overflow_discards_loudly"; passed=$((passed + 1)) ;;
      *) echo "  FAIL  repl_overflow_discards_loudly (session dead after discard)"
         failed=$((failed + 1)) ;;
    esac ;;
  *)
    echo "  FAIL  repl_overflow_discards_loudly (no diagnostic printed)"
    failed=$((failed + 1)) ;;
esac

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
