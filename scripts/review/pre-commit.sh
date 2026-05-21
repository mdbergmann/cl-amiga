#!/usr/bin/env bash
#
# Auto-review gate (pre-commit).
#
# Reviews the STAGED diff with a headless `claude`. If the review is clean the
# commit proceeds untouched. If it finds problems and AUTO_FIX is on, a fix
# agent edits the affected files, the fixes are re-staged, and the commit
# proceeds WITH the fixes included.
#
# Design principles:
#   * Fail-OPEN on tooling errors (missing/erroring/timed-out claude) so a broken
#     reviewer never blocks commits. Fail-CLOSED only on a real review verdict
#     when auto-fix is disabled.
#   * Never sweep in changes the user didn't stage (partial-staging guard).
#
# Override behaviour via env vars (see scripts/review/README.md):
#   CLAUDE_AUTO_REVIEW=0   disable entirely (or use `git commit --no-verify`)
#   CLAUDE_AUTO_FIX=0      review + block on issues, but don't auto-fix
#   CLAUDE_REVIEW_MODEL    default: sonnet
#   CLAUDE_FIX_MODEL       default: sonnet
#   CLAUDE_REVIEW_BUDGET   default: 0.50  (USD, --max-budget-usd)
#   CLAUDE_FIX_BUDGET      default: 1.00
#   CLAUDE_REVIEW_TIMEOUT  default: 180   (seconds, if `timeout`/`gtimeout` exists)

ENABLED="${CLAUDE_AUTO_REVIEW:-1}"
[ "$ENABLED" = "0" ] && exit 0

REVIEW_MODEL="${CLAUDE_REVIEW_MODEL:-sonnet}"
FIX_MODEL="${CLAUDE_FIX_MODEL:-sonnet}"
REVIEW_BUDGET="${CLAUDE_REVIEW_BUDGET:-0.50}"
FIX_BUDGET="${CLAUDE_FIX_BUDGET:-1.00}"
AUTO_FIX="${CLAUDE_AUTO_FIX:-1}"
REVIEW_TIMEOUT="${CLAUDE_REVIEW_TIMEOUT:-180}"

ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" || exit 0
cd "$ROOT" || exit 0

# Defensive: skip while a merge/rebase/cherry-pick is in progress.
GITDIR="$(git rev-parse --git-dir)"
if [ -d "$GITDIR/rebase-merge" ] || [ -d "$GITDIR/rebase-apply" ] || \
   [ -f "$GITDIR/MERGE_HEAD" ] || [ -f "$GITDIR/CHERRY_PICK_HEAD" ]; then
  exit 0
fi

CLAUDE_BIN="$(command -v claude || true)"
if [ -z "$CLAUDE_BIN" ]; then
  echo "[auto-review] 'claude' not on PATH — skipping review (commit allowed)." >&2
  exit 0
fi

TIMEOUT_BIN="$(command -v timeout || command -v gtimeout || true)"
maybe_timeout() { # maybe_timeout <secs> <cmd...>
  if [ -n "$TIMEOUT_BIN" ]; then "$TIMEOUT_BIN" "$@"; else shift; "$@"; fi
}

# Files staged for this commit (added/copied/modified/renamed).
STAGED_FILES="$(git diff --cached --name-only --diff-filter=ACMR)"
[ -z "$STAGED_FILES" ] && exit 0

DIFF="$(git diff --cached --no-color)"
[ -z "$DIFF" ] && exit 0

LOG=".reviews/log.md"
mkdir -p .reviews
[ -f "$LOG" ] || printf '# Auto-review log\n\n' > "$LOG"

TS="$(date '+%Y-%m-%d %H:%M:%S')"
PARENT="$(git rev-parse --short HEAD 2>/dev/null || echo root)"

REVIEW_PROMPT='You are reviewing the STAGED git diff (provided on stdin) for a commit about to be made to CL-Amiga, a Common Lisp environment for AmigaOS written in C89/C99.

Enforce these project rules (from CLAUDE.md):
- GC safety: any CL_Obj held across an allocating call (cl_alloc/cl_cons/cl_make_*/cl_vm_apply etc.) must be CL_GC_PROTECT-ed; watch for list-building loops.
- 32-bit-clean heap structs: no size_t or pointer-sized fields; use uint32_t/int32_t explicitly.
- C89/C99 only: no C11+ features.
- Common Lisp behaviour must conform to the HyperSpec.
- Every feature/bugfix needs tests; every bug fix needs a regression test.

Output format, STRICTLY:
- The FIRST line must be exactly "STATUS: CLEAN" or "STATUS: ISSUES".
- If ISSUES, follow with a markdown list, one item per finding:
  "- [HIGH|MED|LOW] path:line - problem - suggested fix"
