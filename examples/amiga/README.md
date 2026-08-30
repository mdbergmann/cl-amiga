# AmigaOS examples

Runnable programs for the AmigaOS / MorphOS build of CL-Amiga.  Each one
is a single file: `clamiga --load <file>` from the repository (or release)
root runs it.  The GUI ones need a 128K stack (`stack 131072`) on
AmigaOS, see the main README.

| Directory | What | Needs |
|-----------|------|-------|
| `gfx/bouncing-lines.lisp` | Bouncing colour-cycling lines in a window — the `AMIGA.INTUITION` / `AMIGA.GFX` curated bindings, an IDCMP event loop, FPS overlay | AmigaOS 3.1+ or MorphOS |
| `gfx/doublebuffer.lisp` | The NDK 3.1 `intuition/doublebuffer.c` example — a face bouncing at the frame rate on a HIRES custom screen through two `ScreenBuffer`s (`AllocScreenBuffer` / `ChangeScreenBuffer`, the `dbi_SafeMessage` reply protocol, the held-off `WaitTOF` retry), an attached control screen (`SA_Parent`) with GadTools sliders, `LendMenus`, two windows on one IDCMP port | AmigaOS 3.0+ (V39) |
| `gfx/sprite.lisp` | The RKM "Simple Sprite" example (`ssprite.c`) — a hardware sprite: `GetSprite` / `ChangeSprite` / `MoveSprite` / `FreeSprite`, the sprite colour registers via `SetRGB4`, image data in CHIP memory, sprite DMA off/on through a custom-chip register write | AmigaOS 3+ on a native chipset (real or FS-UAE) |
| `arexx/` | `clamiga.rexx`, a shell client for the `AMIGA.AREXX` development port, and a CygnusEd macro that saves and loads the current file | ARexx |
| `reaction/` | ReAction GUIs — ports of the NDK 3.2 `Examples/` programs (below) | AmigaOS 3.5+/3.2 or MorphOS (ReAction classes) |
| `asyncio/copyfile.lisp` | Double-buffered asynchronous file copy over DOS packets — the `AMIGA.ASYNCIO` port of the NDK 3.1 AsynchIO package, timed against plain synchronous streams and byte-verified | AmigaOS 3.1+ or MorphOS |
| `iff/sift.lisp` | The NDK 3.1 `sift` program as Lisp — `AMIGA.IFF` over iffparse.library builds a nested IFF with back-patched chunk sizes, prints its IFFCheck-like listing (files or the clipboard, the C's `-c`), reads chunk data back | AmigaOS 3.1+ or MorphOS |

## ReAction (`reaction/`)

Common Lisp ports of the ReAction examples shipped with the AmigaOS 3.2
NDK, written against the generated raw class modules
(`lib/amiga/raw/classes/window`, `gadgets/*`, `images/*`,
`classes/requester`) and the `AMIGA.REACTION` helpers
(`lib/amiga/reaction.lisp`: `RA_OpenWindow` / `RA_HandleInput`, object
creation, over `lib/amiga/boopsi.lisp`'s `DoMethod`, string pools and
label lists, which it re-exports).  Each file names the NDK program it
ports and what it demonstrates.

| Example | NDK source | Shows |
|---------|-----------|-------|
| `buttons.lisp` | `Buttons.c` | window.class + layout.gadget + 100 button.gadget objects, built in a loop; the basic event loop |
| `checkbox.lisp` | `CheckBox.c` | checkbox.gadget, label.image in a bevelled group, an iconifiable window (`WINDOW_AppPort`, `WMHI_ICONIFY` / `WMHI_UNICONIFY`) |
| `chooser.lisp` | `Chooser1.c` | chooser.gadget over a node list (`new-list`, `alloc-chooser-node-a`), `CHILD_Label` |
| `clicktab.lisp` | `ClickTab.c` | clicktab.gadget tabs switching a page.gadget (`PAGE_Current` + `RethinkLayout`) |
| `fuelgauge.lisp` | `FuelGauge.c` | fuelgauge.gadget animated from the program with `set-gadget-attrs`, busy pointer via `set-attrs` |
| `integer.lisp` | `Integer.c` | integer.gadget with/without arrows, `GA_TabCycle`, `ActivateLayoutGadget`, reading `INTEGER_Number` back |
| `listbrowser.lisp` | `ListBrowser1.c` | listbrowser.gadget with three columns (`struct ColumnInfo` built by hand), node attributes read back, multi-select / auto-fit toggled live |
| `requester.lisp` | `Requester.c` | requester.class: info, multi-button, string (with chooser presets) and integer requesters through `open-requester` |

Run one interactively:

```
clamiga --load examples/amiga/reaction/listbrowser.lisp
```

Run one unattended — the window closes itself after the timeout (this
is how the examples are smoke-tested):

```
clamiga --eval '(require "amiga/reaction")' \
        --eval '(setf amiga.reaction:*event-loop-timeout* 5)' \
        --load examples/amiga/reaction/listbrowser.lisp
```

On a system without the ReAction classes (the host, a bare AmigaOS 3.1)
each example loads and says so instead of failing.

## Graphics (`gfx/`)

`bouncing-lines.lisp` uses the curated `AMIGA.INTUITION` / `AMIGA.GFX`
bindings; `doublebuffer.lisp` and `sprite.lisp` are ports of the NDK 3.1
and RKM Companion C examples written against the generated raw bindings
(`AMIGA.RAW.GRAPHICS`, `AMIGA.RAW.INTUITION`, `AMIGA.RAW.EXEC`, plus the
GadTools helpers) and keep the C's structure — each file names its
source and what it demonstrates.  Both open their own custom screen;
the sprite one has to, because a hardware sprite is invisible over an
RTG screen.  `run` returns a plist of what happened (frames swapped,
sprite number and final position), which is what the test suite checks.

Run one unattended — its loop returns after the timeout:

```
clamiga --eval '(require "amiga/intuition")' \
        --eval '(setf amiga.intuition:*event-loop-timeout* 5)' \
        --load examples/amiga/gfx/doublebuffer.lisp
```

`make -f Makefile.cross examples-amiga` (= `verify/realamiga/run-examples.sh`)
runs all the GUI examples — `gfx/` and `reaction/` — in FS-UAE (whose OS
3.9 Workbench has the ReAction classes), photographs every new window on
the Workbench screen and every custom screen, and converts the shots to
PNG under `build/amiga/shots/`.
