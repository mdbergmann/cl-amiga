#!/bin/sh
# Run the eta-hab load verification in a Debian Bookworm arm64 container.
#
# Builds clamiga from this repo inside the container, mirrors the developer
# host's ASDF/quicklisp layout, and loads eta-hab (chipi + chipi-api +
# chipi-ui + binding-knx + cl-eta + slynk) end to end.
#
# Usage:
#   verify/linux-arm64/run.sh                 # load-verify, then exit
#   ETA_STAY=1 verify/linux-arm64/run.sh      # keep running, slynk on :4005
#   CLAMIGA_HEAP=768M verify/linux-arm64/run.sh
#   CLAMIGA_LOCK_DIAG=1000 verify/linux-arm64/run.sh  # report MP lock waits >1s
#                                             # (named lock + holder, stderr)
#
# Notes:
#   * KNX (192.168.50.40) is reached from the container via NAT; the gateway
#     may refuse the tunnel if its slots are taken — the run reports it.
#   * NET_ADMIN is needed to alias 192.168.50.43 (the address eta-hab's
#     defconfig binds api/ui to) onto the container's lo.
#   * /root/quicklisp/local-projects is a container-private tmpfs that the
#     entry script fills with symlinks to the host's local-projects entries
#     (minus icl, whose vendored ocicl copies of babel/static-vectors/...
#     shadow the dist releases and break on clamiga).  Sharing the host's
#     local-projects directly is NOT safe: quicklisp rewrites its
#     system-index.txt with container-absolute paths, corrupting the host's
#     index.  Only the dist cache (releases) is shared read-write.
#   * ~/Development/MySources and ~/quicklisp are also mounted at their
#     macOS paths so the host's absolute symlinks (the swank stub ->
#     cl-amiga/contrib/shims, ~/common-lisp entries, ...) resolve.
#   * cl-eta is mounted read-only and copied to a writable /tmp/cl-eta by
#     the entry script: chipi's api-env writes the apikey store into
#     runtime/ next to the eta-hab system at boot (like on the Pi), and
#     the checkout itself must stay untouched.
#   * mini.local / picellar (openhab + influx endpoints) are pinned to
#     127.0.0.1: unresolvable in the container, each item push would hang
#     ~1 min in DNS — pinned, the POSTs fail instantly (connection refused,
#     logged) and the load proceeds at full speed.
set -e

REPO=$(cd "$(dirname "$0")/../.." && pwd)
SRC="$HOME/Development/MySources"
IMAGE=clamiga-verify-linux-arm64

docker build -q -t "$IMAGE" "$REPO/verify/linux-arm64" >/dev/null

STAY_ARGS=""
if [ -n "$ETA_STAY" ]; then
    STAY_ARGS="-e ETA_STAY=1 -p 4005:4005"
fi

# shellcheck disable=SC2086
exec docker run --rm --platform linux/arm64 \
    --cap-add NET_ADMIN \
    --add-host mini.local:127.0.0.1 \
    --add-host picellar:127.0.0.1 \
    -e CLAMIGA_HEAP="${CLAMIGA_HEAP:-512M}" \
    -e CLAMIGA_LOCK_DIAG="${CLAMIGA_LOCK_DIAG:-}" \
    $STAY_ARGS \
    -v "$REPO":/cl-amiga:ro \
    -v "$SRC":"$SRC":ro \
    -v "$HOME/quicklisp":/root/quicklisp \
    -v "$HOME/quicklisp":"$HOME/quicklisp":ro \
    --tmpfs /root/quicklisp/local-projects \
    -e MAC_QL="$HOME/quicklisp" \
    -v clamiga-verify-linux-arm64-cache:/root/.cache \
    -v "$SRC/cl-hab":/work/cl-hab:ro \
    -v "$SRC/knx-con":/work/knx-con:ro \
    -v "$SRC/cl-gserver":/work/cl-gserver:ro \
    -v "$SRC/cl-eta":/work/cl-eta-src:ro \
    -v "$HOME/.emacs.d/plugins/sly":/work/sly:ro \
    "$IMAGE" /cl-amiga/verify/linux-arm64/container-entry.sh
