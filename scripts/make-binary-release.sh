#!/bin/bash
# make-binary-release.sh — assemble the AmigaOS/MorphOS binary release.
#
# Produces build/release/clamiga-<version>/ and .zip/.lha archives:
#
#   clamiga-<version>/
#     bin/aos3/clamiga      AmigaOS 3+ (68020+) soft-float binary, runs on
#                           any CPU — cross-compiled here
#     bin/aos3-fpu/clamiga  AmigaOS 3+ hard-float binary (Makefile.cross
#                           FPU=1, -m68881) — REQUIRES an FPU (68881/68882,
#                           68040/68060, Vampire/PiStorm)
#     bin/mos/clamiga       MorphOS (PPC) binary, built natively on MorphOS
#     bin/*/clamiga.img     bare-boot heap image (boot + CLOS) beside EACH
#                           binary: startup restores it in one read instead
#                           of loading lib/boot.fasl + clos.fasl (see below)
#     lib/                  runtime library — FASLs where portable, sources
#                           where compilation must happen on the target
#     docs/                 package API reference (signatures + descriptions)
#     examples/             example programs, as Lisp source
#     README.md LICENSE README-BINARY.txt
#
# lib/ packaging policy (correctness, not preference):
#   FASL   boot clos ffi gray-streams
#          — self-contained, no reader conditionals / compile-time feature
#          detection, so a host-compiled FASL is portable (FASLs are
#          arch/endian-neutral; boot.fasl + clos.fasl have shipped this way
#          all along).
#   FASL + SOURCE
#          amiga/**       — the curated modules, AMIGA.REACTION and the
#                           generated raw OS bindings (lib/amiga/raw/**): no
#                           reader conditionals, all platform variance is
#                           load-time ((member :morphos *features*),
#                           (%version>= n)) and every struct layout is an
#                           explicit 32-bit offset, so ONE host-compiled FASL
#                           serves aos3, aos3-fpu and MorphOS.  Precompiled
#                           because the alternative is compiling ~1 MB of
#                           generated source on a 68020 at first REQUIRE
#                           (minutes, plus compile heap).  The sources ship
#                           too, as the readable API reference and as the
#                           fallback REQUIRE takes if a FASL is ever rejected;
#                           the FASLs are written AFTER the sources are
#                           copied, so REQUIRE's "FASL at least as new as the
#                           source" rule picks the FASL.
#   SOURCE asdf.lisp      — uiop's (detect-os) runs at compile time and bakes
#                           :os-unix branches of the *compiling* host in.
#          quicklisp.lisp — contains #+amigaos reader conditionals.
#          quicklisp-compat.lisp, quicklisp-install.lisp — reference
#                           quicklisp packages that don't exist at host
#                           compile time (macros would compile wrong).
#          shims/         — the cl+ssl facade and swank stub ASDF systems.
#                           lib/asdf.lisp auto-registers them on
#                           ASDF:*CENTRAL-REGISTRY* (searched before the
#                           Quicklisp/ocicl searchers), so they shadow any
#                           package-manager copy with no installation step.
#   Source-shipped files compile on the target on first (require ...) and are
#   cached under S:cl-amiga/faslcache/, so the cost is paid once.
#
#   Every FASL is compiled with CLAMIGA_FASL_PORTABLE=1: the m68k AmigaOS
#   binaries are byte-string builds with no decoder for FASL_TAG_WIDE_STRING
#   (MorphOS is a wide build but loads the same FASLs), so a
#   non-ASCII string literal in a shipped module fails the build HERE with
#   the source line, not on the Amiga with BAD_TAG at REQUIRE time
#   (tests/test_lib_fasl_portable.sh runs the same compile in `make test`).
#
# Heap images (specs/image-save-load.md): each binary starts from the
# clamiga.img beside it — a snapshot of the booted runtime (boot + CLOS),
# restored in one read instead of loading the two FASLs form by form.
#   - One image PER BINARY, next to it.  Images are per-build (a fingerprint
#     ties them to version/format/FPU/platform) and PROGDIR: is the first
#     executable-relative place startup looks.  A single image at the
#     release root would be found via the cwd by all three binaries and
#     refused by two of them on every start.
#   - The host cannot write them the way it writes FASLs.  The aos3 pair is
#     saved by the staged m68k binaries themselves, unattended in FS-UAE
#     (verify/realamiga/make-image.sh against the staged layout, so each
#     image is dumped from the very FASLs that ship, then restarted from the
#     release root to verify).  The MorphOS one is saved natively
#     (`make -f Makefile.mos image`) and passed in as MOS_IMG.
#   - boot.fasl + clos.fasl still ship: they are what --no-image boots from
#     and the fallback when an image is refused.
#
# The binaries sit two directory levels below the release root on purpose:
# both the boot search (repl.c) and REQUIRE resolve lib/ via the
# executable-ancestor fallback (PROGDIR: two levels up), so the release runs
# from any current directory without assigns or environment variables.
#
# Usage:
#   scripts/make-binary-release.sh [--no-smoke]
#
#   MOS_BIN=path   MorphOS binary to package (default: ./clamiga-mos).
#                  There is no MorphOS cross toolchain here — build it
#                  natively with Makefile.mos and copy it over.
#   MOS_IMG=path   Its bare-boot heap image (default: ./clamiga-mos.img),
#                  saved on MorphOS by THAT binary from the same source
#                  tree: `make -f Makefile.mos image` writes
#                  build/morphos/clamiga.img (and verifies it).
#
# The aos3 images need the FS-UAE setup of `make -f Makefile.cross
# test-amiga` (pkill fs-uae first if an emulator is lingering).

