#!/bin/sh
# md2guide.sh — build the AmigaGuide documentation from the Markdown sources.
#
# Usage:
#   md2guide.sh <clamiga-binary> <output-dir>
#
# Converts the documentation the binary release ships (README.md and the
# package reference under docs/) into one .guide per .md in <output-dir>,
# with tools/docs/md2guide.lisp run by the given clamiga.  When the clamacs/
# submodule is checked out, its README becomes clamacs.guide as well.  The
# version and date stamped into every @$VER: line come from src/core/types.h,
# the single source of truth for the version.
#
# Called by `make guide` (into build/guide/), by scripts/make-binary-release.sh
# (into the staged docs/), and by tests/test_md2guide.sh.  Exit status 1 with a
# `file:line: message` diagnostic when a source uses Markdown the converter
# does not handle, or contains a dangling link -- see specs/amigaguide-docs.md.

set -e

CLAMIGA_BIN="$1"
OUT="$2"
ROOT=$(CDPATH= cd "$(dirname "$0")/../.." && pwd)

if [ -z "$CLAMIGA_BIN" ] || [ -z "$OUT" ]; then
    echo "usage: $0 <clamiga-binary> <output-dir>" >&2
    exit 2
fi
if [ ! -x "$CLAMIGA_BIN" ]; then
    echo "md2guide.sh: clamiga binary not found: $CLAMIGA_BIN" >&2
    exit 2
fi

ver_field() { sed -n "s/^#define CL_VERSION_$1 \([0-9][0-9]*\)$/\1/p" "$ROOT/src/core/types.h"; }
VERSION="$(ver_field MAJOR).$(ver_field MINOR).$(ver_field PATCH)"
DATE=$(sed -n 's/^#define CL_VERSION_DATE "\([^"]*\)"$/\1/p' "$ROOT/src/core/types.h")
case "$VERSION" in
    *..*|.*|*.) echo "md2guide.sh: could not parse the version from src/core/types.h" >&2; exit 1 ;;
esac
[ -n "$DATE" ] || { echo "md2guide.sh: could not parse CL_VERSION_DATE from src/core/types.h" >&2; exit 1; }

mkdir -p "$OUT"
# Absolute, in the spelling clamiga uses (tests/shpath.sh: on an MSYS2 host
# the shell's path is not the native binary's path).
. "$ROOT/tests/shpath.sh"
OUT_NATIVE=$(native_path "$(CDPATH= cd "$OUT" && pwd)")

INPUTS='("README.md" . "cl-amiga.guide")
        ("docs/README.md" . "README.guide")
        ("docs/ext.md" . "ext.guide")
        ("docs/mp.md" . "mp.guide")
        ("docs/ffi.md" . "ffi.guide")
        ("docs/gray.md" . "gray.guide")
        ("docs/mop.md" . "mop.guide")
        ("docs/clamiga.md" . "clamiga.guide")
        ("docs/amiga.md" . "amiga.guide")'
if [ -f "$ROOT/clamacs/README.md" ]; then
    INPUTS="$INPUTS
        (\"clamacs/README.md\" . \"clamacs.guide\")"
else
    echo "md2guide.sh: note: clamacs/ submodule not checked out, no clamacs.guide" >&2
fi

cd "$ROOT"
CLAMIGA_NO_USERINIT=1 "$CLAMIGA_BIN" --no-userinit --non-interactive \
    --load tools/docs/md2guide.lisp \
    --eval "(md2guide:cli '($INPUTS) \"$OUT_NATIVE/\" \"$VERSION\" \"$DATE\")" </dev/null
