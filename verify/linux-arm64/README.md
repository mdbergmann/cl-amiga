# Linux arm64 (Debian Bookworm) verification

Builds clamiga inside a Debian Bookworm arm64 container and loads the
real-world `eta-hab` application (chipi + chipi-api + chipi-ui/clog +
binding-knx + cl-eta + slynk) end to end — the same stack that runs in
production on SBCL/Raspberry Pi.

```
verify/linux-arm64/run.sh                 # build + load-verify, then exit
ETA_STAY=1 verify/linux-arm64/run.sh      # keep running, slynk on :4005
CLAMIGA_HEAP=768M verify/linux-arm64/run.sh
```

## How it works

- `Dockerfile` — debian:bookworm with build-essential (clamiga's host
  build needs nothing else; FFI is dlopen-based), libsqlite3 for clog,
  tzdata, iproute2.
- `run.sh` (host side) — builds the image and mounts:
  - this repo read-only at `/cl-amiga`
  - `~/quicklisp` read-write at `/root/quicklisp` (dist releases and the
    cl-amiga library forks in local-projects are shared with the host)
  - the local project checkouts (`cl-hab`, `knx-con`, `cl-gserver`,
    `cl-eta`, the SLY fork) read-only under `/work`
- `container-entry.sh` — copies the sources to `/tmp/cl-amiga` (the repo
  mount is read-only), runs `make host`, recreates the developer host's
  `~/common-lisp` .asd symlinks, satisfies eta-hab.asd's hardcoded
  `/home/manfred/quicklisp/local-projects/chipi` path, and aliases
  `192.168.50.43` onto `lo` so eta-hab's api/ui bind succeeds
  (`--cap-add NET_ADMIN`).
- `eta-boot.lisp` — clamiga adaptation of cl-eta's `eta-boot.lisp`; see
  its header comment for the exact differences.

## Expected environment-dependent failures

- **KNX**: the gateway (`192.168.50.40`) usually has its tunnel slot
  taken by the production instance; the connect then fails or times out.
- **openhab/influx hosts** (`mini.local`, `picellar`) don't resolve from
  the container; runtime pushes log errors but don't block the load.
- **serial/pigpio**: no ETA serial port or pigpio daemon in the
  container; the boot rule wraps these in `ignore-errors`.