set -euo pipefail

cd "$(dirname "$0")/.."
ROOT=$(pwd)

SMOKE=1
[ "${1:-}" = "--no-smoke" ] && SMOKE=0

# macOS ships no `timeout`; prefer coreutils' if present, else run unguarded.
if command -v timeout > /dev/null 2>&1; then TIMEOUT="timeout 300"
elif command -v gtimeout > /dev/null 2>&1; then TIMEOUT="gtimeout 300"
else TIMEOUT=""; fi

MOS_BIN=${MOS_BIN:-$ROOT/clamiga-mos}
MOS_IMG=${MOS_IMG:-$ROOT/clamiga-mos.img}
FSUAE_BIN=verify/realamiga/FS-UAE.app/Contents/MacOS/fs-uae

# --- version from the single source of truth ------------------------------
ver_field() { sed -n "s/^#define CL_VERSION_$1 \([0-9][0-9]*\)$/\1/p" src/core/types.h; }
VMAJOR=$(ver_field MAJOR); VMINOR=$(ver_field MINOR); VPATCH=$(ver_field PATCH)
VERSION="$VMAJOR.$VMINOR.$VPATCH"
[ -n "$VMAJOR" ] && [ -n "$VMINOR" ] && [ -n "$VPATCH" ] || {
    echo "ERROR: could not parse version from src/core/types.h" >&2; exit 1; }

REL="clamiga-$VERSION"
OUT="$ROOT/build/release"
STAGE="$OUT/$REL"

echo "=== CL-Amiga binary release $VERSION ==="

# --- inputs ---------------------------------------------------------------
if [ ! -f "$MOS_BIN" ]; then
    echo "ERROR: MorphOS binary not found: $MOS_BIN" >&2
    echo "       Build it natively on MorphOS (make -f Makefile.mos) and copy it" >&2
    echo "       here, or point MOS_BIN=... at it." >&2
    exit 1
fi
if [ ! -f "$MOS_IMG" ]; then
    echo "ERROR: MorphOS heap image not found: $MOS_IMG" >&2
    echo "       Save it on MorphOS with the binary in MOS_BIN (make -f Makefile.mos image" >&2
    echo "       writes build/morphos/clamiga.img) and copy it here, or point MOS_IMG=... at it." >&2
    exit 1
