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
| `mui/` | MUI GUIs — the `AMIGA.MUI` helpers over `muimaster.library` (below) | AmigaOS 3.x with MUI 3.8+, or MorphOS |
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

## MUI (`mui/`)

Common Lisp MUI programs written against the generated raw module
(`lib/amiga/raw/muimaster`: the 26 `muimaster.library` functions and
every `MUIA_` / `MUIM_` / `MUIV_` / `MUIC_` constant of `libraries/mui.h`)
and the `AMIGA.MUI` helpers (`lib/amiga/mui.lisp`: objects by class name,
`MUI_MakeObject`, the `MUIM_Notify` idiom, the `MUIM_Application_NewInput`
loop, over `lib/amiga/boopsi.lisp`'s `DoMethod` and string pools, which
it re-exports).  Most are ports of the demos in the MUI 3.8 developer
kit (`MUI:Developer/C/Examples/`); the kit's `Layout.c` and
`Slidorama.c` are hook and custom-class demos, which Lisp cannot write
on the target yet, so `layout` and `slidorama` are hook-free programs
on the same subjects.  Each file names its source and what it
demonstrates.

| Example | SDK source | Shows |
|---------|-----------|-------|
| `hello.lisp` | — | The minimal MUI application: Application / Window / Group / Text / `MUI_MakeObject` button, `MUIM_Notify` on `MUIA_Window_CloseRequest` and `MUIA_Pressed`, `MUIA_Window_Open`, the event loop |
| `layout.lisp` | — | What the group layout does with its attributes: `MUIA_Group_Horiz` / `_Columns` / `_SameSize`, `MUIA_Weight`, every `MUIV_Frame_*`, the `MUII_*` backgrounds, `MUIA_FrameTitle` |
| `balancing.lisp` | `Balancing.c` | `Balance.mui` objects between weighted rectangles, buttons and labels; `MUIA_ObjectID`; `MUIV_Window_Width_Screen(50)` via `window-size-screen` |
| `pages.lisp` | `Pages.c` | `Register.mui` tabs over four pages of string / cycle / radio / checkmark / slider objects, plus a `MUIA_Group_PageMode` group switched by a cycle through `notify` with `MUIV_TriggerValue` |
| `menus.lisp` | `Menus.c` | A menu strip built from `Menustrip` / `Menu` / `Menuitem` objects (shortcuts, checkmark, toggle and radio items, separators), menu selections as return IDs, `MUIA_Application_MenuAction`, `MUIM_FindUData` / `SetUData` / `GetUData`, a menu inserted and removed at run time, `MUI_Request` |
| `showhide.lisp` | `ShowHide.c` | Checkmarks that show and hide buttons through `MUIA_ShowMe` with `MUIV_TriggerValue`; buttons added and removed live with `MUIM_Group_InitChange` / `OM_ADDMEMBER` / `OM_REMMEMBER` / `MUIM_Group_ExitChange` |
| `slidorama.lisp` | (`Slidorama.c`) | Knobs, sliders, numeric buttons, a gauge with its scale and a levelmeter; `MUIA_Numeric_Format`; a slider driving the gauge and the levelmeter by notification |
| `virtual.lisp` | `Virtual.c` | `Scrollgroup.mui` / `Virtgroup.mui` viewports: a long text with the window's border scrollers, the `MUII_*` images and backgrounds, a list filled by `MUIM_List_Insert`, a virtual group inside a virtual group scrolled by arrow buttons, `MUIM_Window_SetCycleChain` |
| `requester.lisp` | — | `MUI_Request`: an information requester, a yes/no question, a three-way choice with a default, and one formatted with `%ld` / `%s` parameters |

Run one interactively:

```
clamiga --load examples/amiga/mui/hello.lisp
```

Run one unattended — the window closes itself after the timeout:

```
clamiga --eval '(require "amiga/mui")' \
        --eval '(setf amiga.mui:*event-loop-timeout* 5)' \
        --load examples/amiga/mui/hello.lisp
```

On a system without MUI (the host, an Amiga without `muimaster.library`)
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
runs all the GUI examples — `gfx/`, `reaction/` and `mui/` — in FS-UAE (whose
OS 3.9 Workbench has the ReAction classes and MUI 3.8), photographs every new window on
the Workbench screen and every custom screen, and converts the shots to
PNG under `build/amiga/shots/`.
