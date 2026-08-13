#!/bin/sh
# Install the AmiSSL SDK into tools/amissl-sdk (headers only are used).
#
# The SDK gives the Amiga cross build TLS support: when
# tools/amissl-sdk/AmiSSL/Developer/include/proto/amissl.h exists,
# Makefile.cross compiles with -DCL_HAVE_AMISSL and the runtime talks to
# amisslmaster.library at runtime (AmiSSL v5+, the AmigaOS/MorphOS port of
# OpenSSL 3.x — Apache-2.0 licensed, https://github.com/jens-maus/amissl).
# Without the SDK the build still succeeds; (ext:tls-available-p) is just
# NIL on Amiga.
#
# Requires `lha` (or `lhasa`) on PATH to unpack the Amiga archive
# (macOS: brew install lha).
#
# Flags:
#   --force    reinstall even if the SDK is already present.

set -eu

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
SDK_DIR="$REPO_ROOT/tools/amissl-sdk"
MARKER="$SDK_DIR/AmiSSL/Developer/include/proto/amissl.h"

# Pinned release — bump deliberately and re-run cross test-amiga afterwards.
AMISSL_VERSION="5.27"
ARCHIVE="AmiSSL-$AMISSL_VERSION-SDK.lha"
URL="https://github.com/jens-maus/amissl/releases/download/$AMISSL_VERSION/$ARCHIVE"

FORCE=0
[ "${1:-}" = "--force" ] && FORCE=1

if [ -f "$MARKER" ] && [ "$FORCE" -eq 0 ]; then
    echo "=> AmiSSL SDK already installed at $SDK_DIR (use --force to reinstall)"
    exit 0
fi

LHA=$(command -v lha 2>/dev/null || command -v lhasa 2>/dev/null || true)
if [ -z "$LHA" ]; then
    echo "ERROR: need 'lha' or 'lhasa' to unpack $ARCHIVE (macOS: brew install lha)" >&2
    exit 1
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "=> downloading $URL"
curl -fsSL -o "$TMP/$ARCHIVE" "$URL"

echo "=> extracting into $SDK_DIR"
rm -rf "$SDK_DIR"
mkdir -p "$SDK_DIR"
(cd "$SDK_DIR" && "$LHA" -xq "$TMP/$ARCHIVE")

if [ ! -f "$MARKER" ]; then
    echo "ERROR: extraction finished but $MARKER is missing" >&2
    exit 1
fi
echo "=> AmiSSL SDK $AMISSL_VERSION installed ($SDK_DIR)"
echo "   Rebuild with: make -f Makefile.cross amiga"