fi
if [ ! -x "$FSUAE_BIN" ]; then
    echo "ERROR: FS-UAE not found at $FSUAE_BIN — the aos3 heap images are saved" >&2
    echo "       in the emulator (same setup as make -f Makefile.cross test-amiga)." >&2
    exit 1
fi

# --- build ----------------------------------------------------------------
echo "--- Building host binary (FASL compiler) ---"
make host

echo "--- Cross-compiling AmigaOS 3 binary (soft-float) ---"
make -f Makefile.cross amiga

echo "--- Cross-compiling AmigaOS 3 binary (hard-float, FPU=1) ---"
make -f Makefile.cross amiga FPU=1

HOST_BIN="$ROOT/build/host/clamiga"
AOS3_BIN="$ROOT/build/cross/clamiga"
AOS3FPU_BIN="$ROOT/build/cross-fpu/clamiga"
[ -x "$HOST_BIN" ] || { echo "ERROR: $HOST_BIN missing" >&2; exit 1; }
[ -f "$AOS3_BIN" ] || { echo "ERROR: $AOS3_BIN missing" >&2; exit 1; }
[ -f "$AOS3FPU_BIN" ] || { echo "ERROR: $AOS3FPU_BIN missing" >&2; exit 1; }

# --- stage ----------------------------------------------------------------
echo "--- Staging $STAGE ---"
rm -rf "$STAGE"
mkdir -p "$STAGE/bin/aos3" "$STAGE/bin/aos3-fpu" "$STAGE/bin/mos" \
         "$STAGE/lib/amiga" "$STAGE/docs"

cp "$AOS3_BIN"    "$STAGE/bin/aos3/clamiga"
cp "$AOS3FPU_BIN" "$STAGE/bin/aos3-fpu/clamiga"
cp "$MOS_BIN"     "$STAGE/bin/mos/clamiga"
chmod +x "$STAGE/bin/aos3/clamiga" "$STAGE/bin/aos3-fpu/clamiga" \
         "$STAGE/bin/mos/clamiga"

# lib: FASL-portable modules, compiled by the just-built host binary so
# CL_FASL_VERSION matches the packaged binaries exactly.  The script compiles
# with CLAMIGA_FASL_PORTABLE=1 and refuses a module whose compile printed an
# error or produced no FASL (see its header).
FASL_LIBS="boot clos ffi gray-streams"
echo "--- compile-file $FASL_LIBS -> $REL/lib/*.fasl ---"
sh scripts/compile-lib-fasls.sh -o "$STAGE" -b "$HOST_BIN" \
    $(for m in $FASL_LIBS; do printf 'lib/%s.lisp ' "$m"; done) \
    || { echo "ERROR: lib FASLs not produced" >&2; exit 1; }

# lib: source-shipped modules (see policy above)
cp lib/asdf.lisp lib/quicklisp.lisp lib/quicklisp-compat.lisp \
   lib/quicklisp-install.lisp "$STAGE/lib/"
cp -R lib/shims "$STAGE/lib/shims"

# lib/amiga/**: sources first (only .lisp -- an in-repo `make fasl-amiga`
# output must not leak in), then the FASLs compiled on top of them so they
# are the newer of the pair and REQUIRE picks them (see policy above).
find lib/amiga -name '*.lisp' | while read -r f; do
    mkdir -p "$STAGE/$(dirname "$f")"
    cp "$f" "$STAGE/$f"
done
echo "--- compile-file lib/amiga/** -> $REL/lib/amiga/**/*.fasl ---"
# --no-docstrings: the DEFCFUN bindings ship without their C-prototype
# docstrings (~16 KB of heap per raw OS module on the target; the .lisp
# sources next to them keep the prototypes).
sh scripts/compile-lib-fasls.sh -o "$STAGE" -b "$HOST_BIN" --no-docstrings \
    || { echo "ERROR: lib/amiga FASLs not produced" >&2; exit 1; }

