# AmigaOS examples

Runnable programs for the AmigaOS / MorphOS build of CL-Amiga.  Each one
is a single file: `clamiga --load <file>` from the repository (or release)
root runs it.  The GUI ones need a 128K stack (`stack 131072`) on
AmigaOS, see the main README.

| Directory | What | Needs |
|-----------|------|-------|
| `gfx/bouncing-lines.lisp` | Bouncing colour-cycling lines in a window — the `AMIGA.INTUITION` / `AMIGA.GFX` curated bindings, an IDCMP event loop, FPS overlay | AmigaOS 3.1+ or MorphOS |
| `arexx/` | `clamiga.rexx`, a shell client for the `AMIGA.AREXX` development port, and a CygnusEd macro that saves and loads the current file | ARexx |
| `reaction/` | ReAction GUIs — ports of the NDK 3.2 `Examples/` programs (below) | AmigaOS 3.5+/3.2 or MorphOS (ReAction classes) |

## ReAction (`reaction/`)

Common Lisp ports of the ReAction examples shipped with the AmigaOS 3.2
NDK, written against the generated raw class modules
(`lib/amiga/raw/classes/window`, `gadgets/*`, `images/*`,
`classes/requester`) and the `AMIGA.REACTION` helpers
(`lib/amiga/reaction.lisp`: `DoMethod`, `RA_OpenWindow` /
`RA_HandleInput`, string pools, label lists).  Each file names the NDK
program it ports and what it demonstrates.

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

`make -f Makefile.cross examples-amiga` (= `verify/realamiga/run-reaction-examples.sh`)
runs all of them in FS-UAE (whose OS 3.9 Workbench has the classes),
photographs every window and converts the shots to PNG under
`build/amiga/shots/`.
