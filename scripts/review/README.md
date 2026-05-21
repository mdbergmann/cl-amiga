# Auto-review gate

A `pre-commit` hook that (1) reviews your **staged** changes with a headless
`claude` and auto-fixes any problems it finds, then (2) runs the fast test tier.
Problematic code never enters history.

## Flow

```
git commit
   └─ githooks/pre-commit  →  scripts/review/pre-commit.sh
        1. staged diff written to .reviews/staged.diff; claude READS it
           (Read+Bash, read-only)              →  .reviews/log-<timestamp>.md
             STATUS: CLEAN   →  (go to step 2)
             STATUS: ISSUES  →  fix agent edits the staged files,
                                re-stages them  →  (go to step 2)
        2. make test-fast  (C unit tests + shell tests; NO sento/host-cold-test)
             pass  →  commit proceeds
             fail  →  commit aborted; output saved to .reviews/last-test.log
```

`make test-fast` is the fast tier carved out of `make test`; the slow
`host-cold-test` (sento's quicklisp suite) is intentionally excluded so commits
aren't delayed by minutes. Run the full `make test` (incl. sento) yourself, in
CI, or set `CLAUDE_TEST_TARGET=test`.

## Install (per clone)

Git never auto-runs repo-supplied hooks (cloning would otherwise execute
arbitrary code), so each clone activates it once:

```sh
make install-hooks
```

This sets a **relative** `core.hooksPath=githooks` (in `.git/config`, which is
local to the clone). The relative path resolves to `<repo-root>/githooks` in
every clone and survives the repo being moved — unlike an absolute path, which
breaks on a move and can't be shared. The tracked `githooks/pre-commit` launcher
then delegates to `scripts/review/pre-commit.sh`.

## Safety

- **Review + tests are mandatory (fail-closed)** — if the review or tests don't
  *complete* (missing `claude`/`make`, an error, or a timeout), the commit is
  *blocked*, never silently allowed. The only bypass is `git commit --no-verify`.
- **Fail-closed on real failures** — a genuine review verdict (with auto-fix
  off) or an actual test/compile failure blocks the commit. That's the point.
- **Diff delivered as a file, not piped** — the staged diff is written to
  `.reviews/staged.diff` and the reviewer is told to read it. Headless
  `claude -p` stalls when a large diff is piped on stdin and it must answer in
  one shot; reading it as a file (with Read+Bash) lets the agent work reliably,
  at the cost of an agentic review that can run for minutes (hence the 600s
  default timeout).
- **Partial-staging guard** — if a staged file also has *unstaged* edits,
  auto-restaging would sweep those in, so the hook leaves the fixes unstaged and
  aborts instead; you re-stage deliberately.
- **Fix agent only edits files in the commit** and records a `RESOLVED:` /
  `DISMISSED:` note per finding in that review's log file.
- Tests run on the working tree as-is (after any fixes), validating the final
  state. Fixes land in the commit you just made — run `git show HEAD` to see them.

## Escape hatches

| Want | Do |
|------|----|
| Skip one commit (review + tests) | `git commit --no-verify` |
| Disable entirely | `export CLAUDE_AUTO_REVIEW=0` |
| Review + block, but don't auto-fix | `export CLAUDE_AUTO_FIX=0` |
| Skip the test stage only | `export CLAUDE_RUN_TESTS=0` |

## Tuning (env vars)

| Var | Default | Meaning |
|-----|---------|---------|
| `CLAUDE_REVIEW_MODEL` | `sonnet` | model for the review pass |
| `CLAUDE_FIX_MODEL`    | `sonnet` | model for the fix pass (try `opus` for harder fixes) |
| `CLAUDE_REVIEW_TIMEOUT`| `600`   | seconds per claude call (needs `timeout`/`gtimeout`); the agentic Read+grep review can take several minutes |
| `CLAUDE_RUN_TESTS`    | `1`      | run the test stage (`0` to skip) |
| `CLAUDE_TEST_TARGET`  | `test-fast` | make target for the test stage (`test` = incl. sento) |
| `CLAUDE_TEST_TIMEOUT` | `600`    | seconds for the test stage |

## The log

Each review writes its own `.reviews/log-<YYYYMMDD-HHMMSS>.md` (timestamp +
parent SHA, the files, the findings, and resolution notes) — no shared
append-only file. Logs older than **30 days** are pruned automatically at the
start of each run. `.reviews/last-test.log` holds the most recent test-stage
output. All are git-ignored via `.gitignore` (`.reviews/`). Track them in the
repo instead if you want a shared history.
