#!/bin/sh
#
# clamiga-slynk.sh --- Start a SLYNK server inside clamiga for SLY/Emacs.
#
# This is the "connect to a running server" workflow: start a server here, then
# from Emacs run  M-x sly-connect RET 127.0.0.1 RET <port> RET.
# (For the auto-start workflow, just use M-x sly with a `clamiga' entry in
# `sly-lisp-implementations'; see README "Emacs (SLY) integration".)
#
# clamiga comes up through SLY's *generic* loader, exactly like every other
# implementation: load slynk-loader.lisp, slynk-loader:init, create a server.
# No clamiga-specific Lisp helper is needed — the SLY backend
# (slynk/backend/clamiga.lisp) pulls in clamiga's Gray streams via (require
# "gray-streams"), and slynk-loader's #+clamiga branch loads SLYNK from source.
#
# Why the `tail -f /dev/null': clamiga drops into a REPL that reads stdin after
# the server starts.  In a non-interactive shell stdin is empty, so the REPL
# EOFs immediately and the process exits, taking the server thread with it.
# Holding stdin open parks the REPL read and keeps the spawned server serving.
#
# Usage:
#   tools/sly/clamiga-slynk.sh                  # defaults: port 4005, heap 96M
#   CLAMIGA_PORT=4006 tools/sly/clamiga-slynk.sh
#   tools/sly/clamiga-slynk.sh --heap 192M      # extra args pass through to clamiga
#
# Configuration (override via environment):
#   CLAMIGA_BIN   clamiga binary           (default: <repo>/build/host/clamiga)
#   CLAMIGA_HEAP  heap size                (default: 96M — see note below)
#   CLAMIGA_PORT  TCP port to listen on    (default: 4005)
#   SLY_SLYNK_DIR SLY's slynk/ source dir  (default: ~/.emacs.d/plugins/sly/slynk)
#
# Heap: the 4M default thrashes GC once SLYNK + contribs load; 96M is a practical
# minimum that also carries a real app's dependency graph (e.g. sento).  Bump
# higher for larger systems.
#
set -eu

# Repo root = two levels up from this script (tools/sly/clamiga-slynk.sh).
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CLAMIGA_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

CLAMIGA_BIN="${CLAMIGA_BIN:-$CLAMIGA_ROOT/build/host/clamiga}"
CLAMIGA_HEAP="${CLAMIGA_HEAP:-96M}"
CLAMIGA_PORT="${CLAMIGA_PORT:-4005}"
SLY_SLYNK_DIR="${SLY_SLYNK_DIR:-$HOME/.emacs.d/plugins/sly/slynk}"

LOADER="$SLY_SLYNK_DIR/slynk-loader.lisp"

# --- Sanity checks -----------------------------------------------------------
[ -x "$CLAMIGA_BIN" ] || { echo "clamiga-slynk: binary not found/executable: $CLAMIGA_BIN" >&2; exit 1; }
[ -f "$LOADER" ]      || { echo "clamiga-slynk: slynk-loader.lisp not found: $LOADER
clamiga-slynk: set SLY_SLYNK_DIR to your SLY checkout's slynk/ directory." >&2; exit 1; }

echo "clamiga-slynk: starting SLYNK on port $CLAMIGA_PORT (heap $CLAMIGA_HEAP)"
echo "clamiga-slynk: connect from Emacs with  M-x sly-connect RET 127.0.0.1 RET $CLAMIGA_PORT RET"

# Run from the source root so the backend's (require "gray-streams") resolves
# lib/gray-streams.lisp (host: CWD-relative; AmigaOS: PROGDIR:lib/).
cd "$CLAMIGA_ROOT"
tail -f /dev/null | "$CLAMIGA_BIN" \
  --heap "$CLAMIGA_HEAP" \
  --eval "(load \"$LOADER\")" \
  --eval "(funcall (read-from-string \"slynk-loader:init\"))" \
  --eval "(funcall (read-from-string \"slynk:create-server\") :port $CLAMIGA_PORT :dont-close t)" \
  "$@"