- Report ONLY substantive problems (bugs, GC-safety, memory/32-bit, C89/C99, HyperSpec deviations, missing/incorrect tests, security). No style nits, no speculation.
- You may Read surrounding files for context. Do NOT edit anything.'

REVIEW_OUT="$(printf '%s' "$DIFF" | maybe_timeout "$REVIEW_TIMEOUT" "$CLAUDE_BIN" -p "$REVIEW_PROMPT" \
  --model "$REVIEW_MODEL" \
  --max-budget-usd "$REVIEW_BUDGET" \
  --allowedTools "Read,Grep,Glob,Bash(git show:*),Bash(git log:*),Bash(git diff:*)" \
  --output-format text 2>/dev/null)"
RC=$?

if [ $RC -ne 0 ] || [ -z "$REVIEW_OUT" ]; then
  echo "[auto-review] reviewer error/timeout (rc=$RC) — commit allowed (fail-open)." >&2
  {
    printf '## %s — parent %s (staged)\n\n' "$TS" "$PARENT"
    printf '_Reviewer error (rc=%s) — commit allowed without review._\n\n' "$RC"
  } >> "$LOG"
  exit 0
fi

STATUS_LINE="$(printf '%s\n' "$REVIEW_OUT" | head -n1)"

{
  printf '## %s — parent %s (staged)\n\n' "$TS" "$PARENT"
  printf 'Files: %s\n\n' "$(echo "$STAGED_FILES" | tr '\n' ' ')"
  printf '%s\n\n' "$REVIEW_OUT"
} >> "$LOG"

case "$STATUS_LINE" in
  *CLEAN*)
    echo "[auto-review] clean — commit proceeding." >&2
    exit 0
    ;;
esac

echo "[auto-review] issues found (logged to $LOG)." >&2

if [ "$AUTO_FIX" != "1" ]; then
  echo "[auto-review] AUTO_FIX disabled — aborting commit. Fix the findings, or 'git commit --no-verify' to bypass." >&2
  exit 1
fi

# Partial-staging guard: a file that is BOTH staged and has unstaged edits cannot
# be auto-restaged without sweeping in the unstaged hunks. Detect and bail safely.
UNSTAGED_TRACKED="$(git diff --name-only)"
PARTIAL=""
while IFS= read -r f; do
  [ -z "$f" ] && continue
  if printf '%s\n' "$UNSTAGED_TRACKED" | grep -Fxq -- "$f"; then
    PARTIAL="$PARTIAL $f"
  fi
done <<EOF
$STAGED_FILES
EOF

echo "[auto-review] applying automatic fixes..." >&2

FIX_PROMPT='Read .reviews/log.md and address the findings in the LAST "## ... (staged)" entry in the file (the most recent section). For each finding not already marked RESOLVED or DISMISSED:
- Edit the code to fix it. ONLY modify files listed in that entry'\''s "Files:" line — do not touch any other file.
- Make minimal, correct fixes. Follow CLAUDE.md: GC_PROTECT CL_Obj across allocations, keep heap structs 32-bit-clean, C89/C99 only, conform to the HyperSpec, and add/adjust tests when the finding calls for it (only within the listed files).
- Then append under that finding in the log: "  - RESOLVED: <what you changed>". If a finding is a false positive, append "  - DISMISSED: <why>" and change no code for it.
- Do NOT run git and do NOT commit. Only edit files and update the log.'

maybe_timeout "$REVIEW_TIMEOUT" "$CLAUDE_BIN" -p "$FIX_PROMPT" \
  --model "$FIX_MODEL" \
  --max-budget-usd "$FIX_BUDGET" \
  --permission-mode acceptEdits \
  --allowedTools "Read,Grep,Glob,Edit,Write" \
  --output-format text >/dev/null 2>&1
FRC=$?

if [ $FRC -ne 0 ]; then
  echo "[auto-review] fix agent error/timeout (rc=$FRC) — aborting commit. Any fixes are in your working tree; re-stage and commit, or use --no-verify." >&2
  exit 1
fi

if [ -n "$PARTIAL" ]; then
  echo "[auto-review] partially-staged files (staged + unstaged edits):$PARTIAL" >&2
  echo "[auto-review] NOT auto-staging — would sweep in unstaged changes. Fixes are in your working tree." >&2
  echo "[auto-review] review, 'git add' what you want, and commit again (or --no-verify)." >&2
  exit 1
fi

# Re-stage exactly the originally-staged files (now carrying the fixes).
echo "$STAGED_FILES" | while IFS= read -r f; do
  [ -n "$f" ] && git add -- "$f"
done

echo "[auto-review] fixes applied and re-staged — commit proceeding WITH fixes (see $LOG; 'git show HEAD' after)." >&2
exit 0
