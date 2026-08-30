#!/bin/sh
# gen-amiga-bindings.sh — regenerate lib/amiga/raw/*.lisp (the raw OS bindings)
#
# Inputs:
#   * the AmigaOS 3.2 NDK — SFD/*_lib.sfd for the function tables,
#     Include_I/**/*.i for struct layouts and constants, Include_H/**/*.h
#     for the constants that exist only as C macros (the ReAction tags).
#     Taken from NDK=<dir> (default tools/aos32-ndk, an unpacked copy of
#     the original NDK 3.2 — not redistributed, gitignored); when that is
#     absent, from the copy inside the cross toolchain
#     (tools/m68k-amigaos-gcc/prefix, built by tools/setup-toolchain.sh:
#     ndk/lib/sfd + ndk-include holding .i and .h side by side).  The two
#     are the same NDK release; the toolchain copy only adds the Roadshow
#     bsdsocket interfaces and ==basetype lines.
#   * optionally the MorphOS SDK: MOS_SDK=<dir> pointing at a copy of
#     GG:os-include (needs its fd/ and clib/ subdirectories).  Its fd+clib
#     pairs are converted to sfd with the toolchain's fd2sfd and merged —
#     functions MorphOS lacks at the same LVO get a (not :morphos) guard,
#     MorphOS-only ones a :morphos guard.  Without MOS_SDK the output is
#     AmigaOS-only (no guards, no MorphOS extensions) — commit only output
#     generated WITH the MorphOS SDK.
#   * optionally the MUI 3.8 developer kit: MUI_SDK=<dir> (default
#     tools/mui-sdk, a copy of MUI:Developer — FD/muimaster_lib.fd,
#     C/Include/clib/muimaster_protos.h, C/Include/libraries/mui.h).  Its
#     fd+clib pair is converted with fd2sfd like the MorphOS SDK's and joins
#     the AmigaOS (primary) tables; C/Include is a second C-header root, so
#     the muimaster module gets every MUIA_/MUIM_/MUIV_/MUII_/MUIO_ tag and
#     the MUIC_* class-name strings of libraries/mui.h.  Without MUI_SDK
#     muimaster is emitted from the MorphOS SDK's function table alone
#     (no constants) — commit only output generated WITH the MUI SDK.
#
# Usage:
#   scripts/gen-amiga-bindings.sh                      # NDK only
#   MOS_SDK=~/sdk/morphos/os-include scripts/gen-amiga-bindings.sh
#   OUT=/tmp/raw scripts/gen-amiga-bindings.sh         # elsewhere
#
# Environment knobs (all optional): NDK (unpacked NDK 3.2), PREFIX
# (toolchain prefix), HOST_BIN, NDK_SFD, NDK_INCLUDE, NDK_INCLUDE_H, OUT,
# MOS_SDK, MUI_SDK, BINDGEN_LIBS (comma list), BINDGEN_MOS_ONLY (comma
# list of MorphOS-only libraries to emit), BINDGEN_DOCSTRINGS=0.
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PREFIX=${PREFIX:-$ROOT/tools/m68k-amigaos-gcc/prefix}
HOST_BIN=${HOST_BIN:-$ROOT/build/host/clamiga}
NDK=${NDK:-$ROOT/tools/aos32-ndk}
OUT=${OUT:-$ROOT/lib/amiga/raw}

if [ -d "$NDK/SFD" ] && [ -d "$NDK/Include_I" ] && [ -d "$NDK/Include_H" ]; then
    NDK_SFD=${NDK_SFD:-$NDK/SFD}
    NDK_INCLUDE=${NDK_INCLUDE:-$NDK/Include_I}
    NDK_INCLUDE_H=${NDK_INCLUDE_H:-$NDK/Include_H}
    echo "NDK: $NDK (original NDK 3.2 layout)"
else
    NDK_SFD=${NDK_SFD:-$PREFIX/m68k-amigaos/ndk/lib/sfd}
    NDK_INCLUDE=${NDK_INCLUDE:-$PREFIX/m68k-amigaos/ndk-include}
    NDK_INCLUDE_H=${NDK_INCLUDE_H:-$NDK_INCLUDE}
    echo "NDK: $PREFIX (toolchain copy; set NDK=<unpacked NDK 3.2> to use the original)"
fi

if [ ! -x "$HOST_BIN" ]; then
    echo "gen-amiga-bindings: $HOST_BIN not found — run 'make host' first" >&2
    exit 1
