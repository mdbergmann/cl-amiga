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
reads the five words back.  `vN` selects one of 20 shapes of the store chain
(the table is in the source); `v1` is the default.

Healthy CPU (68000..68060, UAE):     `storeprobe: N rounds, 0 lost store(s) -- all stores landed`
Vampire V4 core 10760 (68080, 92 MHz): `storeprobe: N rounds, ~2N lost store(s) -- STORES LOST`

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

Caveat on the "fine" shapes: they are not stable across builds.  Variant 8
(the absolute source loaded into a register first) lost nothing in one build
of this program and ~68% of its stores in the next, with the same source
and flags -- the chain sat at a different code address.  So the defect also
depends on instruction placement, only the failure of gcc's own sequence
(`v1`) is a reliable signature, and a compile-time workaround chosen by
instruction shape cannot be trusted without the hardware in the loop.

What to run elsewhere, and why:

- a real 68030/040/060: expected clean -- shows the defect is the 68080's.
- another Vampire (V2, V4, a different core revision): the same, plus
  `v9`, `v20`, `v14`, `v8` -- tells whether a newer core fixes it.
- `CPU NODATACACHE` (3.x `CPU` command), then `storeprobe`, then `CPU DATACACHE`:
  tells whether the data cache / write buffer is the path (untested so far).
- Report to the Apollo team with: board (`ApolloControl BFN`), core
  (`ApolloControl CORE`), `ApolloControl CPU`, the verdict lines of v1, v5,
  v8, v9, v14, v20, and this file's source.

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
