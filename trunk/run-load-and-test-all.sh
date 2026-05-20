#!/bin/sh
# run-load-and-test-all.sh — run every trunk/load-and-test-*.lisp script in
# turn, save each script's full output, and print a one-line result per script.
#
# Run from the repo ROOT (the lisp scripts use repo-root-relative paths):
#
#   trunk/run-load-and-test-all.sh            # warm FASL cache (fast re-runs)
#   trunk/run-load-and-test-all.sh --cold     # clear the FASL cache before
#                                             # each run -> true cold boot
#
# Per-script full logs:  build/load-and-test-logs/<name>.log
# Summary table:         stdout + build/load-and-test-logs/SUMMARY.txt
#
# Exit status = number of scripts that did not finish cleanly (exit 0).
#
# New trunk/load-and-test-*.lisp files are picked up automatically; only
# their heap/timeout overrides (below) need adding if the defaults don't fit.

set -u

CLAMIGA=build/host/clamiga
LOGDIR=build/load-and-test-logs
COLD=0

for arg in "$@"; do
  case "$arg" in
    --cold) COLD=1 ;;
    -h|--help)
      sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "unknown option: $arg (try --help)" >&2; exit 2 ;;
  esac
done

if [ ! -x "$CLAMIGA" ]; then
  echo "error: $CLAMIGA not found or not executable — run 'make host' first" >&2
  exit 2
fi
if ! ls trunk/load-and-test-*.lisp >/dev/null 2>&1; then
  echo "error: no trunk/load-and-test-*.lisp files — run from the repo root" >&2
  exit 2
fi

mkdir -p "$LOGDIR"
SUMMARY="$LOGDIR/SUMMARY.txt"
: > "$SUMMARY"

# Heap (MB) per script. Defaults to 96M for anything not matched, so a new
# load-and-test-*.lisp still runs without editing this file.
heap_for() {
  case "$1" in
    *-5am.lisp|*-closer-mop.lisp|*-fset.lisp) echo 24 ;;
    *-str.lisp)                               echo 64 ;;
    *-ansi.lisp|*-ansi-numbers.lisp)          echo 96 ;;
    *-sento.lisp|*-sento-system.lisp)         echo 192 ;;
    *)                                        echo 96 ;;
  esac
}

# Wall-clock timeout (seconds) per script. Cold sento compiles a large dep
# tree from scratch, so it gets the most headroom.
timeout_for() {
  case "$1" in
    *-sento.lisp|*-sento-system.lisp) echo 1800 ;;
    *-ansi.lisp|*-ansi-numbers.lisp)  echo 900 ;;
    *)                                echo 600 ;;
  esac
}

# Distil a finished log down to one human-readable result line. Each test
# family prints its tally differently, so probe for the formats we know.
summarize() {
  log="$1"
  if grep -q "FSet Results" "$log"; then
    grep "FSet Results" "$log" | tail -1 | sed 's/^[^A-Za-z]*//; s/ *=*$//'
  elif grep -qE "^passed: [0-9]" "$log"; then            # ansi rt:do-tests
    echo "passed=$(grep -E '^passed: ' "$log" | tail -1 | awk '{print $2}')" \
         "failed=$(grep -E '^failed: ' "$log" | tail -1 | awk '{print $2}')"
  elif grep -q "shim OK" "$log"; then                    # closer-mop smoke test
    echo "closer-mop shim OK"
  elif grep -qE "Pass: [0-9]+ \(" "$log"; then           # fiveam-style (5am/str/sento)
    echo "$(grep -E 'Pass: [0-9]+ \(' "$log" | tail -1 | sed 's/^ *//')" \
         "/ $(grep -E 'Fail: [0-9]+ \(' "$log" | tail -1 | sed 's/^ *//')"
  else
    echo "?? no recognized summary — check the log"
  fi
}

fail_count=0
printf '%-32s %-5s %-9s %s\n' "SCRIPT" "HEAP" "STATUS" "RESULT" | tee -a "$SUMMARY"
printf '%-32s %-5s %-9s %s\n' "------" "----" "------" "------" | tee -a "$SUMMARY"

for f in trunk/load-and-test-*.lisp; do
  name=$(basename "$f")
  heap=$(heap_for "$f")
  tmo=$(timeout_for "$f")
  log="$LOGDIR/${name%.lisp}.log"

  if [ "$COLD" -eq 1 ]; then
    rm -rf "$HOME"/.cache/common-lisp/cl-amiga-*
  fi

  echo ">>> $name  (heap ${heap}M, timeout ${tmo}s, cold=$COLD)" >&2
  # stdin from /dev/null so the post-load REPL sees EOF and exits instead
  # of blocking; everything (incl. stderr) captured to the per-script log.
  timeout "$tmo" "$CLAMIGA" --heap "${heap}M" --load "$f" </dev/null >"$log" 2>&1
  ec=$?

  result=$(summarize "$log")
  if grep -q "load-libs-ql: fetching" "$log"; then
    result="$result  [fetched from dist]"
  fi

  case "$ec" in
    0)   status="ok" ;;
    124) status="TIMEOUT"; result="timed out after ${tmo}s — $result" ;;
    *)   status="exit=$ec" ;;
  esac
  [ "$ec" -eq 0 ] || fail_count=$((fail_count + 1))

  printf '%-32s %-5s %-9s %s\n' "$name" "${heap}M" "$status" "$result" | tee -a "$SUMMARY"
done

echo "" | tee -a "$SUMMARY"
echo "full logs: $LOGDIR/<name>.log   summary: $SUMMARY" | tee -a "$SUMMARY"
exit "$fail_count"
