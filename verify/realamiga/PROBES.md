# CPU / system probes for real Amigas

Small standalone m68k programs that test one machine behaviour each, built
for the hunt of a corruption that turned out to be a hardware defect of the
Apollo 68080 core (Vampire V4, core 10760, 2026-09-19).  Run them on any
Amiga you can reach; each prints a one-line verdict and exits with 0 (fine)
or 20 (defect seen).

Build all four on the host:

    make -f Makefile.cross probes        # -> build/cross/probes/{storeprobe,fpuregtest,stackprobe,regtest}

Copy the binaries to the Amiga any way you like (amiagent, FTP, SD card) and
run them from a Shell.  With an amiagent on the box and the amimcp client
on the host, `verify/realamiga/run-probes.py` does the upload and the runs:

    AMIGA_HOST=192.168.1.50 AMIGA_TOKEN=xyz verify/realamiga/run-probes.py

## storeprobe -- the one that matters

    storeprobe [SECONDS] [swap|noswap] [vN]

Replays, as one back-to-back instruction chain, the prologue gcc emits for a
C function that initialises five locals through a post-incremented pointer:

    move.l (a4),(a0)+ ; move.l 4(a4),(a0)+ ; move.l <absolute>,(a0)+ ; clr.l (a0)+ ; clr.l (a0)

into a pattern-filled buffer at every alignment mod 16, on the stack and in a
heap block, hundreds of thousands of times, with task switches mixed in, and
reads the five words back.  `vN` selects one of 21 shapes of the store chain
(the table is in the source); `v1` is the default.  `v21` freezes a
Vampire -- see below before running it.

Healthy CPU (68000..68060, UAE):     `storeprobe: N rounds, 0 lost store(s) -- all stores landed`
Vampire V4 core 10760 (68080, 92 MHz): `storeprobe: N rounds, ~2N lost store(s) -- STORES LOST` (on a launch whose source address loses; run it a few times)

On the Vampire the LAST store of that chain does not reach memory (or lands
late) in ~100% of replays.  The trigger, from the 20 variants: a
memory-to-memory MOVE whose source is an absolute address, followed by two
or more further stores nearby.  Loading the absolute source into a register
first, or indirect sources, are fine; a nop or an ALU op between the stores,
the displacement form (`clr.l 4(a0)`), a different address register, word
clears, all still lose it; superscalar off (`ApolloControl SS=0`) changes
nothing.  In clamiga this was bi_mismatch's `key_fn` local staying whatever
dead data the slot held before (an FPU context image, the caller's function
object), and every "Too few arguments to PREFIX-P" / "not a function" /
"keyword key is not a symbol" of the Clamacs editor suite on that machine.

**What decides it is the ADDRESS OF THE ABSOLUTE SOURCE** (found later the
same day with a scratch probe that ran the chain from 32 code offsets and
from 32 sources at consecutive addresses): the code address makes no
difference (all 32 copies lose), the destination makes no difference, but
of 16 sources at consecutive longword addresses exactly 8 lose every replay
and 8 never do -- with the source's address mod 64 deciding (on core 10760:
24, 32, 40 and 44..60 lose; 0..20, 28, 36 are fine).  That is why the "fine"
shapes flipped between builds and why one and the same clamiga binary
reported "4095 of 4096 lost" on one launch and 0 on the next: a program's
data hunk lands at a different address on every launch.  Variants 1..20
pass or fail per launch -- run them more than once.

**`v21` FREEZES THE MACHINE -- do not run it on a Vampire you cannot
reboot.**  It sweeps 16 absolute sources at consecutive addresses, one per
16 rounds, which would flag every launch; on core 10760 both it and an
equivalent C loop froze the box within seconds with the display "out of
range" (2026-09-19, twice, reboot needed each time): switching the source
address between replays makes the mis-executed store land outside the
target buffer, in hardware registers.  The placement probe that found the
address dependence switched sources only every 2000 rounds and survived
six runs.  It stays in the source as the record for the Apollo team.
clamiga's startup self-test (`src/platform/cpu_store_probe_m68k.s`) is
therefore the single-source chain and flags about every second launch on
that core.

`CPU NODATACACHE` changes nothing (2026-09-19: v1, v5 and v9 lose at
exactly the same rate with `DATA: NoCache NoBurst`), nor does
`ApolloControl SS=0`: the write path itself, not a cache or dual issue.

What to run elsewhere, and why:

- a real 68030/040/060: expected clean -- shows the defect is the 68080's.
- another Vampire (V2, V4, a different core revision): `v1` a few times
  (per launch!), then `v5`, `v9`, `v20`, `v14`, `v8` -- tells whether a
  newer core fixes it.  `v21` only if you can reboot the machine.
- Report to the Apollo team with: board (`ApolloControl BFN`), core
  (`ApolloControl CORE`), `ApolloControl CPU`, the verdict lines, the
  address dependence and the v21 freeze above, and this file's source.

## fpuregtest, regtest, stackprobe -- the ones that exonerated the rest

    fpuregtest [SECONDS] [swap|noswap] [fpu] [sin] [cr]
    regtest    [SECONDS] [swap|noswap] [chip|reverse|clear]
    stackprobe [SECONDS] [swap|noswap] [fpu] [io] [chip|reverse|clear]

They keep constants in the callee-saved registers d2-d7/a2-a4 (regtest,
fpuregtest -- the latter with FPU state alive, including trapped `fsin` /
`fmovecr`) or a 16K sentinel array in a live stack frame (stackprobe) across
thousands of task switches (Delay + timer interrupts), on the Shell's stack
or on a 128K block reached through exec StackSwap, and report the first
change.  All three are clean on the Vampire: exec's context save/restore,
the FPU frames and the stack swap are not involved in the defect above.  On
a new machine they are a 30-second sanity check of the same paths.