fi
if [ ! -d "$NDK_SFD" ] || [ ! -d "$NDK_INCLUDE" ] || [ ! -d "$NDK_INCLUDE_H" ]; then
    echo "gen-amiga-bindings: NDK not found — unpack NDK 3.2 to $NDK or build the toolchain (tools/setup-toolchain.sh)" >&2
    exit 1
fi

FD2SFD=${FD2SFD:-$PREFIX/bin/fd2sfd}

# Convert every <fd dir>/*_lib.fd that has a <clib dir>/*_protos.h twin to
# <out dir>/*_lib.sfd; prints the count.  A fd without a clib (the MUI
# kit's muiclass_lib.fd, an interface .mcc files export) is skipped: no
# library to open, no module.
fd_dir_to_sfd() {
    fddir="$1"; clibdir="$2"; outdir="$3"
    n=0
    for fd in "$fddir"/*_lib.fd; do
        b=$(basename "$fd" _lib.fd)
        clib="$clibdir/${b}_protos.h"
        if [ -f "$clib" ]; then
            if "$FD2SFD" --quiet "$fd" "$clib" "$outdir/${b}_lib.sfd" >/dev/null 2>&1; then
                n=$((n + 1))
            else
                echo "gen-amiga-bindings: warning: fd2sfd failed for $b" >&2
            fi
        fi
    done
    echo "$n"
}

need_fd2sfd() {
    if [ ! -x "$FD2SFD" ]; then
        echo "gen-amiga-bindings: $FD2SFD not found (part of the toolchain)" >&2
        exit 1
    fi
}

MOS_TMP=
if [ -n "$MOS_SDK" ]; then
    need_fd2sfd
    if [ ! -d "$MOS_SDK/fd" ] || [ ! -d "$MOS_SDK/clib" ]; then
        echo "gen-amiga-bindings: MOS_SDK=$MOS_SDK needs fd/ and clib/ subdirectories" >&2
        exit 1
    fi
    MOS_TMP=$(mktemp -d "${TMPDIR:-/tmp}/mos-sfd.XXXXXX")
    n=$(fd_dir_to_sfd "$MOS_SDK/fd" "$MOS_SDK/clib" "$MOS_TMP")
    echo "MorphOS SDK: $n libraries converted to sfd"
    export BINDGEN_MOS_SFD="$MOS_TMP"
fi

# The MUI 3.8 developer kit (MUI:Developer): FD/ + C/Include/clib/ for the
# function table, C/Include/ as the second C-header root (libraries/mui.h).
MUI_SDK=${MUI_SDK:-$ROOT/tools/mui-sdk}
MUI_TMP=
if [ -d "$MUI_SDK/FD" ] && [ -d "$MUI_SDK/C/Include/clib" ] && [ -f "$MUI_SDK/C/Include/libraries/mui.h" ]; then
    need_fd2sfd
    MUI_TMP=$(mktemp -d "${TMPDIR:-/tmp}/mui-sfd.XXXXXX")
    n=$(fd_dir_to_sfd "$MUI_SDK/FD" "$MUI_SDK/C/Include/clib" "$MUI_TMP")
    echo "MUI SDK: $MUI_SDK ($n libraries converted to sfd)"
    export BINDGEN_MUI_SFD="$MUI_TMP" BINDGEN_MUI_INCLUDE_H="$MUI_SDK/C/Include"
else
    echo "MUI SDK: none (set MUI_SDK=<copy of MUI:Developer with FD/ and C/Include/> — muimaster is emitted from the MorphOS SDK function table only, without libraries/mui.h constants)"
fi

# The directory holds generated files only — clear stale ones.
mkdir -p "$OUT"
find "$OUT" -name '*.lisp' -type f -exec rm -f {} +

export BINDGEN_NDK_SFD="$NDK_SFD" BINDGEN_NDK_INCLUDE="$NDK_INCLUDE" \
       BINDGEN_NDK_INCLUDE_H="$NDK_INCLUDE_H" BINDGEN_OUT="$OUT"
"$HOST_BIN" --non-interactive --no-userinit --heap 64M \
    --load "$ROOT/scripts/gen-amiga-bindings.lisp" </dev/null
status=$?

[ -n "$MOS_TMP" ] && rm -rf "$MOS_TMP"
[ -n "$MUI_TMP" ] && rm -rf "$MUI_TMP"
exit $status
