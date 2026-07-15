#!/bin/sh
# Container-side entry point: copy the read-only source mount to a
# writable tree, then run the requested command in it.  With no
# arguments, runs the fast test tier (make test).
#
# Expected mounts (see run.sh):
#   /cl-amiga   cl-amiga repo (ro)
set -e

echo "=== [1/2] Copying cl-amiga sources to /tmp/cl-amiga ==="
mkdir -p /tmp/cl-amiga
(cd /cl-amiga && tar cf - \
    --exclude=./build \
    --exclude=./tools \
    --exclude=./.git \
    --exclude=./verify/realamiga \
    . ) | tar xf - -C /tmp/cl-amiga

cd /tmp/cl-amiga
if [ $# -eq 0 ]; then
    set -- make test
fi
echo "=== [2/2] Running: $* ==="
exec "$@"
