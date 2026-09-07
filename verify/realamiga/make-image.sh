#!/bin/sh
# make-image.sh — save the bare-boot heap image an Amiga deployment ships
# beside its binary, unattended in FS-UAE, and prove that it restores.
#
#   verify/realamiga/make-image.sh BINDIR [VERIFY-CWD]
#
#   BINDIR      repo-relative directory holding the m68k `clamiga` to save
#               the image for: build/cross, build/cross-fpu, or a staged
#               release's bin/aos3.  The image is written next to it as
#               BINDIR/clamiga.img — where startup looks for one (PROGDIR:).
#   VERIFY-CWD  repo-relative directory the restore check runs from
#               (default: the repo root).  It must not contain a
#               clamiga.img: the current-directory leg of discovery would
#               shadow the beside-the-binary leg this is meant to prove.
#
# Heap images are per-build (specs/image-save-load.md), so the host cannot
# write them the way it writes FASLs: the Amiga binary itself boots from
# the FASLs in FS-UAE and dumps its heap.  Both runs go through the
# boot-override hook of call-on-ustartup (in place of the test suite):
#
#   1. save:   cd BINDIR;     clamiga --no-image --load scripts/save-boot-image.lisp
#   2. verify: cd VERIFY-CWD; clamiga            --load scripts/verify-boot-image.lisp
#              (no --image: discovery has to find BINDIR/clamiga.img itself)
#
# Needs the FS-UAE setup of `make -f Makefile.cross test-amiga`; the 68040
# config there has an FPU, so it serves the FPU=1 binary too.  Exit 0 only
# when the verify run printed BOOT-IMAGE-VERIFIED; the emulator-side log
# stays in build/amiga/image.log (the suite log is left alone).
set -u

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT" || exit 1

BINDIR="${1:?usage: make-image.sh BINDIR [VERIFY-CWD]}"
VCWD="${2:-}"
LOG=build/amiga/image.log

case "$BINDIR" in
    /*|*:*)
        echo "ERROR: BINDIR must be relative to the repo root — FS-UAE sees it as CLAmiga:$BINDIR" >&2
        exit 1 ;;
esac
BINDIR=${BINDIR%/}
VCWD=${VCWD%/}
[ -f "$BINDIR/clamiga" ] || {
    echo "ERROR: $BINDIR/clamiga missing — make -f Makefile.cross amiga first" >&2; exit 1; }
[ -d "${VCWD:-.}" ] || {
    echo "ERROR: verify directory '$VCWD' does not exist" >&2; exit 1; }
if [ -f "${VCWD:-.}/clamiga.img" ]; then
    echo "ERROR: ${VCWD:-.}/clamiga.img exists — discovery would take it ahead of $BINDIR/clamiga.img and hide what this verifies; remove it" >&2
    exit 1
fi
if pgrep -x fs-uae >/dev/null 2>&1; then
    echo "ERROR: an FS-UAE is already running — pkill fs-uae first" >&2
    exit 1
fi

IMG="$BINDIR/clamiga.img"
mkdir -p build/amiga
rm -f "$IMG" "$IMG.uaem" "$LOG" "$LOG.uaem"

# AmigaDOS view of the same paths: CLAmiga: is the repo root
# (verify.fs-uae hard_drive_1).  An empty VCWD is the root itself.
AMI_BIN="CLAmiga:$BINDIR/clamiga"
AMI_BINDIR="CLAmiga:$BINDIR"
AMI_VCWD="CLAmiga:$VCWD"
AMI_LOG="CLAmiga:$LOG"

# The AmigaDOS script call-on-ustartup executes instead of the suite.  It
# must write the "=== run end ===" marker run-fs-uae.sh watches for and quit
# the emulator itself.  --boot-log on both runs puts the FASL-boot and the
# image-restore timings side by side in the log.
cat > build/amiga/boot-override <<EOF
stack 128000
echo "=== image start ===" >$AMI_LOG
date >>$AMI_LOG
echo "=== save: $AMI_BIN ===" >>$AMI_LOG
cd $AMI_BINDIR
$AMI_BIN --no-userinit --no-image --heap 8M --non-interactive --boot-log --load CLAmiga:scripts/save-boot-image.lisp >>$AMI_LOG
echo "=== verify from $AMI_VCWD ===" >>$AMI_LOG
cd $AMI_VCWD
$AMI_BIN --no-userinit --heap 8M --non-interactive --boot-log --load CLAmiga:scripts/verify-boot-image.lisp >>$AMI_LOG
echo "=== run end ===" >>$AMI_LOG
date >>$AMI_LOG
C:UAEquit
EOF

echo "=== Launching FS-UAE — bare-boot heap image for $BINDIR/clamiga ==="
KEEP_BOOT_OVERRIDE=1 FSUAE_LOG="$LOG" \
    verify/realamiga/run-fs-uae.sh verify/realamiga/verify.fs-uae
rm -f build/amiga/boot-override "$IMG.uaem" "$LOG.uaem"

fail() {
    echo "ERROR: $1" >&2
    if [ -f "$LOG" ]; then
        echo "--- $LOG ---" >&2
        cat "$LOG" >&2
    fi
    rm -f "$IMG" "$IMG.uaem"
    exit 1
}

[ -f "$LOG" ] || fail "no $LOG — FS-UAE never ran the boot-override"

echo "=== Image run log (boot phases, markers) ==="
grep -E '^; \[boot\]|^=== (save|verify)|^IMAGE-|^; Image saved|IMAGE-FAILED|^BOOT-IMAGE' "$LOG" \
    | sed 's/^/    /'

grep -q '=== run end ===' "$LOG" \
    || fail "the run did not complete (watchdog kill? see the log)"
grep -q '^; Image saved to' "$LOG" \
    || fail "the save run wrote no image"
grep -q 'IMAGE-FAILED' "$LOG" \
    && fail "a check failed (see BOOT-IMAGE-FAILED / SAVE-BOOT-IMAGE-FAILED above)"
grep -q '^BOOT-IMAGE-VERIFIED' "$LOG" \
    || fail "the restore check did not print BOOT-IMAGE-VERIFIED"
[ -s "$IMG" ] || fail "$IMG is missing or empty after a verified run"

echo "=== $IMG: $(wc -c < "$IMG" | tr -d ' ') bytes, restore verified in FS-UAE ==="
exit 0
