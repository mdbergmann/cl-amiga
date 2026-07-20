#!/bin/sh
# run-amiga.sh — play Closure on the emulated Amiga with one command.
#
# Cross-compiles clamiga if needed (incremental, a few seconds when
# up to date), drops a one-shot boot-override script for the FS-UAE
# boot sequence (see verify/realamiga/call-on-ustartup) and launches
# FS-UAE in the foreground.  The Amiga boots straight into the game
# on its own custom screen; quitting the game ('q', or the menu's
# Quit) exits clamiga, which quits FS-UAE.
#
#   ./run-amiga.sh                      play (lores custom screen)
#   HEAP=16M STACK=200000 ./run-amiga.sh   override clamiga heap/stack
#   CONFIG=lowend ./run-amiga.sh        the 68020 baseline machine
#   ENTRY=worlds/closure/gfx/run.lisp ./run-amiga.sh   any entry script
#   DEBUG=1 ./run-amiga.sh              engine debug log -> tale-debug.log
#                                       in this directory (tail it from
#                                       another shell); any other value is
#                                       an Amiga-side path (DEBUG=ram:t.log)
#
# The override file is consumed by the boot script (and cleaned up
# here on exit), so a later `make -f Makefile.cross test-amiga` is
# never affected.
set -eu

HEAP="${HEAP:-8M}"
STACK="${STACK:-128000}"
CONFIG="${CONFIG:-default}"     # default (A4000/68040 JIT) | lowend (68020)
ENTRY="${ENTRY:-src/main-amiga.lisp}"
DEBUG="${DEBUG:-}"              # engine debug log (TALE_DEBUG_LOG)
case "$DEBUG" in
	0|no|off) DEBUG="" ;;   # explicit off reads as off, not as a path
esac

# Repo root: this script lives in examples/games/closure/.
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT"

case "$CONFIG" in
	lowend) FSCONFIG=verify/realamiga/verify-lowend.fs-uae ;;
	*)      FSCONFIG=verify/realamiga/verify.fs-uae ;;
esac
FSUAE="verify/realamiga/FS-UAE.app/Contents/MacOS/fs-uae"
OVERRIDE="build/amiga/boot-override"

if [ ! -x "$FSUAE" ]; then
	echo "FS-UAE not found at $FSUAE" >&2
	exit 1
fi

echo "=== Cross-compiling clamiga (incremental) ==="
make -f Makefile.cross amiga

mkdir -p build/amiga
{
	echo "cd CLAmiga:examples/games/closure"
	echo "stack $STACK"
	# The engine enables its debug log when TALE_DEBUG_LOG is set
	# (src/debug-log.lisp); SetEnv only touches ENV: (RAM), so the
	# flag lives for this boot only.
	if [ -n "$DEBUG" ]; then
		echo "SetEnv TALE_DEBUG_LOG $DEBUG"
	fi
	echo "CLAmiga:build/amiga/clamiga --heap $HEAP --load $ENTRY"
	echo "C:UAEquit"
} > "$OVERRIDE"
# The boot script deletes the override once consumed; this covers an
# abort before the emulated Amiga gets that far.
trap 'rm -f "$OVERRIDE"' EXIT INT TERM

echo "=== Launching FS-UAE — the game boots on its own screen ==="
echo "    (quit the game with 'q' or the Quit menu to close FS-UAE)"
"$FSUAE" "$FSCONFIG"