# heap images: one per binary, beside it (policy above).  The m68k pair is
# saved by the staged binaries from the staged layout in FS-UAE, and each is
# restarted from the release root — where no image sits, so discovery has to
# take the PROGDIR: leg — and must report the restore and this version.
REL_DIR="build/release/$REL"
for t in aos3 aos3-fpu; do
    echo "--- Saving + verifying $REL/bin/$t/clamiga.img in FS-UAE ---"
    verify/realamiga/make-image.sh "$REL_DIR/bin/$t" "$REL_DIR" \
        || { echo "ERROR: heap image for bin/$t not produced" >&2; exit 1; }
    grep -q "^IMAGE-VERSION $VERSION" build/amiga/image.log || {
        echo "ERROR: the bin/$t image run reported another version than $VERSION — see build/amiga/image.log" >&2
        exit 1; }
done
cp "$MOS_IMG" "$STAGE/bin/mos/clamiga.img"

# docs: package API reference only (no benchmarks/screenshots)
cp docs/README.md docs/amiga.md docs/clamiga.md docs/ext.md docs/ffi.md \
   docs/gray.md docs/mop.md docs/mp.md docs/package-symbols.txt \
   docs/clamiga-documented-symbols.txt "$STAGE/docs/"

# examples, as-is
cp -R examples "$STAGE/examples"

cp README.md LICENSE "$STAGE/"

cat > "$STAGE/README-BINARY.txt" <<EOF
CL-Amiga $VERSION — binary release
==================================

