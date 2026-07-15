#!/bin/sh
# Container-side entry point: build clamiga from the read-only source mount,
# wire up the ASDF/quicklisp environment to mirror the developer host, then
# run the eta-boot verification script.
#
# Expected mounts (see run.sh):
#   /cl-amiga            cl-amiga repo (ro)
#   /root/quicklisp      host ~/quicklisp (rw — dist fetches, local-projects forks)
#   /work/cl-hab         chipi + chipi-api + chipi-ui + bindings/knx (ro)
#   /work/knx-con        knx-conn (ro)
#   /work/cl-gserver     sento (ro)
#   /work/cl-eta-src     cl-eta + eta-hab (ro; copied to writable /tmp/cl-eta)
#   /work/sly            SLY fork with the clamiga slynk backend (ro)
set -e

echo "=== [1/4] Copying cl-amiga sources to /tmp/cl-amiga ==="
mkdir -p /tmp/cl-amiga
(cd /cl-amiga && tar cf - \
    --exclude=./build \
    --exclude=./tools \
    --exclude=./.git \
    --exclude=./verify/realamiga \
    . ) | tar xf - -C /tmp/cl-amiga

echo "=== [2/4] Building clamiga (make host) ==="
if ! make -C /tmp/cl-amiga host -j"$(nproc)" >/tmp/clamiga-build.log 2>&1; then
    echo "BUILD FAILED — tail of /tmp/clamiga-build.log:"
    tail -50 /tmp/clamiga-build.log
    exit 1
fi
/tmp/cl-amiga/build/host/clamiga --version 2>/dev/null || true

echo "=== [3/4] Setting up ASDF environment ==="
# /root/quicklisp/local-projects is a container-private tmpfs (see run.sh);
# populate it with symlinks to the host's entries via the macOS-path mount,
# skipping icl (vendored ocicl .asds shadow dist releases and break clamiga)
# and the host's system-index.txt (paths only valid on the host).
for p in "${MAC_QL:-/root/quicklisp}"/local-projects/*/; do
    name=$(basename "$p")
    [ "$name" = "icl" ] && continue
    ln -sfn "$p" "/root/quicklisp/local-projects/$name"
done

# /root/quicklisp/dists/quicklisp/installed is a container-private tmpfs
# (see run.sh): quicklisp install markers hold absolute paths, and letting
# the container write /root/... paths into the shared metadata makes the
# host treat those releases as uninstalled.  Seed the tmpfs from the host's
# markers with the paths rewritten for the container, so already-installed
# releases resolve without re-extraction.
for d in releases systems; do
    mkdir -p "/root/quicklisp/dists/quicklisp/installed/$d"
    for f in "${MAC_QL:-/root/quicklisp}"/dists/quicklisp/installed/$d/*.txt; do
        [ -e "$f" ] || continue
        sed "s|^${MAC_QL:-/root/quicklisp}/|/root/quicklisp/|" "$f" \
            > "/root/quicklisp/dists/quicklisp/installed/$d/$(basename "$f")"
    done
done

# Mirror the developer host's ~/common-lisp symlinks (ASDF source registry).
mkdir -p /root/common-lisp
ln -sf /work/cl-hab/chipi.asd /root/common-lisp/chipi.asd
ln -sf /work/cl-hab/chipi-api.asd /root/common-lisp/chipi-api.asd
ln -sf /work/cl-hab/chipi-ui.asd /root/common-lisp/chipi-ui.asd
ln -sf /work/knx-con/knx-conn.asd /root/common-lisp/knx-conn.asd
ln -sf /work/cl-gserver/sento.asd /root/common-lisp/sento.asd
ln -sf /tmp/cl-eta/cl-eta.asd /root/common-lisp/cl-eta.asd

# cl-eta must be writable at run time (chipi's api-env creates
# runtime/apikey-sign-key next to the eta-hab system, as on the Pi) —
# copy the read-only mount to /tmp.
cp -a /work/cl-eta-src /tmp/cl-eta
mkdir -p /tmp/cl-eta/runtime

# eta-hab.asd hardcodes the production path of binding-knx.asd
# (/home/manfred/quicklisp/local-projects/chipi/...) — satisfy it.
mkdir -p /home/manfred/quicklisp/local-projects
ln -sfn /work/cl-hab /home/manfred/quicklisp/local-projects/chipi

# eta-hab's defconfig binds api/ui to the production LAN address; alias it
# onto lo so the bind succeeds inside the container (needs NET_ADMIN).
ip addr add 192.168.50.43/32 dev lo 2>/dev/null \
    || echo "WARN: could not alias 192.168.50.43 (run with --cap-add NET_ADMIN); api/ui bind will fail"

# The item bindings pull from LAN devices (solar/shelly/fenecon at
# 192.168.50.x).  Through the container NAT those TCP connects hang in SYN
# retries for minutes and tie up ALL shared dispatcher workers — every
# actor interaction then queues ~60s and the load crawls.  Blackhole the
# production subnet so connects fail instantly (ENETUNREACH), but keep a
# real route to the KNX gateway so the tunnel connect is genuinely tried.
GW=$(ip route 2>/dev/null | awk '/^default/{print $3; exit}')
ip route add unreachable 192.168.50.0/24 2>/dev/null || true
[ -n "$GW" ] && ip route add 192.168.50.40/32 via "$GW" 2>/dev/null || true

# log4cl :daily appender writes logs/app.log relative to cwd.
mkdir -p /tmp/cl-amiga/logs

echo "=== [4/4] Running eta-boot (heap ${CLAMIGA_HEAP:-512M}) ==="
cd /tmp/cl-amiga
exec ./build/host/clamiga --heap "${CLAMIGA_HEAP:-512M}" \
    --load /cl-amiga/verify/linux-arm64/eta-boot.lisp
