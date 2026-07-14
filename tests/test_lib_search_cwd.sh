#!/bin/sh
# The runtime library (lib/) must be found when clamiga starts from a
# directory other than the source root:
#   - executable-relative fallback: build/host/clamiga resolves ../../lib/
#     (also through a $PATH symlink) with no environment setup
#   - $CLAMIGA_HOME fallback: a bare copied binary finds lib/ via the env var
#   - when nothing is found: boot fails with a diagnostic naming
#     CLAMIGA_HOME instead of a generic REQUIRE error later, and REQUIRE
#     errors name the module they could not find
# Run: sh tests/test_lib_search_cwd.sh [path-to-clamiga]

CLAMIGA="${1:-build/host/clamiga}"
case "$CLAMIGA" in
    /*) ABS_CLAMIGA="$CLAMIGA" ;;
    *)  ABS_CLAMIGA="$(pwd)/$CLAMIGA" ;;
esac
ROOT="$(pwd)"

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

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
            echo "    got: $(echo "$haystack" | head -5)"
            failed=$((failed + 1)) ;;
    esac
}

check_not_contains() {
    desc="$1"
    needle="$2"
    haystack="$3"
    total=$((total + 1))
    case "$haystack" in
        *"$needle"*)
            echo "  FAIL  $desc"
            echo "    expected NOT to contain: $needle"
            failed=$((failed + 1)) ;;
        *)
            echo "  ok  $desc"
            passed=$((passed + 1)) ;;
    esac
}

EVAL_REQUIRE='(progn (require "gray-streams") (format t "GS-OK R=~A~%" (+ 1 2)))'

# --- Executable-relative fallback: repo binary run from elsewhere ---

result=$(cd "$WORKDIR" && env -u CLAMIGA_HOME "$ABS_CLAMIGA" --no-userinit \
    --non-interactive --eval "$EVAL_REQUIRE" </dev/null 2>&1)
check_contains     "exedir_boot_and_require"  "GS-OK R=3" "$result"
check_not_contains "exedir_no_boot_error"     "cannot locate its runtime library" "$result"

# --- Executable-relative fallback through a $PATH-style symlink ---

ln -s "$ABS_CLAMIGA" "$WORKDIR/clamiga-link"
result=$(cd "$WORKDIR" && env -u CLAMIGA_HOME ./clamiga-link --no-userinit \
    --non-interactive --eval "$EVAL_REQUIRE" </dev/null 2>&1)
check_contains "symlink_boot_and_require" "GS-OK R=3" "$result"

# --- $CLAMIGA_HOME fallback: bare copied binary, lib/ nowhere nearby ---

cp "$ABS_CLAMIGA" "$WORKDIR/clamiga-copy"
result=$(cd "$WORKDIR" && CLAMIGA_HOME="$ROOT" ./clamiga-copy --no-userinit \
    --non-interactive --eval "$EVAL_REQUIRE" </dev/null 2>&1)
check_contains "clamiga_home_boot_and_require" "GS-OK R=3" "$result"

# --- Nothing findable: clear diagnostics instead of a generic error ---

result=$(cd "$WORKDIR" && env -u CLAMIGA_HOME ./clamiga-copy --no-userinit \
    --non-interactive --eval '(require "gray-streams")' </dev/null 2>&1)
check_contains "lost_boot_names_problem"   "cannot locate its runtime library" "$result"
check_contains "lost_boot_names_fix"       "CLAMIGA_HOME" "$result"
check_contains "lost_require_names_module" 'cannot find module "gray-streams"' "$result"

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
