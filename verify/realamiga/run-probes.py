#!/usr/bin/env python3
"""run-probes.py [PROBE ARGS ...] -- upload the cross-built CPU probes
(build/cross/probes/*, see PROBES.md) to a real Amiga through amiagent and
run them, printing each verdict line.

With no arguments runs the standard set: storeprobe v1 (three launches,
since v1..v20 pass or fail per launch -- PROBES.md) v5 v8 v9 v14 v20,
fpuregtest fpu/sin, regtest, stackprobe.  Otherwise each argument is one
run, e.g. "storeprobe 5 noswap v9".  storeprobe v21 FREEZES a Vampire and
is never run by default; its per-source table is printed when asked for.

Environment: AMIGA_HOST (default 192.168.50.235), AMIGA_TOKEN (default
"vamp"), AMIGA_DIR (drawer for the binaries, default T:), AMIMCP (path of
the amimcp client's server directory, default
~/Development/MySources/amimcp/server)."""
import os, sys
sys.path.insert(0, os.path.expanduser(os.environ.get("AMIMCP", "~/Development/MySources/amimcp/server")))
try:
    from amiga import Amiga
except ImportError:
    sys.exit("amimcp client not found: set AMIMCP to its server directory, or copy "
             "build/cross/probes/* to the Amiga by hand and run them from a Shell "
             "(verify/realamiga/PROBES.md)")

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PROBES = os.path.join(ROOT, "build", "cross", "probes")
HOST = os.environ.get("AMIGA_HOST", "192.168.50.235")
TOKEN = os.environ.get("AMIGA_TOKEN", "vamp")
DIR = os.environ.get("AMIGA_DIR", "T:")
if DIR and not DIR.endswith((":", "/")):
    DIR += "/"  # a drawer ("Work:Download") needs the separator; a volume or assign ("T:") has it

DEFAULT = ["storeprobe 3 noswap v1", "storeprobe 3 noswap v1", "storeprobe 3 noswap v1",
           "storeprobe 3 noswap v5", "storeprobe 3 noswap v8",
           "storeprobe 3 noswap v9", "storeprobe 3 noswap v14", "storeprobe 3 noswap v20",
           "fpuregtest 5 noswap fpu", "fpuregtest 5 noswap sin",
           "regtest 5 noswap", "stackprobe 5 noswap fpu"]

runs = sys.argv[1:] or DEFAULT
a = Amiga(HOST, token=TOKEN)
info = a.system_info()
print(f"# {HOST}: {info}")
for name in sorted({r.split()[0] for r in runs}):
    path = os.path.join(PROBES, name)
    if not os.path.exists(path):
        sys.exit(f"{path} missing: make -f Makefile.cross probes")
    a.write_file(f"{DIR}{name}", open(path, "rb").read())
    a.exec_command(f"Protect {DIR}{name} +e", timeout=20)
for r in runs:
    rc, out = a.exec_command(f"{DIR}{r}", timeout=300)
    verdict = [l for l in out.splitlines() if l.startswith(r.split()[0] + ":")]
    print(f"{r:32s} rc={rc:<3} {verdict[-1] if verdict else out.strip()[-200:]}", flush=True)
    if r.endswith("v21"):
        table = out.split("source address mod 64", 1)
        if len(table) == 2:
            print("  source address mod 64" + table[1].split("storeprobe:")[0].rstrip(), flush=True)
