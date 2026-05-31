#!/bin/sh
# run-load-and-test-all.sh — run every trunk/load-and-test-*.lisp script in
# turn, save each script's full output, and print a one-line result per script.
#
# Run from the repo ROOT (the lisp scripts use repo-root-relative paths):
#
#   trunk/run-load-and-test-all.sh            # warm FASL cache (fast re-runs)
#   trunk/run-load-and-test-all.sh --cold     # clear the FASL cache before
#                                             # each run -> true cold boot
#   trunk/run-load-and-test-all.sh --tally-dir <DIR>
#                                             # skip running; tally .log files
#                                             # already in <DIR> and print the
#                                             # grand total (for unit-testing)
#
# Per-script full logs:  build/load-and-test-logs/<name>.log
# Summary table:         stdout + build/load-and-test-logs/SUMMARY.txt
#
# Exit status = non-zero if any script failed/timed-out OR any test failed.
#
# New trunk/load-and-test-*.lisp files are picked up automatically; only
# their heap/timeout overrides (below) need adding if the defaults don't fit.

set -u

CLAMIGA=build/host/clamiga
LOGDIR=build/load-and-test-logs
COLD=0
TALLY_DIR=""

while [ $# -gt 0 ]; do
  case "$1" in
    --cold) COLD=1; shift ;;
    --tally-dir)
      if [ $# -lt 2 ]; then
        echo "error: --tally-dir requires a directory argument" >&2; exit 2
      fi
      TALLY_DIR="$2"; shift 2 ;;
    -h|--help)
      sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "unknown option: $1 (try --help)" >&2; exit 2 ;;
  esac
done

# Heap (MB) per script. Defaults to 96M for anything not matched, so a new
# load-and-test-*.lisp still runs without editing this file.
heap_for() {
  case "$1" in
      *-5am.lisp|*-closer-mop.lisp)           echo 24 ;;
      *-str.lisp|*-fset.lisp)                 echo 64 ;;
      *-ansi.lisp)                            echo 96 ;;
      *-sento.lisp|*-sento-system.lisp)       echo 192 ;;
    *)                                        echo 96 ;;
  esac
}

# Wall-clock timeout (seconds) per script. Cold sento compiles a large dep
# tree from scratch, so it gets the most headroom.
timeout_for() {
  case "$1" in
    *-sento.lisp|*-sento-system.lisp) echo 1800 ;;
    *-ansi.lisp)                      echo 900 ;;
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

# Extract numeric pass/fail from a log file. Outputs: "<pass> <fail>"
# Matches the same format priority as summarize(). Unrecognized logs and
# closer-mop (no per-test numbers) both yield "0 0".
counts_for() {
  log="$1"
  if grep -q "FSet Results" "$log"; then
    pass=$(grep "FSet Results" "$log" | tail -1 | \
           awk '{for(i=1;i<=NF;i++) if($i~/^passed/) {print $(i-1); exit}}')
    fail=$(grep "FSet Results" "$log" | tail -1 | \
           awk '{for(i=1;i<=NF;i++) if($i~/^failed/) {print $(i-1); exit}}')
    echo "${pass:-0} ${fail:-0}"
  elif grep -qE "^passed: [0-9]" "$log"; then
    pass=$(grep -E '^passed: ' "$log" | tail -1 | awk '{print $2}')
    fail=$(grep -E '^failed: ' "$log" | tail -1 | awk '{print $2}')
    echo "${pass:-0} ${fail:-0}"
  elif grep -q "shim OK" "$log"; then
    echo "0 0"
  elif grep -qE "Pass: [0-9]+ \(" "$log"; then
    pass=$(grep -E 'Pass: [0-9]+ \(' "$log" | tail -1 | awk '{print $2}')
    fail=$(grep -E 'Fail: [0-9]+ \(' "$log" | tail -1 | awk '{print $2}')
    echo "${pass:-0} ${fail:-0}"
  else
    echo "0 0"
  fi
}

# --- Tally-only path: aggregate pre-existing logs without running clamiga ---
if [ -n "$TALLY_DIR" ]; then
  if [ ! -d "$TALLY_DIR" ]; then
    echo "error: --tally-dir '$TALLY_DIR' is not a directory" >&2
    exit 2
  fi
  total_pass=0
  total_fail=0
  total_scripts=0
  printf '%-32s %s\n' "LOG" "COUNTS"
  printf '%-32s %s\n' "---" "------"
  for log in "$TALLY_DIR"/*.log; do
    [ -e "$log" ] || continue
    total_scripts=$((total_scripts + 1))
    counts=$(counts_for "$log")
    c_pass=$(echo "$counts" | awk '{print $1}')
    c_fail=$(echo "$counts" | awk '{print $2}')
    total_pass=$((total_pass + c_pass))
    total_fail=$((total_fail + c_fail))
    printf '%-32s pass=%-5d fail=%d\n' "$(basename "$log")" "$c_pass" "$c_fail"
  done
  echo ""
  echo "=== GRAND TOTAL ==="
  echo "scripts: $total_scripts/$total_scripts ok"
  echo "tests:   $total_pass passed, $total_fail failed"
  [ "$total_fail" -gt 0 ] && exit 1
  exit 0
fi

# --- Normal path: run scripts, then print grand total ---

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

fail_count=0
total_scripts=0
total_pass=0
total_fail=0
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

  total_scripts=$((total_scripts + 1))
  echo ">>> $name  (heap ${heap}M, timeout ${tmo}s, cold=$COLD)" >&2
  # stdin from /dev/null so the post-load REPL sees EOF and exits instead
  # of blocking; everything (incl. stderr) captured to the per-script log.
  timeout "$tmo" "$CLAMIGA" --no-userinit --heap "${heap}M" --load "$f" </dev/null >"$log" 2>&1
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

  counts=$(counts_for "$log")
  c_pass=$(echo "$counts" | awk '{print $1}')
  c_fail=$(echo "$counts" | awk '{print $2}')
  total_pass=$((total_pass + c_pass))
  total_fail=$((total_fail + c_fail))

  printf '%-32s %-5s %-9s %s\n' "$name" "${heap}M" "$status" "$result" | tee -a "$SUMMARY"
done

echo "" | tee -a "$SUMMARY"
echo "full logs: $LOGDIR/<name>.log   summary: $SUMMARY" | tee -a "$SUMMARY"
echo "" | tee -a "$SUMMARY"
ok_count=$((total_scripts - fail_count))
{
  echo "=== GRAND TOTAL ==="
  echo "scripts: $ok_count/$total_scripts ok"
  echo "tests:   $total_pass passed, $total_fail failed"
} | tee -a "$SUMMARY"

if [ "$fail_count" -gt 0 ] || [ "$total_fail" -gt 0 ]; then
  exit 1
fi
exit 0
