#!/bin/sh
# The Workbench icons of the binary release (icons/*.info, written by
# scripts/make-icons.py) -- their on-disk layout, byte by byte, independent
# of the generator.  Takes no clamiga binary.
#
# Why a test: the drawer icon's layout is easy to "correct" into something
# icon.library rejects.  On disk, struct DrawerData is split: its first 56
# bytes (the NewWindow + dd_CurrentX/Y) follow the 78-byte DiskObject
# directly, the Image comes next, and the OS 2.x dd_Flags/dd_ViewModes are
# the LAST six bytes of the file -- the layout of every drawer icon AmigaOS
# writes (SYS:Prefs.info, Devs.info, ...), see the generator's docstring.
# A 62-byte block after the image, the in-memory struct, makes Workbench
# fall back silently to its default drawer icon and window (seen on an
# AmigaOS 3.2.3 Vampire, 2026-09-15).
#
# Covered:
#   every icon: magic E310, version 1, OS 2.x revision in the gadget's
#     UserData, a 48x24 two-plane image
#   the launchers: project icons, default tool C:IconX, a WINDOW tool
#     type, 128K stack, pinned to the first row of the package root
#   the root guide icons: project icons, default tool SYS:Utilities/MultiView,
#     pinned to the second row, same columns as the launchers
#   Guide.info: the same, unpositioned (NO_ICON_POSITION)
#   Drawer.info: type WBDRAWER (2), DrawerData at 78 with the window
#     580x160 at 40/30 on the Workbench screen, the Image header at 134,
#     dd_Flags 1 (show only files with icons) and dd_ViewModes 1 (view by
#     icon) as the last six bytes, 448 bytes in all
#   when python3 is available: the generator reproduces the committed
#     files byte for byte (art or layout changes must be committed)
#
# Run: sh tests/test_icons.sh

ROOT=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1

passed=0
failed=0
total=0
ok()   { total=$((total + 1)); passed=$((passed + 1)); echo "  ok  $1"; }
fail() { total=$((total + 1)); failed=$((failed + 1)); echo "  FAIL  $1"; shift; for l in "$@"; do echo "    $l"; done; }

# hex <file> <offset> <length>: the bytes as lower-case hex
hex() { od -An -tx1 -j "$2" -N "$3" "$1" | tr -d ' \n'; }
# u32 <file> <offset>: a big-endian LONG, decimal
u32() { printf '%d' "0x$(hex "$1" "$2" 4)"; }
u16() { printf '%d' "0x$(hex "$1" "$2" 2)"; }

