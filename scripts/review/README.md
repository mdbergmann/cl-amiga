# Auto-review gate

A `pre-commit` hook that reviews your **staged** changes with a headless
`claude`, and — when it finds problems — auto-fixes them, re-stages the fixes,
and lets the commit proceed **with the fixes included**. Problematic code never
enters history.

## Flow

```
git commit
   └─ .git/hooks/pre-commit  →  scripts/review/pre-commit.sh
        1. review staged diff (read-only claude)  →  .reviews/log.md
        2. STATUS: CLEAN   →  commit proceeds untouched
           STATUS: ISSUES  →  fix agent edits the staged files,
                              re-stages them, commit proceeds with fixes
```

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

- **Fail-open on tooling errors** — if `claude` is missing, errors, or times
  out, the commit is *allowed*. A broken reviewer never blocks your work.
- **Partial-staging guard** — if a staged file also has *unstaged* edits,
  auto-restaging would sweep those in, so the hook leaves the fixes unstaged and
  aborts instead; you re-stage deliberately.
- **Fix agent only edits files in the commit** and records a `RESOLVED:` /
  `DISMISSED:` note per finding in `.reviews/log.md`.
- Fixes land in the commit you just made — run `git show HEAD` to see them.

## Escape hatches

| Want | Do |
|------|----|
| Skip one commit | `git commit --no-verify` |
| Disable entirely | `export CLAUDE_AUTO_REVIEW=0` |
| Review + block, but don't auto-fix | `export CLAUDE_AUTO_FIX=0` |

## Tuning (env vars)

| Var | Default | Meaning |
|-----|---------|---------|
| `CLAUDE_REVIEW_MODEL` | `sonnet` | model for the review pass |
| `CLAUDE_FIX_MODEL`    | `sonnet` | model for the fix pass (try `opus` for harder fixes) |
| `CLAUDE_REVIEW_BUDGET`| `0.50`   | USD cap (`--max-budget-usd`) for review |
| `CLAUDE_FIX_BUDGET`   | `1.00`   | USD cap for the fix pass |
| `CLAUDE_REVIEW_TIMEOUT`| `180`   | seconds per claude call (needs `timeout`/`gtimeout`) |

## The log

`.reviews/log.md` accumulates one section per commit (timestamp + parent SHA,
the files, the findings, and resolution notes). It's a local artifact and is
git-ignored via `.git/info/exclude`. Track it in the repo instead if you want a
shared review history.
