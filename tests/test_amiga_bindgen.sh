#!/bin/sh
# Executable specification of scripts/gen-amiga-bindings.lisp — the
# generator behind lib/amiga/raw/ (the raw AmigaOS/MorphOS API bindings).
#
# Part 1 runs the generator on the small SDK fixture under
# tests/fixtures/bindgen/ (an NDK-style SFD, a MorphOS-style SFD with
# private slots, a moved function, a sysv entry and a MorphOS-only library,
# plus two assembler includes) and checks the output with
# tests/test_amiga_bindgen.lisp: LVO assignment (==bias/==reserve/varargs/
# alias), register and result-kind encoding, the >7-register plist path,
# skips (DOUBLE, A5, register pairs, sysv), version and platform guards,
# CL-name shadowing, constant evaluation (expressions, BITDEF, ENUM,
# DEVCMD, LIBDEF, forward and cross-file references) and struct layouts.
#
# Part 2 loads every COMMITTED module in lib/amiga/raw/ on the host and
# checks well-known OS values, so a stale or hand-edited generated file
# fails the build.
#
# Both parts run twice: plain, and with :MORPHOS pushed on *FEATURES*, so
# both sides of every platform conditional execute.
#
# Run: sh tests/test_amiga_bindgen.sh build/host/clamiga

CLAMIGA="${1:-build/host/clamiga}"
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FIX="$ROOT/tests/fixtures/bindgen"
CHECK="$ROOT/tests/test_amiga_bindgen.lisp"
TMPD=$(mktemp -d "${TMPDIR:-/tmp}/test_amiga_bindgen_XXXXXX") || exit 1
trap 'rm -rf "$TMPD"' EXIT INT TERM

fail=0

run_checks() {
    # $1 = mode, $2 = RAWDIR, $3 = "morphos" to push :MORPHOS first (or empty)
    if [ "$3" = morphos ]; then
        out=$(BINDGEN_CHECK="$1" RAWDIR="$2" "$CLAMIGA" --no-userinit --non-interactive \
              --heap 256M --eval "(push :morphos *features*)" --load "$CHECK" \
              </dev/null 2>&1 | grep -v '^; Loading')
    else
        out=$(BINDGEN_CHECK="$1" RAWDIR="$2" "$CLAMIGA" --no-userinit --non-interactive \
              --heap 256M --load "$CHECK" </dev/null 2>&1 | grep -v '^; Loading')
    fi
    echo "$out" | grep -E '^(FAIL|ok  )' | sed 's/^/  /'
    summary=$(echo "$out" | grep '^BINDGEN-RESULT')
    if [ -z "$summary" ]; then
        echo "  FAIL: check script did not finish ($1, $3)"
        echo "$out" | tail -15 | sed 's/^/    /'
        fail=1
        return
    fi
    echo "  $summary"
    case "$summary" in
        *"fail=0"*) ;;
        *) fail=1 ;;
    esac
}

echo "=== test_amiga_bindgen: generator on fixtures ==="
GEN_OUT="$TMPD/out"
gen_log=$(BINDGEN_NDK_SFD="$FIX/sfd" BINDGEN_NDK_INCLUDE="$FIX/include" \
          BINDGEN_MOS_SFD="$FIX/mos-sfd" BINDGEN_MOS_ONLY=mosonly \
          BINDGEN_OUT="$GEN_OUT" \
          "$CLAMIGA" --no-userinit --non-interactive --heap 64M \
          --load "$ROOT/scripts/gen-amiga-bindings.lisp" </dev/null 2>&1 | grep -v '^; Loading')
echo "$gen_log" | sed 's/^/  /'
if ! echo "$gen_log" | grep -q '^3 modules:'; then
    echo "  FAIL: expected 3 modules (example, mosonly, exec/exbase)"
    fail=1
fi
if echo "$gen_log" | grep -q 'warnings:'; then
    echo "  FAIL: generator reported warnings on the fixture (must be clean)"
    fail=1
fi
for f in example.lisp mosonly.lisp exec/exbase.lisp; do
    [ -f "$GEN_OUT/$f" ] || { echo "  FAIL: $f not generated"; fail=1; }
done
run_checks fixture "$GEN_OUT" ""
run_checks fixture "$GEN_OUT" morphos

echo "=== test_amiga_bindgen: committed lib/amiga/raw ==="
run_checks committed "$ROOT/lib/amiga/raw" ""
run_checks committed "$ROOT/lib/amiga/raw" morphos

if [ $fail -eq 0 ]; then
    echo "test_amiga_bindgen: PASS"
    exit 0
else
    echo "test_amiga_bindgen: FAIL"
    exit 1
fi
