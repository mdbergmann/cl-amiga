#!/bin/sh
# Makefile.cross WIDE=1 knob: flag/object-dir wiring, checked via dry-run.
# Run: sh tests/test_cross_wide_knob.sh   (no m68k toolchain needed — make -n
# only prints recipes, so check-toolchain never executes)
#
# The knob contract (Makefile.cross):
#   default      -> build/cross,          no -DCL_WIDE_STRINGS  (narrow 8-bit)
#   WIDE=1       -> build/cross-wide,     -DCL_WIDE_STRINGS
#   FPU=1        -> build/cross-fpu,      no -DCL_WIDE_STRINGS
#   WIDE=1 FPU=1 -> build/cross-fpu-wide, -DCL_WIDE_STRINGS -m68881
# Every combination has its own object dir so variants never mix.

passed=0
failed=0

check() {
    desc="$1"
    ok="$2"
    if [ "$ok" = "yes" ]; then
        echo "  ok  $desc"
        passed=$((passed + 1))
    else
        echo "  FAIL  $desc"
        failed=$((failed + 1))
    fi
}

dryrun() {
    # -n: print recipes without running them (works without the m68k
    # toolchain — check-toolchain never executes).  -B: treat everything as
    # out of date, so the full command list prints even when the variant's
    # object dir exists and is current.
    # shellcheck disable=SC2086
    make -f Makefile.cross -nB amiga $1 2>/dev/null
}

out=$(dryrun "")
check "default: objects under build/cross/" \
    "$(echo "$out" | grep -q 'build/cross/' && echo yes)"
check "default: no -DCL_WIDE_STRINGS" \
    "$(echo "$out" | grep -q 'DCL_WIDE_STRINGS' || echo yes)"
check "default: no -wide object dir" \
    "$(echo "$out" | grep -q 'cross-wide\|cross-fpu-wide' || echo yes)"

out=$(dryrun "WIDE=1")
check "WIDE=1: -DCL_WIDE_STRINGS present" \
    "$(echo "$out" | grep -q 'DCL_WIDE_STRINGS' && echo yes)"
check "WIDE=1: objects under build/cross-wide/" \
    "$(echo "$out" | grep -q 'build/cross-wide/' && echo yes)"

out=$(dryrun "FPU=1")
check "FPU=1 alone: no -DCL_WIDE_STRINGS" \
    "$(echo "$out" | grep -q 'DCL_WIDE_STRINGS' || echo yes)"
check "FPU=1 alone: objects under build/cross-fpu/ (not -wide)" \
    "$(echo "$out" | grep -q 'build/cross-fpu/' && echo yes)"

out=$(dryrun "WIDE=1 FPU=1")
check "WIDE=1 FPU=1: -DCL_WIDE_STRINGS present" \
    "$(echo "$out" | grep -q 'DCL_WIDE_STRINGS' && echo yes)"
check "WIDE=1 FPU=1: -m68881 present" \
    "$(echo "$out" | grep -q 'm68881' && echo yes)"
check "WIDE=1 FPU=1: objects under build/cross-fpu-wide/" \
    "$(echo "$out" | grep -q 'build/cross-fpu-wide/' && echo yes)"

echo "test_cross_wide_knob: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