Common Lisp for AmigaOS 3+ and MorphOS.

  bin/aos3/clamiga      AmigaOS 3.x, 68020 or better — runs on any CPU
  bin/aos3-fpu/clamiga  AmigaOS 3.x, hard-float build — REQUIRES an FPU
  bin/mos/clamiga       MorphOS (PowerPC, native)
  bin/*/clamiga.img     heap image of the bare boot, one per binary (see Startup)
  lib/                  runtime library (precompiled FASLs + Lisp sources)
  docs/                 package API reference (call signatures included)
  examples/             example programs (Lisp source)

Which AmigaOS binary?
---------------------
bin/aos3 does float math in software (mathieeedoubbas.library) and runs
on every 68020+ machine, FPU or not.  bin/aos3-fpu is compiled for the
68881/68882 FPU: float arithmetic runs directly on the FPU and is much
faster.  Use it if your machine has one — 68881/68882 boards, 68040/68060
(with the standard 68040/68060.library installed), Vampire/Apollo,
PiStorm.  On a machine without an FPU it will crash; when in doubt,
start with bin/aos3.

Both binaries print and read floats identically (conversion is exact
integer arithmetic, independent of the FPU), so FASLs and float-heavy
source files are fully interchangeable between them.

Quick start (AmigaOS shell)
---------------------------
  stack 131072
  cd clamiga-$VERSION
  bin/aos3/clamiga

(on MorphOS use bin/mos/clamiga, on FPU machines bin/aos3-fpu/clamiga)

The binary finds lib/ on its own: it looks in the current directory, in
PROGDIR:lib, and two directory levels above the executable — which is
exactly where lib/ sits in this layout.  No assigns or environment
variables are needed; you can run it from any current directory.

A stack of 128K (stack 131072) is recommended.  The AmigaOS default of
64K is enough for the core, but deeply nested source (the GUI libraries,
Quicklisp systems) needs more — with too little stack you get a clean
"C stack nearly exhausted" error instead of a crash.

For bigger programs raise the heap, e.g.:
  bin/aos3/clamiga --heap 16M

Startup
-------
Each binary starts from the clamiga.img beside it: a snapshot of the
booted runtime (boot + CLOS), restored in one read instead of loading
lib/boot.fasl and lib/clos.fasl form by form.  Keep the image next to
its binary — it is tied to that exact build and refused by any other.
"clamiga --no-image" boots from the FASLs instead, and "--boot-log"
prints the startup phase timings either way.

Libraries
---------
  (require "asdf")             ; ASDF system loader
  (require "quicklisp")        ; Quicklisp client
  (require "amiga/intuition")  ; windows, screens, IDCMP events
  (require "amiga/graphics")   ; drawing primitives
  (require "amiga/gadtools")   ; GadTools gadgets and menus
  (require "amiga/reaction")   ; ReAction helpers (OS 3.5+/3.2, MorphOS)
  (require "amiga/raw/<lib>")  ; generated 1:1 OS bindings, every library/class
  (require "amiga/exec")       ; memory introspection, chip RAM
  (require "amiga/audio")      ; audio.device sample playback

The core library and everything under lib/amiga/ (including the generated
raw OS bindings) ship precompiled (*.fasl, loaded directly -- the sources sit
next to them for reference); the remaining modules (asdf, quicklisp) are
Lisp sources that compile on your machine the first time they are required
and are cached under S:cl-amiga/faslcache/, so later loads are fast.

Documentation
-------------
docs/README.md is the index of the package reference: EXT (sockets, GC,
introspection), MP (threads), FFI, GRAY (Gray streams), MOP, CLAMIGA,
and the AMIGA.* GUI bindings — every function documented with its call
signature.

Examples
--------
  bin/aos3/clamiga --load examples/amiga/gfx/bouncing-lines.lisp
  bin/aos3/clamiga --load examples/amiga/reaction/listbrowser.lisp   (ReAction GUIs, see examples/amiga/README.md)

Project: https://github.com/mdbergmann/cl-amiga
EOF

# keep emulator/host metadata out of the archive
find "$STAGE" -name '*.uaem' -delete
find "$STAGE" -name '.DS_Store' -delete

# --- smoke test -----------------------------------------------------------
# Prove the deployed layout resolves lib/ executable-relatively: run a copy
# of the tree with the HOST binary substituted at bin/aos3/clamiga (same C
# search code as the Amiga builds), from an unrelated working directory.
if [ "$SMOKE" = 1 ]; then
    echo "--- Smoke test: lib resolution in deployed layout ---"
    SMOKEDIR=$(mktemp -d)
    trap 'rm -rf "$SMOKEDIR"' EXIT
    # -p: keep the staged mtimes, i.e. the FASL-newer-than-source ordering
    # that the archives preserve too and that REQUIRE decides on.
    cp -Rp "$STAGE" "$SMOKEDIR/rel"
    cp "$HOST_BIN" "$SMOKEDIR/rel/bin/aos3/clamiga"
    # Heap images are per-build: the staged bin/aos3/clamiga.img is the m68k
    # one, which the host binary would refuse and fall back to the FASLs — a
    # green run proving nothing about images.  Save a host image into the
    # copy with the same script the targets use, verify it from the release
    # root with the same check script, and require the main smoke run below
    # to have come from it.
    rm -f "$SMOKEDIR/rel/bin/aos3/clamiga.img"
    ( cd "$SMOKEDIR/rel/bin/aos3" && CLAMIGA_NO_USERINIT=1 CLAMIGA_HOME= $TIMEOUT \
        ./clamiga --no-userinit --no-image --non-interactive \
        --load "$ROOT/scripts/save-boot-image.lisp" ) > "$OUT/smoke-image.log" 2>&1 \
      && [ -s "$SMOKEDIR/rel/bin/aos3/clamiga.img" ] || {
        echo "ERROR: host image save in the release layout failed — see $OUT/smoke-image.log" >&2
        exit 1; }
    ( cd "$SMOKEDIR/rel" && CLAMIGA_NO_USERINIT=1 CLAMIGA_HOME= $TIMEOUT \
        bin/aos3/clamiga --no-userinit --non-interactive \
        --load "$ROOT/scripts/verify-boot-image.lisp" ) >> "$OUT/smoke-image.log" 2>&1 \
      && grep -q "^BOOT-IMAGE-VERIFIED" "$OUT/smoke-image.log" || {
        echo "ERROR: release layout did not start from bin/aos3/clamiga.img — see $OUT/smoke-image.log" >&2
        exit 1; }
    ( cd "$SMOKEDIR" && \
      CLAMIGA_NO_USERINIT=1 CLAMIGA_HOME= $TIMEOUT \
        "$SMOKEDIR/rel/bin/aos3/clamiga" --non-interactive --heap 48M \
        --eval '(format t "IMAGE-RESTORED ~a~%" ext:*image-restored-p*)' \
        --eval '(require "gray-streams")' \
        --eval '(require "asdf")' \
        --eval '(format t "SHIM-AT ~a~%" (asdf:system-source-directory (asdf:find-system "cl+ssl")))' \
        --eval '(require "amiga/raw/exec")' \
        --eval '(require "amiga/reaction")' \
        --eval '(format t "MEMF-CHIP ~a~%" (symbol-value (find-symbol "+MEMF-CHIP+" "AMIGA.RAW.EXEC")))' \
        --eval '(format t "SMOKE-OK ~a~%" (lisp-implementation-version))' \
        --eval '(quit)' ) | tee "$OUT/smoke.log" | grep -q "SMOKE-OK $VERSION" || {
        echo "ERROR: smoke test failed — see $OUT/smoke.log" >&2; exit 1; }
    grep -q "^IMAGE-RESTORED T" "$OUT/smoke.log" || {
        echo "ERROR: the smoke session did not come from bin/aos3/clamiga.img — see $OUT/smoke.log" >&2
        exit 1; }
    # --no-image: the FASL boot must still bring the layout up — it is the
    # fallback when an image is refused.
    ( cd "$SMOKEDIR" && CLAMIGA_NO_USERINIT=1 CLAMIGA_HOME= $TIMEOUT \
        "$SMOKEDIR/rel/bin/aos3/clamiga" --non-interactive --no-image \
        --eval '(format t "FASL-BOOT ~a ~a~%" ext:*image-restored-p* (lisp-implementation-version))' \
        --eval '(quit)' ) | tee "$OUT/smoke-noimage.log" | grep -q "^FASL-BOOT NIL $VERSION" || {
        echo "ERROR: --no-image FASL boot of the release layout failed — see $OUT/smoke-noimage.log" >&2
        exit 1; }
    # The cl+ssl shim must resolve out of the release's own lib/shims/ —
    # this is what makes drakma/hunchentoot TLS work from a binary install
    # regardless of what Quicklisp/ocicl have on disk.
    grep -q "SHIM-AT .*rel/lib/shims/cl+ssl" "$OUT/smoke.log" || {
        echo "ERROR: cl+ssl shim did not resolve from the release lib/shims/ — see $OUT/smoke.log" >&2
        exit 1; }
    # lib/amiga must come up from the shipped FASLs (not the sources next to
    # them) and work: REQUIRE prints the file it loads.  REQUIRE's
    # executable-relative leg concatenates the layout onto the exedir and
    # never normalises, so the loaded path reads
    # rel/bin/aos3/../../lib/amiga/... — the `rel/` anchor plus `.*` still
    # pins the file to the staged copy, which is what this asserts.
    grep -q "; Loading .*rel/.*lib/amiga/raw/exec\.fasl" "$OUT/smoke.log" &&
    grep -q "; Loading .*rel/.*lib/amiga/reaction\.fasl" "$OUT/smoke.log" &&
    grep -q "^MEMF-CHIP 2" "$OUT/smoke.log" || {
        echo "ERROR: lib/amiga did not load from the release FASLs — see $OUT/smoke.log" >&2
        exit 1; }
    echo "smoke test passed"
fi

# --- archives -------------------------------------------------------------
echo "--- Archiving ---"
rm -f "$OUT/$REL-bin.zip" "$OUT/$REL-bin.lha"
( cd "$OUT" && zip -rq "$REL-bin.zip" "$REL" )
if command -v lha > /dev/null 2>&1; then
    ( cd "$OUT" && lha aq "$REL-bin.lha" "$REL" ) \
        || echo "warning: lha archiving failed — the .zip is still valid"
else
    echo "note: lha not found — only the .zip was created"
fi

echo "=== Done ==="
ls -lh "$OUT" | grep -E "$REL" || true
