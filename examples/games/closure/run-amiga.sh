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
#
# The override file is consumed by the boot script (and cleaned up
# here on exit), so a later `make -f Makefile.cross test-amiga` is
# never affected.
set -eu

HEAP="${HEAP:-8M}"
STACK="${STACK:-128000}"
CONFIG="${CONFIG:-default}"     # default (A4000/68040 JIT) | lowend (68020)
ENTRY="${ENTRY:-src/main-amiga.lisp}"

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
cat > "$OVERRIDE" <<EOF
cd CLAmiga:examples/games/closure
stack $STACK
CLAmiga:build/amiga/clamiga --heap $HEAP --load $ENTRY
C:UAEquit
EOF
# The boot script deletes the override once consumed; this covers an
# abort before the emulated Amiga gets that far.
trap 'rm -f "$OVERRIDE"' EXIT INT TERM

echo "=== Launching FS-UAE — the game boots on its own screen ==="
echo "    (quit the game with 'q' or the Quit menu to close FS-UAE)"
"$FSUAE" "$FSCONFIG"