# --- every icon -----------------------------------------------------------
for f in icons/*.info; do
    bad=""
    [ "$(hex "$f" 0 4)" = "e3100001" ] || bad="$bad magic/version"
    [ "$(u32 "$f" 44)" -eq 1 ] || bad="$bad gadget-userdata-not-os2-revision"
    [ "$(u16 "$f" 12)" -eq 48 ] && [ "$(u16 "$f" 14)" -eq 24 ] || bad="$bad gadget-size"
    [ -z "$bad" ] && ok "diskobject_$(basename "$f" .info)" || fail "diskobject_$(basename "$f" .info)" "$bad"
done

# --- project icons ------------------------------------------------------
# project <name> <tool> <x> <y> [stack]: type WBPROJECT (4), the default
# tool string, the position (hex of two LONGs), the stack
project() {
    f="icons/$1.info"; bad=""
    [ "$(hex "$f" 48 1)" = "04" ] || bad="$bad type-not-project"
    [ "$(u32 "$f" 66)" -eq 0 ] || bad="$bad has-drawerdata"
    [ "$(hex "$f" 58 8)" = "$3$4" ] || bad="$bad position=$(hex "$f" 58 8)"
    [ "$(u32 "$f" 74)" -eq "$5" ] || bad="$bad stack=$(u32 "$f" 74)"
    # image header at 78: 48x24, depth 2; the default tool follows the planes
    [ "$(u16 "$f" 82)" -eq 48 ] && [ "$(u16 "$f" 84)" -eq 24 ] && [ "$(u16 "$f" 86)" -eq 2 ] || bad="$bad image-header"
    tool=$(od -An -c -j 390 -N "$(u32 "$f" 386)" "$f" | tr -d ' \n' | sed 's/\\0$//')
    [ "$tool" = "$2" ] || bad="$bad default-tool='$tool'"
    [ -z "$bad" ] && ok "project_$1" || fail "project_$1" "$bad"
}
# the two rows of the package root: columns 64/234/404, rows 8/60
X0=00000040; X1=000000ea; X2=00000194; Y0=00000008; Y1=0000003c
NOPOS=80000000
project CLAmiga            "C:IconX" $X0 $Y0 131072
project CLAmiga-FPU        "C:IconX" $X1 $Y0 131072
project Clamacs            "C:IconX" $X2 $Y0 131072
project README-FIRST.guide "SYS:Utilities/MultiView" $X0 $Y1 16384
project cl-amiga.guide     "SYS:Utilities/MultiView" $X1 $Y1 16384
project clamacs.guide      "SYS:Utilities/MultiView" $X2 $Y1 16384
project Guide              "SYS:Utilities/MultiView" $NOPOS $NOPOS 16384
# the launchers carry their console window as a WINDOW= tool type
for n in CLAmiga CLAmiga-FPU Clamacs; do
    if LC_ALL=C grep -q "WINDOW=" "icons/$n.info"; then ok "tooltype_window_$n"; else fail "tooltype_window_$n"; fi
done
# the root guide icons are Guide.info with a position and nothing else
for n in README-FIRST cl-amiga clamacs; do
    if [ "$(hex "icons/$n.guide.info" 66 382)" = "$(hex icons/Guide.info 66 382)" ]; then
        ok "guide_icon_same_image_$n"
    else
        fail "guide_icon_same_image_$n"
    fi
done

# --- the drawer icon ----------------------------------------------------
f=icons/Drawer.info; bad=""
[ "$(wc -c < "$f" | tr -d ' ')" -eq 448 ] || bad="$bad size=$(wc -c < "$f")"
[ "$(hex "$f" 48 1)" = "02" ] || bad="$bad type-not-WBDRAWER"
[ "$(u32 "$f" 66)" -ne 0 ] || bad="$bad no-drawerdata"
# DrawerData right after the DiskObject: NewWindow left/top/width/height
[ "$(u16 "$f" 78)" -eq 40 ] && [ "$(u16 "$f" 80)" -eq 30 ] || bad="$bad window-pos=$(u16 "$f" 78)/$(u16 "$f" 80)"
[ "$(u16 "$f" 82)" -eq 580 ] && [ "$(u16 "$f" 84)" -eq 160 ] || bad="$bad window-size=$(u16 "$f" 82)x$(u16 "$f" 84)"
[ "$(u16 "$f" 124)" -eq 1 ] || bad="$bad newwindow-type-not-wbenchscreen"
[ "$(hex "$f" 126 8)" = "0000000000000000" ] || bad="$bad dd_current-not-0"
# then the Image header at 134
[ "$(u16 "$f" 138)" -eq 48 ] && [ "$(u16 "$f" 140)" -eq 24 ] && [ "$(u16 "$f" 142)" -eq 2 ] || bad="$bad image-header-not-at-134"
# and dd_Flags / dd_ViewModes as the last six bytes
[ "$(hex "$f" 442 6)" = "000000010001" ] || bad="$bad tail=$(hex "$f" 442 6)"
[ -z "$bad" ] && ok "drawer_layout" || fail "drawer_layout" "$bad"

# --- generator reproduces the committed files ---------------------------
if command -v python3 > /dev/null 2>&1; then
    WORK=$(mktemp -d "${TMPDIR:-/tmp}/clamiga_icons_XXXXXX")
    trap 'rm -rf "$WORK"' EXIT INT TERM
    if python3 scripts/make-icons.py "$WORK" > /dev/null 2>&1; then
        bad=""
        for f in icons/*.info; do
            cmp -s "$f" "$WORK/$(basename "$f")" || bad="$bad $(basename "$f")"
        done
        [ -z "$bad" ] && ok "generator_matches_committed" || fail "generator_matches_committed" "differs:$bad (re-run scripts/make-icons.py and commit icons/)"
    else
        fail "generator_runs" "python3 scripts/make-icons.py failed"
    fi
    rm -rf scripts/__pycache__
else
    echo "  skip  generator_matches_committed (no python3)"
fi

echo ""
echo "$passed passed, $failed failed, $total total"
if [ "$failed" -gt 0 ]; then echo "FAIL"; exit 1; else echo "PASS"; exit 0; fi
